.. SPDX-License-Identifier: GPL-2.0-or-later

SAM9X75 SD-over-SPI consumer gate
=================================

This is a pending Linux4Microchip 2026.04 end-to-end gate.  It has not yet
passed in the exact guest and must not be quoted as a completed validation.
The preferred first SPI consumer is QEMU's existing SD-over-SPI device.  It
speaks the standard MMC/SD SPI protocol, gives Linux a real block device and
drives both short PIO command transfers and 512-byte XDMAC data transfers.
It represents storage, not the WILCS02 Wi-Fi/SDIO functions.

Exact topology and device tree
------------------------------

``-M sam9x75-curiosity,m2-interface=spi`` models J24 on pins 2--3.  A legacy
``if=sd,index=1`` drive then has this topology::

  FLEXCOM4 SPI SSI bus -> ssi-sd adapter at native address 1 -> SPI SD card

The exact Linux4Microchip FIT contains image 20, ``fdt_wilcs02_spi``.  It is
the compiled
``sam9x75_curiosity/sam9x75_curiosity_wilcs02_spi.dtso`` from the
``linux4microchip-2026.04`` tag of ``linux4microchip/dt-overlay-mchp``.  The
unchanged overlay:

* selects SPI mode in ``&flx4`` and enables ``&spi4``;
* assigns an 80 MHz FLEXCOM4 generic clock and the PA9/PA10/PA11 SPI pins;
* declares ``cs-gpios = <&pioA 13 GPIO_ACTIVE_LOW>``; and
* creates ``wilc_spi@0`` with ``compatible = "mmc-spi-slot"`` and
  ``reg = <0>``.

The exact kernel has ``CONFIG_SPI_ATMEL=y``, ``CONFIG_AT_XDMAC=y``,
``CONFIG_MMC_SPI=m`` and ``CONFIG_CRC7=m``.  Its root filesystem contains
``mmc_spi.ko`` and its dependencies.

Both blockers are closed and the gate below has passed; see
`Achieved result`_ before repeating it.

Achieved result
---------------

At QEMU checkpoint ``fcf594706b`` the gate passed with the exact
Linux4Microchip 2026.04 kernel and the unchanged ``wilc_spi`` overlay taken
from the shipped FIT.  The driver logged ``Atmel SPI Controller version
0x410`` and selected FIFO plus XDMAC, ``mmc_spi`` registered its host, a
64 MiB SPI-mode card enumerated, an 8 MiB deterministic payload wrote and
read back with identical SHA-256, and a FAT32 filesystem was written,
checked with ``fsck.fat -vn``, re-mounted read-only and verified against its
own manifest.  QEMU's ``unimp,guest_errors`` log was empty and the host
filesystem check was clean.  Evidence:
``t/linux4microchip-mmc-spi-20260828/release-r4``.

Running it first exposed two model faults, both fixed at that checkpoint: a
spurious XDMAC request overflow caused by re-driving an already-asserted
FLEXCOM request line, and a ``WDRBT`` stall in the FIFO PIO path.  Runs r1
to r3 are diagnostic only.


The first blocker is closed.  IO4/NPCS1 and PA13 are the same pad, so the
board now models that pad with ``at91-pad-mux``: the PIO output wins whenever
the PIO drives the pad, and the pad otherwise follows the FLEXCOM NPCS1
function.  Both routes reach the same ``ssi-sd`` adapter.  The
``sam9x75/sdcard/spi-gpio-chip-select`` qtest covers selection, deselection,
reselection and the handover back to the peripheral function, and
``sam9x75/sdcard/spi-gpio-chip-select-migration`` covers migrating a machine
whose card is selected through the GPIO route.  That migration test also
exposed a generic QEMU bug: ``sd_vmstate_pre_load()`` unconditionally called
the assert-guarded ``sd_ocr_powerup()``, so any machine holding an SPI-mode
SD card aborted on incoming migration.

The second blocker was closed by a silicon measurement on 2026-08-28.  With
the shipped ``wilc_spi`` FIT configuration selected, FLEXCOM4 SPI bound and
``runtime_status`` reporting ``active``, ``devmem 0xf00004fc 32`` returned
``0x00000410`` on three reads with no abort, a 16-bit read ``0x0410`` and an
8-bit read ``0x10``; the driver logged ``Atmel SPI Controller version 0x410``
and selected FIFO plus XDMAC.  QEMU now models that read-only value with
byte lanes, so the driver takes the modern path instead of the unmodelled
PDC window.  The original blocker text is kept for the record:

#. The SAM9X7 data sheet marks the FLEXCOM SPI ``+0xfc`` location reserved,
   so QEMU currently returns zero.  The exact ``spi-atmel`` driver reads this
   location as ``SPI_VERSION`` to choose SPI2/WDRBT, XDMAC or legacy PDC
   behavior.  A zero value selects the unmodeled legacy PDC window at
   ``+0x100`` and prevents a trustworthy Linux gate.  Do not invent a version
   value: measure the read-only silicon value first.

Read-only hardware probe
------------------------

Boot the physical board with the unchanged ``wilc_spi`` overlay so FLEXCOM4
SPI is bound and its clocks can be enabled.  Pin that platform device in the
runtime-active state, make no userspace MMIO write, and capture::

  uname -a
  dmesg | grep -Ei 'Atmel SPI Controller version|f0000400.*spi|spi-atmel'
  SPI_PLATFORM=$(find /sys/bus/platform/drivers/atmel_spi -maxdepth 1 \
      -type l -name '*f0000400*' -print -quit)
  test -n "$SPI_PLATFORM"
  printf 'on\n' > "$SPI_PLATFORM/power/control"
  cat "$SPI_PLATFORM/power/runtime_status"
  devmem 0xf00004fc 32

Require ``runtime_status`` to report ``active`` before the read.  Record an
external abort as a result rather than attempting any write.  FLEXCOM3 and
FLEXCOM5 are not positive controls in this topology because the unchanged
overlay does not enable or clock them.  If silicon exposes a constant, QEMU
can model that exact read-only value and test its reset, access-size and
write-ignore behavior.  If silicon reads zero or aborts, capability selection
belongs in the Linux ``microchip,sam9x7-spi`` match data instead; QEMU must not
grow a fictional PDC or version register merely to accommodate the driver.

Disposable Linux4Microchip gate
-------------------------------

After both blockers are resolved, prepare a merged DTB and a new disposable
64 MiB card.  These commands assume the project layout used by this checkout
and deliberately keep temporary files on disk rather than in ``/tmp``::

  set -eu
  QEMU_TREE=/home/karl/linux-work/qemu-SAM9X75/qemu
  SAM_TOP=/home/karl/linux-work/qemu-SAM9X75
  SPI_WORK="$SAM_TOP/t/spi-sd-20260826"
  SPI_CARD="$SPI_WORK/spi-card.raw"
  mkdir -p "$SPI_WORK"
  dumpimage -T flat_dt -p 20 -o "$SPI_WORK/wilc_spi.dtbo" \
      "$SAM_TOP/t/linux4microchip-2026.04-sd.itb"
  fdtoverlay \
      -i "$SAM_TOP/t/linux4microchip-2026.04-base.dtb" \
      -o "$SPI_WORK/sam9x75-wilc-spi.dtb" \
      "$SPI_WORK/wilc_spi.dtbo"
  test ! -e "$SPI_CARD"
  qemu-img create -f raw "$SPI_CARD" 64M

The root filesystem is attached as SD drive zero with ``snapshot=on``.  Only
the newly created 64 MiB drive at index one is persistent and writable::

  TMPDIR="$SPI_WORK" "$QEMU_TREE/build/qemu-system-arm" \
      -M sam9x75-curiosity,m2-interface=spi \
      -kernel "$SAM_TOP/t/linux4microchip-2026.04-zImage" \
      -dtb "$SPI_WORK/sam9x75-wilc-spi.dtb" \
      -device loader,file="$SAM_TOP/artifacts/mcan-linux-guest-20260825/linux-direct-boot-shim.elf",cpu-num=0 \
      -append 'console=ttyS0,115200 root=/dev/mmcblk0 rw rootwait rootfstype=ext4 nowatchdog panic=-1 loglevel=8 lpj=1000000 random.trust_bootloader=on' \
      -watchdog-action none \
      -drive file="$SAM_TOP/t/linux4microchip-2026.04-rootfs.ext4",if=sd,index=0,format=raw,snapshot=on \
      -drive file="$SPI_CARD",if=sd,index=1,format=raw,auto-read-only=off \
      -display none -monitor none -serial stdio -nic none \
      -d unimp,guest_errors -D "$SPI_WORK/qemu-unimp-guest-errors.log"

At the guest shell, select the card through its device-tree ancestry rather
than assuming that it is always ``mmcblk1``.  The exact 64 MiB size check and
the mount check are safety barriers before any write::

  export PATH=/usr/sbin:/usr/bin:/sbin:/bin
  set -eu
  modprobe mmc_spi

  SPI_HOST=
  for host in /sys/class/mmc_host/mmc*; do
      node=$(readlink -f "$host/device/of_node" 2>/dev/null || true)
      case "$node" in
          */flexcom@f0000000/spi@400/wilc_spi@0) SPI_HOST=${host##*/} ;;
      esac
  done
  test -n "$SPI_HOST"

  SPI_DEV=
  for block in /sys/class/block/mmcblk*; do
      test ! -e "$block/partition" || continue
      path=$(readlink -f "$block/device")
      case "$path" in
          *"/$SPI_HOST/"*) SPI_DEV=/dev/${block##*/} ;;
      esac
  done
  test -b "$SPI_DEV"
  test "$(blockdev --getsize64 "$SPI_DEV")" = 67108864
  ! awk -v dev="$SPI_DEV" '$1 == dev { found = 1 } END { exit found ? 0 : 1 }' \
      /proc/mounts

First prove read-only access, then write and compare a deterministic 8 MiB
payload at the 1 MiB offset of the disposable card::

  dd if="$SPI_DEV" of=/dev/null bs=1M count=1
  python3 -c 'open("/root/spi-payload.bin", "wb").write(bytes(range(256)) * 32768)'
  dd if=/root/spi-payload.bin of="$SPI_DEV" bs=512 seek=2048 conv=fsync
  dd if="$SPI_DEV" of=/root/spi-readback.bin bs=512 skip=2048 count=16384
  cmp /root/spi-payload.bin /root/spi-readback.bin
  sha256sum /root/spi-payload.bin /root/spi-readback.bin

For filesystem coverage, reformat only after repeating the identity, size and
unmounted-device barriers above.  FAT is used because the exact image
contains both ``mkfs.fat`` and ``fsck.fat``::

  mkfs.fat -F 32 -n QEMUSPI "$SPI_DEV"
  mkdir -p /mnt/spi
  mount -t vfat "$SPI_DEV" /mnt/spi
  cp /root/spi-payload.bin /mnt/spi/payload-a.bin &
  p1=$!
  cp /root/spi-payload.bin /mnt/spi/payload-b.bin &
  p2=$!
  wait "$p1" && wait "$p2"
  (cd /mnt/spi && sha256sum payload-*.bin > SHA256SUMS)
  sync
  umount /mnt/spi
  fsck.fat -vn "$SPI_DEV"
  mount -t vfat -o ro "$SPI_DEV" /mnt/spi
  (cd /mnt/spi && sha256sum -c SHA256SUMS)
  umount /mnt/spi

A passing transcript must show the exact kernel release, the SPI controller
version, FIFO and XDMAC selection, the ``mmc_spi`` host and its 64 MiB block
device, successful ``cmp``/hash/fsck results and no SPI timeout, DMA error,
MMC I/O error or QEMU ``unimp,guest_errors`` diagnostic.

Reset and migration follow-up
-----------------------------

Two of these are done at `5bcb06b161`:
``sam9x75/sdcard/spi-reset-and-partial-command-migration`` covers a machine
reset taken with a command half sent, after which a complete CMD0 is
accepted with no stale argument bytes, and a migration taken with four of
six command bytes transferred, which the destination completes.

The release gate still needs qtests that complete SPI-mode card
initialization and single/multiple-block reads and writes, then migrate
during a partial response, data read, data write, CRC and stop-command
phase.

That question is answered.  Tracing the working gate with
``-trace enable=sdcard_normal_command -trace enable=sdcard_app_command``
showed the real sequence: ``ACMD41`` reaches ``sd_ready_state`` and
**CMD10 (SEND_CID) is what advances to** ``sd_transfer_state``, because
``spi_cmd_SEND_CxD()`` switches state on the grounds that SPI returns the
CID and CSD on the data lines.  CMD1 is accepted but shares the ACMD41
handler, so it leaves the card idle, and CMD16 is refused outside transfer
state.  A direct qtest sequence that omits CMD9 or CMD10 therefore cannot
reach block I/O, which is what made this look like a model gap.  Evidence:
``t/linux4microchip-mmc-spi-20260828/trace-r1``.

One further detail costs time if unknown: every command helper must clock
one extra byte after the R1 answer, because the card returns to its command
state on that byte and otherwise consumes the next command's opcode.

``sam9x75/sdcard/spi-block-transfer-and-data-migration`` at `0469ae3b29` now covers
single-block read and write and a migration at a command boundary.

Candidate bug, not yet confirmed: migration taken *inside* a read data
phase did not resume correctly.  With 100 bytes of a 512-byte block already
clocked out on the source, the destination continued at byte 48 rather than
byte 100.  ``SDState`` migrates ``data_start``, ``data_offset`` and the
512-byte buffer, and ``ssi_sd_state`` migrates ``mode``, ``read_bytes`` and
``response_pos``, so the two sides appear to disagree after load rather
than either being absent.  Reproduce by asserting the continuation instead
of migrating at a command boundary, and compare ``data_offset`` against
``read_bytes`` on both ends before calling it a bug.

Reset and migration follow-up
-----------------------------

Two of these are done at `5bcb06b161`:
``sam9x75/sdcard/spi-reset-and-partial-command-migration`` covers a machine
reset taken with a command half sent, after which a complete CMD0 is
accepted with no stale argument bytes, and a migration taken with four of
six command bytes transferred, which the destination completes.

The release gate still needs qtests that complete SPI-mode card
initialization and single/multiple-block reads and writes, then migrate
during a partial response, data read, data write, CRC and stop-command
phase.

Before writing those, resolve this open question, because an attempt to
write them ran straight into it.  Driving the generic SD model directly
from a qtest, ``ACMD41`` (CMD55 then CMD41) leaves the card in
``sd_ready_state``, and no command in the SPI protocol table moves it from
there to ``sd_transfer_state``: CMD2, CMD3 and CMD7 are not in that table,
and ``sd_cmd_READ_SINGLE_BLOCK`` and ``sd_cmd_SET_BLOCKLEN`` both reject
any state but transfer, answering with the illegal-command bit.  CMD1 is
accepted but leaves the card idle, because it shares the ACMD41 handler
which requires the idle state and then sets ready.

That contradicts the passing Linux gate above, which moved 8 MiB through
CMD17 and CMD24, so one of two things is true and it is worth knowing
which: either the driver reaches transfer by a path the direct sequence
misses, or the block I/O succeeded for a reason unrelated to the modeled
state machine.  The cheap way to settle it is to re-run the gate with
``-trace 'sdcard_*'`` and read the actual command and state sequence rather
than infer it.  Two useful details for whoever does: the card leaves idle
only after a correctly framed ACMD41, and every command helper must clock
one extra byte after the R1 answer, because the model returns to its
command state on that byte and otherwise eats the next command's opcode.  An
incomplete write must not reach the backing image; a completed write must do
so exactly once.  A machine reset must return the card protocol to idle while
preserving backing bytes.

RAM/device migration does not copy ``SPI_CARD`` or a transient snapshot
overlay.  Before the full-guest migration gate, give the destination the same
coordinated shared block node or arrange explicit block migration/replication;
never allow source and destination QEMUs to write an ordinary raw file at the
same time.  With that storage contract in place, first migrate after ``sync``
and verify the FAT manifest on the destination.  Then migrate during a
workload that alternates two generation files using ``fsync`` and rename;
after shutdown, require a clean host-side filesystem check and one complete
generation.  Reboot with the same scratch image and repeat the read-only hash
check.  Dynamic card removal and the WILCS02 interrupt GPIO remain separate
future gates.
