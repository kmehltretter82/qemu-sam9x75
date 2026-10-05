#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Cross-build selected unmodified upstream libc-test functional cases."""

import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess

from libc_build import VARIANTS, compile_command, copy_runtimes, digest

SOURCE_COMMIT = "7b95dfa5f5d5ca4d949221e0228ccc290bacc14e"
DEFAULT_TESTS = (
    "argv", "basename", "clock_gettime", "clocale_mbfuncs", "dirname", "env",
    "fcntl", "fdopen", "fnmatch", "fscanf", "fwscanf", "inet_pton",
    "ipc_msg", "ipc_sem", "ipc_shm", "memstream", "mntent", "popen",
    "pthread_cancel", "pthread_cancel-points", "pthread_cond", "pthread_mutex",
    "pthread_mutex_pi", "pthread_robust", "pthread_tsd", "qsort",
    "search_hsearch", "search_insque", "search_lsearch", "search_tsearch",
    "sem_init", "sem_open", "setjmp", "snprintf", "spawn", "sscanf", "sscanf_long",
    "stat", "string", "string_memcpy", "string_memmem", "string_memset",
    "string_strchr", "string_strcspn", "string_strstr", "strftime", "strptime",
    "strtod", "strtod_simple", "strtof", "strtol", "strtold", "swprintf", "time",
    "tls_init", "tls_local_exec", "udiv", "ungetc", "utime", "vfork", "wcstol", "wcsstr",
)


def build(args):
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=args.source, text=True).strip()
    if commit != SOURCE_COMMIT:
        raise ValueError(f"libc-test must be at pinned commit {SOURCE_COMMIT}")
    dirty = subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=all"],
        cwd=args.source, text=True)
    if dirty:
        raise ValueError("libc-test source checkout must be clean")
    if not args.tests or len(set(args.tests)) != len(args.tests):
        raise ValueError("Select at least one test, without duplicate names")
    for name in args.tests:
        if not re.fullmatch(r"[a-z][a-z0-9_-]*", name) or name == "runtest":
            raise ValueError(f"Invalid functional test name: {name}")
        if not (args.source / "src/functional" / f"{name}.c").is_file():
            raise ValueError(f"Functional test does not exist: {name}")
        if (args.source / "src/functional" / f"{name}.mk").exists():
            raise ValueError(f"{name} needs additional upstream build rules; not supported")

    args.output.mkdir(parents=True, exist_ok=False)
    common = args.source / "src/common"
    helpers = [common / name for name in
               ("print.c", "rand.c", "setrlim.c", "utf8.c", "path.c")]
    commands, hashes = [], copy_runtimes(args)
    with (args.output / "build.log").open("w") as log:
        for variant in VARIANTS:
            (args.output / variant).mkdir()
            for name in [*args.tests, "runtest"]:
                source = ((common if name == "runtest" else args.source / "src/functional")
                          / f"{name}.c")
                destination = args.output / variant / name
                command = compile_command(
                    args, variant, [source, *helpers], destination,
                    strict=False, extra=("-I", str(common)))
                log.write(shlex.join(command) + "\n")
                log.flush()
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
                commands.append({"variant": variant, "test": name, "argv": command})
                hashes[f"{variant}/{name}"] = digest(destination)
            print(f"Built {variant}: {len(args.tests)} upstream functional tests", flush=True)

    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "src/common", "src/functional"], cwd=args.source)
    source_hashes = {name: digest(args.source / name) for name in
                     tracked.decode().split("\0") if name}
    manifest = {
        "schema": 1, "kind": "upstream-functional", "source_commit": commit,
        "source_sha256": source_hashes, "variants": list(VARIANTS),
        "tests": args.tests, "commands": commands, "sha256": hashes,
        "compiler": subprocess.check_output(shlex.split(args.cc) + ["--version"], text=True),
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    for name in ("source", "glibc", "musl", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--gcc-install", type=Path)
    parser.add_argument("--tests", nargs="+", default=list(DEFAULT_TESTS))
    args = parser.parse_args()
    for name in ("source", "glibc", "musl"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    build(args)


if __name__ == "__main__":
    main()
