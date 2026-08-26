#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import importlib.util
import json
import math
import os
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "sam9x75_linux4sam_stress.py")
SPEC = importlib.util.spec_from_file_location("sam9x75_stress", SCRIPT)
stress = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stress)


class ManifestTests(unittest.TestCase):
    def write_manifest(self, directory, value):
        path = os.path.join(directory, "manifest.json")
        with open(path, "w", encoding="utf-8") as output:
            json.dump(value, output)
        return path

    def test_manifest_accepts_argv_and_defaults_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_manifest(directory, {
                "schema": stress.MANIFEST_SCHEMA,
                "preflight": [{"name": "uname", "argv": ["uname", "-a"]}],
                "consumers": [],
                "postflight": [],
            })
            value = stress.load_manifest(path)
        self.assertEqual(value["preflight"][0]["argv"], ["uname", "-a"])
        self.assertEqual(value["preflight"][0]["timeout"],
                         stress.DEFAULT_TIMEOUT)

    def test_manifest_rejects_shell_command_string(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_manifest(directory, {
                "schema": stress.MANIFEST_SCHEMA,
                "preflight": [{"name": "unsafe", "argv": "true; false"}],
            })
            with self.assertRaisesRegex(stress.StressError, "string array"):
                stress.load_manifest(path)

    def test_manifest_rejects_duplicate_names_across_phases(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_manifest(directory, {
                "schema": stress.MANIFEST_SCHEMA,
                "preflight": [{"name": "same", "argv": ["true"]}],
                "postflight": [{"name": "same", "argv": ["true"]}],
            })
            with self.assertRaisesRegex(stress.StressError, "duplicate"):
                stress.load_manifest(path)

    def test_manifest_rejects_nonfinite_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_manifest(directory, {
                "schema": stress.MANIFEST_SCHEMA,
                "consumers": [{"name": "bad", "argv": ["true"],
                               "timeout": math.nan}],
            })
            with self.assertRaisesRegex(stress.StressError, "timeout"):
                stress.load_manifest(path)


class WorkloadTests(unittest.TestCase):
    def test_worker_falls_back_to_path_python3(self):
        with tempfile.TemporaryDirectory() as directory:
            args = stress.build_parser().parse_args([
                "--log-dir", directory,
                "--json", os.path.join(directory, "report.json"),
                "--cpu-mib", "1",
            ])
            with mock.patch.object(stress.sys, "executable", ""):
                specs, unused_paths = stress.workload_specs(args, directory)
        self.assertEqual(specs[0]["argv"][0], "python3")

    def test_cpu_workload_repeats_digest(self):
        result = stress.cpu_workload(1, 2)
        self.assertTrue(result["success"])
        self.assertEqual(result["mib_per_pass"], 1)
        self.assertEqual(result["passes"], 2)

    def test_memory_workload_changes_and_verifies_patterns(self):
        result = stress.memory_workload(1, 2)
        self.assertTrue(result["success"])
        self.assertEqual(len(result["sha256"]), 2)
        self.assertNotEqual(result["sha256"][0], result["sha256"][1])

    def test_storage_workload_is_scoped_and_cleans_up(self):
        with tempfile.TemporaryDirectory() as directory:
            before = os.listdir(directory)
            result = stress.storage_workload(directory, 1, 2, False)
            after = os.listdir(directory)
        self.assertTrue(result["success"])
        self.assertEqual(before, after)
        self.assertIsNone(result["work_directory"])

    def test_storage_rejects_filesystem_root(self):
        with self.assertRaisesRegex(stress.StressError, "root"):
            stress.storage_workload(os.path.sep, 1, 1, False)

    def test_malformed_success_result_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "cpu.json")
            with open(path, "w", encoding="utf-8") as output:
                json.dump({"success": True, "kind": "not-cpu"}, output)
            command = {"name": "cpu", "success": True, "status": "pass"}
            result = stress.read_workload_results([command], [("cpu", path)])
        self.assertFalse(result[0]["success"])
        self.assertFalse(result[0]["command"]["success"])
        self.assertEqual(result[0]["command"]["status"], "invalid-result")


class CommandTests(unittest.TestCase):
    def command(self, name, code, timeout=5):
        return {"name": name,
                "argv": [sys.executable, "-c", code],
                "timeout": float(timeout)}

    def test_output_is_drained_but_retained_log_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            results = stress.run_commands(
                [self.command("noisy", "print('x' * 4096)")],
                "test", directory, 64)
        self.assertTrue(results[0]["success"])
        self.assertGreater(results[0]["output_bytes"], 64)
        self.assertEqual(results[0]["retained_bytes"], 64)
        self.assertTrue(results[0]["log_truncated"])

    def test_short_progress_is_logged_while_child_is_running(self):
        code = ("import time; print('progress-marker', flush=True); "
                "time.sleep(5)")
        with tempfile.TemporaryDirectory() as directory:
            command = stress.RunningCommand(
                self.command("progress", code, 5), "test", directory, 1024)
            seen = False
            try:
                deadline = stress.time.monotonic() + 2.0
                while stress.time.monotonic() < deadline:
                    with open(command.log_path, "rb") as source:
                        if b"progress-marker" in source.read():
                            seen = True
                            break
                    stress.time.sleep(.01)
            finally:
                command._stop()
                command.finish()
        self.assertTrue(seen)

    def test_commands_really_start_concurrently(self):
        with tempfile.TemporaryDirectory() as directory:
            first = os.path.join(directory, "first")
            second = os.path.join(directory, "second")
            code = ("import os,sys,time; "
                    "open(sys.argv[1], 'w').close(); "
                    "end=time.monotonic()+2; "
                    "exec(compile('while not os.path.exists(sys.argv[2]):\\n"
                    " if time.monotonic() >= end: raise SystemExit(3)\\n"
                    " time.sleep(.01)', '<wait>', 'exec'))")
            results = stress.run_commands([
                {"name": "one", "argv": [sys.executable, "-c", code,
                                           first, second], "timeout": 3.0},
                {"name": "two", "argv": [sys.executable, "-c", code,
                                           second, first], "timeout": 3.0},
            ], "test", directory, 1024, concurrent=True)
        self.assertTrue(all(result["success"] for result in results))

    def test_concurrent_timeout_is_not_masked_by_finish_order(self):
        with tempfile.TemporaryDirectory() as directory:
            results = stress.run_commands([
                self.command("first", "import time; time.sleep(.4)", 1),
                self.command("late", "import time; time.sleep(.2)", .05),
            ], "test", directory, 1024, concurrent=True)
        self.assertTrue(results[0]["success"])
        self.assertEqual(results[1]["status"], "timeout")

    def test_descendant_holding_output_cannot_mask_timeout(self):
        code = ("import subprocess,sys; "
                "subprocess.Popen([sys.executable, '-c', "
                "'import time; time.sleep(.4)'])")
        with tempfile.TemporaryDirectory() as directory:
            results = stress.run_commands([
                self.command("descendant", code, .05),
            ], "test", directory, 1024)
        self.assertEqual(results[0]["status"], "timeout")

    def test_timeout_is_a_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            results = stress.run_commands([
                self.command("late", "import time; time.sleep(5)", .05),
            ], "test", directory, 1024)
        self.assertFalse(results[0]["success"])
        self.assertEqual(results[0]["status"], "timeout")

    def test_log_symlink_is_rejected_without_clobbering_target(self):
        with tempfile.TemporaryDirectory() as directory:
            target = os.path.join(directory, "target")
            log_path = os.path.join(directory, "test-linked.log")
            with open(target, "wb") as output:
                output.write(b"keep me")
            os.symlink(target, log_path)
            results = stress.run_commands([
                self.command("linked", "print('must not run')"),
            ], "test", directory, 1024)
            with open(target, "rb") as source:
                target_data = source.read()
        self.assertEqual(target_data, b"keep me")
        self.assertEqual(results[0]["status"], "launch-error")
        self.assertIsNone(results[0]["retained_sha256"])

    def test_reader_start_failure_cleans_launched_process_and_fds(self):
        class StartedProcess:
            pid = 1234
            returncode = None

            def __init__(self):
                self.killed = False
                self.stdout = mock.Mock()
                self.wait_timeouts = []

            def poll(self):
                return self.returncode

            def wait(self, timeout):
                self.wait_timeouts.append(timeout)
                if self.killed:
                    self.returncode = -signal.SIGKILL
                    return self.returncode
                raise subprocess.TimeoutExpired(["started"], timeout)

        process = StartedProcess()

        def killpg(pid, sig):
            self.assertEqual(pid, process.pid)
            if sig == signal.SIGKILL:
                process.killed = True

        spec = self.command("thread-failure", "pass")
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(stress.subprocess, "Popen",
                                   return_value=process), \
                    mock.patch.object(stress.threading.Thread, "start",
                                      side_effect=RuntimeError("no thread")), \
                    mock.patch.object(stress.os, "killpg",
                                      side_effect=killpg) as kill_group, \
                    mock.patch.object(stress.os, "close",
                                      wraps=os.close) as close_fd:
                with self.assertRaisesRegex(RuntimeError, "no thread"):
                    stress.RunningCommand(spec, "test", directory, 1024)

        self.assertEqual([call.args[1] for call in kill_group.call_args_list],
                         [signal.SIGTERM, signal.SIGKILL])
        self.assertEqual(process.wait_timeouts, [2.0, 2.0])
        process.stdout.close.assert_called_once_with()
        close_fd.assert_called_once_with(mock.ANY)

    def test_post_kill_wait_is_bounded_and_reports_unkillable(self):
        class UnkillableProcess:
            pid = 1234
            returncode = None

            def __init__(self):
                self.wait_timeouts = []

            def poll(self):
                return None

            def wait(self, timeout):
                self.wait_timeouts.append(timeout)
                raise subprocess.TimeoutExpired(["unkillable"], timeout)

        class StuckReader:
            def join(self, timeout):
                self.join_timeout = timeout

            def is_alive(self):
                return True

        command = stress.RunningCommand.__new__(stress.RunningCommand)
        command.spec = self.command("unkillable", "pass", .01)
        command.phase = "test"
        command.started_wall = stress.time.time()
        command.started = stress.time.monotonic() - 1.0
        command.deadline = command.started
        command.log_path = "/unused/unkillable.log"
        command.max_log_bytes = 1024
        command.output_bytes = 0
        command.retained_bytes = 0
        command.truncated = False
        command.reader_error = None
        command.launch_error = None
        command.unkillable = False
        command.process = UnkillableProcess()
        command.reader = StuckReader()
        command.log_opened = False
        command.retained_hasher = stress.hashlib.sha256()

        with mock.patch.object(stress.os, "killpg") as killpg:
            result = command.finish()

        self.assertEqual(result["status"], "timeout-unkillable")
        self.assertTrue(result["unkillable"])
        self.assertEqual(command.process.wait_timeouts, [0.0, 2.0, 2.0])
        self.assertEqual(killpg.call_count, 2)


class EndToEndTests(unittest.TestCase):
    def test_failed_preflight_skips_load_but_runs_postflight(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = os.path.join(directory, "manifest.json")
            logs = os.path.join(directory, "logs")
            report = os.path.join(directory, "report.json")
            consumer_marker = os.path.join(directory, "consumer-ran")
            postflight_marker = os.path.join(directory, "postflight-ran")
            marker_code = ("from pathlib import Path; "
                           "Path(%r).write_text('ran')")
            with open(manifest, "w", encoding="utf-8") as output:
                json.dump({
                    "schema": stress.MANIFEST_SCHEMA,
                    "preflight": [{
                        "name": "fail",
                        "argv": [sys.executable, "-c", "raise SystemExit(7)"],
                        "timeout": 10,
                    }],
                    "consumers": [{
                        "name": "consumer",
                        "argv": [sys.executable, "-c",
                                 marker_code % consumer_marker],
                        "timeout": 10,
                    }],
                    "postflight": [{
                        "name": "postflight",
                        "argv": [sys.executable, "-c",
                                 marker_code % postflight_marker],
                        "timeout": 10,
                    }],
                }, output)
            completed = subprocess.run([
                sys.executable, SCRIPT,
                "--manifest", manifest,
                "--log-dir", logs,
                "--json", report,
            ], check=False, capture_output=True, text=True, timeout=30)
            with open(report, "r", encoding="utf-8") as source:
                value = json.load(source)
            consumer_ran = os.path.exists(consumer_marker)
            postflight_ran = os.path.exists(postflight_marker)

        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertFalse(value["success"])
        self.assertFalse(consumer_ran)
        self.assertTrue(postflight_ran)
        self.assertEqual(value["consumers"][0]["status"], "skipped")
        self.assertTrue(value["consumers"][0]["skipped"])
        self.assertTrue(value["postflight"][0]["success"])
        self.assertIn("# SKIP preflight failed", completed.stdout)

    def test_runner_combines_command_and_three_workers(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = os.path.join(directory, "manifest.json")
            logs = os.path.join(directory, "logs")
            report = os.path.join(directory, "report.json")
            with open(manifest, "w", encoding="utf-8") as output:
                json.dump({
                    "schema": stress.MANIFEST_SCHEMA,
                    "preflight": [{
                        "name": "python",
                        "argv": [sys.executable, "--version"],
                        "timeout": 10,
                    }],
                    "consumers": [],
                    "postflight": [],
                }, output)
            completed = subprocess.run([
                sys.executable, SCRIPT,
                "--manifest", manifest,
                "--log-dir", logs,
                "--json", report,
                "--cpu-mib", "1",
                "--memory-mib", "1",
                "--scratch-dir", directory,
                "--storage-mib", "1",
            ], check=False, capture_output=True, text=True, timeout=30)
            with open(report, "r", encoding="utf-8") as source:
                value = json.load(source)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("1..4", completed.stdout)
        self.assertTrue(value["success"])
        self.assertEqual({item["kind"] for item in value["workloads"]},
                         {"cpu-sha256", "memory-pattern",
                          "filesystem-fsync-rename"})


if __name__ == "__main__":
    unittest.main()
