#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Cross-build unmodified upstream libc-test functional or regression cases."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess

from libc_build import VARIANTS, compile_command, copy_runtimes, digest
from upstream_suite import (
    LAUNCHER, RUN_AS, SOURCE_COMMIT, SUITES, TLS_DSO, TLS_TEST,
    check_tests, selection,
)

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
    source_dir = args.source / "src" / args.suite
    if args.tests is None:
        args.tests = (list(DEFAULT_TESTS) if args.suite == "functional" else
                      sorted(path.stem for path in source_dir.glob("*.c")
                             if path.stem != Path(TLS_DSO).stem))
    check_tests(args.suite, args.tests)
    variants, support_assets, excluded = selection(args.suite, args.tests)
    for name in args.tests:
        if not (source_dir / f"{name}.c").is_file():
            raise ValueError(f"{args.suite} test does not exist: {name}")
        if ((source_dir / f"{name}.mk").exists() and
                not (args.suite == "regression" and name == TLS_TEST)):
            raise ValueError(f"{name} needs additional upstream build rules; not supported")

    args.output.mkdir(parents=True, exist_ok=False)
    common = args.source / "src/common"
    helpers = [common / name for name in
               ("print.c", "rand.c", "setrlim.c", "utf8.c", "path.c")]
    if args.suite == "regression":
        helpers += [common / name for name in
                    ("fdfill.c", "memfill.c", "vmfill.c")]
    launcher_source = Path(__file__).with_name("run-unprivileged.c")
    commands, hashes = [], copy_runtimes(args)
    with (args.output / "build.log").open("w") as log:
        if args.suite == "regression":
            destination = args.output / LAUNCHER
            destination.parent.mkdir()
            command = compile_command(args, "musl-static", [launcher_source],
                                      destination)
            log.write(shlex.join(command) + "\n")
            log.flush()
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                           check=True)
            commands.append({"variant": "musl-static",
                             "test": "run-unprivileged", "argv": command})
            hashes[LAUNCHER] = digest(destination)
        for variant in VARIANTS:
            (args.output / variant).mkdir()
            names = [*variants[variant], "runtest"]
            if f"{variant}/{TLS_DSO}" in support_assets:
                names.append(TLS_DSO)
            for name in names:
                shared = name == TLS_DSO
                source = ((common if name == "runtest" else source_dir) /
                          (Path(name).stem + ".c"))
                destination = args.output / variant / name
                extra = ["-I", str(common)]
                if name == TLS_TEST:
                    extra.append("-Wl,-rpath,$ORIGIN")
                inputs = [source] if shared else [source, *helpers]
                command = compile_command(
                    args, variant, inputs, destination,
                    strict=False, extra=extra, shared=shared)
                log.write(shlex.join(command) + "\n")
                log.flush()
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
                commands.append({"variant": variant, "test": name, "argv": command})
                hashes[f"{variant}/{name}"] = digest(destination)
            print(f"Built {variant}: {len(variants[variant])} "
                  f"upstream {args.suite} tests", flush=True)

    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "src/common", f"src/{args.suite}"],
        cwd=args.source)
    source_hashes = {name: digest(args.source / name) for name in
                     tracked.decode().split("\0") if name}
    manifest = {
        "schema": 1 if args.suite == "functional" else 2,
        "kind": f"upstream-{args.suite}", "source_commit": commit,
        "source_sha256": source_hashes, "variants": list(VARIANTS),
        "tests": args.tests, "commands": commands, "sha256": hashes,
        "compiler": subprocess.check_output(shlex.split(args.cc) + ["--version"], text=True),
    }
    if args.suite == "regression":
        manifest.update(variant_tests=variants, support_assets=support_assets,
                        excluded_cases=excluded, run_as=RUN_AS,
                        launcher_source_sha256=digest(launcher_source))
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    for name in ("source", "glibc", "musl", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--gcc-install", type=Path)
    parser.add_argument("--suite", choices=SUITES, default="functional")
    parser.add_argument("--tests", nargs="+")
    args = parser.parse_args()
    for name in ("source", "glibc", "musl"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    build(args)


if __name__ == "__main__":
    main()
