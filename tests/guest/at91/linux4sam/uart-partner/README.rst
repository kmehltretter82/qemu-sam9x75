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
the server.  Copy the script into the guest, make sure no getty owns
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

QEMU's ``serialN`` backend numbering does not apply to the physical board;
Linux4SAM still exposes the enabled FLEXCOM1 USART as ``/dev/ttyS1``.  Cross
its TX/RX pins to the RX/TX pins of a 3.3-V adapter (never an RS-232-voltage
cable).  Run the standard-library guest role on the board as above.  Run the
peer role on the workstation using optional ``pyserial``::

  python3 -m pip install pyserial
  python3 sam9x75_uart_partner.py peer \
      --serial /dev/ttyUSB0 --baud 115200 --sessions 2 --timeout 900 \
      --backpressure-every-bytes 32768 \
      --backpressure-ms 20 \
      --json sam9x75-uart-hardware.json

For macOS the adapter is normally named ``/dev/cu.usbserial-*``.  The default
line format is 115200 baud, 8 data bits, no parity, one stop bit and no flow
control.  Reset the board and restart the guest role to exercise HELLO resync.

Host-only protocol test
-----------------------

No QEMU image, guest or serial device is required::

  cd tests/guest/at91/linux4sam/uart-partner
  python3 -m unittest -v test_sam9x75_uart_partner.py

The tests cover incremental framing at all boundary sizes, CRC recovery,
deterministic directions, simultaneous full-duplex traffic, fragmentation,
backpressure, reset resynchronization from an interrupted maximum-size frame,
and the quiesce/resume barrier.
