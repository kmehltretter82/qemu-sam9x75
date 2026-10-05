#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject incomplete math build/capability accounting without waiving API failures."""

import copy
from contextlib import redirect_stderr
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("math_runner", Path(__file__).with_name("run-math.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def observations(glibc=False):
    rows = ["MATH_ABI ptr=4 float=4 double=8 long_double=8 mant=53 max_exp=1024 all_except=" +
            ("31" if glibc else "0")]
    rows += ["MATH_MODE RN available=1 requested=0 set_rc=0 get=0 reset_rc=" + ("1" if glibc else "0")]
    for name, requested in zip(("RZ", "RD", "RU"), (12582912, 8388608, 4194304)):
        rows.append(f"MATH_MODE {name} available=1 requested={requested} set_rc=1 get=0 reset_rc=1"
                    if glibc else f"MATH_MODE {name} available=0 requested=-1 set_rc=-999 get=0 reset_rc=0")
    for name, mask in zip(runner.EXCEPTION_NAMES, (1, 2, 4, 8, 16)):
        rows.append(f"MATH_EXCEPTION {name} available=1 mask={mask} raise_rc=1 observed=0 clear_rc=1"
                    if glibc else f"MATH_EXCEPTION {name} available=0 mask=0 raise_rc=-999 observed=0 clear_rc=0")
    rows.append("MATH_ENV_RESTORE rc=" + ("1" if glibc else "0"))
    return "\n".join(rows)


def manifest():
    tests = ["fenv", "sqrt"]
    result = {"schema": 1, "kind": "upstream-math-characterization",
              "source_commit": "7b95dfa5f5d5ca4d949221e0228ccc290bacc14e",
              "math_variants": list(runner.DYNAMIC), "probe_variants": list(runner.VARIANTS),
              "tests": tests, "source_assertions_modified": False,
              "excluded_variant": {"variant": "musl-static", "tests": tests,
                                   "reason": "Pinned upstream math.BINS_TEMPL is dynamic-only"},
              "outcomes": [], "infrastructure": [], "sha256": {}}
    for variant in runner.DYNAMIC:
        for name in tests:
            asset = f"{variant}/{name}"
            result["outcomes"].append({"variant": variant, "test": name, "returncode": 0, "asset": asset})
            result["sha256"][asset] = "0" * 64
    supports = [("musl-static", "tools/run-unprivileged")]
    for variant in runner.VARIANTS:
        supports.append((variant, "fenv-probe"))
        if variant in runner.DYNAMIC:
            supports.append((variant, "runtest"))
    for variant, name in supports:
        asset = name if "/" in name else f"{variant}/{name}"
        result["infrastructure"].append({"variant": variant, "test": name, "returncode": 0, "asset": asset})
        result["sha256"][asset] = "0" * 64
    return result


class CapabilityTests(unittest.TestCase):
    def test_musl_round_nearest_only(self):
        result = runner.capabilities(observations())
        self.assertEqual([row["name"] for row in result["modes"] if row["api_supported"]], ["RN"])
        self.assertFalse(any(row["api_supported"] for row in result["exceptions"]))
        self.assertEqual(result["environment_restore_rc"], 0)

    def test_glibc_unsupported_apis_are_observations(self):
        result = runner.capabilities(observations(True))
        self.assertEqual(result["abi"]["all_except"], 31)
        self.assertEqual(result["environment_restore_rc"], 1)
        self.assertTrue(result["modes"][0]["api_supported"])
        self.assertTrue(all(row["available"] for row in result["modes"]))
        self.assertFalse(any(row["api_supported"] for row in result["modes"][1:]))
        self.assertFalse(any(row["api_supported"] for row in result["exceptions"]))

    def test_partial_or_extra_probe_rejected(self):
        for text in ("\n".join(observations().splitlines()[:-1]), observations() + "\nextra"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                runner.capabilities(text)

    def test_wrong_abi_rejected(self):
        for old, new in (("ptr=4", "ptr=8"), ("mant=53", "mant=64"),
                         ("max_exp=1024", "max_exp=16384"), ("long_double=8", "long_double=16")):
            with self.subTest(new=new), self.assertRaises(ValueError):
                runner.capabilities(observations().replace(old, new))

    def test_round_modes_not_reorderable(self):
        rows = observations().splitlines()
        rows[2], rows[3] = rows[3], rows[2]
        with self.assertRaises(ValueError):
            runner.capabilities("\n".join(rows))

    def test_exceptions_not_reorderable(self):
        rows = observations().splitlines()
        rows[5], rows[6] = rows[6], rows[5]
        with self.assertRaises(ValueError):
            runner.capabilities("\n".join(rows))

    def test_unavailable_mode_cannot_claim_success(self):
        with self.assertRaises(ValueError):
            runner.capabilities(observations().replace("requested=-1 set_rc=-999", "requested=-1 set_rc=0", 1))

    def test_unavailable_exception_cannot_claim_success(self):
        with self.assertRaises(ValueError):
            runner.capabilities(observations().replace("mask=0 raise_rc=-999", "mask=0 raise_rc=0", 1))

    def test_numeric_fields_required(self):
        with self.assertRaises(ValueError):
            runner.capabilities(observations().replace("MATH_ENV_RESTORE rc=0", "MATH_ENV_RESTORE rc=unknown"))


class ManifestTests(unittest.TestCase):
    def test_complete_profile(self):
        self.assertEqual(len(runner.validate_manifest(manifest())), 6)

    def test_malformed_manifest_types_rejected(self):
        for data in (None, [], "not a manifest"):
            with self.subTest(data=data), self.assertRaises(ValueError):
                runner.validate_manifest(data)
        for key, value in (("schema", True), ("tests", [None]), ("tests", [{}]),
                           ("outcomes", [None]), ("infrastructure", [None]), ("sha256", [])):
            data = manifest()
            data[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                runner.validate_manifest(data)

    def test_failed_build_stays_accounted(self):
        data = manifest()
        row = data["outcomes"][0]
        row["returncode"] = 1
        del data["sha256"][row["asset"]]
        self.assertEqual(len(runner.validate_manifest(data)), 6)

    def test_missing_build_is_not_an_exclusion(self):
        data = manifest()
        data["outcomes"].pop()
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_duplicate_build_rejected(self):
        data = manifest()
        data["outcomes"][-1] = copy.deepcopy(data["outcomes"][0])
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_build_order_required(self):
        data = manifest()
        data["outcomes"].reverse()
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_false_success_without_elf_hash_rejected(self):
        data = manifest()
        del data["sha256"][data["outcomes"][0]["asset"]]
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_failed_build_cannot_have_successful_elf(self):
        data = manifest()
        data["outcomes"][0]["returncode"] = 1
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_boolean_build_status_is_not_a_native_return_code(self):
        for value in (True, False):
            data = manifest()
            data["outcomes"][0]["returncode"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                runner.validate_manifest(data)

    def test_boolean_support_status_is_not_a_native_return_code(self):
        data = manifest()
        data["infrastructure"][0]["returncode"] = False
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_missing_or_failed_support_rejected(self):
        for action in ("missing", "failed", "unhashed"):
            data = manifest()
            row = data["infrastructure"][-1]
            if action == "missing":
                data["infrastructure"].pop()
            elif action == "failed":
                row["returncode"] = 1
            else:
                del data["sha256"][row["asset"]]
            with self.subTest(action=action), self.assertRaises(ValueError):
                runner.validate_manifest(data)

    def test_support_cannot_refer_to_another_program(self):
        data = manifest()
        data["infrastructure"][-1]["asset"] = data["outcomes"][0]["asset"]
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_changed_pin_rejected(self):
        data = manifest()
        data["source_commit"] = "0" * 40
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_source_edits_not_accepted(self):
        data = manifest()
        data["source_assertions_modified"] = True
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_changed_upstream_selection_rejected(self):
        data = manifest()
        data["math_variants"].append("musl-static")
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_excluded_cases_cannot_disappear(self):
        data = manifest()
        data["excluded_variant"]["tests"] = []
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_duplicate_inventory_rejected(self):
        data = manifest()
        data["tests"].append("sqrt")
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)

    def test_unsafe_test_name_rejected(self):
        data = manifest()
        data["tests"] = ["../sqrt"]
        with self.assertRaises(ValueError):
            runner.validate_manifest(data)


class SourceIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        self.path = self.source / "src/math/sanity/sqrt.h"
        self.path.parent.mkdir(parents=True)
        self.path.write_text("original test vector\n")
        self.relative = self.path.relative_to(self.source).as_posix()
        self.data = {"source_sha256": {self.relative: runner.digest(self.path)}}
        self.git = [runner.SOURCE_COMMIT + "\n", "", (self.relative + "\0").encode()]

    def test_complete_source_inventory(self):
        with patch.object(runner.subprocess, "check_output", side_effect=self.git):
            runner.verify_source(self.source, self.data)

    def test_dirty_checkout_rejected(self):
        self.git[1] = " M src/math/sanity/sqrt.h\n"
        with patch.object(runner.subprocess, "check_output", side_effect=self.git), self.assertRaises(ValueError):
            runner.verify_source(self.source, self.data)

    def test_wrong_revision_rejected(self):
        self.git[0] = "0" * 40 + "\n"
        with patch.object(runner.subprocess, "check_output", side_effect=self.git), self.assertRaises(ValueError):
            runner.verify_source(self.source, self.data)

    def test_partial_hash_inventory_rejected(self):
        self.data["source_sha256"] = {}
        with patch.object(runner.subprocess, "check_output", side_effect=self.git), self.assertRaises(ValueError):
            runner.verify_source(self.source, self.data)

    def test_extra_hash_inventory_rejected(self):
        self.data["source_sha256"]["unused"] = "0" * 64
        with patch.object(runner.subprocess, "check_output", side_effect=self.git), self.assertRaises(ValueError):
            runner.verify_source(self.source, self.data)

    def test_altered_vector_rejected(self):
        self.path.write_text("weakened test vector\n")
        with patch.object(runner.subprocess, "check_output", side_effect=self.git), self.assertRaises(ValueError):
            runner.verify_source(self.source, self.data)


class CommandLineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.inputs = self.root / "inputs"
        self.inputs.mkdir()
        self.output = self.root / "output"
        self.argv = ["run-math.py"]
        for name in ("source", "qemu", "kernel", "dtb", "initramfs", "assets"):
            self.argv += [f"--{name}", str(self.inputs)]
        self.argv += ["--output", str(self.output)]

    def invoke(self, argv=None):
        with patch.object(runner.sys, "argv", argv or self.argv), redirect_stderr(io.StringIO()):
            return runner.main()

    def test_existing_evidence_is_never_overwritten(self):
        self.output.mkdir()
        path = self.output / "results.json"
        path.write_bytes(b"previous evidence\n")
        with patch.object(runner, "run") as run:
            self.assertEqual(self.invoke(), 2)
            run.assert_not_called()
        self.assertEqual(path.read_bytes(), b"previous evidence\n")

    def test_missing_input_is_infrastructure_error(self):
        argv = list(self.argv)
        argv[argv.index("--kernel") + 1] = str(self.root / "absent-kernel")
        with patch.object(runner, "run") as run:
            self.assertEqual(self.invoke(argv), 2)
            run.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_runtime_validation_error_retains_incomplete_evidence(self):
        with patch.object(runner, "run", side_effect=ValueError("invalid inputs")):
            self.assertEqual(self.invoke(), 2)
        self.assertEqual(json.loads((self.output / "results.json").read_text()),
                         {"complete": False, "exit_status": 2, "error": "invalid inputs"})

    def test_malformed_manifest_retains_incomplete_evidence(self):
        (self.inputs / "manifest.json").write_text("[]\n")
        self.assertEqual(self.invoke(), 2)
        result = json.loads((self.output / "results.json").read_text())
        self.assertIs(result["complete"], False)
        self.assertEqual(result["exit_status"], 2)

    def test_characterization_failure_stays_nonzero(self):
        with patch.object(runner, "run", return_value=1) as run:
            self.assertEqual(self.invoke(), 1)
            run.assert_called_once()

    def test_invalid_deadlines_rejected_before_output_creation(self):
        for flags in (("--timeout", "nan"), ("--timeout", "inf"),
                      ("--timeout", "0"), ("--case-timeout", "0"),
                      ("--batch-size", "33")):
            with self.subTest(flags=flags), self.assertRaises(SystemExit) as error:
                self.invoke([*self.argv, *flags])
            self.assertEqual(error.exception.code, 2)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
