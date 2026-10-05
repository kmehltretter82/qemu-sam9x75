#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Characterize upstream libc-test cases on SAM9X75; raw failures exit one."""

import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from libc_build import VARIANTS, runtime_asset_names
from libc_guest import collect_guest, install_assets, overlay_initramfs


def manifest_tests(manifest):
    if (manifest.get("schema") != 1 or manifest.get("kind") != "upstream-functional" or
            manifest.get("variants") != list(VARIANTS)):
        raise ValueError("Unsupported upstream build manifest")
    tests = manifest.get("tests")
    if (not isinstance(tests, list) or not tests or
            any(not isinstance(name, str) or not re.fullmatch(r"[a-z][a-z0-9_-]*", name)
                or name == "runtest" for name in tests) or len(set(tests)) != len(tests)):
        raise ValueError("Invalid or duplicate upstream functional tests")
    return tests


def validate(text, tests):
    expected = [(variant, test) for variant in VARIANTS for test in tests]
    results, output = [], []
    current, started, done = None, False, False
    for raw in text.splitlines():
        line = raw.rstrip("\r")
        if line == "UPSTREAM_SYSTEM_BEGIN":
            if started or current is not None or done:
                raise ValueError("Duplicate upstream system marker")
            started = True
        elif line.startswith("UPSTREAM_BEGIN "):
            fields = line.split()
            pair = tuple(fields[1:])
            if (not started or done or current is not None or len(fields) != 3 or
                    len(results) >= len(expected) or pair != expected[len(results)]):
                raise ValueError("Unexpected, duplicate or out-of-order upstream case")
            current, output = pair, []
        elif line.startswith("UPSTREAM_END "):
            fields = line.split()
            if (len(fields) != 4 or current is None or tuple(fields[1:3]) != current or
                    not fields[3].isdigit() or not 0 <= int(fields[3]) <= 255):
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
    if not done or current is not None or len(results) != len(expected):
        raise ValueError("Incomplete upstream functional results")
    return results


def report(results, diagnostics):
    failures = sum(result["status"] != 0 for result in results)
    status = 2 if diagnostics else int(failures != 0)
    return {"complete": True, "passed": len(results) - failures, "failures": failures,
            "qemu_diagnostics_empty": not diagnostics, "exit_status": status,
            "cases": results}


def build_initramfs(args, root):
    manifest = json.loads((args.assets / "manifest.json").read_text())
    tests = manifest_tests(manifest)
    names = [*runtime_asset_names(manifest["sha256"]),
             *(f"{variant}/{name}" for variant in VARIANTS for name in [*tests, "runtest"])]
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
mount -t tmpfs -o size=32m tmpfs /tmp || abort
mkdir -p /dev/shm || abort
mount -t tmpfs -o size=16m tmpfs /dev/shm || abort
cd /tmp || abort
echo UPSTREAM_SYSTEM_BEGIN
uname -a
failed=0
"""
    for variant in VARIANTS:
        for name in tests:
            init += (f"echo UPSTREAM_BEGIN {variant} {name}\n"
                     f"/tests/{variant}/runtest -t 30 /tests/{variant}/{name}\n"
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
            results = validate(transcript, tests)
        result = report(results, (args.output / "qemu.log").stat().st_size != 0)
    except (KeyError, ValueError, OSError, RuntimeError, TimeoutError,
            subprocess.SubprocessError) as error:
        result = {"complete": False, "exit_status": 2, "error": str(error)}
    (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    if result["complete"]:
        print(f"Upstream functional cases: {result['passed']} passed, "
              f"{result['failures']} failed; exit {result['exit_status']}; {args.output}")
    else:
        print(f"Upstream run incomplete: {result['error']}; {args.output}", file=sys.stderr)
    return result["exit_status"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("qemu", "kernel", "dtb", "initramfs", "assets", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    for name in ("qemu", "kernel", "dtb", "initramfs", "assets"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    sys.exit(run(args))


if __name__ == "__main__":
    main()
