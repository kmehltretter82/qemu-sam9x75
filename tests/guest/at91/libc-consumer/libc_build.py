# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared ARMv5 soft-float compiler and runtime setup for libc fixtures."""

import hashlib
from pathlib import Path
import shlex
import shutil

VARIANTS = ("glibc-time32", "glibc-time64", "musl-shared", "musl-static")
RUNTIMES = ("lib/arm-linux-gnueabi/libc.so.6",
            "lib/arm-linux-gnueabi/libm.so.6",
            "lib/arm-linux-gnueabi/ld-linux.so.3",
            "lib/arm-linux-gnueabi/libgcc_s.so.1",
            "lib/ld-musl-arm.so.1")
LOCALE_FILES = tuple("lib/locale/C.utf8/" + name for name in (
    "LC_ADDRESS", "LC_COLLATE", "LC_CTYPE", "LC_IDENTIFICATION", "LC_MEASUREMENT",
    "LC_MESSAGES/SYS_LC_MESSAGES", "LC_MONETARY", "LC_NAME", "LC_NUMERIC",
    "LC_PAPER", "LC_TELEPHONE", "LC_TIME",
))


def runtime_asset_names(hashes):
    """Locale data is optional for independent sysroots, but never partial."""
    return [*RUNTIMES, *(LOCALE_FILES if set(hashes).intersection(LOCALE_FILES) else ())]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def find_library(root, name):
    for prefix in ("usr/lib/arm-linux-gnueabi", "lib/arm-linux-gnueabi", "lib"):
        candidate = root / prefix / name
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(f"{name} not found in {root}")


def gcc_runtime(args):
    if args.gcc_install is not None:
        return args.gcc_install.resolve(strict=True)
    candidates = sorted((args.glibc / "usr/lib/gcc/arm-linux-gnueabi").glob("*"))
    if len(candidates) != 1:
        raise ValueError("Specify --gcc-install when there is not exactly one GCC runtime")
    return candidates[0].resolve(strict=True)


def compile_command(args, variant, sources, output, *, strict=True, extra=()):
    if variant not in VARIANTS:
        raise ValueError(f"Unknown libc variant: {variant}")
    gcc = gcc_runtime(args)
    command = shlex.split(args.cc) + [
        "--target=arm-linux-gnueabi", "-march=armv5te", "-marm",
        "-mfloat-abi=soft", f"--gcc-install-dir={gcc}", "-fuse-ld=lld",
        "-O2", "-g", "-Wall", "-Wextra",
    ]
    if strict:
        command += ["-Werror"]
    command += ["-fno-builtin", "-D_FILE_OFFSET_BITS=64", "-pthread", *extra]
    sources = [str(source) for source in sources]
    if variant.startswith("glibc"):
        command += [f"--sysroot={args.glibc}"]
        if variant == "glibc-time64":
            command += ["-D_TIME_BITS=64"]
        command += sources + ["-lm"]
    else:
        lib = args.musl / "lib"
        command += ["-nostdinc", "-isystem", str(args.musl / "include"),
                    "-nostdlib", str(lib / "crt1.o"), str(lib / "crti.o")]
        command += sources + ["-L", str(lib), "-L", str(gcc)]
        if variant == "musl-static":
            command += ["-static"]
        else:
            command += ["-no-pie", "-Wl,--dynamic-linker,/lib/ld-musl-arm.so.1"]
        command += ["-Wl,--start-group", "-lc", "-lgcc", "-lgcc_eh",
                    "-Wl,--end-group", str(lib / "crtn.o")]
    return command + ["-o", str(output)]


def copy_runtimes(args):
    hashes = {}
    for name in RUNTIMES:
        source = (args.musl / "lib/libc.so" if name == "lib/ld-musl-arm.so.1"
                  else find_library(args.glibc, Path(name).name))
        destination = args.output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        destination.chmod(0o755)
        hashes[name] = digest(destination)
    locale = args.glibc / "usr/lib/locale/C.utf8"
    if locale.is_dir():
        for name in LOCALE_FILES:
            destination = args.output / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(locale / Path(name).relative_to("lib/locale/C.utf8"), destination)
            destination.chmod(0o644)
            hashes[name] = digest(destination)
    return hashes
