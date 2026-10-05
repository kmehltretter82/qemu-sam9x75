#!/bin/sh

set -eu

source_dir=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
build_dir=${SAM9X75_BUILD_DIR:-"$source_dir/build-sam9x75"}

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

"$build_dir/pyvenv/bin/meson" test \
    -C "$build_dir" \
    --print-errorlogs \
    --no-rebuild \
    --timeout-multiplier 4 \
    'qemu:qtest-arm/sam9x75-curiosity-test' \
    'qemu:qtest-arm/sam9x75-lan8840-eeprom-test' \
    'qemu:qtest-arm/sam9x7-adc-test' \
    'qemu:qtest-arm/sam9x7-pwm-test'
