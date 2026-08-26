#!/usr/bin/env python3
"""Full-duplex UART partner/exerciser for SAM9X75 Linux4SAM guests.

The ``peer`` role runs on the QEMU host (AF_UNIX) or on a workstation
connected to a physical board (pyserial).  The ``guest`` role runs inside
Linux4SAM and uses only Python's standard library to open a Linux tty.
"""

# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import dataclasses
import enum
import errno
import hashlib
import json
import os
import pathlib
import queue
import secrets
import select
import socket
import stat
import struct
import sys
import termios
import threading
import time
import zlib


MAGIC = b"S9U1"
VERSION = 1
MAX_PAYLOAD = 256 * 1024
HEADER_NO_CRC = struct.Struct(">4sBBBBQII")
HEADER = struct.Struct(">4sBBBBQIII")
BOUNDARY_SIZES = (
    0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256,
    511, 512, 1023, 1024, 4095, 4096, 16383, 16384, 32767, 65535,
    65536,
)
DEFAULT_FRAGMENT_PATTERN = (1, 2, 3, 5, 8, 13, 21, 64, 257, 1024)


class Kind(enum.IntEnum):
    HELLO = 1
    DATA = 2
    ACK = 3
    QUIESCE = 4
    QUIESCED = 5
    RESUME = 6
    DONE = 7
    ERROR = 255


class Direction(enum.IntEnum):
    GUEST_TO_PEER = 1
    PEER_TO_GUEST = 2


class StreamTimeout(Exception):
    """A transport read timed out without consuming data."""


class StreamEOF(Exception):
    """A transport connection closed."""


class ProtocolError(Exception):
    """The remote endpoint violated the fixture protocol."""


@dataclasses.dataclass(frozen=True)
class Frame:
    kind: Kind
    direction: Direction
    session: int
    sequence: int
    payload: bytes = b""
    flags: int = 0

    def encode(self):
        if not 0 <= self.session <= 0xffffffffffffffff:
            raise ValueError("session is not a uint64")
        if not 0 <= self.sequence <= 0xffffffff:
            raise ValueError("sequence is not a uint32")
        if len(self.payload) > MAX_PAYLOAD:
            raise ValueError("payload exceeds protocol limit")
        prefix = HEADER_NO_CRC.pack(
            MAGIC, VERSION, int(self.kind), int(self.direction), self.flags,
            self.session, self.sequence, len(self.payload),
        )
        checksum = zlib.crc32(prefix)
        checksum = zlib.crc32(self.payload, checksum) & 0xffffffff
        return HEADER.pack(
            MAGIC, VERSION, int(self.kind), int(self.direction), self.flags,
            self.session, self.sequence, len(self.payload), checksum,
        ) + self.payload


class FrameDecoder:
    """Incremental decoder which can regain framing after line noise."""

    def __init__(self):
        self.buffer = bytearray()
        self.crc_errors = 0
        self.header_errors = 0
        self.discarded_bytes = 0

    def _complete_reset_hello_at(self):
        """Find a complete guest HELLO nested after an interrupted frame."""

        offset = 1
        expected_payload = hello_payload()
        while True:
            offset = self.buffer.find(MAGIC, offset)
            if offset < 0:
                return None
            remaining = len(self.buffer) - offset
            if remaining < HEADER.size:
                return None
            values = HEADER.unpack(
                self.buffer[offset:offset + HEADER.size],
            )
            (magic, version, kind, direction, flags, session, sequence,
             length, crc) = values
            if (version != VERSION or kind != Kind.HELLO or
                    direction != Direction.GUEST_TO_PEER or flags != 0 or
                    session == 0 or sequence != 0 or
                    length != len(expected_payload)):
                offset += 1
                continue
            total = HEADER.size + length
            if remaining < total:
                return None
            payload = bytes(
                self.buffer[offset + HEADER.size:offset + total],
            )
            prefix = HEADER_NO_CRC.pack(
                magic, version, kind, direction, flags, session, sequence,
                length,
            )
            actual = zlib.crc32(prefix)
            actual = zlib.crc32(payload, actual) & 0xffffffff
            if actual == crc and payload == expected_payload:
                return offset
            offset += 1

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while True:
            magic_at = self.buffer.find(MAGIC)
            if magic_at < 0:
                keep = min(len(self.buffer), len(MAGIC) - 1)
                self.discarded_bytes += len(self.buffer) - keep
                if keep:
                    del self.buffer[:-keep]
                else:
                    self.buffer.clear()
                break
            if magic_at:
                self.discarded_bytes += magic_at
                del self.buffer[:magic_at]
            if len(self.buffer) < HEADER.size:
                break
            values = HEADER.unpack(self.buffer[:HEADER.size])
            (magic, version, kind, direction, flags, session, sequence,
             length, crc) = values
            if (version != VERSION or length > MAX_PAYLOAD or
                    kind not in Kind._value2member_map_ or
                    direction not in Direction._value2member_map_):
                self.header_errors += 1
                self.discarded_bytes += 1
                del self.buffer[0]
                continue
            total = HEADER.size + length
            if len(self.buffer) < total:
                reset_hello_at = self._complete_reset_hello_at()
                if reset_hello_at is not None:
                    self.discarded_bytes += reset_hello_at
                    del self.buffer[:reset_hello_at]
                    continue
                break
            payload = bytes(self.buffer[HEADER.size:total])
            prefix = HEADER_NO_CRC.pack(
                magic, version, kind, direction, flags, session, sequence,
                length,
            )
            actual = zlib.crc32(prefix)
            actual = zlib.crc32(payload, actual) & 0xffffffff
            if actual != crc:
                self.crc_errors += 1
                self.discarded_bytes += 1
                del self.buffer[0]
                continue
            frames.append(Frame(
                Kind(kind), Direction(direction), session, sequence, payload,
                flags,
            ))
            del self.buffer[:total]
        return frames


def deterministic_payload(session, direction, sequence, length):
    """Return stable incompressible-looking bytes without shared PRNG state."""

    seed = struct.pack(">QBI", session, int(direction), sequence)
    result = bytearray()
    block = 0
    while len(result) < length:
        result.extend(hashlib.sha256(seed + struct.pack(">I", block)).digest())
        block += 1
    return bytes(result[:length])


def hello_payload():
    sizes = b"".join(struct.pack(">I", value) for value in BOUNDARY_SIZES)
    digest = hashlib.sha256(sizes).digest()
    return struct.pack(">I", len(BOUNDARY_SIZES)) + digest


def parse_fragment_pattern(value):
    try:
        result = tuple(int(item, 0) for item in value.split(",") if item)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    if not result or any(item <= 0 for item in result):
        raise argparse.ArgumentTypeError("fragment sizes must be positive")
    return result


class UnixServerStream:
    """Reconnectable AF_UNIX stream used by a QEMU socket chardev client."""

    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener.setblocking(False)
        self._connection = None
        self._lock = threading.Lock()
        self._closed = False
        self.connections = 0
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.listener.bind(str(self.path))
        self.listener.listen(4)

    def _get_connection(self, timeout=None):
        deadline = None if timeout is None else time.monotonic() + timeout
        while not self._closed:
            with self._lock:
                if self._connection is not None:
                    return self._connection
            wait = 0.25
            if deadline is not None:
                wait = min(wait, max(0.0, deadline - time.monotonic()))
                if wait == 0.0:
                    raise StreamTimeout()
            try:
                readable, _, _ = select.select(
                    [self.listener], [], [], wait,
                )
            except (OSError, ValueError) as exc:
                if self._closed:
                    raise StreamEOF() from exc
                raise
            if not readable:
                continue
            with self._lock:
                if self._connection is not None:
                    return self._connection
                try:
                    connection, _ = self.listener.accept()
                except BlockingIOError:
                    continue
                connection.setblocking(False)
                self._connection = connection
                self.connections += 1
                return connection
        raise StreamEOF()

    def _drop(self, connection):
        with self._lock:
            if self._connection is connection:
                self._connection = None
                try:
                    connection.close()
                except OSError:
                    pass

    def read(self, size, timeout):
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise StreamTimeout()
            connection = self._get_connection(remaining)
            try:
                readable, _, _ = select.select([connection], [], [], remaining)
                if not readable:
                    raise StreamTimeout()
                data = connection.recv(size)
                if not data:
                    self._drop(connection)
                    continue
                return data
            except (BrokenPipeError, ConnectionResetError, OSError) as exc:
                if isinstance(exc, OSError) and exc.errno not in (
                        errno.EBADF, errno.ECONNRESET, errno.ENOTCONN):
                    raise
                self._drop(connection)

    def write(self, data):
        view = memoryview(data)
        while view:
            connection = self._get_connection()
            try:
                _, writable, _ = select.select([], [connection], [], 1.0)
                if not writable:
                    continue
                written = connection.send(view)
                if written == 0:
                    self._drop(connection)
                    continue
                view = view[written:]
            except (BrokenPipeError, ConnectionResetError, OSError) as exc:
                if isinstance(exc, OSError) and exc.errno not in (
                        errno.EBADF, errno.EPIPE, errno.ECONNRESET,
                        errno.ENOTCONN):
                    raise
                self._drop(connection)

    def migration_resume(self, timeout):
        """Replace the source-QEMU connection with a queued destination one."""

        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            readable, _, _ = select.select([self.listener], [], [], remaining)
            if not readable:
                return False
            try:
                replacement, _ = self.listener.accept()
            except BlockingIOError:
                continue
            replacement.setblocking(False)
            with self._lock:
                previous = self._connection
                self._connection = replacement
                self.connections += 1
            if previous is not None:
                try:
                    previous.close()
                except OSError:
                    pass
            return True

    def close(self):
        self._closed = True
        with self._lock:
            if self._connection is not None:
                self._connection.close()
                self._connection = None
            self.listener.close()
        try:
            mode = self.path.lstat().st_mode
            if stat.S_ISSOCK(mode):
                self.path.unlink()
        except FileNotFoundError:
            pass


class PosixTTYStream:
    """Standard-library raw tty transport for the Linux4SAM guest role."""

    def __init__(self, path, baud):
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        speed = getattr(termios, "B%d" % baud, None)
        if speed is None:
            os.close(self.fd)
            raise ValueError("unsupported termios baud rate: %d" % baud)
        attributes = termios.tcgetattr(self.fd)
        attributes[0] = 0
        attributes[1] = 0
        attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attributes[3] = 0
        attributes[4] = speed
        attributes[5] = speed
        attributes[6][termios.VMIN] = 0
        attributes[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attributes)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def read(self, size, timeout):
        readable, _, _ = select.select([self.fd], [], [], timeout)
        if not readable:
            raise StreamTimeout()
        data = os.read(self.fd, size)
        if not data:
            raise StreamTimeout()
        return data

    def write(self, data):
        view = memoryview(data)
        while view:
            _, writable, _ = select.select([], [self.fd], [], 1.0)
            if not writable:
                continue
            try:
                written = os.write(self.fd, view)
            except BlockingIOError:
                continue
            view = view[written:]

    def close(self):
        os.close(self.fd)


class PySerialStream:
    """Optional pyserial transport for a workstation-to-board adapter."""

    def __init__(self, path, baud):
        try:
            import serial
        except ImportError as exc:
            raise RuntimeError(
                "the peer serial transport needs pyserial "
                "(python3 -m pip install pyserial)"
            ) from exc
        self.serial = serial.Serial(
            path, baudrate=baud, bytesize=8, parity="N", stopbits=1,
            timeout=0.25, write_timeout=5,
        )
        self.serial.reset_input_buffer()
        self.serial.reset_output_buffer()

    def read(self, size, timeout):
        old_timeout = self.serial.timeout
        self.serial.timeout = timeout
        try:
            data = self.serial.read(size)
        finally:
            self.serial.timeout = old_timeout
        if not data:
            raise StreamTimeout()
        return data

    def write(self, data):
        self.serial.write(data)
        self.serial.flush()

    def close(self):
        self.serial.close()


class OutboundPump:
    """Single writer which leaves the UART reader free to run concurrently."""

    def __init__(self, stream, active_session, fragment_pattern, pace_us):
        self.stream = stream
        self.active_session = active_session
        self.fragment_pattern = fragment_pattern
        self.pace = pace_us / 1000000.0
        self.items = queue.Queue()
        self.error = None
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def enqueue(self, frame):
        self.items.put((frame.session, frame.encode()))

    def _write_fragmented(self, encoded):
        offset = 0
        fragment = 0
        while offset < len(encoded):
            index = fragment % len(self.fragment_pattern)
            length = self.fragment_pattern[index]
            self.stream.write(encoded[offset:offset + length])
            offset += length
            fragment += 1
            if self.pace:
                time.sleep(self.pace)

    def _run(self):
        while not self.stop.is_set():
            try:
                session, encoded = self.items.get(timeout=0.1)
            except queue.Empty:
                continue
            try:
                if session == self.active_session():
                    self._write_fragmented(encoded)
            # Transport errors must reach the main thread through ``error``.
            except Exception as exc:
                self.error = exc
                self.stop.set()
            finally:
                self.items.task_done()

    def wait_idle(self, timeout):
        deadline = time.monotonic() + timeout
        while self.items.unfinished_tasks:
            if self.error:
                raise self.error
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.01)
        return True

    def close(self):
        self.stop.set()
        self.thread.join(timeout=1.0)


class EndpointRunner:
    def __init__(self, stream, role, args):
        self.stream = stream
        self.role = role
        self.local_direction = (Direction.GUEST_TO_PEER if role == "guest"
                                else Direction.PEER_TO_GUEST)
        self.remote_direction = (Direction.PEER_TO_GUEST if role == "guest"
                                 else Direction.GUEST_TO_PEER)
        self.args = args
        self.decoder = FrameDecoder()
        self.session_lock = threading.Lock()
        self.session = args.session if role == "guest" else 0
        self.session_started = False
        self.tx_next = 0
        self.tx_outstanding = None
        self.rx_next = 0
        self.local_done = False
        self.remote_done = False
        self.paused = False
        self.quiesce_pending = False
        self.barrier_requested = False
        self.barrier_seen = False
        self.barrier_complete = False
        self.barrier_ready = False
        self.barrier_resumed = False
        self.hello_count = 0
        self.resync_count = 0
        self.completed_sessions = 0
        self.aborted_sessions = 0
        self.tx_acked_total = 0
        self.rx_valid_total = 0
        self.duplicate_frames = 0
        self.read_batches = 0
        self.errors = []
        self.started_at = time.monotonic()
        self.finished_at = None
        self.pump = OutboundPump(
            stream, self._active_session, args.fragment_pattern,
            args.pace_us,
        )

    def _active_session(self):
        with self.session_lock:
            return self.session

    def _set_session(self, value):
        with self.session_lock:
            self.session = value

    def _frame(self, kind, sequence=0, payload=b""):
        return Frame(
            kind, self.local_direction, self.session, sequence, payload,
        )

    def _enqueue(self, kind, sequence=0, payload=b""):
        self.pump.enqueue(self._frame(kind, sequence, payload))

    def _reset_state(self, session):
        if self.session_started and not (self.local_done and self.remote_done):
            # A hardware reset intentionally abandons the old session.  Keep
            # its partial progress out of the totals used to judge the newly
            # synchronized session.
            self.tx_acked_total -= self.tx_next
            self.rx_valid_total -= self.rx_next
            self.aborted_sessions += 1
        self._set_session(session)
        self.session_started = True
        self.tx_next = 0
        self.tx_outstanding = None
        self.rx_next = 0
        self.local_done = False
        self.remote_done = False
        self.paused = False
        self.quiesce_pending = False
        self.barrier_requested = False
        self.barrier_seen = False
        self.barrier_complete = False
        self.barrier_ready = False
        self.barrier_resumed = False

    def _send_next_data(self):
        if (self.paused or self.local_done or
                self.tx_outstanding is not None or
                self.tx_next >= len(BOUNDARY_SIZES)):
            return
        sequence = self.tx_next
        length = BOUNDARY_SIZES[sequence]
        payload = deterministic_payload(
            self.session, self.local_direction, sequence, length,
        )
        self.tx_outstanding = sequence
        self._enqueue(Kind.DATA, sequence, payload)

    def _send_done_if_ready(self):
        if (not self.local_done and self.tx_next == len(BOUNDARY_SIZES) and
                self.rx_next == len(BOUNDARY_SIZES) and
                self.tx_outstanding is None and not self.paused):
            self.local_done = True
            self._enqueue(Kind.DONE, len(BOUNDARY_SIZES))

    def _validate_common(self, frame):
        if frame.direction != self.remote_direction:
            raise ProtocolError(
                "frame has wrong direction %s" % frame.direction.name
            )
        if frame.session != self.session:
            return False
        return True

    def _handle_hello(self, frame):
        if self.role != "peer" or frame.direction != Direction.GUEST_TO_PEER:
            raise ProtocolError("unexpected HELLO")
        if frame.payload != hello_payload():
            raise ProtocolError("HELLO boundary-set digest mismatch")
        if not self.session_started or frame.session != self.session:
            self._reset_state(frame.session)
            if self.hello_count:
                self.resync_count += 1
            self.hello_count += 1
        self._enqueue(Kind.HELLO, 0, hello_payload())
        self._send_next_data()

    def _handle_data(self, frame):
        if not self._validate_common(frame):
            return
        if frame.sequence >= len(BOUNDARY_SIZES):
            raise ProtocolError("DATA sequence outside boundary table")
        length = BOUNDARY_SIZES[frame.sequence]
        expected = deterministic_payload(
            self.session, self.remote_direction, frame.sequence, length,
        )
        if len(frame.payload) != length or frame.payload != expected:
            raise ProtocolError(
                "DATA payload mismatch at sequence %d" % frame.sequence
            )
        if frame.sequence < self.rx_next:
            self.duplicate_frames += 1
            self._enqueue(Kind.ACK, frame.sequence)
            return
        if frame.sequence != self.rx_next:
            raise ProtocolError(
                "DATA sequence gap: expected %d, received %d" %
                (self.rx_next, frame.sequence)
            )
        self.rx_next += 1
        self.rx_valid_total += 1
        self._enqueue(Kind.ACK, frame.sequence)

    def _maybe_request_barrier(self):
        if (self.role == "peer" and self.args.barrier_after is not None and
                not self.barrier_requested and not self.barrier_complete and
                self.tx_next == self.args.barrier_after and
                self.tx_outstanding is None):
            self.paused = True
            self.barrier_requested = True
            self._enqueue(Kind.QUIESCE, self.tx_next)
            return True
        return False

    def _maybe_guest_quiesced(self):
        if (self.role == "guest" and self.quiesce_pending and
                self.tx_outstanding is None):
            self.quiesce_pending = False
            self.barrier_ready = True
            self._enqueue(Kind.QUIESCED, self.tx_next)

    def _handle_ack(self, frame):
        if not self._validate_common(frame):
            return
        if frame.payload:
            raise ProtocolError("ACK must have an empty payload")
        if self.tx_outstanding is None:
            if frame.sequence < self.tx_next:
                self.duplicate_frames += 1
                return
            raise ProtocolError("ACK without an outstanding DATA frame")
        if frame.sequence != self.tx_outstanding:
            raise ProtocolError(
                "ACK mismatch: expected %d, received %d" %
                (self.tx_outstanding, frame.sequence)
            )
        self.tx_outstanding = None
        self.tx_next += 1
        self.tx_acked_total += 1
        self._maybe_guest_quiesced()
        if not self._maybe_request_barrier():
            self._send_next_data()

    def _write_json_atomically(self, path, value):
        target = pathlib.Path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + ".tmp.%d" % os.getpid())
        contents = json.dumps(value, indent=2, sort_keys=True) + "\n"
        temporary.write_text(contents)
        os.replace(temporary, target)

    def _wait_for_migration_resume(self):
        self.barrier_ready = True
        ready = {
            "event": "sam9x75-uart-migration-barrier-ready",
            "session": "0x%016x" % self.session,
            "tx_acked": self.tx_next,
            "rx_validated": self.rx_next,
        }
        print("# MIGRATION_BARRIER_READY session=0x%016x" % self.session,
              file=sys.stderr, flush=True)
        if self.args.barrier_ready_file:
            self._write_json_atomically(self.args.barrier_ready_file, ready)
        deadline = time.monotonic() + self.args.timeout
        if self.args.resume_file:
            resume = pathlib.Path(self.args.resume_file)
            while not resume.exists():
                if time.monotonic() >= deadline:
                    raise ProtocolError(
                        "timed out waiting for migration resume file"
                    )
                time.sleep(0.05)
        else:
            print("# press Enter after migration completes", file=sys.stderr,
                  flush=True)
            if not select.select([sys.stdin], [], [], self.args.timeout)[0]:
                raise ProtocolError("timed out waiting for migration resume")
            sys.stdin.readline()
        if self.args.migration_reconnect_timeout:
            reconnect = getattr(self.stream, "migration_resume", None)
            if reconnect is None:
                raise ProtocolError(
                    "migration reconnect was requested for a non-AF_UNIX "
                    "transport"
                )
            if not reconnect(self.args.migration_reconnect_timeout):
                raise ProtocolError(
                    "destination QEMU did not connect before migration resume"
                )
        self.barrier_resumed = True
        self.barrier_complete = True
        self.barrier_requested = False
        self.paused = False
        self._enqueue(Kind.RESUME, self.tx_next)
        self._send_next_data()

    def _handle_control(self, frame):
        if not self._validate_common(frame):
            return
        if frame.kind == Kind.QUIESCE:
            if self.role != "guest":
                raise ProtocolError("peer received QUIESCE")
            self.barrier_seen = True
            self.paused = True
            self.quiesce_pending = True
            self._maybe_guest_quiesced()
        elif frame.kind == Kind.QUIESCED:
            if self.role != "peer" or not self.barrier_requested:
                raise ProtocolError("unexpected QUIESCED")
            self._wait_for_migration_resume()
        elif frame.kind == Kind.RESUME:
            if self.role != "guest" or not self.paused:
                raise ProtocolError("unexpected RESUME")
            self.barrier_resumed = True
            self.barrier_complete = True
            self.paused = False
            self._send_next_data()
        elif frame.kind == Kind.DONE:
            self.remote_done = True
        elif frame.kind == Kind.ERROR:
            raise ProtocolError("remote ERROR: %r" % frame.payload[:160])

    def _handle_frame(self, frame):
        if frame.kind == Kind.HELLO:
            if self.role == "peer":
                self._handle_hello(frame)
            else:
                if not self._validate_common(frame):
                    return
                if frame.payload != hello_payload():
                    raise ProtocolError(
                        "peer HELLO boundary-set digest mismatch"
                    )
                if not self.session_started:
                    self.session_started = True
                    self.hello_count += 1
                    self._send_next_data()
            return
        if not self.session_started:
            return
        if frame.kind == Kind.DATA:
            self._handle_data(frame)
        elif frame.kind == Kind.ACK:
            self._handle_ack(frame)
        else:
            self._handle_control(frame)
        self._send_done_if_ready()

    def _session_complete(self):
        return self.local_done and self.remote_done

    def _finish_session(self):
        if not self.pump.wait_idle(10.0):
            raise ProtocolError("UART writer did not drain at session end")
        self.completed_sessions += 1
        if (self.role == "peer" and
                self.completed_sessions < self.args.sessions):
            self.session_started = False
            self.local_done = False
            self.remote_done = False
            return False
        return True

    def run(self):
        deadline = time.monotonic() + self.args.timeout
        last_hello = 0.0
        if self.role == "guest":
            self._reset_state(self.session)
            # The session number is selected, but DATA cannot start until the
            # peer echoes a valid HELLO for it.
            self.session_started = False
        try:
            while time.monotonic() < deadline:
                if self.pump.error:
                    raise self.pump.error
                if self.role == "guest" and self.hello_count == 0:
                    now = time.monotonic()
                    if now - last_hello >= 1.0:
                        self._enqueue(Kind.HELLO, 0, hello_payload())
                        last_hello = now
                try:
                    data = self.stream.read(16384, 0.25)
                except StreamTimeout:
                    continue
                except StreamEOF:
                    continue
                self.read_batches += 1
                for frame in self.decoder.feed(data):
                    self._handle_frame(frame)
                    if self._session_complete() and self._finish_session():
                        self.finished_at = time.monotonic()
                        return self.report()
                if self.args.backpressure_every:
                    inject_pause = (
                        self.read_batches % self.args.backpressure_every == 0
                    )
                    if inject_pause:
                        time.sleep(self.args.backpressure_ms / 1000.0)
            raise ProtocolError("overall test timeout")
        except Exception as exc:
            self.errors.append("%s: %s" % (type(exc).__name__, exc))
            self.finished_at = time.monotonic()
            return self.report()
        finally:
            self.pump.close()

    def report(self):
        finished = self.finished_at or time.monotonic()
        expected = self.completed_sessions * len(BOUNDARY_SIZES)
        required_sessions = self.args.sessions if self.role == "peer" else 1
        if self._session_complete() and self.completed_sessions == 0:
            expected = len(BOUNDARY_SIZES)
        success = (
            not self.errors and
            self.decoder.crc_errors == 0 and
            self.decoder.header_errors == 0 and
            self.completed_sessions >= required_sessions and
            self.tx_acked_total == expected and
            self.rx_valid_total == expected
        )
        return {
            "schema": "sam9x75-uart-partner-report-v1",
            "role": self.role,
            "transport": getattr(self.args, "transport_label", "test-stream"),
            "success": success,
            "session": "0x%016x" % self.session,
            "duration_seconds": round(finished - self.started_at, 6),
            "boundary_sizes": list(BOUNDARY_SIZES),
            "required_sessions": required_sessions,
            "completed_sessions": self.completed_sessions,
            "hello_count": self.hello_count,
            "reset_resync_count": self.resync_count,
            "aborted_sessions": self.aborted_sessions,
            "tx_data_acked": self.tx_acked_total,
            "rx_data_validated": self.rx_valid_total,
            "duplicate_frames": self.duplicate_frames,
            "transport_read_batches": self.read_batches,
            "decoder_crc_errors": self.decoder.crc_errors,
            "decoder_header_errors": self.decoder.header_errors,
            "decoder_discarded_bytes": self.decoder.discarded_bytes,
            "fragment_pattern": list(self.args.fragment_pattern),
            "pace_us": self.args.pace_us,
            "backpressure_every": self.args.backpressure_every,
            "backpressure_ms": self.args.backpressure_ms,
            "migration_barrier_requested": (
                self.args.barrier_after is not None or self.barrier_seen
            ),
            "migration_barrier_ready": self.barrier_ready,
            "migration_barrier_resumed": self.barrier_resumed,
            "errors": list(self.errors),
        }


def emit_tap(report):
    expected = report["required_sessions"] * len(BOUNDARY_SIZES)
    checks = [
        (report["hello_count"] >= 1,
         "HELLO and reset-session synchronization", None),
        (report["rx_data_validated"] == expected,
         "received DATA payload, sequence and CRC validation", None),
        (report["tx_data_acked"] == expected,
         "transmitted DATA acknowledged in sequence", None),
        (report["decoder_crc_errors"] == 0 and
         report["decoder_header_errors"] == 0,
         "stream framing stayed valid", None),
        (not report["errors"], "fragmentation, pacing and backpressure", None),
    ]
    if report["migration_barrier_requested"]:
        checks.append((
            report["migration_barrier_ready"] and
            report["migration_barrier_resumed"],
            "migration quiesce barrier and resume", None,
        ))
    else:
        checks.append((True, "migration quiesce barrier", "not requested"))
    checks.append((
        report["success"], "overall full-duplex UART session", None,
    ))
    print("TAP version 13")
    print("1..%d" % len(checks))
    for number, (passed, description, skip) in enumerate(checks, 1):
        status = "ok" if passed else "not ok"
        suffix = " # SKIP %s" % skip if skip else ""
        print("%s %d - %s%s" % (status, number, description, suffix))
    if report["errors"]:
        for error in report["errors"]:
            print("# error: %s" % error)


def write_json_report(path, report):
    if not path:
        return
    target = pathlib.Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + ".tmp.%d" % os.getpid())
    temporary.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    os.replace(temporary, target)


def common_arguments(parser):
    parser.add_argument("--baud", type=int, default=115200,
                        help="8N1 baud rate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=300.0,
                        help="overall timeout in seconds (default: 300)")
    parser.add_argument(
        "--fragment-pattern", type=parse_fragment_pattern,
        default=DEFAULT_FRAGMENT_PATTERN,
        help="comma-separated UART write sizes",
    )
    parser.add_argument("--pace-us", type=int, default=0,
                        help="delay after every write fragment")
    parser.add_argument("--backpressure-every", type=int, default=0,
                        help="pause receiver after every N reads (0 disables)")
    parser.add_argument("--backpressure-ms", type=int, default=25,
                        help="duration of each injected pause")
    parser.add_argument("--json", metavar="FILE",
                        help="write the structured result atomically")


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="role", required=True)

    peer = subparsers.add_parser("peer", help="host/workstation UART partner")
    transport = peer.add_mutually_exclusive_group(required=True)
    transport.add_argument("--unix-listen", metavar="PATH",
                           help="listen for a QEMU socket chardev")
    transport.add_argument("--serial", metavar="DEVICE",
                           help="open a physical adapter using pyserial")
    peer.add_argument("--sessions", type=int, default=1,
                      help="completed reset sessions to require")
    peer.add_argument("--barrier-after", type=int, metavar="N",
                      help="quiesce after N peer-to-guest DATA frames")
    peer.add_argument("--barrier-ready-file", metavar="FILE",
                      help="write JSON when it is safe to migrate")
    peer.add_argument("--resume-file", metavar="FILE",
                      help="resume when this file appears")
    peer.add_argument(
        "--migration-reconnect-timeout", type=float, default=0.0,
        help="require/switch to a destination QEMU connection on resume",
    )
    peer.set_defaults(session=0)
    common_arguments(peer)

    guest = subparsers.add_parser("guest", help="Linux4SAM tty exerciser")
    guest.add_argument("--device", default="/dev/ttyS1",
                       help="guest tty (default: /dev/ttyS1)")
    guest.add_argument("--session", type=lambda value: int(value, 0),
                       default=None, help="fixed uint64 session for replay")
    guest.set_defaults(sessions=1, barrier_after=None,
                       barrier_ready_file=None, resume_file=None,
                       migration_reconnect_timeout=0.0)
    common_arguments(guest)
    return parser


def validate_args(parser, args):
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if (args.pace_us < 0 or args.backpressure_every < 0 or
            args.backpressure_ms < 0):
        parser.error("pacing and backpressure values cannot be negative")
    if args.role == "peer":
        if args.sessions <= 0:
            parser.error("--sessions must be positive")
        if args.barrier_after is not None:
            if not 1 <= args.barrier_after < len(BOUNDARY_SIZES):
                parser.error(
                    "--barrier-after must be within the DATA sequence"
                )
            if args.sessions != 1:
                parser.error(
                    "migration barrier currently requires --sessions 1"
                )
        if args.migration_reconnect_timeout < 0:
            parser.error("--migration-reconnect-timeout cannot be negative")
        if args.migration_reconnect_timeout and not args.unix_listen:
            parser.error("migration reconnect requires --unix-listen")
        if args.migration_reconnect_timeout and args.barrier_after is None:
            parser.error("migration reconnect requires --barrier-after")
        for option, path in (
            ("--barrier-ready-file", args.barrier_ready_file),
            ("--resume-file", args.resume_file),
        ):
            if path and os.path.lexists(path):
                parser.error("%s path already exists: %s" % (option, path))
    else:
        if args.session is None:
            args.session = secrets.randbits(64) or 1
        if not 0 < args.session <= 0xffffffffffffffff:
            parser.error("--session must be a nonzero uint64")


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    validate_args(parser, args)
    if args.role == "guest":
        args.transport_label = "tty:%s" % args.device
    elif args.unix_listen:
        args.transport_label = "unix-listen:%s" % args.unix_listen
    else:
        args.transport_label = "serial:%s" % args.serial
    stream = None
    try:
        if args.role == "peer" and args.unix_listen:
            stream = UnixServerStream(args.unix_listen)
        elif args.role == "peer":
            stream = PySerialStream(args.serial, args.baud)
        else:
            stream = PosixTTYStream(args.device, args.baud)
        report = EndpointRunner(stream, args.role, args).run()
    except Exception as exc:
        report = {
            "schema": "sam9x75-uart-partner-report-v1",
            "role": args.role,
            "transport": args.transport_label,
            "success": False,
            "session": "0x%016x" % (args.session or 0),
            "duration_seconds": 0.0,
            "boundary_sizes": list(BOUNDARY_SIZES),
            "required_sessions": (
                args.sessions if args.role == "peer" else 1
            ),
            "completed_sessions": 0,
            "hello_count": 0,
            "reset_resync_count": 0,
            "aborted_sessions": 0,
            "tx_data_acked": 0,
            "rx_data_validated": 0,
            "duplicate_frames": 0,
            "transport_read_batches": 0,
            "decoder_crc_errors": 0,
            "decoder_header_errors": 0,
            "decoder_discarded_bytes": 0,
            "fragment_pattern": list(args.fragment_pattern),
            "pace_us": args.pace_us,
            "backpressure_every": args.backpressure_every,
            "backpressure_ms": args.backpressure_ms,
            "migration_barrier_requested": args.barrier_after is not None,
            "migration_barrier_ready": False,
            "migration_barrier_resumed": False,
            "errors": ["%s: %s" % (type(exc).__name__, exc)],
        }
    finally:
        if stream is not None:
            stream.close()
    emit_tap(report)
    write_json_report(args.json, report)
    return 0 if report["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
