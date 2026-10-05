#!/bin/sh

set -eu

source_dir=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
build_dir=${SAM9X75_BUILD_DIR:-"$source_dir/build-sam9x75"}

PYTHONDONTWRITEBYTECODE=1 python3 \
    "$source_dir/tests/guest/at91/libc-consumer/test-run-libc.py"
PYTHONDONTWRITEBYTECODE=1 python3 \
    "$source_dir/tests/guest/at91/libc-consumer/test-run-upstream.py"

mkdir -p "$build_dir"
(
    cd "$build_dir"
    "$source_dir/configure" \
        --target-list=arm-softmmu \
        --enable-werror \
        --disable-docs \
        "$@"
)

ninja -C "$build_dir" \
    qemu-system-arm \
    tests/qtest/sam9x75-curiosity-test \
    tests/qtest/sam9x75-lan8840-eeprom-test \
    tests/qtest/sam9x7-adc-test \
    tests/qtest/sam9x7-pwm-test

if ! "$build_dir/pyvenv/bin/meson" test \
    -C "$build_dir" \
    --print-errorlogs \
    --no-rebuild \
    --timeout-multiplier 4 \
    'qemu:qtest-arm/sam9x75-curiosity-test' \
    'qemu:qtest-arm/sam9x75-lan8840-eeprom-test' \
    'qemu:qtest-arm/sam9x7-adc-test' \
    'qemu:qtest-arm/sam9x7-pwm-test'; then
    # Meson's TAP error summary can omit the last successful test and the
    # command that started the failing guest. Preserve that context in CI.
    if test -f "$build_dir/meson-logs/testlog.txt"; then
        tail -n 400 "$build_dir/meson-logs/testlog.txt"
    fi
    exit 1
fi
