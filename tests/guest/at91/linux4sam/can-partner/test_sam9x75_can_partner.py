#!/usr/bin/env python3
"""Host-only tests for the deterministic SAM9X75 CAN partner."""

# SPDX-License-Identifier: GPL-2.0-or-later

import dataclasses
import json
import pathlib
import queue
import tempfile
import threading
import time
import unittest
from unittest import mock

import sam9x75_can_partner as partner


class CanFrameTests(unittest.TestCase):
    def test_socketcan_round_trip_for_supported_formats(self):
        frames = (
            partner.CanFrame(0x123, b"\x01\x02\x03"),
            partner.CanFrame(
                partner.CAN_EFF_MASK, b"abcdefgh", extended=True,
            ),
            partner.CanFrame(0, remote=True, dlc=0),
            partner.CanFrame(
                partner.CAN_SFF_MASK, extended=False, remote=True, dlc=8,
            ),
            partner.CanFrame(
                0x123, bytes(range(12)), fd=True,
            ),
            partner.CanFrame(
                0x1234567, bytes(range(64)), extended=True, fd=True,
                bitrate_switch=True, error_state_indicator=True,
            ),
            partner.CanFrame(1, error=True),
            partner.CanFrame(
                partner.CAN_ERR_MASK, bytes(range(8)), error=True,
            ),
        )
        for frame in frames:
            with self.subTest(frame=frame):
                encoded = frame.encode_socketcan()
                expected_mtu = (
                    partner.CANFD_MTU if frame.fd else partner.CAN_MTU
                )
                self.assertEqual(len(encoded), expected_mtu)
                if frame.fd:
                    self.assertTrue(encoded[5] & partner.CANFD_FDF)
                self.assertEqual(
                    partner.CanFrame.decode_socketcan(encoded), frame,
                )

    def test_linux_canfd_fdf_receive_marker_is_accepted(self):
        encoded = bytearray(
            partner.CanFrame(0x123, bytes(range(12)), fd=True).encode_socketcan()
        )
        encoded[5] = partner.CANFD_FDF
        decoded = partner.CanFrame.decode_socketcan(encoded)
        self.assertTrue(decoded.fd)
        self.assertFalse(decoded.bitrate_switch)
        self.assertFalse(decoded.error_state_indicator)

    def test_illegal_frame_combinations_are_rejected(self):
        constructors = (
            lambda: partner.CanFrame(0x800),
            lambda: partner.CanFrame(0, b"x", remote=True),
            lambda: partner.CanFrame(0, b"123456789"),
            lambda: partner.CanFrame(0, bytes(10), fd=True),
            lambda: partner.CanFrame(0, b"", bitrate_switch=True),
            lambda: partner.CanFrame(0, b"", fd=True, remote=True),
        )
        for constructor in constructors:
            with self.subTest(constructor=constructor):
                with self.assertRaises(ValueError):
                    constructor()

    def test_malformed_socketcan_datagrams_are_rejected(self):
        classic = bytearray(partner.CanFrame(1, b"x").encode_socketcan())
        classic[4] = 9
        with self.assertRaisesRegex(partner.ProtocolError, "invalid length"):
            partner.CanFrame.decode_socketcan(classic)
        upper_sff = partner.CLASSIC_FRAME.pack(
            partner.CAN_SFF_MASK + 1, 1, b"x".ljust(8, b"\0"),
        )
        with self.assertRaisesRegex(partner.ProtocolError, "upper identifier"):
            partner.CanFrame.decode_socketcan(upper_sff)

        fd = bytearray(
            partner.CanFrame(1, bytes(12), fd=True).encode_socketcan()
        )
        fd[4] = 10
        with self.assertRaisesRegex(partner.ProtocolError, "illegal length"):
            partner.CanFrame.decode_socketcan(fd)
        fd[4] = 12
        fd[5] = 0x80
        with self.assertRaisesRegex(partner.ProtocolError, "unknown flags"):
            partner.CanFrame.decode_socketcan(fd)
        fd[5] = 0x10  # QEMU's internal FD marker must never leak to Linux.
        with self.assertRaisesRegex(partner.ProtocolError, "unknown flags"):
            partner.CanFrame.decode_socketcan(fd)

        with self.assertRaisesRegex(partner.ProtocolError, "16 or 72"):
            partner.CanFrame.decode_socketcan(b"short")


class ProtocolEncodingTests(unittest.TestCase):
    def test_control_round_trip_and_namespaces(self):
        session = 0x123456789abcdef0
        for kind in partner.Kind:
            for direction in partner.Direction:
                frame = partner.make_control(kind, direction, 0xabcde, session)
                control = partner.parse_control(frame)
                self.assertEqual(control.kind, kind)
                self.assertEqual(control.direction, direction)
                self.assertEqual(control.sequence, 0xabcde)
                self.assertEqual(control.session, session)
                self.assertIsNone(partner.parse_stress_id(frame))

    def test_stress_identifier_and_payload_are_deterministic(self):
        session = 0x1020304050607080
        for direction in partner.Direction:
            for sequence in (0, 1, 0x1234, 0xffff):
                with self.subTest(direction=direction, sequence=sequence):
                    frame = partner.make_stress_frame(
                        session, direction, sequence,
                    )
                    parsed = partner.parse_stress_id(frame)
                    self.assertEqual(parsed, (
                        direction, partner.session_tag(session), sequence,
                    ))
                    self.assertIsNone(partner.parse_control(frame))
                    self.assertEqual(
                        frame,
                        partner.make_stress_frame(
                            session, direction, sequence,
                        ),
                    )

    def test_zero_control_session_is_rejected(self):
        frame = partner.CanFrame(
            partner.control_id(
                partner.Kind.HELLO, partner.Direction.GUEST_TO_PEER, 1,
            ),
            bytes(8), extended=True,
        )
        with self.assertRaisesRegex(partner.ProtocolError, "zero session"):
            partner.parse_control(frame)

    def test_hello_carries_version_count_and_supplemental_options(self):
        session = 0x55
        sequence = partner.hello_sequence(65535, include_esi=True)
        control = partner.parse_control(partner.make_control(
            partner.Kind.HELLO, partner.Direction.GUEST_TO_PEER,
            sequence, session,
        ))
        self.assertEqual(
            partner.hello_from_control(control),
            partner.Hello(session, 65535, True),
        )

        unsupported = dataclasses.replace(
            control,
            sequence=(sequence & ~partner.HELLO_VERSION_MASK),
        )
        with self.assertRaisesRegex(partner.ProtocolError, "version"):
            partner.hello_from_control(unsupported)


class BoundaryMatrixTests(unittest.TestCase):
    def test_matrix_covers_classic_rtr_fd_brs_and_esi(self):
        session = 0x1111222233334444
        for direction in partner.Direction:
            cases = partner.boundary_cases(session, direction)
            self.assertEqual(len(cases), 86)
            frames = [case.frame for case in cases]
            self.assertEqual(sum(not frame.fd and not frame.remote
                                 for frame in frames), 18)
            self.assertEqual(sum(frame.remote for frame in frames), 4)
            self.assertEqual(sum(frame.fd for frame in frames), 64)
            self.assertEqual(sum(frame.fd and frame.bitrate_switch
                                 for frame in frames), 32)
            self.assertEqual(
                sum(frame.error_state_indicator for frame in frames), 0,
            )
            self.assertEqual(
                {frame.dlc for frame in frames if frame.fd},
                set(partner.FD_LENGTHS),
            )
            self.assertIn(0, {frame.can_id for frame in frames})
            self.assertIn(
                partner.CAN_SFF_MASK,
                {frame.can_id for frame in frames if not frame.extended},
            )
            self.assertIn(
                partner.CAN_EFF_MASK,
                {frame.can_id for frame in frames if frame.extended},
            )

        supplemental = partner.boundary_cases(
            session, partner.Direction.PEER_TO_GUEST, include_esi=True,
        )
        self.assertEqual(len(supplemental), 90)
        self.assertEqual(
            sum(case.frame.error_state_indicator for case in supplemental), 4,
        )
        self.assertEqual(
            {(case.frame.extended, case.frame.bitrate_switch)
             for case in supplemental
             if case.frame.error_state_indicator},
            {(False, False), (False, True), (True, False), (True, True)},
        )

    def test_patterns_are_stable_and_distinct(self):
        values = [
            partner.deterministic_payload(
                1, partner.Direction.GUEST_TO_PEER, 1, 9, 32, pattern,
            )
            for pattern in partner.Pattern
        ]
        self.assertEqual(values[partner.Pattern.ZERO], bytes(32))
        self.assertEqual(values[partner.Pattern.ONES], b"\xff" * 32)
        self.assertEqual(
            values[partner.Pattern.INCREMENTING],
            bytes(range(9, 9 + 32)),
        )
        self.assertEqual(len(set(values)), len(partner.Pattern))


class IntegrityOracleTests(unittest.TestCase):
    def test_stress_receiver_detects_duplicate(self):
        receiver = partner.StressReceiver(
            1, partner.Direction.GUEST_TO_PEER,
        )
        frame = partner.make_stress_frame(
            1, partner.Direction.GUEST_TO_PEER, 0,
        )
        self.assertEqual(receiver.accept(frame), 0)
        with self.assertRaisesRegex(partner.ProtocolError, "duplicate"):
            receiver.accept(frame)
        self.assertEqual(receiver.counters.duplicate_frames, 1)

    def test_stress_receiver_detects_gap(self):
        receiver = partner.StressReceiver(
            1, partner.Direction.PEER_TO_GUEST,
        )
        frame = partner.make_stress_frame(
            1, partner.Direction.PEER_TO_GUEST, 1,
        )
        with self.assertRaisesRegex(partner.ProtocolError, "gap"):
            receiver.accept(frame)
        self.assertEqual(receiver.counters.gap_frames, 1)

    def test_stress_receiver_detects_payload_corruption(self):
        receiver = partner.StressReceiver(
            1, partner.Direction.GUEST_TO_PEER,
        )
        frame = partner.make_stress_frame(
            1, partner.Direction.GUEST_TO_PEER, 0,
        )
        damaged = dataclasses.replace(
            frame, data=bytes([frame.data[0] ^ 1]) + frame.data[1:],
        )
        with self.assertRaisesRegex(partner.ProtocolError, "corrupt"):
            receiver.accept(damaged)
        self.assertEqual(receiver.counters.corrupt_frames, 1)

    def test_stale_stress_session_is_discarded(self):
        receiver = partner.StressReceiver(
            0x0102030405060708, partner.Direction.GUEST_TO_PEER,
        )
        other = 0x1111111111111111
        self.assertNotEqual(
            partner.session_tag(receiver.session), partner.session_tag(other),
        )
        frame = partner.make_stress_frame(
            other, partner.Direction.GUEST_TO_PEER, 0,
        )
        self.assertIsNone(receiver.accept(frame))
        self.assertEqual(receiver.expected, 0)
        self.assertEqual(receiver.counters.stale_frames, 1)


class EndToEndTests(unittest.TestCase):
    def run_pair(self, frames=64, barrier=None, include_esi=True,
                 validation_order=None):
        guest_transport, peer_transport = partner.MemoryTransport.pair()
        deadline = time.monotonic() + 15
        session = 0x123456789abcdef0
        results = queue.Queue()

        def peer_endpoint():
            try:
                hello, foreign = partner.wait_for_hello(
                    peer_transport, frames, deadline,
                    expected_include_esi=include_esi,
                )
                self.assertEqual(foreign, 0)
                value = partner.run_peer_protocol(
                    peer_transport, hello, 8, deadline,
                    barrier_ready_file=(barrier[0] if barrier else None),
                    resume_file=(barrier[1] if barrier else None),
                    validation=(
                        (lambda: validation_order.append("peer"))
                        if validation_order is not None else None
                    ),
                )
                results.put(("peer", value, None))
            except BaseException as exc:
                results.put(("peer", None, exc))

        thread = threading.Thread(target=peer_endpoint)
        thread.start()
        try:
            guest_value = partner.run_guest_protocol(
                guest_transport, session, frames, 8, deadline,
                include_esi=include_esi,
                validation=(
                    (lambda: validation_order.append("guest"))
                    if validation_order is not None else None
                ),
            )
            results.put(("guest", guest_value, None))
            thread.join(15)
        finally:
            guest_transport.close()
            peer_transport.close()
        self.assertFalse(thread.is_alive())
        values = {}
        while not results.empty():
            name, value, error = results.get_nowait()
            if error:
                self.fail("%s endpoint failed: %s" % (name, error))
            values[name] = value
        self.assertEqual(set(values), {"guest", "peer"})
        return values, guest_transport, peer_transport

    def test_local_validation_is_attested_in_protocol_order(self):
        order = []
        self.run_pair(8, include_esi=False, validation_order=order)
        self.assertEqual(order, ["peer", "guest"])

    def test_complete_boundary_and_full_duplex_session(self):
        values, guest_transport, peer_transport = self.run_pair(128)
        self.assertEqual(values["guest"]["boundary_cases_sent"], 86)
        self.assertEqual(values["guest"]["boundary_cases_received"], 90)
        self.assertEqual(values["peer"]["boundary_cases_sent"], 90)
        self.assertEqual(values["peer"]["boundary_cases_received"], 86)
        for value in values.values():
            self.assertEqual(value["stress_frames_sent"], 128)
            self.assertEqual(value["stress_frames_received"], 128)
            self.assertEqual(value["stress_frames_acked"], 128)
            counters = value["protocol_counters"]
            self.assertEqual(counters["duplicate_frames"], 0)
            self.assertEqual(counters["gap_frames"], 0)
            self.assertEqual(counters["corrupt_frames"], 0)
        for transport in (guest_transport, peer_transport):
            self.assertGreater(transport.stats["tx_classic"], 0)
            self.assertGreater(transport.stats["tx_fd"], 0)
            self.assertGreater(transport.stats["tx_rtr"], 0)
            self.assertGreater(transport.stats["tx_brs"], 0)

    def test_quiescent_migration_barrier(self):
        with tempfile.TemporaryDirectory() as directory:
            ready = pathlib.Path(directory) / "ready.json"
            resume = pathlib.Path(directory) / "resume"

            # The normal pair helper blocks in the guest while the peer waits,
            # so a small coordinator creates the resume marker after READY.
            errors = queue.Queue()

            def coordinator():
                try:
                    deadline = time.monotonic() + 10
                    while not ready.exists():
                        if time.monotonic() >= deadline:
                            raise TimeoutError(
                                "barrier ready file not written",
                            )
                        time.sleep(0.01)
                    content = json.loads(ready.read_text(encoding="utf-8"))
                    self.assertEqual(
                        content["phase"],
                        "boundary-complete-stress-not-started",
                    )
                    resume.write_text("resume\n", encoding="ascii")
                except BaseException as exc:
                    errors.put(exc)

            thread = threading.Thread(target=coordinator)
            thread.start()
            values, _guest, _peer = self.run_pair(
                16, (str(ready), str(resume)),
            )
            thread.join(10)
            self.assertFalse(thread.is_alive())
            if not errors.empty():
                raise errors.get_nowait()
            self.assertTrue(values["peer"]["migration_barrier_used"])

    def test_two_quiescent_reset_sessions(self):
        guest_transport, peer_transport = partner.MemoryTransport.pair()
        deadline = time.monotonic() + 20
        frames = 16
        results = queue.Queue()

        def peer_server():
            try:
                sessions = []
                for _index in range(2):
                    hello, _foreign = partner.wait_for_hello(
                        peer_transport, frames, deadline,
                    )
                    sessions.append(partner.run_peer_protocol(
                        peer_transport, hello, 4, deadline,
                    ))
                results.put((sessions, None))
            except BaseException as exc:
                results.put((None, exc))

        thread = threading.Thread(target=peer_server)
        thread.start()
        try:
            first = partner.run_guest_protocol(
                guest_transport, 0x1001, frames, 4, deadline,
            )
            second = partner.run_guest_protocol(
                guest_transport, 0x1002, frames, 4, deadline,
            )
            thread.join(20)
        finally:
            guest_transport.close()
            peer_transport.close()
        self.assertFalse(thread.is_alive())
        peer_sessions, error = results.get_nowait()
        if error:
            self.fail("peer failed: %s" % error)
        self.assertEqual(len(peer_sessions), 2)
        self.assertNotEqual(first["session"], second["session"])


class UtilityTests(unittest.TestCase):
    def test_socketcan_subscribes_to_all_error_frames(self):
        class FakeSocket:
            def __init__(self):
                self.options = []
                self.closed = False

            def setblocking(self, value):
                self.blocking = value

            def setsockopt(self, level, option, value):
                self.options.append((level, option, value))

            def getsockopt(self, _level, _option):
                return 8 * 1024 * 1024

            def bind(self, address):
                self.address = address

            def close(self):
                self.closed = True

        fake = FakeSocket()
        with mock.patch.object(partner.socket, "socket", return_value=fake):
            transport = partner.SocketCanTransport("can0", 4 * 1024 * 1024)
        try:
            self.assertIn(
                (partner.SOL_CAN_RAW, partner.CAN_RAW_ERR_FILTER,
                 partner.struct.pack("=I", partner.CAN_ERR_MASK)),
                fake.options,
            )
        finally:
            transport.close()
        self.assertTrue(fake.closed)

    def test_restarted_hello_must_keep_negotiated_options(self):
        partner.validate_hello(partner.Hello(0x1, 32, True), 32, True)
        with self.assertRaisesRegex(partner.ProtocolError, "requested"):
            partner.validate_hello(partner.Hello(0x2, 31, True), 32, True)
        with self.assertRaisesRegex(partner.ProtocolError, "include-esi"):
            partner.validate_hello(partner.Hello(0x3, 32, False), 32, True)

    def test_atomic_json_replaces_target(self):
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "report.json"
            partner.atomic_json(target, {"generation": 1})
            partner.atomic_json(target, {"generation": 2})
            self.assertEqual(
                json.loads(target.read_text(encoding="utf-8")),
                {"generation": 2},
            )
            self.assertEqual(list(target.parent.glob("*.tmp")), [])

    def test_wait_for_hello_can_count_foreign_frames(self):
        sender, receiver = partner.MemoryTransport.pair()
        deadline = time.monotonic() + 2
        try:
            sender.send(partner.CanFrame(0x321, b"noise"), deadline)
            sender.send(partner.make_control(
                partner.Kind.HELLO, partner.Direction.GUEST_TO_PEER,
                partner.hello_sequence(10), 0x55,
            ), deadline)
            hello, foreign = partner.wait_for_hello(
                receiver, 10, deadline, ignore_foreign=True,
            )
        finally:
            sender.close()
            receiver.close()
        self.assertEqual(hello, partner.Hello(0x55, 10))
        self.assertEqual(foreign, 1)

    def test_argument_limits(self):
        parser = partner.build_parser()
        args = parser.parse_args([
            "guest", "--interface", "can0", "--frames", "65535",
            "--window", "1",
        ])
        partner.validate_args(parser, args)
        self.assertEqual(args.frames, 65535)

    def test_counter_gate_rejects_drops_and_socket_overflow(self):
        class Transport:
            stats = partner._empty_transport_stats()

        before = {"statistics": {
            "rx_packets": 10, "tx_packets": 20,
            "rx_bytes": 100, "tx_bytes": 200,
            "rx_errors": 0, "tx_errors": 0,
            "rx_dropped": 0, "tx_dropped": 0,
        }}
        after = {"statistics": dict(before["statistics"])}
        after["statistics"].update({
            "rx_packets": 11, "tx_packets": 21,
            "rx_bytes": 164, "tx_bytes": 264,
        })
        delta = partner.validate_kernel_and_transport_counters(
            Transport, before, after,
        )
        self.assertEqual(delta["rx_packets"], 1)

        after["statistics"]["rx_dropped"] = 1
        with self.assertRaisesRegex(RuntimeError, "drop"):
            partner.validate_kernel_and_transport_counters(
                Transport, before, after,
            )
        after["statistics"]["rx_dropped"] = 0
        Transport.stats["rx_queue_overflows"] = 1
        try:
            with self.assertRaisesRegex(RuntimeError, "overflow"):
                partner.validate_kernel_and_transport_counters(
                    Transport, before, after,
                )
        finally:
            Transport.stats["rx_queue_overflows"] = 0


if __name__ == "__main__":
    unittest.main()
