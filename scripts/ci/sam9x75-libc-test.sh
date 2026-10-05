#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

source_dir=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
build_dir=${SAM9X75_BUILD_DIR:-"$source_dir/build-sam9x75"}
guest_dir=${SAM9X75_GUEST_DIR:-"$source_dir/build-sam9x75-libc"}
fixture="$source_dir/tests/guest/at91/libc-consumer"
jobs=${SAM9X75_GUEST_JOBS:-4}
export PYTHONDONTWRITEBYTECODE=1

# Every build/result directory must be new. Only downloads may be reused;
# their pinned digests are checked again before extraction.
python3 "$fixture/prepare-sysroots.py" \
    --downloads "$guest_dir/downloads" --output "$guest_dir/sysroots" --jobs "$jobs"
python3 "$fixture/prepare-boot.py" \
    --downloads "$guest_dir/downloads" --output "$guest_dir/boot-build" \
    --glibc "$guest_dir/sysroots/glibc-sysroot" \
    --musl "$guest_dir/sysroots/musl-sysroot" --jobs "$jobs"
python3 "$fixture/build-libc.py" \
    --glibc "$guest_dir/sysroots/glibc-sysroot" \
    --musl "$guest_dir/sysroots/musl-sysroot" --output "$guest_dir/contracts"
python3 "$fixture/run-libc.py" \
    --qemu "$build_dir/qemu-system-arm" \
    --kernel "$guest_dir/boot-build/boot/zImage" \
    --dtb "$guest_dir/boot-build/boot/board.dtb" \
    --initramfs "$guest_dir/boot-build/boot/initramfs.cpio.gz" \
    --assets "$guest_dir/contracts" --output "$guest_dir/results" --timeout 240
