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

spec = importlib.util.spec_from_file_location("run_upstream", Path(__file__).with_name("run-upstream.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
TESTS = ["clock_gettime", "strtod"]


def transcript(failing=False):
    lines = ["Linux boot output", "UPSTREAM_SYSTEM_BEGIN"]
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

    def test_unsafe_manifest_names(self):
        for names in (["../strtod"], ["strtod;poweroff"], ["runtest"], ["strtod", "strtod"], []):
            with self.assertRaises(ValueError):
                runner.manifest_tests({"schema": 1, "kind": "upstream-functional",
                                       "variants": list(VARIANTS), "tests": names})


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

    def guest_args(self):
        asset = self.root / "input"
        asset.write_bytes(b"input")
        return SimpleNamespace(qemu=asset, kernel=asset, dtb=asset, initramfs=asset,
                               assets=self.assets, output=self.root / "result", timeout=2)

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

        with patch.object(runner, "build_initramfs", return_value=(args.initramfs, {}, TESTS)), \
                patch.object(runner, "collect_guest", side_effect=collect), patch("builtins.print"):
            self.assertEqual(runner.run(args), 1)
        result = json.loads((args.output / "results.json").read_text())
        self.assertTrue(result["complete"])
        self.assertEqual((result["failures"], result["exit_status"]), (1, 1))

    def test_incomplete_run_writes_error_and_exits_two(self):
        args = self.guest_args()
        with patch.object(runner, "build_initramfs", return_value=(args.initramfs, {}, TESTS)), \
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
