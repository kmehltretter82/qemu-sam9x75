#!/usr/bin/env python3
"""Host-only tests for the SAM9X75 UART partner protocol."""

# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import contextlib
import importlib.util
import io
import json
import os
import pathlib
import pty
import select
import socket
import sys
import tempfile
import threading
import time
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("sam9x75_uart_partner.py")
SPEC = importlib.util.spec_from_file_location(
    "sam9x75_uart_partner", MODULE_PATH,
)
uart = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = uart
SPEC.loader.exec_module(uart)


class SocketStream:
    def __init__(self, sock):
        self.sock = sock
        self.sock.setblocking(False)

    def read(self, size, timeout):
        readable, _, _ = select.select([self.sock], [], [], timeout)
        if not readable:
            raise uart.StreamTimeout()
        data = self.sock.recv(size)
        if not data:
            raise uart.StreamEOF()
        return data

    def write(self, data):
        view = memoryview(data)
        while view:
            _, writable, _ = select.select([], [self.sock], [], 1.0)
            if not writable:
                continue
            written = self.sock.send(view)
            view = view[written:]

    def close(self):
        self.sock.close()


def endpoint_args(role, **overrides):
    values = {
        "session": 0x0123456789abcdef if role == "guest" else 0,
        "fragment_pattern": (1, 3, 17, 257, 4096),
        "pace_us": 0,
        "backpressure_every_bytes": 32768,
        "backpressure_ms": 1,
        "progress": False,
        "timeout": 20.0,
        "sessions": 1,
        "barrier_after": None,
        "barrier_ready_file": None,
        "resume_file": None,
        "migration_reconnect_timeout": 0.0,
        "rts_pause_after_bytes": 0,
        "rts_pause_ms": 0,
        "rtscts": False,
    }
    values.update(overrides)
    return argparse.Namespace(**values)


class ProtocolUnitTests(unittest.TestCase):
    def test_peer_tty_cli_is_distinct_from_pyserial_transport(self):
        parser = uart.build_parser()
        args = parser.parse_args([
            "peer", "--tty", "/dev/ttyACM0", "--sessions", "2",
        ])
        uart.validate_args(parser, args)
        self.assertEqual(args.tty, "/dev/ttyACM0")
        self.assertIsNone(args.serial)
        self.assertIsNone(args.unix_listen)
        self.assertEqual(args.sessions, 2)

        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                parser.parse_args([
                    "peer", "--tty", "/dev/ttyACM0",
                    "--serial", "/dev/ttyUSB0",
                ])

    def test_peer_tty_rejects_pyserial_only_manual_rts(self):
        parser = uart.build_parser()
        args = parser.parse_args([
            "peer", "--tty", "/dev/ttyACM0",
            "--rts-pause-after-bytes", "1",
        ])
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                uart.validate_args(parser, args)

    def test_posix_tty_stream_transfers_binary_data_in_both_directions(self):
        master, slave = pty.openpty()
        path = os.ttyname(slave)
        stream = None
        try:
            stream = uart.PosixTTYStream(path, 115200)
            os.close(slave)
            slave = -1

            master_to_stream = b"\x00peer-to-gadget\xff\r\n"
            os.write(master, master_to_stream)
            self.assertEqual(
                stream.read(len(master_to_stream), 1.0), master_to_stream,
            )

            stream_to_master = b"\xffgadget-to-peer\x00\n\r"
            stream.write(stream_to_master)
            readable, _, _ = select.select([master], [], [], 1.0)
            self.assertTrue(readable)
            self.assertEqual(os.read(master, 4096), stream_to_master)

            with self.assertRaises(uart.StreamTimeout):
                stream.read(1, 0.01)
        finally:
            if stream is not None:
                stream.close()
            if slave >= 0:
                os.close(slave)
            os.close(master)

    def test_manual_rts_pause_attests_lines_and_queue(self):
        class FakeSerial:
            def __init__(self):
                self._rts = True
                self.cts = True
                self.in_waiting = 3
                self.transitions = []

            @property
            def rts(self):
                return self._rts

            @rts.setter
            def rts(self, value):
                self._rts = bool(value)
                self.transitions.append(self._rts)

        fake = FakeSerial()
        stream = uart.PySerialStream.__new__(uart.PySerialStream)
        stream.serial = fake
        stream.manual_rts = True
        sleeps = []

        def sleep(seconds):
            sleeps.append(seconds)
            fake.in_waiting = 11

        callback_rts = []
        result = stream.manual_rts_pause(
            125,
            during_pause=lambda: callback_rts.append(fake.rts),
            sleep_fn=sleep,
        )
        self.assertEqual(fake.transitions, [False, True])
        self.assertEqual(callback_rts, [False])
        self.assertEqual(sleeps, [0.125])
        self.assertTrue(result["rts_deasserted_readback"])
        self.assertTrue(result["rts_asserted_readback"])
        self.assertEqual(result["queued_at_rts_low"], 3)
        self.assertEqual(result["queued_before_resume"], 11)
        self.assertTrue(result["remote_transmit_released_while_rts_low"])

    def test_manual_rts_pause_restores_line_after_failure(self):
        class FakeSerial:
            rts = True
            cts = True
            in_waiting = 0

        fake = FakeSerial()
        stream = uart.PySerialStream.__new__(uart.PySerialStream)
        stream.serial = fake
        stream.manual_rts = True

        def fail(_seconds):
            raise RuntimeError("injected sleep failure")

        with self.assertRaisesRegex(RuntimeError, "injected"):
            stream.manual_rts_pause(1, sleep_fn=fail)
        self.assertTrue(fake.rts)

    def test_incremental_round_trip_at_every_boundary(self):
        encoded = b"".join(
            uart.Frame(
                uart.Kind.DATA,
                uart.Direction.GUEST_TO_PEER,
                0x1122334455667788,
                sequence,
                uart.deterministic_payload(
                    0x1122334455667788,
                    uart.Direction.GUEST_TO_PEER,
                    sequence,
                    length,
                ),
            ).encode()
            for sequence, length in enumerate(uart.BOUNDARY_SIZES)
        )
        decoder = uart.FrameDecoder()
        frames = []
        pattern = (1, 2, 3, 5, 8, 13, 64, 1024)
        offset = 0
        fragment = 0
        while offset < len(encoded):
            size = pattern[fragment % len(pattern)]
            frames.extend(decoder.feed(encoded[offset:offset + size]))
            offset += size
            fragment += 1
        self.assertEqual(len(frames), len(uart.BOUNDARY_SIZES))
        self.assertEqual([len(frame.payload) for frame in frames],
                         list(uart.BOUNDARY_SIZES))
        self.assertEqual(decoder.crc_errors, 0)
        self.assertEqual(decoder.header_errors, 0)

    def test_crc_failure_resynchronizes_at_next_magic(self):
        bad = bytearray(uart.Frame(
            uart.Kind.DATA, uart.Direction.GUEST_TO_PEER, 1, 0, b"damaged",
        ).encode())
        bad[-1] ^= 0x80
        good = uart.Frame(
            uart.Kind.ACK, uart.Direction.PEER_TO_GUEST, 1, 0,
        )
        decoder = uart.FrameDecoder()
        frames = decoder.feed(b"line-noise" + bytes(bad) + good.encode())
        self.assertEqual(frames, [good])
        self.assertEqual(decoder.crc_errors, 1)
        self.assertGreater(decoder.discarded_bytes, 0)

    def test_reset_hello_preempts_interrupted_large_data_frame(self):
        old_data = uart.Frame(
            uart.Kind.DATA,
            uart.Direction.GUEST_TO_PEER,
            0x1111111111111111,
            len(uart.BOUNDARY_SIZES) - 1,
            uart.deterministic_payload(
                0x1111111111111111,
                uart.Direction.GUEST_TO_PEER,
                len(uart.BOUNDARY_SIZES) - 1,
                uart.BOUNDARY_SIZES[-1],
            ),
        ).encode()
        new_hello = uart.Frame(
            uart.Kind.HELLO,
            uart.Direction.GUEST_TO_PEER,
            0x2222222222222222,
            0,
            uart.hello_payload(),
        )
        decoder = uart.FrameDecoder()

        self.assertEqual(decoder.feed(old_data[:uart.HEADER.size + 1]), [])
        decoded = []
        for value in new_hello.encode():
            decoded.extend(decoder.feed(bytes((value,))))
        self.assertEqual(decoded, [new_hello])
        self.assertEqual(decoder.crc_errors, 0)
        self.assertEqual(decoder.header_errors, 0)
        self.assertEqual(decoder.discarded_bytes, uart.HEADER.size + 1)

    def test_payload_is_stable_and_direction_sensitive(self):
        first = uart.deterministic_payload(
            9, uart.Direction.GUEST_TO_PEER, 4, 65535,
        )
        second = uart.deterministic_payload(
            9, uart.Direction.GUEST_TO_PEER, 4, 65535,
        )
        reverse = uart.deterministic_payload(
            9, uart.Direction.PEER_TO_GUEST, 4, 65535,
        )
        self.assertEqual(first, second)
        self.assertNotEqual(first, reverse)
        self.assertEqual(len(first), 65535)

    def test_backpressure_thresholds_ignore_read_granularity(self):
        def pause_count(chunks, interval):
            total = 0
            pauses = 0
            for length in chunks:
                previous = total
                total += length
                pauses += uart.byte_threshold_crossings(
                    previous, total, interval,
                )
            return total, pauses

        fine = pause_count([1] * 10000, 4096)
        coarse = pause_count([10000], 4096)
        irregular = pause_count([3, 4092, 2, 4097, 1806], 4096)
        self.assertEqual(fine, (10000, 2))
        self.assertEqual(coarse, fine)
        self.assertEqual(irregular, fine)
        self.assertEqual(
            uart.byte_threshold_crossings(0, 10000, 0), 0,
        )

    def test_unix_migration_switches_to_destination_connection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory, "uart.sock")
            server = uart.UnixServerStream(path)
            source = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            destination = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                source.connect(str(server.path))
                source.sendall(b"source")
                self.assertEqual(server.read(16, 1.0), b"source")
                destination.connect(str(server.path))
                self.assertTrue(server.migration_resume(1.0))
                server.write(b"resume")
                self.assertEqual(destination.recv(16), b"resume")
                self.assertEqual(server.connections, 2)
            finally:
                source.close()
                destination.close()
                server.close()

    def test_cli_failure_still_emits_tap_and_complete_json(self):
        with tempfile.TemporaryDirectory() as directory:
            report_path = pathlib.Path(directory, "failure.json")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                status = uart.main([
                    "guest",
                    "--device", str(pathlib.Path(directory, "missing-tty")),
                    "--json", str(report_path),
                ])
            report = json.loads(report_path.read_text())
            self.assertEqual(status, 1)
            self.assertIn("TAP version 13", output.getvalue())
            self.assertIn("not ok 7 - overall", output.getvalue())
            self.assertFalse(report["success"])
            self.assertEqual(report["boundary_sizes"],
                             list(uart.BOUNDARY_SIZES))
            self.assertEqual(report["transport_read_batches"], 0)
            self.assertTrue(report["errors"])


class EndToEndTests(unittest.TestCase):
    def receive_until(self, stream, decoder, predicate, timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data = stream.read(16384, 0.25)
            except uart.StreamTimeout:
                continue
            for frame in decoder.feed(data):
                if predicate(frame):
                    return frame
        self.fail("timed out waiting for protocol frame")

    def run_pair(self, peer_args=None, guest_args=None, resume_ready=None):
        left, right = socket.socketpair()
        streams = SocketStream(left), SocketStream(right)
        arguments = (
            peer_args or endpoint_args("peer"),
            guest_args or endpoint_args("guest"),
        )
        reports = [None, None]

        def run(index, role):
            reports[index] = uart.EndpointRunner(
                streams[index], role, arguments[index],
            ).run()

        threads = [
            threading.Thread(target=run, args=(0, "peer")),
            threading.Thread(target=run, args=(1, "guest")),
        ]
        for thread in threads:
            thread.start()
        if resume_ready is not None:
            resume_ready()
        for thread in threads:
            thread.join(25.0)
            self.assertFalse(thread.is_alive(), "endpoint did not terminate")
        for stream in streams:
            stream.close()
        return reports

    def test_full_duplex_fragmentation_and_backpressure(self):
        reports = self.run_pair()
        for report in reports:
            self.assertTrue(report["success"], report)
            self.assertEqual(report["tx_data_acked"],
                             len(uart.BOUNDARY_SIZES))
            self.assertEqual(report["rx_data_validated"],
                             len(uart.BOUNDARY_SIZES))
            self.assertEqual(
                report["backpressure_pause_count"],
                report["transport_read_bytes"] //
                report["backpressure_every_bytes"],
            )
            self.assertGreater(report["backpressure_pause_count"], 0)
        with tempfile.TemporaryDirectory() as directory:
            report_path = pathlib.Path(directory, "success.json")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                uart.emit_tap(reports[0])
            uart.write_json_report(report_path, reports[0])
            saved = json.loads(report_path.read_text())
            self.assertIn("ok 7 - overall", output.getvalue())
            self.assertIn(
                "# backpressure: interval-bytes=32768",
                output.getvalue(),
            )
            self.assertTrue(saved["success"])

    def test_migration_quiesce_barrier(self):
        with tempfile.TemporaryDirectory() as directory:
            ready = pathlib.Path(directory, "ready.json")
            resume = pathlib.Path(directory, "resume")
            peer_args = endpoint_args(
                "peer", barrier_after=7, barrier_ready_file=str(ready),
                resume_file=str(resume),
            )

            def resume_when_ready():
                deadline = time.monotonic() + 10.0
                while not ready.exists() and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertTrue(ready.exists(), "barrier was not reached")
                resume.touch()

            reports = self.run_pair(
                peer_args=peer_args, resume_ready=resume_when_ready,
            )
            for report in reports:
                self.assertTrue(report["success"], report)
            self.assertTrue(reports[0]["migration_barrier_ready"])
            self.assertTrue(reports[0]["migration_barrier_resumed"])
            self.assertTrue(reports[1]["migration_barrier_ready"])
            self.assertTrue(reports[1]["migration_barrier_resumed"])

    def test_two_hello_sessions_resynchronize_without_peer_restart(self):
        left, right = socket.socketpair()
        peer_stream = SocketStream(left)
        guest_stream = SocketStream(right)
        peer_report = [None]

        def run_peer():
            peer_report[0] = uart.EndpointRunner(
                peer_stream, "peer", endpoint_args("peer", sessions=2),
            ).run()

        thread = threading.Thread(target=run_peer)
        thread.start()
        first = uart.EndpointRunner(
            guest_stream, "guest",
            endpoint_args("guest", session=0x1111111111111111),
        ).run()
        second = uart.EndpointRunner(
            guest_stream, "guest",
            endpoint_args("guest", session=0x2222222222222222),
        ).run()
        thread.join(25.0)
        self.assertFalse(
            thread.is_alive(), "peer did not accept the new HELLO",
        )
        peer_stream.close()
        guest_stream.close()
        self.assertTrue(first["success"], first)
        self.assertTrue(second["success"], second)
        self.assertTrue(peer_report[0]["success"], peer_report[0])
        self.assertEqual(peer_report[0]["hello_count"], 2)
        self.assertEqual(peer_report[0]["completed_sessions"], 2)
        self.assertEqual(peer_report[0]["reset_resync_count"], 1)

    def test_new_hello_abandons_an_incomplete_pre_reset_session(self):
        left, right = socket.socketpair()
        peer_stream = SocketStream(left)
        guest_stream = SocketStream(right)
        peer_report = [None]

        def run_peer():
            peer_report[0] = uart.EndpointRunner(
                peer_stream, "peer", endpoint_args("peer"),
            ).run()

        thread = threading.Thread(target=run_peer)
        thread.start()
        old_session = 0xaaaaaaaaaaaaaaaa
        decoder = uart.FrameDecoder()
        guest_stream.write(uart.Frame(
            uart.Kind.HELLO, uart.Direction.GUEST_TO_PEER, old_session, 0,
            uart.hello_payload(),
        ).encode())
        self.receive_until(
            guest_stream, decoder,
            lambda frame: frame.kind == uart.Kind.HELLO and
            frame.session == old_session,
        )
        guest_stream.write(uart.Frame(
            uart.Kind.DATA, uart.Direction.GUEST_TO_PEER, old_session, 0,
            uart.deterministic_payload(
                old_session, uart.Direction.GUEST_TO_PEER, 0,
                uart.BOUNDARY_SIZES[0],
            ),
        ).encode())

        new_session = 0xbbbbbbbbbbbbbbbb
        guest_stream.write(uart.Frame(
            uart.Kind.HELLO, uart.Direction.GUEST_TO_PEER, new_session, 0,
            uart.hello_payload(),
        ).encode())
        final_guest = uart.EndpointRunner(
            guest_stream, "guest",
            endpoint_args("guest", session=new_session),
        ).run()
        thread.join(25.0)
        self.assertFalse(thread.is_alive(), "peer did not resync after reset")
        peer_stream.close()
        guest_stream.close()
        self.assertTrue(final_guest["success"], final_guest)
        self.assertTrue(peer_report[0]["success"], peer_report[0])
        self.assertEqual(peer_report[0]["reset_resync_count"], 1)
        self.assertEqual(peer_report[0]["aborted_sessions"], 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
