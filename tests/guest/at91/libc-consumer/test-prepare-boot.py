#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-only tests for pinned downloads and root-owned boot archive construction."""

import gzip
import hashlib
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("prepare_boot", Path(__file__).with_name("prepare-boot.py"))
boot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boot)
spec = importlib.util.spec_from_file_location("prepare_sysroots", Path(__file__).with_name("prepare-sysroots.py"))
sysroots = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sysroots)


def records(archive):
    data = gzip.decompress(archive)
    offset = 0
    result = []
    while True:
        header = data[offset:offset + 110]
        assert header[:6] == b"070701"
        fields = [int(header[i:i + 8], 16) for i in range(6, 110, 8)]
        start = offset + 110
        name = data[start:start + fields[11] - 1].decode()
        start = (start + fields[11] + 3) & ~3
        payload = data[start:start + fields[6]]
        result.append((name, fields, payload))
        offset = (start + fields[6] + 3) & ~3
        if name == "TRAILER!!!":
            assert all(byte == 0 for byte in data[offset:])
            assert len(data) % 512 == 0
            return result


class BootTests(unittest.TestCase):
    def test_debian_compression_is_explicit(self):
        for member, flags in (("data.tar", ["-xf"]), ("data.tar.xz", ["-xJf"]),
                              ("data.tar.gz", ["-xzf"]), ("data.tar.bz2", ["-xjf"]),
                              ("data.tar.zst", ["--zstd", "-xf"])):
            with self.subTest(member=member):
                selected, command = sysroots.debian_tar_command(
                    f"debian-binary\ncontrol.tar.xz\n{member}\n", Path("root"))
                self.assertEqual(selected, member)
                self.assertEqual(command, ["tar", *flags, "-", "-C", "root"])

    def test_debian_payload_is_required(self):
        with self.assertRaises(ValueError):
            sysroots.debian_tar_command("debian-binary\ncontrol.tar.xz\n", Path("root"))

    def test_compressed_debian_payload_roundtrip(self):
        # Use GNU tar on macOS too, when installed: BSD tar's pipe autodetection
        # otherwise hides the Linux regression this test is meant to catch.
        tar = shutil.which("gtar") or shutil.which("tar")
        self.assertIsNotNone(tar)
        for suffix in ("", ".xz", ".gz", ".bz2"):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as temp:
                data = io.BytesIO()
                mode = "w" + (":" + suffix[1:] if suffix else "")
                with tarfile.open(fileobj=data, mode=mode) as archive:
                    info = tarfile.TarInfo("fixture")
                    info.size = len(b"public test data")
                    archive.addfile(info, io.BytesIO(b"public test data"))
                root = Path(temp)
                _, command = sysroots.debian_tar_command("data.tar" + suffix, root)
                command[0] = tar
                subprocess.run(command, input=data.getvalue(), capture_output=True, check=True)
                self.assertEqual((root / "fixture").read_bytes(), b"public test data")

    def test_ambiguous_debian_payload_is_rejected(self):
        with self.assertRaises(ValueError):
            sysroots.debian_tar_command("data.tar.xz\ndata.tar.gz\n", Path("root"))

    def test_unknown_debian_compression_is_rejected(self):
        with self.assertRaises(ValueError):
            sysroots.debian_tar_command("data.tar.unknown\n", Path("root"))

    def test_archive_is_deterministic(self):
        self.assertEqual(boot.initramfs(b"fake executable"), boot.initramfs(b"fake executable"))

    def test_archive_layout(self):
        result = records(boot.initramfs(b"executable"))
        self.assertEqual([name for name, _, _ in result],
                         ["bin", "dev", "proc", "sys", "tmp", "bin/busybox", "bin/sh",
                          "dev/console", "TRAILER!!!"])
        self.assertEqual(result[5][2], b"executable")
        self.assertEqual(result[5][1][1], 0o100755)
        self.assertEqual(result[6][2], b"busybox")
        self.assertEqual(result[6][1][1], 0o120777)
        self.assertEqual(result[7][1][1], 0o20600)
        self.assertEqual(result[7][1][9:11], [5, 1])
        for name, fields, _ in result:
            self.assertEqual(fields[2:4], [0, 0])
            self.assertEqual(fields[5], 0)
            self.assertEqual(fields[7:9], [0, 0])
            if name != "dev/console":
                self.assertEqual(fields[9:11], [0, 0])

    def test_record_alignment(self):
        for size in range(12):
            result = records(boot.initramfs(b"x" * size))
            self.assertEqual(result[5][2], b"x" * size)

    def test_require_options(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / ".config"
            path.write_text("CONFIG_FOO=y\nCONFIG_BAR=m\n")
            boot.require_options(path, ("FOO",))
            with self.assertRaisesRegex(ValueError, "BAR"):
                boot.require_options(path, ("BAR",))

    def test_enable_options(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / ".config"
            path.write_text("CONFIG_FOO=m\n# CONFIG_BAR is not set\nCONFIG_OTHER=n\n")
            boot.enable_options(path, ("FOO", "BAR"))
            self.assertEqual(path.read_text(), "CONFIG_FOO=y\nCONFIG_BAR=y\nCONFIG_OTHER=n\n")

    def test_unknown_options_do_not_change_config(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / ".config"
            path.write_text("CONFIG_FOO=n\n")
            with self.assertRaisesRegex(ValueError, "TYPO"):
                boot.enable_options(path, ("FOO", "TYPO"))
            self.assertEqual(path.read_text(), "CONFIG_FOO=n\n")

    def test_corrupt_cache_is_rejected_without_network(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(boot.urllib.request, "urlopen") as network:
            root = Path(temp)
            (root / "source").write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                boot.fetch(root, ("source", "https://example.invalid/source", "0" * 64))
            network.assert_not_called()
            self.assertEqual((root / "source").read_bytes(), b"corrupt")

    def test_valid_cache_needs_no_network(self):
        content = b"valid source"
        with tempfile.TemporaryDirectory() as temp, patch.object(boot.urllib.request, "urlopen") as network:
            root = Path(temp)
            (root / "source").write_bytes(content)
            self.assertEqual(boot.fetch(root, ("source", "https://example.invalid/source",
                                              hashlib.sha256(content).hexdigest())), root / "source")
            network.assert_not_called()

    def test_download_checks_hash_before_rename(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(
                boot.urllib.request, "urlopen", return_value=io.BytesIO(b"wrong")):
            root = Path(temp)
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                boot.fetch(root, ("source", "https://example.invalid/source", "0" * 64))
            self.assertFalse((root / "source").exists())

    def test_new_download(self):
        content = b"valid source"
        with tempfile.TemporaryDirectory() as temp, patch.object(
                boot.urllib.request, "urlopen", return_value=io.BytesIO(content)):
            root = Path(temp)
            result = boot.fetch(root, ("source", "https://example.invalid/source",
                                       hashlib.sha256(content).hexdigest()))
            self.assertEqual(result.read_bytes(), content)
            self.assertFalse((root / "source.part").exists())

    def test_extract_rejects_path_escape(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "source.tar"
            with tarfile.open(archive, "w") as output:
                info = tarfile.TarInfo("../escape")
                info.size = 1
                output.addfile(info, io.BytesIO(b"x"))
            with self.assertRaises(tarfile.FilterError):
                boot.extract(archive, root / "output")

    def test_extract_rejects_symlink_escape(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "source.tar"
            with tarfile.open(archive, "w") as output:
                info = tarfile.TarInfo("escape")
                info.type = tarfile.SYMTYPE
                info.linkname = "../escape"
                output.addfile(info)
            with self.assertRaises(tarfile.FilterError):
                boot.extract(archive, root / "output")


if __name__ == "__main__":
    unittest.main()
