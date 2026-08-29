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
- Latest tested repository checkpoint: `2b8d35c9b8`
  (`hw/timer/at91_tcb: Add quadrature speed measurement`)
- Preceding implementation checkpoint: `02407e0694`
  (`hw/timer/at91_tcb: Add the quadrature index input`)
- Preceding implementation checkpoint: `e1c545f5e4`
  (`hw/timer/at91_tcb: Add the quadrature decoder`)
- Preceding implementation checkpoint: `76011576d9`
  (`hw/timer/at91_tcb: Accept XC0-XC2 as external event sources`)
- Preceding implementation checkpoint: `46d4328430`
  (`hw/timer/at91_tcb: Clock channels from XC0-XC2`)
- Preceding implementation checkpoint: `66740ef43a`
  (`hw/timer/at91_tcb: Add UPDOWN counting`)
- Preceding implementation checkpoint: `f243bf9689`
  (`hw/timer/at91_tcb: Drive the TIOB waveform output`)
- Preceding implementation checkpoint: `cfc8205878`
  (`hw/timer/at91_tcb: Add the waveform-mode external event`)
- Preceding implementation checkpoint: `bf926d5931`
  (`hw/timer/at91_tcb: Add the capture-mode external trigger`)
- Preceding implementation checkpoint: `4d83948c14`
  (`hw/timer/at91_tcb: Add TIOA waveform output and capture mode`)
- Preceding test checkpoint: `87451ea2a6`
  (`tests/qtest/sam9x75: Cover SPI response-phase migration`)
- Preceding test checkpoint: `cbc7592d80`
  (`tests/qtest/sam9x75: Cover SPI write and CRC phase migration`)
- Preceding implementation checkpoint: `d35568571c`
  (`hw/misc: Add an AT91 SSC model and wire it on SAM9X7`)
- Preceding test checkpoint: `b61da6d205`
  (`tests/qtest/sam9x75: Cover SPI-mode multiple-block read`)
- Preceding implementation checkpoint: `a07f50cd94`
  (`hw/sd: Migrate the data-phase size`)
- Preceding test checkpoint: `0469ae3b29`
  (`tests/qtest/sam9x75: Cover SPI-mode block read, write and migration`)
- Preceding test checkpoint: `5bcb06b161`
  (`tests/qtest/sam9x75: Cover SPI-mode card reset and partial-command
  migration`)
- Preceding implementation checkpoint: `62872c90d2`
  (`hw/timer/at91_tcb: Add RA/RB compare events and their XDMAC requests`)
- Preceding implementation checkpoint: `936d31a9e3`
  (`hw/net/cadence_gem: Count statistics by wire length`)
- Preceding implementation checkpoint: `fcf594706b`
  (`hw/arm/sam9x75: Fix two faults the mmc_spi Linux gate exposed`)
- Preceding implementation checkpoint: `f8891de72a`
  (`hw/arm/sam9x75: Apply two SAM9X75 silicon measurements`)
- Preceding implementation checkpoint: `73631d172f`
  (`hw/arm/sam9x75-curiosity: Let the SDMMC0 card-detect pin follow the
  medium`)
- Preceding implementation checkpoint: `ebe3340acc`
  (`hw/ssi/at91_ospi: Drive the QSPI XDMAC request lines`)
- Preceding implementation checkpoint: `28529c9b12`
  (`hw/net/can/bosch_m_can: Model error counters and error confinement`)
- Preceding board checkpoint: `c5ee939fd0`
  (`hw/arm/sam9x75-curiosity: Route the PA13 GPIO chip select to the M.2
  card`)
- Preceding generic QEMU fix: `8fb8b5db49`
  (`hw/sd: Fix incoming migration of a powered-up SPI-mode card`)
- The frozen binary used by the Linux4Microchip integration and migration
  evidence reports `v11.1.0-417-g1851743e84`.  Commits through the latest
  tested repository checkpoint after that implementation source change only
  documentation and test profiles; the full qtest was repeated after relinking
  at `fcfbe1ae0e`, and every regression below was repeated once more at the
  documentation tip.  The isolated external SocketCAN gates used that same
  `build-verify-serial` binary.

Do not reset to an older UDPHS or crypto checkpoint; continue from the tip of
`main` after fetching.  Verify that the named implementation fixes and latest
tested checkpoint are present in the published history before starting a new
implementation slice.

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
`build-verify-serial`; the host regressions below were repeated at repository
checkpoint `fcfbe1ae0e` after relinking.

- The complete SAM9X75 Curiosity qtest binary passed **247/247**, including
  the two SPI chip-select tests, the three M_CAN error-confinement tests, the
  two QSPI XDMAC request tests and the two SDMMC0 card-detect tests below.
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
- Repository checkpoint `fcfbe1ae0e` adds a controlled active-stress marker to
  the CAN partner.  The peer options `--inflight-ready-file FILE` and
  `--inflight-at FRAMES` atomically record a live, non-pausing stress window
  only while more traffic remains and its local outstanding-sequence set is
  nonempty.  The marker records sent, received, acknowledged and next-sequence
  counters plus the exact outstanding set.  It is mutually exclusive with the
  quiescent barrier.  The complete host-only CAN fixture suite passed **25/25**.
  That host suite validates the trigger/oracle itself; the separate
  whole-machine result follows.
- Controlled active-stress whole-machine migration passed at repository
  checkpoint `fcfbe1ae0e`.  After all 86 boundary cases, the atomic marker
  observed stress receive sequence 3000 while neither endpoint had completed
  and 16 peer transmissions remained outstanding: sent/next 3008, received
  3000 and acknowledged 2992, with exact outstanding sequences 2992--3007.
  The marker did not pause traffic and the quiescent barrier was not used.
  QMP transferred 56,553,183 bytes of the 256 MiB machine in 431 ms with
  25 ms downtime and zero RAM remaining, then the source exited via
  `host-qmp-quit`.  The restored destination was proven running before either
  endpoint rc file existed.  Both roles subsequently exited zero, passed TAP
  5/5 and completed exactly 10,000 sent, received and acknowledged stress
  frames with zero gap, duplicate, corruption, stale/foreign frame, CAN
  error/drop or receive-queue overflow.  Interrupts reached 40,353/40,354 and
  both controllers remained `ERROR-ACTIVE`.  Both QEMU diagnostic logs were
  empty, host `e2fsck -fn` and `qemu-img check` passed, and the original root
  overlay hash was unchanged.  Preserve the authoritative evidence at
  `/home/karl/linux-work/qemu-SAM9X75/t/linux4microchip-inflight-migration-20260828/release-r4/`.
  Runs r1--r3 remain diagnostics for, respectively, overlong USB serial
  strings, an overlong destination QMP socket path after an otherwise valid
  source migration, and an external interruption before the trigger.
- The isolated external SocketCAN gates passed on two independent host `vcan`
  paths.  Each guest controller had its own QEMU `can-bus` with one
  `can-host-socketcan` endpoint (`can0`/`canbus0`/`s9x75c0` and
  `can1`/`canbus1`/`s9x75c1`), so neither guest controller could answer for
  the other and every frame crossed a real host backend.
- The semantic profile ran both peers as host processes with `--include-esi`.
  Each path passed TAP 5/5 on both roles with exactly 10,000 sent, received
  and acknowledged stress frames and zero gap, duplicate, corruption,
  stale-session, foreign frame, CAN error/drop or receive-queue overflow.
  Because a virtual `vcan` peer can set the flag, each guest controller
  **received 90 boundary cases while sending 86**; those four supplemental
  CAN-FD ESI cases are the ESI receive-path coverage the shared internal bus
  cannot produce.  Guest interrupts reached 32,245 and 32,244, both
  controllers stayed `ERROR-ACTIVE` with `berr-counter tx 0 rx 0`, and the two
  paths used distinct sessions `0x0000202608280010` and `0x0000202608280011`.
  Each host `vcan` moved 40,362 packets with zero error, drop or missed frame.
  Preserve `t/linux4microchip-can-socketcan-20260828/release-r1`.
- The interoperability profile ran host can-utils 2023.03-1build1 `canfdtest`
  generators against guest responders on both paths.  All four runs --
  classic `-s 8` and CAN-FD/BRS `-d -b -s 64`, each against `s9x75c0` and
  `s9x75c1` -- reported exactly 10,000 messages sent and received and exited
  zero.  Preserve `t/linux4microchip-can-socketcan-20260828/canfdtest-r1`.
- In both gates QEMU's `unimp,guest_errors` log was exactly empty, host
  `e2fsck -fn` passed and the snapshot-backed root overlay hash was unchanged.
  `smoke-r1` is a retained 200-frame bring-up run of the same topology.
- The first recorded `mmc_spi` blocker is closed.  FLEXCOM4 IO4/NPCS1 and
  PA13 are the same pad, and the board previously wired only the native NPCS1
  output to the `ssi-sd` adapter, so the unchanged upstream `wilc_spi`
  overlay could not select the card.  A new `at91-pad-mux` device models the
  shared pad: the PIO output wins whenever the PIO drives it, and the pad
  otherwise follows the peripheral function.  Both routes now reach the same
  adapter and the native route is unchanged.
- `sam9x75/sdcard/spi-gpio-chip-select` covers selection, deselection,
  reselection and the handover back to the peripheral function.
  `sam9x75/sdcard/spi-gpio-chip-select-migration` migrates a machine whose
  card is selected through the GPIO route and re-checks both routes on the
  destination.
- That migration test exposed a generic QEMU bug, fixed separately in
  `8fb8b5db49`.  `sd_set_ocr()` powers an SPI-mode card up at reset, but
  `sd_vmstate_pre_load()` then called the assert-guarded `sd_ocr_powerup()`
  unconditionally, so **any** machine holding an SPI-mode SD card aborted on
  incoming migration.  The pre-load path is now idempotent.
- The second `mmc_spi` blocker is untouched and still gates the consumer
  gate: the read-only FLEXCOM SPI `+0xfc` `SPI_VERSION` value must be
  measured on silicon.  Do not invent it, and do not add a fictional PDC or
  version register to satisfy the driver.
- The M_CAN model now has ISO 11898-1 error confinement from the transmit
  side, at `28529c9b12`.  Before this the model had no TEC/REC at all and an
  unacknowledged frame stayed pending in `TXBRP` forever with nothing
  recorded.  A frame no other node acknowledges now records `LEC=3`, adds 8
  to TEC, logs the error in `ECR.CEL` and raises `IR.PEA`; it stays pending
  for automatic retransmission unless `CCCR.DAR` cancels it through `TXBCF`
  and `IR.TCF`.  `PSR.EW`/`EP`/`BO` derive from the counters at 96/128/256,
  every change is reported through `IR.EW`/`EP`/`BO`, bus-off sets
  `CCCR.INIT`, and clearing `INIT` recovers.  Successful transfers decrement
  the counters, `ECR` exposes TEC/REC/RP, and the counters migrate in vmstate
  version 3.
- Three new qtests cover it: `sam9x75/mcan/error-confinement-and-bus-off`
  drives a lone controller through warning, passive and bus-off, recovers,
  retries and exercises DAR; `sam9x75/mcan/error-recovery-with-peer` holds
  the peer in `INIT` so it acknowledges nothing, then releases it and checks
  that the retried frame succeeds and decrements TEC; and
  `sam9x75/mcan/error-state-migration` migrates in the error-passive state
  and completes bus-off and recovery on the destination.
- Deliberate limits of that slice, so they are not mistaken for bugs: the
  CAN bus abstraction has no bit-level arbitration, error frames or
  receive-error injection, so REC never increments; `PSR.ACT` is not driven;
  and bus-off recovery is immediate rather than timed over 129 recessive-bit
  sequences.  The last of these is also why a real error-passive controller
  still cannot be produced for the open ESI sub-gate: it needs a receive-side
  error source first.
- The QSPI XDMAC request lines are wired at `ebe3340acc`.  DS60001813E
  Table 16.1 assigns XDMAC0 requests 26/27 to QSPI transmit/receive and the
  exact sam9x7 dtsi declares them, but the SoC never connected them and the
  OSPI model had no request outputs.  They now follow `TDRE`/`RDRF` while
  enabled and are re-driven after migration.  The same slice fixed a real
  defect it exposed: the OSPI register window only accepted 32-bit accesses,
  so a request-paced channel's byte-wide TDR write faulted and the channel
  halted; narrow accesses are now widened like the other AT91 data
  registers.  `sam9x75/qspi/xdmac-requests` and
  `sam9x75/qspi/xdmac-request-migration` cover request levels, byte access,
  byte-by-byte identity reads through paced channels, a channel started
  before `QSPIEN`, and post-migration re-drive.
- Be precise about what that proves: the exact Linux `atmel-quadspi` driver
  uses `dmaengine_prep_dma_memcpy` through the AHB window, not request-paced
  transfers, so requests 26/27 have no Linux consumer and the qtests are the
  gate.  Do not claim a Linux DMA gate for QSPI from this evidence.
- **Every request in DS60001813E Table 16.1 now has a source**, the last
  two wired at `bf926d5931`.  The four timer lines 41--42 and 49--50 stay idle on
  this board: TIOA and TIOB are modeled as named GPIOs and the Curiosity
  routes neither, so capture and triggering are reachable from a test but
  not from a guest.  If a future expansion board routes those pins, the
  model side is already in place.  The TC compare events 43--48 were wired
  with the TCB compare slice, and SSC 38/39 with the new SSC model at
  `d35568571c`.
- The SDMMC0 card-detect pin follows the medium at `73631d172f`.  The exact
  device tree detects the card through `cd-gpios` on PA23, but the board
  drove PA23 once at creation and only when a drive existed, so a guest could
  never see a medium change.  The generic SDHCI model now exposes a
  `card-inserted` output (unconnected on every other board), `at91-sdhci`
  re-exports it, and the board inverts it onto PA23 unconditionally.  QMP
  `eject` and `blockdev-change-medium` address the legacy `-drive` backend
  with `device`, not `id`.  `sam9x75/sdcard/card-detect-follows-medium` and
  `sam9x75/sdcard/card-detect-migration` cover pin and present-state, system
  reset and migration.
- The Linux SDMMC0 media-change gate passed at `73631d172f`.  Root was on a
  USB disk (`root=/dev/sda`, rootfs attached with `snapshot=on`) so the SD
  socket held a disposable 256 MiB ext4 card that also served as the
  evidence disk.  The guest wrote a 16 MiB deterministic payload, unmounted,
  and the host ejected and re-inserted the card through QMP; Linux saw the
  removal and re-insertion through the PA23 `cd-gpios` interrupt, and the
  payload hash, a rename and a commit marker survived.  A raw read of the
  whole card was then yanked mid-transfer: it failed with `EIO`
  (`OSError: [Errno 5]`), the card re-inserted, and the data verified again
  `ro,noload`.  Linux logged 3 insertions and 2 removals, the
  `80000000.mmc cd` interrupt advanced exactly 0 -> 4, QEMU's
  `unimp,guest_errors` log was empty, host `e2fsck -fn` passed and the
  rootfs hash was unchanged.  Preserve
  `t/linux4microchip-sd-media-change-20260828/release-r2`; `release-r1` is
  diagnostic only because its validator looked for the `EIO` text in
  `dmesg` instead of the captured read error, while every guest phase of r1
  had already passed.
- The host `vcan` prerequisite is an environment setup step, not a code
  change.  On this workstation the README's private-namespace recipe cannot
  work because Ubuntu sets `kernel.apparmor_restrict_unprivileged_userns=1`,
  so `unshare --user --map-root-user` fails before creating any interface.
  Two global `vcan` interfaces were created instead with one privileged
  setup; QEMU, both host peers and the validators then ran unprivileged.
  Recreate them with `sudo modprobe vcan` and
  `sudo ip link add s9x75c0 type vcan && sudo ip link set s9x75c0 up`
  (likewise `s9x75c1`); remove them with `sudo ip link del`.

## Physical-board results, 2026-08-28

A macOS workstation with a SAM9X75 Curiosity LAN Kit ran the hardware section
of this file.  Personal identifiers (addresses, host-key fingerprints, card
identifiers, UUIDs) are deliberately omitted here; the raw evidence stays on
that workstation.

- **Both Linux crypto fixes are silicon-confirmed.**  The stock 2026.04 kernel
  wedged the first TDES request on 2 of 2 cold boots: task in `D` state at
  `skcipher_recvmsg`, unkillable, zero `atmel-tdes` completions, no XDMAC
  advance, no kernel message, while AES and SHA on the same boot completed
  normally.  The packaged fixed kernel completed that request in about 1 ms
  with the reference ciphertext and then passed the full release profile
  (3 workers, 4 iterations, through 65,536 bytes, TAP 6/6, `atmel-sha`
  +6360/+2535, `at_xdmac` +128/+352).  Its concurrent HMAC path validates the
  `atmel-sha` fix as well.  Both patches are ready for upstream submission.
- **FLEXCOM4 `SPI_VERSION` = `0x410`**, read-only, byte lanes `0x0410`/`0x10`,
  with the driver logging `version 0x410` and choosing FIFO plus XDMAC.
  Modeled at `f8891de72a`.  This closes the last `mmc_spi` blocker.
- **SDMMC0 card-detect matched every QEMU criterion** with root moved to
  NFS so the card was idle: clean removal and re-insertion preserved the
  boot partition byte-for-byte, an active raw read failed with `EIO` and
  recovered, and the card re-enumerated identically each time.  One fidelity
  divergence: contact bounce.  Removal gave 1 card-detect edge, insertion 3
  or 4, nine interrupts for four changes against QEMU's four; Linux debounce
  absorbed it into exactly one enumeration each.  The Linux gate validator
  already requires `>= 4` edges, not an exact count; keep it that way.
- **M_CAN**: core release 3.3 on both instances (confirming the `crel` top
  byte), 40 MHz clock, and on `can0` the full ERROR-WARNING -> ERROR-PASSIVE
  -> BUS-OFF progression from one unacknowledged frame, each reported once;
  TEC read **248** through bus-off and STOPPED and zero after re-enable;
  restart-ms recovery worked.  QEMU stored 256 and read 0xff; fixed at
  `f8891de72a`.  `can1` stayed ERROR-ACTIVE with no interrupts and is inconclusive
  without a pinmux -- do not quote it.
- **GEM statistics.** Three findings were checked against the model.  The
  address classification already matched silicon: broadcast increments only
  the broadcast counter, so the "I/G bit set means multicast" double-count
  never existed here.  Per-queue counters being live across q0/q1/q4 is
  consistent with the modeled queues.  The octet finding was a real bug,
  fixed at `936d31a9e3`: the model counted transmit frames without the FCS the MAC
  appends, and counted receive frames with it only when `FCS_REMOVE` was
  clear, which confuses "the application wants the FCS" with "the FCS was on
  the wire".  Statistics now count the wire length, which also moves a
  64-octet buffer into the 65--127 bucket where silicon puts it.  The fix is
  in the shared Cadence GEM, so the Xilinx boards get it too.
- A third generic QEMU bug came out of this work, at `a07f50cd94`: `hw/sd` never
  migrated `data_size`, which bounds every data phase.  A machine migrated
  mid-transfer served one more byte and then silently truncated the block.
  Any host controller is affected, not only SPI; it was found by migrating
  100 bytes into a 512-byte SPI read and is now covered by a qtest.
- **QSPI and NAND on the tested unit**: JEDEC identity read all ones with no
  MTD device, and no NAND device was found with the shipped device tree.
  Check J10 and J9 (and NAND enablement in that DTB) before treating the
  modeled SST26VF064BEUI and MX30LF4G28AD as divergences.
- The board now boots the fixed kernel from a second FIT with FLEXCOM4 SPI
  enabled; the original FIT and U-Boot environment are retained alongside
  for revert, and a byte-exact boot-partition backup exists on the macOS
  side.

## The mmc_spi gate is achieved

The full consumer gate passed at `fcf594706b` with the exact kernel and the
unchanged upstream `wilc_spi` overlay: `SPI_VERSION` 0x410 read by the
driver, FIFO/XDMAC selected, a 64 MiB SPI-mode card enumerated, an 8 MiB
payload written and read back identically, and a FAT32 filesystem written,
`fsck`-checked and verified against its manifest, with an empty QEMU
diagnostic log and a clean host filesystem check.  Preserve
`t/linux4microchip-mmc-spi-20260828/release-r4`; r1--r3 are diagnostics.

Running it exposed two real model faults, now fixed and qtested:

- The XDMAC treated any write of 1 on a request line as a new request, so a
  channel with an accepted chunk outstanding reported a spurious overflow.
  The FLEXCOM made it reachable by re-driving its shared transmit/receive
  request lines on every update, so a change on one direction re-asserted
  the other.  A request is a level: only a rising edge counts, and each line
  is driven only when its level changes.
- `WDRBT` blocked on the first unread datum, which deadlocks Linux's FIFO
  PIO sequence (fill the transmit FIFO, wait for `RXFTHF`, only then read).
  With the FIFOs enabled it now gates on a full receive FIFO; the non-FIFO
  meaning is unchanged.

Both were reachable only through the real driver sequence, which is the
argument for keeping exact-guest gates alongside qtests.

## Two designed-but-unimplemented slices

Both were scoped against the model and the data sheet in this session but
deliberately not started, because each is a multi-hour change with real
regression risk and neither should be half-landed.  The analysis is here so
the next agent does not have to redo it.

### TCB RA/RB compare events -- DONE at `62872c90d2`

Implemented: the channel now runs one ptimer segment at a time to the
nearest of RA, RB and the period end, `CPAS`/`CPBS` fire in counter order,
the segment base migrates in channel vmstate version 2, and each compare
drives an XDMAC request line with channel 1 of each block wired to requests
43--48.  The original analysis follows, because the remaining capture work
starts from the same place.

`hw/timer/at91_tcb.c` modeled only `COVFS`, `CPCS` and `SECE`.  Each channel
runs one ptimer that counts down to the RC limit, so intermediate compares
do not exist.  Adding them is what unblocks XDMAC requests 43--48, which
DS60001813E Table 16.1 assigns to `TC1_CPA/CPB/CPC` and `TC4_CPA/CPB/CPC`
(channel 1 of TC0 and channel 1 of TC1 respectively).

The change is to schedule the ptimer to the nearest upcoming compare among
RA, RB and RC rather than to RC alone, set the matching `CPAS`/`CPBS`/`CPCS`
bit on expiry, and re-arm to the next boundary.  Touch points that must be
handled together, not piecemeal:

- `at91_tcb_counter()` derives the counter from the ptimer's remaining
  count against the RC limit; with intermediate boundaries it must account
  for which segment is running.
- `at91_tcb_periodic()`, `CPCSTOP` and `CPCDIS` decide one-shot versus
  periodic; only the RC boundary may stop or disable the clock.
- Migration carries the ptimer, so the segment state must migrate too or be
  recomputed in post-load.
- Writes to RA/RB/RC while running must re-arm.

Only waveform mode was in scope.  Capture mode (`LDRAS`/`LDRBS`) and
`ETRGS`, which cover requests 41--42 and 49--50, additionally need TIOA and
TIOB input pins that the board does not currently wire, so they remain a
separate and larger slice.  Requests 38--39 need an SSC model, which does
not exist at all.  Those four lines and the SSC pair are now the only
entries of Table 16.1 still unwired.

### UDPHS suspend, resume and SOF

Linux `atmel_usba_udc` consumes `DET_SUSPD`, `WAKE_UP` and `ENDOFRSM`; the
model defines none of them.  The blocker is not the peripheral, it is that
QEMU's USB core gives a device no callback for host port suspend or resume:
`USBPortOps` has attach, detach, child_detach, wakeup and complete, and
`USBDeviceClass` has handle_attach, handle_reset, handle_control,
handle_data and ep_stopped.  Port suspend lives only in the EHCI `PORTSC`
and OHCI `HcControl` registers and never reaches the device.  The UDPHS
gadget bridge is a plain `USBDevice`, so it cannot observe it either.

Two honest options, and the choice should be deliberate:

1. Add a generic device-visible suspend/resume hook that EHCI and OHCI call
   when a port enters or leaves suspend, and have the bridge forward it.
   This is the faithful route and is upstream-shaped, but it changes shared
   USB core API and needs sign-off beyond this board.
2. Add a bridge-only back-channel between the modeled UHPHS and UDPHS,
   which keeps the change local but only works for the internal
   gadget-to-host topology, not a general host-facing cable backend.

Do not infer suspend from packet inactivity: QEMU never delivers SOFs to
devices, so a polled interrupt endpoint would false-trigger, and the
resulting `DET_SUSPD` storm would be worse than the missing feature.

## Do not run a whole-machine gate and the qtest suite at once

Two full Curiosity qtest runs on 2026-08-28 died with `SIGABRT` while
starting a test's QEMU, reported by the harness as
`kill_qemu() detected QEMU death from signal 6`.  It looked like a flake,
but the cause is almost certainly host memory pressure:

- Every qtest spawns a fresh machine, and `default_ram_size` is 256 MiB.
- Both failures happened while an `mmc_spi` gate VM, another 256 MiB
  machine plus its page cache, was running in the background.
- The abort is at QEMU startup, not inside a test, which is what an
  allocation failure looks like.
- This workstation has 23 GiB of RAM and a 4 GiB swapfile that was fully
  consumed at the time, exactly the condition the operator's notes warn
  about.

Eighteen consecutive runs with no whole-machine gate in the background --
twelve of the `rom` group and six unfiltered full suites -- did not
reproduce it.  So: run the qtest suite or a whole-machine gate, not both.
If it ever reproduces on an otherwise idle machine, that would falsify this
and make it a real bug worth chasing.

## SSC probe check, 2026-08-28

The new SSC model was checked against the unmodified Linux `atmel-ssc`
driver, not only qtests.  The exact device tree ships `ssc@f0010000` with
`status = "disabled"`, so a copy of the base DTB was patched with
`fdtput -t s <dtb> /apb/ssc@f0010000 status okay`, exactly the technique the
CAN README documents.  The driver bound the device and logged
`ssc f0010000.ssc: Atmel SSC device` with its mapped aperture and
interrupt, `/sys/bus/platform/devices/f0010000.ssc` and the `ssc` driver
link both appeared, QEMU's `unimp,guest_errors` log was exactly empty, and
the snapshot-backed root overlay was unchanged.

That exercises the register decode and the reset values a real driver
reads.  It does not exercise data movement: the driver binds but the model
carries data only in loop mode, and an audio consumer would additionally
need frame timing and the sync unit.  Evidence:
`t/linux4microchip-ssc-probe-20260828`.

## TCB rework boot check, 2026-08-29

The TCB counting core was reworked several times in one sitting -- direction
for UPDOWN, an edge-driven path for XC clocking, then the quadrature
decoder -- and that core is what Linux's timer driver sits on, so it was
checked against a real boot and not only qtests.

The exact kernel boots to a shell, registers `timer@f8008000` alongside
`pit64b` and `jiffies` in `available_clocksource`, counts zero
clocksource or timer failures in `dmesg`, measures a five-second guest
sleep as six seconds of wall time, and leaves QEMU's `unimp,guest_errors`
log exactly empty.  Evidence: `t/linux4microchip-tcb-rework-boot-20260829`.

That first boot only showed the TCB clocksource registering, because Linux
selects `pit64b`, which outranks it.  A second boot closes that gap: with
both `pit64b` nodes patched to `status = "disabled"` in a DTB copy
(`/apb/timer@f0028000` and `/apb/timer@f0040000`), Linux reports
`Switched to clocksource timer@f8008000`, `current_clocksource` is the TCB
and it is the only entry in `available_clocksource`, `sched_clock` comes
from it at 32 bits and 1500 kHz, a five-second guest sleep measures as
exactly five seconds, `dmesg` counts zero timer failures and the
diagnostic log is again empty.  Evidence:
`t/linux4microchip-tcb-clocksource-20260829`.

So the reworked counting core has been exercised as the system timebase,
not merely probed.  Rerun that second boot, not just the first, if the
core is touched again.

## mmc_spi gate re-run after the model changes, 2026-08-29

Today's work touched things the qtests alone do not settle: `hw/sd` gained
a migrated `data_size` and a narrower field type, the SoC gained an SSC and
four newly wired XDMAC lines, and the TCB counting core was reworked three
times.  The established mmc_spi consumer gate was therefore re-run
unmodified against `fe5defb597`, using the same runner as the
`release-r4` evidence it is compared with.

All five return codes are zero and the four validator lines pass: the
driver reads `SPI_VERSION` 0x410 over the FIFO and XDMAC path, an 8 MiB raw
write and readback compare identical, FAT32 write plus host `fsck.fat` and
the extracted manifest verify, no SPI timeout, DMA error or MMC I/O error
appears, QEMU's `unimp,guest_errors` log is exactly zero bytes and the
shared rootfs image is byte-identical before and after.  Evidence:
`t/linux4microchip-mmc-spi-20260829-postrework`.

The raw write and readback is the part that matters for the `hw/sd`
change, since it drives full data phases through the SPI path that
exposed the truncation bug in the first place.

## In-flight migration gate re-run, and evidence lost doing it, 2026-08-29

The whole-machine migration gate was re-run against `2628006691`, because
today added vmstate to three devices: the TCB channel went to version 3 and
its block state to version 2, the SSC is a new migrated device, and
`hw/sd` gained a `data_size` subsection.  All six return codes are zero and
the five validator lines pass: migration completes over QMP during active
stress with frames outstanding, the source stops and the destination
resumes before completion, the 10,000-frame bidirectional CAN gate passes
across the migration, both source and destination `unimp,guest_errors` logs
are exactly zero bytes and the backing overlay is byte-identical.
Evidence: `t/linux4microchip-inflight-migration-20260829-postrework`.

**Reference evidence was destroyed getting there, and it is not
recoverable.**  This gate's runner hard-codes its run directory in *two*
files, `run-host.sh` and `run-inflight-migration.exp`.  Repointing only the
first left the expect script spawning QEMU with the old directory's
`-drive`, `-qmp` and `-D` paths, which overwrote `console.log`,
`source-qemu.log` and `results.ext4` inside
`linux4microchip-inflight-migration-20260828/release-r4`.  `t/` is not in
git, so there was no restore path.

What survived there is the whole of `extracted/`, which is what the
validator actually reads, plus `validation.txt`, `result.txt`,
`destination-qemu.log`, `e2fsck.log` and `debugfs.log`.  The five PASS
lines of that run therefore still stand on their supporting artifacts; the
raw source console, the source diagnostic log and the results image behind
those extracts are gone.

Two rules follow.  Relocating a gate means auditing every file in the
runner set for the run directory, not just the entry script -- `grep -rn`
the old path and expect no hits before launching.  And the release
directories under `t/` are now `chmod -R a-w`, so a mistake like this one
fails loudly instead of silently overwriting the evidence.

## Immediate continuation order

Do these steps before starting another device model or another operating
system.

Both `mmc_spi` blockers are now closed and the crypto fixes are
silicon-confirmed.  Do these steps next.

1. Remaining P0 items: the board jumper and mux behavior still unmodeled,
   and broader USB hotplug, error and migration behavior.  The `mmc_spi`
   gate itself is achieved (above).  Its reset and partial-command migration
   follow-ups are done at `5bcb06b161`, and card initialization, single-block
   read and write and command-boundary migration at `0469ae3b29`.  What remains is
   nothing: the reset and migration follow-up is complete at `87451ea2a6`, with
   the partial command, response, read data, write data and CRC phases all
   covered.  Chasing the read-data case found a generic `hw/sd` migration
   fault, fixed at `a07f50cd94`.  What is left for this consumer is the
   exact Linux gate work -- dynamic card removal during I/O and the WILCS02
   interrupt GPIO -- not qtests.
1a. Prepare upstream submission of the two Linux patches in
   `artifacts/linux4microchip-crypto-fixes-20260827/`; both are now
   hardware-confirmed (see the results section above).
2. Then the next bounded P1 implementation slice.  The TCB now covers RA/RB
   compares, the TIOA and TIOB outputs, capture loading, the external
   trigger, the waveform-mode external event, all four `WAVSEL` behaviours
   including UPDOWN, and XC edge clocking with the `BMR` cross-connect, and
   every XDMAC request has a source.
   QDEC landed at `e1c545f5e4`, which was the last TCB gap on the list.  What is
   the index input at `02407e0694` and speed measurement at `2b8d35c9b8` finished
   that list.  What the TCB still lacks is narrower again and none of it was
   ever on the plan: the digital filter, whose `MAXFILT` width is stored
   but inert, and `QERR`.  Neither is a small omission to fill in later.
   Filtering means deciding a pulse is noise only once it has ended, so it
   needs every phase edge delayed by `MAXFILT + 1` peripheral clock cycles
   -- a timer per phase line that changes the timing of every quadrature
   edge, for a feature no driver in the exact kernel uses and that no
   hardware here can validate.  `QERR` cannot be modeled at all: it needs
   both phases to move at once and phase inputs arrive one GPIO event at a
   time.  The badly-located-index detection those two would support is not
   a gap: the SAM9X7 `TC_BMR` has no `BIDXCE` bit, so the part cannot enable
   it -- the datasheet describes it in generic QDEC text that this variant's
   register layout does not carry.
   `QERR` is deliberately absent -- it needs both phases to move at once,
   and phase inputs arrive one GPIO event at a time, so a branch for it
   would be untestable dead code.  The XC external-event sources are
   accepted at `76011576d9`.  The edge-counting rework that
   TCLK and the `BMR` cross-connect needed is done at `46d4328430`: an XC-clocked
   channel advances per edge and keeps its own counter, reusing the same
   boundary handling as the ptimer path, so QDEC now has the counting
   mechanism it needs and is mostly decode work on top.  Note the cross-connect routes TIOA as a *clock*, so wiring it
   means teaching the channel to count edges rather than run on a ptimer --
   a change to the counting core that Linux's clocksource depends on, so it
   deserves its own careful slice.  The UDPHS suspend/resume design
   decision, written up above, is still the one genuine blocker.  Done this checkpoint: transmit-side
   M_CAN error confinement, the QSPI XDMAC request lines, the SDMMC0
   card-detect path, the two mmc_spi faults and the GEM wire-length
   statistics.  Next candidates, in rough order of
   boundedness: UDPHS suspend/resume/SOF signalling (`atmel_usba_udc`
   consumes it, but it needs the gadget-bridge protocol to carry host
   suspend/resume); TCB capture/compare events, which would then let XDMAC
   requests 41--50 be wired; an SSC model as the prerequisite for requests
   38/39; the remaining M_CAN items (receive-side error source, `PSR.ACT`,
   timed bus-off recovery); UDPHS `NB_TRANS`/isochronous; and the UHPHS
   fidelity items.  Keep Linux4Microchip as the primary OS;
   NetBSD, FreeBSD, other guests and the newer-mainline matrix remain
   deliberately deferred.
3. **Physical-board agent (macOS workstation with the Curiosity board):**
   the ordered, safe measurements that this checkpoint needs, all read-only
   or on disposable media.  Use the r5 safety rules; never program OTPC,
   QSPI or NAND.
   Done on 2026-08-28 (see the results section): the `SPI_VERSION` probe,
   the crypto validation, the SDMMC0 card-detect comparison, M_CAN error
   confinement on `can0`, and the QSPI identity read.  Still open:
   - Confirm the CAN header pinmux against the schematic (tentative reading:
     CANRX0/PA27, CANTX0/PA28, CANTX1/PA29, CANRX1/PA30 on peripheral B --
     **not verified, not to be acted on until confirmed**).  Only then
     repeat the `can1` probe and, with transceivers, can0/can1 traffic.
   - Check J10 (QSPI chip select) and J9 (NAND) on the tested unit and
     re-read the QSPI identity; program or erase nothing.
   - Run the packaged 20-cold-boot TDES repeatability profile if upstream
     reviewers ask for it; the 2-of-2 reproduction already stands.
   Everything workspace-local that this section needs was packaged on
   2026-08-28 as `sam9x75-hardware-agent-20260828.tar.gz` (102 MB, SHA-256
   `324a43b9f2b9fa21f622d57c2bbe4ae765aac068602242390db4da174658d059`) next to the workspace root on the Linux
   workstation.  It holds the crypto-fix and EHCI-fix artifacts with their
   fixed kernels, the r5 hardware package, the stress matrix, the data sheet,
   and every authoritative QEMU evidence directory named in this file with
   disposable disk images removed (each run keeps its `extracted/` tree and
   hashes).  Its `hardware-agent-manifest/MANIFEST.md` lists exact contents
   and exclusions.  Verify the archive hash before use.
   The datasheet behind every register claim in this checkpoint is the
   Microchip SAM9X7 Series Data Sheet, document **DS60001813E** (Table 16.1
   for XDMAC requests, the UDPHS `INTSTA` and TC `SR` bit layouts).  It is
   **not in Git**: on this workstation it sits outside the repository at
   `/home/karl/linux-work/qemu-SAM9X75/references/SAM9X7-Series-Data-Sheet-DS60001813E.pdf`.
   Another machine must obtain revision E from Microchip; an earlier or later
   revision may renumber tables and bits, so check the document number
   before comparing values.
4. Also on the board, take the read-only FLEXCOM SPI `+0xfc` probe in
   `tests/guest/at91/linux4sam/spi-consumer/README.rst`.  It is the only thing
   still blocking the `mmc_spi` gate now that the chip-select routing exists.
5. **UDPHS suspend/resume/SOF needs a design decision before any code.**
   Linux `atmel_usba_udc` consumes `DET_SUSPD`, `WAKE_UP` and `ENDOFRSM`,
   the model defines none of them, and QEMU's USB core gives a device no
   callback for host port suspend or resume: `USBPortOps` and
   `USBDeviceClass` have attach/reset/packet/wakeup hooks only, and port
   suspend lives solely in EHCI `PORTSC`/OHCI `HcControl`.  The gadget
   bridge is a plain `USBDevice`, so a faithful implementation needs either
   a generic USB API addition (new port/device suspend-resume hooks that
   EHCI and OHCI call) or a bridge-only back-channel.  Choose deliberately;
   do not infer suspend from packet inactivity, because QEMU never delivers
   SOFs to devices and a polled interrupt endpoint would false-trigger.
6. Two CAN sub-gates remain deliberately open and must not be claimed from the
   evidence above.  ESI is injected by a virtual `vcan` peer rather than
   observed from a real error-passive controller, and host SocketCAN state
   lives outside the VM, so it is not itself migrated; the migrated-CAN
   evidence remains the internal-QEMU-bus result.

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

python3 -m py_compile \
  tests/guest/at91/linux4sam/can-partner/sam9x75_can_partner.py \
  tests/guest/at91/linux4sam/can-partner/test_sam9x75_can_partner.py

python3 -m unittest discover \
  -s tests/guest/at91/linux4sam/can-partner \
  -p 'test_*.py' -v
```

The UART suite uses local sockets and pseudo-terminals.  A restricted sandbox
may need explicit permission for those resources; that is not a reason to skip
the suite.  At `fcfbe1ae0e`, the CAN host-only command reports 25 tests.

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
  classic plus 10,000 64-byte CAN-FD/BRS exchanges.  Both profiles have since
  been repeated through `can-host-socketcan` on two independent host `vcan`
  paths, closing this item; see the isolated-gate entries above and preserve
  `t/linux4microchip-can-socketcan-20260828/release-r1` and `canfdtest-r1`.
  Do not use the shared-bus runs as proof of two independent external paths.
  ESI injection is available from virtual host peers only, and the isolated
  semantic gate uses it; never request it from the Linux M_CAN used as a peer.
- Quiescent and controlled active-stress whole-machine CAN migration are both
  achieved.  Preserve their separate authoritative `release-r4` evidence.
  The active run migrated with 16 peer sequences outstanding and then passed
  the complete 10,000-frame bidirectional semantic oracle after destination
  resume.  Keep this fully migrated internal-QEMU-bus result distinct from
  external SocketCAN state, which lives outside the VM and is not itself
  migrated.
- The current-head firmware-to-disk-root plus GEM gate is achieved at
  repository checkpoint `9665c77235`.  Preserve
  `linux4microchip-firmware-gem-20260827/release-r4`; do not confuse its
  `-kernel` entry into AT91Bootstrap with the still-unimplemented mask-ROM
  media-selection state machine.
- Add/reset/migrate tests for any divergence found.  Fix the model, not the
  guest device tree, unless the same issue is proven to be a Linux bug.

### P1: bounded QEMU improvements

- Complete UDPHS `NB_TRANS`, isochronous DATAX/MDATA termination,
  SOF/suspend/resume timing and SAM-BA while retaining and extending the
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
release evidence.  The isolated external SocketCAN gates are also green on two
independent host `vcan` paths: the semantic profile passed 10,000 frames per
role per path with each guest controller receiving 90 boundary cases while
sending 86 (the four supplemental CAN-FD ESI cases a virtual peer can inject),
and host can-utils `canfdtest` generators passed 10,000 messages for both the
classic and CAN-FD/BRS profiles on each path.  Preserve
`t/linux4microchip-can-socketcan-20260828/release-r1` and `canfdtest-r1`.  Quiescent whole-machine migration plus 10,000 post-resume
frames is green at `95ba1ee289`; preserve migration release-r4.  Controlled
active-stress migration is also green at `fcfbe1ae0e`: QMP migrated with 16
peer sequences outstanding, the source exited, the destination resumed before
either role completed, and both roles then passed the exact 10,000-frame
semantic gate.  Preserve its separate in-flight-migration release-r4 evidence.
The post-gate regressions pass 268/268 for Curiosity, 42/42 for chardev, 7/7
for LAN8840 EEPROM, 9/9 for ADC and 25/25 for the CAN host fixture.  Two
full Curiosity runs on 2026-08-28 aborted at QEMU startup; that was host
memory pressure, not a model fault, and the rule it implies is below.
The `mmc_spi` consumer gate is achieved, and running it exposed and fixed a
spurious XDMAC request overflow and a `WDRBT` FIFO stall.
Physical-board results from the same day are recorded in the handoff: both
Linux crypto fixes silicon-confirmed, `SPI_VERSION` measured as 0x410 and
modeled, M_CAN TEC retained at 248 through bus-off and modeled, card-detect
matched with a contact-bounce fidelity note, and QSPI/NAND absent on the
tested unit pending a jumper check.  The
SDMMC0 card-detect pin now follows the medium and a Linux media-change gate
covers clean removal, re-insertion and an active-read yank; see the handoff
for its evidence directory.  The next physical-board work is listed as an
ordered, safety-bounded section in the continuation order.  The QSPI
XDMAC request lines 26/27 are wired and qtest-gated; Linux uses memcpy DMA
through the AHB window, so do not claim a Linux DMA gate for them.  The
M_CAN model now has transmit-side ISO 11898-1 error confinement: TEC/REC,
EW/EP/BO with their interrupts, DAR cancellation, bus-off setting INIT and
recovery on clearing it, all migrated; REC never increments because the bus
abstraction has no receive-error source.  The
board now routes the upstream overlay's PA13 GPIO chip select to the M.2 card
through an `at91-pad-mux`, closing the first `mmc_spi` blocker; the second
still needs a silicon `SPI_VERSION` measurement and must not be invented.
That work also fixed a generic QEMU bug where any machine holding an
SPI-mode SD card aborted on incoming migration.  The two
CAN sub-gates still deliberately open are ESI observed from a real
error-passive controller and migration of host SocketCAN state, which lives
outside the VM.  Keep other operating systems deferred.

Use the r5 hardware handoff only for sanitized evidence, safety rules and
physical-test design because its software head is older than current main.
Never improvise destructive OTPC, NAND, QSPI or boot-media tests.  Make small
reviewable commits, update SAM9X75_HANDOFF.md at each checkpoint, and push
approved commits to the sam9x75 remote's main branch.
```
