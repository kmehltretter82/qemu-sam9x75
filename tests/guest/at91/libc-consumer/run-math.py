#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run all built upstream math programs; retain missing builds and raw failures."""

import argparse
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
import subprocess
import tempfile
from libc_build import digest
from upstream_suite import SOURCE_COMMIT

DYNAMIC = ("glibc-time32", "glibc-time64", "musl-shared")
VARIANTS = (*DYNAMIC, "musl-static")
MODE_NAMES = ("RN", "RZ", "RD", "RU")
EXCEPTION_NAMES = ("INVALID", "DIVBYZERO", "OVERFLOW", "UNDERFLOW", "INEXACT")


def capabilities(output):
    lines = output.splitlines()
    abi = re.fullmatch(r"MATH_ABI ptr=(\d+) float=(\d+) double=(\d+) long_double=(\d+) "
                       r"mant=(\d+) max_exp=(\d+) all_except=(\d+)", lines[0] if lines else "")
    if len(lines) != 11 or abi is None:
        raise ValueError("Incomplete floating-point capability observations")
    values = list(map(int, abi.groups()))
    if values[:6] != [4, 4, 8, 8, 53, 1024]:
        raise ValueError("Unexpected ARMv5 floating-point ABI")
    result = {"abi": dict(zip(("pointer_size", "float_size", "double_size", "long_double_size",
                               "long_double_mantissa", "long_double_max_exp", "all_except"), values)),
              "modes": [], "exceptions": []}
    for index, name in enumerate(MODE_NAMES, 1):
        match = re.fullmatch(r"MATH_MODE " + name +
                             r" available=([01]) requested=(-?\d+) set_rc=(-?\d+) get=(-?\d+) reset_rc=(-?\d+)",
                             lines[index])
        if match is None:
            raise ValueError("Invalid or reordered rounding observation")
        available, requested, rc, observed, reset = map(int, match.groups())
        if not available and (requested != -1 or rc != -999):
            raise ValueError("Contradictory missing rounding-mode observation")
        result["modes"].append({"name": name, "available": bool(available),
                                "requested": requested, "set_rc": rc, "observed": observed,
                                "reset_rc": reset,
                                "api_supported": bool(available and rc == 0 and
                                                      observed == requested)})
    for index, name in enumerate(EXCEPTION_NAMES, 5):
        match = re.fullmatch(r"MATH_EXCEPTION " + name +
                             r" available=([01]) mask=(\d+) raise_rc=(-?\d+) observed=(\d+) clear_rc=(-?\d+)",
                             lines[index])
        if match is None:
            raise ValueError("Invalid or reordered exception observation")
        available, mask, rc, observed, clear = map(int, match.groups())
        if not available and (mask != 0 or rc != -999):
            raise ValueError("Contradictory missing exception observation")
        result["exceptions"].append({"name": name, "available": bool(available),
                                     "mask": mask, "raise_rc": rc, "observed": observed,
                                     "clear_rc": clear,
                                     "api_supported": bool(available and mask and rc == 0 and
                                                           observed & mask == mask and clear == 0)})
    restore = re.fullmatch(r"MATH_ENV_RESTORE rc=(-?\d+)", lines[-1])
    if restore is None:
        raise ValueError("Invalid floating-point environment restore observation")
    # API failures are the subject of this probe, not missing guest evidence.
    # Never use this observation to waive failures in the unmodified fenv test.
    result["environment_restore_rc"] = int(restore.group(1))
    return result


def validate_manifest(manifest):
    if not isinstance(manifest, dict):
        raise ValueError("Math manifest must be an object")
    tests = manifest.get("tests")
    if (type(manifest.get("schema")) is not int or manifest.get("schema") != 1 or
            manifest.get("kind") != "upstream-math-characterization" or
            manifest.get("source_commit") != SOURCE_COMMIT or
            manifest.get("math_variants") != list(DYNAMIC) or
            manifest.get("probe_variants") != list(VARIANTS) or
            not isinstance(tests, list) or not tests or
            any(not isinstance(name, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", name)
                for name in tests) or tests != sorted(set(tests))):
        raise ValueError("Invalid full math-profile manifest")
    if not isinstance(manifest.get("sha256"), dict):
        raise ValueError("Missing math asset hash map")
    expected = [(variant, name) for variant in DYNAMIC for name in tests]
    outcomes = manifest.get("outcomes", [])
    if not isinstance(outcomes, list) or any(not isinstance(row, dict) for row in outcomes):
        raise ValueError("Math build outcomes must be objects")
    if [(row.get("variant"), row.get("test")) for row in outcomes] != expected:
        raise ValueError("Incomplete or duplicate math build accounting")
    for row in outcomes:
        asset = f"{row['variant']}/{row['test']}"
        if (row.get("asset") != asset or type(row.get("returncode")) is not int or
                (row["returncode"] == 0) != (asset in manifest.get("sha256", {}))):
            raise ValueError("Contradictory math build result or binary hash")
    expected_support = [("musl-static", "tools/run-unprivileged")]
    for variant in VARIANTS:
        expected_support.append((variant, "fenv-probe"))
        if variant in DYNAMIC:
            expected_support.append((variant, "runtest"))
    support = manifest.get("infrastructure", [])
    if not isinstance(support, list) or any(not isinstance(row, dict) for row in support):
        raise ValueError("Math support outcomes must be objects")
    if [(row.get("variant"), row.get("test")) for row in support] != expected_support:
        raise ValueError("Incomplete or duplicate launcher/probe/runtest builds")
    for row in support:
        expected_asset = row["test"] if "/" in row["test"] else f"{row['variant']}/{row['test']}"
        if (type(row.get("returncode")) is not int or row.get("returncode") != 0 or
                row.get("asset") != expected_asset or
                expected_asset not in manifest.get("sha256", {})):
            raise ValueError("Guest launcher/probe/runtest build failed or has no matching hash")
    excluded = {"variant": "musl-static", "tests": tests,
                "reason": "Pinned upstream math.BINS_TEMPL is dynamic-only"}
    if manifest.get("excluded_variant") != excluded or manifest.get("source_assertions_modified") is not False:
        raise ValueError("Upstream math selection or source invariant changed")
    return outcomes


def verify_source(source, manifest):
    source = source.resolve(strict=True)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    dirty = subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=all"], cwd=source, text=True)
    if revision != SOURCE_COMMIT or dirty:
        raise ValueError("Original math source must remain at the clean pinned revision")
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "Makefile", "config.mak.def", "src/common", "src/math"],
        cwd=source).decode().split("\0")
    hashes = manifest.get("source_sha256")
    if not isinstance(hashes, dict) or not hashes or set(hashes) != {name for name in tracked if name}:
        raise ValueError("Incomplete original source/vector hash inventory")
    for relative, expected in hashes.items():
        path = source / relative
        if not path.resolve(strict=True).is_relative_to(source) or digest(path) != expected:
            raise ValueError(f"Original source checksum/confinement mismatch: {relative}")


def run(args):
    sys.path.insert(0, str(args.fixture))
    from libc_build import digest, runtime_asset_names
    from libc_guest import collect_guest, install_assets, overlay_initramfs
    from upstream_suite import LAUNCHER, RUN_AS
    spec = importlib.util.spec_from_file_location("math_upstream_runner", args.fixture / "run-upstream.py")
    upstream = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(upstream)
    manifest = json.loads((args.assets / "manifest.json").read_text())
    outcomes = validate_manifest(manifest)
    # Revalidate the full original program inventory and every input, not merely selected ELF files.
    actual_tests = sorted(path.stem for path in (args.source / "src/math").glob("*.c"))
    if manifest["tests"] != actual_tests:
        raise ValueError("Not the full upstream math inventory")
    verify_source(args.source, manifest)
    for relative, expected in manifest["sha256"].items():
        path = args.assets / relative
        if not path.resolve(strict=True).is_relative_to(args.assets) or digest(path) != expected:
            raise ValueError(f"Asset checksum/confinement mismatch: {relative}")
    report = {"kind": manifest["kind"], "source_programs": len(manifest["tests"]),
              "requested_math_cases": len(outcomes), "source_commit": manifest["source_commit"],
              "build_failures": [row for row in outcomes if row["returncode"]],
              "excluded_variant": manifest["excluded_variant"], "batches": [],
              "capabilities": {}, "case_timeout_seconds": args.case_timeout,
              "run_as": RUN_AS, "physical_hardware_validated": False,
              "compiler_fenv_caveat": manifest["compiler_fenv_caveat"]}

    def batch(label, pairs, *, probes=False):
        output = args.output / label
        output.mkdir()
        args.output = output
        names = {variant: [name for v, name in pairs if v == variant] for variant in VARIANTS}
        hashes = {name: manifest["sha256"][name] for name in runtime_asset_names(manifest["sha256"])}
        wanted = [LAUNCHER, *(f"{v}/{name}" for v, name in pairs)]
        if not probes:
            wanted += [f"{variant}/runtest" for variant in VARIANTS if names[variant]]
        hashes.update((name, manifest["sha256"][name]) for name in wanted)
        batch_manifest = {**manifest, "executed_pairs": pairs, "batch_kind": "capability" if probes else "math"}
        try:
            with tempfile.TemporaryDirectory(prefix="sam9x75-upstream-math-", dir="/tmp") as temp:
                root = Path(temp)
                install_assets(args.assets, root, hashes, list(hashes))
                init = """#!/bin/sh
/bin/busybox --install -s /bin
export LOCPATH=/lib/locale
abort() { echo UPSTREAM_ERROR guest-setup; while :; do sleep 3600; done; }
mkdir -p /proc /sys /dev /tmp || abort
mount -t proc proc /proc || abort
mount -t sysfs sysfs /sys || abort
mount -t devtmpfs devtmpfs /dev || abort
mount -t tmpfs -o size=32m,mode=1777 tmpfs /tmp || abort
cd /tmp || abort
echo UPSTREAM_SYSTEM_BEGIN
uname -a
failed=0
"""
                init += f"echo UPSTREAM_CASE_TIMEOUT {args.case_timeout}\n"
                for variant, name in pairs:
                    wrapper = "" if probes else f"/tests/{variant}/runtest -t {args.case_timeout} "
                    init += (f"echo UPSTREAM_BEGIN {variant} {name}\n"
                             f"/tests/{LAUNCHER} {wrapper}/tests/{variant}/{name}\n"
                             f"status=$?\necho UPSTREAM_END {variant} {name} $status\n"
                             '[ "$status" -eq 0 ] || failed=1\n')
                init += 'echo "UPSTREAM_DONE $failed"\nwhile :; do sleep 3600; done\n'
                (root / "init").write_text(init)
                (root / "init").chmod(0o755)
                initramfs = overlay_initramfs(args.initramfs, root)
                transcript = collect_guest(args, initramfs, batch_manifest,
                                           rb"(?:^|\n)UPSTREAM_DONE [01]\r?\n")
                cases = upstream.validate(transcript, manifest["tests"], variant_tests=names,
                                          case_timeout=args.case_timeout, unprivileged=True)
            result = upstream.report(cases, (output / "qemu.log").stat().st_size != 0)
            result["executed_pairs"] = pairs
            if probes:
                if any(case["status"] != 0 for case in cases):
                    raise ValueError("Capability probe did not complete")
                report["capabilities"] = {case["variant"]: capabilities(case["output"].replace(
                    "UPSTREAM_RUN_AS uid=1000 gid=1000 groups=0 no_new_privs=1\n", "")) for case in cases}
        except (KeyError, ValueError, OSError, RuntimeError, TimeoutError,
                subprocess.SubprocessError) as error:
            result = {"complete": False, "exit_status": 2, "error": str(error), "executed_pairs": pairs}
        finally:
            args.output = output.parent
        (output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
        report["batches"].append({"name": label, "complete": result["complete"],
                                  "exit_status": result["exit_status"], "probes": probes})
        print(f"{label}: complete={result['complete']} exit={result['exit_status']} "
              f"passed={result.get('passed')} failed={result.get('failures')}", flush=True)
        return result

    cap = batch("capabilities", [(variant, "fenv-probe") for variant in VARIANTS], probes=True)
    if not cap["complete"] or cap["exit_status"] == 2:
        report.update(complete=False, exit_status=2, error="Invalid capability evidence")
    else:
        pairs = [(row["variant"], row["test"]) for row in outcomes if not row["returncode"]]
        cases = []
        for index, offset in enumerate(range(0, len(pairs), args.batch_size)):
            result = batch(f"math-{index:03d}", pairs[offset:offset + args.batch_size])
            cases.extend(result.get("cases", []))
        incomplete = any(not row["complete"] or row["exit_status"] == 2 for row in report["batches"])
        failures = sum(row["status"] != 0 for row in cases)
        complete = not incomplete and len(cases) + len(report["build_failures"]) == len(outcomes)
        report.update(complete=complete, executed=len(cases), passed=len(cases) - failures,
                      runtime_failures=failures, cases=cases,
                      exit_status=2 if not complete else int(bool(failures or report["build_failures"])))
    (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report.get(key) for key in
                      ("complete", "requested_math_cases", "executed", "passed", "runtime_failures", "exit_status")}), flush=True)
    return report["exit_status"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, default=Path(__file__).resolve().parent)
    for name in ("source", "qemu", "kernel", "dtb", "initramfs", "assets", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=1200)
    parser.add_argument("--case-timeout", type=int, default=30)
    parser.add_argument("--batch-size", type=int, default=24)
    args = parser.parse_args()
    if (not math.isfinite(args.timeout) or args.timeout <= 0 or
            not 1 <= args.case_timeout <= 3600 or not 1 <= args.batch_size <= 32):
        parser.error("Invalid finite host/case deadline or batch size")
    try:
        for name in ("fixture", "source", "qemu", "kernel", "dtb", "initramfs", "assets"):
            setattr(args, name, getattr(args, name).resolve(strict=True))
        args.output = args.output.resolve()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Invalid math input path: {error}", file=sys.stderr)
        return 2
    try:
        args.output.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        print(f"Math output directory must be new: {error}", file=sys.stderr)
        return 2
    try:
        return run(args)
    except (KeyError, ValueError, OSError, RuntimeError, TimeoutError,
            subprocess.SubprocessError) as error:
        result = {"complete": False, "exit_status": 2, "error": str(error)}
        (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
        print(f"Math run incomplete: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
