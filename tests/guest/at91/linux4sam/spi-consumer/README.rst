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

Two blockers must be closed before running the unchanged-overlay gate:

#. QEMU currently connects the card-select input only to native FLEXCOM4
   NPCS1.  The overlay instead drives PA13 as an active-low GPIO chip select
   and uses logical SPI chip select zero.  The card therefore cannot be
   selected.  The board model must route the PA13 GPIO output to the same
   adapter while retaining the native-NPCS1 route used by the current board
   qtest.  The result needs selection, deselection, reset and migration
   coverage for both routes.
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

The release gate also needs qtests that complete SPI-mode card initialization
and single/multiple-block reads and writes, then migrate during a partial
command, response, data read, data write, CRC and stop-command phase.  An
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
