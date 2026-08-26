Microchip SAM9X75 Curiosity (``sam9x75-curiosity``)
==================================================

The ``sam9x75-curiosity`` machine models the Microchip SAM9X75 Curiosity
board fitted with a SAM9X75D2G (2 Gbit DDR3L SiP).  Work is also tracking
the Curiosity LAN Kit configuration, where a LAN8840 EDS2 board is fitted to
the base board's Ethernet connector.

This machine is under active development.  The support matrix below is a
contract: an item is not considered complete merely because a guest happens
to probe it.

Reference baseline
------------------

The implementation and tests are pinned to the following inputs:

* SAM9X75 Curiosity User Guide DS60001859C, January 2026, SHA-256
  ``37b51cecee93146f97cb1082461bfc1051059f8bf2a42e9c4205d7dc4f6400ce``.
* SAM9X7 Series data sheet DS60001813E.
* SAM9X7 Series silicon errata DS80001082H.
* Macronix MX30LF1G28AD/MX30LF2G28AD/MX30LF4G28AD data sheet PM2579,
  revision 1.3, August 2022.
* AT91Bootstrap v4.0.13, commit
  ``c2e3f87bf694a4c27c60d24db512adcdd4d7b442``.
* U-Boot ``linux4microchip-2026.04``, commit
  ``9fa52b889bd44a7d761b36be1a9c6b1db335022a``.
* Linux mainline snapshot commit
  ``ff68e5f557f69a08fdcfa4ce8b1b809d63bd4f45`` (2026-08-17).
* The local 128 MiB reference SD image, SHA-256
  ``85b04772c2ce4c51af69a16c946d4008191a27456edab8f682929cde7ef03266``.
* QEMU baseline commit
  ``9696bf5dc5a5bf0b4a9d05b6cdfe5f13990f97aa`` (2026-08-16).

Upstream reference documents and software are available from the
`Microchip SAM9X75 product page
<https://www.microchip.com/en-us/product/sam9x75>`_, the
`Linux4Microchip board page
<https://developerhelp.microchip.com/xwiki/bin/view/applications/linux4sam/Boards/sam9x75curiosity/>`_,
and the `Microchip Device Tree overlay repository
<https://github.com/linux4microchip/dt-overlay-mchp>`_.

Fidelity rules
--------------

``Initial`` means that a tested functional subset exists.  ``Complete`` will
only be used after reset values, writable masks, access sizes, interrupts,
DMA, clocks, reset domains, migration state, guest drivers, and relevant
silicon errata have tests.  ``Hardware`` marks behavior whose authoritative
answer requires a differential probe on the physical board.

No QEMU-only device tree is used to conceal missing hardware.  Supported
firmware and operating systems must consume the same DTB as the board.

The machine has not been released or assigned a versioned migration ABI.
Migration tests primarily cover same-build operation.  Selected tests accept
``QTEST_QEMU_BINARY_OLD`` for targeted compatibility checks, but development
snapshots and migration streams are not generally guaranteed to load across
commits.  Freeze or version the machine topology before making that
compatibility promise.

Support matrix
--------------

.. list-table:: SoC and board coverage
   :header-rows: 1
   :widths: 25 15 60

   * - Area
     - Status
     - Required coverage
   * - ARM926EJ-S
     - Initial
     - VFP is disabled.  The Armv5 and Armv6 short-descriptor walkers fetch and
       classify an L2 descriptor before applying the domain access control
       inherited from L1.  On the physical ARM926EJ-S, a valid coarse L1 entry
       in no-access domain 4 followed by an invalid L2 entry produced DFSR
       ``0x47`` (L2 translation fault), rather than the premature page-domain
       fault formerly returned by QEMU.  Bare-metal TCG tests also preserve
       valid-page, section-domain and external-L2-walk priorities.  MIDR, CTR,
       the remaining CP15 reset state, cache/MMU corner cases, FCSE, Jazelle
       and unaligned-access behavior remain under audit.  Nested stage-2 walk
       fault-level preservation in the Armv6 walker is a separate generic
       QEMU follow-up and does not affect this ARM926 configuration.
   * - ROM and boot window
     - Initial
     - The proprietary 80 KiB RomBOOT is visible at address zero after reset;
       CPU-host MATRIX remap switches address zero to the 64 KiB SRAM0 view
       and is covered across reset and migration.  The separate 96 KiB ECC ROM
       at ``0x00100000`` contains the GF(2^13) and GF(2^14) lookup tables used
       by PMECC correction software.  QEMU synthesizes those deterministic
       tables, so firmware entered with ``-kernel`` can use them without a
       proprietary ROM image.  An exact 80 KiB user-supplied RomBOOT dump can
       be loaded with ``-bios`` and executes from the reset view; it does not
       replace the independent ECC tables.  QEMU does not distribute the
       proprietary boot ROM.  The keyed ``BOOT[2:0]`` register in the
       VDDBU-powered Boot Sequence Controller is mapped at its documented
       address and retains its value across core resets.  It restores its
       configurable factory value when VDDBU is removed, persists for the next
       RomBOOT execution, and migrates.  The boot chapter calls bit 0
       ``EMUL_EN`` while the controller chapter and 2026 Microchip device pack
       call the complete field ``BOOT[2:0]``; values 2--7 need hardware
       characterization.  Genuine-image validation, RomBOOT use of the
       modeled OTPC contents for media selection, authentication/error
       fallbacks and revision errata remain incomplete.
   * - Bus MATRIX
     - Initial
     - All 14 host and 12 client configuration/priority banks, remap control,
       error interrupt mask/status/address banks, write protection, permanent
       configuration freeze and migration state have their documented reset
       values and masks.  Arm926 instruction/data remap bits 12/13 drive the
       shared QEMU CPU boot view.  Per-host address spaces for the other bus
       initiators, arbitration timing and automatic access-error capture are
       not yet modeled.
   * - SRAM and DDR3L
     - Initial
     - 64 KiB SRAM0, 4 KiB SRAM1 and fixed 256 MiB DDR are present.  The DDR
       window is unavailable at the hardware reset value and follows
       ``SFR_CCFG.EBI_CS1A``; disabling and reassigning the window preserves
       its contents, and the assignment follows reset and migration.  Direct
       Linux boot synthesizes only the firmware-established ``EBI_CS1A`` bit,
       while mask-ROM and AT91Bootstrap boot retain the true reset value.
       SRAM1 can hold the OTPC emulation packet chain selected by ``MR.EMUL``
       and an unkeyed ``REFRESH`` command.  The BSC request is state that
       RomBOOT reads; there is deliberately no direct BSC-to-OTPC hardware
       signal.  MPDDRC
       configuration, refresh, error reporting, interrupts and write
       protection have an initial register model.  Runtime low-power-register
       updates, the arbitration masks and write-after-initialization safety
       reporting follow the documented register policy; DDR timing/training
       remains missing.
   * - DBGU and chip identification
     - Functional
     - Full SAM9X7 aperture, byte and word data accesses, masks, timeout,
       write protection, loopback, CIDR/EXID, clock gating and XDMAC requests
       are implemented.  Bit-level line timing, break/error injection and DCC
       transport remain.
   * - AIC and system IRQ aggregation
     - Initial
     - 128-source AIC5 register bank with SAM9X7 wiring, priority/nesting,
       IRQ/FIQ, edge/level modes, fast forcing, general mask, spurious vector
       and write protection.  The busless system and EBI interrupt OR gates
       restore their output after migration, deassert on cold reset and
       re-drive retained sources after a core reset.  Protection-mode corner
       cases need hardware tests.
   * - PMC, SCKC and clocks
     - Initial
     - Main and slow oscillators, five SAM9X7 PLL clock entries, master,
       programmable, generated and peripheral clocks, gating, interrupts and
       write protection are modeled.  ``PMC_SR`` resets to ``0x00030008``;
       ``CKGR_MOR`` reset readback is ``0x00000028`` and persistent writable
       state is limited by mask ``0x6700ff09``.  ``PMC_PCKx.CSS`` produces a
       clock only for MD_SLCK, TD_SLCK, MAINCK, MCK, PLLA, UPLL and AUDIOPLL
       (sources 0--6); reserved selectors read back but produce no clock.
       Peripheral and generic clocks follow the DS60001813E per-PID
       availability and source matrix.  This includes revision E's PID 67 GMAC
       TSU peripheral clock and its MD_SLCK, TD_SLCK, MAINCK, MCK, PLLADIV2 and
       AUDIOPLL GCLK sources.  PLLADIV2 is ID 4's gate on the PLLA fractional
       core divided by four: its own ``DIVPMC`` and ``ENPLL`` do not affect the
       rate, while ID 4 ``ENPLLCK`` and ID 0 ``ENPLL`` do.
       PLLA and PLLADIV2 follow MAINCK; UPLL, AUDIOPLL and LVDSPLL require the
       ``MOSCXTEN``-enabled main crystal oscillator independently of
       ``MOSCSEL``.  Erratum DS80001082H section 5.3 is modeled: PCK and GCLK
       ready status follows clock enable state rather than source activity or
       division changes.  SCKC selects the modeled RC/crystal slow clocks.
       Oscillator start-up/failure-monitor timing and the first-PCK
       255-source-cycle delay are not modeled; aggregate and empty-set
       ``GCLKRDY`` behavior remains queued for differential hardware
       validation.
   * - PIOA--PIOD and pinctrl
     - Initial
     - SiP-specific valid lines and reset pulls, GPIO and open-drain drive,
       peripheral A--D muxing, glitch/debounce filters, clock gating, all
       edge/level interrupt modes, write protection, Schmitt, drive and slew
       state are modeled.  Peripheral signal routing and electrical hardware
       comparison remain.
   * - Reset, shutdown and timekeeping
     - Initial
     - WDT has its startup-enabled down-counter, window/level events,
       interrupts, lock and key rules, synchronization guard, reset policy and
       migration state.  RSTC has keyed user/external reset control, status,
       interrupt behavior and General/Backup/Watchdog/Software/User reset
       causes.  Software and Watchdog processor reset remain asserted for
       three MD_SLCK cycles.  Synchronous User reset is sampled after two
       cycles and is released six cycles after NRST rises; its input state and
       all active reset timers migrate.  The external ERSTL pulse runs
       independently.  For newly competing modeled sources, reset arbitration
       follows Backup over Watchdog over Software over User; an already-held
       User reset remains asserted until NRST rises.  ``RSTTYP`` changes only
       when processor reset is actually released, including a Software or
       Watchdog reset continued by a held User reset.  The revision A1
       ``RSTTYP`` erratum is modeled:
       initial and subsequent General resets report Backup, while the other
       causes keep their documented encodings.  MCP16502 nRSTO drives the SoC
       NRST power-reset input: backup exit and warm reset reset VDDCORE devices
       while the VDDBU-powered SYSCWP, BSC, GPBR, RSTC, RTT, RTC, SHDWC and
       SCKC retain state.  The processor and modeled embedded VDDCORE
       peripherals enter a private reset domain at the assertion edge and
       remain there until the RSTC release edge; that held domain is rebuilt
       after migration, and the Watchdog stays stopped while held.  External
       SD cards, QSPI flash, PMIC and power monitor retain their device state
       while their SoC controllers reset.  SHDWC has
       keyed two-slow-clock shutdown, SHDN,
       programmable WKUP0 polarity/debounce, raw RTC/RTT alarm wake-up,
       read-clear status, system write protection, VDDBU retention and
       migration state.  The calendar/UTC RTC includes updates, alarms,
       waveform outputs, correction, tamper timestamps, protection and
       migration; RTT includes slow-clock prescaling, RTC 1 Hz selection,
       alarm and modulo events.  GPBR models its eight retained words,
       individual write-once protection and tamper clearing.  Both PIT64B
       instances have clocked one-shot/continuous operation, interrupts,
       protection, sequence errors and migration coverage.  Remaining reset
       and wake causes need hardware comparison.
   * - FLEXCOM0--12
     - Initial
     - All thirteen wrappers expose mode ownership, shared AIC routing and
       shared XDMAC request routing.  Every wrapper has a USART and TWI
       personality and a uniquely named I2C bus.  Only FLEXCOM0--5 synthesize
       an SPI personality; those six instances have uniquely named SSI buses,
       while FLEXCOM6--12 have no SPI child or SSI bus.  The model currently
       leaves their nominal ``+0x400`` slots unmapped; the exact reserved-slot
       access behavior remains a hardware-validation item.
       The common USART path has the SAM9X7 register masks and reset values, a
       16-byte FIFO, byte/halfword/word
       transfers, clocked character-backend transmission and reception,
       local/automatic/remote loopback, timeout and comparison events, write
       protection, migration and its documented XDMAC transmit/receive pair.
       The silicon implements Basic, hardware-handshake and RS485 USART modes
       on all thirteen instances.  FLEXCOM0--3 additionally implement
       ISO7816, LIN, IrDA, Manchester and LON; FLEXCOM4--5 implement the same
       advanced modes except LON.  The model does not yet enforce that
       per-instance advanced-mode/register matrix.
       Each of the six SPI personalities has the SAM9X7 register layout,
       masks and write protection, 16-entry transmit/receive FIFOs, 8--16-bit
       host transfers, fixed and variable peripheral selection, direct and
       decoded chip selects, PCLK/GCLK bit-rate selection, programmable
       chip-select and transfer delays, local loopback, comparison,
       interrupts, migration and its documented XDMAC pair.  The host data
       path uses a normal QEMU SSI bus, and FLEXCOM4 NPCS1 reaches the M.2
       socket through J24.  FLEXCOM0--3 have two physical NPCS signals and
       FLEXCOM4--5 have four.  The behavior of CSR and PCS encodings that do
       not have a corresponding package signal is not inferred from the pin
       count and still requires hardware readback.
       The TWI host path covers PCLK/GCLK-paced byte transfers, internal
       addresses, repeated starts, NACK and AIC signaling,
       alternative-command mode, 16-byte transmit/receive FIFOs, byte,
       halfword and word accesses, ready modes, level and threshold flags,
       overflow/underflow errors, clock gating, migration and each wrapper's
       documented XDMAC transmit/receive pair.
       SPI client mode and pin-level mode-fault/framing input, CRC and two-pin
       engines, bit-level USART framing and synchronous/protocol engines,
       per-instance USART feature enforcement,
       complete flow-control endpoints, TWI client mode, SMBus/PEC,
       high-speed timing, separately timed address/START/STOP phases and
       arbitration fidelity remain missing.  SAM9X7 documentation and its
       device pack do not define the legacy SPI version word at offset
       ``0xfc``; the model conservatively returns zero until the reserved read
       and Linux capability-detection behavior can be measured on hardware.
   * - XDMAC
     - Initial
     - All 16 channels expose global/channel control, clock gating, memory copy
       and memset, byte/halfword/word widths, address modes and 2D strides,
       software and external-request pacing, linked-list descriptor views and
       completion/error interrupts.  Register-level suspend, resume and disable
       are present.  Peripheral-to-memory channels, including linked
       descriptors, cyclic rings and blocks containing multiple microblocks,
       use the GTYPE-reported 256-byte per-channel FIFO.  The next microblock
       or descriptor is not started until the preceding microblock's staged
       data has reached memory.  MBSIZE staging, byte/halfword/word residue,
       independent GRS/GWS read/write suspension, GSWF snapshot drain and FIS,
       graceful GD/DIS ordering, RDIP/WRIP state, write errors and live FIFO
       migration are covered.  Waiting source reads can proceed while a finite
       flush snapshot drains; GD preserves an already scheduled final flush
       write so FIS precedes DIS.  CUBC and the destination address advance
       only after a successful destination write.  Bounded per-channel work
       and an idle reschedule fence prevent continuously requested cyclic
       rings from starving QEMU's event loop or their peer channels.
       Targeted ``QTEST_QEMU_BINARY_OLD`` runs exercise version-2 simple-FIFO
       and mid-CBC state.  Same-build migration covers staged data in a view-3
       receive descriptor with two microblocks, a scheduled flush followed by
       graceful disable, an enabled receive descriptor awaiting FLEXCOM0 USART
       data and an enabled, suspended USART transmit channel that resumes and
       completes after migration.  Request-paced view-1 cyclic periods,
       mid-period flush and disable, and a held-request ring longer than one
       work budget are also covered.  DBGU, all thirteen FLEXCOM USART and TWI
       personalities, the
       six FLEXCOM0--5 SPI
       personalities, I2SMCC, Class-D, AES, SHA and TDES request lines are
       wired.  GWAC pool weighting and CNDC/descriptor QOS effects, the
       remaining peripheral request lines, security policy, bus/burst and
       arbitration timing, and coherency effects remain missing.  DMA memory
       accesses are synchronous in QEMU, so positive RDIP/WRIP intervals can
       be too short for software to sample.
   * - SDMMC0 and SDMMC1
     - Initial
     - Both SAM9X7 hosts, removable-card attachment and PA23 card detect are
       wired.  J24 defaults to SDIO and routes an optional second SD drive to
       the M.2 socket through SDMMC1.  Its SPI position electrically
       disconnects that host and attaches the same drive to FLEXCOM4 NPCS1;
       a board qtest clocks the card and receives its SPI-mode CMD0 response.
       Host preset registers, the SAM9X7 Host Control 2 writable mask, combined
       command/data/all software reset, the vendor registers covered by
       ``SWRSTALL``, and the Linux ADMA descriptor form are covered.  Writable
       Host Control 2 state survives migration.  A physical Curiosity LAN Kit
       exposed Linux's valid ``END|NOP`` descriptor with zero length.  One
       captured 4 KiB transfer advanced the system address past that
       terminator to table base plus 16 bytes with no ADMA error.  A wider run
       captured 83,837 snapshots, including 2,055 snapshots of tables
       containing 128 transfer descriptors; every snapshot contained the same
       zero-length terminator and reported no ADMA or error-interrupt status.
       Descriptor-fetch
       and data-transfer bus faults stop ADMA in the documented fetch and
       transfer states respectively; they cannot spuriously process ``INT`` or
       ``END``, complete the transfer, or retry after the fault.  Incomplete
       descriptor tables report an ADMA length mismatch without raising
       transfer-complete; ``INT`` still raises DMA status when its descriptor
       line itself completed.  At QEMU commit ``cd0edc328d6e``, the pinned
       AT91Bootstrap SD/ADMA path loaded unmodified U-Boot, which initialized
       DDR, NAND, MMC and QSPI and loaded Linux from SD.  Linux reached its
       embedded-initramfs shell.  A disposable 512 MiB derivative that
       preserved the reference image's FAT partition and added ext4 partition
       2 then proved a read/write ``/dev/mmcblk0p2`` mount, ``switch_root``, a
       disk-root shell and clean power-down.  The original 128 MiB image itself
       has only the FAT partition, so it cannot satisfy its
       ``root=/dev/mmcblk0p2`` command line.  The exact-head diagnostic log
       contained only the expected generic CMD1/CMD5/CMD52 media probes.  SDIO
       I/O functions, full M.2
       ``mmc_spi`` guest integration and dynamic card-detect/media-change
       completeness remain under audit.
   * - OSPI/QSPI NOR
     - Initial
     - Controller and XIP windows, SST26VF064BEUI identity, SFDP/EUI data,
       program/erase and U-Boot probing are modeled.  Erase commands select the
       containing erase unit even for unaligned addresses, with an end-of-flash
       bounds regression test.  The EUI-48 follows the configured GEM MAC
       address.  Controller status follows the documented read-clear and
       command-clear rules, including overrun, last-write, timeout, transmit
       readiness and chip-select autoclear flags; IRQ, reset and migration
       tests cover those transitions.  Persistence with a drive, all protocol
       widths, timeout generation, the enable-time ``RFRSHD`` policy with
       ``DQSDLYEN`` clear, and the ROM quad-mode erratum need further coverage.
   * - EBI/SMC and raw NAND
     - Initial
     - The Curiosity U5 interface occupies the complete 256 MiB CS2 window and
       is available only when ``SFR_CCFG.EBI_CS2A`` and
       ``SFR_CCFG.NFD0_ON_D16`` are both set; J9 independently controls its
       physical chip enable.  ALE on A21 and CLE on A22 repeat the protocol
       view through all 32 8 MiB slices because A23--A27 are ignored.
       Assignment state follows reset and migration.  A processor-only reset
       clears the assignment while retaining external NAND protocol state; a
       whole-system reset also resets that protocol state but preserves flash
       contents.  The MX30LF4G28AD identity and its first eight known
       ONFI parameter-page copies match the device data sheet.  After the
       documented pre-data
       ``70h`` completion poll, ``00h`` enables parameter, page and Get Features
       transfer; that poll-to-transfer state also survives migration.  Set
       Features accepts P1--P4 before updating volatile readback and implements
       the documented P1 masks and reset values for board-valid registers.  A
       malformed interruption before P4 follows a deterministic emulator policy
       that has not been compared with hardware.  The resulting volatile
       feature state survives ``FFh`` and SoC core resets but not a power-on
       reset.  Feature address ``A0h`` is invalid because the board
       leaves the NAND PT pin unconnected and its internal pull-down holds PT
       low.  Page and OOB program/read/erase paths, including the AT91Bootstrap
       ``80h``/``85h``/``10h`` Random Data Input sequence, are present.  Active
       ordinary and random-input programs, device-owned sparse/OOB state and
       shared backend data have migration coverage.  The SMC chip-select banks,
       write protection, write-once scrambling keys, safety reporting and
       level interrupt shared with MPDDRC are present, including the modeled
       A1 ``OCMS`` write-protection erratum.  PMECC is connected to the NAND
       data path and implements automatic main-area BCH encoding and syndrome
       generation for 512- and 1024-byte sectors, strengths 2, 4, 8, 12 and
       24, and one, two, four or eight sectors.  It exposes all eight ECC and
       remainder banks through its complete ``0x600`` aperture, reports
       per-sector errors, and implements the GF(2^13)/GF(2^14) PMERRLOC Chien
       search and shared interrupt.  Active encode and decode streams migrate.
       The current upstream Linux ``sam9x7.dtsi`` describes only ``0x300``
       bytes for that first resource and needs a separate correction before
       software can map the upper banks.  In the 2026-08-25 integration gate,
       unmodified AT91Bootstrap 4.0.13
       ``sam9x75_curiositynf_uboot_defconfig`` detected the ONFI device,
       selected timing mode 3, initialized eight-sector 512-byte/BCH8 PMECC,
       copied the configured 1 MiB image from NAND offset ``0x40000`` into DDR
       and reached the unmodified U-Boot prompt.  An authentic raw image and
       variants with two and eight corrupt data bits in the first loaded
       sector produced byte-identical serial transcripts and empty
       ``-d unimp,guest_errors`` logs.  The error cases execute
       AT91Bootstrap's syndrome expansion, Berlekamp-Massey processing,
       PMERRLOC programming and Chien search, and data-bit correction.
       Read, parameter-page, Get/Set Features, program, erase and reset
       operations complete asynchronously on the virtual clock.  The model uses
       the MX30LF4G28AD data-sheet maximum intervals: 25 us for reads, 1 us for
       Get/Set Features, 700 us for ordinary program, 740 us when ``RANDEN`` is
       set, and 6 ms for erase.  Reset takes 5 us while idle or when
       interrupting a read or feature operation, 10 us when interrupting
       program, and 500 us when interrupting erase.  Status
       ready bits 5 and 6 stay clear until completion.  Program and erase media
       changes are deferred until their timer expires and become model-visible
       at completion.  ``70h`` Status and ``FFh`` Reset remain available while
       busy.  ``78h`` Enhanced Status consumes three row cycles and selects the
       single-plane program/erase result through row bit 6; ready bits 5 and 6
       are shared across both planes.  As on the device, ``78h`` is rejected
       during Reset.  Other command, address and data cycles are ignored while
       busy.  Pending operations, captured addresses, status protocol and timer
       deadlines migrate without a ready-line pulse.  On Curiosity, U5 R/B# and
       its R32 pull-up form a resolved active-high ready signal on PIOD14,
       matching the board device tree's ``GPIO_ACTIVE_HIGH`` ready GPIO.

       The delays are fixed data-sheet maxima and have not yet been correlated
       with this physical NAND.  QEMU asserts busy immediately and does not
       model ``tWB``, power-on busy time, timing-mode-dependent latency,
       electrical open-drain release/rise behavior or native SMC ``NWAIT``
       access stretching.  ``FFh`` deterministically discards an interrupted
       program or erase before any modeled media change; silicon may leave
       partially modified media.  The additional redundant parameter pages,
       PMECC spare-area (``SPAREEN``) and manual ``USER`` phases,
       PMECC/PMERRLOC
       timing and clock-gating fidelity, complete bad-block/OOB behavior,
       unique-ID, cache, copyback, interleaved, two-plane, protection,
       recovery-read and randomizer data-path effects, I/O-strength and
       randomizer default-fuse programming, invalid-feature-address readback,
       page-program order/NOP/endurance constraints, storage-error handling and
       DMA remain missing.
   * - GEM and LAN8840
     - Initial
     - GEM0 has six priority queues, DMA transmit/receive, AIC sources 24 and
       60--64, a Clause 22 PHY at address 1 with the LAN8840 identifier, and a
       station address provisioned through the SST26VF064BEUI.  Unmodified
       U-Boot obtained a DHCP lease and exchanged packets in the earlier
       fixed-decode validation.  The exact-head SD repeat re-proved ``eth0``
       discovery but did not repeat packet traffic.  J12 defaults closed and
       supplies the LAN8840's required 25 MHz reference clock;
       opening it makes MDIO inaccessible and prevents external RGMII
       traffic.

       Statistics implement their documented 32-, 18-, 16-, 10- and 8-bit
       widths, saturation, read-to-clear behavior, ``WESTAT``, ``INCSTAT`` and
       ``CLRSTAT``.  The octet counters use the documented 48-bit low/high
       order; VMState version 5 converts the reversed version 4 representation
       and restores the interrupt outputs.  DMA-backed tests cover IPv4/IPv6
       multicast, broadcast exclusion, command ordering, counter limits and
       migration, including a real version 4 source binary.

       Physical hardware confirmed the octet-half order, read-to-clear and
       clear-all behavior, and counting both ``01:00:5e`` and ``33:33``
       destinations by the Ethernet I/G bit.  Broadcast exclusion and the
       ordering of commands combined in one ``NWCTRL`` write remain model
       conventions pending direct hardware checks.  Priority-queue interrupt
       mask reset values also need an early bare-metal read: the SAM9X75 device
       pack exposes five priority-queue masks with ``0x000008e6`` valid bits,
       so the generic Q1-only ``0x00000ce6`` behavior must not simply be copied
       to every queue.  LAN8840 MMD/RGMII delay registers, PHY reset-value
       fidelity, checksum corner cases, filtering and PTP/TSN remain.
   * - USB host and device
     - Initial
     - UHPHS exposes the documented 1 MiB OHCI and EHCI windows, three
       companion ports and their shared AIC source 22.  Tests cover host
       hotplug, EHCI/OHCI ownership handoff, all OHCI root-hub power modes,
       EHCI frame-list sizes, descriptor and payload DMA faults, reset,
       interrupt reconstruction and same-build controller migration.  OHCI
       migration carries partial asynchronous-packet metadata, validates its
       owning schedule and cancels a guest unlink, halted/skipped ED or pending
       control/bulk-list disable before child-device state is saved.  The
       destination reconstruction path is exercised with a real asynchronous
       ``usb-storage`` WRITE held at a retryable block error across live
       migration, then resumed through a successful mass-storage status
       packet.  The host schedules observe the SAM9X7 clock tree: PID 22 gates
       both controllers, UPLLCK gates the EHCI UTMI path, and the
       ``PMC_USB`` PLLA/UPLL selection and divider plus ``PMC_SCER.UHP`` gate
       the OHCI 48/12 MHz path.  Disabling a required clock freezes rather
       than advances a schedule, re-enabling resumes without catch-up, and
       clock state migrates.  Physical-board host-controller clock, reset,
       power and DMA behavior has not yet been compared.

       UDPHS exposes the documented 1 MiB endpoint-FIFO aperture at
       ``0x00500000``, its 1 KiB register aperture at ``0xf803c000``,
       endpoints 0--6, internal DMA-channel register files 1--6, PID 23 and
       UTMI clocks, and AIC source 23.  A raw-token USB gadget bridge lets an
       emulated host deliver staged SETUP transactions to control endpoints
       0--6 and PIO IN/OUT packets instead of collapsing control transfers in
       the generic USB-device layer.  Complete control-IN and control-OUT
       stages on zero and nonzero endpoints, multi-bank PIO, FIFO byte counts
       and endpoint interrupts are covered.  Endpoint 0 is control-only;
       control endpoints use one bank, accept 8--64-byte packets and ignore
       ``EPT_DIR``;
       isochronous endpoints require at least two banks; bulk and interrupt
       endpoints accept at most 512-byte packets; and only isochronous
       endpoints accept 1024 bytes.  Control mode ignores ``AUTO_VALID`` and
       cannot use the endpoint DMA channels.  ``MAPD`` remains a DPRAM resource
       result,
       so a resource-valid but operationally unsupported configuration still
       maps and stalls tokens.  Direct-buffer and three-word linked-descriptor
       DMA implement IN prefetch/refill, OUT drain, 64 KiB counts, ZLP and
       short-packet completion, read-to-clear status, chaining, memory errors
       and active migration.  ``INTDIS_DMA`` suppresses DMA requests from
       enabled local endpoint causes and resumes them when firmware clears or
       disables the cause, changes FIFO state or resets the endpoint.  Endpoint
       disable resets protocol metadata without erasing DPRAM.  Isochronous
       endpoints ignore ``FORCESTALL``; an empty ordinary IN transaction has no
       wire response, a negotiated high-speed ``NB_TRANS > 1`` IN transaction
       supplies the documented default ZLP, and a full OUT queue consumes and
       drops the transaction while recording ``ERR_FL_ISO``.  DMA execution is
       synchronous and ``BURST_LCK`` has no timing effect.  Full ``NB_TRANS``
       multi-transaction microframe scheduling, isochronous DATAX/MDATA
       termination, SOF/suspend/resume timing, a host-facing USB cable backend
       and SAM-BA remain.  ``UDPHS_CTRL.EN_UDPHS`` disconnects and blocks UHPHS
       high-speed Port A while UDPHS owns the shared UTMI transceiver,
       matching the hardware mux.  Gadget-bridge cable presence drives the
       board's active-high VBUS sense on PC8.  The controller, shared-port mux,
       VBUS path and DMA behavior still need comparison with the physical
       board.
   * - I2C board devices
     - Initial
     - The exact MCP16502TAB-E/S8B PMIC is present on FLEXCOM6 with OTP
       defaults, writable masks, Active/Low-Power/Hibernate/HPM regulator
       selection, live status, SHDN/PWRHLD and nSTRT/nSTRTO board signals,
       programmable long-press timing, PBINT/nINTO, forced shutdown and all
       six programmable rail start-up paths.  Per-rail delay and soft-start
       completion drive ENS/SSD/POK before the programmable nRSTO release;
       hibernate-preserved rails and every pending deadline migrate.  nINTO
       drives PA12, while nRSTO drives the SAM9X75 NRST power-reset input after
       all selected rails become valid.  FLEXCOM7 carries
       the PAC1934 with its sparse variable-width register loops, delayed
       REFRESH/REFRESH_V activation, channel skipping, four voltage inputs,
       virtual-time power accumulation, overflow signaling, migration state
       and the shared PB18 SLOW/ALERT connection.  The current-sense inputs
       default to zero and can be driven through QOM properties for workload
       or hardware-trace replay.  The exact upstream board DT probes both
       Linux drivers.  All six regulator states and voltages and the PAC1934
       labels, sampling rate and four voltage channels are verified before and
       after ULP0 suspend/resume.  Conversion-complete alert pulses, SMBus
       timeout/electrical details and hardware-calibrated telemetry remain.
       No EEPROM is populated on the Curiosity base board; unlike the
       SAM9X75-EB configurations, the Curiosity AT91Bootstrap configurations
       do not enable EEPROM loading.  Optional connector-side I2C devices are
       expansion hardware rather than hidden board devices.
   * - LEDs and push buttons
     - Initial
     - PC14, PC20 and PC21 drive observable red, blue and green LED devices.
       Keyboard ``0``, ``W``, ``R`` and ``S`` operate the active-low USER,
       WKUP, RESET and START switches.  USER drives PC9; RESET enters RSTC
       through NRST; WKUP and the PMIC nSTRTO output share WKUP0; and START
       enters the PMIC through nSTRT.  START implements both programmable
       SYS-TMG timeout stages, reset-on-read servicing and oscillator timing
       displacement.  LED intensity and every switch path have board qtests.
   * - Timers, ADC, PWM and SSC
     - Initial
     - TC0 and TC1 each expose the modeled ``0x100``-byte, three-channel TCB
       subset with independent state, GCLK/MCK-divided/slow-clock selection,
       PMC gating, free-running and RC-reset waveform timing, synchronization,
       periodic and one-shot interrupts, write protection and active-timer
       migration.  TC0 uses PID/AIC source 17 and TC1 uses PID/AIC source 45.
       The TC0 instance covers the unmodified Linux clocksource and clockevent
       paths.  The common TCB model does not yet cover external TCLK/TIO
       capture and waveform routing, up/down modes or QDEC.  The SAM9X7 ADC is
       mapped at ``0xf804c000`` with its eight physical inputs, PID/AIC source
       19, peripheral and generic clocks, and XDMAC receive request 40.  It
       models numeric and programmable channel sequences, software, external,
       periodic and continuous triggers, conversion timing, enhanced
       resolution, comparison, result/overrun status, interrupts, halfword
       DMA, write protection, VDDCORE reset and migration.  Input and reference
       voltages are injectable in microvolts through QOM properties.  Register
       tests decode reserved locations, including the absent ``0xfc`` version
       register, without guest-error logging.  The revised data sheet gives
       ``ADC_ACR`` reset value ``0x1200`` while the 2026 device pack gives
       ``0x0101``; the model provisionally follows the revised data sheet until
       safe hardware readback settles the discrepancy.  Touchscreen sampling,
       differential/sign modes, correction behavior and physical trigger
       routing still need implementation or hardware comparison.  The current
       Linux fallback compatible describes a SAMA5D2 ADC and exposes channels
       8--11 that do not exist on SAM9X7; QEMU deliberately keeps the physical
       eight-channel register map.  PWM outputs and synchronous serial
       operation also remain.
   * - Audio
     - Initial
     - I2SMCC register state, clocking, mono/compact/TDM framing, interrupts,
       loopback and bidirectional XDMAC requests are modeled.  Class-D has
       FIFO, clocked sample consumption, interrupt, write-protection, XDMAC and
       sample-sink paths.  The unmodified Linux drivers probe both controllers.
       QEMU audio-backend output, exact serial framing, underrun behavior and
       the analog output path remain.
   * - CAN FD
     - Initial
     - Both Bosch M_CAN instances are mapped with their peripheral and generic
       clocks, AIC sources 29/30 and 68/69, shared SRAM0 message RAM and
       independent QEMU CAN-bus links.  Classic CAN and CAN FD/BRS transmit,
       standard and extended range/dual/classic acceptance filters, XIDAM,
       global nonmatching/reject policy, high-priority status, receive FIFOs 0
       and 1, dedicated receive buffers with NDAT locking, FIFO blocking and
       overwrite modes, watermarks, acknowledgements, transmit events,
       internal loopback, interrupt routing, Message-RAM fault atomicity,
       clock gating, reset and migration have qtests, including traffic
       between the two controllers.  SAM9X7 DBTP/TEST/RWD masks, timeout
       reload selection, the TSCFG/ATB destructive-read behavior, effective
       transmit FDF/BRS/ESI normalization without changing guest Message RAM,
       and disabled, internal and reserved timestamp-source selection are
       also covered.  Successful-transfer LEC/DLEC updates, the last received
       CAN-FD format indicators, PSR read side effects and ECR.CEL
       read-to-clear behavior follow the Bosch interface.  Bus-error counters,
       confinement/retry, bit-level arbitration, debug-message handling,
       timestamp synchronization pins, the external timestamp source and
       precise timestamp-unit timing remain incomplete.
       A Linux
       7.2.0-rc7-next-20260814 guest with a DT overlay probes ``can0`` and
       ``can1`` and passes bidirectional
       CAN FD/BRS traffic (including a 64-byte frame) plus classic CAN traffic
       at 500 kbit/s nominal and 2 Mbit/s data rates.  The upstream Curiosity
       board DTS leaves both CAN nodes disabled, so normal use still needs an
       overlay or alternate DT.  Reset constants, the CCE reset/loss-status
       interaction, physical interrupt behavior and external-bus operation
       remain for board validation; that work needs the appropriate header
       pinmux and a CAN transceiver rather than a direct connection to the SoC
       pins.
   * - Crypto, TRNG, OTP and PUF
     - Initial
     - AES has 128/192/256-bit keys; ECB, CBC, OFB, CFB8/16/32/64/128, CTR,
       GCM and XTS processing; manual, automatic and DMA data ports; AIC and
       XDMAC request wiring; write protection; and migration state.  NIST and
       IEEE vectors cover chaining, GCM AAD/tag generation, the Linux GCM
       mode transition and paired XDMAC transfers.  The unmodified Linux
       driver probes version 0x700 and acquires both DMA channels.  The
       version value requires hardware confirmation; processing timing,
       double buffering, auto-padding, the AES/SHA protocol path, private-key
       bus and security-event behavior remain incomplete.  TRNG provides
       guest-random 32-bit values after the clocked 84/168-cycle interval,
       including HALFR and DIFF modes, PMC gating, AIC source 38, interrupt
       masking and clear-on-read status, write protection, software-access
       reports and migration state.  The unmodified Linux hwrng driver
       completes its runtime-PM read path.  Private-key-bus data delivery,
       fault injection and hardware entropy characteristics remain missing.
       SHA supports SHA-1/224/256/384/512 and their HMAC variants, standard and
       user initial values, manual/automatic/IDATAR0 start modes, automatic
       padding and digest checking, clocked processing, AIC source 41, XDMAC
       request 34, write protection, security-access reports and migration.
       Known-answer coverage includes every hash width, RFC HMAC-SHA256,
       software and automatic padding, both check modes, timing and multi-block
       DMA.  The unmodified Linux driver probes version 0x700 and acquires its
       DMA channel.  SHA double-buffer performance, Always-On dummy processing,
       exact check latency, tamper/fault injection and the AES/SHA protocol path
       remain incomplete; version and timing values require hardware
       confirmation.  TDES supports DES, two- and three-key TDES and XTEA;
       ECB, CBC, OFB and CFB8/16/32/64; manual, automatic and DMA start modes;
       the CBC-MAC last-output path; clocked 18/50-cycle DES/TDES processing;
       AIC source 40; XDMAC requests 31/30; write protection, security-access
       reports and migration.  Known-answer tests cover encryption and
       decryption for every algorithm and mode, exact DES/TDES completion
       timing, paired DMA and TX-only CBC-MAC.  The unmodified Linux driver
       probes version 0x700 and acquires both DMA channels.  XTEA register-word
       ordering and timing, the version value, private-key bus, tamper and
       fault-injection behavior require hardware confirmation.  OTPC is
       mapped at ``0xeff00000``.  With no backing image it provides a blank,
       VM-local 10 KiB physical OTP array on each QEMU launch.  An optional
       raw backend can seed or persist that array; it must contain exactly
       10240 bytes, interpreted as 2560 little-endian 32-bit words.  A backend
       is immutable to the guest by default.  Persistent mutation requires
       the explicit ``otpc-write-enable=on`` machine option and remains
       one-way: programming may only change zero bits to one bits.  A writable
       backend blocks migration and ``savevm`` so that a snapshot cannot
       silently diverge from the externally modified OTP image.  Without a
       writable backend, the physical array and controller state migrate.  A
       write or flush error permanently inhibits further physical programming
       for that QEMU process, because the medium's irreversible state may be
       ambiguous after an I/O failure.

       ``MR.EMUL`` records whether emulation is requested; ``REFRESH`` makes
       that request active and updates ``SR.EMUL`` (this command is unkeyed).
       Active emulation parses a packet chain from the 4 KiB SRAM1 and never
       modifies the physical-OTP backend.  Regular, key, boot, secure-boot,
       hardware and custom packet types are recognized, with the last
       applicable special packet supplying ``BAR``, ``CAR`` and ``UHC``
       values.  Reads, temporary writes, flush, new-packet and whole-word
       update programming, packet invalidation, volatile packet hiding,
       interrupts, write protection and migration are modeled.  Programming
       validates packet layout before changing the source and writes a new
       header last; structural or one-way conflicts are distinguished from
       unavailable or failed physical storage through the documented status
       paths.  Hiding suppresses subsequent payload reads, survives refresh
       and migration, and clears on controller reset.  The global UHC
       read/program/refresh disables and the special-packet program,
       invalidation and lock-command gates are enforced.

       The four documented device-unique-ID registers at ``0x60``--``0x6c``
       are read-only and configurable through QOM properties; their default is
       zero because the data-sheet reset value depends on hardware
       configuration.  Aligned reserved words in the documented register
       aperture decode as read-zero/write-ignore rather than bad offsets.  The
       data sheet marks ``0x70``--``0xe3`` reserved, although nonzero,
       potentially device-specific contents were observed at some of those
       offsets and deliberately not captured.  Physical silicon reports the
       otherwise undocumented read-only version value ``0x00000202`` at
       ``0xfc``.

       Hardware also confirmed that each ``DR`` read increments ``AR.DADDR``
       when ``AR.INCRT`` is clear, and that the observed ``WPSR.SWE`` flag
       clears on read.  A 32-bit ``AR`` write made from a Linux userspace
       ``/dev/mem`` mapping raised an external abort twice, while an unkeyed
       ``CR.REFRESH`` write did not.  SAM9X75 uses an Arm926EJ-S processor and
       has no TrustZone secure/non-secure worlds, and DS60001813E specifies
       ``AR`` as read/write without an access restriction.  Arm926EJ-S does
       expose the core's user/privileged mode on the AHB ``HPROT`` attributes,
       so an undocumented privilege check remains possible.  QEMU therefore
       remains permissive until the same access is compared from privileged
       bare-metal code with a tested data-abort handler and the aborting bus
       transaction is isolated.

       The maximum-address pre-programming probe exposes raw bits at the
       prospective packet tail.  Corrupt-header reads also expose the
       documented extra next-header word; a SIZE=255 packet cannot expose a
       257th temporary word through the hardware's 8-bit ``DADDR`` field.
       The checksum algorithm is not published and has not yet been derived
       from hardware, so checksum validation and faithful ``CKSGEN`` behavior
       are not implemented.  Locked packets consequently report a checksum
       error and are not interpreted as special packets.  Private-key-bus
       delivery, live-repair behavior, command timing and OTPC
       peripheral-clock gating also remain incomplete.  In particular, the
       data sheet asks software to enable the peripheral clock while its
       identifier table gives instance 46 no PMC clock-control bit.  Commands
       whose effect is not modeled remain non-destructive and report an error
       where the documented interface supplies one.  After normal key
       validation, writes containing more than one command bit are ignored
       rather than composing potentially irreversible operations whose
       hardware priority is unknown.  The BSC and OTPC remain independent
       hardware blocks: RomBOOT software reads BSC state and then configures
       OTPC.  PUF is missing.
   * - Display and camera
     - Missing
     - XLCDC, GFX2D, LVDS, DSI/CSI, MIPI PHY, CSI2DC and ISC backends.
   * - Board controls and expansion
     - Initial
     - The populated LEDs and buttons are covered above.  The J9 NAND and J10
       QSPI chip-select jumpers default closed and have machine options with
       functional disconnected states.  The default J38/J39 setting connects
       PAC1934 to FLEXCOM7; its SoC, USB-bridge and disconnected jumper routes
       have a machine option and functional SoC-side NACK behavior.  J24
       defaults to the M.2 SDIO route; its SPI position disconnects SDMMC1 and
       routes the card to FLEXCOM4 NPCS1.  J12 controls the LAN8840
       daughterboard's required 25 MHz clock, with functional MDIO and traffic
       loss when open.  The remaining power, clock and interface-selection
       jumpers, mikroBUS, Raspberry Pi header, non-storage M.2 peripherals and
       official Microchip overlay attachments remain.

Execution roadmap
-----------------

Work proceeds in the following order.  A later phase does not turn an earlier
phase green merely by avoiding it in the device tree.

#. **Keep a reproducible baseline.**  Maintain this matrix against the pinned
   data sheet, errata, board guide, firmware and Linux revisions.  Every model
   must have reset/mask/access, clock/reset-domain, interrupt, migration and
   negative-path coverage where those concepts apply.  The full board qtest
   suite and a boot with ``-d unimp,guest_errors`` are mandatory regression
   gates after every slice.
#. **Finish the populated base board.**  The MCP16502 regulators, PAC1934
   power monitor, RGB LED and four push buttons are modeled with their board
   wiring.  The exact upstream board DT now exercises regulator and IIO sysfs
   state across ULP0 suspend/resume.  The two boot-memory chip-select jumpers
   are modeled, and the board has been verified not to contain an EEPROM.  The
   PMIC nRSTO/NRST handoff
   now distinguishes VDDCORE resets from the retained VDDBU domain.  Next
   complete the remaining meaningful jumper/mux selections.
#. **Complete reusable data paths.**  The SPI personality and SSI host path now
   exist on the documented FLEXCOM0--5 instances, and remain absent from
   FLEXCOM6--12.  Finish SPI client, CRC and two-pin modes; finish synchronous
   and protocol-specific USART behavior and enforce the documented
   per-instance USART feature matrix once unsupported-register readback has
   been measured; complete TWI
   client/SMBus/PEC and high-speed/arbitration behavior; complete XDMAC
   GWAC/CNDC.QOS behavior;
   and wire every remaining documented XDMAC request.  Complete SSC, the
   remaining TCB modes and external timer pins, PWM and ADC so expansion-board
   drivers can use normal QEMU chardev, SSI, I2C and analog/digital endpoint
   abstractions.
#. **Close storage and memory-controller fidelity.**  Complete SDHCI command,
   error, media-change and migration behavior; complete NAND OOB, bad-block and
   DMA behavior, correlate the modeled ready/busy delays with silicon, add
   timing-mode-dependent latency and model the native SMC ``NWAIT`` path;
   finish the PMECC ``SPAREEN`` and ``USER`` paths, timing and clock-gating
   behavior; finish SMC
   transaction timing, matrix and MPDDRC-visible behavior, including per-host
   remap and arbitration effects; and cover persistent QSPI protocol widths
   and errata.
#. **Boot like the board.**  Implement the mask-ROM media-selection state
   machine, straps, authentication/error fallbacks and the documented QSPI
   erratum on top of the modeled reset and SRAM remap path.  Prove cold boot
   from SD, QSPI NOR and raw NAND without using ``-kernel`` as a ROM
   substitute.
#. **Complete major external interfaces.**  Prove the initial OHCI/EHCI model
   with unmodified Linux host storage and input, then compare reset, port-power,
   hotplug, DMA-error and interrupt behavior with the board.  Complete UDPHS
   isochronous and suspend/resume timing, add a host-facing cable backend, and
   prove gadget/SAM-BA operation.  Extend M_CAN with error confinement,
   retry, timestamp synchronization and debug-message behavior; keep the Linux
   CAN-FD/QEMU-backend regression passing and compare it against the physical
   controllers.
#. **Complete high-bandwidth and security blocks.**  Add XLCDC, GFX2D, ISC,
   CSI2DC, MIPI CSI/DSI PHY and LVDS endpoints, complete the initial OTPC
   model, and add PUF.  Close documented crypto, TRNG, audio and GEM/PTP/TSN
   corner cases rather than treating successful driver probes as completion.
#. **Differentially validate on hardware.**  Run the same bare-metal probes,
   firmware, DTB and Linux tests on the Curiosity LAN Kit and QEMU.  Compare
   reset values, reserved-bit behavior, interrupt timing, DMA ordering,
   clocks, error paths and board I/O.  Record unavoidable nondeterminism,
   resolve every actionable difference, rerun migration and integration
   tests, and split the result into reviewable upstream QEMU series.  Exclude
   OTPC emulation, mutation and command-characterization tests from physical
   hardware even when their QEMU-only equivalents are safe against disposable
   VM-local state.

Current invocation
------------------

The initial machine can be inspected with qtest or started without firmware::

  qemu-system-arm -M sam9x75-curiosity -nographic

This uses the true ``SFR_CCFG`` reset value: external DDR and NAND windows are
not assigned until guest firmware configures them.  For a raw or uImage Linux
``-kernel`` boot, QEMU acts as the skipped firmware only for DDR assignment and
sets ``EBI_CS1A`` on reset.  It does not implicitly assign NAND or enable the
DDR multi-port performance route.

``serial0`` is the dedicated DBGU console.  ``serial1`` through ``serial13``
are FLEXCOM0 through FLEXCOM12 respectively, so FLEXCOM0 can instead be used
as the interactive character backend with::

  qemu-system-arm -M sam9x75-curiosity -serial null -serial stdio

The M_CAN controllers are disconnected from an external CAN network unless a
CAN bus is supplied.  To place both controllers on one emulated network, use::

  qemu-system-arm \
    -object can-bus,id=canbus \
    -M sam9x75-curiosity,canbus0=canbus,canbus1=canbus \
    -nographic

On Linux hosts that network can also be connected to a SocketCAN interface
such as ``vcan0``::

  qemu-system-arm \
    -object can-bus,id=canbus \
    -object can-host-socketcan,id=canhost,if=vcan0,canbus=canbus \
    -M sam9x75-curiosity,canbus0=canbus,canbus1=canbus \
    -nographic

USB keyboard, storage and other standard QEMU USB devices can attach to the
UHPHS host ports.  UDPHS is present, but a cable is not created automatically
and QEMU does not yet expose it directly to a physical host.  For controller
development, its raw-token bridge can be linked to an emulated USB host bus;
this loopback example attaches it to UHPHS Port B::

  qemu-system-arm \
    -M sam9x75-curiosity \
    -device at91-udphs-gadget,udphs=/machine/soc/udphs,bus=usb-bus.0,port=2 \
    -nographic

Guest firmware must still enable PID 23, UPLL and UDPHS before tokens are
accepted.  The loopback is a test topology, not a physical-board topology.
On silicon ``UDPHS_CTRL.EN_UDPHS`` takes the shared UTMI transceiver away from
UHPHS high-speed Port A; the model disconnects and blocks that port while
device mode is selected.  Port B is used above so the emulated host can act as
a protocol test peer.

The physical OTPC array is blank and private to the VM unless a backing image
is selected.  For example, create an exactly 10 KiB blank image and attach it
as an immutable seed with::

  truncate -s 10240 sam9x75-otp.bin
  qemu-system-arm \
    -M sam9x75-curiosity,otpc-drive=otp0 \
    -drive if=none,id=otp0,file=sam9x75-otp.bin,format=raw,readonly=on \
    -nographic

Nonzero images use little-endian 32-bit words.  Omitting ``readonly=on`` does
not by itself authorize guest writes: the OTPC still treats the backend as an
immutable seed.  To test persistent physical-array programming against a
disposable, writable image, first preserve a known-good copy and then use::

  qemu-system-arm \
    -M sam9x75-curiosity,otpc-drive=otp0,otpc-write-enable=on \
    -drive if=none,id=otp0,file=sam9x75-otp-working.bin,format=raw \
    -nographic

The writable form deliberately disables migration and ``savevm``.  Guest
programming is flushed to the image and can only change bits from zero to one;
invalidation also modifies packet headers persistently.  SRAM1 emulation
remains separate and never writes this file.  Do not enable backend writes for
an irreplaceable image or for a dump that is your only record of a physical
device.  OTP images, VM snapshots and migration streams may contain key-packet
material; protect stored artifacts and use an encrypted migration channel.

The following pinned AT91Bootstrap ELF and SD image are the integration target
for the current media boot path.  This flow was repeated at QEMU commit
``d0c6dff95350`` after the TC1, UDPHS and M_CAN refinement and protocol-status
waves::

  qemu-system-arm -M sam9x75-curiosity \
    -kernel sam9x7-sdcardboot-uboot-4.0.13.elf \
    -drive file=sam9x75-sdcard.img,if=sd,format=raw,snapshot=on \
    -nic user,mac=02:00:00:09:75:01 -nographic

That run loaded unmodified U-Boot, initialized DDR, NAND, MMC and QSPI,
discovered GEM0, loaded the DTB and kernel from the FAT partition, and reached
the Linux embedded-initramfs shell.  The pinned 128 MiB image has no partition
2, despite the kernel command line naming ``/dev/mmcblk0p2``.  To close that
separate gate without changing the pinned image, a sparse 512 MiB derivative
preserved partition 1 byte-for-byte and added a 255 MiB ext4 partition 2.  It
proved a read/write mount, ``switch_root`` with ``/dev/mmcblk0p2`` as ``/``, an
interactive disk-root shell and clean power-down.  The derivative image has
SHA-256
``d912f64deb059ce8a2fb0cd666681bd97aa175a5310dafd0f939bba905caf8a4``.

The diagnostic log remained clean of SAM9X75 MMIO warnings; it contained one
generic CMD1, two CMD52 and four CMD5 failed media probes while firmware and
Linux distinguished the memory-only SD card from MMC/SDIO.  This exact-head
run did not repeat DHCP or packet exchange, and the base Linux DT did not probe
GEM.  A future complete integration regression must add current GEM traffic
without losing the firmware and disk-root results.  ``-kernel`` remains a
development entry path and is not a substitute for ROM media selection.

The populated raw NAND path can be exercised independently with the pinned
NAND AT91Bootstrap.  Authentic pre-populated PMECC and correction validation
requires a raw-page backend; the model also accepts a 512 MiB data-only image
and supplies sparse OOB storage for ordinary NAND use.  In the raw layout,
each page contains 4096 data bytes followed by 256 OOB bytes.  For the board's
eight-sector 512-byte/BCH8 profile, place the thirteen
ECC bytes for each sector consecutively in OOB bytes 152--255.  This profile
uses GF(2^13), primitive polynomial ``0x201b`` and Atmel's swapped-bit byte
ordering.  Keep OOB bytes 0--151 at ``0xff``.  A completely erased data page
must retain an all-``0xff`` OOB area.  Thus a 512 MiB logical NAND image has
131072 raw pages and an exact backend size of 570425344 bytes.  After
preparing that image, start it with::

  qemu-system-arm -M sam9x75-curiosity \
    -kernel sam9x7-nandflashboot-uboot-4.0.13.elf \
    -drive file=sam9x75-nand-raw.bin,if=mtd,index=0,format=raw \
    -nic none -nographic

The 2026-08-25 repeat proved that the unmodified bootstrap assigns CS2 and the
dedicated eight-bit NAND path, performs the documented ONFI status-poll and
read-resume sequence, changes timing mode through Set/Get Features, initializes
PMECC and copies 1 MiB from NAND before entering U-Boot.  Clean, two-bit-error
and eight-bit-error media all produced the same U-Boot transcript, closing the
BCH8 correction gate.  Like the SD command above, this deliberately bypasses
the proprietary mask ROM.

AT91Bootstrap polls the NAND status register and does not configure PD14, so
the exact asynchronous-completion boot repeat exercises modeled busy intervals
but not the board R/B# GPIO path.  The 2026-08-26 asynchronous-completion
repeat produced byte-identical 1346-byte serial transcripts for clean,
two-bit-error and eight-bit-error media, with empty ``unimp,guest_errors``
logs in all three cases.

Do not mechanically replay the exhaustive feature-register qtest on a physical
board.  Keep ``80h``, ``90h`` and ``B0h`` checks read-only: those registers
include OTP-protection or one-time default-programming controls.  Initial
differential tests should use read-only defaults plus reversible timing and
recovery-read changes unless the NAND device is explicitly sacrificial.

The U5 R/B# transition has not yet been measured on this board revision.  Keep
PD14 configured as an input and never request or reconfigure it while Linux MTD
owns the line.  First confirm that R32 is 10 kohm to 3.3 V.  A safe comparison
uses the NAND-net pad of R32 or another verified exposed point; the U5 C8 BGA
ball is not probeable.  Capture power-on busy non-invasively.  During page reads
and ``FFh`` reset, correlate falling and rising R/B# edges with both status bits
5 and 6 and record ``tR`` and ``tRST``.  Measuring ``tWB`` additionally requires
a safe NAND ``WE#``/``NWE`` reference for the confirm-command strobe.  With
PIOD's PID44 clock enabled, verify PDSR, edge interrupt status and AIC source 44;
normal Linux NAND polling need not expose a NAND GPIO interrupt in
``/proc/interrupts``.  In controlled bare metal, disabling only PD14's internal
pull-up must leave the external resistor holding the input high.

Program and erase timing require independent boot media and an explicitly
approved disposable block.  Repeated ``FFh`` while reset-busy and non-status
commands while busy are model policies that also need comparison.  A safe
``78h`` probe can verify its three address cycles and shared ready bits; forcing
plane-specific failures needs sacrificial media.  Native SMC ``NWAIT`` testing
is a separate recoverable bare-metal experiment: use mode 0 as the control and
try modes 2/3 only with watchdog or JTAG recovery because an access may stall.

A dump of the proprietary 80 KiB SAM9X7 RomBOOT can instead enter through the
real reset vector::

  qemu-system-arm -M sam9x75-curiosity \
    -bios sam9x7-rom.bin \
    -drive file=sam9x75-sdcard.img,if=sd,format=raw \
    -nic user,mac=02:00:00:09:75:01 -nographic

The raw image must be exactly 80 KiB (81920 bytes).  The separate 96 KiB ECC
GF-table ROM at ``0x00100000`` is synthesized by QEMU and is neither supplied
nor replaced by ``-bios``.  QEMU does not include the proprietary boot image.
``-bios`` and ``-kernel`` are mutually exclusive.  The ROM loader makes
genuine RomBOOT execution possible, but the real image and its SD, QSPI and
NAND selection/fallback flows have not yet been validated.

The initial VDDBU/factory value of ``BSC_CR.BOOT`` defaults to zero.  This
QEMU-only option can set it to the OTP-emulation request value for RomBOOT
experiments, for example::

  -global at91-bsc.factory-boot-sequence=1

This models the retained BSC request that RomBOOT reads after reset.  It does
not directly switch the OTPC: there is no BSC-to-OTPC hardware signal.  Guest
software requests SRAM1 emulation with ``OTPC_MR.EMUL`` and then issues the
unkeyed ``OTPC_CR.REFRESH``.  The requested and active states are separate,
and ``OTPC_SR.EMUL`` reports the active state after refresh.

Do not use a physical board to characterize the OTPC command model.  In
particular, never enter OTP emulation mode, write SRAM1, or issue programming,
invalidation, hiding, checksum/key-generation or key-bus commands.  This also
rules out ``PGM``, ``INVLD``, ``HIDE`` and ``CKSGEN`` even when software
believes emulation is active.  The boundary between SRAM emulation and the
factory OTP array has not been independently established well enough to make
those experiments safe.

The only pending OTPC hardware follow-up is a privileged access-control probe
for ``AR``.  A privileged bare-metal payload with code, vectors, stack, log
buffer and a tested data-abort handler all outside SRAM1 may attempt one
aligned word write of a benign address value.  It may read ``AR`` back only if
the write completes without an abort or loss of OTPC decode.  On either
failure, it must stop all OTPC access and reboot; it must not issue a recovery
command.  The probe must not write ``CR`` or ``MR``, read ``DR`` or
device-unique registers, or use a kernel path that may turn an imprecise abort
into a panic.  The existing Linux userspace abort is already established and
should not be repeated merely for confirmation.  Leave ``BSC_CR.BOOT``
unchanged on physical hardware.

The pinned Linux ``i2c-at91`` driver probes the FLEXCOM6 FIFO and obtains its
XDMAC channels.  Byte-width DMA, including an unaligned 13-byte transfer,
works.  Word-aligned receive DMA currently exposes a guest-driver ordering
bug: ``at91_twi_configure_dma()`` runs before the driver reads
``atmel,fifo-size``, so XDMAC remains byte-wide while the TWI is later set to
``RXRDYM=FOUR_DATA``.  A 12-byte read therefore correctly stops with three
bytes remaining below the four-byte request threshold.  Reading the FIFO
size before configuring DMA makes the channel word-wide as required by the
data sheet.  Qtests cover the working 32-bit linked-descriptor path and the
mismatched mode's three-byte stall; the Linux ordering fix remains a separate
guest change to validate on the physical board.

The XDMAC transfer width encoded as ``CC.DWIDTH=3`` also requires a physical
board result before it can be modeled.  The SAM9X7 data sheet and device pack
define only byte, halfword and word widths, while the upstream Linux
``at_xdmac`` driver selects the fourth encoding for 8-byte-aligned memcpy,
memset and interleaved transfers.  Run a RAM-resident 64-byte memcpy with
8-byte-aligned source and destination, ``CC.DWIDTH=3`` and a microblock length
of eight; record ``CC``, ``CUBC``, ``CIS``, ``GS`` and the destination bytes.
Repeat with ``CC.DWIDTH=2`` and a microblock length of 16 as the control.  In
particular, determine whether the first case copies 64 bytes as dword transfers,
copies only 32 bytes by aliasing word transfers, raises ``RBEIS`` or has another
effect.  Do not add a guessed fourth width to QEMU until this differential
result is available.

XDMAC FIFO and maintenance behavior should be compared with an uncached SRAM
destination.  Use FLEXCOM0 USART local loopback to feed fewer bytes than a
16-data memory burst into an active peripheral-to-memory channel, apply read
suspend, then write ``GSWF``.  Record the destination bytes, ``CC`` (including
``RDIP`` and ``WRIP``), ``CUBC``, ``GS``, ``GRS``, ``GWS`` and ``CIS`` before
and after: pending data must drain, ``FIS`` must assert and the channel must
remain enabled.  Repeat with an empty suspended source-peripheral channel,
after ``GD``, and with enabled memory-to-peripheral and read/write-suspended
memory-to-memory channels.  Read ``CIS`` to clear it between cases.  These cases
distinguish an accepted empty flush from the documented ignored scopes and
capture disable/flush ordering.

Repeat the receive test with a terminal view-3 descriptor containing two
two-byte microblocks (``UBC.UBLEN=2`` and ``CBC.BLEN=1``).  Pulse the peripheral
request once per byte and hold writes suspended so each microblock can be
observed in the FIFO.  Flush the first microblock, then record ``CSA``, ``CDA``,
``CUBC``, ``CBC``, ``CNDA``, ``CNDC``, ``CC``, ``GS``, ``GWS`` and ``CIS``:
``CUBC`` should reload only after both bytes reach SRAM, ``CBC`` should fall to
zero, while ``BIS`` and ``LIS`` must remain clear.  Flush the second microblock
and confirm that ``FIS``, ``BIS`` and ``LIS`` are reported only after its final
destination write.  Finally, run a two-descriptor view-1 ring
with two-byte periods, pulse requests through both periods, then issue ``GD``
with one byte staged in the next period.  The staged byte should drain before
``DIS`` without a spurious ``BIS`` or descriptor fetch.  These are benign SRAM
and USART-loopback tests; keep caches disabled for the DMA buffers and read
``CIS`` only once at each checkpoint because it is clear-on-read.

For scheduler characterization, run equal long descriptor chains with ``QOS``
values 0--3, vary ``GWAC.PW0`` through ``PW3``, and record completion order and
bandwidth.  Do not expect QEMU's 16-operation channel quantum on hardware: it
is an emulator liveness bound, not modeled SAM9X7 arbitration.  Migration has
no physical-board counterpart; the qtest migrates an enabled FLEXCOM0 USART
receive descriptor plus an enabled, suspended transmit channel and proves both
transfers complete after resume on the destination.

By default a valid SHDWC shutdown command requests a normal QEMU guest
shutdown.  Backup-domain wake-up experiments can leave the process running
while still modeling SHDN, timing and wake status by adding::

  -global at91-shdwc.request-system-shutdown=off

Holding START through both MCP16502 SYS-TMG timeouts likewise requests a
normal QEMU guest shutdown after asserting PBINT/nINTO and nRSTO.  PMIC timing
or forced-OFF experiments can suppress only that process-level request with::

  -global mcp16502-ab.request-system-shutdown=off

The physical board switches are available through QEMU keyboard input.  The
QMP ``send-key`` command (or the equivalent ``input-send-event`` command) maps
the following keys:

.. list-table:: Board switch input
   :header-rows: 1

   * - Key
     - Board switch
     - Signal path
   * - ``0``
     - SW1 USER
     - PC9, active-low
   * - ``W``
     - SW2 WKUP
     - WKUP0, active-low
   * - ``R``
     - SW3 RESET
     - NRST into RSTC
   * - ``S``
     - SW4 START
     - MCP16502 nSTRT, then nSTRTO to WKUP0

The three LED objects are visible as ``/machine/rgb-led-red``,
``/machine/rgb-led-blue`` and ``/machine/rgb-led-green``.  Their read-only
``intensity-percent`` QOM property is either 0 or 100 for the GPIO-driven
board LED.

The J9 NAND and J10 QSPI chip-select jumpers are closed by default, like the
physical board.  Either memory remains populated but can be electrically
deselected by opening its jumper at machine creation::

  -M sam9x75-curiosity,nand-cs=off,qspi-cs=off

Software must still assign CS2 and select the dedicated eight-bit NAND data
path through ``SFR_CCFG`` before U5 is decoded.  An open J9 then makes decoded
NAND bus reads return the deselected value and ignores writes.  An open J10
holds the serial flash chip select inactive while leaving the QSPI controller
and its CCFG-independent memory window available.

J12 is closed by default and supplies the 25 MHz reference clock required by
the LAN8840 on the Ethernet daughterboard.  Open it with::

  -M sam9x75-curiosity,ethernet-25mhz=off

Without that clock the LAN8840 management interface is inaccessible, MDIO
reads return ``0xffff``, link remains down and external RGMII traffic is
blocked.  The GEM controller itself remains available.

J38 and J39 both default to pins 2--3, routing PAC1934 I2C to FLEXCOM7.  The
only other valid hardware settings route both jumpers to the external
MCP2221A USB bridge on pins 1--2 or leave both open.  Select them with::

  -M sam9x75-curiosity,pac1934-route=usb
  -M sam9x75-curiosity,pac1934-route=off

Both alternatives disconnect the monitor from the SoC and therefore make
FLEXCOM7 accesses to address ``0x10`` NACK.  The external MCP2221A host USB
interface is not yet exposed by QEMU.

J24 defaults to pins 1--2 and connects the M.2 host interface to SDMMC1.  A
second legacy SD drive represents an SD/MMC storage-card adapter in that
socket; the current card model does not implement SDIO I/O functions.  Move
J24 to pins 2--3 with::

  -M sam9x75-curiosity,m2-interface=spi

This electrically disconnects the M.2 card from SDMMC1.  Attaching the second
SD drive attaches an SPI-mode SD card to FLEXCOM4 NPCS1 instead.  The same
``if=sd,index=1`` drive syntax is used for both jumper positions; only the
electrical route changes.

FLEXCOM hardware validation
---------------------------

The FLEXCOM synthesis boundary and the readback behavior of unavailable
features must be measured separately.  Use a RAM-resident bare-metal probe
with a data-abort handler, keep normal board drivers quiescent, and save raw
register values rather than reducing an unexpected result to pass/fail.  The
probe should proceed as follows:

#. Enable each control instance through its documented PMC peripheral ID:
   FLEXCOM3 is PID 8, FLEXCOM4 is PID 13, FLEXCOM5 is PID 14, FLEXCOM6 is
   PID 9 and FLEXCOM12 is PID 33.  Record the chip identification, clock
   configuration and wrapper ``MR`` before each case, and reset the wrapper
   and personality between cases.
#. Use FLEXCOM3, FLEXCOM4 and FLEXCOM5 as positive SPI controls without
   assigning their pins to the peripheral.  Select SPI in the wrapper, use
   local loopback, and record the SPI ``MR``, ``SR``, receive data, FIFO state,
   shared AIC result and both XDMAC request directions.  The corresponding
   XDMAC request pairs are 6/7, 8/9 and 10/11.  This establishes that the same
   probe and clocks can observe both a two-NPCS and the four-NPCS instances.
#. Repeat the register sequence at the FLEXCOM6 SPI offset, but leave PA24,
   PA25 and PB7--PB9 under PIO control.  Record whether selecting SPI in the
   wrapper reads back, whether each access at wrapper offset ``0x400`` aborts,
   reads as zero or another value, or retains a write, and whether local
   loopback can change data/FIFO status.  Also prove whether AIC source 9 or
   XDMAC request 12/13 can be asserted.  This is the decisive first-absent
   instance test: the documented synthesis has no FLEXCOM6 SPI controller,
   while QEMU's current unmapped slot is only a provisional reserved-access
   behavior.  A read-only repeat at FLEXCOM12 provides a useful far-boundary
   check.  Do not configure any FLEXCOM6 or FLEXCOM12 candidate pin as an SPI
   signal during these tests.
#. Select USART mode and compare the per-instance feature boundary without
   assigning external pins.  FLEXCOM3 is the positive control for Basic,
   hardware-handshake, RS485, ISO7816, LIN, IrDA, Manchester and LON modes.
   FLEXCOM4 must be checked for the same set except LON.  FLEXCOM6 must be
   checked for Basic, hardware-handshake and RS485 only.  For each unsupported
   case, record the ``US_MR`` mode-field readback, ``US_MAN``, ``US_LINMR``,
   ``US_LINIR`` and the ``US_LON*`` register window, plus status, interrupt and
   DMA effects.  Reset after each pattern.  A mode's absence does not by
   itself establish that its registers are read-as-zero/write-ignored; that
   distinction determines the eventual QEMU masks.
#. Only after the internal tests are repeatable, observe physical chip-select
   outputs with a high-impedance probe and no external stimulus.  FLEXCOM3
   exposes NPCS0 on IO3/PC25 and NPCS1 on IO4/PC24.  FLEXCOM4 exposes NPCS0 on
   IO3/PA12, NPCS1 on IO4/PA13 or PA30, NPCS2 on IO5/PA14 or PA31, and NPCS3 on
   IO6/PB3.  FLEXCOM5 exposes NPCS0 on IO3/PA14, NPCS1 on IO4/PA12 or PA30,
   NPCS2 on IO5/PA25 and NPCS3 on IO6/PA24.  Select one documented I/O set at
   a time and verify the schematic net before changing the PIO mux.  PA12 is
   shared with the PAC1934 alert, PA24/PA25 carry the board PMIC I2C link, and
   FLEXCOM4 NPCS1 can reach the M.2 socket through J24; isolate the other
   device or skip that output if the net cannot be guaranteed undriven, and
   remove any M.2 card before changing J24.  FLEXCOM6 candidate signals also
   overlap PA24/PA25 and Ethernet signals on PB7--PB9, so SPI6 must remain an
   internal register probe rather than a pin-level experiment.

The scope result establishes only which NPCS signals leave the package.
Separately capture FLEXCOM0--3 ``CSR2``/``CSR3`` and direct/decoded ``PCS``
readback while the pins remain unassigned.  Do not mask those fields in QEMU
solely because NPCS2/NPCS3 have no physical signal; their internal behavior
must follow the hardware result.

Completion gates
----------------

Polling DBGU from SRAM, interrupt-driven bare metal and the populated
LED/button paths are achieved.  The exact-EBI SD AT91Bootstrap path, Linux
embedded-initramfs shell and a derivative-image ext4 disk-root shell are now
repeated.  Exact-EBI NAND AT91Bootstrap is also repeated with clean, two-bit
and BCH8-limit eight-bit-error media; all three reached a byte-identical U-Boot
prompt with an empty diagnostic log.  GEM/LAN8840 packet exchange remains
pending against the exact-EBI model; GEM discovery alone was repeated.  The
other remaining integration gates include full guest ``mmc_spi`` operation
through J24, the remaining board jumper and mux behavior, genuine QSPI and
NAND RomBOOT, USB, CAN, expansion buses, multimedia/security, whole-machine
migration and finally hardware differential validation.  Normal supported
boots must be clean with ``-d unimp,guest_errors``.
