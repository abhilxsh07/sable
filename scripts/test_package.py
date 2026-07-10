# Sable, a native port of NightPDF
# Copyright (C) 2021  Advaith Madhukar
# Copyright (C) 2026  Abhilash Kar
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import package


class PackageTests(unittest.TestCase):
    def setUp(self):
        # This folder belongs to this test invocation, not a build cache.
        self.temp = tempfile.TemporaryDirectory(prefix="sable_package_")
        self.root = Path(self.temp.name).resolve()
        temp_root = Path(tempfile.gettempdir()).resolve()
        self.assertTrue(self.root.is_relative_to(temp_root))
        self.addCleanup(self.temp.cleanup)

    def write(self, name, data=b"payload"):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def runtime(self, machine=0x8664):
        directory = self.root / "build"
        binary = bytearray(134)
        binary[:2] = b"MZ"
        struct.pack_into("<I", binary, 0x3C, 128)
        struct.pack_into("<4sH", binary, 128, b"PE\0\0", machine)
        for name in ("Sable.exe", "pdfium.dll", "Microsoft.ui.xaml.dll"):
            self.write(f"build/{name}", binary)
        for name in ("Sable.pri", "App.xbf", "MainWindow.xbf", "HomePage.xbf",
                     "ViewerPage.xbf", "assets/fonts/Manrope-VF.ttf",
                     "assets/fonts/OFL.txt"):
            self.write(f"build/{name}")
        self.write("third_party/pdfium/x64/bin/pdfium.dll", binary)
        return directory

    def test_archive_preserves_bytes_and_has_no_development_files(self):
        directory = self.runtime()
        for name in ("Sable.Tests.exe", "Sable.pdb", "gtest.dll",
                     "fixtures/a.pdf"):
            self.write(f"build/{name}")
        self.write("build/en-us/Resources.pri", b"locale data")
        with patch.object(package, "ROOT", self.root):
            files = package.runtime_files(directory, "x64")
        archive = self.root / "one.zip"
        package.archive(archive, "Sable", files, {"version": "test"})
        manifest = package.verify(archive)
        self.assertEqual(set(manifest["files"]), set(files))
        self.assertNotIn("gtest.dll", files)
        self.assertNotIn("Sable.Tests.exe", files)
        self.assertNotIn("Sable.pdb", files)
        self.assertNotIn("fixtures/a.pdf", files)
        with zipfile.ZipFile(archive) as zip:
            self.assertEqual(zip.read("Sable/en-us/Resources.pri"),
                             b"locale data")
        # Timestamps and traversal order do not change the same-input archive.
        second = self.root / "two.zip"
        package.archive(second, "Sable", files, {"version": "test"})
        self.assertEqual(package.digest(archive), package.digest(second))

    def test_runtime_omits_webview_dlls(self):
        directory = self.runtime()
        self.write("build/Microsoft.Web.WebView2.Core.dll")
        self.write("build/runtimes/win-x64/native/WebView2Loader.dll")
        with patch.object(package, "ROOT", self.root):
            files = package.runtime_files(directory, "x64")
        self.assertNotIn("Microsoft.Web.WebView2.Core.dll", files)
        self.assertNotIn("runtimes/win-x64/native/WebView2Loader.dll", files)
        self.assertIn("Microsoft.ui.xaml.dll", files)

    def test_source_snapshot_excludes_untracked_files(self):
        self.write("source.txt", b"staged source")
        with patch.object(package, "ROOT", self.root):
            package.git("init", "-q")
            package.git("add", "--", "source.txt")
            original = package.source_snapshot()
            self.write("private-notes.md", b"local notes")
            self.assertEqual(package.source_snapshot(), original)
            self.write("source.txt", b"edited source")
            files, tree = package.source_snapshot()

        self.assertNotEqual(tree, original[1])
        archive = self.root / "source.zip"
        package.archive(archive, "source", files, {"sourceSnapshotSha256": tree})
        self.assertEqual(set(package.verify(archive)["files"]), {"source.txt"})
        with zipfile.ZipFile(archive) as zip:
            self.assertEqual(zip.read("source/source.txt"), b"edited source")

    def test_wrong_architecture_and_missing_or_mismatched_pdfium_fail(self):
        directory = self.runtime()
        with patch.object(package, "ROOT", self.root):
            with self.assertRaisesRegex(ValueError, "architecture"):
                package.runtime_files(directory, "ARM64")
            self.write("build/pdfium.dll", b"broken")
            with self.assertRaisesRegex(ValueError, "Windows binary"):
                package.runtime_files(directory, "x64")
            self.runtime()
            self.write("third_party/pdfium/x64/bin/pdfium.dll", b"different")
            with self.assertRaisesRegex(ValueError, "pinned"):
                package.runtime_files(directory, "x64")
            self.runtime()
            (directory / "Sable.pri").unlink()
            with self.assertRaisesRegex(ValueError, "missing runtime"):
                package.runtime_files(directory, "x64")

    def test_existing_archive_is_never_overwritten(self):
        archive = self.write("keep.zip", b"existing destination")
        with self.assertRaisesRegex(ValueError, "replace"):
            package.archive(archive, "Sable", {}, {})
        self.assertEqual(archive.read_bytes(), b"existing destination")

    def test_tampered_contents_fail_the_manifest(self):
        manifest = {"files": {"a.txt": {"bytes": 4,
                    "sha256": hashlib.sha256(b"good").hexdigest()}}}
        archive = self.root / "bad.zip"
        with zipfile.ZipFile(archive, "w") as zip:
            zip.writestr("Sable/a.txt", b"evil")
            zip.writestr("Sable/PACKAGE_MANIFEST.json", json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "damaged"):
            package.verify(archive)

    def test_case_collisions_and_unsafe_output_fail(self):
        file = self.write("a.txt")
        with self.assertRaisesRegex(ValueError, "collide"):
            package.archive(self.root / "bad.zip", "Sable",
                            {"A.txt": file, "a.txt": file}, {})
        with patch.object(package, "ROOT", self.root):
            with self.assertRaisesRegex(ValueError, "inside"):
                package.inside_root(self.root.parent / "elsewhere")
            with self.assertRaisesRegex(ValueError, "metadata"):
                package.inside_root(self.root / ".git/dist")

    def test_tagged_packaging_requires_clean_matching_source(self):
        self.write("VERSION", b"v1.0.0-alpha.1\n")
        self.write("vcpkg.json", b'{"version-string":"1.0.0-alpha.1"}')
        arguments = ["package.py", "--platform", "x64", "--tag",
                     "v1.0.0-alpha.1", "--allow-dirty"]
        with (patch.object(package, "ROOT", self.root),
              patch.object(sys, "argv", arguments)):
            with patch.object(package, "git", return_value=b" M app/file.cpp"):
                with self.assertRaisesRegex(ValueError, "clean source"):
                    package.main()
            with patch.object(package, "git",
                              side_effect=[b"", b"one", b"two"]):
                with self.assertRaisesRegex(ValueError, "does not point"):
                    package.main()

    def test_checksums_reject_mixed_source_snapshots(self):
        file = self.write("input.txt")
        package.archive(self.root / "one.zip", "one", {"a.txt": file},
                        {"sourceCommit": "one"})
        package.archive(self.root / "two.zip", "two", {"a.txt": file},
                        {"sourceCommit": "two"})
        with self.assertRaisesRegex(ValueError, "different source"):
            package.checksums(self.root)

    def test_unsafe_archive_entries_are_rejected_before_extraction(self):
        archive = self.root / "unsafe.zip"
        with zipfile.ZipFile(archive, "w") as zip:
            zip.writestr("Sable/../outside.txt", b"bad")
        with self.assertRaisesRegex(ValueError, "unsafe archive path"):
            package.verify(archive)


if __name__ == "__main__":
    unittest.main()
