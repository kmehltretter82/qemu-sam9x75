#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-only result validation tests; no cross compiler or QEMU required."""

import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("run_libc", Path(__file__).with_name("run-libc.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def passing_log():
    lines = ["Linux boot output"]
    for variant in runner.VARIANTS:
        lines.append(f"LIBC_VARIANT_BEGIN {variant}")
        for name in runner.CASES:
            lines.append(f"LIBC_CASE {name} PASS")
        abi = "32 off_bits=64 future=2114380800" if variant == "glibc-time32" else "64 off_bits=64 future=2208988800"
        lines += [f"LIBC_ABI time_bits={abi}", "LIBC_RESULT cases=11 failures=0",
                  f"LIBC_VARIANT_END {variant} 0"]
    return "\n".join(lines + ["LIBC_GATE_DONE 0", ""])


class ValidationTests(unittest.TestCase):
    def test_complete_log(self):
        self.assertEqual(len(runner.validate(passing_log())), 4)

    def test_serial_crlf(self):
        self.assertEqual(len(runner.validate(passing_log().replace("\n", "\r\n"))), 4)

    def test_missing_case(self):
        with self.assertRaises(ValueError):
            runner.validate(passing_log().replace("LIBC_CASE memory PASS\n", "", 1))

    def test_duplicate_case(self):
        with self.assertRaises(ValueError):
            runner.validate(passing_log().replace("LIBC_CASE memory PASS", "LIBC_CASE memory PASS\nLIBC_CASE memory PASS", 1))

    def test_failing_case(self):
        with self.assertRaises(ValueError):
            runner.validate(passing_log().replace("LIBC_CASE memory PASS", "LIBC_CASE memory FAIL", 1))

    def test_wrong_time_abi(self):
        with self.assertRaises(ValueError):
            runner.validate(passing_log().replace("time_bits=32", "time_bits=64", 1))

    def test_missing_variant_end(self):
        with self.assertRaises(ValueError):
            runner.validate(passing_log().replace("LIBC_VARIANT_END musl-static 0\n", ""))

    def test_bad_summary(self):
        with self.assertRaises(ValueError):
            runner.validate(passing_log().replace("cases=11 failures=0", "cases=10 failures=0", 1))

    def test_guest_failure_marker(self):
        with self.assertRaises(ValueError):
            runner.validate(passing_log().replace("LIBC_CASE memory PASS", "LIBC_FAILURE memory:1 bad errno=1\nLIBC_CASE memory PASS", 1))

    def test_final_marker_required(self):
        with self.assertRaises(ValueError):
            runner.validate(passing_log().replace("LIBC_GATE_DONE 0", "LIBC_GATE_DONE 1"))


if __name__ == "__main__":
    unittest.main()
