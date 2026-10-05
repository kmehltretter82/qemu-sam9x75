# SPDX-License-Identifier: GPL-2.0-or-later
"""RAM-only overlays and bounded SAM9X75 guest transcript collection."""

import gzip
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import time

from libc_build import digest


def install_assets(assets, root, hashes, expected_names):
    if set(hashes) != set(expected_names):
        raise ValueError("Build manifest has missing or unexpected assets")
    for name, expected in hashes.items():
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts or "\n" in name:
            raise ValueError("Unsafe asset name")
        source = assets / relative
        if not source.resolve(strict=True).is_relative_to(assets.resolve()):
            raise ValueError(f"Asset escapes build directory: {name}")
        if not re.fullmatch(r"[0-9a-f]{64}", expected) or digest(source) != expected:
            raise ValueError(f"Asset checksum mismatch: {name}")
        destination = root / (relative if name.startswith("lib/") else Path("tests") / relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        destination.chmod(0o644 if name.startswith("lib/locale/") else 0o755)
    (root / "lib/ld-linux.so.3").symlink_to("arm-linux-gnueabi/ld-linux.so.3")
    (root / "lib/libc.so").symlink_to("ld-musl-arm.so.1")


def overlay_initramfs(base, root):
    names = sorted(str(path.relative_to(root)) for path in root.rglob("*"))
    archive = subprocess.run(
        ["cpio", "-o", "-H", "newc"], cwd=root,
        input=("\n".join(names) + "\n").encode(), capture_output=True, check=True,
    ).stdout
    destination = root / "initramfs.cpio.gz"
    with destination.open("wb") as output, base.open("rb") as source:
        shutil.copyfileobj(source, output)
        output.write(gzip.compress(archive, mtime=0))
    return destination


def collect_guest(args, initramfs, manifest, final_marker):
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
        evidence["inputs"][name] = digest(getattr(args, name))
    evidence["inputs"]["combined_initramfs"] = digest(initramfs)
    (args.output / "inputs.json").write_text(json.dumps(evidence, indent=2) + "\n")
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               bufsize=0)
    transcript = bytearray()
    marker = re.compile(final_marker)
    deadline = time.monotonic() + args.timeout
    try:
        with selectors.DefaultSelector() as selector, (args.output / "serial.log").open("wb") as log:
            selector.register(process.stdout, selectors.EVENT_READ)
            while not marker.search(transcript):
                if time.monotonic() >= deadline:
                    raise TimeoutError("SAM9X75 libc guest timed out")
                if not selector.select(min(1, max(0, deadline - time.monotonic()))):
                    continue
                data = os.read(process.stdout.fileno(), 65536)
                if not data:
                    raise RuntimeError("QEMU exited before the libc tests finished")
                log.write(data)
                log.flush()
                transcript.extend(data)
                if len(transcript) > 32 * 1024 * 1024:
                    raise RuntimeError("SAM9X75 libc serial output exceeds 32 MiB")
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
        else:
            process.wait()
        process.stdout.close()
    return transcript.decode(errors="replace")
