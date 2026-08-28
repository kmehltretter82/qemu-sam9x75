.. SPDX-License-Identifier: GPL-2.0-or-later

SAM9X75 M_CAN protocol partner
==============================

``sam9x75_can_partner.py`` is an opt-in, deterministic SocketCAN endpoint for
the two Bosch M_CAN controllers in the SAM9X75 model.  Its ``peer`` role runs
on a Linux QEMU host or on a workstation connected to a physical board.  Its
``guest`` role runs inside Linux4Microchip.  The protocol and SocketCAN codec
use only Python's standard library; python-can and can-utils remain useful
independent cross-checks but are not prerequisites for the integrity oracle.

The fixture validates semantic frames rather than merely counting traffic.
Each session performs:

* a versioned session handshake;
* classic SFF and EFF frames at lengths zero through eight;
* SFF and EFF remote frames with DLC zero and eight;
* every legal CAN-FD length in SFF and EFF format, with BRS off and on;
* optional host-to-guest ESI reception at the 64-byte boundary;
* zero, all-one, incrementing and SHA-256-derived payload patterns;
* windowed simultaneous 64-byte CAN-FD/BRS traffic in both directions;
* per-frame sequence, session, identifier, flags, length and payload checks;
* an explicit final completion exchange.

Control and stress frames occupy separate extended-ID namespaces.  Stress
IDs contain direction, a session tag and sequence.  The full 64-bit session
is carried in every control frame.  The receiver therefore reports gaps,
duplicates, corruption and stale sessions separately.  Socket receive queue
overflow is collected through ``SO_RXQ_OVFL`` and is always a failed gate.

Linux4Microchip 2026.04 inventory
---------------------------------

The exact Linux4Microchip 2026.04 image uses Linux
``6.18.17-linux4microchip-2026.04`` and provides:

* ``CONFIG_CAN_RAW=y`` and the M_CAN platform driver;
* iproute2 6.14.0;
* can-utils 2023.03, including ``candump``, ``cansend``, ``cangen``,
  ``canfdtest`` and ``cansequence``;
* Python 3.12.12, python-can 4.5.0 and python-canopen 2.3.0.

ISO-TP and J1939 userspace binaries are present, but their kernel protocols
are disabled in this image and are not part of this test.

Host-only tests
---------------

The unit suite uses queue-backed endpoints and never needs a CAN device,
root privilege or an external host::

  cd tests/guest/at91/linux4sam/can-partner
  python3 -m unittest -v test_sam9x75_can_partner.py

It covers classic/RTR/FD SocketCAN codecs, malformed datagrams, namespace
encoding, the complete boundary matrix, deterministic data, explicit
duplicate/gap/corruption/stale-session failures, simultaneous full duplex,
the migration barrier, two reset sessions and atomic JSON replacement.

Isolated QEMU topology
----------------------

Never attach both guest controllers to one QEMU CAN bus for a host-peer
gate.  The other guest controller could accept a frame and make transmission
appear successful even when the host backend is missing or broken.  Use one
emulated controller and one host endpoint per QEMU bus::

  -object can-bus,id=canbus0 \
  -object can-host-socketcan,id=canhost0,if=s9x75c0,canbus=canbus0 \
  -object can-bus,id=canbus1 \
  -object can-host-socketcan,id=canhost1,if=s9x75c1,canbus=canbus1 \
  -M sam9x75-curiosity,canbus0=canbus0,canbus1=canbus1

Run one fixture pair per independent interface.  It is fine to begin with
only ``can0``/``s9x75c0``.  The two-controller gate starts two peer processes
with distinct JSON paths and two guest processes with distinct sessions.

Private vcan setup
------------------

On Linux, load the vcan module once.  Then create interfaces in a private
user/network namespace so the test does not alter the host network::

  sudo modprobe vcan
  unshare --user --map-root-user --net --mount-proc sh
  ip link add s9x75c0 type vcan
  ip link add s9x75c1 type vcan
  ip link set s9x75c0 up
  ip link set s9x75c1 up

QEMU and the corresponding host peer must run in that same namespace.  Raw
SocketCAN open/bind does not normally require root.  ``modprobe`` requires
host privilege and creating a global vcan requires ``CAP_NET_ADMIN``; the
private namespace avoids granting that capability in the initial namespace.

macOS has no native SocketCAN backend.  Run the host peer and QEMU in a Linux
VM, or use a Linux workstation for this gate.

Device tree
-----------

The exact base device tree has both controller nodes disabled.  Work on a
copy of the merged DTB and change only the desired ``status`` properties::

  fdtput -t s sam9x75-can-test.dtb /apb/can@f8000000 status okay
  fdtput -t s sam9x75-can-test.dtb /apb/can@f8004000 status okay

Preserve the existing clocks and ``bosch,mram-cfg`` values.  MCAN0 is at
``0xf8000000`` with Linux INT0 on AIC source 29; MCAN1 is at ``0xf8004000``
with INT0 on source 30.  Both allocate message RAM from SRAM0 at
``0x00300000``.

QEMU host peer
--------------

Start the peer before booting the guest so the timeout includes firmware and
Linux startup::

  CAN_DIR=/path/to/qemu/tests/guest/at91/linux4sam/can-partner
  WORK=/path/on/disk/sam9x75-can
  mkdir -p "$WORK"

  python3 "$CAN_DIR/sam9x75_can_partner.py" peer \
      --interface s9x75c0 --frames 10000 --window 16 --include-esi \
      --timeout 1200 \
      --json "$WORK/can0-peer.json"

The default is 1,000 stress frames; 10,000 is the longer release gate.  A
window of 16 keeps pressure below the modeled 32-entry TX FIFO while still
exercising more than stop-and-wait traffic.  Use a work directory on disk for
logs and JSON output.

Linux4Microchip guest
---------------------

At a root shell, the guest role can configure the controller itself::

  python3 ./sam9x75_can_partner.py guest \
      --interface can0 --configure \
      --bitrate 500000 --data-bitrate 2000000 --txqueuelen 4096 \
      --frames 10000 --window 16 --include-esi --timeout 1200 \
      --session 0x202608260001 \
      --json /root/can0-guest.json

``--include-esi`` must match at both endpoints.  It adds four supplemental
peer-to-guest CAN-FD frames, covering SFF/EFF with BRS off/on.  Omit it on
physical adapters which cannot request ESI from userspace; the normal matrix
always tests ESI clear.

The same restriction applies when a second Linux M_CAN interface is used as
the peer on an internal QEMU CAN bus.  Linux documents ``CANFD_ESI`` as
controller-generated for real CAN devices, and the ``m_can`` transmit path
does not copy a userspace ESI request into the transmit element.  Therefore a
healthy, error-active ``can1`` cannot inject the supplemental ESI frames into
``can0``.  Omit ``--include-esi`` for that two-controller topology.  Retain it
when the peer is a virtual SocketCAN interface such as host ``vcan``, which is
able to inject the flag and remains the ESI receive-path gate.

Without ``--configure``, perform the equivalent setup explicitly::

  ip link set can0 down 2>/dev/null || true
  ip link set can0 type can bitrate 500000 dbitrate 2000000 \
      fd on berr-reporting on
  ip link set can0 txqueuelen 4096
  ip link set can0 up
  ip -details -statistics link show can0
  grep -E 'f8000000.can|m_can|can0| 29:' /proc/interrupts

The script records before/after sysfs packet, byte, error and drop counters,
the ``ip -details -statistics`` text, protocol counts and transport counts.
Both roles print TAP and atomically replace their JSON report.  The host
peer's final TAP result is authoritative: it sends ``COMPLETE`` only after
its local counter gate passes, the guest sends ``COMPLETE_ACK`` only after
its own gate passes, and the peer repeats its gate after receiving that ACK.

Independent can-utils checks
----------------------------

The installed tools should also be exercised independently.  Start one of
these responder commands in the guest::

  canfdtest -f 16 -s 8 -v can0
  canfdtest -d -b -e -f 16 -s 64 -v can0

Run the matching generator on the host-side interface::

  timeout 300 canfdtest -g -f 16 -l 10000 -s 8 -v s9x75c0
  timeout 300 canfdtest -g -d -b -e -f 16 -l 10000 -s 64 -v s9x75c0

Stop the guest responder after the generator exits successfully.  The
responder intentionally has no finite-loop exit, so wrapping that side in
``timeout`` normally returns 124 and is not a pass result by itself.

Other useful generator/capture tools include::

  cangen can0 -g 0 -p 100 -n 10000 -I i -L 8 -D i
  cangen can0 -g 0 -p 100 -n 10000 -f -b -I i -L 64 -D i

  candump -L -d -e -x -n 10000 -r 4194304 -T 30000 can0
  cansequence --loop=10000 --poll can0

``canfdtest`` is not the protocol implemented by this script.  Treat these as
separate interoperability cases, not commands to run concurrently with the
deterministic fixture.

Validated internal Linux4Microchip gates
-----------------------------------------

At QEMU repository checkpoint ``4ab0af5c6a`` (machine binary source
``1851743e84``), Linux4Microchip 2026.04 passed the standalone semantic
profile with 10,000 stress frames in each direction, window 16 and all 86
boundary cases each way.  Both roles passed TAP 5/5 with exact sent, received
and acknowledged counts, zero integrity error, zero interface error/drop,
zero receive-queue overflow and advancing controller interrupts.  The
authoritative workspace-local evidence is
``t/linux4microchip-can-20260827/release-r2`` relative to the workspace root.

A separate boot ran can-utils 2023.03 entirely inside the guest, with
``can0`` as responder and ``can1`` as generator on the shared QEMU CAN bus.
Both ``canfdtest -g -f 16 -l 10000 -s 8`` and
``canfdtest -g -d -b -f 16 -l 10000 -s 64`` reported exactly 10,000 messages
sent and received.  ESI was correctly omitted for this real-controller peer
topology.  The authoritative evidence is
``t/linux4microchip-canfdtest-20260827/release-r1``.

These are internal controller/driver/protocol and userspace-interoperability
gates.  They do not replace the isolated ``can-host-socketcan`` profile above,
which is still required to prove both host backends independently and to
inject ESI through virtual ``vcan`` peers.

Reset and migration hooks
-------------------------

For a quiescent reset test, make the peer require two completed sessions::

  python3 sam9x75_can_partner.py peer --interface s9x75c0 \
      --sessions 2 --frames 1000 --timeout 1800

Complete one guest invocation, reset/reboot/reconfigure the board, then run a
second invocation with a new session ID.  Reusing a completed session is an
error.  The peer can also recognize a new HELLO while an older session is
incomplete and records that older session as aborted; stale boundary frames
after an abrupt reset are not accepted as a successful recovery.

Migration uses an application-quiescent barrier after all boundary frames
and before stress traffic::

  python3 sam9x75_can_partner.py peer --interface s9x75c0 \
      --frames 1000 --timeout 1800 \
      --barrier-ready-file "$WORK/migrate-ready.json" \
      --resume-file "$WORK/migrate-resume"

Start the guest normally.  When the fresh ready JSON appears, migrate QEMU
with no CAN application frames in flight.  After the destination is running,
create the resume file.  The complete full-duplex stress phase must then
pass.  Both paths must be absent before peer startup to prevent a stale file
from silently bypassing the barrier.

This quiescent whole-machine profile passed at repository checkpoint
``95ba1ee289``.  QMP completed a file migration after all 86 boundary cases
and before stress, the source exited, and an identical destination restored
the serial shell, Linux processes, both M_CAN controllers, shared Message RAM,
interrupt state and the mounted USB evidence disk.  After the destination
created the resume file, both roles passed TAP 5/5 and exactly 10,000 stress
frames in each direction.  The authoritative workspace-local evidence is
``t/linux4microchip-migration-20260828/release-r4``; its peer report attests
``migration_barrier_used: true`` and its QMP report records completed status.
The source and destination diagnostic logs are both empty.

The quiescent result alone does not prove in-flight CAN migration because no
application frame is outstanding at its barrier.  The controlled profile
below closes that separate internal-bus gate by identifying an observed
sequence window, proving that the source stopped and applying the existing
gap/duplicate/corruption oracle after the destination resumes.

The peer can emit that non-pausing active-stress trigger explicitly::

  python3 sam9x75_can_partner.py peer --interface s9x75c0 \
      --frames 10000 --window 16 --timeout 1800 \
      --inflight-ready-file "$WORK/inflight-ready.json" \
      --inflight-at 3000

The peer atomically writes the marker after receiving at least the requested
stress count while it still has locally transmitted sequences awaiting
acknowledgement and more traffic remains to send.  The JSON records the exact
sent, received and acknowledged counts, next sequence, outstanding sequence
set, session and timestamp.  It does not pause either endpoint.  Start QMP
migration only after observing a fresh marker, require both endpoint rc files
to remain absent at that point, and retain QMP's completed migration report.
The in-flight marker and quiescent barrier options are intentionally mutually
exclusive, and each currently requires a single peer session.

This controlled profile passed at repository checkpoint ``fcfbe1ae0e`` with
the exact Linux4Microchip image.  Its marker observed receive sequence 3000
while both roles were unfinished and 16 peer transmissions remained
outstanding: sequences 2992--3007, with sent/next 3008, received 3000 and
acknowledged 2992.  QMP migrated the 256 MiB machine in 431 ms with 25 ms
downtime and zero RAM remaining, then the source exited.  The destination was
proven running before either endpoint rc file existed.  Both roles then
passed TAP 5/5 and exactly 10,000 sent, received and acknowledged frames with
zero semantic, controller or queue failure.  Both QEMU diagnostic logs were
empty and every filesystem, image and immutable-root integrity gate passed.
The authoritative evidence is
``t/linux4microchip-inflight-migration-20260828/release-r4``.

For link recovery, operate on the host vcan only between completed sessions::

  ip link set s9x75c0 down
  ip link set s9x75c0 up

Then reconfigure the guest if needed and require a new complete session.
Link-down during active traffic is exploratory: the QEMU CAN bus API has no
link-state notification and a failed host write can leave M_CAN TX pending
without a modeled retry/error-confinement transition.

Pass criteria and model limits
------------------------------

A release run requires exact boundary/stress counts in both JSON reports,
zero sequence gaps, duplicates or corruption, zero ``SO_RXQ_OVFL``, no
kernel RX/TX errors or drops, an ``ERROR-ACTIVE`` controller, advancing INT0
counts and clean QEMU ``-d unimp,guest_errors`` output.  Foreign traffic is a
failure by default.  ``--ignore-foreign`` exists only for diagnosis on a
shared bus and must not be used for the isolated release gate.

This fixture cannot make QEMU's untimed CAN model prove physical bitrate,
arbitration, ACK/error-frame behavior, retransmission, error confinement or
bus-off recovery.  The achieved active-migration profile uses one internal
QEMU bus, so both M_CAN controllers and their bus state are inside the
migrated VM.  SocketCAN backend state is external to migration; active traffic
across a host backend therefore remains exploratory and is not covered by the
internal-bus release result.

Physical Curiosity board
------------------------

Use the same peer role against the workstation's real SocketCAN adapter.
Start with MCAN1: J25 pin 13 PA28 is CAN1 TX and pin 14 PA29 is CAN1 RX.
Those pins conflict with FLEXCOM1 UART, so the hardware DT must disable that
USART and select the CAN1 pinmux.  MCAN0 PA26/PA27 overlaps the debug-console
path and is a poor first external target.

Use a CAN-FD transceiver such as the documented MCP2542WFD Click, a CAN-FD
adapter, common ground and proper bus termination.  Never wire SoC TX/RX
directly to CANH/CANL.  Hardware remains necessary to confirm actual 500
kbit/s / 2 Mbit/s timing, arbitration, error counters, bus-off/recovery,
controller clocks, interrupt routing and transceiver/pinmux behavior.

Configure the workstation adapter before starting ``peer``::

  sudo ip link set can-test down 2>/dev/null || true
  sudo ip link set can-test type can bitrate 500000 dbitrate 2000000 fd on
  sudo ip link set can-test txqueuelen 4096
  sudo ip link set can-test up

Opening the peer's raw CAN socket is normally unprivileged after that setup.
Do not inject bus faults until serial recovery access and the baseline
full-duplex test have both been proven.
