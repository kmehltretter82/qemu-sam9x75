.. SPDX-License-Identifier: GPL-2.0-or-later

SAM9X75 full-duplex UART partner
================================

``sam9x75_uart_partner.py`` is an opt-in end-to-end fixture.  It does not add
a UART peer to the board model and therefore does not change the default
SAM9X75 Curiosity topology.  One copy runs as the host/workstation ``peer``;
the same file runs as the Linux4SAM ``guest`` exerciser.

Every frame carries magic, protocol version, session, direction, sequence,
length and CRC32 fields.  Both ends generate and check deterministic payloads
at boundary sizes from zero through 65,536 bytes.  Stop-and-wait streams run in
both directions concurrently, while configurable write fragmentation, pacing
and receive-side pauses exercise FIFO pressure.  A new guest ``HELLO`` is
accepted at any time, so restarting the guest after a warm reset establishes
a clean session without restarting the peer.  Both roles print TAP and can
atomically write a JSON report.

QEMU and Linux4SAM
------------------

The peer is an external synthetic device, not an implicit part of the QEMU
machine.  Start its AF_UNIX server before QEMU (use a path on disk, not a
RAM-backed ``/tmp``)::

  UART_DIR=/home/karl/linux-work/qemu-SAM9X75/t/uart
  mkdir -p "$UART_DIR"
  FC1_SOCKET="$UART_DIR/fc1.sock"
  python3 tests/guest/at91/linux4sam/uart-partner/sam9x75_uart_partner.py \
      peer \
      --unix-listen "$FC1_SOCKET" \
      --timeout 900 \
      --backpressure-every-bytes 32768 --backpressure-ms 20 \
      --json "$UART_DIR/peer.json"

Backpressure intervals count received wire bytes, not transport ``read()``
calls.  The 32,768-byte interval therefore injects the same bounded workload
when an AF_UNIX backend returns one byte per call and when a physical adapter
coalesces many bytes.  Timeout accounting begins before QEMU boots, so the
900-second peer timeout includes firmware and Linux startup.  The unthrottled
full-duplex test is the release gate; this 32-KiB/20-ms profile is supplemental
RX-pressure coverage and produced six deterministic pauses at each endpoint
in the exact Linux4Microchip 2026.04 guest.  Leave ``--pace-us`` at zero:
fragmentation still occurs on every frame, while byte-based pauses avoid
making the result depend on emulated userspace's sub-millisecond sleep
scheduling.  Omit both backpressure options for the release-gate run.  Add
``--progress`` for one diagnostic line per validated DATA and ACK frame.

QEMU serial backends and SAM9X75 peripherals have this exact ordering:

* ``serial0`` is DBGU at ``0xfffff200``; Linux4SAM names it ``ttyS0``.
* ``serial1`` is FLEXCOM0 at ``0xf801c000``.  It occupies an index even though
  its USART child is disabled in the Linux4Microchip 2026.04 device tree.
* ``serial2`` is FLEXCOM1 at ``0xf8020000``.  Its enabled USART child at
  ``0xf8020200`` is Linux4SAM ``ttyS1``.
* ``serial6`` is FLEXCOM5.  ``serial3`` through ``serial5`` are the required
  FLEXCOM2 through FLEXCOM4 positions if a QEMU FC5 chardev is added.

Consequently, the null ``serial1`` is intentional and must not be omitted.
The following runnable skeleton shows the AF_UNIX client connection; append
the firmware, storage and network arguments from the normal Linux4SAM launch::

  FC1_CHARDEV=socket,id=fc1,path="$FC1_SOCKET",server=off
  FC1_CHARDEV="$FC1_CHARDEV",reconnect-ms=1000

  qemu-system-arm \
      -M sam9x75-curiosity \
      -display none -monitor none \
      -serial stdio \
      -serial null \
      -chardev "$FC1_CHARDEV" \
      -serial chardev:fc1

The QEMU chardev is the AF_UNIX client (``server=off``); the synthetic peer is
the server.  AF_UNIX transports bytes only: they do not carry CTS, RTS or
other modem-line state, so ``--rtscts`` and the manual RTS gate below are for
a physical serial adapter, not this QEMU topology.  Copy the script into the
guest, make sure no getty owns
``ttyS1``, and run::

  test -c /dev/ttyS1
  dmesg | grep 'ttyS1 at MMIO 0xf8020200'
  systemctl stop serial-getty@ttyS1.service 2>/dev/null || true
  python3 /root/sam9x75_uart_partner.py guest --device /dev/ttyS1 \
      --timeout 900 \
      --backpressure-every-bytes 32768 --backpressure-ms 20 \
      --json /tmp/sam9x75-uart-guest.json

The process exits nonzero on a framing, CRC, payload, direction, sequence or
timeout failure.  Use ``peer --sessions 2`` and restart the guest exerciser
after a reset to require two complete, separately identified sessions.

QEMU four-wire serial partner
-----------------------------

For a true modem-line-capable QEMU peer, use two 3.3-V USB-to-UART adapters as
a null-modem link.  This needs no SAM9X75 board.  Call the adapter opened by
QEMU A and the adapter opened by the Python peer B.  Do not connect either
adapter's supply pin::

  A TX  -> B RX
  B TX  -> A RX
  A RTS -> B CTS
  B RTS -> A CTS
  A GND -> B GND

The host drivers for both adapters must implement ``TIOCMGET`` and
``TIOCMSET``.  An ordinary PTY and QEMU's socket chardev do not.  Start the
peer on adapter B::

  python3 sam9x75_uart_partner.py peer \
      --serial /dev/ttyUSB1 --baud 115200 --timeout 900 \
      --rts-pause-after-bytes 32768 --rts-pause-ms 250 \
      --json qemu-four-wire-peer.json

Attach adapter A to the already-enabled FLEXCOM1 USART.  The preceding null
entry is FLEXCOM0 and is required to put this backend at ``serial2``::

  qemu-system-arm \
      -M sam9x75-curiosity \
      -display none -monitor none \
      -serial stdio \
      -serial null \
      -chardev serial,id=fc1,path=/dev/ttyUSB0 \
      -serial chardev:fc1 \
      ... normal Linux4SAM firmware, storage and network arguments ...

Inside Linux4SAM, stop any getty and enable hardware flow control on the
guest endpoint::

  systemctl stop serial-getty@ttyS1.service 2>/dev/null || true
  python3 /root/sam9x75_uart_partner.py guest \
      --device /dev/ttyS1 --baud 115200 --rtscts --timeout 900 \
      --json /tmp/sam9x75-uart-rtscts-guest.json

The AT91 USART model consumes adapter A's CTS state and drives its RTS state;
QEMU's host tty is kept out of automatic ``CRTSCTS`` mode so flow control is
owned by the emulated controller.  The peer's manual pause drives B RTS low,
which becomes A CTS and holds the guest's next transmit byte.  B CTS samples
record the opposite, guest-RTS direction.  This topology is the pre-hardware
gate for the same four-wire behavior later checked on the Curiosity board.

QEMU USB-serial consumer
------------------------

The same fixture can drive QEMU's FT232BM-compatible ``usb-serial`` device.
This is a protocol-aware external consumer for the SAM9X75 UHPHS host: the
Linux4Microchip 2026.04 guest enumerates the full-speed device through the
OHCI companion, binds ``ftdi_sio`` and exposes ``/dev/ttyUSB0``.  No device
tree change is needed.

From the QEMU source directory, prepare a disposable payload directory and
start the peer in a separate terminal.  Keep the socket and JSON report on a
disk-backed path::

  USB_SERIAL_DIR="$PWD/t/usb-serial"
  USB_SERIAL_SOCKET="$USB_SERIAL_DIR/usbserial.sock"
  PAYLOAD_DIR="$USB_SERIAL_DIR/payload"
  mkdir -p "$PAYLOAD_DIR"
  cp tests/guest/at91/linux4sam/uart-partner/sam9x75_uart_partner.py \
      "$PAYLOAD_DIR/"
  python3 tests/guest/at91/linux4sam/uart-partner/sam9x75_uart_partner.py \
      peer --unix-listen "$USB_SERIAL_SOCKET" --timeout 900 --progress \
      --json "$USB_SERIAL_DIR/usbserial-peer.json"

Add these arguments to the normal Linux4SAM firmware and SD-image launch.
Port 1 supplies the script to the guest; the full-speed serial adapter on
port 2 is automatically routed to the OHCI companion of ``usb-bus.0``::

  -drive file=fat:rw:"$PAYLOAD_DIR",if=none,id=payload,format=raw \
  -device usb-storage,id=payload,bus=usb-bus.0,port=1,drive=payload \
  -chardev socket,id=usbser,path="$USB_SERIAL_SOCKET",server=off,reconnect-ms=1000 \
  -device usb-serial,id=usbserial,bus=usb-bus.0,port=2,chardev=usbser,serial=SAM9X75FTDI

With an ``init=/bin/sh`` Linux4SAM boot, mount the pseudo-filesystems and FAT
payload, then run the guest endpoint::

  mount -t proc proc /proc
  mount -t sysfs sysfs /sys
  mkdir -p /mnt/payload
  mount -t vfat -o ro /dev/sda1 /mnt/payload
  test -c /dev/ttyUSB0
  lsusb -t
  dmesg | grep -E 'FTDI|ttyUSB|QEMU USB SERIAL'
  python3 /mnt/payload/sam9x75_uart_partner.py guest \
      --device /dev/ttyUSB0 --timeout 600 --progress \
      --json /tmp/sam9x75-usbserial-guest.json

The exact Linux4Microchip 2026.04 gate completed TAP plan ``1..7`` without a
failure at either endpoint.  Six exercised checks passed and the unrequested
migration-barrier check was skipped.  The run covered 27 boundary frames in
each direction, 210,478 received wire bytes per endpoint, and reported no
framing, CRC, payload, sequence or timeout error.  ``lsusb -t`` showed
``ftdi_sio`` at 12 Mbit/s.

By default the peer exits and closes its AF_UNIX socket after the requested
session count.  QEMU's ``usb-serial`` defaults to ``always-plugged=off``, so a
closed chardev detaches the adapter and ``ttyUSB0`` may disappear immediately
after successful TAP completion.  Treat the six exercised checks as the
data-path result.  A disconnect or re-enumeration gate must deliberately keep
or replace the peer connection and assert the corresponding Linux hotplug
events.
``always-plugged=on`` can retain the USB device when the peer closes, but it
deliberately hides that detach event.  The generic QEMU ``usb-serial`` device
is unmigratable, so the migration barrier below does not apply to this
topology.

QEMU UDPHS gadget-serial self-loop
----------------------------------

An optional cable bridge can connect the SAM9X75 UDPHS device controller to
Port B of its own UHPHS host controller.  This is deliberately not part of
the default machine: it represents attaching a cable between two board
connectors.  Unlike ``usb-serial`` above, Linux owns both ends of this link.
The gadget side uses the real ``atmel_usba_udc`` and ``g_serial`` drivers;
the host side enumerates that gadget with ``cdc_acm``::

  /dev/ttyGS0 -> UDPHS -> at91-udphs-gadget -> UHPHS -> /dev/ttyACM0

The unchanged Linux4Microchip 2026.04 device tree already enables
``gadget@500000``.  Its exact kernel has ``atmel_usba_udc.ko`` and
``g_serial.ko``, built-in AT91 EHCI/OHCI and built-in CDC ACM support.  Add
the bridge to a normal Linux4SAM invocation; using Port 2 avoids the shared
Port A transceiver which UDPHS takes away from UHPHS while device mode is
enabled::

  -device at91-udphs-gadget,id=udphs-loop,udphs=/machine/soc/udphs,bus=usb-bus.0,port=2

The script may initially be supplied with the read-only USB-storage payload
shown in the preceding section.  Copy it to the root filesystem and unmount
that payload before enabling UDPHS: the storage device uses high-speed Port A
(``port=1``), which the hardware mux intentionally disconnects when UDPHS
takes the shared transceiver.  The self-loop remains on independent Port B
(``port=2``)::

  cp /mnt/payload/sam9x75_uart_partner.py /root/
  sync
  umount /mnt/payload

Alternatively, put the script in the root filesystem before boot.  Start
QEMU with ``-d unimp,guest_errors`` and a disk-backed ``-D`` path so the
diagnostic log can be checked after shutdown.

Inside the guest, mount ``proc`` and ``sysfs`` when using ``init=/bin/sh``,
load the UDC before its legacy gadget function, and wait for enumeration::

  mount -t proc proc /proc 2>/dev/null || true
  mount -t sysfs sysfs /sys 2>/dev/null || true
  modprobe atmel_usba_udc
  test -n "$(ls -A /sys/class/udc)"
  modprobe g_serial

  n=0
  while test ! -c /dev/ttyGS0 && test "$n" -lt 30; do
      sleep 1
      n=$((n + 1))
  done
  test -c /dev/ttyGS0

Do not rely on ``ttyACM0`` when another ACM device is present.  Find the
host-side tty by walking its USB parents and requiring gadget serial's
``0525:a4a7`` identity.  This also records the USB device node used for the
speed and topology checks::

  discover_gadget_acm()
  {
      matches=0
      match_tty=
      match_node=
      for tty in /sys/class/tty/ttyACM*; do
          test -e "$tty/device" || continue
          node=$(readlink -f "$tty/device")
          usb_node=
          while test "$node" != /; do
              if test -r "$node/idVendor" && test -r "$node/idProduct" && \
                 test "$(cat "$node/idVendor")" = 0525 && \
                 test "$(cat "$node/idProduct")" = a4a7; then
                  usb_node=$node
                  break
              fi
              node=${node%/*}
              test -n "$node" || node=/
          done
          if test -n "$usb_node"; then
              matches=$((matches + 1))
              match_tty=$tty
              match_node=$usb_node
          fi
      done
      test "$matches" = 1 || return 1
      printf '%s %s\n' "$match_tty" "$match_node"
  }

  n=0
  until HOST_INFO=$(discover_gadget_acm); do
      test "$n" -lt 30 || break
      sleep 1
      n=$((n + 1))
  done
  test -n "$HOST_INFO"
  set -- $HOST_INFO
  HOST_TTY=/dev/${1##*/}
  GADGET_USB_NODE=$2
  test -c "$HOST_TTY"
  test "$(cat "$GADGET_USB_NODE/speed")" = 480
  test "${GADGET_USB_NODE##*/}" = 1-2

The final ``1-2`` assertion is for the exact single-controller topology and
the explicit ``port=2`` above; use the discovered node rather than that bus
number if another launch changes USB bus enumeration.  Capture both shared
UHPHS source 22 and UDPHS source 23 after the two ttys exist::

  irq_count()
  {
      awk -v source="$1" \
          '$0 ~ "atmel-aic5[[:space:]]+" source "[[:space:]]" { print $2 }' \
          /proc/interrupts
  }
  UHPHS_BEFORE=$(irq_count 22)
  UDPHS_BEFORE=$(irq_count 23)
  test -n "$UHPHS_BEFORE" && test -n "$UDPHS_BEFORE"
  DMESG_LINES=$(dmesg | wc -l)

Run the dependency-free POSIX-tty peer on the host-controller endpoint, then
run the normal guest role on the gadget endpoint.  ``--tty`` is intentionally
different from ``--serial``: the former uses only Python's standard library;
the latter selects pyserial and its physical-adapter modem-line controls::

  python3 /root/sam9x75_uart_partner.py peer \
      --tty "$HOST_TTY" --timeout 900 \
      --json /root/sam9x75-udphs-acm-peer.json &
  PEER_PID=$!
  python3 /root/sam9x75_uart_partner.py guest \
      --device /dev/ttyGS0 --session 0x202608260104 --timeout 900 \
      --json /root/sam9x75-udphs-acm-gadget.json
  GADGET_STATUS=$?
  wait "$PEER_PID"
  PEER_STATUS=$?
  test "$GADGET_STATUS" = 0 && test "$PEER_STATUS" = 0

Both TAP plans and both JSON reports are required.  They prove deterministic
full-duplex data, CRC, direction and sequence integrity across control
enumeration, the CDC interrupt endpoint, bulk endpoints, the UDPHS data path
and the UHPHS schedule.  Check that traffic reached both controllers::

  UHPHS_AFTER=$(irq_count 22)
  UDPHS_AFTER=$(irq_count 23)
  test "$UHPHS_AFTER" -gt "$UHPHS_BEFORE"
  test "$UDPHS_AFTER" -gt "$UDPHS_BEFORE"
  dmesg | tail -n +$((DMESG_LINES + 1)) > /root/udphs-acm-dmesg-new.txt
  ! grep -Ei 'usb.*(error|fail|timeout|stall|reset)|dma.*error' \
      /root/udphs-acm-dmesg-new.txt

At QEMU ``8af5934947``, the exact Linux4Microchip 2026.04 first session passed
TAP plan ``1..7`` at both endpoints.  Each side validated 27 DATA frames and
27 acknowledgements, including the 65,536-byte boundary, with 210,478
received wire bytes and no CRC, framing, discard, duplicate or protocol
error.  The shared UHPHS IRQ advanced by 6,435, the UDPHS UDC IRQ by 3,898,
and Linux's global error count stayed zero.

After the guest and peer processes have closed both ttys, a second session
checks a deliberate pull-up disconnect and fresh enumeration.  Unload only
the gadget function, wait for the matching ``ttyACM`` to disappear, reload
it, repeat identity-based tty discovery, and rerun both roles with a new
nonzero session such as ``0x202608260105``::

  rmmod g_serial
  n=0
  while test -e "$HOST_TTY" && test "$n" -lt 30; do
      sleep 1
      n=$((n + 1))
  done
  test ! -e "$HOST_TTY"
  modprobe g_serial

Do not reuse the old host tty name: Linux may allocate a different
``ttyACM`` number.  Rediscover ``0525:a4a7`` and start two fresh processes::

  HOST_INFO=
  n=0
  until HOST_INFO=$(discover_gadget_acm); do
      test "$n" -lt 30 || break
      sleep 1
      n=$((n + 1))
  done
  test -n "$HOST_INFO"
  set -- $HOST_INFO
  HOST_TTY=/dev/${1##*/}
  GADGET_USB_NODE=$2
  test -c "$HOST_TTY" && test -c /dev/ttyGS0

  python3 /root/sam9x75_uart_partner.py peer \
      --tty "$HOST_TTY" --timeout 900 \
      --json /root/sam9x75-udphs-acm-reload-peer.json &
  PEER_PID=$!
  python3 /root/sam9x75_uart_partner.py guest \
      --device /dev/ttyGS0 --session 0x202608260105 --timeout 900 \
      --json /root/sam9x75-udphs-acm-reload-gadget.json
  GADGET_STATUS=$?
  wait "$PEER_PID"
  PEER_STATUS=$?
  test "$GADGET_STATUS" = 0 && test "$PEER_STATUS" = 0
  test "$(irq_count 22)" -gt "$UHPHS_AFTER"
  test "$(irq_count 23)" -gt "$UDPHS_AFTER"

An expected disconnect message is not an error.  Require the second pair of
TAP/JSON reports, no unexpected reset or DMA error, and an empty QEMU
``unimp,guest_errors`` log after a clean shutdown.

That same exact-head run passed the reconnect gate.  The identity-matched
device and tty disappeared after ``rmmod``, both module operations returned
zero, and the fresh device reused Port B and ``ttyACM0`` while its USB device
number changed from 3 to 4.  Both second-session endpoints again passed TAP
``1..7``, all 27 frames and acknowledgements, the 65,536-byte boundary and
210,478 received wire bytes.  UHPHS and UDPHS interrupts advanced by 6,347
and 3,893 respectively, the Linux error count remained zero, and QEMU's
diagnostic log was empty.

This first gate is bounded data-path and reconnect coverage.  SOF timing,
suspend/resume, isochronous transfers and migration with in-flight USB
packets remain separate tests.  For quiescent migration, both destination
QEMUs must include the identical optional bridge and the endpoints must be
stopped at the protocol barrier before the source is handed over.

The same topology can later be compared with the physical Curiosity board.
Have the hardware operator identify the connector wired to UHPHS Port B and
the UDPHS device connector from the board schematic, then join those two
data ports with one known-good cable.  Do not join two host ports or introduce
a second VBUS source.  Run the same modules, sysfs discovery, two tty roles,
session IDs and interrupt/dmesg oracle.  Record the negotiated speed and
connector mapping, and compare both JSON byte counts and hashes with QEMU.

Migration barrier
-----------------

The peer can stop both transmitters only after in-flight DATA has been
acknowledged.  The ready file is the point at which a live migration or
save/restore should be initiated::

  python3 sam9x75_uart_partner.py peer \
      --unix-listen "$FC1_SOCKET" \
      --barrier-after 13 \
      --barrier-ready-file "$UART_DIR/ready.json" \
      --resume-file "$UART_DIR/resume" \
      --migration-reconnect-timeout 30 \
      --json "$UART_DIR/peer-migration.json"

Wait for ``ready.json``, migrate to a destination configured with the same
client chardev, wait for the migration to complete, then create ``resume``.
The ready and resume paths must not exist when the peer starts.
With ``--migration-reconnect-timeout``, the fixture requires a second AF_UNIX
connection and switches from the stopped source to the destination before it
sends ``RESUME``.  Omit that option for an in-process save/restore test.

Physical SAM9X75 board
----------------------

Do not remux the Curiosity kit's FLEXCOM1 PC27/PC28 pins for CTS/RTS.  Those
pins conflict with the board routing used by the shipping configuration.
Keep the existing FLEXCOM1 ``/dev/ttyS1`` test as a two-wire TX/RX gate.

A candidate independent four-wire path is FLEXCOM5 on the M.2 interface.  It
still needs a board-specific device-tree/pinctrl overlay and physical
continuity check before use.  Wire a 3.3-V TTL adapter (never an RS-232-level
cable) as follows, with a common ground:

* board PA16/FLEXCOM5 TX to adapter RX;
* board PA15/FLEXCOM5 RX to adapter TX;
* board PA14/FLEXCOM5 CTS from adapter RTS; and
* board PA30/FLEXCOM5 RTS to adapter CTS.

Do not assume a Linux tty number: identify the newly enabled FLEXCOM5 USART
from its MMIO address in ``dmesg`` and pass that device explicitly.  QEMU's
corresponding byte chardev position is ``serial6``, but QEMU AF_UNIX modem
lines are not implemented and cannot validate this four-wire gate.

Run the peer role on the workstation using optional ``pyserial``.  The
following opt-in gate deasserts adapter RTS once, after 32,768 received wire
bytes.  While RTS is low it sends the protocol ACK which releases the board's
next transmission, holds the board's CTS inactive for 250 ms, and then
restores RTS::

  python3 -m pip install pyserial
  python3 sam9x75_uart_partner.py peer \
      --serial /dev/ttyUSB0 --baud 115200 --sessions 2 --timeout 900 \
      --rts-pause-after-bytes 32768 --rts-pause-ms 250 \
      --backpressure-every-bytes 32768 \
      --backpressure-ms 20 \
      --json sam9x75-uart-hardware.json

For macOS the adapter is normally named ``/dev/cu.usbserial-*``.  The default
line format is 115200 baud, 8 data bits, no parity and one stop bit.  Enable
hardware handshaking on the board endpoint explicitly::

  python3 /root/sam9x75_uart_partner.py guest \
      --device /dev/ttyS_DEVICE_FOR_FLEXCOM5 --baud 115200 --rtscts \
      --timeout 900 --json /tmp/sam9x75-uart-rtscts-guest.json

The JSON peer report records RTS deassert/assert readback, adapter CTS samples
and input-queue depths around the pause.  The peer deliberately leaves
pyserial's automatic ``rtscts`` mode off because an OS-owned RTS line cannot
also be toggled deterministically.  This gate therefore exercises the
adapter-RTS to board-CTS direction; adapter CTS is observed but does not gate
peer writes.  Queue growth during the low interval may include bytes already
in hardware or driver queues, so it is diagnostic rather than a strict
failure.  Successful protocol completion proves that the line transition did
not lose or corrupt data, but it does not by itself prove electrical polarity
or the exact instant at which silicon stopped transmitting.  Confirm those
with a logic analyzer for the hardware sign-off.

Without ``--rtscts`` and ``--rts-pause-after-bytes``, behavior remains the
original two-wire test.  Reset the board and restart the guest role to
exercise HELLO resync.

Host-only protocol test
-----------------------

No QEMU image, guest or serial device is required::

  cd tests/guest/at91/linux4sam/uart-partner
  python3 -m unittest -v test_sam9x75_uart_partner.py

The tests cover incremental framing at all boundary sizes, CRC recovery,
deterministic directions, simultaneous full-duplex traffic, fragmentation,
backpressure, reset resynchronization from an interrupted maximum-size frame,
the quiesce/resume barrier, and safe RTS restore/readback with a fake
PySerial-like endpoint.  They also cover ``peer --tty`` parser isolation and
binary I/O in both directions through a real host PTY without pyserial.
