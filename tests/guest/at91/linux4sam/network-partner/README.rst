.. SPDX-License-Identifier: GPL-2.0-or-later

SAM9X75 GEM/LAN8840 network partner
===================================

``sam9x75_network_partner.py`` is an opt-in end-to-end fixture for the
SAM9X75 Curiosity LAN Kit.  Its host role is a real layer-4 peer behind
QEMU's slirp backend.  Packets exercise the modeled GEM data path plus PHY
control and link behavior; QEMU does not serialize them over a modeled RGMII
electrical interface.

The host ``peer`` and Linux4SAM ``guest`` roles share a CRC-protected binary
protocol.  Two TCP streams run concurrently and each stream transfers
deterministic data in both directions at the same time.  Application-frame
sizes cover one byte through 65,535 bytes and writes are deliberately split
at irregular boundaries.  A UDP phase then checks both directions at payload
sizes from zero through 1,400 bytes.  UDP uses bounded retries, records stale
duplicate replies and rejects corruption, gaps and replies from the future.

The guest role also requires and exercises the image's normal userspace:

* ``ip`` for carrier, addressing and interface state;
* ``ping`` for the slirp host-alias reachability check;
* ``ethtool`` for PHY negotiation and driver statistics;
* Python 3 for the deterministic integrity workload;
* ``iperf3`` for an optional forward/reverse throughput phase.

Both roles print TAP and can atomically replace a JSON report.  A nonzero exit
status means that a prerequisite, semantic link check, payload check, counter
check or timeout failed.

Linux4Microchip 2026.04 inventory
---------------------------------

The exact headless Buildroot image published with Linux4Microchip 2026.04 was
inspected and exercised.  It contains all of the required tools plus the
optional programs below:

.. list-table::
   :header-rows: 1

   * - Program
     - Exact-image result
   * - ``ip``
     - iproute2 6.14.0, ``/usr/sbin/ip``
   * - ``ethtool``
     - 6.14, ``/usr/sbin/ethtool``
   * - ``iperf3``
     - 3.18, ``/usr/bin/iperf3``
   * - ``ping``
     - BusyBox 1.37.0, ``/usr/bin/ping``
   * - ``python3``
     - ``/usr/bin/python3``
   * - ``nc``
     - OpenBSD netcat, ``/usr/bin/nc``
   * - ``tftp``
     - BusyBox applet, ``/usr/bin/tftp``
   * - ``udhcpc``
     - BusyBox applet, ``/usr/sbin/udhcpc``
   * - ``ssh`` / ``sshd``
     - Present in ``/usr/bin`` and ``/usr/sbin``

The script supplies a conventional ``/usr/sbin``-inclusive search path when
PID 1 is a diagnostic shell which has a shell-local but unexported ``PATH``.

Host-only protocol tests
------------------------

The tests need permission to create and exchange data on local sockets; they
do not contact an external host::

  cd tests/guest/at91/linux4sam/network-partner
  python3 -m unittest -v test_sam9x75_network_partner.py

Coverage includes framing and CRC failures, exact boundary accounting,
fragmented receive, simultaneous full duplex over a socket pair, an actual
localhost TCP/UDP peer, delayed/lost UDP completion handling, semantic
IPv4/link/route failures, stale-readiness cleanup, prompt send failures,
early-init ``PATH`` handling and atomic JSON replacement.

QEMU user-mode network
----------------------

Use a work directory on disk.  Start the peer before QEMU so its timeout
includes firmware and Linux startup::

  REPO=/path/to/qemu
  NET_DIR="$REPO/tests/guest/at91/linux4sam/network-partner"
  WORK=/path/on/disk/sam9x75-network
  mkdir -p "$WORK"

  python3 "$NET_DIR/sam9x75_network_partner.py" peer \
      --bind 127.0.0.1 --tcp-port 19091 --udp-port 19092 \
      --streams 2 --bytes-per-direction 2097152 --udp-packets 128 \
      --timeout 900 --ready-file "$WORK/peer-ready.json" \
      --json "$WORK/peer.json"

Attach user-mode networking to the board's on-SoC GEM.  The ``-nic`` entry
without a model is intentional: the SAM9X75 machine consumes the first
default NIC configuration for its already-instantiated Cadence GEM::

  qemu-system-arm \
      -M sam9x75-curiosity \
      ...normal Linux4SAM firmware and SD arguments... \
      -nic user,id=net0,ipv6=off,tftp="$NET_DIR"

The peer binds only loopback.  The guest reaches host services through
slirp's ``10.0.2.2`` alias; the fixture never asks the guest to contact an
Internet address.  The built-in DHCP server normally assigns ``10.0.2.15``.
QEMU's ``restrict=on`` option also restricts host-alias services on some
libslirp versions and cannot support this fixture's UDP path through
``guestfwd`` (which is TCP-only).  Use the shown unrestricted, loopback-only
slirp peer, or place a TAP peer in an isolated network namespace.

At a root prompt in the exact image, fetch the same file from QEMU's read-only
TFTP export and run it with an explicit session identifier::

  tftp -g -r sam9x75_network_partner.py 10.0.2.2
  python3 ./sam9x75_network_partner.py guest \
      --host 10.0.2.2 --tcp-port 19091 --udp-port 19092 \
      --streams 2 --bytes-per-direction 2097152 --udp-packets 128 \
      --timeout 900 --session 0x2026082601 \
      --json /root/sam9x75-network-guest.json

The installed ``eth0.network`` requests DHCPv4.  When booting with a minimal
``init=/bin/sh`` rather than systemd, mount ``proc`` and ``sysfs`` and add
``--configure-dhcp`` to the guest command.  A direct-kernel boot also bypasses
AT91Bootstrap's watchdog setup; disable the watchdog immediately in that
diagnostic configuration only::

  mount -t proc proc /proc 2>/dev/null || true
  mount -t sysfs sysfs /sys 2>/dev/null || true
  devmem 0xffffff84 32 0x00001000

Use an explicit ``--session`` for reproducibility during early-boot tests.
When omitted, the script derives a nonblocking identifier from wall-clock,
monotonic-clock and process values; it does not wait for the kernel random
pool.

The guest validates that ``ip route get`` selects the requested interface and
source address, binds its TCP, UDP and optional iperf3 sockets to that source,
passes the interface explicitly to ``ping``, and requires ``ethtool -i`` to
identify the ``macb`` driver.  Traffic on another guest NIC therefore cannot
satisfy this test accidentally.  The 900-second guest timeout is intentional:
TCG can require several minutes for the default 2 MiB in each direction.

Optional iperf3 phase
---------------------

The Linux4SAM image contains iperf3, but the workstation must provide its own
iperf3 executable.  Start a separate persistent host server bound to loopback::

  iperf3 -s -B 127.0.0.1 -p 19093

Then add ``--iperf3-port 19093`` to the guest role.  It runs five-second,
two-stream JSON tests first guest-to-host and then host-to-guest.  Change the
duration with ``--iperf3-seconds``.  iperf3 is supplementary: the built-in
protocol remains the integrity oracle because it validates every byte and
does not depend on an extra host package.

Reset, link and migration hooks
-------------------------------

These cases are deliberately opt-in and occur at a quiescent protocol
boundary:

* For reset recovery, start the peer with ``--sessions 2 --timeout 1800``.
  Complete one guest invocation, reset the board, and run the guest again with
  a different explicit session.  A reused session is rejected.  Budget the
  timeout for both default 2 MiB transfers as well as both boots.
* Give QEMU a QMP socket and use ``set_link`` on backend ``net0`` to drive
  link down and up.  Observe ``ip monitor link`` in the guest, then run a new
  complete session after carrier returns.
* For migration, complete session one, migrate to a destination with the same
  NIC and host-peer configuration, then run session two.  This proves device
  continuity around a quiet network boundary.  Migration while application
  TCP/UDP frames are in flight is not automated by this fixture and must not
  be claimed from the quiescent test.

The peer removes stale readiness at the next start and on handled exit,
including failed startup.  Automation must still check its fresh PID and
must not trust the file after a crash or forced termination.  The peer's TAP
``ok`` line is emitted only after the UDP completion reply is acknowledged by
the guest and is the end-of-session barrier.  Do not reset, disconnect the
backend or migrate before that line unless the purpose of the run is fault
injection.

Physical-board peer
-------------------

The same two roles can compare QEMU with the physical Curiosity LAN Kit.
Run ``peer`` on a workstation address reachable from the board, allow the two
selected ports through the local firewall, and pass that address as the
guest's ``--host``.  Use a private test LAN, keep the byte/stream/packet
arguments identical, and archive both JSON reports plus ``ethtool -S eth0``.

Exact-image result
------------------

On 2026-08-26, Linux 6.18.17-linux4microchip-2026.04 completed the reduced
two-stream 256 KiB-per-direction and 32-packet UDP gate through slirp with the
normal merged LAN8840 interrupt overlay.  The guest and host TAP suites passed
10/10 and 1/1 respectively.  Linux received ``10.0.2.15`` by DHCP, selected
``eth0`` for the host route, passed three pings and negotiated 1 Gbit/s full
duplex.  The measured interval added 601,980 RX bytes / 1,469 packets and
605,395 TX bytes / 1,269 packets, with no errors or drops.  QEMU's
``-d unimp,guest_errors`` log was empty.

The run also covered the active-low LAN8841 interrupt on PD5.  Its level IRQ
was idle at count zero.  QMP ``set_link net0 down`` produced phylink ``Link is
Down``, carrier zero and IRQ count one; restoring the backend produced ``Link
is Up``, carrier one and IRQ count two.  A three-packet ping and another
complete TCP/UDP integrity session passed after recovery.  This closes the
earlier polling-DTB isolation, where an undriven-low PD5 caused an IRQ storm
and locked Linux in the GPIO mask/unmask path while opening ``eth0``.
