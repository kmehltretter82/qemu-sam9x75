#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Download pinned ARM soft-float assets and build musl in a disposable directory."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import urllib.request

ASSETS = (
    ("g/glibc", "libc6_2.41-12+deb13u4_armel.deb", "fd6308283cdad9b6dc5d04b1478d13022593997d269db26ee09bb3a31515e94f"),
    ("g/glibc", "libc6-dev_2.41-12+deb13u4_armel.deb", "26fa65007c9934d0836aee761577b598dd1432cc8a4a7e14bc7c2b9dc6eca1d9"),
    ("g/gcc-12", "libgcc-12-dev_12.2.0-14+deb12u1_armel.deb", "5916311189291783f80bfde6cf833bfef1a55593b5a845cb662c036a54fc5f55"),
    ("g/gcc-12", "libgcc-s1_12.2.0-14+deb12u1_armel.deb", "4d9bf44b70e4437e652160ec8c042ed7a186662281f4d58a728f4a87225f1527"),
    ("l/linux", "linux-libc-dev_6.1.176-1_armel.deb", "06c6fb53c25b204500c74a44b5b39aaaf5609bcf12f6b1c0680665db1ea4366a"),
    (None, "musl-1.2.6.tar.gz", "d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a"),
)


def prepare(args):
    args.output.mkdir(parents=True, exist_ok=False)
    args.downloads.mkdir(parents=True, exist_ok=True)
    glibc = args.output / "glibc-sysroot"
    glibc.mkdir()
    env = dict(os.environ, LC_ALL="C", LANG="C")
    manifest = []
    for directory, name, expected in ASSETS:
        url = (f"https://deb.debian.org/debian/pool/main/{directory}/{name}" if directory
               else f"https://musl.libc.org/releases/{name}")
        path = args.downloads / name
        if not path.exists():
            print(f"Downloading {name}", flush=True)
            partial = path.with_suffix(path.suffix + ".part")
            with partial.open("xb") as dest:
                with urllib.request.urlopen(url, timeout=60) as source:
                    while data := source.read(1024 * 1024):
                        dest.write(data)
            partial.rename(path)
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"Checksum mismatch: {name}; asset was not extracted")
        manifest.append({"name": name, "url": url, "sha256": actual})
        if directory:
            members = subprocess.check_output([args.ar, "t", str(path)], text=True)
            member = next(line for line in members.splitlines() if line.startswith("data.tar"))
            archive = subprocess.check_output([args.ar, "p", str(path), member])
            subprocess.run(["tar", "-xf", "-", "-C", str(glibc)],
                           input=archive, env=env, check=True)
        else:
            subprocess.run(["tar", "-xf", str(path), "-C", str(args.output)],
                           env=env, check=True)
    # Trixie's usrmerge layout plus Bookworm's compiler runtime gives a real
    # /lib directory. Restore the paths referenced by glibc's linker scripts.
    for name in ("arm-linux-gnueabi/libc.so.6", "arm-linux-gnueabi/libm.so.6",
                 "arm-linux-gnueabi/ld-linux.so.3", "ld-linux.so.3"):
        source, target = glibc / "usr/lib" / name, glibc / "lib" / name
        if source.exists() and not target.exists() and not target.is_symlink():
            target.parent.mkdir(parents=True, exist_ok=True)
            target.symlink_to(os.path.relpath(source, target.parent))
    (args.output / "assets.json").write_text(json.dumps(manifest, indent=2) + "\n")
    compiler = shlex.split(args.cc) + [
        "--target=arm-linux-gnueabi", "-march=armv5te", "-marm", "-mfloat-abi=soft",
        f"--gcc-install-dir={glibc}/usr/lib/gcc/arm-linux-gnueabi/12", "-fuse-ld=lld",
    ]
    env.update(CC=shlex.join(compiler), AR=args.ar, RANLIB=args.ranlib)
    prefix = args.output / "musl-sysroot"
    with (args.output / "musl-build.log").open("w") as log:
        for command in (
            ["./configure", "--target=arm-linux-musleabi", f"--prefix={prefix}",
             f"--syslibdir={prefix}/lib"],
            ["make", "-s", f"-j{args.jobs}"], ["make", "-s", "install"],
        ):
            print(f"musl: {shlex.join(command)}", flush=True)
            subprocess.run(command, cwd=args.output / "musl-1.2.6", env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    print(f"Prepared ARMv5 glibc and musl sysroots: {args.output}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New directory, never overwritten")
    parser.add_argument("--downloads", type=Path, required=True, help="Reusable download cache")
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--ar", default="llvm-ar")
    parser.add_argument("--ranlib", default="llvm-ranlib")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    args.output = args.output.resolve()
    args.downloads = args.downloads.resolve()
    prepare(args)


if __name__ == "__main__":
    main()
