#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Account for every pinned libc-test math program, without source edits."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys

DYNAMIC = ("glibc-time32", "glibc-time64", "musl-shared")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, default=Path(__file__).resolve().parent)
    for name in ("source", "glibc", "musl", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--gcc-install", type=Path)
    args = parser.parse_args()
    for name in ("fixture", "source", "glibc", "musl"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    sys.path.insert(0, str(args.fixture))
    from libc_build import VARIANTS, compile_command, copy_runtimes, digest
    from upstream_suite import LAUNCHER, SOURCE_COMMIT

    def clean():
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=args.source, text=True).strip()
        dirty = subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=all"],
            cwd=args.source, text=True)
        if revision != SOURCE_COMMIT or dirty:
            raise ValueError("Source must be a clean checkout of the pinned revision")

    clean()
    math_dir = args.source / "src/math"
    tests = sorted(path.stem for path in math_dir.glob("*.c"))
    # Nested generators/templates are not executable programs in upstream SRCS.
    if not tests or list(math_dir.glob("*.mk")):
        raise ValueError("Unexpected per-program upstream math build rules")
    makefile = (args.source / "Makefile").read_text()
    if "math.BINS_TEMPL:=bin.exe" not in makefile.splitlines():
        raise ValueError("Upstream dynamic-only math selection changed")
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "Makefile", "config.mak.def", "src/common", "src/math"],
        cwd=args.source).decode().split("\0")
    source_hashes = {name: digest(args.source / name) for name in tracked if name}
    args.output.mkdir(parents=True, exist_ok=False)
    hashes = copy_runtimes(args)
    common = args.source / "src/common"
    helpers = [common / name for name in
               ("print.c", "rand.c", "setrlim.c", "utf8.c", "path.c", "mtest.c")]
    extra = ["-I", str(common), "-std=c99", "-D_POSIX_C_SOURCE=200809L",
             "-frounding-math", "-Werror=implicit-function-declaration",
             "-Werror=implicit-int", "-Werror=pointer-sign", "-Werror=pointer-arith"]
    commands = []
    outcomes = []
    infrastructure = []
    for variant in VARIANTS:
        (args.output / variant).mkdir()

    def compile_one(variant, name, inputs, *, flags=(), strict=False, support=False):
        destination = args.output / name if "/" in name else args.output / variant / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        command = compile_command(args, variant, inputs, destination,
                                  strict=strict, extra=flags)
        log_name = (name.replace("/", "_") if "/" in name else f"{variant}_{name}") + ".log"
        log_path = args.output / "logs" / log_name
        log_path.parent.mkdir(exist_ok=True)
        with log_path.open("w") as log:
            log.write(shlex.join(command) + "\n")
            log.flush()
            status = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT).returncode
        relative = destination.relative_to(args.output).as_posix()
        row = {"variant": variant, "test": name, "returncode": status,
               "log": log_path.relative_to(args.output).as_posix(), "asset": relative}
        commands.append({"variant": variant, "test": name, "argv": command})
        if status == 0:
            hashes[relative] = digest(destination)
        (infrastructure if support else outcomes).append(row)
        return status

    probe = Path(__file__).with_name("math-fenv-probe.c")
    launcher = args.fixture / "run-unprivileged.c"
    compile_one("musl-static", LAUNCHER, [launcher], strict=True, support=True)
    for variant in VARIANTS:
        compile_one(variant, "fenv-probe", [probe], strict=True, support=True)
        if variant not in DYNAMIC:
            continue
        compile_one(variant, "runtest", [common / "runtest.c", *helpers],
                    flags=extra, support=True)
        for index, test in enumerate(tests, 1):
            compile_one(variant, test, [math_dir / f"{test}.c", *helpers], flags=extra)
            if index % 25 == 0 or index == len(tests):
                failed = sum(row["returncode"] != 0 for row in outcomes
                             if row["variant"] == variant)
                print(f"{variant}: attempted {index}/{len(tests)}, build failures {failed}", flush=True)
    clean()
    if any(digest(args.source / name) != expected for name, expected in source_hashes.items()):
        raise ValueError("Source changed during compilation")
    manifest = {"schema": 1, "kind": "upstream-math-characterization",
                "source_commit": SOURCE_COMMIT, "source_sha256": source_hashes,
                "tests": tests, "math_variants": list(DYNAMIC),
                "probe_variants": list(VARIANTS), "commands": commands,
                "outcomes": outcomes, "infrastructure": infrastructure, "sha256": hashes,
                "probe_source_sha256": digest(probe), "launcher_source_sha256": digest(launcher),
                "compiler": subprocess.check_output(shlex.split(args.cc) + ["--version"], text=True),
                "excluded_variant": {"variant": "musl-static", "tests": tests,
                                     "reason": "Pinned upstream math.BINS_TEMPL is dynamic-only"},
                "source_assertions_modified": False,
                "compiler_fenv_caveat": "ARM soft-float Clang warns that rounding-math/FENV_ACCESS are unsupported; logs retained"}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    failures = sum(row["returncode"] != 0 for row in outcomes)
    failed_support = sum(row["returncode"] != 0 for row in infrastructure)
    report = {"complete": True, "source_programs": len(tests),
              "attempted": len(outcomes), "built": len(outcomes) - failures,
              "build_failures": failures, "infrastructure_failures": failed_support,
              "exit_status": 2 if failed_support else int(bool(failures))}
    (args.output / "build-results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report), flush=True)
    return report["exit_status"]


if __name__ == "__main__":
    sys.exit(main())
