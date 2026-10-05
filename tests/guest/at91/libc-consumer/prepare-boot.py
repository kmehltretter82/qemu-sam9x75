#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build public, pinned SAM9X75 Linux boot assets without a board or disk image."""

import argparse
import gzip
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import urllib.request

from libc_build import digest, gcc_runtime

LINUX_REVISION = "8f2c610093aa3d5a7bd50a8d0597be4f0b7a3eda"
SOURCES = (
    (f"linux-{LINUX_REVISION}.tar.gz",
     f"https://codeload.github.com/linux4microchip/linux/tar.gz/{LINUX_REVISION}",
     "d32aedc61df7321f60db983c89e71a6ff0c82d3f6395c0c6edba485723f4d717"),
    ("busybox-1.37.0.tar.bz2", "https://busybox.net/downloads/busybox-1.37.0.tar.bz2",
     "3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4"),
)
BUSYBOX_OPTIONS = (
    "STATIC", "LFS", "BUSYBOX", "FEATURE_INSTALLER", "ASH", "SH_IS_ASH", "ASH_ECHO", "ASH_TEST",
    "ASH_OPTIMIZE_FOR_SIZE", "CAT", "ECHO", "FALSE", "MKDIR", "SLEEP", "TEST",
    "FEATURE_TEST_64", "TRUE", "UNAME", "MOUNT", "FEATURE_MOUNT_FLAGS",
)
KERNEL_OPTIONS = (
    "BLK_DEV_INITRD", "RD_GZIP", "DEVTMPFS", "PROC_FS", "SYSFS", "TMPFS",
    "FUTEX", "POSIX_TIMERS", "TIMERFD", "EVENTFD", "SIGNALFD", "SYSVIPC",
    "SHMEM", "POSIX_MQUEUE",
)


def fetch(downloads, asset):
    """Never extract an unverified source or replace a corrupt cached download."""
    name, url, expected = asset
    path = downloads / name
    if not path.exists():
        partial = downloads / (name + ".part")
        print(f"Downloading {name}", flush=True)
        with partial.open("xb") as output, urllib.request.urlopen(url, timeout=60) as source:
            shutil.copyfileobj(source, output)
        if digest(partial) != expected:
            raise ValueError(f"Source checksum mismatch: {name}")
        partial.rename(path)
    if digest(path) != expected:
        raise ValueError(f"Source checksum mismatch: {name}; source was not extracted")
    return path


def extract(archive, destination):
    with tarfile.open(archive) as source:
        # Python's data filter also rejects escaping symlink/hardlink targets.
        source.extractall(destination, filter="data")


def require_options(config, options):
    actual = set(config.read_text().splitlines())
    missing = [name for name in options if f"CONFIG_{name}=y" not in actual]
    if missing:
        raise ValueError(f"Required built-in configuration missing: {', '.join(missing)}")


def enable_options(config, options):
    remaining = set(options)
    lines = []
    for line in config.read_text().splitlines():
        name = (line.removeprefix("CONFIG_").split("=", 1)[0]
                if line.startswith("CONFIG_") else
                line.removeprefix("# CONFIG_").removesuffix(" is not set"))
        if name in remaining:
            line = f"CONFIG_{name}=y"
            remaining.remove(name)
        lines.append(line)
    if remaining:
        raise ValueError(f"Unknown configuration options: {', '.join(sorted(remaining))}")
    config.write_text("\n".join(lines) + "\n")


def newc_entry(name, data=b"", *, mode=0o40755, inode=1, rdev=(0, 0)):
    """Canonical root-owned newc record; no host uid, timestamps or device reads."""
    encoded = name.encode() + b"\0"
    fields = (inode, mode, 0, 0, 1, 0, len(data), 0, 0, *rdev, len(encoded), 0)
    header = b"070701" + b"".join(f"{value:08x}".encode() for value in fields)
    prefix = header + encoded
    return prefix + b"\0" * (-len(prefix) % 4) + data + b"\0" * (-len(data) % 4)


def initramfs(busybox):
    records = [newc_entry(name, inode=i) for i, name in enumerate(
        ("bin", "dev", "proc", "sys", "tmp"), 1)]
    records += [newc_entry("bin/busybox", busybox, mode=0o100755, inode=6),
                newc_entry("bin/sh", b"busybox", mode=0o120777, inode=7),
                newc_entry("dev/console", mode=0o20600, inode=8, rdev=(5, 1)),
                newc_entry("TRAILER!!!", mode=0, inode=9)]
    archive = b"".join(records)
    return gzip.compress(archive + b"\0" * (-len(archive) % 512), mtime=0)


def build_busybox(args, run, env):
    busybox = args.output / "busybox-1.37.0"
    compiler = shlex.split(args.cc) + [
        "--target=arm-linux-musleabi", "-march=armv5te", "-marm", "-mfloat-abi=soft",
        f"--sysroot={args.musl}", f"--gcc-install-dir={gcc_runtime(args)}", "-fuse-ld=lld",
        "-idirafter", str(args.glibc / "usr/include"),
    ]
    busybox_make = [args.make, f"-j{args.jobs}", "ARCH=arm", f"CC={shlex.join(compiler)}",
                    f"AR={args.ar}", "LD=ld.lld", f"STRIP={args.strip}"]
    print("Building minimal static ARMv5 musl BusyBox", flush=True)
    with (args.output / "busybox-build.log").open("w") as log:
        run(busybox_make + ["allnoconfig"], cwd=busybox, log=log)
        # BusyBox's allnoconfig deliberately resets loaded boolean values;
        # enable the named applets afterward and resolve dependencies normally.
        enable_options(busybox / ".config", BUSYBOX_OPTIONS)
        run(busybox_make + ["oldconfig"], cwd=busybox, log=log, input=b"\n" * 4096)
        require_options(busybox / ".config", BUSYBOX_OPTIONS)
        run(busybox_make, cwd=busybox, log=log)
    return busybox


def prepare(args):
    args.output.mkdir(parents=True, exist_ok=False)
    args.downloads.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, LC_ALL="C", LANG="C", SOURCE_DATE_EPOCH="0",
               KBUILD_BUILD_TIMESTAMP="Thu Jan 1 00:00:00 UTC 1970",
               KBUILD_BUILD_USER="sam9x75-ci", KBUILD_BUILD_HOST="builder")
    commands = []

    def run(command, *, cwd=None, log, input=None):
        commands.append(command)
        subprocess.run(command, cwd=cwd, env=env, stdout=log,
                       stderr=subprocess.STDOUT, check=True, input=input)

    for asset in SOURCES:
        extract(fetch(args.downloads, asset), args.output)
    linux = args.output / f"linux-{LINUX_REVISION}"
    kernel_build = args.output / "kernel-build"
    kernel_build.mkdir()
    kernel_make = [args.make, "-C", str(linux), f"O={kernel_build}", "ARCH=arm",
                   "LLVM=1", "LLVM_IAS=1", f"-j{args.jobs}"]
    print("Building pinned Linux4Microchip SAM9X75 kernel and board DTB", flush=True)
    with (args.output / "kernel-build.log").open("w") as log:
        run(kernel_make + ["at91_dt_defconfig"], log=log)
        run([str(linux / "scripts/config"), "--file", str(kernel_build / ".config"),
             *[flag for option in KERNEL_OPTIONS for flag in ("--enable", option)]], log=log)
        run(kernel_make + ["olddefconfig"], log=log)
        require_options(kernel_build / ".config", KERNEL_OPTIONS + (
            "SOC_SAM9X7", "MICROCHIP_PIT64B", "SERIAL_ATMEL", "SERIAL_ATMEL_CONSOLE"))
        run(kernel_make + ["zImage", "microchip/at91-sam9x75_curiosity.dtb"], log=log)

    busybox = build_busybox(args, run, env)
    assets = args.output / "boot"
    assets.mkdir()
    shutil.copyfile(kernel_build / "arch/arm/boot/zImage", assets / "zImage")
    shutil.copyfile(kernel_build / "arch/arm/boot/dts/microchip/at91-sam9x75_curiosity.dtb",
                    assets / "board.dtb")
    (assets / "initramfs.cpio.gz").write_bytes(initramfs((busybox / "busybox").read_bytes()))
    manifest = {
        "schema": 1, "linux_revision": LINUX_REVISION,
        "sources": [{"name": name, "url": url, "sha256": sha} for name, url, sha in SOURCES],
        "compiler": subprocess.check_output(shlex.split(args.cc) + ["--version"], text=True),
        "commands": commands,
        "configs": {"linux": digest(kernel_build / ".config"),
                    "busybox": digest(busybox / ".config")},
        "sha256": {name: digest(assets / name)
                   for name in ("zImage", "board.dtb", "initramfs.cpio.gz")},
    }
    (assets / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared public-source RAM-only boot assets: {assets}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("glibc", "musl", "output", "downloads"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--gcc-install", type=Path)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--ar", default="llvm-ar")
    parser.add_argument("--strip", default="llvm-strip")
    parser.add_argument("--make", default="make")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    for name in ("glibc", "musl"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    args.downloads = args.downloads.resolve()
    prepare(args)


if __name__ == "__main__":
    main()
