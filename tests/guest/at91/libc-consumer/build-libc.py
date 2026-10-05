#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build ARMv5 soft-float libc contracts from existing glibc/musl sysroots."""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess


VARIANTS = ("glibc-time32", "glibc-time64", "musl-shared", "musl-static")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def find_library(root, name):
    for prefix in ("usr/lib/arm-linux-gnueabi", "lib/arm-linux-gnueabi", "lib"):
        candidate = root / prefix / name
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(f"{name} not found in {root}")


def build(args):
    args.output.mkdir(parents=True, exist_ok=False)
    compiler = shlex.split(args.cc)
    gcc = args.gcc_install
    if gcc is None:
        candidates = sorted((args.glibc / "usr/lib/gcc/arm-linux-gnueabi").glob("*"))
        if len(candidates) != 1:
            raise ValueError("Specify --gcc-install when there is not exactly one GCC runtime")
        gcc = candidates[0]
    gcc = gcc.resolve(strict=True)
    common = compiler + [
        "--target=arm-linux-gnueabi", "-march=armv5te", "-marm",
        "-mfloat-abi=soft", f"--gcc-install-dir={gcc}", "-fuse-ld=lld",
        "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
        "-D_FILE_OFFSET_BITS=64", "-pthread",
    ]
    source = Path(__file__).resolve().parent / "libc-contract.c"
    commands = []
    for variant in VARIANTS:
        command = common.copy()
        if variant.startswith("glibc"):
            command += [f"--sysroot={args.glibc}"]
            if variant == "glibc-time64":
                command += ["-D_TIME_BITS=64"]
            command += [str(source), "-lm"]
        else:
            lib = args.musl / "lib"
            command += ["-nostdinc", "-isystem", str(args.musl / "include"),
                        "-nostdlib", str(lib / "crt1.o"),
                        str(lib / "crti.o"), str(source), "-L", str(lib),
                        "-L", str(gcc)]
            if variant == "musl-static":
                command += ["-static"]
            else:
                command += ["-no-pie", "-Wl,--dynamic-linker,/lib/ld-musl-arm.so.1"]
            command += ["-Wl,--start-group", "-lc", "-lgcc", "-lgcc_eh",
                        "-Wl,--end-group", str(lib / "crtn.o")]
        command += ["-o", str(args.output / variant)]
        print(f"Building {variant}", flush=True)
        subprocess.run(command, check=True)
        commands.append({"variant": variant, "argv": command})

    libraries = {
        "lib/arm-linux-gnueabi/libc.so.6": find_library(args.glibc, "libc.so.6"),
        "lib/arm-linux-gnueabi/libm.so.6": find_library(args.glibc, "libm.so.6"),
        "lib/arm-linux-gnueabi/ld-linux.so.3": find_library(args.glibc, "ld-linux.so.3"),
        "lib/arm-linux-gnueabi/libgcc_s.so.1": find_library(args.glibc, "libgcc_s.so.1"),
        "lib/ld-musl-arm.so.1": args.musl / "lib/libc.so",
    }
    hashes = {}
    for guest, source_lib in libraries.items():
        dest = args.output / guest
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_lib, dest)
        dest.chmod(0o755)
        hashes[guest] = digest(dest)
    for variant in VARIANTS:
        hashes[variant] = digest(args.output / variant)
    manifest = {
        "schema": 1, "variants": list(VARIANTS), "cases_per_variant": 11,
        "compiler": subprocess.check_output(compiler + ["--version"], text=True),
        "commands": commands, "sha256": hashes,
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang", help="Clang command (LLD must be available)")
    for name in ("glibc", "musl", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--gcc-install", type=Path)
    args = parser.parse_args()
    args.glibc = args.glibc.resolve(strict=True)
    args.musl = args.musl.resolve(strict=True)
    args.output = args.output.resolve()
    build(args)


if __name__ == "__main__":
    main()
