# SAM9X75 Curiosity QEMU continuation handoff

Last updated: 2026-08-27 UTC

This is the entry point for another development agent.  It records the exact
checkpoint, what was actually validated, what must be repeated next, where the
larger backlog lives, and which physical-board experiments are unsafe.  Keep
this file current whenever a new checkpoint is committed.

## Canonical repository and checkpoint

- Published repository: <https://github.com/kmehltretter82/qemu-sam9x75>
- Published branch: `main`
- Local source tree: `/home/karl/linux-work/qemu-SAM9X75/qemu`
- Local development branch: `sam9x75-curiosity`
- Last implementation checkpoint: `49e71bb614`
  (`tests/guest: add SAM9X75 gadget serial self-loop`)
- Preceding UDPHS implementation checkpoint: `4aebd91443`
  (`hw/usb: support multi-packet AT91 UDPHS transfers`)
- Latest tested source: `56a14d47d0`
  (`docs: record SAM9X75 gadget serial reconnect validation`)
- The current test binary reports implementation source `8af5934947`; the
  commits through `56a14d47d0` after that build are documentation-only.

The documentation-only commits at current `main` follow the two implementation
commits above.  Do not reset to `49e71bb614`; continue from the tip of `main`.

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

The QEMU implementation at `8af5934947` compiled cleanly in
`build-verify-serial`; source through tested checkpoint `56a14d47d0` only
changes documentation.

- The complete SAM9X75 Curiosity qtest binary passed **237/237**.
- The LAN8840 EEPROM and separate SAM9X7 ADC suites passed **7/7** and
  **9/9** respectively.
- The focused UDPHS subgroup passed **20/20**, and the UART partner host suite
  passed **16/16**.
- `git diff --check` was clean before the validation-only run.
- Independent reviews of the UDPHS deferred-DMA, replacement-SETUP and
  migration changes found no remaining memory-safety or migration-ABI defect.
- The exact Linux4Microchip 2026.04 UDPHS gadget-serial loop passed at that
  same source revision in two fresh sessions separated by `g_serial` removal
  and reload.  All four endpoints passed TAP `1..7`, 27 DATA frames and 27
  acknowledgements, the 65,536-byte boundary and 210,478 received wire bytes
  with no protocol error.  USB device number 3 disappeared and re-enumerated
  as 4; UHPHS and UDPHS interrupts advanced in both sessions, Linux's global
  error count stayed zero, and QEMU's `unimp,guest_errors` log was empty.
- A Linux4Microchip kernel containing the separately packaged Atmel SHA and
  TDES fixes passed the full crypto release profile: four iterations, three
  workers and requests through 65,536 bytes.  TAP was 6/6 for 85 SHA, 34 HMAC,
  32 AES and 12 TDES jobs; XDMAC advanced by 8,808 and SHA by 10,034
  interrupts; QEMU's `unimp,guest_errors` log was empty.

No successful full *multi-subsystem* Linux4Microchip stress run is documented.
The standalone full crypto release gate is now green after fixing two Linux
driver bugs; the earlier combined attempt predates those fixes.  Reduced
Linux4Microchip GEM, USB storage, USB serial and FLEXCOM1 UART gates have older
passing evidence.  CAN has newer-mainline guest evidence, but no completed
Linux4Microchip partner-fixture release result.  Repeat each affected gate
before describing it as current-head coverage.

## Immediate continuation order

Do these steps before starting another device model or another operating
system.

1. Run the combined GEM, CAN, UART and crypto stress runner with all host peers
   and its independent TAP/JSON oracles.  Then repeat the remaining P0
   consumers, starting with disposable USB mass storage.
2. Finish with one AT91Bootstrap to U-Boot to disk-root Linux4Microchip boot
   that also exercises GEM traffic and leaves `-d unimp,guest_errors` empty.
3. Add quiescent whole-machine migration at the documented partner barriers,
   then carefully controlled in-flight migration.
4. In parallel, the physical-board agent can run the safe crypto validation in
   `artifacts/linux4microchip-crypto-fixes-20260827/REAL-HARDWARE-TESTS.md`.
5. Only after the remaining integration gates are green, take the next bounded
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

At the validated source the Curiosity binary reports 237 tests.  Record the
resulting count on every later revision rather than assuming it remains 237.

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

- Run UHPHS with a newly created disposable USB mass-storage image through the
  real Linux block/filesystem stack; verify payload hashes, clean unmount and
  a host-side filesystem check.
- Repeat GEM/LAN8840 deterministic TCP and UDP traffic, CAN-FD, FLEXCOM UART
  and crypto concurrently through the combined stress runner.
  The checked-in example's two-iteration/two-worker crypto entry is diagnostic
  and does not replace the four-iteration/three-worker/65,536-byte crypto
  release profile.
- Complete the Linux4Microchip CAN partner fixture first with one controller
  and 1,000 frames, then independent host connections for both controllers,
  the 10,000-frame gate and a separate `canfdtest` interoperability run.  Do
  not use one shared guest CAN bus as proof of two independent external paths.
- Run normal firmware-to-disk-root boot with `-d unimp,guest_errors`, then
  include current GEM traffic and confirm the log contains no SAM9X75 model
  diagnostics.
- Add/reset/migrate tests for any divergence found.  Fix the model, not the
  guest device tree, unless the same issue is proven to be a Linux bug.

### P1: bounded QEMU improvements

- Complete UDPHS `NB_TRANS`, isochronous DATAX/MDATA termination,
  SOF/suspend/resume timing and SAM-BA, then add Linux4Microchip end-to-end
  quiescent and in-flight migration gates while retaining and extending the
  existing low-level migration qtests.  The current raw-token bridge is a
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

The current-head SAM9X75, LAN8840, ADC, UDPHS and UART regressions and the
two-session Linux4Microchip UDPHS g_serial reconnect gate are complete.  The
Atmel SHA/TDES Linux fixes and complete standalone crypto release profile are
also validated in QEMU; read the dated crypto-fixes artifact and do not add a
QEMU workaround for those Linux bugs.  The immediate task is the combined
GEM, CAN, UART, crypto, CPU, memory and filesystem stress gate, followed by
the remaining P0 Linux4Microchip consumer matrix.  Keep other operating
systems deferred.

Use the r5 hardware handoff only for sanitized evidence, safety rules and
physical-test design because its software head is older than current main.
Never improvise destructive OTPC, NAND, QSPI or boot-media tests.  Make small
reviewable commits, update SAM9X75_HANDOFF.md at each checkpoint, and push
approved commits to the sam9x75 remote's main branch.
```
