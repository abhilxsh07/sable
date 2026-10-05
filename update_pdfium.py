#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = ["requests"]
# ///
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

# port of update_pdfjs.py: same steps (get_version -> get_zip -> unzip_zip ->
# cleanup -> write the version file), but for PDFium prebuilt by
# bblanchon/pdfium-binaries.
# see: https://github.com/bblanchon/pdfium-binaries/releases/latest
#
# Usage:
#   uv run update_pdfium.py           update to the latest release
#   uv run update_pdfium.py --pinned  fetch the version in .pdfium_version
#                                     (fresh clones: the binaries are gitignored)
import os
import sys
import tarfile
from shutil import rmtree
from urllib.parse import quote

import requests

ROOT = os.path.dirname(os.path.abspath(__file__))
VERSION_FILE = os.path.join(ROOT, ".pdfium_version")
PDFIUM_DIR = os.path.join(ROOT, "third_party", "pdfium")
# asset name -> folder under third_party/pdfium/
ASSETS = {
    "pdfium-win-x64.tgz": "x64",
    "pdfium-win-arm64.tgz": "arm64",
}


def read_current_version() -> str:
    if not os.path.exists(VERSION_FILE):
        return ""
    with open(VERSION_FILE, "r") as file:
        return file.read().strip()


def get_version() -> str | None:
    with requests.get(
        "https://api.github.com/repos/bblanchon/pdfium-binaries/releases/latest",
        timeout=30,
    ) as req:
        if req.status_code == 200:
            json = req.json()
            version = json.get("tag_name")
            if version:
                if read_current_version() == version and os.path.isdir(
                    PDFIUM_DIR
                ):
                    print("current version is the latest version")
                else:
                    return version
        else:
            print(f"GitHub API returned {req.status_code}")


def get_zip(version: str):
    os.makedirs(PDFIUM_DIR, exist_ok=True)
    for asset in ASSETS:
        # the tag contains a "/" (chromium/NNNN), which must be encoded
        request = requests.get(
            f"https://github.com/bblanchon/pdfium-binaries/releases/download/{quote(version, safe='')}/{asset}",
            timeout=300,
        )
        request.raise_for_status()
        with open(os.path.join(PDFIUM_DIR, asset), "wb") as fd:
            fd.write(request.content)


def unzip_zip(version: str):
    # cleanup pdfium dirs
    for asset, folder in ASSETS.items():
        target = os.path.join(PDFIUM_DIR, folder)
        if os.path.isdir(target):
            rmtree(target)
        with tarfile.open(os.path.join(PDFIUM_DIR, asset), "r:gz") as tgz:
            tgz.extractall(target, filter="data")


def cleanup(version: str):
    for asset in ASSETS:
        os.remove(os.path.join(PDFIUM_DIR, asset))


if __name__ == "__main__":
    if "--pinned" in sys.argv:
        version = read_current_version() or None
        if not version:
            print(".pdfium_version is missing; run without --pinned")
            sys.exit(1)
    else:
        version = get_version()
    if version:
        get_zip(version)
        unzip_zip(version)
        cleanup(version)
        with open(VERSION_FILE, "w") as file:
            file.write(version)
        print(f"PDFium {version} -> third_party/pdfium/{{x64,arm64}}")
