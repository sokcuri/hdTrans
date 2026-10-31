#!/usr/bin/env python3
"""Test native archive layouts without external dictionaries or build artifacts."""

import hashlib
import importlib.util
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tools/package-release.py"
spec = importlib.util.spec_from_file_location("package_release", SCRIPT)
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hdtrans-package-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.artifacts = self.root / "artifacts"
        self.output = self.root / "output"
        self.version = "8.7.6-test"
        self.sources = {
            "base.tsv": "#! layer=base\r\nkey\tko\tpos\tid\n学生\t학생\tN110\t1\n".encode(),
            "kana.tsv": "﻿#! layer=kana\r\nがくせい\t学生\t\r\n".encode(),
            ".tsv": b"# UTF-8 filename order\n",
            "\U00010000.tsv": b"# supplementary filename\n",
        }
        for name, content in self.sources.items():
            self.write(self.source / "dicts" / name, content)
        self.write(self.source / "dicts/ignored.txt", b"not dictionary data\n")
        self.write(self.source / "npm/package.json", b'{"version":"0.1.0"}\n')
        self.ehnd = {path.name: path.read_bytes() for path in (ROOT / "ehnd").iterdir()}
        for name, content in self.ehnd.items():
            self.write(self.source / "ehnd" / name, content)
        for name in (
            "README.txt", "LICENSE", "NOTICE", "THIRD-PARTY-NOTICES.txt",
            "third_party/pcre2/LICENCE.md", "third_party/pcre2/deps/sljit/LICENSE",
            "third_party/libyaml/License",
        ):
            self.write(self.source / name, f"fixture {name}\n".encode())
        for preset in ("windows-x64", "windows-x86", "linux", "mac", "wasm"):
            folder = self.artifacts / f"hdtrans-{preset}"
            self.write(folder / "hdtrans-version.txt", f"{self.version}\n".encode())
            self.write(folder / "ARCH", b"ARM64\n" if preset == "mac" else b"X64\n")
            names = ("hdtrans.exe", "hdTransGUI.exe", "hdTransEngine.dll", "J2KEngine.dll") \
                if preset.startswith("windows-") else ("hdtrans", "libhdTransEngine.so", "libhdTransEngine.dylib")
            for name in names:
                self.write(folder / name, f"{preset}/{name}\n".encode())
            data = folder / "hdtrans-data"
            self.write(data / "user/hdtrans.config.example.yaml", b"# user configuration\n")
            self.write(data / "user/userdict.example.txt", b"# user dictionary\n")
            self.write(data / "user/names.example.txt", b"# name list\n")
            self.write(data / "dicts/stale.tsv", b"must not be shipped\n")
            self.write(data / "dicts.txt", b"stale metadata\n")
            self.write(data / "dicts.hdsnap", b"stale snapshot\n")
        self.snapshot = self.root / "shared.hdsnap"
        self.snapshot.write_bytes(b"packaging fixture\0" + bytes(range(256)))

    @staticmethod
    def write(path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def expected_version(self):
        digest = hashlib.sha256()
        for name in sorted(self.sources, key=lambda name: name.encode("utf-8")):
            digest.update(name.encode("utf-8") + b"\0" + self.sources[name] + b"\0")
        return f"0.1.0+{digest.hexdigest()[:12]}\n".encode()

    def package(self, *dictionary_args):
        result = subprocess.run(
            [sys.executable, "-I", str(SCRIPT), "--source", str(self.source),
             "--artifacts", str(self.artifacts), "--output", str(self.output), *dictionary_args],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0,
                         f"packager failed\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}")

    def inspect_packages(self, expected_snapshot=None):
        names = {
            f"hdTrans-{self.version}-windows.zip",
            f"hdTrans-{self.version}-linux-x64.tar.gz",
            f"hdTrans-{self.version}-macos-arm64.tar.gz",
        }
        self.assertEqual({path.name for path in self.output.iterdir()}, names | {"SHA256SUMS.txt"})
        sums = "".join(
            f"{hashlib.sha256((self.output / name).read_bytes()).hexdigest()}  {name}\n"
            for name in sorted(names)
        )
        self.assertEqual((self.output / "SHA256SUMS.txt").read_text(), sums)
        shared_snapshot = expected_snapshot
        common = {"README.txt", "LICENSE", "NOTICE", "THIRD-PARTY-NOTICES.txt"}
        for name in sorted(names):
            with self.subTest(archive=name):
                path = self.output / name
                if name.endswith(".zip"):
                    with zipfile.ZipFile(path) as archive:
                        members = archive.namelist()
                        files = {member: archive.read(member) for member in members}
                    binaries = {"hdtrans.exe", "hdTransGUI.exe", "hdTransEngine.dll",
                                "J2KEngine.dll", "hdTransEngine_x86.dll", "hdtrans_x86.exe"}
                else:
                    prefix = f"hdTrans-{self.version}/"
                    with tarfile.open(path, "r:gz") as archive:
                        members = [member.name for member in archive.getmembers()]
                        files = {member.name.removeprefix(prefix): archive.extractfile(member).read()
                                 for member in archive.getmembers() if member.isfile()}
                        cli = archive.getmember(prefix + "hdtrans")
                        self.assertEqual(cli.mode & 0o777, 0o755)
                    self.assertTrue(all(member == prefix[:-1] or member.startswith(prefix) for member in members))
                    library = "libhdTransEngine.so" if "-linux-" in name else "libhdTransEngine.dylib"
                    binaries = {"hdtrans", library}
                self.assertFalse(any(PurePosixPath(member).suffix.lower() == ".tsv" for member in members))
                self.assertFalse(any("dicts" in PurePosixPath(member).parts for member in members))
                data = "hdtrans-data/"
                ehnd = {data + "ehnd/" + name for name in self.ehnd}
                self.assertEqual(set(files), common | binaries | ehnd | {
                    data + "dicts.hdsnap", data + "dicts.txt",
                    data + "user/hdtrans.config.example.yaml", data + "user/userdict.example.txt",
                    data + "user/names.example.txt",
                })
                self.assertEqual(files[data + "dicts.txt"], self.expected_version())
                if shared_snapshot is None:
                    shared_snapshot = files[data + "dicts.hdsnap"]
                    self.assertGreater(len(shared_snapshot), 160)
                self.assertEqual(files[data + "dicts.hdsnap"], shared_snapshot)
                self.assertEqual(files[data + "user/hdtrans.config.example.yaml"], b"# user configuration\n")
                self.assertEqual(files[data + "user/userdict.example.txt"], b"# user dictionary\n")
                self.assertEqual(files[data + "user/names.example.txt"], b"# name list\n")
                for name, content in self.ehnd.items():
                    self.assertEqual(files[data + "ehnd/" + name], content)
                for notice in ("third_party/pcre2/LICENCE.md", "third_party/pcre2/deps/sljit/LICENSE",
                               "third_party/libyaml/License"):
                    self.assertIn(f"=== {notice} ===".encode(), files["THIRD-PARTY-NOTICES.txt"])
        for name, content in self.sources.items():
            self.assertEqual((self.source / "dicts" / name).read_bytes(), content)

    def test_snapshot_archives_share_data_and_metadata(self):
        self.package("--snapshot", str(self.snapshot))
        self.inspect_packages(self.snapshot.read_bytes())

    def test_hash_includes_original_kana_bytes_and_utf8_basenames(self):
        self.assertEqual(packager.dictionary_version(self.source).encode(), self.expected_version())
        previous = packager.dictionary_version(self.source)
        self.sources["kana.tsv"] = self.sources["kana.tsv"].replace(b"\r\n", b"\n")
        (self.source / "dicts/kana.tsv").write_bytes(self.sources["kana.tsv"])
        self.assertNotEqual(packager.dictionary_version(self.source), previous)
        self.assertEqual(packager.dictionary_version(self.source).encode(), self.expected_version())

    @unittest.skipUnless(os.environ.get("HDTRANS_PACKAGE_TEST_CLI"), "set HDTRANS_PACKAGE_TEST_CLI for native compilation")
    def test_cli_compiles_one_shared_snapshot(self):
        self.package("--cli", str(Path(os.environ["HDTRANS_PACKAGE_TEST_CLI"]).resolve()))
        self.inspect_packages()


if __name__ == "__main__":
    unittest.main()
