#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Concurrent Linux4SAM userspace and protocol-consumer stress runner.

This file is intentionally self-contained so it can be copied into a
Buildroot guest.  Commands are supplied as JSON argv arrays; the runner never
passes manifest text through a shell.
"""

import argparse
import hashlib
import json
import math
import os
import platform
import re
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time


MANIFEST_SCHEMA = "sam9x75-linux4sam-stress-manifest-v1"
REPORT_SCHEMA = "sam9x75-linux4sam-stress-report-v1"
WORKLOAD_SCHEMA = "sam9x75-linux4sam-workload-result-v1"
MIB = 1024 * 1024
DEFAULT_TIMEOUT = 900.0
DEFAULT_MAX_LOG_BYTES = 4 * MIB
DEFAULT_GUEST_PATH = "/usr/sbin:/usr/bin:/sbin:/bin"
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")


class StressError(Exception):
    pass


def positive_int(text):
    value = int(text, 0)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return value


def nonnegative_int(text):
    value = int(text, 0)
    if value < 0:
        raise argparse.ArgumentTypeError("must not be negative")
    return value


def bounded(value, name, maximum):
    if not isinstance(value, int) or isinstance(value, bool):
        raise StressError("%s must be an integer" % name)
    if value < 0 or value > maximum:
        raise StressError("%s is outside 0..%d" % (name, maximum))
    return value


def atomic_json(path, value):
    directory = os.path.dirname(os.path.abspath(path)) or "."
    os.makedirs(directory, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".sam9x75-report-", dir=directory)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            json.dump(value, output, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def load_manifest(path):
    with open(path, "r", encoding="utf-8") as source:
        manifest = json.load(source)
    if not isinstance(manifest, dict):
        raise StressError("manifest root must be an object")
    allowed = {"schema", "preflight", "consumers", "postflight"}
    unknown = sorted(set(manifest) - allowed)
    if unknown:
        raise StressError("unknown manifest keys: %s" % ", ".join(unknown))
    if manifest.get("schema") != MANIFEST_SCHEMA:
        raise StressError("manifest schema must be %s" % MANIFEST_SCHEMA)

    result = {"schema": MANIFEST_SCHEMA}
    names = set()
    for phase in ("preflight", "consumers", "postflight"):
        entries = manifest.get(phase, [])
        if not isinstance(entries, list):
            raise StressError("manifest %s must be an array" % phase)
        result[phase] = []
        for index, entry in enumerate(entries):
            where = "%s[%d]" % (phase, index)
            if not isinstance(entry, dict):
                raise StressError("%s must be an object" % where)
            unknown = sorted(set(entry) - {"name", "argv", "timeout"})
            if unknown:
                raise StressError("%s has unknown keys: %s" %
                                  (where, ", ".join(unknown)))
            name = entry.get("name")
            if not isinstance(name, str) or not NAME_RE.fullmatch(name):
                raise StressError("%s name is not a safe identifier" % where)
            if name in names:
                raise StressError("duplicate command name: %s" % name)
            names.add(name)
            argv = entry.get("argv")
            if (not isinstance(argv, list) or not argv or
                    any(not isinstance(arg, str) or not arg or "\0" in arg
                        for arg in argv)):
                raise StressError("%s argv must be a nonempty string array" %
                                  where)
            timeout = entry.get("timeout", DEFAULT_TIMEOUT)
            if (not isinstance(timeout, (int, float)) or
                    isinstance(timeout, bool) or timeout <= 0 or
                    timeout > 86400 or not math.isfinite(timeout)):
                raise StressError("%s timeout is outside (0, 86400]" % where)
            result[phase].append({
                "name": name,
                "argv": list(argv),
                "timeout": float(timeout),
            })
    return result


def make_pattern(selector):
    """Return a deterministic 1 MiB pattern without relying on randomness."""
    seed = hashlib.sha256(("sam9x75-pattern-%d" % selector).encode()).digest()
    block = bytearray(MIB)
    state = seed
    for offset in range(0, MIB, len(state)):
        state = hashlib.sha256(state + offset.to_bytes(4, "little")).digest()
        block[offset:offset + len(state)] = state
    return bytes(block)


def repeated_digest(pattern, count):
    digest = hashlib.sha256()
    for _ in range(count):
        digest.update(pattern)
    return digest.hexdigest()


def cpu_workload(mib, passes):
    bounded(mib, "cpu MiB", 1048576)
    bounded(passes, "CPU passes", 1000)
    if mib == 0 or passes == 0:
        raise StressError("CPU workload size and passes must be nonzero")
    pattern = make_pattern(0x435055)
    expected = repeated_digest(pattern, mib)
    digests = []
    started = time.monotonic()
    for _ in range(passes):
        actual = repeated_digest(pattern, mib)
        if actual != expected:
            raise StressError("CPU SHA-256 repeat did not reproduce")
        digests.append(actual)
    return {
        "schema": WORKLOAD_SCHEMA,
        "kind": "cpu-sha256",
        "mib_per_pass": mib,
        "passes": passes,
        "bytes_hashed": mib * MIB * (passes + 1),
        "sha256": expected,
        "elapsed_seconds": round(time.monotonic() - started, 6),
        "success": True,
    }


def memory_workload(mib, passes):
    bounded(mib, "memory MiB", 2048)
    bounded(passes, "memory passes", 100)
    if mib == 0 or passes == 0:
        raise StressError("memory workload size and passes must be nonzero")
    allocation = bytearray(mib * MIB)
    digests = []
    started = time.monotonic()
    for pass_number in range(passes):
        pattern = make_pattern(0x4d454d + pass_number)
        for block in range(mib):
            start = block * MIB
            allocation[start:start + MIB] = pattern
        expected = repeated_digest(pattern, mib)
        actual = hashlib.sha256(allocation).hexdigest()
        if actual != expected:
            raise StressError("memory pattern mismatch on pass %d" %
                              pass_number)
        digests.append(actual)
    return {
        "schema": WORKLOAD_SCHEMA,
        "kind": "memory-pattern",
        "mib": mib,
        "passes": passes,
        "bytes_written": mib * MIB * passes,
        "bytes_verified": mib * MIB * passes,
        "sha256": digests,
        "elapsed_seconds": round(time.monotonic() - started, 6),
        "success": True,
    }


def storage_workload(scratch_dir, mib, passes, keep):
    bounded(mib, "storage MiB", 4096)
    bounded(passes, "storage passes", 100)
    if mib == 0 or passes == 0:
        raise StressError("storage workload size and passes must be nonzero")
    if not os.path.isabs(scratch_dir):
        raise StressError("scratch directory must be an absolute path")
    requested = os.path.abspath(scratch_dir)
    if requested == os.path.sep:
        raise StressError("filesystem root cannot be a scratch directory")
    info = os.lstat(requested)
    if not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise StressError("scratch path must be an existing real directory")
    real = os.path.realpath(requested)
    if real == os.path.sep:
        raise StressError(
            "scratch directory must not resolve to filesystem root")
    if real != requested:
        raise StressError("scratch directory path must not contain symlinks")
    free = os.statvfs(real).f_bavail * os.statvfs(real).f_frsize
    required = mib * MIB * 2 + 16 * MIB
    if free < required:
        raise StressError("scratch filesystem has insufficient free space")

    work = tempfile.mkdtemp(prefix=".sam9x75-stress-", dir=real)
    temporary = os.path.join(work, "generation.tmp")
    committed = os.path.join(work, "generation.bin")
    directory_fd = os.open(work, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    success = False
    digests = []
    started = time.monotonic()
    try:
        for pass_number in range(passes):
            pattern = make_pattern(0x465300 + pass_number)
            expected = repeated_digest(pattern, mib)
            with open(temporary, "wb", buffering=0) as output:
                for _ in range(mib):
                    output.write(pattern)
                os.fsync(output.fileno())
            os.replace(temporary, committed)
            os.fsync(directory_fd)
            digest = hashlib.sha256()
            size = 0
            with open(committed, "rb", buffering=0) as source:
                while True:
                    block = source.read(MIB)
                    if not block:
                        break
                    digest.update(block)
                    size += len(block)
            if size != mib * MIB or digest.hexdigest() != expected:
                raise StressError("storage readback mismatch on pass %d" %
                                  pass_number)
            digests.append(expected)
        success = True
        return {
            "schema": WORKLOAD_SCHEMA,
            "kind": "filesystem-fsync-rename",
            "scratch_parent": real,
            "work_directory": work if keep else None,
            "mib": mib,
            "passes": passes,
            "bytes_written": mib * MIB * passes,
            "bytes_verified": mib * MIB * passes,
            "sha256": digests,
            "kept": bool(keep),
            "elapsed_seconds": round(time.monotonic() - started, 6),
            "success": True,
        }
    finally:
        os.close(directory_fd)
        if success and not keep:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass
            try:
                os.unlink(committed)
            except FileNotFoundError:
                pass
            os.rmdir(work)


def worker_main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", choices=("cpu", "memory", "storage"),
                        required=True)
    parser.add_argument("--mib", type=positive_int, required=True)
    parser.add_argument("--passes", type=positive_int, required=True)
    parser.add_argument("--scratch-dir")
    parser.add_argument("--keep-scratch", action="store_true")
    parser.add_argument("--result", required=True)
    args = parser.parse_args(argv)
    try:
        if args.kind == "cpu":
            result = cpu_workload(args.mib, args.passes)
        elif args.kind == "memory":
            result = memory_workload(args.mib, args.passes)
        else:
            if not args.scratch_dir:
                raise StressError("storage worker needs --scratch-dir")
            result = storage_workload(args.scratch_dir, args.mib,
                                      args.passes, args.keep_scratch)
        atomic_json(args.result, result)
        return 0
    except BaseException as error:
        result = {
            "schema": WORKLOAD_SCHEMA,
            "kind": args.kind,
            "success": False,
            "error": "%s: %s" % (type(error).__name__, error),
        }
        atomic_json(args.result, result)
        return 1


class RunningCommand:
    def __init__(self, spec, phase, log_dir, max_log_bytes):
        self.spec = spec
        self.phase = phase
        self.started_wall = time.time()
        self.started = time.monotonic()
        self.deadline = self.started + spec["timeout"]
        self.log_path = os.path.join(log_dir, "%s-%s.log" %
                                     (phase, spec["name"]))
        self.max_log_bytes = max_log_bytes
        self.output_bytes = 0
        self.retained_bytes = 0
        self.truncated = False
        self.reader_error = None
        self.launch_error = None
        self.unkillable = False
        self.process = None
        self.reader = None
        self.reader_started = False
        self.log_opened = False
        self.retained_hasher = hashlib.sha256()
        log_fd = None
        try:
            flags = (os.O_WRONLY | os.O_CREAT |
                     os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
            log_fd = os.open(self.log_path, flags, 0o600)
            info = os.fstat(log_fd)
            if not stat.S_ISREG(info.st_mode):
                raise OSError("log path is not a regular file")
            os.fchmod(log_fd, 0o600)
            os.ftruncate(log_fd, 0)
            os.set_blocking(log_fd, True)
            self.log_opened = True
            environment = dict(os.environ)
            current_path = environment.get("PATH", "")
            entries = [item for item in current_path.split(":") if item]
            entries.extend(item for item in DEFAULT_GUEST_PATH.split(":")
                           if item not in entries)
            environment["PATH"] = ":".join(entries)
            self.process = subprocess.Popen(
                spec["argv"], stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, start_new_session=True,
                env=environment)
            self.reader = threading.Thread(
                target=self._drain, args=(log_fd,), daemon=True)
            self.reader.start()
            self.reader_started = True
            log_fd = None
        except OSError as error:
            self._cleanup_failed_launch(log_fd)
            self.launch_error = "%s: %s" % (type(error).__name__, error)
        except BaseException:
            self._cleanup_failed_launch(log_fd)
            raise

    def _cleanup_failed_launch(self, log_fd):
        try:
            if self.process is not None:
                self._stop()
        finally:
            reader_alive = (self.reader is not None and
                            self.reader.is_alive())
            reader_owns_log = (self.reader_started or
                               (self.reader is not None and
                                self.reader.ident is not None))
            if reader_owns_log and reader_alive:
                self.reader.join(timeout=5.0)
                reader_alive = self.reader.is_alive()
            elif not reader_owns_log and log_fd is not None:
                os.close(log_fd)
            if (self.process is not None and self.process.stdout is not None and
                    not reader_alive):
                self.process.stdout.close()

    def _drain(self, log_fd):
        try:
            with os.fdopen(log_fd, "wb") as output:
                while True:
                    block = self.process.stdout.read1(65536)
                    if not block:
                        break
                    self.output_bytes += len(block)
                    remaining = self.max_log_bytes - self.retained_bytes
                    if remaining > 0:
                        retained = block[:remaining]
                        output.write(retained)
                        output.flush()
                        self.retained_hasher.update(retained)
                        self.retained_bytes += len(retained)
                    if len(block) > remaining:
                        self.truncated = True
        except OSError as error:
            self.reader_error = "%s: %s" % (type(error).__name__, error)
        finally:
            self.process.stdout.close()

    def _wait(self, timeout):
        if self.process.poll() is not None:
            return True
        try:
            self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return False
        return True

    def _stop(self):
        if self.process is None:
            return True
        try:
            os.killpg(self.process.pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            if self.process.poll() is None:
                self.process.terminate()
        exited = self._wait(2.0)
        reader_alive = self.reader is not None and self.reader.is_alive()
        if exited and not reader_alive:
            return True
        try:
            os.killpg(self.process.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            if self.process.poll() is None:
                self.process.kill()
        exited = self._wait(2.0)
        if not exited:
            self.unkillable = True
        return exited

    def finish(self):
        timed_out = False
        if self.launch_error is None:
            remaining = max(0.0, self.deadline - time.monotonic())
            try:
                self.process.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                timed_out = True
                self._stop()
            if not timed_out:
                remaining = max(0.0, self.deadline - time.monotonic())
                self.reader.join(timeout=remaining)
                if self.reader.is_alive():
                    timed_out = True
                    self._stop()
            if self.reader.is_alive():
                self.reader.join(timeout=5.0)
                if self.reader.is_alive():
                    self.truncated = True
                    self.reader_error = "output reader did not stop"
        elapsed = round(time.monotonic() - self.started, 6)
        if self.launch_error is not None:
            status = "launch-error"
            returncode = None
        elif timed_out and self.unkillable:
            status = "timeout-unkillable"
            returncode = self.process.returncode
        elif timed_out:
            status = "timeout"
            returncode = self.process.returncode
        elif self.reader_error is not None:
            status = "log-error"
            returncode = self.process.returncode
        elif self.process.returncode == 0:
            status = "pass"
            returncode = 0
        else:
            status = "fail"
            returncode = self.process.returncode

        digest = None
        if self.log_opened and not (self.reader and self.reader.is_alive()):
            digest = self.retained_hasher.hexdigest()
        return {
            "name": self.spec["name"],
            "phase": self.phase,
            "argv": self.spec["argv"],
            "timeout_seconds": self.spec["timeout"],
            "status": status,
            "success": status == "pass",
            "skipped": False,
            "skip_reason": None,
            "returncode": returncode,
            "launch_error": self.launch_error,
            "reader_error": self.reader_error,
            "unkillable": self.unkillable,
            "started_unix": self.started_wall,
            "elapsed_seconds": elapsed,
            "log": self.log_path,
            "output_bytes": self.output_bytes,
            "retained_bytes": self.retained_bytes,
            "log_truncated": self.truncated,
            "retained_sha256": digest,
        }


def skipped_command(spec, phase, reason):
    return {
        "name": spec["name"],
        "phase": phase,
        "argv": spec["argv"],
        "timeout_seconds": spec["timeout"],
        "status": "skipped",
        "success": True,
        "skipped": True,
        "skip_reason": reason,
        "returncode": None,
        "launch_error": None,
        "reader_error": None,
        "unkillable": False,
        "started_unix": None,
        "elapsed_seconds": 0.0,
        "log": None,
        "output_bytes": 0,
        "retained_bytes": 0,
        "log_truncated": False,
        "retained_sha256": None,
    }


def run_commands(specs, phase, log_dir, max_log_bytes, concurrent=False):
    results = []
    if concurrent:
        running = []
        finishers = []
        failures = []
        try:
            for spec in specs:
                running.append(RunningCommand(
                    spec, phase, log_dir, max_log_bytes))
            results = [None] * len(running)

            def finish_one(index, command):
                try:
                    results[index] = command.finish()
                except BaseException as error:
                    failures.append(error)

            finishers = [threading.Thread(
                target=finish_one, args=(index, command), daemon=True)
                for index, command in enumerate(running)]
            for thread in finishers:
                thread.start()
            for thread in finishers:
                thread.join()
            if failures:
                raise failures[0]
        except BaseException:
            for command in running:
                command._stop()
            raise
    else:
        for spec in specs:
            command = RunningCommand(spec, phase, log_dir, max_log_bytes)
            try:
                results.append(command.finish())
            except BaseException:
                command._stop()
                raise
    return results


def workload_specs(args, log_dir):
    script = os.path.abspath(__file__)
    interpreter = sys.executable or "python3"
    specs = []
    values = []
    choices = [
        ("cpu", args.cpu_mib, args.cpu_passes),
        ("memory", args.memory_mib, args.memory_passes),
    ]
    if args.scratch_dir:
        choices.append(("storage", args.storage_mib,
                        args.storage_passes))
    elif args.storage_mib:
        raise StressError("--storage-mib requires --scratch-dir")
    for kind, mib, passes in choices:
        if not mib:
            continue
        result_path = os.path.join(log_dir, "workload-%s.json" % kind)
        argv = [interpreter, script, "_worker", "--kind", kind,
                "--mib", str(mib), "--passes", str(passes),
                "--result", result_path]
        if kind == "storage":
            argv.extend(["--scratch-dir", args.scratch_dir])
            if args.keep_scratch:
                argv.append("--keep-scratch")
        specs.append({
            "name": kind,
            "argv": argv,
            "timeout": float(args.workload_timeout),
        })
        values.append((kind, result_path))
    return specs, values


def read_workload_results(command_results, result_paths):
    commands = {result["name"]: result for result in command_results}
    results = []
    expected_kinds = {
        "cpu": "cpu-sha256",
        "memory": "memory-pattern",
        "storage": "filesystem-fsync-rename",
    }
    for name, path in result_paths:
        command = commands[name]
        if command.get("skipped"):
            result = {
                "schema": WORKLOAD_SCHEMA,
                "kind": expected_kinds[name],
                "success": False,
                "skipped": True,
                "error": "worker skipped: %s" % command["skip_reason"],
            }
        elif command["success"]:
            try:
                with open(path, "r", encoding="utf-8") as source:
                    result = json.load(source)
            except (OSError, ValueError) as error:
                result = {"kind": name, "success": False,
                          "error": "cannot read worker result: %s" % error}
            if (not isinstance(result, dict) or
                    result.get("schema") != WORKLOAD_SCHEMA or
                    result.get("kind") != expected_kinds[name] or
                    result.get("success") is not True):
                result = {
                    "schema": WORKLOAD_SCHEMA,
                    "kind": expected_kinds[name],
                    "success": False,
                    "error": "worker result failed schema or semantic checks",
                }
                command["success"] = False
                command["status"] = "invalid-result"
        else:
            result = {"schema": WORKLOAD_SCHEMA,
                      "kind": expected_kinds[name], "success": False,
                      "error": "worker process %s" % command["status"]}
        result["command"] = command
        results.append(result)
    return results


def build_parser():
    parser = argparse.ArgumentParser(
        description="Run Linux4SAM consumers and deterministic load together")
    parser.add_argument("--manifest", help="JSON command manifest")
    parser.add_argument("--log-dir", required=True,
                        help="persistent directory for bounded command logs")
    parser.add_argument("--json", required=True,
                        help="atomic aggregate JSON report path")
    parser.add_argument("--max-log-bytes", type=positive_int,
                        default=DEFAULT_MAX_LOG_BYTES)
    parser.add_argument("--cpu-mib", type=nonnegative_int, default=0,
                        help="MiB hashed per CPU pass (zero disables)")
    parser.add_argument("--cpu-passes", type=positive_int, default=2)
    parser.add_argument("--memory-mib", type=nonnegative_int, default=0,
                        help="allocated MiB per memory pass (zero disables)")
    parser.add_argument("--memory-passes", type=positive_int, default=2)
    parser.add_argument("--scratch-dir",
                        help="existing absolute parent for opt-in storage load")
    parser.add_argument("--storage-mib", type=nonnegative_int, default=0,
                        help="MiB per storage generation (requires scratch)")
    parser.add_argument("--storage-passes", type=positive_int, default=2)
    parser.add_argument("--keep-scratch", action="store_true")
    parser.add_argument("--workload-timeout", type=positive_int, default=1800)
    parser.add_argument("--expect-kernel",
                        help="required substring of platform.release()")
    return parser


def runner_main(argv):
    args = build_parser().parse_args(argv)
    started_wall = time.time()
    started = time.monotonic()
    os.makedirs(args.log_dir, exist_ok=True)
    log_dir = os.path.abspath(args.log_dir)
    report_path = os.path.abspath(args.json)
    if args.expect_kernel and args.expect_kernel not in platform.release():
        raise StressError("kernel release %r does not contain %r" %
                          (platform.release(), args.expect_kernel))
    manifest = ({"schema": MANIFEST_SCHEMA, "preflight": [],
                 "consumers": [], "postflight": []}
                if args.manifest is None else load_manifest(args.manifest))
    specs, result_paths = workload_specs(args, log_dir)
    command_names = {entry["name"] for phase in
                     ("preflight", "consumers", "postflight")
                     for entry in manifest[phase]}
    collisions = sorted(command_names & {entry["name"] for entry in specs})
    if collisions:
        raise StressError("manifest names reserved by workloads: %s" %
                          ", ".join(collisions))
    selected = sum(len(manifest[phase]) for phase in
                   ("preflight", "consumers", "postflight")) + len(specs)
    if selected == 0:
        raise StressError("select at least one manifest command or workload")

    preflight = run_commands(manifest["preflight"], "preflight", log_dir,
                             args.max_log_bytes)
    concurrent_specs = list(manifest["consumers"]) + specs
    if all(result["success"] for result in preflight):
        concurrent_results = run_commands(
            concurrent_specs, "concurrent", log_dir,
            args.max_log_bytes, concurrent=True)
    else:
        reason = "preflight failed"
        concurrent_results = [
            skipped_command(spec, "concurrent", reason)
            for spec in concurrent_specs
        ]
    consumer_names = {entry["name"] for entry in manifest["consumers"]}
    consumers = [result for result in concurrent_results
                 if result["name"] in consumer_names]
    worker_commands = [result for result in concurrent_results
                       if result["name"] not in consumer_names]
    workloads = read_workload_results(worker_commands, result_paths)
    postflight = run_commands(manifest["postflight"], "postflight", log_dir,
                              args.max_log_bytes)

    workload_commands = [item["command"] for item in workloads]
    phase_results = preflight + consumers + workload_commands + postflight
    success = all(result["success"] for result in phase_results)
    report = {
        "schema": REPORT_SCHEMA,
        "success": success,
        "started_unix": started_wall,
        "elapsed_seconds": round(time.monotonic() - started, 6),
        "platform": {
            "system": platform.system(),
            "machine": platform.machine(),
            "release": platform.release(),
            "python": platform.python_version(),
        },
        "manifest": os.path.abspath(args.manifest) if args.manifest else None,
        "log_dir": log_dir,
        "preflight": preflight,
        "consumers": consumers,
        "workloads": workloads,
        "postflight": postflight,
    }
    atomic_json(report_path, report)

    print("TAP version 13")
    print("1..%d" % len(phase_results))
    for number, result in enumerate(phase_results, 1):
        if result.get("skipped"):
            print("ok %d - %s/%s # SKIP %s" %
                  (number, result["phase"], result["name"],
                   result["skip_reason"]))
        else:
            marker = "ok" if result["success"] else "not ok"
            print("%s %d - %s/%s" %
                  (marker, number, result["phase"], result["name"]))
        if not result["success"]:
            print("# status=%s log=%s" %
                  (result["status"], result["log"]))
    print("# aggregate JSON: %s" % report_path)
    return 0 if success else 1


def main():
    try:
        if len(sys.argv) > 1 and sys.argv[1] == "_worker":
            return worker_main(sys.argv[2:])
        return runner_main(sys.argv[1:])
    except (StressError, OSError, ValueError, json.JSONDecodeError) as error:
        print("sam9x75-linux4sam-stress: %s" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
