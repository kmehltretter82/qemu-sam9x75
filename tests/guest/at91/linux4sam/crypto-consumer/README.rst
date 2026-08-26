.. SPDX-License-Identifier: GPL-2.0-or-later

SAM9X75 Linux Crypto API consumer
================================

``sam9x75_crypto_consumer.py`` is an opt-in, end-to-end Linux4SAM workload
for the modeled SAM9X75 AES, SHA and TDES blocks.  It is a real guest
consumer: Python uses Linux ``AF_ALG``, binds the driver-specific
``atmel-*`` names and therefore traverses the shipped kernel drivers,
interrupt paths and XDMAC channels.  It does not write device registers
directly.

Every result has an independent userspace oracle.  SHA and HMAC are compared
with Python's ``hashlib`` and ``hmac``.  AES and TDES encryption are compared
byte-for-byte with OpenSSL, then decrypted through AF_ALG and compared with
the original input.  Sizes straddle the 55/56-byte and 111/112-byte SHA
padding transitions, cache-page boundaries and DMA thresholds;
scatter/gather requests use deliberately awkward fragments and multiple
requests are in flight concurrently.  The fixture samples interrupts around
each selected family separately.  It requires the direct ``atmel-sha``
completion interrupt to advance independently for SHA and HMAC, and the shared
``at_xdmac`` completion interrupt to advance independently for AES and TDES
DMA.  The three peripheral counters and XDMAC counter are all recorded.  Exact
driver-name binding is the primary proof that software fallback did not
satisfy the gate; the phase-scoped IRQ deltas add data-path evidence without
letting one selected crypto family satisfy another family's interrupt gate.

The deterministic keys are test vectors, printed nowhere and unrelated to
the board's OTP or private-key bus.  The fixture does not access OTPC.

Exact Linux4Microchip image
---------------------------

This AF_ALG fixture is pinned to the Linux4Microchip 2026.04 headless image
and its ``6.18.17-linux4microchip-2026.04`` kernel.  That image provides
Python 3.12.12, OpenSSL, ``af_alg.ko``, ``algif_hash.ko`` and
``algif_skcipher.ko``; its Atmel AES, SHA and TDES drivers are built in.
Newer mainline kernels no longer expose asynchronous crypto implementations
through AF_ALG, so they require a future in-kernel consumer instead of this
fixture.  Boot the normal image or the documented disposable SD overlay.
From an ``init=/bin/sh`` diagnostic boot, mount the kernel filesystems first::

  mount -t proc proc /proc 2>/dev/null || true
  mount -t sysfs sysfs /sys 2>/dev/null || true

Copy the fixture through a disposable USB payload disk or another already
validated transport, then run as root::

  python3 sam9x75_crypto_consumer.py \
      --iterations 4 --workers 3 --max-bytes 65536 \
      --json /tmp/sam9x75-crypto.json

Always put a host-side watchdog around the *whole QEMU invocation*, for
example a 15-minute timeout with a short forced-kill grace period.  A driver
waiting for an asynchronous engine completion can enter an uninterruptible
guest wait, so a timeout implemented only inside the guest is not sufficient::

  timeout --foreground --signal=TERM --kill-after=30s 900s \
      ./run-linux4microchip-crypto-vm

The command emits TAP 6/6 plus a JSON record.  The default matrix covers all
five SHA widths, including both SHA-2 padding transitions, HMAC-SHA256/SHA512,
AES-128 ECB, AES-128/192/256 CBC, AES-256 CTR, and 24-byte three-key TDES
through the driver's ECB/CBC registrations.
Increase ``--iterations`` for a longer queue/DMA stress run; request sizes
and expected bytes remain deterministic.
For diagnosis, ``--engines sha`` or a comma-separated subset of
``sha,hmac,aes,tdes`` isolates one hardware queue without weakening the
default release gate.  ``--skip-empty`` is a narrower diagnostic switch; the
default keeps the zero-length SHA/HMAC cases required by the Crypto API.

The JSON ``gate_profile`` is ``release`` only when all four families are
selected, interrupt checks and empty messages remain enabled, and
``--iterations``, ``--workers`` and ``--max-bytes`` are at least 4, 3 and
65536 respectively.  Every weaker invocation is labeled ``diagnostic``.  The
report also records ``skip_empty`` and ``require_interrupts`` explicitly, plus
the before/after/delta counters for each selected family and the backward-useful
overall counters.

Archive the JSON report, ``/proc/interrupts`` and the QEMU
``-d unimp,guest_errors`` log.  A normal successful run must leave that QEMU
log empty.  ``--no-require-interrupts`` is diagnostic only and is not a
release gate.

Host-only tests
---------------

The normal unit suite checks parsing, request matrices, fragmentation,
deterministic data, the OpenSSL oracle and atomic reports without requiring
the SAM9X75 guest::

  cd tests/guest/at91/linux4sam/crypto-consumer
  python3 -m unittest -v test_sam9x75_crypto_consumer.py

Linux hosts can additionally validate the Python AF_ALG transport against
their own generic kernel crypto drivers.  This does not claim SAM9X75 device
coverage::

  SAM9X75_AFALG_HOST_TEST=1 \
      python3 -m unittest -v test_sam9x75_crypto_consumer.py

Coverage boundary
-----------------

The consumer deliberately starts with skcipher and hash APIs.  GCM/other
AEAD control messages, XTS, the AES/SHA protocol path, CBC-MAC, private-key
bus, tamper/fault injection and migration with an active kernel request need
separate fixtures.  Passing this gate establishes userspace-to-driver
functional coverage and phase-scoped DMA/interrupt evidence; it does not
settle hardware timing or version-register values.
