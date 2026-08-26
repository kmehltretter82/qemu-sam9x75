#!/usr/bin/env python3
"""Linux AF_ALG consumer for the SAM9X75 AES, SHA and TDES engines.

The fixture binds the driver-specific ``atmel-*`` Crypto API names.  It then
compares hardware-backed results with independent userspace oracles and
requires the corresponding interrupt counters to advance.  No key material
outside the deterministic test vectors is used.
"""

# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import concurrent.futures
import dataclasses
import hashlib
import hmac
import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


REPORT_SCHEMA = "sam9x75-crypto-consumer-report-v1"
DEFAULT_PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

HASH_DRIVERS = {
    "sha1": ("atmel-sha1", 20),
    "sha224": ("atmel-sha224", 28),
    "sha256": ("atmel-sha256", 32),
    "sha384": ("atmel-sha384", 48),
    "sha512": ("atmel-sha512", 64),
}
HMAC_DRIVERS = {
    "sha256": ("atmel-hmac-sha256", 32),
    "sha512": ("atmel-hmac-sha512", 64),
}
ENGINE_CHOICES = frozenset(("sha", "hmac", "aes", "tdes"))


class ConsumerError(Exception):
    """The end-to-end crypto gate could not be satisfied."""


@dataclasses.dataclass(frozen=True)
class CipherCase:
    name: str
    driver: str
    openssl_name: str
    key: bytes
    iv_size: int
    block_size: int
    lengths: tuple


@dataclasses.dataclass(frozen=True)
class HashJob:
    algorithm: str
    driver: str
    digest_size: int
    length: int
    data: bytes
    key: bytes = b""


@dataclasses.dataclass(frozen=True)
class CipherJob:
    case: CipherCase
    length: int
    data: bytes
    iv: bytes


def deterministic_bytes(label, length):
    """Return stable non-repeating bytes without using guest entropy."""

    label = label.encode("utf-8") if isinstance(label, str) else bytes(label)
    output = bytearray()
    counter = 0
    while len(output) < length:
        output.extend(hashlib.sha256(
            label + counter.to_bytes(8, "big")
        ).digest())
        counter += 1
    return bytes(output[:length])


def split_chunks(data):
    """Split data at awkward boundaries while keeping the iovec bounded."""

    if not data:
        return (b"",)
    sizes = (1, 7, 31, 64, 257, 1023, 4096)
    chunks = []
    offset = 0
    index = 0
    while offset < len(data) and len(chunks) < 15:
        remaining_slots = 15 - len(chunks)
        remaining = len(data) - offset
        if remaining_slots == 1:
            size = remaining
        else:
            size = min(sizes[index % len(sizes)], remaining)
        chunks.append(data[offset:offset + size])
        offset += size
        index += 1
    return tuple(chunks)


def parse_proc_crypto(text):
    """Parse /proc/crypto into a list while retaining duplicate algorithms."""

    entries = []
    for block in text.split("\n\n"):
        entry = {}
        for line in block.splitlines():
            if ":" not in line:
                continue
            key, value = line.split(":", 1)
            entry[key.strip()] = value.strip()
        if entry:
            entries.append(entry)
    return entries


def registered_drivers(entries):
    return {entry.get("driver", "") for entry in entries}


def parse_interrupts(text, labels):
    """Return summed per-label interrupt counters from /proc/interrupts."""

    values = {label: 0 for label in labels}
    for line in text.splitlines():
        for label in labels:
            if label not in line:
                continue
            left = line.split(label, 1)[0]
            if ":" not in left:
                continue
            fields = left.split(":", 1)[1].split()
            for field in fields:
                if not field.isdecimal():
                    break
                values[label] += int(field)
    return values


def interrupt_delta(before, after):
    return {
        label: after.get(label, 0) - before.get(label, 0)
        for label in sorted(set(before) | set(after))
    }


def recv_exact(sock, length):
    output = bytearray()
    while len(output) < length:
        value = sock.recv(length - len(output))
        if not value:
            raise ConsumerError("AF_ALG returned a truncated result")
        output.extend(value)
    return bytes(output)


def sendmsg_exact(sock, buffers, *, flags=0):
    """Send one AF_ALG request record and reject a short submission."""

    buffers = tuple(buffers)
    expected = sum(len(buffer) for buffer in buffers)
    actual = sock.sendmsg(buffers, [], flags)
    if actual != expected:
        raise ConsumerError(
            "AF_ALG accepted %u of %u request bytes" % (actual, expected)
        )
    return actual


def afalg_hash(driver, data, digest_size, key=b""):
    """Hash one message through a specific Linux Crypto API driver."""

    with socket.socket(socket.AF_ALG, socket.SOCK_SEQPACKET, 0) as transform:
        transform.bind(("hash", driver))
        if key:
            transform.setsockopt(socket.SOL_ALG, socket.ALG_SET_KEY, key)
        operation, _ = transform.accept()
        with operation:
            chunks = split_chunks(data)
            # Unlike sendall(b""), sendmsg([b""]) makes a real zero-length
            # syscall and initializes/finalizes the empty hash request.  A
            # single record also preserves the deliberately fragmented
            # iovec.  Keep AF_ALG operation sockets in blocking mode: Python
            # polling an unused hash socket for POLLOUT can wait forever.
            sendmsg_exact(operation, chunks)
            return recv_exact(operation, digest_size)


def afalg_cipher(driver, key, iv, data, encrypt=True):
    """Run one scatter/gather skcipher request through an exact driver."""

    with socket.socket(socket.AF_ALG, socket.SOCK_SEQPACKET, 0) as transform:
        transform.bind(("skcipher", driver))
        transform.setsockopt(socket.SOL_ALG, socket.ALG_SET_KEY, key)
        operation, _ = transform.accept()
        with operation:
            submitted = operation.sendmsg_afalg(
                list(split_chunks(data)),
                op=(socket.ALG_OP_ENCRYPT if encrypt else
                    socket.ALG_OP_DECRYPT),
                iv=iv,
            )
            if submitted != len(data):
                raise ConsumerError(
                    "AF_ALG accepted %u of %u cipher request bytes" % (
                        submitted, len(data),
                    )
                )
            return recv_exact(operation, len(data))


def openssl_cipher(executable, name, key, iv, data, encrypt=True):
    command = [
        executable, "enc", "-" + name,
        "-K", key.hex(), "-nopad", "-nosalt",
    ]
    if iv:
        command.extend(("-iv", iv.hex()))
    if not encrypt:
        command.append("-d")
    result = subprocess.run(
        command, input=data, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False,
        env={**os.environ, "PATH": os.environ.get("PATH") or DEFAULT_PATH},
    )
    if result.returncode:
        raise ConsumerError(
            "%s failed for %s: %s" % (
                executable, name,
                result.stderr.decode("utf-8", "replace").strip(),
            )
        )
    return result.stdout


def cipher_cases(max_bytes):
    aes_block_lengths = tuple(
        length for length in (16, 32, 64, 4096, 4112, 65536)
        if length <= max_bytes
    )
    aes_stream_lengths = tuple(
        length for length in (1, 15, 16, 17, 4095, 4096, 4097, 65537)
        if length <= max_bytes
    )
    tdes_lengths = tuple(
        length for length in (8, 16, 40, 4096, 4104, 65536)
        if length <= max_bytes
    )
    return (
        CipherCase(
            "aes-128-ecb", "atmel-ecb-aes", "aes-128-ecb",
            deterministic_bytes("aes-128-ecb-key", 16), 0, 16,
            aes_block_lengths,
        ),
        CipherCase(
            "aes-128-cbc", "atmel-cbc-aes", "aes-128-cbc",
            deterministic_bytes("aes-128-cbc-key", 16), 16, 16,
            aes_block_lengths,
        ),
        CipherCase(
            "aes-192-cbc", "atmel-cbc-aes", "aes-192-cbc",
            deterministic_bytes("aes-192-cbc-key", 24), 16, 16,
            aes_block_lengths,
        ),
        CipherCase(
            "aes-256-cbc", "atmel-cbc-aes", "aes-256-cbc",
            deterministic_bytes("aes-256-cbc-key", 32), 16, 16,
            aes_block_lengths,
        ),
        CipherCase(
            "aes-256-ctr", "atmel-ctr-aes", "aes-256-ctr",
            deterministic_bytes("aes-256-ctr-key", 32), 16, 1,
            aes_stream_lengths,
        ),
        CipherCase(
            "tdes-ecb", "atmel-ecb-tdes", "des-ede3-ecb",
            deterministic_bytes("tdes-ecb-key", 24), 0, 8,
            tdes_lengths,
        ),
        CipherCase(
            "tdes-cbc", "atmel-cbc-tdes", "des-ede3-cbc",
            deterministic_bytes("tdes-cbc-key", 24), 8, 8,
            tdes_lengths,
        ),
    )


def build_hash_jobs(max_bytes, keyed=False, include_empty=True):
    algorithms = HMAC_DRIVERS if keyed else HASH_DRIVERS
    lengths = (
        0, 1, 3, 55, 56, 63, 64, 65, 127, 128, 129,
        4095, 4096, 4097, 65536,
    )
    jobs = []
    for algorithm, (driver, digest_size) in algorithms.items():
        key = (deterministic_bytes("hmac-%s-key" % algorithm, 73)
               if keyed else b"")
        for length in lengths:
            if not include_empty and not length:
                continue
            if length > max_bytes:
                continue
            label = "%s-%s-%u" % (
                "hmac" if keyed else "hash", algorithm, length,
            )
            jobs.append(HashJob(
                algorithm, driver, digest_size, length,
                deterministic_bytes(label, length), key,
            ))
    return tuple(jobs)


def build_cipher_jobs(max_bytes, engines=ENGINE_CHOICES):
    jobs = []
    for case in cipher_cases(max_bytes):
        engine = "aes" if case.name.startswith("aes-") else "tdes"
        if engine not in engines:
            continue
        for length in case.lengths:
            jobs.append(CipherJob(
                case, length,
                deterministic_bytes("%s-data-%u" % (case.name, length),
                                    length),
                deterministic_bytes("%s-iv-%u" % (case.name, length),
                                    case.iv_size),
            ))
    return tuple(jobs)


def run_hash_job(job, iterations):
    if job.key:
        expected = hmac.new(job.key, job.data, job.algorithm).digest()
    else:
        expected = hashlib.new(job.algorithm, job.data).digest()
    for _ in range(iterations):
        actual = afalg_hash(
            job.driver, job.data, job.digest_size, job.key,
        )
        if actual != expected:
            raise ConsumerError(
                "%s mismatch at %u bytes: expected %s, got %s" % (
                    job.driver, job.length, expected.hex(), actual.hex(),
                )
            )
    return job.length * iterations


def run_cipher_job(job, iterations, openssl):
    expected = openssl_cipher(
        openssl, job.case.openssl_name, job.case.key, job.iv, job.data,
    )
    if len(expected) != job.length:
        raise ConsumerError(
            "OpenSSL returned %u bytes for %s/%u" % (
                len(expected), job.case.name, job.length,
            )
        )
    for _ in range(iterations):
        encrypted = afalg_cipher(
            job.case.driver, job.case.key, job.iv, job.data, True,
        )
        if encrypted != expected:
            raise ConsumerError(
                "%s encryption mismatch at %u bytes" % (
                    job.case.name, job.length,
                )
            )
        decrypted = afalg_cipher(
            job.case.driver, job.case.key, job.iv, encrypted, False,
        )
        if decrypted != job.data:
            raise ConsumerError(
                "%s decryption mismatch at %u bytes" % (
                    job.case.name, job.length,
                )
            )
    return job.length * iterations * 2


def job_name(job):
    if isinstance(job, HashJob):
        return "%s/%u" % (job.driver, job.length)
    return "%s/%u" % (job.case.name, job.length)


def run_parallel(function, jobs, workers, progress=False):
    total = 0
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=workers) as executor:
        futures = {
            executor.submit(function, job): job for job in jobs
        }
        complete = 0
        for future in concurrent.futures.as_completed(futures):
            total += future.result()
            complete += 1
            if progress:
                print("# %u/%u %s" % (
                    complete, len(jobs), job_name(futures[future]),
                ), flush=True)
    return total


def atomic_write_json(path, value):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(
        prefix=path.name + ".", suffix=".tmp", dir=path.parent,
    )
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, sort_keys=True, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def ensure_afalg_modules(modprobe):
    for module in ("af_alg", "algif_hash", "algif_skcipher"):
        result = subprocess.run(
            [modprobe, module], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, check=False,
            env={**os.environ, "PATH": os.environ.get("PATH") or DEFAULT_PATH},
        )
        if result.returncode:
            raise ConsumerError(
                "cannot load %s: %s" % (
                    module,
                    result.stderr.decode("utf-8", "replace").strip(),
                )
            )


def read_text(path):
    return pathlib.Path(path).read_text(encoding="utf-8")


def required_drivers(max_bytes, engines=ENGINE_CHOICES):
    result = set()
    if "sha" in engines:
        result.update(driver for driver, _size in HASH_DRIVERS.values())
    if "hmac" in engines:
        result.update(driver for driver, _size in HMAC_DRIVERS.values())
    for case in cipher_cases(max_bytes):
        engine = "aes" if case.name.startswith("aes-") else "tdes"
        if engine in engines:
            result.add(case.driver)
    return result


def failed_driver_selftests(entries, required):
    """Return selected exact drivers whose Crypto API self-test failed."""

    return sorted({
        entry.get("driver", "") for entry in entries
        if entry.get("driver", "") in required and
        entry.get("selftest", "passed") != "passed"
    })


def interrupt_labels(engines):
    """Return IRQ labels recorded for the selected hardware engines."""

    labels = []
    if "aes" in engines:
        labels.append("atmel-aes")
    if engines & {"sha", "hmac"}:
        labels.append("atmel-sha")
    if "tdes" in engines:
        labels.append("atmel-tdes")
    labels.append("at_xdmac")
    return tuple(labels)


def required_interrupt_labels(engines):
    """Return IRQ paths that must advance for selected request types."""

    labels = set()
    if engines & {"sha", "hmac"}:
        labels.add("atmel-sha")
    if engines & {"aes", "tdes"}:
        labels.add("at_xdmac")
    return frozenset(labels)


def parse_engines(value):
    engines = frozenset(part.strip() for part in value.split(",") if
                        part.strip())
    unknown = engines - ENGINE_CHOICES
    if not engines:
        raise argparse.ArgumentTypeError("select at least one engine")
    if unknown:
        raise argparse.ArgumentTypeError(
            "unknown engine(s): " + ", ".join(sorted(unknown))
        )
    return engines


def run_consumer(args):
    if not hasattr(socket, "AF_ALG") or not hasattr(socket.socket,
                                                    "sendmsg_afalg"):
        raise ConsumerError("Python was built without Linux AF_ALG support")

    if args.progress:
        print("# loading AF_ALG modules", flush=True)
    ensure_afalg_modules(args.modprobe)
    if args.progress:
        print("# checking exact driver registrations", flush=True)
    crypto_entries = parse_proc_crypto(read_text(args.proc_crypto))
    available = registered_drivers(crypto_entries)
    required = required_drivers(args.max_bytes, args.engines)
    missing = sorted(required - available)
    if missing:
        raise ConsumerError(
            "required hardware Crypto API drivers are missing: " +
            ", ".join(missing)
        )

    failed_selftests = failed_driver_selftests(crypto_entries, required)
    if failed_selftests:
        raise ConsumerError(
            "kernel crypto self-test did not pass: " +
            ", ".join(failed_selftests)
        )

    labels = interrupt_labels(args.engines)
    irq_before = parse_interrupts(read_text(args.proc_interrupts), labels)
    started = time.monotonic()

    hash_jobs = (build_hash_jobs(args.max_bytes, keyed=False,
                                 include_empty=not args.skip_empty)
                 if "sha" in args.engines else ())
    hmac_jobs = (build_hash_jobs(args.max_bytes, keyed=True,
                                 include_empty=not args.skip_empty)
                 if "hmac" in args.engines else ())
    cipher_jobs = build_cipher_jobs(args.max_bytes, args.engines)

    if args.progress and hash_jobs:
        print("# starting SHA vectors", flush=True)
    hash_bytes = run_parallel(
        lambda job: run_hash_job(job, args.iterations),
        hash_jobs, args.workers, args.progress,
    )
    if args.progress and hmac_jobs:
        print("# starting HMAC vectors", flush=True)
    hmac_bytes = run_parallel(
        lambda job: run_hash_job(job, args.iterations),
        hmac_jobs, args.workers, args.progress,
    )
    if args.progress and cipher_jobs:
        print("# starting AES/TDES vectors", flush=True)
    cipher_bytes = run_parallel(
        lambda job: run_cipher_job(job, args.iterations, args.openssl),
        cipher_jobs, args.workers, args.progress,
    )

    irq_after = parse_interrupts(read_text(args.proc_interrupts), labels)
    irq_deltas = interrupt_delta(irq_before, irq_after)
    missing_irq_paths = sorted(
        label for label in required_interrupt_labels(args.engines)
        if irq_deltas[label] <= 0
    )
    if args.require_interrupts and missing_irq_paths:
        raise ConsumerError(
            "hardware requests completed without required interrupt(s): " +
            ", ".join(missing_irq_paths)
        )

    openssl_version = subprocess.run(
        [args.openssl, "version"], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False,
        env={**os.environ, "PATH": os.environ.get("PATH") or DEFAULT_PATH},
        text=True,
    ).stdout.strip()
    report = {
        "schema": REPORT_SCHEMA,
        "success": True,
        "uname": " ".join(os.uname()),
        "openssl": openssl_version,
        "iterations": args.iterations,
        "workers": args.workers,
        "engines": sorted(args.engines),
        "max_bytes": args.max_bytes,
        "elapsed_seconds": round(time.monotonic() - started, 6),
        "jobs": {
            "hash": len(hash_jobs),
            "hmac": len(hmac_jobs),
            "cipher": len(cipher_jobs),
        },
        "bytes": {
            "hash": hash_bytes,
            "hmac": hmac_bytes,
            "cipher_encrypt_decrypt": cipher_bytes,
        },
        "drivers": sorted(required),
        "required_interrupts": sorted(
            required_interrupt_labels(args.engines)
        ),
        "interrupts_before": irq_before,
        "interrupts_after": irq_after,
        "interrupts_delta": irq_deltas,
    }
    if args.json:
        atomic_write_json(args.json, report)

    print("1..6")
    print("ok 1 - exact atmel Crypto API drivers registered")
    print("ok 2 - SHA digests match hashlib at selected request sizes" +
          ("" if "sha" in args.engines else " # SKIP not selected"))
    print("ok 3 - HMAC digests match Python hmac" +
          ("" if "hmac" in args.engines else " # SKIP not selected"))
    print("ok 4 - AES encryption matches OpenSSL and decrypts" +
          ("" if "aes" in args.engines else " # SKIP not selected"))
    print("ok 5 - TDES encryption matches OpenSSL and decrypts" +
          ("" if "tdes" in args.engines else " # SKIP not selected"))
    if args.require_interrupts:
        print("ok 6 - selected hardware completion interrupts advanced")
    else:
        print("ok 6 - interrupt counters recorded # SKIP not required")
    print(json.dumps(report, sort_keys=True))
    return 0


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iterations", type=int, default=4,
                        help="hardware repetitions of every oracle vector")
    parser.add_argument("--workers", type=int, default=3,
                        help="concurrent AF_ALG requests")
    parser.add_argument("--max-bytes", type=int, default=65536,
                        help="largest request size in bytes")
    parser.add_argument(
        "--engines", type=parse_engines, default=ENGINE_CHOICES,
        help="comma-separated subset of sha,hmac,aes,tdes",
    )
    parser.add_argument("--proc-crypto", default="/proc/crypto")
    parser.add_argument("--proc-interrupts", default="/proc/interrupts")
    parser.add_argument("--modprobe", default="modprobe")
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--json", help="atomically replace this JSON report")
    parser.add_argument("--progress", action="store_true",
                        help="print each completed oracle vector")
    parser.add_argument(
        "--skip-empty", action="store_true",
        help="diagnostic isolation only: omit zero-length hash messages",
    )
    parser.add_argument(
        "--no-require-interrupts", dest="require_interrupts",
        action="store_false",
        help="record but do not require peripheral interrupt deltas",
    )
    parser.set_defaults(require_interrupts=True)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.iterations < 1:
        raise SystemExit("--iterations must be positive")
    if args.workers < 1:
        raise SystemExit("--workers must be positive")
    if args.max_bytes < 16:
        raise SystemExit("--max-bytes must be at least 16")
    try:
        return run_consumer(args)
    except (ConsumerError, OSError, subprocess.SubprocessError) as error:
        print("Bail out! " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
