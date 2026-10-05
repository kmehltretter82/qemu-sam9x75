#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run four libc ABI variants on the actual SAM9X75 Linux machine in RAM."""

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import tempfile
import time

VARIANTS = ("glibc-time32", "glibc-time64", "musl-shared", "musl-static")
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
    if manifest["variants"] != list(VARIANTS) or manifest["cases_per_variant"] != len(CASES):
        raise ValueError("Unsupported build manifest")
    for name, expected in manifest["sha256"].items():
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("Unsafe asset name")
        source = args.assets / relative
        if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Asset checksum mismatch: {name}")
        dest = root / (relative if name.startswith("lib/") else Path("tests") / relative)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        dest.chmod(0o755)
    shutil.copyfile(Path(__file__).resolve().parent / "init", root / "init")
    (root / "init").chmod(0o755)
    (root / "lib/ld-linux.so.3").symlink_to("arm-linux-gnueabi/ld-linux.so.3")
    (root / "lib/libc.so").symlink_to("ld-musl-arm.so.1")
    names = sorted(str(path.relative_to(root)) for path in root.rglob("*"))
    archive = subprocess.run(
        ["cpio", "-o", "-H", "newc"], cwd=root,
        input=("\n".join(names) + "\n").encode(), capture_output=True, check=True,
    ).stdout
    destination = root / "initramfs.cpio.gz"
    with destination.open("wb") as output, args.initramfs.open("rb") as source:
        shutil.copyfileobj(source, output)
        output.write(gzip.compress(archive, mtime=0))
    return destination, manifest


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="sam9x75-libc-", dir="/tmp") as temp:
        initramfs, manifest = build_initramfs(args, Path(temp))
        command = [
            str(args.qemu), "-M", "sam9x75-curiosity",
            "-kernel", str(args.kernel), "-dtb", str(args.dtb),
            "-initrd", str(initramfs), "-append",
            "console=ttyS0,115200 rdinit=/init panic=-1 loglevel=7 "
            "lpj=1000000 kunit.enable=0",
            "-display", "none", "-monitor", "none", "-serial", "stdio",
            "-nic", "none", "-watchdog-action", "none",
            "-d", "unimp,guest_errors", "-D", str(args.output / "qemu.log"),
        ]
        evidence = {"command": command, "build": manifest, "inputs": {}}
        for name in ("qemu", "kernel", "dtb", "initramfs"):
            evidence["inputs"][name] = hashlib.sha256(getattr(args, name).read_bytes()).hexdigest()
        (args.output / "inputs.json").write_text(json.dumps(evidence, indent=2) + "\n")
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   bufsize=0)
        transcript = b""
        deadline = time.monotonic() + args.timeout
        try:
            with selectors.DefaultSelector() as selector, (args.output / "serial.log").open("wb") as log:
                selector.register(process.stdout, selectors.EVENT_READ)
                while not re.search(rb"(?:^|\n)LIBC_GATE_DONE [01]\r?\n", transcript):
                    if time.monotonic() >= deadline:
                        raise TimeoutError("SAM9X75 libc guest timed out")
                    if not selector.select(min(1, max(0, deadline - time.monotonic()))):
                        continue
                    data = os.read(process.stdout.fileno(), 65536)
                    if not data:
                        raise RuntimeError("QEMU exited before the libc gate finished")
                    log.write(data)
                    log.flush()
                    transcript += data
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.stdout.close()
        results = validate(transcript.decode(errors="replace"))
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
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    for name in ("qemu", "kernel", "dtb", "initramfs", "assets"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    run(args)


if __name__ == "__main__":
    main()
