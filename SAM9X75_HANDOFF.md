# SAM9X75 Curiosity QEMU continuation handoff

Last updated: 2026-08-28 UTC

This is the entry point for another development agent.  It records the exact
checkpoint, what was actually validated, what must be repeated next, where the
larger backlog lives, and which physical-board experiments are unsafe.  Keep
this file current whenever a new checkpoint is committed.

## Canonical repository and checkpoint

- Published repository: <https://github.com/kmehltretter82/qemu-sam9x75>
- Published branch: `main`
- Local source tree: `/home/karl/linux-work/qemu-SAM9X75/qemu`
- Local development branch: `sam9x75-curiosity`
- Last implementation checkpoint: `dff47e3418`
  (`hw/char: Pace AT91 USART receive characters`)
- Preceding generic QEMU fix: `569ca3a66d`
  (`chardev: Avoid unregistering yank after failed reconnect`)
- Latest tested repository checkpoint: `95ba1ee289`
  (`docs: record SAM9X75 long CAN gates`)
- `build-verify-serial/qemu-system-arm --version` reports exact source
  `v11.1.0-417-g1851743e84`; commits through the repository checkpoint after
  that implementation source change only documentation and test profiles.

Do not reset to an older UDPHS or crypto checkpoint; continue from the tip of
`main` after fetching.  Verify that all three commits above are present in the
published history before starting a new implementation slice.

On this workstation the only expected untracked item at handoff time was
`build-verify-serial/`.  It is a build directory, not source, and must never be
staged.  Existing files below `/home/karl/linux-work/qemu-SAM9X75/t` and
`artifacts` contain disposable images and test evidence; they are not all part
of Git.

Resume an existing checkout with:

```sh
cd /home/karl/linux-work/qemu-SAM9X75/qemu
git fetch sam9x75
git switch sam9x75-curiosity
git merge --ff-only sam9x75/main
git status --short
git log -5 --oneline
```

For a new checkout, clone `qemu-sam9x75`, use `main`, and create a local topic
branch before editing.  Preserve any unrelated local changes and never stage a
build directory or workspace test artifact.

## What is validated at this checkpoint

The QEMU implementation at `1851743e84` compiled cleanly in
`build-verify-serial`.

- The complete SAM9X75 Curiosity qtest binary passed **238/238**.
- The complete generic chardev unit binary passed **42/42**.  The new TCP and
  Unix reconnect cases prove that a failed reconnect after a previous client
  closes no longer tries to unregister an unregistered yank callback; the old
  code deterministically aborted in this test.
- The LAN8840 EEPROM and separate SAM9X7 ADC suites passed **7/7** and
  **9/9** respectively.  The focused UDPHS subgroup passed **20/20**.
- The UART partner host suite passed **16/16**, and the network partner suite
  passed **22/22** after adding the independently configurable TCP frame-idle
  deadline.
- The paced USART qtests cover two queued backend bytes separated by one exact
  programmed character interval and migration of a half-expired receive
  timer with a pending destination byte.
- `git diff --check` was clean before the documentation update.
- The exact Linux4Microchip 2026.04 UDPHS gadget-serial loop remains validated
  from its earlier exact-head run: two fresh sessions were separated by
  `g_serial` removal and reload.  All four endpoints passed TAP `1..7`, 27 DATA
  frames and 27 acknowledgements, the 65,536-byte boundary and 210,478 received
  wire bytes
  with no protocol error.  USB device number 3 disappeared and re-enumerated
  as 4; UHPHS and UDPHS interrupts advanced in both sessions, Linux's global
  error count stayed zero, and QEMU's `unimp,guest_errors` log was empty.
- A Linux4Microchip kernel containing the separately packaged Atmel SHA and
  TDES fixes passed the full crypto release profile: four iterations, three
  workers and requests through 65,536 bytes.  TAP was 6/6 for 85 SHA, 34 HMAC,
  32 AES and 12 TDES jobs; XDMAC advanced by 8,808 and SHA by 10,034
  interrupts; QEMU's `unimp,guest_errors` log was empty.
- The full multi-subsystem Linux4Microchip 2026.04 release gate passed at
  `1851743e84`.  Aggregate TAP was **23/23**: two simultaneous 2 MiB-per-
  direction GEM streams plus 128 UDP packets; 1,000 bidirectional stress
  frames through each of `can0` and `can1`; the full 27-frame UART boundary
  matrix through 65,536 bytes with six induced backpressure pauses; two
  workers over 85 SHA, 34 HMAC, 32 AES and 12 TDES jobs; and deterministic
  CPU, memory and filesystem integrity loads.  The guest, network peer and
  UART peer all exited zero.  QEMU's `unimp,guest_errors` log was empty, all
  reports were copied to a separate USB ext4 disk, the disk was cleanly
  unmounted, and host `e2fsck -fn` passed.
- The authoritative combined evidence is workspace-local at
  `/home/karl/linux-work/qemu-SAM9X75/t/linux4sam-combined-20260827/release-r4/`.
  The exact kernel is
  `/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-tdes-diag-build-20260827/arch/arm/boot/zImage`
  and the CAN-enabled DTB is
  `/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-2026.04-can01.dtb`.
- The larger UHPHS Linux4Microchip gate passed at repository checkpoint
  `9f424139aa`.  A fresh 768 MiB ext4 disk carried a deterministic 256 MiB
  file across three high-speed enumerations.  All three SHA-256 values were
  `486cc817b95d853d3c357ff283b204c0144bd255e73fe2deb1389493b257e3c0`.
  One clean detach/re-attach preserved the hash and a rename; a second detach
  occurred only after the guest had opened the unmounted raw block device and
  produced the required `EIO`.  The final re-attach preserved the hash,
  UHPHS reached 45,877 interrupts, QEMU's diagnostic log was exactly empty,
  and host `e2fsck -fn` passed.
- Preserve the authoritative UHPHS evidence at
  `/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-uhphs-20260827/release-r4/`
  and the separately packaged Linux EHCI fix at
  `/home/karl/linux-work/qemu-SAM9X75/artifacts/linux4microchip-ehci-fix-20260827/`.
- A preceding run exposed a generic writable-vvfat assertion in
  `block/vvfat.c:2758` after the guest created and renamed a result tree.  This
  is not a SAM9X75 USB-model failure.  Keep directory-export payloads
  read-only and use a separate disposable raw ext4 result disk as in
  `release-r4`.
- The full firmware-chain/GEM gate passed from repository checkpoint
  `9665c77235` using the same machine binary at `1851743e84`.  AT91Bootstrap
  4.0.13 loaded U-Boot through SD/ADMA; U-Boot initialized DDR, NAND, MMC,
  QSPI and GEM and verified every selected FIT hash; Linux mounted
  `/dev/mmcblk0p2` as ext4 `/`.  Guest TAP **11/11** and host TAP **1/1**
  covered DHCP, LAN8840 carrier, route, ping, 1 MiB deterministic TCP in each
  direction and 64 bidirectional UDP packets with no retry, stale response,
  error or drop.  QEMU's diagnostic log was empty, the evidence filesystem
  passed `e2fsck -fn`, and the snapshot-backed SD hash remained unchanged.
- The authoritative firmware-chain evidence is workspace-local at
  `/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-firmware-gem-20260827/release-r4/`.
- At repository checkpoint `4ab0af5c6a`, the standalone Linux4Microchip CAN
  semantic gate passed **10,000** simultaneous 64-byte CAN-FD/BRS stress
  frames in each direction between `can0` and `can1`, in addition to all 86
  classic, RTR and CAN-FD boundary cases in each direction.  Both endpoints
  passed TAP 5/5 and attested their final counters.  Each sent, received and
  acknowledged exactly 10,000 stress frames with zero gap, duplicate,
  corruption, stale-session, foreign-frame, CAN error/drop or SocketCAN queue
  overflow.  Interrupt counts reached 40,353/40,354, QEMU's diagnostic log
  was empty, the result ext4 filesystem passed host `e2fsck -fn`, and the
  snapshot-backed root overlay hash was unchanged.  Preserve the authoritative
  evidence at
  `/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-can-20260827/release-r2/`;
  `release-r1` is diagnostic because its host validator mishandled the
  report's fixed-width session-ID formatting.
- A separate userspace interoperability boot at the same QEMU checkpoint ran
  Linux4Microchip's can-utils 2023.03 `canfdtest`.  The classic 8-byte profile
  and the 64-byte CAN-FD/BRS profile each reported exactly 10,000 messages
  sent and received.  Both generators exited zero, each controller ended with
  20,030 RX and TX packets, zero errors/drops and `ERROR-ACTIVE` state, QEMU's
  diagnostic log was empty, `e2fsck -fn` passed and the root overlay was
  unchanged.  Preserve
  `/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-canfdtest-20260827/release-r1/`.
- Both new gates deliberately use one internal QEMU CAN bus with the second
  Linux M_CAN as the peer.  They do not prove `can-host-socketcan`, ESI
  injection or two isolated external paths.  Those remain a separate host
  `vcan` gate requiring the host `vcan` module and permission to create two
  network interfaces.
- Quiescent whole-machine migration passed at repository checkpoint
  `95ba1ee289`.  Both Linux M_CAN endpoints completed all 86 boundary cases,
  then stopped at the fixture's application barrier with no semantic frame in
  flight.  QMP migrated the running 256 MiB machine to a 56,056,437-byte file
  in 425 ms with 22 ms reported downtime.  The source exited, an identical
  destination restored the state and accepted serial-console input, and only
  then was the stress phase released.  Both restored endpoints passed TAP 5/5
  and exactly 10,000 sent, received and acknowledged frames, with zero
  integrity error, CAN error/drop or receive-queue overflow.  Interrupts
  reached 40,353/40,354.  Both QEMU diagnostic logs were empty, the evidence
  ext4 disk passed `e2fsck -fn`, the disposable root qcow2 passed
  `qemu-img check`, and the original root-overlay hash was unchanged.  Preserve
  `/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-migration-20260828/release-r4/`.
  Runs r1--r3 are diagnostics for, respectively, managed-sandbox QMP socket
  denial, QEMU's read-only SD-card rejection and missing mount directories on
  the deliberately read-only guest root.

## Immediate continuation order

Do these steps before starting another device model or another operating
system.

1. Extend the remaining P0 external-path coverage with independent host
   connections for both CAN controllers.  Repeat the now-green 10,000-frame
   semantic and separate `canfdtest` profiles over those isolated SocketCAN
   paths, retaining ESI injection on the virtual host peers.
2. Extend the now-green quiescent whole-machine migration gate to carefully
   controlled in-flight migration.  The USART receive timer itself already
   has qtest migration coverage; preserve the quiescent release-r4 baseline.
3. In parallel, the physical-board agent can run the safe crypto validation in
   `artifacts/linux4microchip-crypto-fixes-20260827/REAL-HARDWARE-TESTS.md`.
4. Only after the remaining integration gates are green, take the next bounded
   implementation slice.  Keep Linux4Microchip as the primary OS; NetBSD,
   FreeBSD and other guests and the separate newer-mainline matrix are
   deliberately deferred.

### Focused build and host tests

Keep temporary files on the disk-backed project volume, not in `/tmp`:

```sh
cd /home/karl/linux-work/qemu-SAM9X75/qemu
mkdir -p /home/karl/linux-work/qemu-SAM9X75/t/qtest-tmp

# Recreate this build only if it does not already exist:
# mkdir build-verify-serial
# cd build-verify-serial
# ../configure --target-list=arm-softmmu --enable-debug --disable-werror
# cd ..

ninja -C build-verify-serial \
  qemu-system-arm tests/qtest/sam9x75-curiosity-test \
  tests/qtest/sam9x75-lan8840-eeprom-test \
  tests/qtest/sam9x7-adc-test

env TMPDIR=/home/karl/linux-work/qemu-SAM9X75/t/qtest-tmp \
  QTEST_QEMU_BINARY=./build-verify-serial/qemu-system-arm \
  ./build-verify-serial/tests/qtest/sam9x75-curiosity-test \
  -p /arm/sam9x75/udphs

python3 -m py_compile \
  tests/guest/at91/linux4sam/uart-partner/sam9x75_uart_partner.py \
  tests/guest/at91/linux4sam/uart-partner/test_sam9x75_uart_partner.py

python3 -m unittest discover \
  -s tests/guest/at91/linux4sam/uart-partner \
  -p 'test_*.py' -v
```

The UART suite uses local sockets and pseudo-terminals.  A restricted sandbox
may need explicit permission for those resources; that is not a reason to skip
the suite.

For the full SAM9X75/SAM9X7 regressions, omit the UDPHS path filter and then
run the LAN8840 and ADC binaries:

```sh
env TMPDIR=/home/karl/linux-work/qemu-SAM9X75/t/qtest-tmp \
  QTEST_QEMU_BINARY=./build-verify-serial/qemu-system-arm \
  ./build-verify-serial/tests/qtest/sam9x75-curiosity-test

env TMPDIR=/home/karl/linux-work/qemu-SAM9X75/t/qtest-tmp \
  QTEST_QEMU_BINARY=./build-verify-serial/qemu-system-arm \
  ./build-verify-serial/tests/qtest/sam9x75-lan8840-eeprom-test

env TMPDIR=/home/karl/linux-work/qemu-SAM9X75/t/qtest-tmp \
  QTEST_QEMU_BINARY=./build-verify-serial/qemu-system-arm \
  ./build-verify-serial/tests/qtest/sam9x7-adc-test
```

At the validated source the Curiosity binary reports 238 tests.  Record the
resulting count on every later revision rather than assuming it remains 238.

### Exact Linux4Microchip UDPHS gate

The authoritative procedure is the **QEMU UDPHS gadget-serial self-loop**
section of:

- `tests/guest/at91/linux4sam/uart-partner/README.rst`

It connects the modeled UDPHS gadget to UHPHS Port B using
`at91-udphs-gadget`, loads Linux `g_serial`, binds the host side through
`cdc_acm`, and validates `/dev/ttyGS0` against the identity-discovered
`ttyACM` endpoint.  Both TAP plans, both JSON reports, interrupt deltas,
post-test `dmesg`, and the QEMU diagnostic log are release artifacts.

The current two-session passing evidence, frozen binary, input hashes,
Expect-based launch recipe and result summary are workspace-local at:

```text
/home/karl/linux-work/qemu-SAM9X75/t/udphs-gserial-e2e-20260827/current-main-8af5934947-two-session/
```

For a future source revision, copy only the runner, guest helper and current
tracked fixture into a new, empty evidence directory.  Never overwrite the
passing result or copy its ``payload/results`` directory.  Freeze and hash the
newly built QEMU binary before boot.

The current runner uses VID:PID `0525:a4a7` identity discovery, fresh session
IDs, proof of a complete detach interval, and a changed USB device number on
reload.  Preserve those gates.  Archive `git rev-parse HEAD`, the complete
QEMU command line, image hashes, guest `uname -a`, TAP, JSON, interrupt
snapshots, new `dmesg` lines and `qemu.log`.  Do not report a pass merely
because both TTY devices appeared.

## Linux4Microchip images and local evidence

The primary guest is Linux4Microchip 2026.04 with Linux 6.18.17.  Useful
workspace-local inputs include:

```text
/home/karl/linux-work/qemu-SAM9X75/artifacts/linux4microchip-2026.04/linux4microchip-buildroot-sam9x75_curiosity-headless-2026.04.img
/home/karl/linux-work/qemu-SAM9X75/artifacts/linux4microchip-2026.04/linux4microchip-buildroot-sam9x75_curiosity-headless-2026.04-qemu-1g.img
/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-2026.04-official-1g-overlay.qcow2
/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-2026.04-zImage
/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-2026.04-base.dtb
/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-2026.04-rootfs.ext4
/home/karl/linux-work/qemu-SAM9X75/t/at91bootstrap-v4.0.13-prosd-validation-20260826/build/binaries/sam9x7-sdcardboot-uboot-4.0.13.elf
/home/karl/linux-work/qemu-SAM9X75/t/LINUX4SAM_USERSPACE_STRESS_MATRIX_20260826.md
/home/karl/linux-work/qemu-SAM9X75/artifacts/linux4microchip-crypto-fixes-20260827/
/home/karl/linux-work/qemu-SAM9X75/artifacts/linux4microchip-ehci-fix-20260827/
```

The 554,696,704-byte plain `.img` is the immutable official image, the
`-qemu-1g.img` file is a derived 1-GiB raw image, and the `.qcow2` file is a
working overlay.  Record and verify the selected file's hash for every run.
Never write the official image.  Attach either reusable raw image and the
reusable qcow2 overlay with `snapshot=on`; only newly created, explicitly
disposable test media may be persistently writable.  The stress matrix
documents available userspace programs and the ordered P0/P1/P2 consumers.
It is workspace evidence rather than a tracked repository file, so its
essential priorities are repeated below.

## Where the durable backlog is documented

Read these tracked files in this order:

1. `docs/system/arm/sam9x75-curiosity.rst`
   - the **Support matrix** records implemented and missing behavior for every
     modeled block;
   - **Execution roadmap** gives the ordered whole-project phases;
   - **Current invocation** gives supported boot and attachment examples;
   - **Completion gates** lists the remaining board-level integration gates.
2. `tests/guest/at91/linux4sam/uart-partner/README.rst`
   - UART, USB-serial and UDPHS-to-UHPHS consumers, reconnect and migration
     barriers, and the physical-cable comparison.
3. `tests/guest/at91/linux4sam/stress-runner/README.rst`
   - concurrent GEM, CAN, UART, crypto, CPU, memory and filesystem pressure.
4. `tests/guest/at91/linux4sam/crypto-consumer/README.rst`
   - exact AF_ALG/XDMAC coverage and the Linux `atmel-sha` state-restoration
     and upstream `atmel-tdes` uninitialized-allocation bugs; both fixes still
     need physical-board confirmation.
5. `tests/guest/at91/linux4sam/network-partner/README.rst` and
   `tests/guest/at91/linux4sam/can-partner/README.rst`
   - real external peers and data-integrity oracles.
6. `tests/guest/at91/linux4sam/spi-consumer/README.rst`
   - the pending SD-over-SPI end-to-end gate and its two explicit blockers.

The machine document's final **Completion gates** paragraph predates some of
the newer CAN and USB evidence and still names those areas generically.  Use
the detailed support-matrix rows and consumer READMEs to decide which specific
sub-gates remain; do not interpret that paragraph as saying no CAN or USB
integration has passed.

The dated hardware package is outside the Git repository:

```text
/home/karl/linux-work/qemu-SAM9X75/sam9x75-hardware-followup-20260825-r5/
/home/karl/linux-work/qemu-SAM9X75/sam9x75-hardware-followup-20260825-r5.tar.gz
```

Use revision **r5**, never merge instructions from r2/r3/r4.  Its code head
`af1373de36a2` is older than current `main`, so use it for sanitized physical
evidence, safety rules and test design—not as the current software status.
Reconcile every expected value against the current machine documentation and
source before a board run.

Its `UPSTREAM_BUGS.md` preserves the classification between generic QEMU bugs
and SAM9X7-specific findings, including the hardware-confirmed generic SDHCI
ADMA NOP issue.

## Prioritized remaining work

### P0: current Linux4Microchip integration

- The larger UHPHS writable-media and hotplug/error gate is achieved.  The
  authoritative `release-r4` used a fresh 768 MiB ext4 disk, preserved one
  deterministic 256 MiB payload hash across three high-speed enumerations,
  returned `EIO` for removal during a proven-active raw read while unmounted,
  recovered the same hash, and passed host `e2fsck -fn`.  Preserve the raw
  evidence and use a new disposable disk for any extension.
- The concurrent GEM/LAN8840, CAN-FD, FLEXCOM UART, crypto, CPU, memory and
  filesystem gate is achieved at `1851743e84`.  Preserve `release-r4` and its
  independent host/guest TAP and JSON.  The two-iteration/two-worker crypto
  entry remains a combined-contention profile and does not replace the
  standalone four-iteration/three-worker/65,536-byte crypto release gate.
- The standalone Linux4Microchip CAN fixture passed 10,000 frames per role on
  one shared internal bus, and a separate `canfdtest` boot passed 10,000
  classic plus 10,000 64-byte CAN-FD/BRS exchanges.  Next add independent host
  connections for both controllers and repeat both profiles through
  `can-host-socketcan`.  Do not use the shared bus as proof of two independent
  external paths, and do not request ESI injection from the Linux M_CAN used
  as its peer; retain ESI for virtual host peers.
- The current-head firmware-to-disk-root plus GEM gate is achieved at
  repository checkpoint `9665c77235`.  Preserve
  `linux4microchip-firmware-gem-20260827/release-r4`; do not confuse its
  `-kernel` entry into AT91Bootstrap with the still-unimplemented mask-ROM
  media-selection state machine.
- Add/reset/migrate tests for any divergence found.  Fix the model, not the
  guest device tree, unless the same issue is proven to be a Linux bug.

### P1: bounded QEMU improvements

- Complete UDPHS `NB_TRANS`, isochronous DATAX/MDATA termination,
  SOF/suspend/resume timing and SAM-BA, then extend the achieved
  Linux4Microchip quiescent whole-machine migration gate to controlled
  in-flight traffic while retaining and extending the existing low-level
  migration qtests.  The current raw-token bridge is a
  development topology; a general host-facing cable backend remains missing.
- Close UHPHS reset, port-power, hotplug, DMA-error and interrupt fidelity with
  Linux storage and input devices.
- Finish the SD-over-SPI consumer.  Its tracked README records two blockers:
  PA13 GPIO chip-select routing in the board model, and the real FLEXCOM4
  `SPI_VERSION` readback needed before modeling Linux capability selection.
- Continue remaining XDMAC request wiring and arbitration/QOS semantics, then
  the storage, boot-ROM, expansion and multimedia/security phases in the
  machine document.  Take one reviewable slice at a time with reset, negative,
  migration and guest coverage.

### P1: known Linux work, kept separate from QEMU

- Generic Linux `ehci_bus_resume()` writes `CTRLDSSEGMENT` even when
  `HCCPARAMS` does not advertise 64-bit addressing.  EHCI 1.0 forbids that
  access; the initial run path already guards it.  Applying the same
  capability guard to resume removed two QEMU guest-error diagnostics and
  passed the full UHPHS release gate.  Upstream Linux master inspected on
  2026-08-27 still has the bug.  The patch, exact config, fixed `zImage` and
  evidence map are in
  `/home/karl/linux-work/qemu-SAM9X75/artifacts/linux4microchip-ehci-fix-20260827/`.
  Do not silence QEMU's standards-based diagnostic.
- The Linux4Microchip `atmel-sha` concurrent HMAC state-restoration fix must be
  validated on physical silicon before upstream submission.  It already
  passes the complete QEMU AF_ALG release profile.
- The TDES stall is classified as an upstream Linux bug.  Commit
  `7608a43d8f2e` accidentally changed zeroed allocation to
  `devm_kmalloc()`, leaving `TDES_FLAGS_BUSY` and DMA configuration
  uninitialized.  The one-line `devm_kzalloc()` fix passed the targeted
  two-worker run and the complete QEMU release profile; Linux 7.2 still has
  the bug.  Run the packaged 20-cold-boot silicon repeatability gate, then
  prepare upstream submission.
- The two-patch series, fixed `zImage`, diagnosis, QEMU evidence and physical
  procedure are in
  `/home/karl/linux-work/qemu-SAM9X75/artifacts/linux4microchip-crypto-fixes-20260827/`.
- The older, pre-classification evidence remains in
  `/home/karl/linux-work/qemu-SAM9X75/artifacts/linux4sam-atmel-sha-fix-20260826/`.
- The Linux `i2c-at91` driver configures XDMAC before reading
  `atmel,fifo-size`; the documented 12-byte receive case can stall with three
  bytes left.  Validate the ordering fix on the board.  Do not weaken QEMU to
  hide the driver ordering error.

### P2: physical-board differential validation

Use the ordered and sanitized procedures in
`/home/karl/linux-work/qemu-SAM9X75/sam9x75-hardware-followup-20260825-r5/NEXT_HARDWARE_TESTS.md`.
High-value
Linux4Microchip follow-ups include a runtime-active, read-only FLEXCOM4
`SPI_VERSION` read at `0xf00004fc` (the highest-value current SPI blocker),
UHPHS and UDPHS functional comparison, XDMAC `DWIDTH=3` and FIFO/flush
ordering, and the USB gadget serial cable loop after connector and VBUS
verification.

Physical hardware does not test QEMU migration; migration remains a QEMU-only
gate.  Conversely, reset values, reserved-bit behavior, pin routing,
electrical timing and undocumented access policy require silicon evidence.

## Hardware safety boundaries

The detailed r5 safety rules are authoritative.  At minimum:

- Never program, invalidate, hide, checksum, generate keys, or use the key bus
  in OTPC.  Never enter OTPC emulation or write SRAM1 on the physical board.
- The only remaining OTPC lead is the narrowly specified privileged `AR`
  access probe with a tested abort handler.  Do not improvise beyond it.
- Never mutate boot media, factory data, OTP, mounted filesystems or unknown
  NAND blocks.  Use explicitly disposable media and independently verified
  recovery paths.
- Never issue QSPI program/erase commands against the fitted flash.
- Verify connector identity, voltage, ground and VBUS ownership before serial
  or USB loopback wiring.  Never cable two powered host ports together.
- Sanitize serial numbers, MAC addresses, network details, OTP values, unique
  IDs and key material before publishing evidence.

## Definition of a good checkpoint

For each bounded change:

1. Add reset/mask/access, positive, negative, interrupt and migration tests as
   applicable.
2. Run focused tests, the full SAM9X75 qtests, and the affected exact
   Linux4Microchip consumer.
3. Keep `-d unimp,guest_errors` clean and archive exact commands and hashes.
4. Run `git diff --check`; review only the intended files.
5. Commit a logically reviewable change and push `HEAD:main` to the
   `sam9x75` remote.
6. Update this handoff with the new head, test counts, evidence directory and
   next unresolved item.

Do not mark a peripheral complete merely because Linux probes it.  The support
matrix's fidelity contract remains the definition of completion.

## Copy-paste prompt for another agent

```text
Continue the SAM9X75 Curiosity QEMU project from the published main branch of
https://github.com/kmehltretter82/qemu-sam9x75.  Work in
/home/karl/linux-work/qemu-SAM9X75/qemu if that workspace is available.

First read SAM9X75_HANDOFF.md completely, then read the SAM9X75 machine
support matrix/execution roadmap and the relevant Linux4SAM consumer README.
Verify the Git head and preserve unrelated/untracked files.  Do not stage any
build or evidence directory.

The current-head SAM9X75 qtests pass 238/238 and the chardev unit tests pass
42/42.  The exact Linux4Microchip 2026.04 combined release gate is green:
GEM, both M_CAN controllers, paced FLEXCOM1 UART, all crypto engines, CPU,
memory, filesystem and a separate UHPHS result disk passed together with an
empty QEMU diagnostic log.  Preserve the release-r4 evidence.  The two-session
UDPHS g_serial reconnect gate and standalone crypto release profile are also
green.  The current-head AT91Bootstrap-to-U-Boot-to-FIT-to-ext4-root plus GEM
gate is green at repository checkpoint 9665c77235; preserve its release-r4
evidence.  Read the dated crypto-fixes artifact and do not add a QEMU
workaround for the documented Linux driver bugs.  The larger UHPHS
write/hash, clean hotplug and proven-active raw-read removal gate is also
green; preserve its release-r4 evidence and the dated EHCI-fix artifact.  The
standalone shared-bus 10,000-frame CAN semantic gate and separate classic plus
CAN-FD/BRS `canfdtest` gate are green at `4ab0af5c6a`; preserve their dated
release evidence.  Quiescent whole-machine migration plus 10,000 post-resume
frames is green at `95ba1ee289`; preserve migration release-r4.  The immediate
tasks are isolated external SocketCAN paths and controlled in-flight
migration.  Keep other operating systems deferred.

Use the r5 hardware handoff only for sanitized evidence, safety rules and
physical-test design because its software head is older than current main.
Never improvise destructive OTPC, NAND, QSPI or boot-media tests.  Make small
reviewable commits, update SAM9X75_HANDOFF.md at each checkpoint, and push
approved commits to the sam9x75 remote's main branch.
```
