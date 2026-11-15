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

"""Build the setup .exe from a package ZIP that package.py made."""
import argparse
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import zipfile

import package

ROOT = package.ROOT
SCRIPT = ROOT / "installer" / "Sable.nsi"
STAGING = ROOT / "installer" / "obj"


def version_number(version):
    """NSIS wants a plain a.b.c.d version for the file's properties."""
    match = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)(?:-[0-9A-Za-z.-]+)?", version)
    if not match:
        raise ValueError(f"not a release version: {version}")
    return ".".join(match.groups()) + ".0"


def uninstall_list(files):
    """Delete lines for exactly the packaged files, then their folders,
    deepest first, so uninstalling never touches anything else."""
    folders = set()
    lines = []
    for name in sorted(files):
        path = PurePosixPath(name)
        lines.append(f'\tDelete "$INSTDIR\\{str(path).replace("/", chr(92))}"')
        folders.update(str(parent) for parent in path.parents
                       if str(parent) != ".")
    for folder in sorted(folders, key=lambda f: (-f.count("/"), f)):
        lines.append(f'\tRMDir "$INSTDIR\\{folder.replace("/", chr(92))}"')
    return "\n".join(lines) + "\n"


def makensis():
    found = shutil.which("makensis")
    if found:
        return found
    for base in (os.environ.get("ProgramFiles(x86)"),
                 os.environ.get("ProgramFiles")):
        if base and (Path(base) / "NSIS" / "makensis.exe").is_file():
            return str(Path(base) / "NSIS" / "makensis.exe")
    raise FileNotFoundError("makensis wasn't found; install NSIS 3")


def build(archive, output):
    manifest = package.verify(archive)
    version = manifest["version"]
    platform = manifest["platform"]
    if platform not in package.MACHINES:
        raise ValueError(f"not an app package: {archive.name}")
    target = output / f"{archive.stem}-setup.exe"
    if target.exists():
        raise FileExistsError(f"{target.name} already exists")

    staging = STAGING / archive.stem
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    with zipfile.ZipFile(archive) as zip:
        prefix = next(name for name in zip.namelist() if name.endswith(
            "/PACKAGE_MANIFEST.json")).removesuffix("PACKAGE_MANIFEST.json")
        zip.extractall(staging)
    source = staging / prefix.rstrip("/")
    files = [*manifest["files"], "PACKAGE_MANIFEST.json"]
    listing = staging / "uninstall.nsh"
    listing.write_text(uninstall_list(files), encoding="utf-8", newline="\n")

    partial = target.with_name(target.name + ".partial")
    partial.unlink(missing_ok=True)
    subprocess.run([
        makensis(), "/V2", "/WX",
        f"/DVERSION={version}",
        f"/DVERSION_NUMBER={version_number(version)}",
        f"/DARCH={platform}",
        f"/DSOURCE={source}",
        f"/DUNINSTALL_LIST={listing}",
        f"/DOUTFILE={partial}",
        str(SCRIPT)], check=True)
    partial.rename(target)
    shutil.rmtree(staging)
    print(f"installer: {target.name} ({target.stat().st_size:,} bytes)",
          flush=True)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path,
                        help="a Sable-<version>-<platform>.zip from package.py")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    build(args.archive.resolve(), package.inside_root(args.output))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"installer failed: {error}")
