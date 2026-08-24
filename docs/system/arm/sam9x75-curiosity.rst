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
     - 64 KiB SRAM0, 4 KiB SRAM1 and fixed 256 MiB DDR mapped; OTP emulation
       control and MPDDRC behavior are missing.
   * - DBGU and chip identification
     - Initial
     - Polling TX/RX, masks, local loopback, CIDR and EXID implemented;
       identification metadata, break/error injection and timing remain.
   * - AIC and system IRQ aggregation
     - Initial
     - 128-source AIC5 register bank with SAM9X7 wiring, priority/nesting,
       IRQ/FIQ, edge/level modes, fast forcing, general mask, spurious vector
       and write protection; protection-mode corner cases need hardware tests.
   * - PMC, SCKC and clocks
     - Initial
     - Main and slow oscillators, five SAM9X7 PLL clock entries, master,
       programmable, generated and peripheral clocks, gating, interrupts and
       write protection are modeled.  SCKC, failure-monitor timing and the
       remaining revision-erratum cases are missing.
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
       migration state.  RSTC, SHDWC, RTT, RTC, GPBR and wake/reset causes are
       missing.  Both PIT64B instances already have clocked one-shot/continuous
       operation, interrupts, protection, sequence errors and migration
       coverage.
   * - FLEXCOM0--12
     - Missing
     - Mode ownership and USART, SPI and TWI register interfaces.
   * - XDMAC
     - Missing
     - 16 channels, descriptors, peripheral handshakes, errors and security.
   * - SDMMC0 and SDMMC1
     - Missing
     - SAM9X7 SDHCI quirks, removable-card signals, DMA and boot operation.
   * - OSPI/QSPI NOR
     - Missing
     - Controller, XIP window, SST26VF064BE, persistence and ROM quad-mode
       erratum.
   * - EBI/SMC and raw NAND
     - Missing
     - MX30LF4G28AD, ONFI, OOB/bad blocks, PMECC and PMERRLOC.
   * - GEM and LAN8840
     - Missing
     - Queues, MDIO/RGMII PHY, checksum, PTP/TSN and LAN Kit interrupts.
   * - USB host and device
     - Missing
     - OHCI, EHCI, UDPHS, port power, hotplug, gadget mode and SAM-BA path.
   * - I2C board devices
     - Missing
     - MCP16502, PAC1934 and board/extension EEPROMs.
   * - Timers, ADC, PWM and SSC
     - Missing
     - TC blocks, ADC inputs, PWM outputs and synchronous serial operation.
   * - Audio
     - Missing
     - I2SMCC and Class-D controller with QEMU audio backends.
   * - CAN FD
     - Missing
     - Two Bosch M_CAN instances, shared message RAM and CAN bus backends.
   * - Crypto, TRNG, OTP and PUF
     - Missing
     - Functional engines, deterministic testing, protected virtual fuse/key
       state and revision errata without using physical-board secrets.
   * - Display and camera
     - Missing
     - XLCDC, GFX2D, LVDS, DSI/CSI, MIPI PHY, CSI2DC and ISC backends.
   * - Board controls and expansion
     - Missing
     - LEDs, buttons, jumpers, mikroBUS, Raspberry Pi header, M.2 and official
       Microchip overlay attachments.

Current invocation
------------------

The initial machine can be inspected with qtest or started without firmware::

  qemu-system-arm -M sam9x75-curiosity -nographic

Until RomBOOT and media controllers exist, a bare-metal image should be loaded
into SRAM with the generic loader and its entry point assigned to CPU 0.  This
is a development path only, not a substitute for the real boot flow.

Completion gates
----------------

The integration gates, in order, are polling DBGU from SRAM, interrupt-driven
bare metal, unmodified SD AT91Bootstrap to U-Boot, Linux shell from SD, genuine
QSPI and NAND boot, base-board I/O, LAN/USB, multimedia/security, migration and
finally hardware differential validation.  Normal supported boots must be
clean with ``-d unimp,guest_errors``.
