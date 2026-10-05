#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-only upstream parser, asset integrity and failure-reporting tests."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from libc_build import LOCALE_FILES, RUNTIMES, VARIANTS, compile_command, digest, runtime_asset_names
from libc_guest import collect_guest, install_assets
from upstream_suite import (
    IDENTITY, LAUNCHER, RUN_AS, SOURCE_COMMIT, TLS_DSO, TLS_TEST,
    manifest_selection, selection,
)

spec = importlib.util.spec_from_file_location("run_upstream", Path(__file__).with_name("run-upstream.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
spec = importlib.util.spec_from_file_location(
    "build_upstream", Path(__file__).with_name("build-upstream.py"))
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
TESTS = ["clock_gettime", "strtod"]


def functional_manifest():
    return {"schema": 1, "kind": "upstream-functional",
            "variants": list(VARIANTS), "tests": TESTS}


def regression_manifest(tests=None):
    tests = ["malloc-0", TLS_TEST] if tests is None else tests
    variants, helpers, excluded = selection("regression", tests)
    return {"schema": 2, "kind": "upstream-regression",
            "source_commit": SOURCE_COMMIT, "variants": list(VARIANTS),
            "tests": tests, "variant_tests": variants,
            "support_assets": helpers,
            "excluded_cases": excluded, "run_as": RUN_AS}


def transcript(failing=False):
    lines = ["Linux boot output", "UPSTREAM_SYSTEM_BEGIN",
             "UPSTREAM_CASE_TIMEOUT 30"]
    for variant in VARIANTS:
        for name in TESTS:
            failed = failing and variant == "musl-static" and name == "strtod"
            lines += [f"UPSTREAM_BEGIN {variant} {name}",
                      "test diagnostic" if failed else "",
                      f"UPSTREAM_END {variant} {name} {int(failed)}"]
    return "\n".join(lines + [f"UPSTREAM_DONE {int(failing)}", ""])


class ValidationTests(unittest.TestCase):
    def reject(self, text):
        with self.assertRaises(ValueError):
            runner.validate(text, TESTS)

    def test_complete(self):
        self.assertEqual(len(runner.validate(transcript(), TESTS)), 8)

    def test_crlf(self):
        self.assertEqual(len(runner.validate(transcript().replace("\n", "\r\n"), TESTS)), 8)

    def test_raw_failure_is_preserved(self):
        results = runner.validate(transcript(True), TESTS)
        self.assertEqual(results[-1]["status"], 1)
        self.assertEqual(results[-1]["output"], "test diagnostic")
        report = runner.report(results, False)
        self.assertEqual((report["passed"], report["failures"], report["exit_status"]), (7, 1, 1))

    def test_qemu_diagnostics_are_not_pass(self):
        self.assertEqual(runner.report(runner.validate(transcript(), TESTS), True)["exit_status"], 2)

    def test_missing_case(self):
        self.reject(transcript().replace("UPSTREAM_END glibc-time32 clock_gettime 0\n", ""))

    def test_duplicate_case(self):
        self.reject(transcript().replace("UPSTREAM_BEGIN glibc-time32 strtod",
                                         "UPSTREAM_BEGIN glibc-time32 clock_gettime"))

    def test_mismatched_end(self):
        self.reject(transcript().replace("UPSTREAM_END glibc-time32 strtod",
                                         "UPSTREAM_END glibc-time64 strtod"))

    def test_nested_begin(self):
        self.reject(transcript().replace("UPSTREAM_BEGIN glibc-time32 strtod",
                                         "UPSTREAM_BEGIN glibc-time32 strtod\nUPSTREAM_BEGIN glibc-time32 strtod"))

    def test_bad_status(self):
        for status in ("-1", "256", "bad"):
            self.reject(transcript().replace("UPSTREAM_END glibc-time32 strtod 0",
                                             f"UPSTREAM_END glibc-time32 strtod {status}"))

    def test_false_pass_marker(self):
        self.reject(transcript(True).replace("UPSTREAM_DONE 1", "UPSTREAM_DONE 0"))

    def test_false_failure_marker(self):
        self.reject(transcript().replace("UPSTREAM_DONE 0", "UPSTREAM_DONE 1"))

    def test_duplicate_final_marker(self):
        self.reject(transcript() + "UPSTREAM_DONE 0\n")

    def test_missing_final_marker(self):
        self.reject(transcript().replace("UPSTREAM_DONE 0\n", ""))

    def test_guest_setup_error(self):
        self.reject("UPSTREAM_ERROR guest-setup\nUPSTREAM_DONE 1\n")

    def test_system_marker_required(self):
        self.reject(transcript().replace("UPSTREAM_SYSTEM_BEGIN\n", ""))

    def test_requested_timeout_is_required(self):
        results = runner.validate(transcript(), TESTS, case_timeout=30)
        self.assertEqual(len(results), 8)
        with self.assertRaises(ValueError):
            text = transcript().replace("UPSTREAM_CASE_TIMEOUT 30\n", "")
            runner.validate(text,
                            TESTS, case_timeout=30)

    def test_requested_timeout_must_match(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            runner.validate(transcript(), TESTS, case_timeout=120)

    def test_timeout_marker_cannot_be_duplicated_or_forged_inside_a_case(self):
        self.reject(transcript().replace("UPSTREAM_CASE_TIMEOUT 30",
                                         "UPSTREAM_CASE_TIMEOUT 30\n"
                                         "UPSTREAM_CASE_TIMEOUT 30"))
        self.reject(transcript().replace("UPSTREAM_BEGIN glibc-time32 strtod\n",
                                         "UPSTREAM_BEGIN glibc-time32 strtod\n"
                                         "UPSTREAM_CASE_TIMEOUT 30\n"))

    def test_bad_timeout_marker(self):
        for timeout in ("-1", "0", "3601", "nan", "30 30"):
            text = transcript().replace("UPSTREAM_CASE_TIMEOUT 30",
                                        f"UPSTREAM_CASE_TIMEOUT {timeout}")
            self.reject(text)

    def test_unprivileged_identity_is_required_for_each_case(self):
        text = transcript().replace("UPSTREAM_END ",
                                    IDENTITY + "\nUPSTREAM_END ")
        results = runner.validate(text, TESTS, unprivileged=True)
        self.assertEqual(len(results), 8)
        with self.assertRaises(ValueError):
            runner.validate(transcript(), TESTS, unprivileged=True)
        with self.assertRaises(ValueError):
            runner.validate(text.replace(IDENTITY + "\n", "", 1), TESTS,
                            unprivileged=True)

    def test_wrong_or_duplicate_identity_cannot_pass(self):
        identity = IDENTITY + "\n"
        text = transcript().replace("UPSTREAM_END ", identity + "UPSTREAM_END ")
        for changed in (text.replace("uid=1000", "uid=0"),
                        text.replace("groups=0", "groups=1"),
                        text.replace("no_new_privs=1", "no_new_privs=0"),
                        text.replace(identity, identity + identity, 1)):
            with self.assertRaises(ValueError):
                runner.validate(changed, TESTS, unprivileged=True)

    def test_identity_outside_a_case_is_rejected(self):
        with self.assertRaises(ValueError):
            runner.validate(IDENTITY + "\n" + transcript(), TESTS,
                            unprivileged=True)

    def test_privilege_setup_failure_is_not_a_raw_library_failure(self):
        marker = "UPSTREAM_END glibc-time32 clock_gettime 0"
        error = ("UPSTREAM_ERROR unprivileged-launcher: "
                 "Permission denied")
        self.reject(transcript().replace(marker, error))

    def test_unsafe_manifest_names(self):
        for names in (["../strtod"], ["strtod;poweroff"], ["runtest"], ["strtod", "strtod"], []):
            with self.assertRaises(ValueError):
                runner.manifest_tests({"schema": 1, "kind": "upstream-functional",
                                       "variants": list(VARIANTS), "tests": names})


class SuiteTests(unittest.TestCase):
    def test_legacy_functional_manifest(self):
        result = manifest_selection(functional_manifest())
        suite, tests, variants, helpers, excluded = result
        self.assertEqual((suite, tests, helpers, excluded),
                         ("functional", TESTS, [], []))
        self.assertEqual(variants, {variant: TESTS for variant in VARIANTS})

    def test_dynamic_only_rule(self):
        manifest = regression_manifest()
        _, tests, variants, helpers, excluded = manifest_selection(manifest)
        self.assertEqual(sum(map(len, variants.values())), 7)
        self.assertEqual(variants["musl-static"], ["malloc-0"])
        self.assertEqual(len(helpers), 4)
        self.assertNotIn("musl-static/" + TLS_DSO, helpers)
        self.assertEqual(excluded[0]["test"], TLS_TEST)

    def test_no_dynamic_test_has_no_dso_or_exclusion(self):
        manifest = regression_manifest(["malloc-0"])
        _, _, variants, helpers, excluded = manifest_selection(manifest)
        self.assertEqual((helpers, excluded), ([LAUNCHER], []))
        self.assertTrue(all(names == ["malloc-0"]
                            for names in variants.values()))

    def test_manifest_must_be_an_object(self):
        for manifest in (None, [], 1, "manifest"):
            with self.assertRaises(ValueError):
                manifest_selection(manifest)

    def test_manifest_cannot_skip_an_ordinary_regression(self):
        manifest = regression_manifest()
        manifest["variant_tests"]["glibc-time32"].remove("malloc-0")
        with self.assertRaises(ValueError):
            manifest_selection(manifest)

    def test_manifest_cannot_run_static_dlopen(self):
        manifest = regression_manifest()
        manifest["variant_tests"]["musl-static"].append(TLS_TEST)
        with self.assertRaises(ValueError):
            manifest_selection(manifest)

    def test_manifest_cannot_hide_exclusion(self):
        manifest = regression_manifest()
        manifest["excluded_cases"] = []
        with self.assertRaises(ValueError):
            manifest_selection(manifest)

    def test_root_or_missing_identity_contract_is_rejected(self):
        for run_as in (None, {**RUN_AS, "uid": 0}, {**RUN_AS, "groups": [0]},
                       {**RUN_AS, "no_new_privs": False}):
            manifest = regression_manifest()
            manifest["run_as"] = run_as
            with self.assertRaises(ValueError):
                manifest_selection(manifest)

    def test_manifest_cannot_drop_helper(self):
        manifest = regression_manifest()
        manifest["support_assets"].pop()
        with self.assertRaises(ValueError):
            manifest_selection(manifest)

    def test_manifest_requires_pinned_source(self):
        manifest = regression_manifest()
        manifest["source_commit"] = "0" * 40
        with self.assertRaises(ValueError):
            manifest_selection(manifest)

    def test_helper_is_not_an_executable_case(self):
        for name in ("tls_get_new-dtv_dso", "tls_get_new-dtv_dso.so"):
            with self.assertRaises(ValueError):
                selection("regression", [name])

    def test_dynamic_only_results_require_every_eligible_case(self):
        manifest = regression_manifest()
        variants = manifest["variant_tests"]
        lines = ["UPSTREAM_SYSTEM_BEGIN"]
        for variant in VARIANTS:
            for name in variants[variant]:
                lines += [f"UPSTREAM_BEGIN {variant} {name}",
                          f"UPSTREAM_END {variant} {name} 0"]
        text = "\n".join([*lines, "UPSTREAM_DONE 0", ""])
        results = runner.validate(text, manifest["tests"],
                                  variant_tests=variants)
        self.assertEqual(len(results), 7)
        with self.assertRaises(ValueError):
            changed = text.replace(
                f"UPSTREAM_END musl-shared {TLS_TEST} 0\n", "")
            runner.validate(changed, manifest["tests"], variant_tests=variants)

    def test_shared_object_flags_do_not_include_executable_startup(self):
        args = SimpleNamespace(cc="clang", glibc=Path("glibc"),
                               musl=Path("musl"),
                               gcc_install=Path("/tmp"))
        for variant in VARIANTS[:-1]:
            command = compile_command(args, variant, [Path("module.c")],
                                      Path("module.so"),
                                      shared=True)
            self.assertIn("-shared", command)
            self.assertIn("-fPIC", command)
            self.assertIn("-DSHARED", command)
            self.assertNotIn("musl/lib/crt1.o", command)
            self.assertNotIn("-no-pie", command)
            self.assertFalse(any("--dynamic-linker" in arg for arg in command))
        with self.assertRaises(ValueError):
            compile_command(args, "musl-static", [], Path("module.so"),
                            shared=True)


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(
            prefix="sam9x75-upstream-build-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.directory = self.source / "src/regression"
        self.directory.mkdir(parents=True)
        for name in ("malloc-0", "sigreturn", TLS_TEST, Path(TLS_DSO).stem):
            (self.directory / f"{name}.c").write_text("fixture source\n")
        rule = self.directory / f"{TLS_TEST}.mk"
        rule.write_text("fixture dynamic-only rule\n")
        self.args = SimpleNamespace(source=self.source, suite="regression",
                                    tests=None,
                                    output=self.root / "output", cc="clang",
                                    glibc=self.root, musl=self.root,
                                    gcc_install=self.root)

    def build(self):
        def check_output(command, **kwargs):
            if command[:2] == ["git", "rev-parse"]:
                return SOURCE_COMMIT + "\n"
            if command[:2] == ["git", "status"]:
                return ""
            if command[:2] == ["git", "ls-files"]:
                return b""
            return "fixture compiler\n"

        def run(command, **kwargs):
            destination = Path(command[command.index("-o") + 1])
            destination.write_bytes(b"fixture binary")

        with patch.object(builder.subprocess, "check_output",
                          side_effect=check_output), \
                patch.object(builder, "copy_runtimes", return_value={}), \
                patch.object(builder.subprocess, "run", side_effect=run), \
                patch("builtins.print"):
            builder.build(self.args)
        return json.loads((self.args.output / "manifest.json").read_text())

    def test_regression_default_discovers_all_tests_but_not_the_helper(self):
        manifest = self.build()
        self.assertEqual(manifest["tests"], ["malloc-0", "sigreturn", TLS_TEST])
        self.assertEqual(len(manifest["commands"]), 19)
        self.assertEqual(len(manifest["sha256"]), 19)
        manifest_selection(manifest)

    def test_dso_and_rpath_follow_upstream_rules(self):
        manifest = self.build()
        for item in manifest["commands"]:
            if item["test"] == TLS_TEST:
                self.assertIn("-Wl,-rpath,$ORIGIN", item["argv"])
                self.assertNotEqual(item["variant"], "musl-static")
            elif item["test"] == TLS_DSO:
                self.assertIn("-shared", item["argv"])
                self.assertNotEqual(item["variant"], "musl-static")

    def test_resource_exhaustion_helpers_are_linked(self):
        manifest = self.build()
        for item in manifest["commands"]:
            if item["test"] not in (TLS_DSO, "run-unprivileged"):
                for name in ("fdfill.c", "memfill.c", "vmfill.c"):
                    source = self.source / "src/common" / name
                    self.assertIn(str(source), item["argv"])

    def test_guest_launcher_is_a_strict_static_build_and_hashed(self):
        manifest = self.build()
        command = manifest["commands"][0]
        self.assertEqual(command["test"], "run-unprivileged")
        self.assertIn("-static", command["argv"])
        self.assertIn("-Werror", command["argv"])
        source = Path(builder.__file__).with_name("run-unprivileged.c")
        self.assertEqual(manifest["launcher_source_sha256"], digest(source))
        self.assertIn(LAUNCHER, manifest["sha256"])

    def test_unsupported_build_rule_is_refused_before_output(self):
        rule = self.directory / "sigreturn.mk"
        rule.write_text("fixture unsupported rule\n")
        with self.assertRaisesRegex(ValueError, "additional upstream build"):
            self.build()
        self.assertFalse(self.args.output.exists())

    def test_source_helper_cannot_be_selected_as_a_program(self):
        self.args.tests = [Path(TLS_DSO).stem]
        with self.assertRaises(ValueError):
            self.build()
        self.assertFalse(self.args.output.exists())

    def test_missing_test_is_refused_before_output(self):
        self.args.tests = ["nonexistent"]
        with self.assertRaisesRegex(ValueError, "test does not exist"):
            self.build()
        self.assertFalse(self.args.output.exists())


class AssetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sam9x75-libc-test-", dir="/tmp")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        self.guest = self.root / "guest"
        self.guest.mkdir()
        self.hashes = {}
        for name in RUNTIMES:
            path = self.assets / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture runtime")
            self.hashes[name] = digest(path)

    def test_valid_assets_and_loaders(self):
        install_assets(self.assets, self.guest, self.hashes, RUNTIMES)
        self.assertEqual((self.guest / "lib/ld-linux.so.3").read_bytes(), b"fixture runtime")

    def test_missing_hash(self):
        self.hashes.pop(RUNTIMES[0])
        with self.assertRaisesRegex(ValueError, "missing or unexpected"):
            install_assets(self.assets, self.guest, self.hashes, RUNTIMES)

    def test_changed_asset(self):
        (self.assets / RUNTIMES[0]).write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            install_assets(self.assets, self.guest, self.hashes, RUNTIMES)

    def test_partial_locale_data_is_rejected(self):
        self.hashes[LOCALE_FILES[0]] = "0" * 64
        with self.assertRaisesRegex(ValueError, "missing or unexpected"):
            install_assets(self.assets, self.guest, self.hashes, runtime_asset_names(self.hashes))

    def test_extra_unhashed_binary(self):
        with self.assertRaisesRegex(ValueError, "missing or unexpected"):
            install_assets(self.assets, self.guest, self.hashes, [*RUNTIMES, "glibc-time32/strtod"])

    def test_asset_symlink_outside_build(self):
        outside = self.root / "outside"
        outside.write_bytes(b"fixture runtime")
        path = self.assets / RUNTIMES[0]
        path.unlink()
        path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "escapes build"):
            install_assets(self.assets, self.guest, self.hashes, RUNTIMES)

    def test_compiler_abi_flags(self):
        args = SimpleNamespace(cc="clang", glibc=self.assets, musl=self.assets,
                               gcc_install=self.assets)
        for variant in VARIANTS:
            command = compile_command(args, variant, [self.root / "case.c"], self.root / "case")
            self.assertIn("-march=armv5te", command)
            self.assertIn("-mfloat-abi=soft", command)
            self.assertIn("-fno-builtin", command)
            self.assertEqual("-D_TIME_BITS=64" in command, variant == "glibc-time64")
            self.assertEqual("-static" in command, variant == "musl-static")

    def regression_assets(self):
        manifest = regression_manifest()
        for variant in VARIANTS:
            for name in [*manifest["variant_tests"][variant], "runtest"]:
                path = self.assets / variant / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture test")
                self.hashes[f"{variant}/{name}"] = digest(path)
        for name in manifest["support_assets"]:
            path = self.assets / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture shared object")
            self.hashes[name] = digest(path)
        manifest["sha256"] = self.hashes
        (self.assets / "manifest.json").write_text(json.dumps(manifest))
        return manifest

    def test_regression_overlay_checks_cases_and_timeout(self):
        manifest = self.regression_assets()
        args = self.guest_args()
        args.case_timeout = 120
        with patch.object(runner, "overlay_initramfs",
                          return_value=args.initramfs):
            runner.build_initramfs(args, self.guest)
        inner = (self.guest / "init").read_text()
        self.assertIn("echo UPSTREAM_CASE_TIMEOUT 120", inner)
        self.assertEqual(inner.count("/runtest -t 120 "), 7)
        self.assertEqual(inner.count("echo UPSTREAM_BEGIN "), 7)
        self.assertEqual(inner.count(f"/tests/{LAUNCHER} /tests/"), 7)
        self.assertIn("size=32m,mode=1777", inner)
        self.assertIn("size=16m,mode=1777", inner)
        self.assertNotIn(f"/tests/musl-static/{TLS_TEST}", inner)
        for name in manifest["support_assets"]:
            path = self.guest / "tests" / name
            self.assertEqual(path.read_bytes(), b"fixture shared object")

    def test_regression_helper_must_be_hashed(self):
        manifest = self.regression_assets()
        del manifest["sha256"][manifest["support_assets"][0]]
        (self.assets / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "missing or unexpected"):
            runner.build_initramfs(self.guest_args(), self.guest)

    def test_regression_helper_checksum_must_match(self):
        manifest = self.regression_assets()
        (self.assets / manifest["support_assets"][0]).write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            runner.build_initramfs(self.guest_args(), self.guest)

    def test_bad_asset_hash_map_writes_incomplete_results(self):
        for index, hashes in enumerate((None, [], "hashes")):
            manifest = functional_manifest()
            manifest["sha256"] = hashes
            (self.assets / "manifest.json").write_text(json.dumps(manifest))
            args = self.guest_args()
            args.output = self.root / f"invalid-result-{index}"
            with patch("builtins.print"):
                self.assertEqual(runner.run(args), 2)
            result = json.loads((args.output / "results.json").read_text())
            self.assertFalse(result["complete"])
            self.assertIn("hash map", result["error"])

    def guest_args(self):
        asset = self.root / "input"
        asset.write_bytes(b"input")
        return SimpleNamespace(qemu=asset, kernel=asset, dtb=asset, initramfs=asset,
                               assets=self.assets, output=self.root / "result",
                               timeout=2,
                               case_timeout=30)

    def test_complete_failing_run_exits_one(self):
        args = self.guest_args()
        args.output.mkdir()
        # Real pipe draining and process cleanup, without needing a QEMU build.
        real_popen = subprocess.Popen

        def fake_qemu(command, **kwargs):
            self.assertIn("sam9x75-curiosity", command)
            self.assertIn("-nic", command)
            self.assertNotIn("-drive", command)
            (args.output / "qemu.log").write_bytes(b"")
            return real_popen([sys.executable, "-c", f"print({transcript(True)!r}, end='')"], **kwargs)

        with patch("libc_guest.subprocess.Popen", side_effect=fake_qemu):
            text = collect_guest(args, args.initramfs, {}, rb"(?:^|\n)UPSTREAM_DONE [01]\r?\n")
        result = runner.report(runner.validate(text, TESTS), False)
        self.assertEqual(result["exit_status"], 1)
        self.assertTrue((args.output / "serial.log").is_file())
        self.assertIn("combined_initramfs", json.loads((args.output / "inputs.json").read_text())["inputs"])

    def test_false_embedded_marker_does_not_finish(self):
        args = self.guest_args()
        args.output.mkdir()
        real_popen = subprocess.Popen
        with patch("libc_guest.subprocess.Popen", side_effect=lambda *a, **kw:
                   real_popen([sys.executable, "-c", "print('noise UPSTREAM_DONE 0')"], **kw)):
            with self.assertRaisesRegex(RuntimeError, "exited before"):
                collect_guest(args, args.initramfs, {}, rb"(?:^|\n)UPSTREAM_DONE [01]\r?\n")

    def test_failed_run_writes_results_and_exits_one(self):
        args = self.guest_args()

        def collect(*unused):
            (args.output / "qemu.log").write_bytes(b"")
            return transcript(True)

        prepared = (args.initramfs, functional_manifest(), TESTS)
        with patch.object(runner, "build_initramfs", return_value=prepared), \
                patch.object(runner, "collect_guest", side_effect=collect), patch("builtins.print"):
            self.assertEqual(runner.run(args), 1)
        result = json.loads((args.output / "results.json").read_text())
        self.assertTrue(result["complete"])
        self.assertEqual((result["failures"], result["exit_status"]), (1, 1))

    def test_incomplete_run_writes_error_and_exits_two(self):
        args = self.guest_args()
        prepared = (args.initramfs, functional_manifest(), TESTS)
        with patch.object(runner, "build_initramfs", return_value=prepared), \
                patch.object(runner, "collect_guest", return_value="UPSTREAM_DONE 0\n"), \
                patch("builtins.print"):
            self.assertEqual(runner.run(args), 2)
        result = json.loads((args.output / "results.json").read_text())
        self.assertFalse(result["complete"])
        self.assertEqual(result["exit_status"], 2)

    def test_non_finite_timeout_rejected(self):
        args = self.guest_args()
        for script in ("run-libc.py", "run-upstream.py"):
            for timeout in ("nan", "inf"):
                command = [sys.executable, str(Path(__file__).with_name(script)), "--timeout", timeout]
                for name in ("qemu", "kernel", "dtb", "initramfs", "assets", "output"):
                    command += [f"--{name}", str(getattr(args, name))]
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn("finite and positive", result.stderr)
        self.assertFalse(args.output.exists())

    def test_out_of_range_case_timeout_is_rejected_before_execution(self):
        args = self.guest_args()
        for timeout in ("0", "3601", "-1", "nan"):
            command = [sys.executable, runner.__file__,
                       "--case-timeout", timeout]
            for name in ("qemu", "kernel", "dtb", "initramfs",
                         "assets", "output"):
                command += [f"--{name}", str(getattr(args, name))]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("--case-timeout", result.stderr)
        self.assertFalse(args.output.exists())

    def test_timeout_preserves_output(self):
        args = self.guest_args()
        args.output.mkdir()
        args.timeout = 0.2
        real_popen = subprocess.Popen
        with patch("libc_guest.subprocess.Popen", side_effect=lambda *a, **kw:
                   real_popen([sys.executable, "-c", "import time; print('started', flush=True); time.sleep(10)"], **kw)):
            with self.assertRaises(TimeoutError):
                collect_guest(args, args.initramfs, {}, rb"(?:^|\n)UPSTREAM_DONE [01]\r?\n")
        self.assertIn("started", (args.output / "serial.log").read_text())


if __name__ == "__main__":
    unittest.main()
