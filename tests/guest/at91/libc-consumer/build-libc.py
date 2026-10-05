#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build ARMv5 soft-float libc contracts from existing glibc/musl sysroots."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess

from libc_build import VARIANTS, compile_command, copy_runtimes, digest


def build(args):
    args.output.mkdir(parents=True, exist_ok=False)
    compiler = shlex.split(args.cc)
    source = Path(__file__).resolve().parent / "libc-contract.c"
    commands = []
    for variant in VARIANTS:
        command = compile_command(args, variant, [source], args.output / variant)
        print(f"Building {variant}", flush=True)
        subprocess.run(command, check=True)
        commands.append({"variant": variant, "argv": command})

    hashes = copy_runtimes(args)
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
