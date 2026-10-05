# SPDX-License-Identifier: GPL-2.0-or-later
"""Pinned upstream test selection, including dynamic-only build rules."""

import re

from libc_build import VARIANTS

SOURCE_COMMIT = "7b95dfa5f5d5ca4d949221e0228ccc290bacc14e"
SUITES = ("functional", "regression")
TLS_TEST = "tls_get_new-dtv"
TLS_DSO = "tls_get_new-dtv_dso.so"
LAUNCHER = "tools/run-unprivileged"
RUN_AS = {"uid": 1000, "gid": 1000, "groups": [], "no_new_privs": True}
IDENTITY = "UPSTREAM_RUN_AS uid=1000 gid=1000 groups=0 no_new_privs=1"


def check_tests(suite, tests):
    if (suite not in SUITES or not isinstance(tests, list) or not tests or
            any(not isinstance(name, str) or
                not re.fullmatch(r"[a-z][a-z0-9_-]*", name) or
                name in ("runtest", "tls_get_new-dtv_dso") for name in tests) or
            len(set(tests)) != len(tests)):
        raise ValueError("Invalid or duplicate upstream tests")


def selection(suite, tests):
    """Respect the pinned regression .mk: its TLS DSO test is dynamic only."""
    check_tests(suite, tests)
    dynamic_only = suite == "regression" and TLS_TEST in tests
    variants = {
        variant: [name for name in tests
                  if not (dynamic_only and variant == "musl-static" and
                          name == TLS_TEST)]
        for variant in VARIANTS
    }
    helpers = ([f"{variant}/{TLS_DSO}" for variant in VARIANTS
                if variant != "musl-static"] if dynamic_only else [])
    if suite == "regression":
        helpers.append(LAUNCHER)
    excluded = ([{"variant": "musl-static", "test": TLS_TEST,
                  "reason": "Upstream .mk selects a dynamic executable only"}]
                if dynamic_only else [])
    return variants, helpers, excluded


def manifest_selection(manifest):
    if (not isinstance(manifest, dict) or
            manifest.get("variants") != list(VARIANTS)):
        raise ValueError("Unsupported upstream build manifest")
    schema, kind = manifest.get("schema"), manifest.get("kind")
    if schema == 1 and kind == "upstream-functional":
        suite = "functional"
    elif (schema == 2 and kind == "upstream-regression" and
          manifest.get("source_commit") == SOURCE_COMMIT):
        suite = "regression"
    else:
        raise ValueError("Unsupported upstream build manifest")
    tests = manifest.get("tests")
    variants, helpers, excluded = selection(suite, tests)
    if schema == 2 and any(manifest.get(key) != expected for key, expected in (
            ("variant_tests", variants), ("support_assets", helpers),
            ("excluded_cases", excluded), ("run_as", RUN_AS))):
        raise ValueError("Manifest does not match upstream build rules")
    return suite, tests, variants, helpers, excluded
