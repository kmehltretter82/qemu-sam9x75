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
The matching glibc ``libc-bin`` package supplies C.UTF-8 locale data; only
those locale files, not its host utilities, are copied into the guest.
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
    PYTHONDONTWRITEBYTECODE=1 python3 "$fixture/test-run-upstream.py"

Public-source boot assets and CI
-------------------------------

``prepare-boot.py`` removes the need for a locally supplied boot image. It
downloads the Linux4Microchip ``linux4microchip-2026.04`` source at immutable
commit ``8f2c610093aa3d5a7bd50a8d0597be4f0b7a3eda`` and BusyBox 1.37.0, checks
their pinned SHA-256 digests before extraction, and builds a SAM9X75 kernel,
the unmodified vendor board DTB and a minimal static ARMv5 musl initramfs.
The kernel starts from ``at91_dt_defconfig``; the extra configuration enables
the RAM filesystems, IPC and timer interfaces required by these tests. It
does not remove board peripherals from the DTB to avoid driver coverage.

Use a Linux build host with Clang/LLVM/LLD, GNU make, a native C compiler,
bc, bison, flex, OpenSSL development headers and Python 3.12 or newer.
Prepare the libc sysroots as above, then run::

    python3 "$fixture/prepare-boot.py" \
        --downloads /tmp/sam9x75-libc-downloads \
        --output /tmp/sam9x75-boot-build \
        --glibc /tmp/sam9x75-libc-sysroots/glibc-sysroot \
        --musl /tmp/sam9x75-libc-sysroots/musl-sysroot

The files under ``boot/`` are ``zImage``, ``board.dtb``,
``initramfs.cpio.gz`` and ``manifest.json``. The manifest records public
source URLs/hashes, compiler version, build commands, configuration hashes
and output hashes. These are reproducible inputs, not a promise of identical
kernel binaries across different toolchain versions. The base initramfs has
canonical root ownership and zero timestamps, contains no credentials or
host files, and requires this fixture's ``/init`` overlay. Builds and result
directories must be new; only the checksum-verified download cache is reused.

``scripts/ci/sam9x75-libc-test.sh`` prepares those assets and runs all 44
contracts inside Linux on ``sam9x75-curiosity``. The GitHub SAM9X75 workflow
runs it after the board qtests, with the guest network and persistent disks
absent. Missing results, a failed case, a host deadline or QEMU diagnostics
fail the job. Build logs and guest evidence are retained for 14 days, also
on failure. Only CI-generated evidence is uploaded automatically; private
local test directories remain local. The 248-case upstream characterization
is separate and is not silently treated as an all-passing CI gate.
The minimal initramfs also includes ``kill``, required by upstream ``popen``
and ``vfork`` cases; missing shell utilities are test-environment failures,
not evidence of libc or emulator defects. Local characterization with this
initramfs reproduced all 248 per-case statuses from the earlier full-rootfs
baseline, including the 25 raw failures described below.

Initial validation on 2026-10-05 passed all 44 contracts with Linux
``7.3.0-rc1+`` on the SAM9X75 machine, using PIT64B as the guest clocksource,
glibc ``2.41-12+deb13u4`` and musl ``1.2.6``. No QEMU diagnostic messages
were produced. This result does not claim an upstream libc/kernel bug was
found or that other operating systems boot on this board.

Upstream functional characterization
------------------------------------

``build-upstream.py`` builds 62 unmodified, single-source functional tests
from upstream ``libc-test`` for the same four ABI/linkage variants: 248 cases
per guest boot. This includes pthread cancellation and cleanup, named
semaphores, SysV IPC, TLS initialization, process spawning, vfork, stdio,
search algorithms, time conversion and software floating-point parsing.
It is a selected functional subset, not the complete upstream API, math,
regression or dynamic-module suite. Source assertions are not patched to
make a particular libc pass.

Create a fresh checkout of the `upstream mirror
<https://repo.or.cz/w/libc-test.git>`_ and detach at the pinned revision::

    git clone https://repo.or.cz/libc-test.git /tmp/sam9x75-libc-test
    git -C /tmp/sam9x75-libc-test checkout --detach \
        7b95dfa5f5d5ca4d949221e0228ccc290bacc14e
    python3 "$fixture/build-upstream.py" \
        --source /tmp/sam9x75-libc-test \
        --glibc /tmp/sam9x75-libc-sysroots/glibc-sysroot \
        --musl /tmp/sam9x75-libc-sysroots/musl-sysroot \
        --output /tmp/sam9x75-upstream-build
    python3 "$fixture/run-upstream.py" \
        --qemu build/qemu-system-arm \
        --kernel /path/to/zImage \
        --dtb /path/to/at91-sam9x75_curiosity.dtb \
        --initramfs /path/to/initramfs-armv5l.cpio.gz \
        --assets /tmp/sam9x75-upstream-build \
        --output /tmp/sam9x75-upstream-result

The builder refuses a dirty checkout or a different revision and records
source hashes, compiler commands and binary/runtime hashes. It keeps all
compiler warnings in ``build.log``; unlike the local contracts, upstream
code is not compiled with ``-Werror``. ``--tests name ...`` selects other
single-source functional cases or a smaller diagnostic set. Cases with
additional ``.mk`` rules are explicitly refused instead of silently
building an incomplete test. Additional cases may need extra guest setup;
for example, socket tests need an enabled loopback interface.

The runner mounts disposable tmpfs filesystems at ``/tmp`` and ``/dev/shm``
and executes every case sequentially with upstream's timeout wrapper.
The default per-case deadline is 30 guest seconds; ``--case-timeout`` accepts
1 through 3600 seconds and records the selected value in the transcript and
results. Allocation-exhaustion tests can need a longer deadline under system
emulation; changing the deadline does not change their loops or assertions.
An independent 600-second host deadline is configurable with ``--timeout``.
Every matching begin/end pair and its exit status is required
exactly once, in manifest order. Missing results, duplicates, inconsistent
final markers, changed assets and QEMU diagnostics cannot produce a pass.

Exit status is 0 only for a complete, all-passing run with an empty QEMU
diagnostic log; 1 means a complete run with raw test failures; 2 means an
incomplete/invalid run or QEMU diagnostics. ``results.json`` preserves
complete failing results and each case's output, or an incomplete-run error.
It does not convert known library differences into successes. Keep generated
metadata and logs private unless their host paths have been reviewed.

Upstream regression characterization
-----------------------------------

``--suite regression`` selects all 68 executable programs in the pinned
regression directory. The additional source file is the original TLS helper
DSO, not a standalone test. Its upstream ``.mk`` selects a dynamic executable
only: the fixture builds that test and DSO for both glibc variants and shared
musl, with the original ``$ORIGIN`` search path, and records the static-musl
exclusion separately. The complete matrix therefore has 271 executable
outcomes, not 272. Other unsupported custom build rules are still refused.
No upstream assertions or stress loops are edited.

Build and run it using the same pinned source checkout and sysroots::

    python3 "$fixture/build-upstream.py" \
        --suite regression --source /tmp/libc-test \
        --glibc /tmp/sam9x75-libc-sysroots/glibc-sysroot \
        --musl /tmp/sam9x75-libc-sysroots/musl-sysroot \
        --output /tmp/sam9x75-regression-build
    python3 "$fixture/run-upstream.py" \
        --qemu build/qemu-system-arm \
        --kernel /path/to/zImage \
        --dtb /path/to/at91-sam9x75_curiosity.dtb \
        --initramfs /path/to/initramfs-armv5l.cpio.gz \
        --assets /tmp/sam9x75-regression-build \
        --output /tmp/sam9x75-regression-result \
        --case-timeout 120 --timeout 2400

Regression programs run as guest UID/GID 1000, without supplementary groups
and with ``no_new_privs`` set. A small, hash-verified static launcher checks
the identity and emits a required marker for every eligible case. Guest
setup remains privileged; the RAM-only working directories have mode 1777.
This is necessary for process-limit tests: root is exempt from Linux's
``RLIMIT_NPROC`` enforcement. No host user database or credential is copied.
The existing functional characterization keeps its original execution mode
and schema-1 manifests remain supported.

The full regression run covers allocation failure, cancellation, thread exit,
signal/fork races, large offsets, stdio, conversion, regex and late-loaded TLS.
Four outcomes from ``fpclassify-invalid-ld80`` return zero through its original
x86-only architecture guard; they are not ARM floating-point coverage. The
ARM EABI used here has 53-bit long-double precision, not x86's 64-bit precision.
Success statuses and raw failures remain distinct from test applicability,
fixture errors and incomplete runs. These tests are not a complete upstream
libc API/math suite or a hardware-fidelity claim.
Timeouts remain raw failed outcomes. Emulation speed, host load and guest
address-space layout can affect exhaustion-test duration; a timeout alone
does not establish a libc or QEMU correctness defect.

On 2026-10-05 the full unprivileged matrix completed on the official
Linux4Microchip ``6.18.17-linux4microchip-2026.04`` kernel with 260 zero-exit
outcomes and 11 raw failures, using a 120-second per-case deadline and an
empty QEMU diagnostic log. All 135 eligible musl outcomes and all three
late-loaded TLS cases returned zero. Ten failures were the two glibc variants
of ``daemon-failure``, ``dn_expand-empty``, ``dn_expand-ptr-0``,
``regex-ere-backref`` and ``regex-escaped-high-byte``; these remain library/test
contract mismatches, not automatically QEMU bugs. The remaining outcome was
a ``glibc-time64/malloc-brk-fail`` timeout and remains unresolved. The 260
zero-exit outcomes include the four x86-guarded no-ops noted above. No failing
status is waived and this is not an all-passing conformance gate.

Interpreting failures
~~~~~~~~~~~~~~~~~~~~

A raw failure is not automatically an emulator bug. The pinned versions
already show several library or test-contract differences:

* ``clocale_mbfuncs`` expects musl's treatment of all byte values in the C
  locale; glibc has different C-locale character semantics.
* ``fnmatch`` includes malformed patterns with unspecified behavior.
* ``strtol``/``wcstol`` assume an invalid-base call changes ``endptr``;
  the observed glibc behavior leaves it unchanged.
* Legacy time32 ``utime`` fails an explicit 64-bit ``time_t`` assertion.
* Some glibc ``sscanf``/``fscanf``/``fwscanf`` failures correspond to
  `glibc bug 12701
  <https://sourceware.org/pipermail/glibc-bugs/2025-March/059178.html>`_.
* The nonblocking ``pthread_join`` cancellation scenario differs in glibc;
  preserve it for further triage rather than claiming a new QEMU defect.
* ``strftime`` includes year-padding and extended-format expectations
  which differ between these libraries.
* Musl ``strptime`` deliberately differs for ``%s`` and short ``%z``;
  this exact test mismatch was `discussed upstream in 2025
  <https://www.openwall.com/lists/musl/2025/08/20/2>`_.
* Musl 1.2.6 ``strtod``/``strtold`` reproduce a known hexadecimal rounding
  issue for 53-bit long double, explained in the `upstream fix discussion
  <https://www.openwall.com/lists/musl/2026/05/21/5>`_.

None of these annotations suppresses a failing status. An independently
prepared sysroot without UTF-8 locale data may also fail ``swprintf`` because
its test environment is incomplete; the pinned preparation supplies that data.

On 2026-10-05, the 248-case set completed with 223 passes and 25 raw failures
on both Linux ``7.3.0-rc1+`` and the official Linux4Microchip
``6.18.17-linux4microchip-2026.04`` kernel. Both QEMU diagnostic logs were
empty. These are characterization results, not a passing conformance gate.
An independent focused run of ``strtod``, ``strtold``, ``strtod_simple`` and
``strtof`` passed all 16 cases with the known upstream musl rounding fix
backported into a disposable sysroot, without changing QEMU or test assertions.
