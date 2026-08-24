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
* SAM9X7 Series silicon errata DS80001082G.
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
     - VFP disabled; MIDR, CTR, CP15 reset, cache/MMU, FCSE, Jazelle and
       unaligned-access behavior remain under audit.
   * - ROM and boot window
     - Initial
     - 176 KiB ROM and reset alias mapped; RomBOOT image/behavior, straps,
       remap and revision errata remain missing.
   * - SRAM and DDR3L
     - Initial
     - 64 KiB SRAM0, 4 KiB SRAM1 and fixed 256 MiB DDR mapped.  MPDDRC
       configuration, refresh, error reporting, interrupts and write
       protection have an initial register model; DDR timing/training and OTP
       emulation control remain missing.
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
       and write protection; protection-mode corner cases need hardware tests.
   * - PMC, SCKC and clocks
     - Initial
     - Main and slow oscillators, five SAM9X7 PLL clock entries, master,
       programmable, generated and peripheral clocks, gating, interrupts and
       write protection are modeled.  SCKC selects the modeled RC/crystal slow
       clocks; oscillator start-up/failure-monitor timing and the remaining
       revision-erratum cases are missing.
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
       migration state.  RSTC has keyed user/external reset control, status and
       interrupt behavior.  SHDWC has keyed two-slow-clock shutdown, SHDN,
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
     - All thirteen wrappers expose mode ownership and uniquely named I2C buses.
       The TWI host path covers polled byte transfers, internal addresses,
       repeated starts, NACK and AIC signaling, alternative-command mode,
       FIFO-width accesses, masks, write protection, reset and migration.
       USART/SPI children, true asynchronous FIFOs, client mode, SMBus/PEC and
       timing/arbitration fidelity remain missing.
   * - XDMAC
     - Initial
     - All 16 channels expose global/channel control, clock gating, memory copy
       and memset, byte/halfword/word widths, address modes and 2D strides,
       software and external-request pacing, suspend/resume/flush/disable,
       linked-list descriptor views, completion/error interrupts and migration
       state.  AES, SHA and TDES request lines are wired; the remaining
       peripheral request lines, security policy, microblock/burst timing and
       coherency effects remain missing.
   * - SDMMC0 and SDMMC1
     - Initial
     - Both SAM9X7 hosts, removable-card attachment and PA23 card detect are
       wired.  Host preset registers and the Linux ADMA descriptor form are
       covered.  The unmodified AT91Bootstrap SD/ADMA path loads U-Boot, and
       Linux mounts the SD root filesystem and reaches a shell.  Register,
       command, DMA error and media-change completeness remains under audit.
   * - OSPI/QSPI NOR
     - Initial
     - Controller and XIP windows, SST26VF064BEUI identity, SFDP/EUI data,
       program/erase and U-Boot probing are modeled.  The EUI-48 follows the
       configured GEM MAC address.  Persistence with a drive, all protocol
       widths and the ROM quad-mode erratum need further coverage.
   * - EBI/SMC and raw NAND
     - Initial
     - The MX30LF4G28AD identity, ONFI parameter data, page program/read and
       erase paths, SMC registers, and initial PMECC/PMERRLOC control/status are
       present.  Real ECC generation/correction, OOB/bad-block behavior and DMA
       remain missing.
   * - GEM and LAN8840
     - Initial
     - GEM0 has six priority queues, DMA transmit/receive, AIC sources 24 and
       60--64, a Clause 22 PHY at address 1 with the LAN8840 identifier, and a
       station address provisioned through the SST26VF064BEUI.  Unmodified
       U-Boot obtains a DHCP lease and exchanges packets.  LAN8840 MMD/RGMII
       delay registers, PHY reset-value fidelity, checksum corner cases,
       filtering, PTP/TSN and hardware comparison remain.
   * - USB host and device
     - Missing
     - OHCI, EHCI, UDPHS, port power, hotplug, gadget mode and SAM-BA path.
   * - I2C board devices
     - Initial
     - The exact MCP16502TAB-E/S8B PMIC is present on FLEXCOM6 with OTP
       defaults, writable masks, Active/Low-Power/Hibernate/HPM regulator
       selection, live status and its SHDN/PWRHLD and nSTRT/nSTRTO board
       signals.  FLEXCOM7 carries the PAC1934 with its sparse variable-width
       register loops, delayed
       REFRESH/REFRESH_V activation, channel skipping, four voltage inputs,
       virtual-time power accumulation, overflow signaling, migration state
       and the shared PB18 SLOW/ALERT connection.  The current-sense inputs
       default to zero and can be driven through QOM properties for workload
       or hardware-trace replay.  Conversion-complete alert pulses, SMBus
       timeout/electrical details, hardware-calibrated telemetry and the
       board/extension EEPROMs remain.
   * - LEDs and push buttons
     - Initial
     - PC14, PC20 and PC21 drive observable red, blue and green LED devices.
       Keyboard ``0``, ``W``, ``R`` and ``S`` operate the active-low USER,
       WKUP, RESET and START switches.  USER drives PC9; RESET enters RSTC
       through NRST; WKUP and the PMIC nSTRTO output share WKUP0; and START
       enters the PMIC through nSTRT.  LED intensity and every switch path
       have board qtests.  PMIC rail sequencing and push-button long-press
       timing remain.
   * - Timers, ADC, PWM and SSC
     - Initial
     - TC0 has three 32-bit channels, GCLK/MCK-divided/slow-clock selection,
       PMC gating, free-running and RC-reset waveform timing, synchronization,
       periodic and one-shot interrupts, write protection and migration state.
       This covers the unmodified Linux clocksource and clockevent paths.
       External TCLK/TIO capture and waveform routing, up/down modes, QDEC,
       TC1, ADC inputs, PWM outputs and synchronous serial operation remain.
   * - Audio
     - Initial
     - I2SMCC register state, clocking, mono/compact/TDM framing, interrupts,
       loopback and bidirectional XDMAC requests are modeled.  Class-D has
       FIFO, clocked sample consumption, interrupt, write-protection, XDMAC and
       sample-sink paths.  The unmodified Linux drivers probe both controllers.
       QEMU audio-backend output, exact serial framing, underrun behavior and
       the analog output path remain.
   * - CAN FD
     - Missing
     - Two Bosch M_CAN instances, shared message RAM and CAN bus backends.
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
       fault-injection behavior require hardware confirmation.  OTP and PUF
       are missing.
   * - Display and camera
     - Missing
     - XLCDC, GFX2D, LVDS, DSI/CSI, MIPI PHY, CSI2DC and ISC backends.
   * - Board controls and expansion
     - Missing
     - LEDs, buttons, jumpers, mikroBUS, Raspberry Pi header, M.2 and official
       Microchip overlay attachments.

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
   wiring.  Next complete PMIC rail sequencing/long-press behavior, default
   strap selection and board/extension EEPROMs.  Exercise the exact upstream
   board DT without QEMU-only changes, including regulator state, IIO
   telemetry and suspend/resume.
#. **Complete reusable data paths.**  Add the USART and SPI personalities to
   all applicable FLEXCOM instances, complete TWI client/SMBus/PEC/FIFO
   behavior, and wire every documented XDMAC request.  Complete SSC, TC1,
   external timer pins, PWM and ADC so expansion-board drivers can use normal
   QEMU chardev, SSI, I2C and analog/digital endpoint abstractions.
#. **Close storage and memory-controller fidelity.**  Complete SDHCI command,
   error, media-change and migration behavior; implement NAND OOB, bad-block,
   PMECC generation/correction and DMA; finish SMC, matrix and MPDDRC-visible
   behavior; and cover persistent QSPI protocol widths and errata.
#. **Boot like the board.**  Implement the mask-ROM media-selection state
   machine, straps, reset alias/remap, authentication/error fallbacks and the
   documented QSPI erratum.  Prove cold boot from SD, QSPI NOR and raw NAND
   without using ``-kernel`` as a ROM substitute.
#. **Add major external interfaces.**  Implement OHCI/EHCI/UDPHS with board
   power and hotplug wiring, then both M_CAN instances and their shared SRAM.
   Prove host storage/input, gadget/SAM-BA and CAN-FD traffic with existing
   QEMU USB and CAN backends.
#. **Complete high-bandwidth and security blocks.**  Add XLCDC, GFX2D, ISC,
   CSI2DC, MIPI CSI/DSI PHY and LVDS endpoints, followed by OTPC and PUF.
   Close documented crypto, TRNG, audio and GEM/PTP/TSN corner cases rather
   than treating successful driver probes as completion.
#. **Differentially validate on hardware.**  Run the same bare-metal probes,
   firmware, DTB and Linux tests on the Curiosity LAN Kit and QEMU.  Compare
   reset values, reserved-bit behavior, interrupt timing, DMA ordering,
   clocks, error paths and board I/O.  Record unavoidable nondeterminism,
   resolve every actionable difference, rerun migration and integration
   tests, and split the result into reviewable upstream QEMU series.

Current invocation
------------------

The initial machine can be inspected with qtest or started without firmware::

  qemu-system-arm -M sam9x75-curiosity -nographic

An unmodified AT91Bootstrap ELF and SD image can exercise the current media
boot path directly::

  qemu-system-arm -M sam9x75-curiosity \
    -kernel sam9x7-sdcardboot-uboot-4.0.13.elf \
    -drive file=sam9x75-sdcard.img,if=sd,format=raw \
    -nic user,mac=02:00:00:09:75:01 -nographic

This loads unmodified U-Boot, initializes DDR, NAND, MMC and QSPI, discovers
the LAN8840, and supports DHCP and packet exchange through GEM0.  It then
loads Linux from SD, uses ADMA for the card, mounts the root filesystem and
reaches the image's interactive shell.  RTC, RTT, reset, shutdown, watchdog,
AES, SHA, TDES, TRNG, I2SMCC and Class-D drivers all probe their modeled
hardware; the crypto and audio paths acquire their documented XDMAC requests.
The 57-test board qtest baseline and this boot are clean of SAM9X75 model
warnings with ``-d unimp,guest_errors``.  Generic SD diagnostics still report
the expected failed MMC/SDIO probes against a memory-only SD card.  FLEXCOM
USART children remain missing but are not the selected board console.
RomBOOT itself is also missing; ``-kernel`` is a development entry path and
not a substitute for ROM media selection.

By default a valid SHDWC shutdown command requests a normal QEMU guest
shutdown.  Backup-domain wake-up experiments can leave the process running
while still modeling SHDN, timing and wake status by adding::

  -global at91-shdwc.request-system-shutdown=off

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

Completion gates
----------------

Polling DBGU from SRAM, interrupt-driven bare metal, unmodified SD
AT91Bootstrap into U-Boot, a Linux shell from SD, and GEM/LAN8840 packet
exchange and the populated LED/button paths are achieved.  The remaining
integration gates are base-board straps/EEPROMs, genuine QSPI and NAND
RomBOOT, USB, CAN, expansion buses,
multimedia/security, whole-machine migration and finally hardware differential
validation.  Normal supported boots must be clean with
``-d unimp,guest_errors``.
