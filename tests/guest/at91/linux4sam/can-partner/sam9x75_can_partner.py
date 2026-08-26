#!/usr/bin/env python3
"""Deterministic SocketCAN partner for SAM9X75 M_CAN validation.

The ``peer`` role runs beside QEMU on a Linux host, normally on a private
vcan interface.  The ``guest`` role runs in Linux4Microchip and binds its
M_CAN SocketCAN interface.  Both roles use only Python's standard library.
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
import re
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from typing import Optional


PROTOCOL_VERSION = 1
REPORT_SCHEMA = "sam9x75-can-partner-report-v1"

CAN_MTU = 16
CANFD_MTU = 72
CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_ERR_FLAG = 0x20000000
CAN_SFF_MASK = 0x000007ff
CAN_EFF_MASK = 0x1fffffff
CAN_ERR_MASK = 0x1fffffff
CANFD_BRS = 0x01
CANFD_ESI = 0x02
CANFD_FDF = 0x04
SOL_CAN_RAW = getattr(socket, "SOL_CAN_RAW", 101)
CAN_RAW_ERR_FILTER = 2
CAN_RAW_LOOPBACK = 3
CAN_RAW_RECV_OWN_MSGS = 4
CAN_RAW_FD_FRAMES = 5
SO_RXQ_OVFL = getattr(socket, "SO_RXQ_OVFL", 40)

CLASSIC_FRAME = struct.Struct("=IB3x8s")
CAN_FD_FRAME = struct.Struct("=IBBBB64s")
SESSION_PAYLOAD = struct.Struct(">Q")

# Bits 28..25 identify the protocol family.  Control frames use bits 24..21
# for Kind, bit 20 for Direction and bits 19..0 for a sequence/value.  Stress
# frames use bit 24 for Direction, bits 23..16 for a session tag and bits
# 15..0 for the sequence.  Boundary frames intentionally use ordinary IDs.
PROTOCOL_FAMILY_MASK = 0x1e000000
CONTROL_PREFIX = 0x1a000000
STRESS_PREFIX = 0x18000000
CONTROL_KIND_SHIFT = 21
CONTROL_DIRECTION_SHIFT = 20
CONTROL_SEQUENCE_MASK = 0x000fffff
STRESS_DIRECTION_SHIFT = 24
STRESS_SESSION_SHIFT = 16
STRESS_SEQUENCE_MASK = 0x0000ffff
HELLO_FRAME_MASK = 0x0000ffff
HELLO_ESI = 0x00010000
HELLO_VERSION_SHIFT = 17
HELLO_VERSION_MASK = 0x000e0000

FD_LENGTHS = (0, 1, 2, 3, 4, 5, 6, 7, 8,
              12, 16, 20, 24, 32, 48, 64)
DEFAULT_PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"


class Direction(enum.IntEnum):
    GUEST_TO_PEER = 0
    PEER_TO_GUEST = 1


class Kind(enum.IntEnum):
    HELLO = 1
    HELLO_ACK = 2
    BOUNDARY_ACK = 3
    READY = 4
    STRESS_ACK = 5
    DONE = 6
    DONE_ACK = 7
    COMPLETE = 8
    COMPLETE_ACK = 9
    ERROR = 15


class Pattern(enum.IntEnum):
    ZERO = 0
    ONES = 1
    INCREMENTING = 2
    HASH = 3


class ProtocolError(Exception):
    """The remote endpoint violated the deterministic test protocol."""


class SessionRestart(Exception):
    """A peer observed a new guest HELLO during an incomplete session."""

    def __init__(self, hello):
        super().__init__("guest started session 0x%016x" % hello.session)
        self.hello = hello


@dataclasses.dataclass(frozen=True)
class CanFrame:
    can_id: int
    data: bytes = b""
    extended: bool = False
    remote: bool = False
    fd: bool = False
    bitrate_switch: bool = False
    error_state_indicator: bool = False
    dlc: Optional[int] = None
    error: bool = False

    def __post_init__(self):
        data = bytes(self.data)
        object.__setattr__(self, "data", data)
        dlc = len(data) if self.dlc is None else self.dlc
        object.__setattr__(self, "dlc", dlc)
        maximum_id = (CAN_ERR_MASK if self.error else
                      CAN_EFF_MASK if self.extended else CAN_SFF_MASK)
        if not 0 <= self.can_id <= maximum_id:
            raise ValueError("CAN identifier is outside its format")
        if self.error and (self.extended or self.remote or self.fd):
            raise ValueError("error frame has incompatible flags")
        if self.fd:
            if self.remote:
                raise ValueError("CAN-FD cannot be an RTR frame")
            if dlc != len(data) or dlc not in FD_LENGTHS:
                raise ValueError("illegal CAN-FD payload length")
        else:
            if self.bitrate_switch or self.error_state_indicator:
                raise ValueError("CAN-FD flags on a classic frame")
            if not 0 <= dlc <= 8:
                raise ValueError("classic CAN DLC is outside 0..8")
            if self.remote:
                if data:
                    raise ValueError("RTR frame cannot contain data")
            elif dlc != len(data):
                raise ValueError("classic CAN DLC/data length mismatch")

    @property
    def wire_length(self):
        return self.dlc

    def signature(self):
        """Fields which identify a case independently of its data bytes."""

        return (self.can_id, self.extended, self.remote, self.fd,
                self.bitrate_switch, self.error_state_indicator, self.dlc,
                self.error)

    def encode_socketcan(self):
        can_id = self.can_id
        if self.extended:
            can_id |= CAN_EFF_FLAG
        if self.remote:
            can_id |= CAN_RTR_FLAG
        if self.error:
            can_id |= CAN_ERR_FLAG
        if self.fd:
            flags = (CANFD_FDF |
                     (CANFD_BRS if self.bitrate_switch else 0) |
                     (CANFD_ESI if self.error_state_indicator else 0))
            return CAN_FD_FRAME.pack(
                can_id, self.dlc, flags, 0, 0,
                self.data.ljust(64, b"\0"),
            )
        payload = b"" if self.remote else self.data
        return CLASSIC_FRAME.pack(can_id, self.dlc, payload.ljust(8, b"\0"))

    @classmethod
    def decode_socketcan(cls, value):
        if len(value) == CAN_MTU:
            raw_id, length, payload = CLASSIC_FRAME.unpack(value)
            if length > 8:
                raise ProtocolError("classic CAN frame has invalid length")
            fd = False
            flags = 0
        elif len(value) == CANFD_MTU:
            raw_id, length, flags, reserved0, reserved1, payload = (
                CAN_FD_FRAME.unpack(value)
            )
            if reserved0 or reserved1:
                raise ProtocolError("CAN-FD reserved bytes are nonzero")
            if flags & ~(CANFD_BRS | CANFD_ESI | CANFD_FDF):
                raise ProtocolError("CAN-FD frame has unknown flags")
            if length not in FD_LENGTHS:
                raise ProtocolError("CAN-FD frame has illegal length")
            fd = True
        else:
            raise ProtocolError(
                "SocketCAN datagram is %d bytes, expected 16 or 72" %
                len(value)
            )

        extended = bool(raw_id & CAN_EFF_FLAG)
        remote = bool(raw_id & CAN_RTR_FLAG)
        error = bool(raw_id & CAN_ERR_FLAG)
        if (not extended and not error and
                (raw_id & CAN_EFF_MASK & ~CAN_SFF_MASK)):
            raise ProtocolError("standard CAN frame has upper identifier bits")
        identifier = raw_id & (CAN_ERR_MASK if error else
                               CAN_EFF_MASK if extended else CAN_SFF_MASK)
        if fd and (remote or error):
            raise ProtocolError("CAN-FD datagram has RTR/error flag")
        data = b"" if remote else payload[:length]
        try:
            return cls(
                identifier, data, extended=extended, remote=remote, fd=fd,
                bitrate_switch=bool(flags & CANFD_BRS),
                error_state_indicator=bool(flags & CANFD_ESI), dlc=length,
                error=error,
            )
        except ValueError as exc:
            raise ProtocolError(str(exc)) from exc


@dataclasses.dataclass(frozen=True)
class Control:
    kind: Kind
    direction: Direction
    sequence: int
    session: int


@dataclasses.dataclass(frozen=True)
class Hello:
    session: int
    stress_frames: int
    include_esi: bool = False


@dataclasses.dataclass(frozen=True)
class BoundaryCase:
    name: str
    frame: CanFrame


@dataclasses.dataclass
class ProtocolCounters:
    control_sent: int = 0
    control_received: int = 0
    boundary_sent: int = 0
    boundary_received: int = 0
    stress_sent: int = 0
    stress_received: int = 0
    stress_acked: int = 0
    stale_frames: int = 0
    foreign_frames: int = 0
    duplicate_frames: int = 0
    gap_frames: int = 0
    corrupt_frames: int = 0


def opposite(direction):
    return (Direction.PEER_TO_GUEST
            if direction == Direction.GUEST_TO_PEER
            else Direction.GUEST_TO_PEER)


def session_id(value):
    parsed = int(value, 0)
    if not 0 < parsed <= 0xffffffffffffffff:
        raise argparse.ArgumentTypeError(
            "session must be a nonzero unsigned 64-bit integer"
        )
    return parsed


def positive_int(value):
    parsed = int(value, 0)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def remaining(deadline):
    value = deadline - time.monotonic()
    if value <= 0:
        raise TimeoutError("CAN partner deadline expired")
    return value


def atomic_json(path, value):
    if not path:
        return
    target = pathlib.Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=target.name + ".", suffix=".tmp", dir=target.parent,
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
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


def deterministic_session(interface):
    seed = struct.pack(
        ">QQQ", time.time_ns(), time.monotonic_ns(), os.getpid(),
    )
    seed += interface.encode("utf-8", "surrogateescape")
    return int.from_bytes(hashlib.sha256(seed).digest()[:8], "big") or 1


def deterministic_payload(session, direction, phase, sequence, length,
                          pattern=Pattern.HASH):
    if pattern == Pattern.ZERO:
        return bytes(length)
    if pattern == Pattern.ONES:
        return b"\xff" * length
    if pattern == Pattern.INCREMENTING:
        return bytes((sequence + offset) & 0xff for offset in range(length))
    seed = struct.pack(">QBBI", session, int(direction), phase, sequence)
    result = bytearray()
    block = 0
    while len(result) < length:
        result.extend(hashlib.sha256(seed + struct.pack(">I", block)).digest())
        block += 1
    return bytes(result[:length])


def control_id(kind, direction, sequence):
    if not 0 <= sequence <= CONTROL_SEQUENCE_MASK:
        raise ValueError("control sequence is outside 20 bits")
    return (CONTROL_PREFIX | (int(kind) << CONTROL_KIND_SHIFT) |
            (int(direction) << CONTROL_DIRECTION_SHIFT) | sequence)


def make_control(kind, direction, sequence, session):
    return CanFrame(
        control_id(kind, direction, sequence),
        SESSION_PAYLOAD.pack(session), extended=True,
    )


def hello_sequence(stress_frames, include_esi=False):
    if not 0 < stress_frames <= HELLO_FRAME_MASK:
        raise ValueError("HELLO stress frame count is outside 1..65535")
    if not 0 < PROTOCOL_VERSION <= 7:
        raise ValueError("protocol version is outside three bits")
    return (stress_frames | (HELLO_ESI if include_esi else 0) |
            (PROTOCOL_VERSION << HELLO_VERSION_SHIFT))


def hello_from_control(control):
    if (control.kind != Kind.HELLO or
            control.direction != Direction.GUEST_TO_PEER):
        raise ProtocolError("control is not a guest HELLO")
    version = ((control.sequence & HELLO_VERSION_MASK) >>
               HELLO_VERSION_SHIFT)
    unknown = control.sequence & ~(
        HELLO_FRAME_MASK | HELLO_ESI | HELLO_VERSION_MASK
    )
    frames = control.sequence & HELLO_FRAME_MASK
    if version != PROTOCOL_VERSION:
        raise ProtocolError(
            "unsupported CAN partner protocol version %d" % version,
        )
    if unknown or frames == 0:
        raise ProtocolError("HELLO has invalid configuration bits")
    return Hello(control.session, frames, bool(control.sequence & HELLO_ESI))


def parse_control(frame):
    if (not frame.extended or frame.remote or frame.fd or frame.error or
            frame.dlc != SESSION_PAYLOAD.size or
            (frame.can_id & PROTOCOL_FAMILY_MASK) != CONTROL_PREFIX):
        return None
    kind_value = (frame.can_id >> CONTROL_KIND_SHIFT) & 0xf
    direction_value = (frame.can_id >> CONTROL_DIRECTION_SHIFT) & 1
    if kind_value not in Kind._value2member_map_:
        raise ProtocolError("control frame has unknown kind")
    session, = SESSION_PAYLOAD.unpack(frame.data)
    if session == 0:
        raise ProtocolError("control frame has zero session")
    return Control(
        Kind(kind_value), Direction(direction_value),
        frame.can_id & CONTROL_SEQUENCE_MASK, session,
    )


def session_tag(session):
    value = session
    tag = 0
    for _ in range(8):
        tag ^= value & 0xff
        value >>= 8
    return tag


def stress_id(session, direction, sequence):
    if not 0 <= sequence <= STRESS_SEQUENCE_MASK:
        raise ValueError("stress sequence is outside 16 bits")
    return (STRESS_PREFIX | (int(direction) << STRESS_DIRECTION_SHIFT) |
            (session_tag(session) << STRESS_SESSION_SHIFT) | sequence)


def parse_stress_id(frame):
    if (not frame.extended or frame.remote or frame.error or
            (frame.can_id & PROTOCOL_FAMILY_MASK) != STRESS_PREFIX):
        return None
    return (
        Direction((frame.can_id >> STRESS_DIRECTION_SHIFT) & 1),
        (frame.can_id >> STRESS_SESSION_SHIFT) & 0xff,
        frame.can_id & STRESS_SEQUENCE_MASK,
    )


def make_stress_frame(session, direction, sequence):
    return CanFrame(
        stress_id(session, direction, sequence),
        deterministic_payload(session, direction, 2, sequence, 64),
        extended=True, fd=True, bitrate_switch=True,
    )


def _boundary_payload(session, direction, sequence, length):
    pattern = Pattern(sequence % len(Pattern))
    return deterministic_payload(
        session, direction, 1, sequence, length, pattern,
    )


def boundary_cases(session, direction, include_esi=False):
    """Return the exact classic/RTR/CAN-FD semantic matrix."""

    result = []
    sequence = 0
    identifiers = {
        False: (0, 0x123, CAN_SFF_MASK),
        True: (0, 0x1abcde, CAN_EFF_MASK),
    }

    for extended in (False, True):
        label = "eff" if extended else "sff"
        ids = identifiers[extended]
        for length in range(9):
            frame = CanFrame(
                ids[length % len(ids)],
                _boundary_payload(
                    session, direction, sequence, length,
                ),
                extended=extended,
            )
            result.append(BoundaryCase(
                "classic-%s-length-%d" % (label, length), frame,
            ))
            sequence += 1

    for extended in (False, True):
        label = "eff" if extended else "sff"
        ids = identifiers[extended]
        for index, dlc in enumerate((0, 8)):
            result.append(BoundaryCase(
                "rtr-%s-dlc-%d" % (label, dlc),
                CanFrame(
                    ids[index * 2], extended=extended, remote=True, dlc=dlc,
                ),
            ))
            sequence += 1

    for extended in (False, True):
        label = "eff" if extended else "sff"
        ids = identifiers[extended]
        for bitrate_switch in (False, True):
            for length_index, length in enumerate(FD_LENGTHS):
                result.append(BoundaryCase(
                    "fd-%s-%s-length-%d" % (
                        label, "brs" if bitrate_switch else "nominal",
                        length,
                    ),
                    CanFrame(
                        ids[length_index % len(ids)],
                        _boundary_payload(
                            session, direction, sequence, length,
                        ),
                        extended=extended, fd=True,
                        bitrate_switch=bitrate_switch,
                    ),
                ))
                sequence += 1

    if include_esi and direction == Direction.PEER_TO_GUEST:
        for extended in (False, True):
            label = "eff" if extended else "sff"
            for bitrate_switch in (False, True):
                result.append(BoundaryCase(
                    "fd-%s-%s-esi-length-64" % (
                        label, "brs" if bitrate_switch else "nominal",
                    ),
                    CanFrame(
                        identifiers[extended][1],
                        _boundary_payload(
                            session, direction, sequence, 64,
                        ),
                        extended=extended, fd=True,
                        bitrate_switch=bitrate_switch,
                        error_state_indicator=True,
                    ),
                ))
                sequence += 1
    return tuple(result)


def _empty_transport_stats():
    return {
        "tx_frames": 0,
        "tx_bytes": 0,
        "rx_frames": 0,
        "rx_bytes": 0,
        "tx_classic": 0,
        "tx_fd": 0,
        "rx_classic": 0,
        "rx_fd": 0,
        "tx_brs": 0,
        "rx_brs": 0,
        "tx_rtr": 0,
        "rx_rtr": 0,
        "tx_eff": 0,
        "rx_eff": 0,
        "tx_esi": 0,
        "rx_esi": 0,
        "rx_error_frames": 0,
        "rx_queue_overflows": 0,
        "receive_buffer_bytes": 0,
        "send_retries": 0,
    }


def _account_frame(stats, prefix, frame):
    stats[prefix + "_frames"] += 1
    stats[prefix + "_bytes"] += frame.dlc
    stats[prefix + ("_fd" if frame.fd else "_classic")] += 1
    if frame.bitrate_switch:
        stats[prefix + "_brs"] += 1
    if frame.remote:
        stats[prefix + "_rtr"] += 1
    if frame.extended:
        stats[prefix + "_eff"] += 1
    if frame.error_state_indicator:
        stats[prefix + "_esi"] += 1
    if prefix == "rx" and frame.error:
        stats["rx_error_frames"] += 1


class SocketCanTransport:
    def __init__(self, interface, receive_buffer):
        if not hasattr(socket, "AF_CAN") or not hasattr(socket, "CAN_RAW"):
            raise RuntimeError("this Python/platform does not provide AF_CAN")
        self.interface = interface
        self.stats = _empty_transport_stats()
        self._last_overflow = 0
        self.sock = socket.socket(
            socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW,
        )
        try:
            self.sock.setblocking(False)
            self.sock.setsockopt(
                SOL_CAN_RAW, CAN_RAW_FD_FRAMES, struct.pack("=I", 1),
            )
            self.sock.setsockopt(
                SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                struct.pack("=I", CAN_ERR_MASK),
            )
            self.sock.setsockopt(
                SOL_CAN_RAW, CAN_RAW_LOOPBACK, struct.pack("=I", 1),
            )
            self.sock.setsockopt(
                SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, struct.pack("=I", 0),
            )
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                 receive_buffer)
            self.stats["receive_buffer_bytes"] = self.sock.getsockopt(
                socket.SOL_SOCKET, socket.SO_RCVBUF,
            )
            self.sock.setsockopt(
                socket.SOL_SOCKET, SO_RXQ_OVFL, struct.pack("=I", 1),
            )
            self.sock.bind((interface,))
        except BaseException:
            self.sock.close()
            raise

    def send(self, frame, deadline):
        encoded = frame.encode_socketcan()
        while True:
            remaining(deadline)
            try:
                written = self.sock.send(encoded)
            except BlockingIOError:
                written = 0
            except OSError as exc:
                if exc.errno not in (errno.ENOBUFS, errno.EAGAIN,
                                     errno.EWOULDBLOCK):
                    raise
                written = 0
            if written == len(encoded):
                _account_frame(self.stats, "tx", frame)
                return
            if written:
                raise OSError("partial SocketCAN datagram write")
            self.stats["send_retries"] += 1
            select.select([], [self.sock], [], min(remaining(deadline), 0.01))

    def recv(self, deadline):
        while True:
            readable, _, _ = select.select(
                [self.sock], [], [], remaining(deadline),
            )
            if not readable:
                raise TimeoutError("SocketCAN receive timed out")
            try:
                value, ancillary, flags, _address = self.sock.recvmsg(
                    CANFD_MTU, 128,
                )
            except BlockingIOError:
                continue
            if flags & getattr(socket, "MSG_TRUNC", 0):
                raise ProtocolError("truncated SocketCAN datagram")
            for level, option, data in ancillary:
                if (level == socket.SOL_SOCKET and option == SO_RXQ_OVFL and
                        len(data) >= 4):
                    cumulative, = struct.unpack("=I", data[:4])
                    delta = (cumulative - self._last_overflow) & 0xffffffff
                    self._last_overflow = cumulative
                    self.stats["rx_queue_overflows"] += delta
            frame = CanFrame.decode_socketcan(value)
            _account_frame(self.stats, "rx", frame)
            return frame

    def close(self):
        self.sock.close()


class MemoryTransport:
    """Queue-backed transport used by host-only protocol tests."""

    _CLOSED = object()

    def __init__(self):
        self._incoming = queue.Queue()
        self._other = None
        self._closed = False
        self.stats = _empty_transport_stats()

    @classmethod
    def pair(cls):
        left = cls()
        right = cls()
        left._other = right
        right._other = left
        return left, right

    def send(self, frame, deadline):
        remaining(deadline)
        if self._closed or self._other is None or self._other._closed:
            raise EOFError("memory CAN transport is closed")
        _account_frame(self.stats, "tx", frame)
        self._other._incoming.put(frame.encode_socketcan())

    def recv(self, deadline):
        try:
            value = self._incoming.get(timeout=remaining(deadline))
        except queue.Empty as exc:
            raise TimeoutError("memory CAN receive timed out") from exc
        if value is self._CLOSED:
            raise EOFError("memory CAN transport is closed")
        frame = CanFrame.decode_socketcan(value)
        _account_frame(self.stats, "rx", frame)
        return frame

    def close(self):
        if not self._closed:
            self._closed = True
            self._incoming.put(self._CLOSED)
            if self._other is not None:
                self._other._incoming.put(self._CLOSED)


class Endpoint:
    def __init__(self, transport, role, session, stress_frames,
                 ignore_foreign=False, progress=False, include_esi=False):
        self.transport = transport
        self.role = role
        self.session = session
        self.stress_frames = stress_frames
        self.ignore_foreign = ignore_foreign
        self.progress = progress
        self.include_esi = include_esi
        self.counters = ProtocolCounters()

    @property
    def send_direction(self):
        return (Direction.GUEST_TO_PEER if self.role == "guest"
                else Direction.PEER_TO_GUEST)

    @property
    def receive_direction(self):
        return opposite(self.send_direction)

    def send_control(self, kind, direction, sequence, deadline):
        self.transport.send(
            make_control(kind, direction, sequence, self.session), deadline,
        )
        self.counters.control_sent += 1

    def _new_hello(self, control):
        return (self.role == "peer" and control.kind == Kind.HELLO and
                control.direction == Direction.GUEST_TO_PEER and
                control.session != self.session)

    def handle_foreign(self, description):
        self.counters.foreign_frames += 1
        if not self.ignore_foreign:
            raise ProtocolError(
                "unexpected/foreign CAN frame: %s" % description,
            )

    def receive_control(self, kind, direction, sequence, deadline):
        while True:
            frame = self.transport.recv(deadline)
            if frame.error:
                raise ProtocolError("SocketCAN error frame during protocol")
            control = parse_control(frame)
            if control is None:
                if parse_stress_id(frame) is not None:
                    raise ProtocolError(
                        "stress frame arrived at control barrier",
                    )
                self.handle_foreign("waiting for %s" % kind.name)
                continue
            if self._new_hello(control):
                raise SessionRestart(hello_from_control(control))
            if control.session != self.session:
                self.counters.stale_frames += 1
                continue
            self.counters.control_received += 1
            if (control.kind != kind or control.direction != direction or
                    control.sequence != sequence):
                raise ProtocolError(
                    "expected %s/%s/%d, received %s/%s/%d" % (
                        kind.name, direction.name, sequence,
                        control.kind.name, control.direction.name,
                        control.sequence,
                    )
                )
            return control


def validate_hello(hello, expected_frames, expected_include_esi):
    if hello.stress_frames != expected_frames:
        raise ProtocolError(
            "guest requested %d stress frames, expected %d" %
            (hello.stress_frames, expected_frames)
        )
    if hello.include_esi != expected_include_esi:
        raise ProtocolError(
            "guest/peer --include-esi configuration differs",
        )


def wait_for_hello(transport, expected_frames, deadline, ignore_foreign=False,
                   expected_include_esi=False):
    foreign = 0
    while True:
        frame = transport.recv(deadline)
        if frame.error:
            raise ProtocolError("SocketCAN error frame before HELLO")
        control = parse_control(frame)
        if (control is not None and control.kind == Kind.HELLO and
                control.direction == Direction.GUEST_TO_PEER):
            hello = hello_from_control(control)
            validate_hello(hello, expected_frames, expected_include_esi)
            return hello, foreign
        foreign += 1
        if not ignore_foreign:
            raise ProtocolError("unexpected frame before guest HELLO")


def _protocol_frame_index(cases, frame):
    exact = []
    signatures = []
    signature = frame.signature()
    for index, case in enumerate(cases):
        if case.frame == frame:
            exact.append(index)
        if case.frame.signature() == signature:
            signatures.append(index)
    return exact, signatures


def run_boundary_sender(endpoint, direction, deadline):
    cases = boundary_cases(
        endpoint.session, direction, endpoint.include_esi,
    )
    for sequence, case in enumerate(cases):
        endpoint.transport.send(case.frame, deadline)
        endpoint.counters.boundary_sent += 1
        endpoint.receive_control(
            Kind.BOUNDARY_ACK, opposite(direction), sequence, deadline,
        )
        if endpoint.progress:
            print("# sent boundary %d: %s" % (sequence, case.name),
                  flush=True)
    return len(cases)


def run_boundary_receiver(endpoint, direction, deadline):
    cases = boundary_cases(
        endpoint.session, direction, endpoint.include_esi,
    )
    for expected_sequence, case in enumerate(cases):
        while True:
            frame = endpoint.transport.recv(deadline)
            if frame.error:
                raise ProtocolError("SocketCAN error frame in boundary phase")
            control = parse_control(frame)
            if control is not None:
                if endpoint._new_hello(control):
                    raise SessionRestart(hello_from_control(control))
                if control.session != endpoint.session:
                    endpoint.counters.stale_frames += 1
                    continue
                raise ProtocolError("unexpected control in boundary phase")
            if parse_stress_id(frame) is not None:
                raise ProtocolError("stress frame before READY barrier")
            if frame == case.frame:
                break
            exact, signatures = _protocol_frame_index(cases, frame)
            if any(index < expected_sequence for index in exact):
                endpoint.counters.duplicate_frames += 1
                raise ProtocolError("duplicate boundary frame")
            if any(index > expected_sequence for index in exact):
                endpoint.counters.gap_frames += 1
                raise ProtocolError("boundary frame gap/out-of-order")
            if expected_sequence in signatures:
                endpoint.counters.corrupt_frames += 1
                raise ProtocolError("boundary payload/flag corruption")
            endpoint.handle_foreign("boundary sequence %d" % expected_sequence)
        endpoint.counters.boundary_received += 1
        endpoint.send_control(
            Kind.BOUNDARY_ACK, opposite(direction), expected_sequence,
            deadline,
        )
        if endpoint.progress:
            print("# received boundary %d: %s" %
                  (expected_sequence, case.name), flush=True)
    return len(cases)


class StressReceiver:
    def __init__(self, session, direction, counters=None):
        self.session = session
        self.direction = direction
        self.expected = 0
        self.counters = counters or ProtocolCounters()

    def accept(self, frame):
        parsed = parse_stress_id(frame)
        if parsed is None:
            raise ProtocolError("frame is outside stress namespace")
        direction, tag, sequence = parsed
        if tag != session_tag(self.session):
            self.counters.stale_frames += 1
            return None
        if direction != self.direction:
            raise ProtocolError("stress frame has wrong direction")
        if sequence < self.expected:
            self.counters.duplicate_frames += 1
            raise ProtocolError("duplicate stress frame %d" % sequence)
        if sequence > self.expected:
            self.counters.gap_frames += 1
            raise ProtocolError(
                "stress gap: expected %d, received %d" %
                (self.expected, sequence)
            )
        expected_frame = make_stress_frame(
            self.session, self.direction, sequence,
        )
        if frame != expected_frame:
            self.counters.corrupt_frames += 1
            raise ProtocolError("stress frame %d is corrupt" % sequence)
        self.expected += 1
        self.counters.stress_received += 1
        return sequence


def run_stress(endpoint, count, window, deadline):
    next_send = 0
    outstanding = set()
    receiver = StressReceiver(
        endpoint.session, endpoint.receive_direction, endpoint.counters,
    )

    while (next_send < count or outstanding or receiver.expected < count):
        while next_send < count and len(outstanding) < window:
            endpoint.transport.send(
                make_stress_frame(
                    endpoint.session, endpoint.send_direction, next_send,
                ),
                deadline,
            )
            outstanding.add(next_send)
            endpoint.counters.stress_sent += 1
            next_send += 1

        frame = endpoint.transport.recv(deadline)
        if frame.error:
            raise ProtocolError("SocketCAN error frame during stress")
        control = parse_control(frame)
        if control is not None:
            if endpoint._new_hello(control):
                raise SessionRestart(hello_from_control(control))
            if control.session != endpoint.session:
                endpoint.counters.stale_frames += 1
                continue
            endpoint.counters.control_received += 1
            if (control.kind != Kind.STRESS_ACK or
                    control.direction != endpoint.receive_direction):
                raise ProtocolError("unexpected control during stress")
            sequence = control.sequence
            if sequence not in outstanding:
                if sequence < next_send:
                    endpoint.counters.duplicate_frames += 1
                    raise ProtocolError("duplicate stress ACK %d" % sequence)
                endpoint.counters.gap_frames += 1
                raise ProtocolError("future stress ACK %d" % sequence)
            outstanding.remove(sequence)
            endpoint.counters.stress_acked += 1
            continue

        parsed = parse_stress_id(frame)
        if parsed is None:
            endpoint.handle_foreign("stress phase")
            continue
        sequence = receiver.accept(frame)
        if sequence is None:
            continue
        endpoint.send_control(
            Kind.STRESS_ACK, endpoint.send_direction, sequence, deadline,
        )
        if endpoint.progress and sequence % 1000 == 0:
            print("# stress received through %d" % sequence, flush=True)

    if endpoint.counters.stress_acked != count:
        raise ProtocolError("not every transmitted stress frame was ACKed")
    return count


def _session_report(endpoint, started):
    return {
        "session": "0x%016x" % endpoint.session,
        "duration_seconds": round(time.monotonic() - started, 6),
        "boundary_cases_sent": endpoint.counters.boundary_sent,
        "boundary_cases_received": endpoint.counters.boundary_received,
        "stress_frames_sent": endpoint.counters.stress_sent,
        "stress_frames_received": endpoint.counters.stress_received,
        "stress_frames_acked": endpoint.counters.stress_acked,
        "protocol_counters": dataclasses.asdict(endpoint.counters),
    }


def _wait_for_resume(ready_file, resume_file, endpoint, deadline):
    if not ready_file:
        return False
    atomic_json(ready_file, {
        "ready": True,
        "session": "0x%016x" % endpoint.session,
        "phase": "boundary-complete-stress-not-started",
        "pid": os.getpid(),
    })
    while not pathlib.Path(resume_file).exists():
        time.sleep(min(0.05, remaining(deadline)))
    return True


def attest_local_validation(endpoint, validation, deadline):
    if validation is None:
        return
    try:
        validation()
    except BaseException:
        try:
            endpoint.send_control(
                Kind.ERROR, endpoint.send_direction, 1, deadline,
            )
        except BaseException:
            pass
        raise


def run_guest_protocol(transport, session, stress_frames, window, deadline,
                       ignore_foreign=False, progress=False,
                       include_esi=False, validation=None):
    endpoint = Endpoint(
        transport, "guest", session, stress_frames, ignore_foreign, progress,
        include_esi,
    )
    started = time.monotonic()
    endpoint.send_control(
        Kind.HELLO, Direction.GUEST_TO_PEER,
        hello_sequence(stress_frames, include_esi), deadline,
    )
    endpoint.receive_control(
        Kind.HELLO_ACK, Direction.PEER_TO_GUEST,
        hello_sequence(stress_frames, include_esi), deadline,
    )
    run_boundary_sender(endpoint, Direction.GUEST_TO_PEER, deadline)
    run_boundary_receiver(endpoint, Direction.PEER_TO_GUEST, deadline)
    endpoint.send_control(Kind.READY, Direction.GUEST_TO_PEER, 0, deadline)
    endpoint.receive_control(Kind.READY, Direction.PEER_TO_GUEST, 0, deadline)
    run_stress(endpoint, stress_frames, window, deadline)
    endpoint.send_control(
        Kind.DONE, Direction.GUEST_TO_PEER, stress_frames, deadline,
    )
    endpoint.receive_control(
        Kind.DONE_ACK, Direction.PEER_TO_GUEST, stress_frames, deadline,
    )
    endpoint.receive_control(
        Kind.DONE, Direction.PEER_TO_GUEST, stress_frames, deadline,
    )
    endpoint.send_control(
        Kind.DONE_ACK, Direction.GUEST_TO_PEER, stress_frames, deadline,
    )
    endpoint.receive_control(
        Kind.COMPLETE, Direction.PEER_TO_GUEST, stress_frames, deadline,
    )
    attest_local_validation(endpoint, validation, deadline)
    endpoint.send_control(
        Kind.COMPLETE_ACK, Direction.GUEST_TO_PEER, stress_frames, deadline,
    )
    return _session_report(endpoint, started)


def run_peer_protocol(transport, hello, window, deadline,
                      ignore_foreign=False, progress=False,
                      barrier_ready_file=None, resume_file=None,
                      validation=None):
    endpoint = Endpoint(
        transport, "peer", hello.session, hello.stress_frames,
        ignore_foreign, progress, hello.include_esi,
    )
    started = time.monotonic()
    endpoint.counters.control_received += 1  # HELLO consumed by caller.
    endpoint.send_control(
        Kind.HELLO_ACK, Direction.PEER_TO_GUEST,
        hello_sequence(hello.stress_frames, hello.include_esi), deadline,
    )
    run_boundary_receiver(endpoint, Direction.GUEST_TO_PEER, deadline)
    run_boundary_sender(endpoint, Direction.PEER_TO_GUEST, deadline)
    endpoint.receive_control(Kind.READY, Direction.GUEST_TO_PEER, 0, deadline)
    barrier_used = _wait_for_resume(
        barrier_ready_file, resume_file, endpoint, deadline,
    )
    endpoint.send_control(Kind.READY, Direction.PEER_TO_GUEST, 0, deadline)
    run_stress(endpoint, hello.stress_frames, window, deadline)
    endpoint.receive_control(
        Kind.DONE, Direction.GUEST_TO_PEER, hello.stress_frames, deadline,
    )
    endpoint.send_control(
        Kind.DONE_ACK, Direction.PEER_TO_GUEST,
        hello.stress_frames, deadline,
    )
    endpoint.send_control(
        Kind.DONE, Direction.PEER_TO_GUEST, hello.stress_frames, deadline,
    )
    endpoint.receive_control(
        Kind.DONE_ACK, Direction.GUEST_TO_PEER,
        hello.stress_frames, deadline,
    )
    attest_local_validation(endpoint, validation, deadline)
    endpoint.send_control(
        Kind.COMPLETE, Direction.PEER_TO_GUEST,
        hello.stress_frames, deadline,
    )
    endpoint.receive_control(
        Kind.COMPLETE_ACK, Direction.GUEST_TO_PEER,
        hello.stress_frames, deadline,
    )
    result = _session_report(endpoint, started)
    result["migration_barrier_used"] = barrier_used
    return result


def _run_command(command, check=True):
    environment = os.environ.copy()
    environment["PATH"] = DEFAULT_PATH
    result = subprocess.run(
        command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=environment, check=False,
    )
    if check and result.returncode:
        raise RuntimeError(
            "%s failed (%d): %s" %
            (" ".join(command), result.returncode, result.stdout.strip())
        )
    return result.stdout.strip()


def configure_guest_interface(interface, bitrate, data_bitrate, txqueuelen):
    if shutil.which("ip", path=DEFAULT_PATH) is None:
        raise RuntimeError("iproute2 'ip' is required for --configure")
    _run_command(["ip", "link", "set", "dev", interface, "down"])
    _run_command([
        "ip", "link", "set", "dev", interface, "type", "can",
        "bitrate", str(bitrate), "dbitrate", str(data_bitrate),
        "fd", "on", "berr-reporting", "on",
    ])
    _run_command([
        "ip", "link", "set", "dev", interface, "txqueuelen",
        str(txqueuelen),
    ])
    _run_command(["ip", "link", "set", "dev", interface, "up"])


def interrupt_count(interface):
    try:
        lines = pathlib.Path("/proc/interrupts").read_text(
            encoding="ascii", errors="replace",
        ).splitlines()
    except OSError:
        return None
    total = 0
    found = False
    pattern = re.compile(r"(?:^|\s)%s(?:\s|$)" % re.escape(interface))
    for line in lines:
        if ":" not in line or not pattern.search(line):
            continue
        found = True
        for token in line.split(":", 1)[1].split():
            if not token.isdecimal():
                break
            total += int(token)
    return total if found else None


def interface_snapshot(interface):
    root = pathlib.Path("/sys/class/net") / interface
    if not root.exists():
        raise RuntimeError("network interface does not exist: %s" % interface)
    statistics = {}
    for name in (
            "rx_packets", "tx_packets", "rx_bytes", "tx_bytes",
            "rx_errors", "tx_errors", "rx_dropped", "tx_dropped"):
        statistics[name] = int(
            (root / "statistics" / name).read_text(encoding="ascii").strip()
        )
    details = None
    if shutil.which("ip", path=DEFAULT_PATH):
        details = _run_command(
            ["ip", "-details", "-statistics", "link", "show", interface],
            check=False,
        )
    return {
        "statistics": statistics,
        "ip_details": details,
        "interrupt_count": interrupt_count(interface),
    }


def counter_delta(before, after):
    return {
        name: after["statistics"][name] - before["statistics"][name]
        for name in before["statistics"]
    }


def validate_kernel_and_transport_counters(transport, before, after,
                                           require_error_active=False,
                                           require_interrupts=False):
    delta = counter_delta(before, after)
    if delta["rx_packets"] <= 0 or delta["tx_packets"] <= 0:
        raise RuntimeError("CAN RX/TX packet counters did not both advance")
    failed = ("rx_errors", "tx_errors", "rx_dropped", "tx_dropped")
    changed = {name: delta[name] for name in failed if delta[name]}
    if changed:
        raise RuntimeError("CAN error/drop counters advanced: %s" % changed)
    if transport.stats["rx_queue_overflows"]:
        raise RuntimeError(
            "SocketCAN receive queue overflowed by %d frames" %
            transport.stats["rx_queue_overflows"]
        )
    if require_error_active:
        details = after.get("ip_details") or ""
        if not re.search(r"\bstate ERROR-ACTIVE\b", details):
            raise RuntimeError("CAN controller is not ERROR-ACTIVE")
    if require_interrupts:
        before_irq = before.get("interrupt_count")
        after_irq = after.get("interrupt_count")
        if before_irq is None or after_irq is None:
            raise RuntimeError("CAN interrupt counter is unavailable")
        if after_irq <= before_irq:
            raise RuntimeError("CAN interrupt counter did not advance")
    return delta


def protocol_totals(sessions):
    result = dataclasses.asdict(ProtocolCounters())
    for session in sessions:
        for name, value in session["protocol_counters"].items():
            result[name] += value
    return result


def emit_tap(report):
    totals = report.get("protocol_totals", {})
    transport = report.get("transport", {})
    required = report.get("required_sessions", 1)
    completed = len(report.get("sessions", []))
    integrity = bool(report.get("sessions")) and all(
        session["stress_frames_sent"] == report["stress_frames"] and
        session["stress_frames_received"] == report["stress_frames"] and
        session["stress_frames_acked"] == report["stress_frames"] and
        session["boundary_cases_sent"] == report["boundary_cases_sent"] and
        session["boundary_cases_received"] ==
        report["boundary_cases_received"]
        for session in report.get("sessions", [])
    )
    semantic_errors = sum(totals.get(name, 0) for name in (
        "duplicate_frames", "gap_frames", "corrupt_frames",
    ))
    checks = [
        (completed == required,
         "required CAN sessions completed"),
        (integrity,
         "classic, RTR, CAN-FD and full-duplex integrity counts"),
        (semantic_errors == 0,
         "no duplicate, gap or corrupt protocol frames"),
        (transport.get("rx_queue_overflows", 0) == 0,
         "SocketCAN receive queue did not overflow"),
        (bool(report.get("success")), "overall CAN partner result"),
    ]
    print("TAP version 13")
    print("1..%d" % len(checks))
    for number, (passed, description) in enumerate(checks, 1):
        print("%s %d - %s" %
              ("ok" if passed else "not ok", number, description))
    if report.get("error"):
        for number, line in enumerate(str(report["error"]).splitlines()):
            print("# %s%s" % ("error: " if number == 0 else "  ", line))


def _base_report(args):
    return {
        "schema": REPORT_SCHEMA,
        "protocol_version": PROTOCOL_VERSION,
        "role": args.role,
        "interface": args.interface,
        "success": False,
        "started_unix": int(time.time()),
        "required_sessions": args.sessions if args.role == "peer" else 1,
        "stress_frames": args.frames,
        "include_esi": args.include_esi,
        "window": args.window,
        "boundary_cases_sent": len(boundary_cases(
            1, Direction.GUEST_TO_PEER if args.role == "guest"
            else Direction.PEER_TO_GUEST,
            args.include_esi,
        )),
        "boundary_cases_received": len(boundary_cases(
            1, Direction.PEER_TO_GUEST if args.role == "guest"
            else Direction.GUEST_TO_PEER,
            args.include_esi,
        )),
        "sessions": [],
        "aborted_sessions": [],
    }


def guest_main(args):
    report = _base_report(args)
    transport = None
    try:
        if args.configure:
            configure_guest_interface(
                args.interface, args.bitrate, args.data_bitrate,
                args.txqueuelen,
            )
        before = interface_snapshot(args.interface)
        transport = SocketCanTransport(args.interface, args.receive_buffer)
        session = args.session or deterministic_session(args.interface)
        deadline = time.monotonic() + args.timeout

        def validate_local():
            after = interface_snapshot(args.interface)
            report["interface_before"] = before
            report["interface_after"] = after
            report["interface_delta"] = validate_kernel_and_transport_counters(
                transport, before, after,
                require_error_active=True, require_interrupts=True,
            )
            report["local_validation_attested"] = True

        report["sessions"].append(run_guest_protocol(
            transport, session, args.frames, args.window, deadline,
            args.ignore_foreign, args.progress, args.include_esi,
            validation=validate_local,
        ))
        report["success"] = True
        return 0
    except BaseException as exc:
        report["error"] = "%s: %s" % (type(exc).__name__, exc)
        return 1
    finally:
        report["transport"] = (
            dict(transport.stats) if transport is not None
            else _empty_transport_stats()
        )
        report["protocol_totals"] = protocol_totals(report["sessions"])
        report["finished_unix"] = int(time.time())
        if transport is not None:
            transport.close()
        emit_tap(report)
        atomic_json(args.json, report)


def peer_main(args):
    report = _base_report(args)
    transport = None
    seen_sessions = set()
    pending = None
    try:
        if args.barrier_ready_file:
            if os.path.lexists(args.barrier_ready_file):
                raise RuntimeError("barrier ready file already exists")
            if os.path.lexists(args.resume_file):
                raise RuntimeError("resume file already exists")
        before = interface_snapshot(args.interface)
        transport = SocketCanTransport(args.interface, args.receive_buffer)
        deadline = time.monotonic() + args.timeout

        def validate_local():
            after = interface_snapshot(args.interface)
            report["interface_before"] = before
            report["interface_after"] = after
            report["interface_delta"] = validate_kernel_and_transport_counters(
                transport, before, after,
            )
            report["local_validation_attested"] = True

        while len(report["sessions"]) < args.sessions:
            if pending is None:
                hello, foreign = wait_for_hello(
                    transport, args.frames, deadline, args.ignore_foreign,
                    args.include_esi,
                )
                if foreign:
                    report.setdefault("pre_hello_foreign_frames", 0)
                    report["pre_hello_foreign_frames"] += foreign
            else:
                hello = pending
                pending = None
                validate_hello(hello, args.frames, args.include_esi)
            if hello.session in seen_sessions:
                raise ProtocolError("guest reused a completed session")
            try:
                value = run_peer_protocol(
                    transport, hello, args.window, deadline,
                    args.ignore_foreign, args.progress,
                    args.barrier_ready_file, args.resume_file,
                    validation=validate_local,
                )
            except SessionRestart as restart:
                report["aborted_sessions"].append({
                    "session": "0x%016x" % hello.session,
                    "reason": str(restart),
                })
                pending = restart.hello
                continue
            validate_local()
            report["local_post_completion_validation"] = True
            seen_sessions.add(hello.session)
            report["sessions"].append(value)
        report["success"] = True
        return 0
    except BaseException as exc:
        report["error"] = "%s: %s" % (type(exc).__name__, exc)
        return 1
    finally:
        report["transport"] = (
            dict(transport.stats) if transport is not None
            else _empty_transport_stats()
        )
        report["protocol_totals"] = protocol_totals(report["sessions"])
        report["finished_unix"] = int(time.time())
        if transport is not None:
            transport.close()
        emit_tap(report)
        atomic_json(args.json, report)


def common_arguments(parser):
    parser.add_argument("--interface", required=True,
                        help="SocketCAN interface (for example can0/vcan0)")
    parser.add_argument(
        "--frames", type=positive_int, default=1000,
        help="full-duplex 64-byte FD frames per direction (default: 1000)",
    )
    parser.add_argument(
        "--window", type=positive_int, default=16,
        help="maximum unacknowledged stress frames (default: 16)",
    )
    parser.add_argument("--timeout", type=float, default=300.0,
                        help="overall deadline in seconds")
    parser.add_argument(
        "--receive-buffer", type=positive_int, default=4 * 1024 * 1024,
        help="requested SocketCAN SO_RCVBUF bytes",
    )
    parser.add_argument(
        "--ignore-foreign", action="store_true",
        help="count and discard unrelated frames (isolated gate omits this)",
    )
    parser.add_argument(
        "--include-esi", action="store_true",
        help="add supplemental host-to-guest CAN-FD ESI cases",
    )
    parser.add_argument("--progress", action="store_true")
    parser.add_argument("--json", metavar="FILE")


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="role", required=True)

    peer = subparsers.add_parser("peer", help="QEMU-host/board CAN peer")
    common_arguments(peer)
    peer.add_argument(
        "--sessions", type=positive_int, default=1,
        help="completed guest sessions to require (reset hook)",
    )
    peer.add_argument("--barrier-ready-file", metavar="FILE")
    peer.add_argument("--resume-file", metavar="FILE")
    peer.set_defaults(function=peer_main)

    guest = subparsers.add_parser("guest", help="Linux4Microchip M_CAN side")
    common_arguments(guest)
    guest.add_argument("--session", type=session_id)
    guest.add_argument(
        "--configure", action="store_true",
        help="configure and bring up the real CAN interface with iproute2",
    )
    guest.add_argument("--bitrate", type=positive_int, default=500000)
    guest.add_argument("--data-bitrate", type=positive_int, default=2000000)
    guest.add_argument("--txqueuelen", type=positive_int, default=4096)
    guest.set_defaults(function=guest_main, sessions=1,
                       barrier_ready_file=None, resume_file=None)
    return parser


def validate_args(parser, args):
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.frames > STRESS_SEQUENCE_MASK:
        parser.error("--frames cannot exceed 65535")
    if args.window > args.frames:
        parser.error("--window cannot exceed --frames")
    if args.role == "peer":
        barrier_values = bool(args.barrier_ready_file), bool(args.resume_file)
        if barrier_values[0] != barrier_values[1]:
            parser.error(
                "--barrier-ready-file and --resume-file require each other"
            )
        if args.barrier_ready_file and args.sessions != 1:
            parser.error("migration barrier currently requires --sessions 1")
        if (args.barrier_ready_file and
                os.path.abspath(args.barrier_ready_file) ==
                os.path.abspath(args.resume_file)):
            parser.error("barrier ready and resume paths must be different")


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    validate_args(parser, args)
    return args.function(args)


if __name__ == "__main__":
    sys.exit(main())
