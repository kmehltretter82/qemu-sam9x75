#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Boot a disposable BusyBox initramfs and validate Linux PWM plus LED routing."""

import argparse
import gzip
import json
import os
from pathlib import Path
import selectors
import shutil
import socket
import subprocess
import tempfile
import time


class QMP:
    def __init__(self, path, deadline, process):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        while True:
            if process.poll() is not None:
                raise RuntimeError("QEMU exited before opening QMP")
            try:
                self.sock.connect(str(path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= deadline:
                    raise TimeoutError("QMP connection timed out")
                time.sleep(0.05)
        self.sock.settimeout(10)
        self.stream = self.sock.makefile("rwb")
        json.loads(self.stream.readline())
        self.command("qmp_capabilities")

    def command(self, name, **arguments):
        self.stream.write(json.dumps({
            "execute": name, "arguments": arguments,
        }).encode() + b"\n")
        self.stream.flush()
        while True:
            response = json.loads(self.stream.readline())
            if "error" in response:
                raise RuntimeError(response["error"])
            if "return" in response:
                return response["return"]

    def close(self):
        self.stream.close()
        self.sock.close()


def build_initramfs(base, destination, root):
    sources = Path(__file__).resolve().parent
    for name in ("init", "pwm-sysfs-test.sh"):
        shutil.copyfile(sources / name, root / name)
        (root / name).chmod(0o755)
    archive = subprocess.run(
        ["cpio", "-o", "-H", "newc"], cwd=root,
        input=b"init\npwm-sysfs-test.sh\n", capture_output=True, check=True,
    ).stdout
    # Linux accepts concatenated gzip-compressed newc archives. Last init wins.
    with destination.open("wb") as output, base.open("rb") as source:
        shutil.copyfileobj(source, output)
        output.write(gzip.compress(archive, mtime=0))


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="sam9x75-pwm-", dir="/tmp") as temp:
        root = Path(temp)
        initramfs = root / "initramfs.cpio.gz"
        qmp_path = root / "qmp.sock"
        build_initramfs(args.initramfs, initramfs, root)
        command = [
            str(args.qemu), "-M", "sam9x75-curiosity",
            "-kernel", str(args.kernel), "-dtb", str(args.dtb),
            "-initrd", str(initramfs), "-append",
            "console=ttyS0,115200 rdinit=/init panic=-1 loglevel=7 "
            "lpj=1000000 kunit.enable=0 PWM_CLOCK_RATE=12000000",
            "-display", "none", "-monitor", "none", "-serial", "stdio",
            "-qmp", f"unix:{qmp_path},server=on,wait=off",
            "-nic", "none", "-watchdog-action", "none",
            "-d", "unimp,guest_errors", "-D", str(args.output / "qemu.log"),
        ]
        deadline = time.monotonic() + args.timeout
        passed = False
        channels = set()
        led_checks = 0
        long_period = False
        qmp = None
        with (args.output / "serial.log").open("wb") as log:
            process = subprocess.Popen(
                command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, bufsize=0,
            )
            try:
                qmp = QMP(qmp_path, deadline, process)
                with selectors.DefaultSelector() as selector:
                    selector.register(process.stdout, selectors.EVENT_READ)
                    pending = b""
                    while not passed:
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            raise TimeoutError("Linux PWM gate timed out")
                        events = selector.select(min(remaining, 1))
                        if not events:
                            continue
                        data = os.read(process.stdout.fileno(), 65536)
                        if not data:
                            raise RuntimeError("QEMU exited before PWM_PASS")
                        log.write(data)
                        log.flush()
                        pending += data
                        while b"\n" in pending:
                            line, pending = pending.split(b"\n", 1)
                            line = line.decode(errors="replace").strip()
                            if line.startswith("PWM_LED "):
                                _, color, expected = line.split()
                                if color not in ("blue", "green"):
                                    raise RuntimeError("Unexpected LED name")
                                actual = qmp.command(
                                    "qom-get", path=f"/machine/rgb-led-{color}",
                                    property="intensity-percent",
                                )
                                if actual != int(expected):
                                    raise RuntimeError(
                                        f"{color} LED: {actual}, expected {expected}"
                                    )
                                led_checks += 1
                                process.stdin.write(b"continue\n")
                                process.stdin.flush()
                                print(f"PASS: {color} LED {expected}%", flush=True)
                            elif line.startswith("PWM_CHANNEL_OK "):
                                channels.add(int(line.split()[1]))
                            elif line == "PWM_LONG_PERIOD_OK":
                                long_period = True
                            elif line == "PWM_FAIL":
                                raise RuntimeError("Guest PWM test failed")
                            elif line == "PWM_PASS":
                                passed = True
                if channels != {2, 3} or led_checks != 8 or not long_period:
                    raise RuntimeError("Incomplete guest PWM result")
                qmp.command("quit")
                process.wait(timeout=10)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                if qmp is not None:
                    qmp.close()
                process.stdin.close()
                process.stdout.close()
        if (args.output / "qemu.log").stat().st_size:
            raise RuntimeError("QEMU unimp/guest_errors log is not empty")
        print(f"PASS: Linux PWM2/PWM3, eight LED checks, 20 s period; {args.output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("qemu", "kernel", "dtb", "initramfs", "output"):
        parser.add_argument(f"--{name}", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=240)
    args = parser.parse_args()
    for name in ("qemu", "kernel", "dtb", "initramfs"):
        path = getattr(args, name).resolve(strict=True)
        setattr(args, name, path)
    args.output = args.output.resolve()
    run(args)


if __name__ == "__main__":
    main()
