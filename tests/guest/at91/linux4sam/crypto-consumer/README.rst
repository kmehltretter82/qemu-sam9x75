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
padding transitions, cache-page boundaries and DMA thresholds.  Hash and
cipher submissions use deliberately awkward, bounded userspace iovecs.  AF_ALG
copies these transmit iovecs into its own page-backed scatterlist, so they test
the gather syscall path without pretending to control the device's DMA segment
layout.  Cipher output is received into anonymous page-aligned storage because
AF_ALG pins that userspace mapping directly for its destination scatterlist.
Bounded AES-CTR totals through 4097 bytes exercise the driver's alignment
bounce path; the large 65536-byte vector is block-aligned and exercises
multi-page DMA without exceeding that fixed bounce allocation.  Multiple
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

On a kernel with both Linux fixes described below, the command emits TAP
6/6 plus a JSON record.  The default matrix covers all five SHA widths,
including both SHA-2 padding transitions, HMAC-SHA256/SHA512, AES-128 ECB,
AES-128/192/256 CBC, AES-256 CTR, and 24-byte three-key TDES through the
driver's ECB/CBC registrations.
Increase ``--iterations`` for a longer queue/DMA stress run; request sizes
and expected bytes remain deterministic.
For diagnosis, ``--engines sha`` or a comma-separated subset of
``sha,hmac,aes,tdes`` isolates one hardware queue without weakening the
default release gate.  ``--skip-empty`` is a narrower diagnostic switch; the
default keeps the zero-length SHA/HMAC cases required by the Crypto API.
An intentionally unaligned 64-KiB cipher destination is not a valid release
stress case: it can return ``ENOMEM`` because the Linux Atmel driver's fixed
alignment bounce allocation is smaller than the request.  That result is a
Linux buffer limit, not evidence of a QEMU DMA hang.

Linux4Microchip atmel-sha concurrency bug
-----------------------------------------

The unmodified Linux4Microchip 2026.04 kernel can deadlock this fixture when
two or more HMAC requests interleave.  A single worker completes, while the
exact two-worker 64-KiB matrix reproducibly leaves both AF_ALG callers in
``crypto_wait_req``.  At the stall the SHA engine is idle with ``WRDY`` set,
DMA inactive, automatic padding still enabled, a stale nonzero ``SHA_MSR``,
and ``SHA_BCR`` zero.

This is a Linux ``drivers/crypto/atmel-sha.c`` state-restoration bug, not a
condition QEMU should work around.  The data sheet requires software-padded
requests to disable automatic padding by clearing both message-size and byte
count.  On HMAC-capable parts, ``atmel_sha_write_ctrl()`` must write
``SHA_MSR = 0`` followed by ``SHA_BCR = 0`` immediately before its final
``SHA_MR`` write.  The writes apply to every generic SHA request because an
ordinary SHA request may follow HMAC state, but must be guarded by
``dd->caps.has_hmac`` for older revisions where those offsets are reserved.

Do not reduce ``--workers`` in a release result.  ``--workers 1`` is useful
only to isolate the QEMU data path while the kernel fix is being reviewed.
The full concurrent gate remains intentionally capable of detecting the
driver bug.  The proposed fix passed the complete release profile in QEMU and, on
2026-08-28, on a physical SAM9X75 Curiosity: the packaged fixed kernel ran the
release profile with three workers, four iterations and requests through
65,536 bytes, TAP 6/6, with the concurrent HMAC path (``ok 3``) exercising
exactly the state-restoration case.  The stock kernel on the same board wedged
the first TDES request on two of two cold boots (task in ``D`` state at
``skcipher_recvmsg``, unkillable, zero TDES completions, no XDMAC advance, no
kernel message) while AES and SHA completed normally, and the fixed kernel
completed the same request in about a millisecond with the reference
ciphertext.  Both fixes are therefore silicon-confirmed and ready for
upstream submission.

Linux atmel-tdes uninitialized device state
--------------------------------------------

The unmodified Linux4Microchip 2026.04 TDES driver allocates
``struct atmel_tdes_dev`` with ``devm_kmalloc()``.  It then reads
``dd->flags`` during probe and relies on zero defaults in the embedded DMA
configuration.  The allocation is not initialized.  On a captured failing
boot, ``dd->flags`` began as ``0x4f79c83c``, which already contained
``TDES_FLAGS_BUSY``.  Two valid AF_ALG requests were queued with
``-EINPROGRESS`` forever; neither the TDES engine nor XDMAC channels 7/8 were
started.  The TDES mode register stayed at reset value ``0x00000002`` and the
XDMAC interrupt count did not advance.

This is an upstream Linux bug, not a QEMU request-line or DMA bug.  It dates
to commit ``7608a43d8f2e`` (``crypto: atmel-tdes - Switch to managed version
of kzalloc``): the stated conversion to zeroed managed allocation accidentally
used ``devm_kmalloc()``.  Linux 7.2 still has the same allocation.  Change it
to ``devm_kzalloc()``, matching the Atmel AES and SHA drivers.

With that one-line fix and the SHA fix above, the formerly failing two-worker
TDES test passed all 12 ECB/CBC jobs, including 65,536-byte requests, with
exact OpenSSL and decrypt-round-trip equality and 88 XDMAC completions.  The
complete release profile then passed all four engine families with four
iterations and three workers in 77.874097 seconds.  It covered 85 SHA, 34
HMAC, 32 AES and 12 TDES jobs; XDMAC advanced by 8,808 and the SHA interrupt
by 10,034; the QEMU ``unimp,guest_errors`` log remained empty.

The fix needs a repeatability run on physical SAM9X75 silicon.  Because the
failure depends on allocator contents, use at least 20 cold boots and retain
two or more workers; a single passing unpatched boot does not disprove it.

The JSON ``gate_profile`` is ``release`` only when all four families are
selected, interrupt checks and empty messages remain enabled, and
``--iterations``, ``--workers`` and ``--max-bytes`` are at least 4, 3 and
65536 respectively.  Every weaker invocation is labeled ``diagnostic``.  The
report also records ``skip_empty`` and ``require_interrupts`` explicitly,
plus the before/after/delta counters for each selected family and the
backward-useful overall counters.

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
