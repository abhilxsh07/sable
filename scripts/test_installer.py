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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

import unittest

import installer


class InstallerTests(unittest.TestCase):
    def test_version_number_drops_the_prerelease(self):
        self.assertEqual(installer.version_number("v1.0.0-alpha.1"),
                         "1.0.0.0")
        self.assertEqual(installer.version_number("v2.13.4"), "2.13.4.0")

    def test_version_number_refuses_other_text(self):
        for bad in ("1.0.0", "v1.0", "v1.0.0 beta", "v1.0.0-alpha/1"):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    installer.version_number(bad)

    def test_uninstall_list_names_only_packaged_files(self):
        text = installer.uninstall_list(
            ["Sable.exe", "assets/icon.png", "assets/fonts/OFL.txt"])
        self.assertEqual(text.splitlines(), [
            '\tDelete "$INSTDIR\\Sable.exe"',
            '\tDelete "$INSTDIR\\assets\\fonts\\OFL.txt"',
            '\tDelete "$INSTDIR\\assets\\icon.png"',
            '\tRMDir "$INSTDIR\\assets\\fonts"',
            '\tRMDir "$INSTDIR\\assets"',
        ])
        self.assertNotIn("/r", text)


if __name__ == "__main__":
    unittest.main()
