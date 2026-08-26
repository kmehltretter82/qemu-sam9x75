.. SPDX-License-Identifier: GPL-2.0-or-later

SAM9X75 Linux4SAM combined stress runner
========================================

``sam9x75_linux4sam_stress.py`` is an opt-in guest-side orchestrator for the
protocol-aware GEM/LAN8840, M_CAN, UART and crypto consumers in the adjacent
directories.  It starts the selected guest roles concurrently and runs
deterministic CPU, memory and filesystem workloads while their integrity
protocols are active.  This adds contention across interrupts, DMA, timers,
the scheduler and userspace without weakening the consumers' byte/frame
oracles.

The runner does not start QEMU or the host peers.  Start each selected peer
exactly as described in ``../network-partner/README.rst``,
``../can-partner/README.rst``, ``../uart-partner/README.rst`` and
``../crypto-consumer/README.rst``.  Copy those four Python scripts, this
runner and a reviewed manifest into the guest.  A
manifest contains argv arrays, never shell text, so quoting is preserved and
the runner cannot accidentally reinterpret metacharacters.

The example is deliberately opt-in
-----------------------------------

``manifest-qemu-example.json`` selects four modeled data paths at once:

* two simultaneous TCP streams plus bidirectional UDP through GEM/LAN8840;
* 1,000 simultaneous bidirectional CAN-FD/BRS frames through M_CAN0;
* framed full-duplex traffic with deliberate reader backpressure on FLEXCOM1;
  and
* exact-driver SHA/HMAC/AES/TDES vectors through AF_ALG, XDMAC and the crypto
  IRQ paths.

It also runs Linux4Microchip's installed ``uname``, ``python3``, ``lsblk``,
``ip``, ``ethtool`` and ``cat`` programs.  Their logs capture the kernel,
block devices, interface/PHY statistics, interrupt counts and memory state
around the workload.  This complements, rather than replaces, the network
fixture's optional ``iperf3`` phase and the CAN fixture's independent
``canfdtest`` gate.

The example assumes the scripts are mounted at ``/mnt/payload`` and that the
normal Linux4Microchip 2026.04 device-tree changes for FLEXCOM1 and M_CAN0
are already present.  Stop a getty which owns ``ttyS1`` before starting the
runner.  Review the interface names, ports, sessions and paths in the copied
manifest; session identifiers must not be reused with a persistent peer.
Running only the consumers present in a particular QEMU topology is valid.
The crypto fixture is intentionally pinned to the Linux4Microchip 6.18 image;
see its README for the newer-mainline AF_ALG compatibility boundary.

Start all requested host peers before QEMU.  For the example, use the normal
commands from their READMEs with matching arguments and sessions.  Then run
inside Linux4SAM::

  mkdir -p /root/sam9x75-stress/logs /root/sam9x75-stress/scratch
  systemctl stop serial-getty@ttyS1.service 2>/dev/null || true

  python3 /mnt/payload/sam9x75_linux4sam_stress.py \
      --manifest /mnt/payload/manifest-qemu-example.json \
      --expect-kernel linux4microchip-2026.04 \
      --cpu-mib 128 --cpu-passes 2 \
      --memory-mib 64 --memory-passes 4 \
      --scratch-dir /root/sam9x75-stress/scratch \
      --storage-mib 32 --storage-passes 4 \
      --workload-timeout 1800 \
      --log-dir /root/sam9x75-stress/logs \
      --json /root/sam9x75-stress/report.json

All three internal loads are optional.  A zero ``--cpu-mib`` or
``--memory-mib`` disables that load.  Storage does not run unless both an
existing absolute ``--scratch-dir`` and nonzero ``--storage-mib`` are given.
The runner refuses ``/`` and symlinks as scratch roots, checks free space,
creates a uniquely named child, uses ``fsync`` plus atomic rename, verifies
every byte and removes only that child after success.  Use a disposable QEMU
snapshot or dedicated scratch filesystem.  Do not point this stress phase at
an SD card containing irreplaceable board data.  ``--keep-scratch`` retains a
successful generation when explicit post-run inspection is wanted.

Each command has its own manifest timeout.  Output is continuously drained
to prevent a chatty child from blocking, while each retained log is capped at
4 MiB by default.  Log files are opened once with ``O_NOFOLLOW``, checked as
regular files before truncation and forced to mode 0600.  The aggregate TAP
result fails for a launch error, timeout, nonzero child exit, malformed worker
result or failed data verification.  A failed preflight command gates the
test: all consumers and internal loads are reported as TAP skips, while the
postflight commands and aggregate report still run.  The runner preserves the
invoking ``PATH`` and appends the conventional
``/usr/sbin:/usr/bin:/sbin:/bin`` entries needed by an early diagnostic shell.
The atomic JSON report contains argv, timing, exit status, bounded-log hashes
and the internal workload byte counts and hashes.  The selected host peers'
final TAP/JSON gates remain independently required; one aggregate guest
report cannot substitute for their view of delivered traffic.

Timeout cleanup sends TERM and then KILL to the child's process group.  Every
wait remains bounded; if a process cannot be reaped after KILL, its result is
``timeout-unkillable`` instead of allowing the runner itself to wait forever.
An outer QEMU/job timeout is still required because a guest task stuck in
uninterruptible kernel I/O may outlive all signals.

Why these loads are useful
--------------------------

The SHA-256 worker repeatedly hashes a deterministic buffer.  The memory
worker fills an allocated region with a different deterministic pattern on
every pass and verifies its full digest.  The filesystem worker writes and
reads complete generations around ``fsync`` and rename boundaries.  They are
separate Python processes, so they run concurrently with each other and the
consumer processes even on a single-core guest.  Fixed byte counts and hashes
make QEMU and hardware reports directly comparable; elapsed time is recorded
but is not a pass criterion.

Start with the sizes above under QEMU.  A longer release gate can multiply
the pass counts and select the consumers' larger documented frame/byte
counts.  Avoid time-based infinite pressure: bounded operations make hangs,
loss and premature completion distinguishable.  Migration and reset still
use each consumer's documented quiescent barriers and multi-session mode;
this runner does not claim in-flight migration merely because it creates
contention.

Host-only tests
---------------

The unit suite needs no QEMU image, network interface, CAN device, serial
port or root permission::

  cd tests/guest/at91/linux4sam/stress-runner
  python3 -m unittest -v test_sam9x75_linux4sam_stress.py

It covers strict manifest parsing, rejection of shell strings and duplicate
names, deterministic CPU/memory verification, scratch containment and
cleanup, bounded and symlink-safe log draining, real concurrent process start,
bounded unkillable-process handling, Python interpreter fallback, preflight
gating and end-to-end aggregate runs with all three internal workers.
