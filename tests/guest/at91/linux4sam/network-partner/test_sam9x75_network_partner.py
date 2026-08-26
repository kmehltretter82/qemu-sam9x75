#!/usr/bin/env python3
"""Host-only tests for the SAM9X75 network partner protocol."""

# SPDX-License-Identifier: GPL-2.0-or-later

import json
import pathlib
import queue
import socket
import tempfile
import threading
import time
import unittest
from unittest import mock

import sam9x75_network_partner as partner


class FrameTests(unittest.TestCase):
    def test_frame_round_trip_at_boundaries(self):
        for length in (0, 1, 31, 1400, 65535, partner.MAX_PAYLOAD):
            with self.subTest(length=length):
                payload = partner.deterministic_payload(
                    0x1234, partner.Direction.GUEST_TO_PEER, 7, 19, length,
                )
                frame = partner.Frame(
                    partner.Kind.DATA, partner.Direction.GUEST_TO_PEER,
                    7, 0x1234, 19, payload,
                )
                self.assertEqual(partner.Frame.decode(frame.encode()), frame)

    def test_crc_corruption_is_rejected(self):
        encoded = bytearray(partner.Frame(
            partner.Kind.DATA, partner.Direction.PEER_TO_GUEST,
            0, 1, 0, b"payload",
        ).encode())
        encoded[-1] ^= 0x80
        with self.assertRaisesRegex(partner.ProtocolError, "CRC"):
            partner.Frame.decode(encoded)

    def test_header_validation(self):
        encoded = bytearray(partner.Frame(
            partner.Kind.UDP_DATA, partner.Direction.GUEST_TO_PEER,
            0, 1, 0, b"",
        ).encode())
        encoded[0] ^= 1
        with self.assertRaisesRegex(partner.ProtocolError, "magic"):
            partner.Frame.decode(encoded)


class StreamTests(unittest.TestCase):
    def test_iter_lengths_is_exact(self):
        for total in (1, 2, 4096, 131071, 262144):
            values = list(partner.iter_lengths(total, partner.TCP_CHUNK_SIZES))
            self.assertEqual(sum(length for _, length in values), total)
            self.assertEqual(
                [sequence for sequence, _ in values],
                list(range(len(values))),
            )

    def test_fragmented_frame_receive(self):
        left, right = socket.socketpair()
        deadline = time.monotonic() + 10
        expected = partner.Frame(
            partner.Kind.DATA, partner.Direction.GUEST_TO_PEER,
            3, 0xfeed, 7, b"fragmented payload" * 500,
        )
        thread = threading.Thread(
            target=partner.send_fragmented,
            args=(left, expected.encode(), (1, 2, 3, 5, 8, 13)),
        )
        thread.start()
        try:
            self.assertEqual(partner.recv_frame(right, deadline), expected)
        finally:
            thread.join(5)
            left.close()
            right.close()

    def test_simultaneous_full_duplex(self):
        guest, peer = socket.socketpair()
        deadline = time.monotonic() + 30
        session = 0x1122334455667788
        results = queue.Queue()

        def endpoint(name, sock, send_direction, receive_direction):
            try:
                value = partner.run_duplex(
                    sock, session, 1, send_direction, receive_direction,
                    512 * 1024, (1, 3, 17, 257, 4096), deadline,
                )
                results.put((name, value, None))
            except BaseException as exc:
                results.put((name, None, exc))

        guest_thread = threading.Thread(
            target=endpoint,
            args=(
                "guest", guest, partner.Direction.GUEST_TO_PEER,
                partner.Direction.PEER_TO_GUEST,
            ),
        )
        peer_thread = threading.Thread(
            target=endpoint,
            args=(
                "peer", peer, partner.Direction.PEER_TO_GUEST,
                partner.Direction.GUEST_TO_PEER,
            ),
        )
        guest_thread.start()
        peer_thread.start()
        guest_thread.join(30)
        peer_thread.join(30)
        guest.close()
        peer.close()
        self.assertFalse(guest_thread.is_alive())
        self.assertFalse(peer_thread.is_alive())
        values = {}
        while not results.empty():
            name, value, error = results.get_nowait()
            if error:
                self.fail("%s endpoint failed: %s" % (name, error))
            values[name] = value
        self.assertEqual(set(values), {"guest", "peer"})
        for value in values.values():
            self.assertEqual(value["sent"]["bytes"], 512 * 1024)
            self.assertEqual(value["received"]["bytes"], 512 * 1024)

    @mock.patch.object(
        partner, "send_tcp_data", side_effect=RuntimeError("send failed"),
    )
    def test_sender_failure_interrupts_receive_promptly(self, _sender):
        guest, peer = socket.socketpair()
        started = time.monotonic()
        try:
            with self.assertRaisesRegex(RuntimeError, "send failed"):
                partner.run_duplex(
                    guest, 1, 0, partner.Direction.GUEST_TO_PEER,
                    partner.Direction.PEER_TO_GUEST, 1024, (1,),
                    time.monotonic() + 10,
                )
        finally:
            guest.close()
            peer.close()
        self.assertLess(time.monotonic() - started, 2)


class UtilityTests(unittest.TestCase):
    def test_udp_boundary_payloads_fit_ethernet_mtu(self):
        session = 1
        for sequence in range(len(partner.UDP_PAYLOAD_SIZES)):
            length = partner.udp_payload_length(sequence)
            encoded = partner.Frame(
                partner.Kind.UDP_DATA,
                partner.Direction.GUEST_TO_PEER,
                0, session, sequence,
                partner.deterministic_payload(
                    session, partner.Direction.GUEST_TO_PEER,
                    0, sequence, length,
                ),
            ).encode()
            self.assertLessEqual(len(encoded), 1472)
            self.assertEqual(partner.Frame.decode(encoded).sequence, sequence)

    def test_atomic_json_replaces_target(self):
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "result.json"
            partner.atomic_json(target, {"generation": 1})
            partner.atomic_json(target, {"generation": 2})
            self.assertEqual(
                json.loads(target.read_text(encoding="utf-8")),
                {"generation": 2},
            )
            self.assertEqual(list(target.parent.glob("*.tmp")), [])

    def test_fragment_parser_rejects_nonpositive_values(self):
        for value in ("", "0", "1,-1"):
            with self.subTest(value=value):
                with self.assertRaises(Exception):
                    partner.parse_fragment_pattern(value)

    def test_session_identifier_is_nonzero_uint64(self):
        self.assertEqual(
            partner.session_id("0xffffffffffffffff"),
            0xffffffffffffffff,
        )
        for value in ("0", "-1", "0x10000000000000000"):
            with self.subTest(value=value):
                with self.assertRaises(partner.argparse.ArgumentTypeError):
                    partner.session_id(value)


class LocalPeerTests(unittest.TestCase):
    def test_tcp_and_udp_peer_end_to_end(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(4)
        udp.bind(("127.0.0.1", 0))
        tcp_port = listener.getsockname()[1]
        udp_port = udp.getsockname()[1]
        deadline = time.monotonic() + 30
        session = 0x123456789abcdef0
        results = queue.Queue()

        def server():
            try:
                actual_session, tcp_result = partner.run_tcp_server(
                    listener, 2, 256 * 1024, (1, 17, 257, 4096), deadline,
                )
                self.assertEqual(actual_session, session)
                udp_result = partner.run_udp_server(
                    udp, session, 32, deadline,
                )
                results.put((tcp_result, udp_result, None))
            except BaseException as exc:
                results.put((None, None, exc))

        thread = threading.Thread(target=server)
        thread.start()
        try:
            tcp_result = partner.run_tcp_client(
                "127.0.0.1", tcp_port, session, 2, 256 * 1024,
                (1, 17, 257, 4096), deadline,
            )
            udp_result = partner.run_udp_client(
                "127.0.0.1", udp_port, session, 32, 1.0, 2, deadline,
            )
            thread.join(30)
        finally:
            listener.close()
            udp.close()
        self.assertFalse(thread.is_alive())
        server_tcp, server_udp, error = results.get_nowait()
        if error:
            self.fail("peer failed: %s" % error)
        self.assertEqual(len(tcp_result), 2)
        self.assertEqual(len(server_tcp), 2)
        self.assertEqual(udp_result["packets"], 32)
        self.assertEqual(server_udp["packets"], 32)
        self.assertEqual(udp_result["retries"], 0)
        self.assertEqual(server_udp["done_requests"], 1)

    def test_udp_done_response_loss_is_retried(self):
        class DropFirstDoneResponse:
            def __init__(self, sock):
                self.sock = sock
                self.dropped = False

            def __getattr__(self, name):
                return getattr(self.sock, name)

            def sendto(self, data, address):
                frame = partner.Frame.decode(data)
                if (not self.dropped and
                        frame.kind == partner.Kind.UDP_DONE and
                        frame.direction == partner.Direction.PEER_TO_GUEST):
                    self.dropped = True
                    return len(data)
                return self.sock.sendto(data, address)

        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        wrapped = DropFirstDoneResponse(server)
        deadline = time.monotonic() + 10
        results = queue.Queue()

        def serve():
            try:
                results.put((partner.run_udp_server(
                    wrapped, 0x5151, 4, deadline,
                ), None))
            except BaseException as exc:
                results.put((None, exc))

        thread = threading.Thread(target=serve)
        thread.start()
        try:
            client = partner.run_udp_client(
                "127.0.0.1", server.getsockname()[1], 0x5151, 4,
                0.1, 2, deadline,
            )
            thread.join(5)
        finally:
            server.close()
        self.assertFalse(thread.is_alive())
        result, error = results.get_nowait()
        if error:
            self.fail("UDP server failed: %s" % error)
        self.assertTrue(wrapped.dropped)
        self.assertEqual(client["retries"], 1)
        self.assertEqual(result["done_requests"], 2)

    def test_failed_peer_start_invalidates_stale_ready_file(self):
        blocker = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        blocker.bind(("127.0.0.1", 0))
        blocker.listen(1)
        with tempfile.TemporaryDirectory() as directory:
            ready = pathlib.Path(directory) / "peer.ready.json"
            result = pathlib.Path(directory) / "peer.json"
            ready.write_text('{"ready": true, "pid": 1}\n', encoding="utf-8")
            args = partner.argparse.Namespace(
                bind="127.0.0.1", tcp_port=blocker.getsockname()[1],
                udp_port=0, sessions=1, streams=2,
                bytes_per_direction=1024, udp_packets=4, timeout=10,
                fragments=(1,), ready_file=str(ready), json=str(result),
            )
            try:
                self.assertEqual(partner.peer_main(args), 1)
            finally:
                blocker.close()
            self.assertFalse(ready.exists())
            self.assertFalse(json.loads(result.read_text())["success"])

    def test_udp_client_discards_delayed_duplicate(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        port = server.getsockname()[1]
        session = 0x4242
        deadline = time.monotonic() + 10
        result = queue.Queue()

        def serve():
            try:
                for sequence in range(2):
                    request_data, address = server.recvfrom(4096)
                    request = partner.Frame.decode(request_data)
                    length = partner.udp_payload_length(sequence)
                    partner.validate_frame(
                        request, partner.Kind.UDP_DATA,
                        partner.Direction.GUEST_TO_PEER, 0, session,
                        sequence,
                    )
                    response = partner.Frame(
                        partner.Kind.UDP_DATA,
                        partner.Direction.PEER_TO_GUEST,
                        0, session, sequence,
                        partner.deterministic_payload(
                            session, partner.Direction.PEER_TO_GUEST,
                            0, sequence, length,
                        ),
                    ).encode()
                    server.sendto(response, address)
                    if sequence == 0:
                        server.sendto(response, address)
                done_data, address = server.recvfrom(4096)
                done = partner.Frame.decode(done_data)
                partner.validate_frame(
                    done, partner.Kind.UDP_DONE,
                    partner.Direction.GUEST_TO_PEER, 0, session, 2,
                )
                server.sendto(partner.Frame(
                    partner.Kind.UDP_DONE,
                    partner.Direction.PEER_TO_GUEST,
                    0, session, 2, done.payload,
                ).encode(), address)
                result.put(None)
            except BaseException as exc:
                result.put(exc)

        thread = threading.Thread(target=serve)
        thread.start()
        try:
            client = partner.run_udp_client(
                "127.0.0.1", port, session, 2, 1.0, 1, deadline,
            )
            thread.join(10)
        finally:
            server.close()
        self.assertFalse(thread.is_alive())
        error = result.get_nowait()
        if error:
            self.fail("UDP server failed: %s" % error)
        self.assertEqual(client["stale_responses"], 1)


class GuestValidationTests(unittest.TestCase):
    @mock.patch.object(partner, "ipv4_addresses", return_value=[])
    def test_empty_ipv4_is_a_failure(self, _addresses):
        with self.assertRaisesRegex(RuntimeError, "no IPv4"):
            partner.require_ipv4_address("eth0")

    @mock.patch.object(partner, "run_command")
    def test_ethtool_link_down_is_a_failure(self, command):
        command.return_value = {
            "returncode": 0,
            "output": "Link detected: no\n",
        }
        with self.assertRaisesRegex(RuntimeError, "active link"):
            partner.require_ethtool_link("eth0")

    @mock.patch.object(partner, "run_command")
    def test_ethtool_requires_macb_driver(self, command):
        command.side_effect = [
            {"returncode": 0, "output": "Link detected: yes\n"},
            {"returncode": 0, "output": "driver: other\n"},
        ]
        with self.assertRaisesRegex(RuntimeError, "macb driver"):
            partner.require_ethtool_link("eth0")

    @mock.patch.object(partner, "run_command")
    def test_route_must_use_requested_interface_and_source(self, command):
        command.return_value = {
            "returncode": 0,
            "output": "10.0.2.2 dev eth0 src 10.0.2.15 uid 0\n",
        }
        addresses = [{"local": "10.0.2.10"}, {"local": "10.0.2.15"}]
        result = partner.require_route("eth0", "10.0.2.2", addresses)
        self.assertEqual(result["source"], "10.0.2.15")
        with self.assertRaisesRegex(RuntimeError, "does not use eth1"):
            partner.require_route("eth1", "10.0.2.2", addresses)
        with self.assertRaisesRegex(RuntimeError, "not assigned"):
            partner.require_route(
                "eth0", "10.0.2.2", [{"local": "10.0.2.10"}],
            )

    def test_default_path_includes_sbin_for_init_shell(self):
        with mock.patch.dict(partner.os.environ, {}, clear=True):
            self.assertTrue(partner.command_path("ip"))
            self.assertTrue(partner.command_path("ethtool"))


if __name__ == "__main__":
    unittest.main()
