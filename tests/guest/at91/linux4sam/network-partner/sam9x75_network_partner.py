#!/usr/bin/env python3
"""Host network peer and Linux4SAM exerciser for SAM9X75 GEM/LAN8840.

The peer binds only host TCP/UDP sockets.  The guest connects through QEMU's
user-mode network, normally using the 10.0.2.2 host alias.  No Internet
service is contacted by this fixture.
"""

# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import dataclasses
import enum
import hashlib
import json
import os
import pathlib
import queue
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib


MAGIC = b"S9N1"
VERSION = 1
MAX_PAYLOAD = 256 * 1024
HEADER_NO_CRC = struct.Struct("!4sBBBBQII")
HEADER = struct.Struct("!4sBBBBQIII")
HELLO_PAYLOAD = struct.Struct("!QI")
DONE_PAYLOAD = struct.Struct("!QI")
TCP_CHUNK_SIZES = (
    1, 2, 3, 7, 8, 31, 32, 63, 64, 127, 128, 255, 256, 511, 512,
    1023, 1024, 1460, 4095, 4096, 16383, 16384, 32767, 65535,
)
UDP_PAYLOAD_SIZES = (0, 1, 7, 31, 63, 64, 127, 255, 511, 1024, 1400)
DEFAULT_FRAGMENTS = (1, 3, 17, 257, 4096)
DEFAULT_PATH = "/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin"


class Kind(enum.IntEnum):
    HELLO = 1
    HELLO_ACK = 2
    DATA = 3
    DONE = 4
    UDP_DATA = 5
    UDP_DONE = 6
    UDP_DONE_ACK = 7


class Direction(enum.IntEnum):
    GUEST_TO_PEER = 1
    PEER_TO_GUEST = 2


class ProtocolError(Exception):
    """The remote endpoint violated the test protocol."""


@dataclasses.dataclass(frozen=True)
class Frame:
    kind: Kind
    direction: Direction
    stream: int
    session: int
    sequence: int
    payload: bytes = b""

    def encode(self):
        if not 0 <= self.stream <= 255:
            raise ValueError("stream is not a uint8")
        if not 0 < self.session <= 0xffffffffffffffff:
            raise ValueError("session must be a nonzero uint64")
        if not 0 <= self.sequence <= 0xffffffff:
            raise ValueError("sequence is not a uint32")
        if len(self.payload) > MAX_PAYLOAD:
            raise ValueError("payload exceeds protocol maximum")
        prefix = HEADER_NO_CRC.pack(
            MAGIC, VERSION, int(self.kind), int(self.direction), self.stream,
            self.session, self.sequence, len(self.payload),
        )
        crc = zlib.crc32(prefix)
        crc = zlib.crc32(self.payload, crc) & 0xffffffff
        return HEADER.pack(
            MAGIC, VERSION, int(self.kind), int(self.direction), self.stream,
            self.session, self.sequence, len(self.payload), crc,
        ) + self.payload

    @classmethod
    def decode(cls, data):
        if len(data) < HEADER.size:
            raise ProtocolError("short frame header")
        values = HEADER.unpack(data[:HEADER.size])
        (magic, version, kind, direction, stream, session, sequence,
         length, expected_crc) = values
        if magic != MAGIC:
            raise ProtocolError("bad frame magic")
        if version != VERSION:
            raise ProtocolError("unsupported protocol version")
        if kind not in Kind._value2member_map_:
            raise ProtocolError("unknown frame kind")
        if direction not in Direction._value2member_map_:
            raise ProtocolError("unknown frame direction")
        if session == 0:
            raise ProtocolError("zero session identifier")
        if length > MAX_PAYLOAD:
            raise ProtocolError("payload exceeds protocol maximum")
        if len(data) != HEADER.size + length:
            raise ProtocolError("frame length mismatch")
        payload = data[HEADER.size:]
        prefix = HEADER_NO_CRC.pack(
            magic, version, kind, direction, stream, session, sequence,
            length,
        )
        actual_crc = zlib.crc32(prefix)
        actual_crc = zlib.crc32(payload, actual_crc) & 0xffffffff
        if actual_crc != expected_crc:
            raise ProtocolError("frame CRC mismatch")
        return cls(
            Kind(kind), Direction(direction), stream, session, sequence,
            payload,
        )


def deterministic_payload(session, direction, stream, sequence, length):
    """Generate stable, incompressible-looking payload bytes."""

    seed = struct.pack("!QBBI", session, int(direction), stream, sequence)
    result = bytearray()
    block = 0
    while len(result) < length:
        result.extend(
            hashlib.sha256(seed + struct.pack("!I", block)).digest(),
        )
        block += 1
    return bytes(result[:length])


def iter_lengths(total, sizes):
    remaining = total
    sequence = 0
    while remaining:
        length = min(sizes[sequence % len(sizes)], remaining)
        yield sequence, length
        sequence += 1
        remaining -= length


def parse_fragment_pattern(value):
    try:
        result = tuple(int(item, 0) for item in value.split(",") if item)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    if not result or any(item <= 0 for item in result):
        raise argparse.ArgumentTypeError("fragment sizes must be positive")
    return result


def atomic_json(path, value):
    """Write JSON through a same-directory temporary file and rename."""

    if path is None:
        return
    target = pathlib.Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(
        prefix=target.name + ".", suffix=".tmp", dir=target.parent,
    )
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, target)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def invalidate_ready_file(path):
    """Remove readiness left by this or an interrupted earlier invocation."""

    if path is None:
        return
    try:
        pathlib.Path(path).unlink()
    except FileNotFoundError:
        pass


def remaining_timeout(deadline, maximum=None):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("network fixture deadline expired")
    if maximum is not None:
        return min(remaining, maximum)
    return remaining


def recv_exact(sock, length, deadline):
    result = bytearray()
    while len(result) < length:
        sock.settimeout(remaining_timeout(deadline, 30.0))
        data = sock.recv(length - len(result))
        if not data:
            raise EOFError("connection closed during a frame")
        result.extend(data)
    return bytes(result)


def recv_frame(sock, deadline):
    header = recv_exact(sock, HEADER.size, deadline)
    length = HEADER.unpack(header)[7]
    if length > MAX_PAYLOAD:
        raise ProtocolError("payload exceeds protocol maximum")
    return Frame.decode(header + recv_exact(sock, length, deadline))


def send_fragmented(sock, data, fragments):
    offset = 0
    fragment = 0
    while offset < len(data):
        length = fragments[fragment % len(fragments)]
        sock.sendall(data[offset:offset + length])
        offset += length
        fragment += 1


def validate_frame(frame, kind, direction, stream, session, sequence=None):
    if frame.kind != kind:
        raise ProtocolError(
            "expected %s, received %s" % (kind.name, frame.kind.name),
        )
    if frame.direction != direction:
        raise ProtocolError("unexpected frame direction")
    if frame.stream != stream or frame.session != session:
        raise ProtocolError("frame stream/session mismatch")
    if sequence is not None and frame.sequence != sequence:
        raise ProtocolError(
            "expected sequence %d, received %d" %
            (sequence, frame.sequence),
        )


def send_tcp_data(sock, session, stream, direction, total, fragments,
                  deadline):
    started = time.monotonic()
    frames = 0
    for sequence, length in iter_lengths(total, TCP_CHUNK_SIZES):
        remaining_timeout(deadline)
        payload = deterministic_payload(
            session, direction, stream, sequence, length,
        )
        send_fragmented(sock, Frame(
            Kind.DATA, direction, stream, session, sequence, payload,
        ).encode(), fragments)
        frames += 1
    send_fragmented(sock, Frame(
        Kind.DONE, direction, stream, session, frames,
        DONE_PAYLOAD.pack(total, frames),
    ).encode(), fragments)
    return {
        "bytes": total,
        "frames": frames,
        "seconds": time.monotonic() - started,
    }


def receive_tcp_data(sock, session, stream, direction, total, deadline):
    started = time.monotonic()
    frames = 0
    for sequence, length in iter_lengths(total, TCP_CHUNK_SIZES):
        frame = recv_frame(sock, deadline)
        validate_frame(
            frame, Kind.DATA, direction, stream, session, sequence,
        )
        expected = deterministic_payload(
            session, direction, stream, sequence, length,
        )
        if frame.payload != expected:
            raise ProtocolError(
                "payload mismatch on stream %d sequence %d" %
                (stream, sequence),
            )
        frames += 1
    done = recv_frame(sock, deadline)
    validate_frame(done, Kind.DONE, direction, stream, session, frames)
    if done.payload != DONE_PAYLOAD.pack(total, frames):
        raise ProtocolError("DONE counters mismatch")
    return {
        "bytes": total,
        "frames": frames,
        "seconds": time.monotonic() - started,
    }


def run_duplex(sock, session, stream, send_direction, receive_direction,
               total, fragments, deadline):
    sender_result = queue.Queue()

    def sender():
        try:
            sender_result.put((True, send_tcp_data(
                sock, session, stream, send_direction, total, fragments,
                deadline,
            )))
        except BaseException as exc:  # Propagate from the worker thread.
            sender_result.put((False, exc))
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    thread = threading.Thread(target=sender, daemon=True)
    thread.start()
    try:
        received = receive_tcp_data(
            sock, session, stream, receive_direction, total, deadline,
        )
    except BaseException as receive_error:
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        thread.join(timeout=2)
        if not sender_result.empty():
            ok, sent = sender_result.get_nowait()
            if not ok:
                raise sent
        raise receive_error
    thread.join(remaining_timeout(deadline))
    if thread.is_alive():
        raise TimeoutError("TCP sender did not stop")
    ok, sent = sender_result.get_nowait()
    if not ok:
        raise sent
    return {"sent": sent, "received": received}


def server_tcp_connection(sock, hello, total, streams, fragments, gate,
                          deadline):
    stream = hello.stream
    validate_frame(
        hello, Kind.HELLO, Direction.GUEST_TO_PEER, stream, hello.session, 0,
    )
    if hello.payload != HELLO_PAYLOAD.pack(total, streams):
        raise ProtocolError("HELLO parameters mismatch")
    sock.sendall(Frame(
        Kind.HELLO_ACK, Direction.PEER_TO_GUEST, stream, hello.session, 0,
        hello.payload,
    ).encode())
    gate.wait(remaining_timeout(deadline))
    result = run_duplex(
        sock, hello.session, stream, Direction.PEER_TO_GUEST,
        Direction.GUEST_TO_PEER, total, fragments, deadline,
    )
    result["stream"] = stream
    return result


def client_tcp_connection(host, port, session, stream, total, streams,
                          fragments, gate, deadline, source=None):
    sock = socket.create_connection(
        (host, port), timeout=remaining_timeout(deadline, 10.0),
        source_address=(source, 0) if source else None,
    )
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        hello_payload = HELLO_PAYLOAD.pack(total, streams)
        sock.sendall(Frame(
            Kind.HELLO, Direction.GUEST_TO_PEER, stream, session, 0,
            hello_payload,
        ).encode())
        reply = recv_frame(sock, deadline)
        validate_frame(
            reply, Kind.HELLO_ACK, Direction.PEER_TO_GUEST, stream,
            session, 0,
        )
        if reply.payload != hello_payload:
            raise ProtocolError("HELLO_ACK parameters mismatch")
        gate.wait(remaining_timeout(deadline))
        result = run_duplex(
            sock, session, stream, Direction.GUEST_TO_PEER,
            Direction.PEER_TO_GUEST, total, fragments, deadline,
        )
        result["stream"] = stream
        return result
    finally:
        sock.close()


def run_tcp_client(host, port, session, streams, total, fragments, deadline,
                   source=None):
    gate = threading.Barrier(streams)
    results = queue.Queue()

    def worker(stream):
        try:
            value = client_tcp_connection(
                host, port, session, stream, total, streams, fragments, gate,
                deadline, source,
            )
            results.put((stream, True, value))
        except BaseException as exc:
            try:
                gate.abort()
            except threading.BrokenBarrierError:
                pass
            results.put((stream, False, exc))

    threads = [
        threading.Thread(target=worker, args=(stream,), daemon=True)
        for stream in range(streams)
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(remaining_timeout(deadline))
    if any(thread.is_alive() for thread in threads):
        raise TimeoutError("TCP client workers did not stop")
    values = []
    failures = []
    while not results.empty():
        stream, ok, value = results.get_nowait()
        if ok:
            values.append(value)
        else:
            failures.append((stream, value))
    if failures:
        stream, failure = sorted(failures, key=lambda item: item[0])[0]
        raise RuntimeError("TCP stream %d failed: %s" % (stream, failure))
    return sorted(values, key=lambda item: item["stream"])


def run_tcp_server(listener, streams, total, fragments, deadline):
    gate = threading.Barrier(streams + 1)
    results = queue.Queue()
    workers = []
    session = None
    seen_streams = set()

    def worker(sock, hello):
        try:
            value = server_tcp_connection(
                sock, hello, total, streams, fragments, gate, deadline,
            )
            results.put((hello.stream, True, value))
        except BaseException as exc:
            try:
                gate.abort()
            except threading.BrokenBarrierError:
                pass
            results.put((hello.stream, False, exc))
        finally:
            sock.close()

    while len(workers) < streams:
        listener.settimeout(remaining_timeout(deadline))
        sock, _address = listener.accept()
        try:
            hello = recv_frame(sock, deadline)
            if hello.kind != Kind.HELLO:
                raise ProtocolError("first TCP frame is not HELLO")
            if session is None:
                session = hello.session
            elif hello.session != session:
                raise ProtocolError("TCP streams use different sessions")
            if hello.stream >= streams or hello.stream in seen_streams:
                raise ProtocolError("invalid or duplicate TCP stream")
            seen_streams.add(hello.stream)
        except BaseException:
            sock.close()
            raise
        thread = threading.Thread(
            target=worker, args=(sock, hello), daemon=True,
        )
        workers.append(thread)
        thread.start()

    gate.wait(remaining_timeout(deadline))
    for thread in workers:
        thread.join(remaining_timeout(deadline))
    if any(thread.is_alive() for thread in workers):
        raise TimeoutError("TCP server workers did not stop")
    values = []
    failures = []
    while not results.empty():
        stream, ok, value = results.get_nowait()
        if ok:
            values.append(value)
        else:
            failures.append((stream, value))
    if failures:
        stream, failure = sorted(failures, key=lambda item: item[0])[0]
        raise RuntimeError("TCP stream %d failed: %s" % (stream, failure))
    return session, sorted(values, key=lambda item: item["stream"])


def udp_payload_length(sequence):
    return UDP_PAYLOAD_SIZES[sequence % len(UDP_PAYLOAD_SIZES)]


def run_udp_client(host, port, session, packets, timeout, retries, deadline,
                   source=None):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if source:
        sock.bind((source, 0))
    sock.connect((host, port))
    started = time.monotonic()
    retry_count = 0
    stale_responses = 0
    try:
        for sequence in range(packets):
            length = udp_payload_length(sequence)
            request = Frame(
                Kind.UDP_DATA, Direction.GUEST_TO_PEER, 0, session,
                sequence, deterministic_payload(
                    session, Direction.GUEST_TO_PEER, 0, sequence, length,
                ),
            ).encode()
            for attempt in range(retries + 1):
                received_current = False
                sock.send(request)
                attempt_deadline = min(
                    deadline, time.monotonic() + timeout,
                )
                while True:
                    try:
                        sock.settimeout(remaining_timeout(attempt_deadline))
                        response = Frame.decode(
                            sock.recv(MAX_PAYLOAD + HEADER.size),
                        )
                    except (socket.timeout, TimeoutError):
                        if attempt == retries:
                            raise TimeoutError(
                                "UDP sequence %d timed out" % sequence,
                            )
                        retry_count += 1
                        break
                    if (response.session == session and
                            response.kind == Kind.UDP_DATA and
                            response.direction == Direction.PEER_TO_GUEST and
                            response.stream == 0 and
                            response.sequence < sequence):
                        stale_responses += 1
                        continue
                    validate_frame(
                        response, Kind.UDP_DATA, Direction.PEER_TO_GUEST, 0,
                        session, sequence,
                    )
                    expected = deterministic_payload(
                        session, Direction.PEER_TO_GUEST, 0, sequence, length,
                    )
                    if response.payload != expected:
                        raise ProtocolError(
                            "UDP payload mismatch at sequence %d" % sequence,
                        )
                    received_current = True
                    break
                if received_current:
                    break
        done = Frame(
            Kind.UDP_DONE, Direction.GUEST_TO_PEER, 0, session, packets,
            DONE_PAYLOAD.pack(sum(
                udp_payload_length(i) for i in range(packets)
            ), packets),
        ).encode()
        for attempt in range(retries + 1):
            received_done = False
            sock.send(done)
            attempt_deadline = min(deadline, time.monotonic() + timeout)
            while True:
                try:
                    sock.settimeout(remaining_timeout(attempt_deadline))
                    response = Frame.decode(
                        sock.recv(MAX_PAYLOAD + HEADER.size),
                    )
                except (socket.timeout, TimeoutError):
                    if attempt == retries:
                        raise TimeoutError("UDP DONE timed out")
                    retry_count += 1
                    break
                if (response.session == session and
                        response.kind == Kind.UDP_DATA and
                        response.direction == Direction.PEER_TO_GUEST and
                        response.stream == 0 and
                        response.sequence < packets):
                    stale_responses += 1
                    continue
                validate_frame(
                    response, Kind.UDP_DONE, Direction.PEER_TO_GUEST, 0,
                    session, packets,
                )
                if response.payload != done[HEADER.size:]:
                    raise ProtocolError("UDP DONE counters mismatch")
                acknowledgement = Frame(
                    Kind.UDP_DONE_ACK, Direction.GUEST_TO_PEER, 0,
                    session, packets, response.payload,
                ).encode()
                # Redundant final acknowledgements make a lost UDP datagram
                # very unlikely to leave the peer waiting after guest success.
                for _unused in range(3):
                    sock.send(acknowledgement)
                received_done = True
                break
            if received_done:
                break
    finally:
        sock.close()
    return {
        "packets": packets,
        "payload_bytes_each_direction": sum(
            udp_payload_length(i) for i in range(packets)
        ),
        "retries": retry_count,
        "stale_responses": stale_responses,
        "seconds": time.monotonic() - started,
    }


def run_udp_server(sock, session, packets, deadline):
    started = time.monotonic()
    seen = set()
    duplicates = 0
    done_requests = 0
    payload_bytes = sum(udp_payload_length(i) for i in range(packets))
    while True:
        sock.settimeout(remaining_timeout(deadline))
        data, address = sock.recvfrom(MAX_PAYLOAD + HEADER.size)
        frame = Frame.decode(data)
        if frame.session != session:
            continue
        if frame.kind == Kind.UDP_DATA:
            validate_frame(
                frame, Kind.UDP_DATA, Direction.GUEST_TO_PEER, 0, session,
            )
            if frame.sequence >= packets:
                raise ProtocolError("UDP sequence outside negotiated range")
            length = udp_payload_length(frame.sequence)
            expected = deterministic_payload(
                session, Direction.GUEST_TO_PEER, 0, frame.sequence, length,
            )
            if frame.payload != expected:
                raise ProtocolError(
                    "UDP payload mismatch at sequence %d" % frame.sequence,
                )
            if frame.sequence in seen:
                duplicates += 1
            seen.add(frame.sequence)
            response = Frame(
                Kind.UDP_DATA, Direction.PEER_TO_GUEST, 0, session,
                frame.sequence, deterministic_payload(
                    session, Direction.PEER_TO_GUEST, 0, frame.sequence,
                    length,
                ),
            ).encode()
            sock.sendto(response, address)
            continue
        if frame.kind == Kind.UDP_DONE:
            validate_frame(
                frame, Kind.UDP_DONE, Direction.GUEST_TO_PEER, 0, session,
                packets,
            )
            if len(seen) != packets:
                raise ProtocolError(
                    "UDP DONE arrived with %d of %d packets" %
                    (len(seen), packets),
                )
            expected_done = DONE_PAYLOAD.pack(payload_bytes, packets)
            if frame.payload != expected_done:
                raise ProtocolError("UDP DONE counters mismatch")
            done_requests += 1
            sock.sendto(Frame(
                Kind.UDP_DONE, Direction.PEER_TO_GUEST, 0, session, packets,
                expected_done,
            ).encode(), address)
            continue
        if frame.kind == Kind.UDP_DONE_ACK:
            validate_frame(
                frame, Kind.UDP_DONE_ACK, Direction.GUEST_TO_PEER, 0,
                session, packets,
            )
            if not done_requests:
                raise ProtocolError("UDP DONE acknowledgement arrived early")
            if frame.payload != expected_done:
                raise ProtocolError("UDP DONE acknowledgement mismatch")
            break
        raise ProtocolError("unexpected UDP frame kind")
    return {
        "packets": packets,
        "payload_bytes_each_direction": payload_bytes,
        "duplicates": duplicates,
        "done_requests": done_requests,
        "seconds": time.monotonic() - started,
    }


class TapReporter:
    def __init__(self):
        self.tests = []
        print("TAP version 13", flush=True)

    def record(self, name, ok, details=None):
        number = len(self.tests) + 1
        entry = {"name": name, "ok": bool(ok)}
        if details is not None:
            entry["details"] = details
        self.tests.append(entry)
        status = "ok" if ok else "not ok"
        print("%s %d - %s" % (status, number, name), flush=True)
        if not ok and details:
            for line in str(details).splitlines():
                print("  # %s" % line, flush=True)

    def finish(self):
        print("1..%d" % len(self.tests), flush=True)


def run_command(arguments, timeout=30, check=True):
    environment = dict(os.environ, LC_ALL="C")
    environment.setdefault("PATH", DEFAULT_PATH)
    result = subprocess.run(
        arguments, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace", timeout=timeout,
        env=environment,
    )
    value = {
        "argv": arguments,
        "returncode": result.returncode,
        "output": result.stdout,
    }
    if check and result.returncode:
        raise RuntimeError(
            "%s exited %d:\n%s" %
            (" ".join(arguments), result.returncode, result.stdout),
        )
    return value


def command_path(name):
    return shutil.which(name, path=os.environ.get("PATH", DEFAULT_PATH))


def userspace_inventory():
    commands = {
        name: command_path(name)
        for name in (
            "ip", "ping", "ethtool", "iperf3", "nc", "python3",
            "tftp", "udhcpc",
        )
    }
    missing = [name for name in ("ip", "ping", "ethtool", "python3")
               if not commands[name]]
    if missing:
        raise RuntimeError("missing required tools: " + ", ".join(missing))
    versions = {}
    probes = {
        "ip": [commands["ip"], "-Version"],
        "ping": [commands["ping"], "-V"],
        "ethtool": [commands["ethtool"], "--version"],
        "iperf3": [commands["iperf3"], "--version"]
        if commands["iperf3"] else None,
        "python3": [commands["python3"], "--version"],
    }
    for name, arguments in probes.items():
        if arguments:
            versions[name] = run_command(arguments, check=False)
    return {"commands": commands, "versions": versions}


def configure_dhcp(interface, timeout):
    run_command(["ip", "link", "set", "dev", interface, "up"])
    existing = ipv4_addresses(interface)
    if existing:
        return {"already_configured": True, "addresses": existing}
    udhcpc = command_path("udhcpc")
    if not udhcpc:
        raise RuntimeError("udhcpc is required for --configure-dhcp")
    result = run_command([
        udhcpc, "-i", interface, "-n", "-q", "-t", "5", "-T", "2",
    ], timeout=timeout)
    return {
        "already_configured": False,
        "addresses": ipv4_addresses(interface),
        "command": result,
    }


def ipv4_addresses(interface):
    result = run_command([
        "ip", "-j", "-4", "address", "show", "dev", interface,
    ])
    decoded = json.loads(result["output"])
    addresses = []
    for link in decoded:
        for address in link.get("addr_info", []):
            if address.get("family") == "inet":
                addresses.append({
                    key: address[key]
                    for key in ("local", "prefixlen", "scope", "dynamic")
                    if key in address
                })
    return addresses


def require_ipv4_address(interface):
    addresses = ipv4_addresses(interface)
    if not addresses:
        raise RuntimeError("interface has no IPv4 address")
    return addresses


def require_ethtool_link(interface):
    settings = run_command(["ethtool", interface])
    if "Link detected: yes" not in settings["output"]:
        raise RuntimeError("ethtool did not report an active link")
    driver = run_command(["ethtool", "-i", interface])
    if "driver: macb" not in driver["output"]:
        raise RuntimeError("ethtool did not report the macb driver")
    return {"settings": settings, "driver": driver}


def require_route(interface, host, addresses):
    result = run_command(["ip", "route", "get", host])
    tokens = result["output"].split()
    try:
        device = tokens[tokens.index("dev") + 1]
        source = tokens[tokens.index("src") + 1]
    except (ValueError, IndexError) as exc:
        raise RuntimeError(
            "route to peer lacks dev/src: %s" % result["output"],
        ) from exc
    if device != interface:
        raise RuntimeError("route to peer does not use %s" % interface)
    local_addresses = {address["local"] for address in addresses}
    if source not in local_addresses:
        raise RuntimeError(
            "route source %s is not assigned to %s" % (source, interface),
        )
    return {"command": result, "source": source}


def wait_for_link(interface, timeout):
    carrier_path = pathlib.Path("/sys/class/net") / interface / "carrier"
    operstate_path = pathlib.Path("/sys/class/net") / interface / "operstate"
    deadline = time.monotonic() + timeout
    last = {}
    while time.monotonic() < deadline:
        try:
            last = {
                "carrier": carrier_path.read_text(encoding="ascii").strip(),
                "operstate": operstate_path.read_text(
                    encoding="ascii",
                ).strip(),
            }
            if last["carrier"] == "1":
                return last
        except FileNotFoundError:
            last = {"error": "interface is absent"}
        time.sleep(0.25)
    raise TimeoutError("link did not become active: %s" % last)


def read_net_counters(interface):
    directory = pathlib.Path("/sys/class/net") / interface / "statistics"
    names = (
        "rx_bytes", "rx_packets", "rx_errors", "rx_dropped",
        "tx_bytes", "tx_packets", "tx_errors", "tx_dropped",
    )
    return {
        name: int((directory / name).read_text(encoding="ascii").strip())
        for name in names
    }


def counter_delta(before, after):
    return {name: after[name] - before[name] for name in before}


def run_iperf3(host, port, seconds, parallel, reverse, timeout,
               source=None):
    arguments = [
        "iperf3", "-c", host, "-p", str(port), "-t", str(seconds),
        "-P", str(parallel), "--json",
    ]
    if reverse:
        arguments.append("--reverse")
    if source:
        arguments.extend(["--bind", source])
    result = run_command(arguments, timeout=timeout)
    parsed = json.loads(result["output"])
    if "error" in parsed:
        raise RuntimeError("iperf3 reported: " + parsed["error"])
    return parsed


def peer_main(args):
    invalidate_ready_file(args.ready_file)
    reporter = TapReporter()
    report = {
        "role": "peer",
        "success": False,
        "started_unix": int(time.time()),
        "configuration": {
            "bind": args.bind,
            "tcp_port": args.tcp_port,
            "udp_port": args.udp_port,
            "sessions": args.sessions,
            "streams": args.streams,
            "bytes_per_direction_per_stream": args.bytes_per_direction,
            "udp_packets": args.udp_packets,
        },
        "sessions": [],
    }
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((args.bind, args.tcp_port))
        listener.listen(args.streams + 2)
        udp_socket.bind((args.bind, args.udp_port))
        actual_tcp_port = listener.getsockname()[1]
        actual_udp_port = udp_socket.getsockname()[1]
        report["configuration"]["tcp_port"] = actual_tcp_port
        report["configuration"]["udp_port"] = actual_udp_port
        atomic_json(args.ready_file, {
            "ready": True,
            "bind": args.bind,
            "tcp_port": actual_tcp_port,
            "udp_port": actual_udp_port,
            "pid": os.getpid(),
        })
        deadline = time.monotonic() + args.timeout
        seen_sessions = set()
        for index in range(args.sessions):
            started = time.monotonic()
            session, tcp = run_tcp_server(
                listener, args.streams, args.bytes_per_direction,
                args.fragments, deadline,
            )
            if session in seen_sessions:
                raise ProtocolError("guest reused a session identifier")
            seen_sessions.add(session)
            udp = run_udp_server(
                udp_socket, session, args.udp_packets, deadline,
            )
            value = {
                "index": index + 1,
                "session": session,
                "tcp": tcp,
                "udp": udp,
                "seconds": time.monotonic() - started,
            }
            report["sessions"].append(value)
            reporter.record(
                "network session %d deterministic TCP/UDP integrity" %
                (index + 1), True, value,
            )
        report["success"] = True
        return 0
    except BaseException as exc:
        report["error"] = "%s: %s" % (type(exc).__name__, exc)
        reporter.record("network peer completed", False, report["error"])
        return 1
    finally:
        listener.close()
        udp_socket.close()
        invalidate_ready_file(args.ready_file)
        report["finished_unix"] = int(time.time())
        reporter.finish()
        atomic_json(args.json, report)


def guest_case(reporter, report, name, function):
    try:
        value = function()
    except BaseException as exc:
        detail = "%s: %s" % (type(exc).__name__, exc)
        reporter.record(name, False, detail)
        report["tests"].append({"name": name, "ok": False, "error": detail})
        raise
    reporter.record(name, True)
    report["tests"].append({"name": name, "ok": True, "result": value})
    return value


def guest_main(args):
    reporter = TapReporter()
    if args.session is not None:
        session = args.session
    else:
        seed = struct.pack(
            "!QQQ", time.time_ns(), time.monotonic_ns(), os.getpid(),
        )
        session = int.from_bytes(hashlib.sha256(seed).digest()[:8], "big") or 1
    report = {
        "role": "guest",
        "success": False,
        "started_unix": int(time.time()),
        "session": session,
        "configuration": {
            "host": args.host,
            "tcp_port": args.tcp_port,
            "udp_port": args.udp_port,
            "interface": args.interface,
            "streams": args.streams,
            "bytes_per_direction_per_stream": args.bytes_per_direction,
            "udp_packets": args.udp_packets,
        },
        "tests": [],
    }
    try:
        guest_case(
            reporter, report, "Linux4SAM network userspace inventory",
            userspace_inventory,
        )
        if args.configure_dhcp:
            guest_case(
                reporter, report, "DHCP configuration",
                lambda: configure_dhcp(args.interface, args.link_timeout),
            )
        link = guest_case(
            reporter, report, "GEM/LAN8840 carrier detected",
            lambda: wait_for_link(args.interface, args.link_timeout),
        )
        addresses = guest_case(
            reporter, report, "DHCP/static IPv4 address present",
            lambda: require_ipv4_address(args.interface),
        )
        route = guest_case(
            reporter, report, "host-peer route uses the GEM interface",
            lambda: require_route(args.interface, args.host, addresses),
        )
        source = route["source"]
        if args.skip_ping:
            reporter.record(
                "ICMP host-alias reachability # SKIP requested", True,
            )
            report["tests"].append({
                "name": "ICMP host-alias reachability",
                "ok": True,
                "skip": "requested",
            })
        else:
            guest_case(
                reporter, report, "ICMP host-alias reachability",
                lambda: run_command([
                    "ping", "-I", args.interface, "-c",
                    str(args.ping_count), "-W", "2", args.host,
                ], timeout=args.ping_count * 3 + 5),
            )
        ethtool = guest_case(
            reporter, report, "ethtool link report",
            lambda: require_ethtool_link(args.interface),
        )
        before = guest_case(
            reporter, report, "baseline kernel network counters",
            lambda: read_net_counters(args.interface),
        )
        deadline = time.monotonic() + args.timeout
        tcp = guest_case(
            reporter, report,
            "concurrent full-duplex deterministic TCP integrity",
            lambda: run_tcp_client(
                args.host, args.tcp_port, session, args.streams,
                args.bytes_per_direction, args.fragments, deadline, source,
            ),
        )
        udp = guest_case(
            reporter, report, "bidirectional deterministic UDP integrity",
            lambda: run_udp_client(
                args.host, args.udp_port, session, args.udp_packets,
                args.udp_timeout, args.udp_retries, deadline, source,
            ),
        )
        def check_counters():
            after = read_net_counters(args.interface)
            delta = counter_delta(before, after)
            if delta["rx_packets"] <= 0 or delta["tx_packets"] <= 0:
                raise RuntimeError("RX/TX packet counters did not advance")
            failed = (
                "rx_errors", "tx_errors", "rx_dropped", "tx_dropped",
            )
            if any(delta[name] for name in failed):
                raise RuntimeError(
                    "RX/TX error or drop counters advanced: %s" % delta,
                )
            return {
                "before": before,
                "after": after,
                "delta": delta,
                "ethtool_statistics": run_command([
                    "ethtool", "-S", args.interface,
                ]),
            }

        counters = guest_case(
            reporter, report, "kernel and ethtool counters",
            check_counters,
        )
        iperf = None
        if args.iperf3_port:
            iperf = {
                "forward": guest_case(
                    reporter, report, "iperf3 guest-to-host throughput",
                    lambda: run_iperf3(
                        args.host, args.iperf3_port, args.iperf3_seconds,
                        args.streams, False, args.timeout, source,
                    ),
                ),
                "reverse": guest_case(
                    reporter, report, "iperf3 host-to-guest throughput",
                    lambda: run_iperf3(
                        args.host, args.iperf3_port, args.iperf3_seconds,
                        args.streams, True, args.timeout, source,
                    ),
                ),
            }
        report["network"] = {
            "link": link,
            "addresses": addresses,
            "route": route,
            "ethtool": ethtool,
            "tcp": tcp,
            "udp": udp,
            "counters": counters,
            "iperf3": iperf,
        }
        report["success"] = True
        return 0
    except BaseException as exc:
        report["error"] = "%s: %s" % (type(exc).__name__, exc)
        return 1
    finally:
        report["finished_unix"] = int(time.time())
        reporter.finish()
        atomic_json(args.json, report)


def positive_int(value):
    parsed = int(value, 0)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def session_id(value):
    parsed = int(value, 0)
    if not 0 < parsed <= 0xffffffffffffffff:
        raise argparse.ArgumentTypeError(
            "session must be a nonzero unsigned 64-bit integer",
        )
    return parsed


def port_number(value):
    parsed = int(value, 0)
    if not 0 <= parsed <= 65535:
        raise argparse.ArgumentTypeError("port must be in the range 0..65535")
    return parsed


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="role", required=True)

    peer = subparsers.add_parser("peer", help="run the QEMU-host peer")
    peer.add_argument("--bind", default="127.0.0.1")
    peer.add_argument("--tcp-port", type=port_number, default=19091)
    peer.add_argument("--udp-port", type=port_number, default=19092)
    peer.add_argument("--sessions", type=positive_int, default=1)
    peer.add_argument("--streams", type=positive_int, default=2)
    peer.add_argument(
        "--bytes-per-direction", type=positive_int, default=2 * 1024 * 1024,
    )
    peer.add_argument("--udp-packets", type=positive_int, default=128)
    peer.add_argument("--timeout", type=positive_int, default=900)
    peer.add_argument(
        "--fragments", type=parse_fragment_pattern,
        default=DEFAULT_FRAGMENTS,
    )
    peer.add_argument("--ready-file")
    peer.add_argument("--json")
    peer.set_defaults(function=peer_main)

    guest = subparsers.add_parser("guest", help="run inside Linux4SAM")
    guest.add_argument("--host", default="10.0.2.2")
    guest.add_argument("--tcp-port", type=port_number, default=19091)
    guest.add_argument("--udp-port", type=port_number, default=19092)
    guest.add_argument("--interface", default="eth0")
    guest.add_argument("--streams", type=positive_int, default=2)
    guest.add_argument(
        "--bytes-per-direction", type=positive_int, default=2 * 1024 * 1024,
    )
    guest.add_argument("--udp-packets", type=positive_int, default=128)
    guest.add_argument("--timeout", type=positive_int, default=900)
    guest.add_argument("--link-timeout", type=positive_int, default=60)
    guest.add_argument("--udp-timeout", type=float, default=2.0)
    guest.add_argument("--udp-retries", type=int, default=4)
    guest.add_argument("--ping-count", type=positive_int, default=3)
    guest.add_argument("--skip-ping", action="store_true")
    guest.add_argument("--configure-dhcp", action="store_true")
    guest.add_argument("--session", type=session_id)
    guest.add_argument(
        "--fragments", type=parse_fragment_pattern,
        default=DEFAULT_FRAGMENTS,
    )
    guest.add_argument("--iperf3-port", type=port_number, default=0)
    guest.add_argument("--iperf3-seconds", type=positive_int, default=5)
    guest.add_argument("--json")
    guest.set_defaults(function=guest_main)
    return parser


def main():
    args = build_parser().parse_args()
    if getattr(args, "streams", 1) > 255:
        raise SystemExit("--streams cannot exceed 255")
    if getattr(args, "udp_retries", 0) < 0:
        raise SystemExit("--udp-retries cannot be negative")
    return args.function(args)


if __name__ == "__main__":
    sys.exit(main())
