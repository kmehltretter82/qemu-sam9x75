#!/usr/bin/env python3
"""Host-only tests for the SAM9X75 Linux AF_ALG crypto consumer."""

# SPDX-License-Identifier: GPL-2.0-or-later

import ctypes
import hashlib
import json
import os
import pathlib
import tempfile
import unittest
from unittest import mock

import sam9x75_crypto_consumer as consumer


class DeterministicDataTests(unittest.TestCase):
    def test_data_is_stable_length_exact_and_labelled(self):
        self.assertEqual(
            consumer.deterministic_bytes("fixture", 32).hex(),
            "45bcf0b45ddd723984c18c19cc9e981825a252fcd7b519bda76c42f5e0d66204",
        )
        self.assertEqual(len(consumer.deterministic_bytes("x", 65537)),
                         65537)
        self.assertNotEqual(consumer.deterministic_bytes("x", 64),
                            consumer.deterministic_bytes("y", 64))

    def test_chunking_is_lossless_bounded_and_crosses_boundaries(self):
        for length in (0, 1, 8, 64, 4095, 4096, 65537):
            with self.subTest(length=length):
                value = consumer.deterministic_bytes("chunks", length)
                chunks = consumer.split_chunks(value)
                self.assertEqual(b"".join(chunks), value)
                self.assertLessEqual(len(chunks), 15)
                self.assertGreaterEqual(len(chunks), 1)
        self.assertEqual(
            tuple(map(len, consumer.split_chunks(bytes(64)))),
            (1, 7, 31, 25),
        )


class LinuxInventoryTests(unittest.TestCase):
    SAMPLE_CRYPTO = """\
name         : sha256
driver       : atmel-sha256
module       : kernel
priority     : 100
selftest     : passed

name         : sha256
driver       : sha256-generic
module       : kernel
priority     : 100
selftest     : passed

name         : cbc(aes)
driver       : atmel-cbc-aes
module       : kernel
priority     : 100
selftest     : passed
"""

    def test_proc_crypto_keeps_duplicate_generic_names(self):
        entries = consumer.parse_proc_crypto(self.SAMPLE_CRYPTO)
        self.assertEqual(len(entries), 3)
        self.assertEqual(entries[0]["name"], "sha256")
        self.assertEqual(
            consumer.registered_drivers(entries),
            {"atmel-sha256", "sha256-generic", "atmel-cbc-aes"},
        )

    def test_interrupt_parser_sums_cpu_columns_and_shared_lines(self):
        text = """\
 40:        12         3  atmel-aic5  40 Level atmel-tdes
 41:         7         0  atmel-aic5  41 Level atmel-aes
 41:         2         1  synthetic                 atmel-aes
IPI0:       99       100  Rescheduling interrupts
"""
        values = consumer.parse_interrupts(
            text, ("atmel-aes", "atmel-sha", "atmel-tdes"),
        )
        self.assertEqual(values, {
            "atmel-aes": 10,
            "atmel-sha": 0,
            "atmel-tdes": 15,
        })
        self.assertEqual(
            consumer.interrupt_delta(
                {"atmel-aes": 10}, {"atmel-aes": 14, "atmel-sha": 2},
            ),
            {"atmel-aes": 4, "atmel-sha": 2},
        )

    def test_irq_requirements_are_engine_specific(self):
        self.assertEqual(
            consumer.interrupt_labels({"sha"}),
            ("atmel-sha", "at_xdmac"),
        )
        self.assertEqual(
            consumer.required_interrupt_labels({"sha", "hmac"}),
            {"atmel-sha"},
        )
        self.assertEqual(
            consumer.required_interrupt_labels({"aes", "tdes"}),
            {"at_xdmac"},
        )
        self.assertEqual(
            consumer.required_interrupt_labels(
                {"sha", "hmac", "aes", "tdes"}
            ),
            {"atmel-sha", "at_xdmac"},
        )

    def test_failed_selftests_are_scoped_to_selected_drivers(self):
        entries = consumer.parse_proc_crypto(self.SAMPLE_CRYPTO)
        for entry in entries:
            if entry["driver"] in {"sha256-generic", "atmel-cbc-aes"}:
                entry["selftest"] = "failed"
        self.assertEqual(
            consumer.failed_driver_selftests(entries, {"atmel-sha256"}),
            [],
        )
        self.assertEqual(
            consumer.failed_driver_selftests(entries, {"atmel-cbc-aes"}),
            ["atmel-cbc-aes"],
        )


class PhaseInterruptTests(unittest.TestCase):
    @mock.patch.object(consumer, "run_parallel", return_value=7)
    @mock.patch.object(consumer, "read_text")
    def test_each_family_requires_its_own_interrupt_delta(
            self, read_text, run_parallel):
        read_text.side_effect = (
            " 22: 0 atmel-aic5 20 Level at_xdmac\n"
            " 40: 0 atmel-aic5 41 Level atmel-sha\n",
            " 22: 5 atmel-aic5 20 Level at_xdmac\n"
            " 40: 2 atmel-aic5 41 Level atmel-sha\n",
            " 22: 5 atmel-aic5 20 Level at_xdmac\n"
            " 39: 0 atmel-aic5 39 Level atmel-aes\n",
            " 22: 5 atmel-aic5 20 Level at_xdmac\n"
            " 39: 0 atmel-aic5 39 Level atmel-aes\n",
        )

        sha_total, sha_evidence = consumer.run_monitored_phase(
            "sha", lambda _job: 1, ("sha-job",), 1, False,
            "/proc/interrupts",
        )
        aes_total, aes_evidence = consumer.run_monitored_phase(
            "aes", lambda _job: 1, ("aes-job",), 1, False,
            "/proc/interrupts",
        )

        self.assertEqual((sha_total, aes_total), (7, 7))
        self.assertEqual(sha_evidence["missing_required_interrupts"], [])
        self.assertEqual(sha_evidence["interrupts_delta"]["atmel-sha"], 2)
        self.assertEqual(
            aes_evidence["missing_required_interrupts"], ["at_xdmac"],
        )
        self.assertEqual(aes_evidence["interrupts_delta"]["at_xdmac"], 0)
        self.assertEqual(run_parallel.call_count, 2)


class MatrixTests(unittest.TestCase):
    def test_hash_matrix_covers_padding_and_dma_boundaries(self):
        jobs = consumer.build_hash_jobs(65536)
        self.assertEqual({job.algorithm for job in jobs},
                         set(consumer.HASH_DRIVERS))
        lengths = {job.length for job in jobs}
        self.assertTrue({0, 1, 55, 56, 63, 64, 65, 4095, 4096, 4097,
                         65536}.issubset(lengths))
        self.assertTrue(all(not job.key for job in jobs))
        for algorithm in ("sha384", "sha512"):
            with self.subTest(algorithm=algorithm):
                algorithm_lengths = {
                    job.length for job in jobs
                    if job.algorithm == algorithm
                }
                self.assertTrue({111, 112}.issubset(algorithm_lengths))

        hmac_jobs = consumer.build_hash_jobs(4097, keyed=True)
        self.assertEqual({job.algorithm for job in hmac_jobs},
                         set(consumer.HMAC_DRIVERS))
        self.assertTrue(all(job.key for job in hmac_jobs))
        self.assertTrue({111, 112}.issubset({
            job.length for job in hmac_jobs if job.algorithm == "sha512"
        }))
        self.assertNotIn(
            0, {job.length for job in consumer.build_hash_jobs(
                4097, include_empty=False,
            )},
        )

    def test_cipher_matrix_covers_key_sizes_modes_and_large_requests(self):
        cases = consumer.cipher_cases(65537)
        self.assertEqual(
            {case.name for case in cases},
            {"aes-128-ecb", "aes-128-cbc", "aes-192-cbc",
             "aes-256-cbc", "aes-256-ctr", "tdes-ecb", "tdes-cbc"},
        )
        self.assertEqual(
            {len(case.key) for case in cases if case.name.startswith("aes")},
            {16, 24, 32},
        )
        ctr = next(case for case in cases if case.name == "aes-256-ctr")
        self.assertIn(65536, ctr.lengths)
        self.assertIn(4097, ctr.lengths)
        self.assertNotIn(65537, ctr.lengths)
        for job in consumer.build_cipher_jobs(65537):
            self.assertEqual(job.length, len(job.data))
            self.assertEqual(len(job.iv), job.case.iv_size)
            self.assertEqual(job.length % job.case.block_size, 0)

    def test_required_names_are_driver_specific(self):
        drivers = consumer.required_drivers(4096)
        self.assertIn("atmel-sha512", drivers)
        self.assertIn("atmel-hmac-sha256", drivers)
        self.assertIn("atmel-cbc-aes", drivers)
        self.assertIn("atmel-cbc-tdes", drivers)
        self.assertTrue(all(driver.startswith("atmel-") for driver in drivers))

    def test_engine_filter_is_strict_and_scopes_drivers_and_jobs(self):
        self.assertEqual(consumer.parse_engines("aes,tdes"),
                         frozenset(("aes", "tdes")))
        with self.assertRaisesRegex(Exception, "unknown engine"):
            consumer.parse_engines("aes,rot13")
        drivers = consumer.required_drivers(4096, {"aes"})
        self.assertTrue(drivers)
        self.assertTrue(all("aes" in driver for driver in drivers))
        jobs = consumer.build_cipher_jobs(4096, {"tdes"})
        self.assertTrue(jobs)
        self.assertTrue(all(job.case.name.startswith("tdes-") for job in jobs))


class GateProfileTests(unittest.TestCase):
    def test_release_profile_requires_the_documented_full_gate(self):
        parser = consumer.build_parser()
        release_args = parser.parse_args([])
        self.assertEqual(consumer.gate_metadata(release_args), {
            "gate_profile": "release",
            "skip_empty": False,
            "require_interrupts": True,
        })

        diagnostic_argv = (
            ("--iterations", "3"),
            ("--workers", "2"),
            ("--max-bytes", "4097"),
            ("--engines", "sha,hmac,aes"),
            ("--skip-empty",),
            ("--no-require-interrupts",),
        )
        for argv in diagnostic_argv:
            with self.subTest(argv=argv):
                args = parser.parse_args(argv)
                self.assertEqual(consumer.gate_profile(args), "diagnostic")

        stronger_args = parser.parse_args((
            "--iterations", "5", "--workers", "4",
            "--max-bytes", "65537",
        ))
        self.assertEqual(consumer.gate_profile(stronger_args), "release")


class OracleTests(unittest.TestCase):
    class SendmsgSocket:
        def __init__(self, result):
            self.result = result
            self.calls = []

        def sendmsg(self, buffers, ancillary, flags):
            self.calls.append((buffers, ancillary, flags))
            return self.result

    class RecvIntoSocket:
        def __init__(self, value, limits=()):
            self.value = value
            self.limits = list(limits)
            self.addresses = []

        def recv_into(self, target, length):
            self.addresses.append(
                ctypes.addressof(ctypes.c_char.from_buffer(target))
            )
            limit = self.limits.pop(0) if self.limits else length
            count = min(len(self.value), length, limit)
            target[:count] = self.value[:count]
            self.value = self.value[count:]
            return count

    def test_sendmsg_exact_makes_and_checks_empty_syscall(self):
        operation = self.SendmsgSocket(0)
        self.assertEqual(consumer.sendmsg_exact(operation, (b"",)), 0)
        self.assertEqual(operation.calls, [((b"",), [], 0)])

    def test_sendmsg_exact_rejects_short_afalg_submission(self):
        operation = self.SendmsgSocket(2)
        with self.assertRaisesRegex(consumer.ConsumerError,
                                    "accepted 2 of 3"):
            consumer.sendmsg_exact(operation, (b"a", b"bc"))

    def test_cipher_receive_buffer_is_dma_aligned(self):
        expected = consumer.deterministic_bytes("aligned-recv", 65536)
        operation = self.RecvIntoSocket(expected)
        self.assertEqual(
            consumer.recv_exact_aligned(operation, len(expected)), expected,
        )
        self.assertTrue(operation.addresses)
        self.assertTrue(all(address % 16 == 0
                            for address in operation.addresses))

    def test_cipher_receive_retries_from_fresh_aligned_buffer(self):
        expected = consumer.deterministic_bytes("short-aligned-recv", 64)
        operation = self.RecvIntoSocket(expected, (3, 13, 48))
        self.assertEqual(
            consumer.recv_exact_aligned(operation, len(expected)), expected,
        )
        self.assertEqual(len(operation.addresses), 3)
        self.assertTrue(all(address % 16 == 0
                            for address in operation.addresses))

    def test_openssl_aes_matches_nist_cbc_vector(self):
        key = bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c")
        iv = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
        plaintext = bytes.fromhex("6bc1bee22e409f96e93d7e117393172a")
        expected = bytes.fromhex("7649abac8119b246cee98e9b12e9197d")
        self.assertEqual(
            consumer.openssl_cipher(
                "openssl", "aes-128-cbc", key, iv, plaintext,
            ),
            expected,
        )

    @unittest.skipUnless(os.environ.get("SAM9X75_AFALG_HOST_TEST"),
                         "opt-in host AF_ALG smoke test")
    def test_real_host_afalg_api_against_independent_oracles(self):
        for length in (0, 4096):
            with self.subTest(hash_length=length):
                data = consumer.deterministic_bytes("host-afalg", length)
                self.assertEqual(
                    consumer.afalg_hash("sha256", data, 32),
                    hashlib.sha256(data).digest(),
                )

        data = consumer.deterministic_bytes("host-afalg", 4096)
        key = bytes(range(16))
        iv = bytes(range(16, 32))
        expected = consumer.openssl_cipher(
            "openssl", "aes-128-cbc", key, iv, data,
        )
        encrypted = consumer.afalg_cipher(
            "cbc(aes)", key, iv, data, True,
        )
        self.assertEqual(encrypted, expected)
        self.assertEqual(
            consumer.afalg_cipher(
                "cbc(aes)", key, iv, encrypted, False,
            ),
            data,
        )


class ReportTests(unittest.TestCase):
    def test_atomic_json_replaces_old_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory, "result.json")
            path.write_text("stale", encoding="utf-8")
            value = {"schema": consumer.REPORT_SCHEMA, "success": True}
            consumer.atomic_write_json(path, value)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")),
                             value)
            self.assertEqual(list(path.parent.glob("result.json.*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
