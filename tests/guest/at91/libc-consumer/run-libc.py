#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run four libc ABI variants on the actual SAM9X75 Linux machine in RAM."""

import argparse
import json
import math
from pathlib import Path
import re
import shutil
import tempfile

from libc_build import VARIANTS, runtime_asset_names
from libc_guest import collect_guest, install_assets, overlay_initramfs

CASES = ("memory", "allocation", "strings", "soft-float", "threads-tls",
         "signals", "processes-poll", "clocks", "events-timers", "files-time",
         "getrandom")


def validate(text):
    """Require every named result exactly once, not just a final pass marker."""
    results = {}
    current = None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("LIBC_VARIANT_BEGIN "):
            current = line.split()[1]
            if current not in VARIANTS or current in results:
                raise ValueError("Unknown or duplicate libc variant")
            results[current] = {"cases": {}, "summary": None, "status": None,
                                "abi": None}
        elif line.startswith("LIBC_CASE "):
            _, name, result = line.split()
            if current is None or name not in CASES or name in results[current]["cases"]:
                raise ValueError("Unexpected or duplicate libc case")
            results[current]["cases"][name] = result
        elif line.startswith("LIBC_RESULT "):
            match = re.fullmatch(r"LIBC_RESULT cases=(\d+) failures=(\d+)", line)
            if current is None or match is None or results[current]["summary"] is not None:
                raise ValueError("Unexpected libc summary")
            results[current]["summary"] = [int(v) for v in match.groups()]
        elif line.startswith("LIBC_VARIANT_END "):
            _, variant, status = line.split()
            if current != variant:
                raise ValueError("Mismatched libc variant end")
            results[current]["status"] = int(status)
            current = None
        elif line.startswith("LIBC_ABI "):
            match = re.fullmatch(r"LIBC_ABI time_bits=(\d+) off_bits=(\d+) future=(\d+)", line)
            if current is None or match is None or results[current]["abi"] is not None:
                raise ValueError("Unexpected libc ABI marker")
            results[current]["abi"] = [int(v) for v in match.groups()]
        elif line.startswith("LIBC_FAILURE "):
            raise ValueError(line)
    if set(results) != set(VARIANTS) or current is not None:
        raise ValueError("Incomplete libc variants")
    for variant, result in results.items():
        expected_abi = [32, 64, 2114380800] if variant == "glibc-time32" else [64, 64, 2208988800]
        if (result["cases"] != dict.fromkeys(CASES, "PASS") or
                result["summary"] != [len(CASES), 0] or result["status"] != 0 or
                result["abi"] != expected_abi):
            raise ValueError(f"Incomplete or failing results: {variant}: {result}")
    if text.splitlines().count("LIBC_GATE_DONE 0") != 1:
        raise ValueError("Missing or duplicate final libc pass marker")
    return results


def build_initramfs(args, root):
    manifest = json.loads((args.assets / "manifest.json").read_text())
    if (manifest.get("schema") != 1 or manifest["variants"] != list(VARIANTS) or
            manifest["cases_per_variant"] != len(CASES)):
        raise ValueError("Unsupported build manifest")
    names = [*runtime_asset_names(manifest["sha256"]), *VARIANTS]
    install_assets(args.assets, root, manifest["sha256"], names)
    shutil.copyfile(Path(__file__).resolve().parent / "init", root / "init")
    (root / "init").chmod(0o755)
    return overlay_initramfs(args.initramfs, root), manifest


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="sam9x75-libc-", dir="/tmp") as temp:
        initramfs, manifest = build_initramfs(args, Path(temp))
        transcript = collect_guest(args, initramfs, manifest,
                                   rb"(?:^|\n)LIBC_GATE_DONE [01]\r?\n")
        results = validate(transcript)
        if (args.output / "qemu.log").stat().st_size:
            raise RuntimeError("QEMU unimp/guest_errors log is not empty")
        (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(f"PASS: {len(VARIANTS) * len(CASES)} libc contracts on SAM9X75; {args.output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("qemu", "kernel", "dtb", "initramfs", "assets", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=240)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    for name in ("qemu", "kernel", "dtb", "initramfs", "assets"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    run(args)


if __name__ == "__main__":
    main()
