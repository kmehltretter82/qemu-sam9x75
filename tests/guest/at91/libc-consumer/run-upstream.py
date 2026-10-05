#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Characterize upstream libc-test cases on SAM9X75; raw failures exit one."""

import argparse
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

from libc_build import VARIANTS, runtime_asset_names
from libc_guest import collect_guest, install_assets, overlay_initramfs
from upstream_suite import IDENTITY, LAUNCHER, RUN_AS, manifest_selection


def manifest_tests(manifest):
    return manifest_selection(manifest)[1]


def validate(text, tests, *, variant_tests=None, case_timeout=None,
             unprivileged=False):
    if variant_tests is None:
        variant_tests = {variant: tests for variant in VARIANTS}
    expected = [(variant, test) for variant in VARIANTS
                for test in variant_tests[variant]]
    results, output = [], []
    current, started, done, observed_timeout = None, False, False, None
    identity = False
    for raw in text.splitlines():
        line = raw.rstrip("\r")
        if line == "UPSTREAM_SYSTEM_BEGIN":
            if started or current is not None or done:
                raise ValueError("Duplicate upstream system marker")
            started = True
        elif line.startswith("UPSTREAM_CASE_TIMEOUT "):
            fields = line.split()
            if (not started or results or current is not None or done or
                    observed_timeout is not None or len(fields) != 2 or
                    not fields[1].isdigit() or not 1 <= int(fields[1]) <= 3600):
                raise ValueError("Invalid or duplicate per-case timeout marker")
            observed_timeout = int(fields[1])
            if case_timeout is not None and observed_timeout != case_timeout:
                raise ValueError("Per-case timeout does not match the request")
        elif line.startswith("UPSTREAM_BEGIN "):
            fields = line.split()
            pair = tuple(fields[1:])
            if (not started or done or current is not None or len(fields) != 3 or
                    len(results) >= len(expected) or pair != expected[len(results)]):
                raise ValueError("Unexpected, duplicate or out-of-order upstream case")
            current, output, identity = pair, [], False
        elif line.startswith("UPSTREAM_RUN_AS "):
            if (not unprivileged or current is None or identity or
                    line != IDENTITY):
                raise ValueError("Unexpected, duplicate or invalid identity")
            identity = True
        elif line.startswith("UPSTREAM_END "):
            fields = line.split()
            if (len(fields) != 4 or current is None or tuple(fields[1:3]) != current or
                    not fields[3].isdigit() or not 0 <= int(fields[3]) <= 255 or
                    (unprivileged and not identity)):
                raise ValueError("Mismatched or invalid upstream case end")
            results.append({"variant": current[0], "test": current[1],
                            "status": int(fields[3]), "output": "\n".join(output)})
            current = None
        elif line.startswith("UPSTREAM_DONE "):
            failed = int(any(result["status"] for result in results))
            if (done or not started or current is not None or len(results) != len(expected)
                    or line != f"UPSTREAM_DONE {failed}"):
                raise ValueError("Incomplete or contradictory upstream final marker")
            done = True
        elif line.startswith("UPSTREAM_ERROR"):
            raise ValueError(line)
        elif current is not None:
            output.append(line)
    if (not done or current is not None or len(results) != len(expected) or
            (case_timeout is not None and observed_timeout != case_timeout)):
        raise ValueError("Incomplete upstream results")
    return results


def report(results, diagnostics):
    failures = sum(result["status"] != 0 for result in results)
    status = 2 if diagnostics else int(failures != 0)
    return {"complete": True, "passed": len(results) - failures, "failures": failures,
            "qemu_diagnostics_empty": not diagnostics, "exit_status": status,
            "cases": results}


def build_initramfs(args, root):
    manifest = json.loads((args.assets / "manifest.json").read_text())
    suite, tests, variants, helpers, _ = manifest_selection(manifest)
    if not isinstance(manifest.get("sha256"), dict):
        raise ValueError("Missing or invalid upstream asset hash map")
    names = [*runtime_asset_names(manifest["sha256"]),
             *(f"{variant}/{name}" for variant in VARIANTS
               for name in [*variants[variant], "runtest"]), *helpers]
    install_assets(args.assets, root, manifest["sha256"], names)
    init = """#!/bin/sh
/bin/busybox --install -s /bin
export LOCPATH=/lib/locale
abort() {
    echo UPSTREAM_ERROR guest-setup
    echo UPSTREAM_DONE 1
    while :; do sleep 3600; done
}
mkdir -p /proc /sys /dev /tmp || abort
mount -t proc proc /proc || abort
mount -t sysfs sysfs /sys || abort
mount -t devtmpfs devtmpfs /dev || abort
mount -t tmpfs -o size=32m,mode=1777 tmpfs /tmp || abort
mkdir -p /dev/shm || abort
mount -t tmpfs -o size=16m,mode=1777 tmpfs /dev/shm || abort
cd /tmp || abort
echo UPSTREAM_SYSTEM_BEGIN
uname -a
failed=0
"""
    case_timeout = getattr(args, "case_timeout", 30)
    init += f"echo UPSTREAM_CASE_TIMEOUT {case_timeout}\n"
    launcher = f"/tests/{LAUNCHER} " if suite == "regression" else ""
    for variant in VARIANTS:
        for name in variants[variant]:
            init += (f"echo UPSTREAM_BEGIN {variant} {name}\n"
                     f"{launcher}/tests/{variant}/runtest -t {case_timeout} "
                     f"/tests/{variant}/{name}\n"
                     f"status=$?\necho UPSTREAM_END {variant} {name} $status\n"
                     '[ "$status" -eq 0 ] || failed=1\n')
    init += 'echo "UPSTREAM_DONE $failed"\nwhile :; do sleep 3600; done\n'
    (root / "init").write_text(init)
    (root / "init").chmod(0o755)
    return overlay_initramfs(args.initramfs, root), manifest, tests


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    try:
        with tempfile.TemporaryDirectory(prefix="sam9x75-upstream-libc-", dir="/tmp") as temp:
            initramfs, manifest, tests = build_initramfs(args, Path(temp))
            transcript = collect_guest(args, initramfs, manifest,
                                       rb"(?:^|\n)UPSTREAM_DONE [01]\r?\n")
            suite, tests, variants, _, excluded = manifest_selection(manifest)
            results = validate(transcript, tests, variant_tests=variants,
                               case_timeout=getattr(args, "case_timeout", 30),
                               unprivileged=suite == "regression")
        result = report(results, (args.output / "qemu.log").stat().st_size != 0)
        result.update(suite=suite, excluded_cases=excluded,
                      case_timeout_seconds=getattr(args, "case_timeout", 30))
        if suite == "regression":
            result["run_as"] = RUN_AS
    except (KeyError, ValueError, OSError, RuntimeError, TimeoutError,
            subprocess.SubprocessError) as error:
        result = {"complete": False, "exit_status": 2, "error": str(error)}
    (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    if result["complete"]:
        print(f"Upstream {result['suite']} cases: {result['passed']} passed, "
              f"{result['failures']} failed; exit {result['exit_status']}; {args.output}")
    else:
        print(f"Upstream run incomplete: {result['error']}; {args.output}", file=sys.stderr)
    return result["exit_status"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("qemu", "kernel", "dtb", "initramfs", "assets", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--case-timeout", type=int, default=30,
                        help="runtest deadline in guest seconds (1..3600)")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    if not 1 <= args.case_timeout <= 3600:
        parser.error("--case-timeout must be between 1 and 3600 seconds")
    for name in ("qemu", "kernel", "dtb", "initramfs", "assets"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    sys.exit(run(args))


if __name__ == "__main__":
    main()
