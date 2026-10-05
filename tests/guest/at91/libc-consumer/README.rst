SAM9X75 Linux libc consumer gate
===============================

This fixture runs real ARMv5TE, soft-float userland on
``-M sam9x75-curiosity``. It does not use ``qemu-user`` or substitute an ARM
``virt`` board. The guest has no network or persistent drives; its initramfs
and sparse test files exist only in RAM. No physical board is contacted.

The four variants are glibc with its default 32-bit ``time_t``, glibc with
``_TIME_BITS=64``, dynamically linked musl, and statically linked musl. All use
64-bit file offsets. Each runs eleven named functional groups:

* Memory copies/moves, offsets and valid accesses ending at a page boundary.
* Allocation, zero initialization, reallocation and aligned allocation.
* Formatting, integer conversion, C-locale wide characters and regular expressions.
* Software floating-point math and 64-bit division/remainder.
* Four threads, mutex/condition synchronization, TLS and key destructors.
* Alternate-stack signal delivery, signal masks and ``sigwait``.
* ``fork``, pipes, ``poll`` and child exit status.
* Monotonic clock resolution and ``nanosleep``.
* ``eventfd``, ``epoll`` and ``timerfd``.
* Sparse 5 GiB file offsets and file timestamps (2037 for time32, 2040 for time64).
* Kernel-backed ``getrandom``.

These are targeted contracts, not the complete upstream glibc or libc-test
suites, a conformance certification, or evidence of physical timing fidelity.
Passing one kernel configuration does not establish all supported kernel
versions. The tests never change the realtime clock.

Preparing and building
----------------------

Requirements: Python 3.9+, Clang with ARM support, LLD, LLVM ar/ranlib, make,
tar and cpio. Native Linux and macOS hosts can cross-build; Docker is not
required. The pinned assets are Debian armel glibc 2.41 packages, GCC 12
soft-float runtime support and Linux UAPI headers, plus upstream musl 1.2.6.
Do not use armhf/ARMv7 sysroots for this ARM926 CPU.

From the QEMU checkout, with Clang/LLD/LLVM tools on ``PATH``::

    fixture=tests/guest/at91/libc-consumer
    python3 "$fixture/prepare-sysroots.py" \
        --downloads /tmp/sam9x75-libc-downloads \
        --output /tmp/sam9x75-libc-sysroots
    python3 "$fixture/build-libc.py" \
        --glibc /tmp/sam9x75-libc-sysroots/glibc-sysroot \
        --musl /tmp/sam9x75-libc-sysroots/musl-sysroot \
        --output /tmp/sam9x75-libc-build

Preparation validates pinned SHA-256 digests before extraction. These are
reproducibility pins for the retrieved HTTPS artifacts, not verification of
Debian release signatures or a substitute for the distributor's trust chain.
Musl's prefix and dynamic-loader installation directory are both confined to
the selected output; nothing is installed into the host's ``/lib``. Existing
output directories are refused. ``--cc``, ``--ar`` and ``--ranlib`` can select
explicit tool paths. Failed-build logs remain in ``musl-build.log``.

``build-libc.py`` also accepts independently prepared compatible sysroots.
Use ``--gcc-install`` if more than one GCC runtime directory is present.
``-fno-builtin`` keeps memory operations as library calls. A manifest records
compiler version, build commands and binary/runtime-library hashes. Keep
generated sysroots, binaries and evidence outside the source checkout.

Booting the actual SAM9X75 machine
---------------------------------

Supply a SAM9X75 Linux zImage, matching board DTB and a small BusyBox
initramfs containing ``/bin/busybox`` and ``/bin/sh``. The kernel needs ELF
and initramfs support, devtmpfs, tmpfs, futexes, ARM kuser helpers, time32
compatibility, timerfd, eventfd and epoll. For example::

    python3 "$fixture/run-libc.py" \
        --qemu build/qemu-system-arm \
        --kernel /path/to/zImage \
        --dtb /path/to/at91-sam9x75_curiosity.dtb \
        --initramfs /path/to/initramfs-armv5l.cpio.gz \
        --assets /tmp/sam9x75-libc-build \
        --output /tmp/sam9x75-libc-result

The runner validates asset hashes, overlays its own ``/init``, and requires
all 44 named results, the expected time/file ABI widths and zero exit status
for each variant. A final marker alone cannot pass the gate. It also requires
an empty QEMU ``unimp,guest_errors`` log. A timeout or failure preserves
``serial.log``, ``qemu.log`` and input/build metadata for diagnosis.
``results.json`` is written only after the complete gate passes. Local paths
in generated metadata can be private; do not publish them without review.

Host-only validator tests::

    PYTHONDONTWRITEBYTECODE=1 python3 "$fixture/test-run-libc.py"

Initial validation on 2026-10-05 passed all 44 contracts with Linux
``7.3.0-rc1+`` on the SAM9X75 machine, using PIT64B as the guest clocksource,
glibc ``2.41-12+deb13u4`` and musl ``1.2.6``. No QEMU diagnostic messages
were produced. This result does not claim an upstream libc/kernel bug was
found or that other operating systems boot on this board.
