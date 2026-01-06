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

"""Package an existing Release build without copying its runtime tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MACHINES = {"x64": 0x8664, "ARM64": 0xAA64}
DEVELOPMENT = {".pdb", ".obj", ".lib", ".exp", ".ilk", ".iobj", ".ipdb"}


def git(*args):
    return subprocess.check_output(
        ["git", *args], cwd=ROOT,
        env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"})


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inside_root(path):
    resolved = path.resolve()
    if not resolved.is_relative_to(ROOT) or resolved == ROOT:
        raise ValueError("the package directory must be inside this repository")
    if ".git" in resolved.relative_to(ROOT).parts:
        raise ValueError("git metadata is not a package directory")
    return resolved


def collect(directory):
    files = {}
    for path in sorted(directory.rglob("*")):
        if path.is_symlink() or path.is_junction():
            raise ValueError(f"linked package input: {path}")
        if path.is_file():
            files[path.relative_to(directory).as_posix()] = path
    return files


def runtime_files(directory, platform):
    files = {}
    for name, path in collect(directory).items():
        parts = PurePosixPath(name).parts
        if (parts[0].lower() in {"fixtures", "tests"}
                or path.suffix.lower() in DEVELOPMENT
                or path.name.lower().startswith(("sable.tests", "gtest"))):
            continue
        files[name] = path
    required = ["Sable.exe", "Sable.pri", "pdfium.dll", "Microsoft.ui.xaml.dll",
                "App.xbf", "MainWindow.xbf", "HomePage.xbf", "ViewerPage.xbf",
                "assets/fonts/Manrope-VF.ttf", "assets/fonts/OFL.txt"]
    for name in required:
        if name not in files:
            raise ValueError(f"missing runtime file: {name}")
    for name in ("Sable.exe", "pdfium.dll", "Microsoft.ui.xaml.dll"):
        try:
            with files[name].open("rb") as stream:
                if stream.read(2) != b"MZ":
                    raise ValueError(f"not a Windows binary: {name}")
                stream.seek(0x3C)
                stream.seek(struct.unpack("<I", stream.read(4))[0])
                signature, machine = struct.unpack("<4sH", stream.read(6))
        except struct.error:
            raise ValueError(f"truncated Windows binary: {name}") from None
        if signature != b"PE\0\0" or machine != MACHINES[platform]:
            raise ValueError(f"wrong binary architecture: {name}")
    pinned = ROOT / "third_party/pdfium" / platform.lower() / "bin/pdfium.dll"
    if digest(files["pdfium.dll"]) != digest(pinned):
        raise ValueError("the output PDFium DLL differs from the pinned input")
    return files


def notices(platform):
    files = {name: ROOT / name for name in
             ("LICENSE", "THIRD_PARTY_NOTICES.md", "VERSION")}
    pdfium = ROOT / "third_party/pdfium" / platform.lower()
    files["licenses/pdfium-binaries/LICENSE"] = pdfium / "LICENSE"
    for name, path in collect(pdfium / "licenses").items():
        files[f"licenses/pdfium/{name}"] = path
    triplet = f"{platform.lower()}-windows-static"
    share = ROOT / "vcpkg_installed" / triplet / triplet / "share"
    for name in ("fmt", "nlohmann-json", "spdlog"):
        files[f"licenses/{name}/copyright"] = share / name / "copyright"
    packages = ET.parse(ROOT / "app/packages.config").getroot()
    for item in packages:
        package = f"{item.attrib['id']}.{item.attrib['version']}"
        if item.attrib["id"].startswith("Microsoft.Windows.SDK.BuildTools"):
            continue
        directory = ROOT / "packages" / package
        texts = {name: path for name, path in collect(directory).items()
                 if re.search("license|notice", path.name, re.IGNORECASE)
                 and path.suffix.lower() in {"", ".txt", ".md"}}
        if not texts:
            raise ValueError(f"missing package license texts: {package}")
        for name, path in texts.items():
            files[f"licenses/nuget/{package}/{name}"] = path
    for path in files.values():
        if not path.is_file():
            raise ValueError(f"missing notice: {path}")
    return files


def source_snapshot():
    names = git("ls-files", "-z", "--cached", "--others", "--exclude-standard")
    files = {}
    tree = hashlib.sha256()
    for raw in sorted(set(names.split(b"\0")) - {b""}):
        name = raw.decode("utf-8")
        path = ROOT / name
        if not path.is_file() or path.is_symlink() or path.is_junction():
            raise ValueError(f"missing or linked source input: {name}")
        sha = digest(path)
        tree.update(raw + b"\0" + sha.encode("ascii") + b"\n")
        files[name] = path
    return files, tree.hexdigest()


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def archive(output, prefix, files, metadata, extra=None):
    if output.exists() or output.with_suffix(".zip.partial").exists():
        raise ValueError(f"refusing to replace an existing archive: {output}")
    if len({name.casefold() for name in files}) != len(files):
        raise ValueError("package paths collide on Windows")
    records = {name: {"bytes": path.stat().st_size, "sha256": digest(path)}
               for name, path in files.items()}
    payload = dict(files)
    payload.update(extra or {})
    if "PACKAGE_MANIFEST.json" in payload:
        raise ValueError("the manifest path is reserved")
    if set(files) & set(extra or {}):
        raise ValueError("package metadata would replace an input file")
    for name, data in (extra or {}).items():
        records[name] = {"bytes": len(data),
                         "sha256": hashlib.sha256(data).hexdigest()}
    payload["PACKAGE_MANIFEST.json"] = json_bytes(
        {**metadata, "schema": 1, "files": records})
    partial = output.with_suffix(".zip.partial")
    output.parent.mkdir(parents=True, exist_ok=True)
    with partial.open("xb") as stream, zipfile.ZipFile(
            stream, "w", compression=zipfile.ZIP_DEFLATED) as zip:
        for name, value in sorted(payload.items()):
            parts = PurePosixPath(name)
            if parts.is_absolute() or ".." in parts.parts or "\\" in name:
                raise ValueError(f"unsafe archive path: {name}")
            info = zipfile.ZipInfo(f"{prefix}/{name}", (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            with zip.open(info, "w") as destination:
                if isinstance(value, Path):
                    with value.open("rb") as source:
                        shutil.copyfileobj(source, destination, 1024 * 1024)
                else:
                    destination.write(value)
    verify(partial)
    partial.rename(output)
    print(f"package: {output.name} ({output.stat().st_size:,} bytes)",
          flush=True)


def verify(path):
    with zipfile.ZipFile(path) as zip:
        names = zip.namelist()
        for name in names:
            parts = PurePosixPath(name)
            if (parts.is_absolute() or ".." in parts.parts or "\\" in name
                    or ":" in name or parts.as_posix() != name):
                raise ValueError(f"unsafe archive path: {name}")
        manifests = [name for name in names if name.endswith(
            "/PACKAGE_MANIFEST.json")]
        unique = {name.casefold() for name in names}
        if len(manifests) != 1 or len(unique) != len(names):
            raise ValueError("missing manifest or duplicate archive entries")
        prefix = manifests[0].removesuffix("PACKAGE_MANIFEST.json")
        manifest = json.loads(zip.read(manifests[0]))
        expected = {prefix + name for name in manifest["files"]}
        if set(names) != expected | {manifests[0]}:
            raise ValueError("archive contents differ from the manifest")
        for name, record in manifest["files"].items():
            with zip.open(prefix + name) as stream:
                sha = hashlib.file_digest(stream, "sha256").hexdigest()
            if (sha != record["sha256"]
                    or zip.getinfo(prefix + name).file_size != record["bytes"]):
                raise ValueError(
                    f"archive input changed or was damaged: {name}")
    return manifest


def checksums(directory):
    paths = sorted(directory.glob("*.zip"))
    if not paths:
        raise ValueError("no release ZIPs found")
    snapshots = set()
    for path in paths:
        metadata = verify(path)
        keys = ("version", "sourceCommit", "sourceSnapshotSha256",
                "uncommittedChanges", "localCandidate", "tag")
        snapshots.add(tuple(metadata.get(key) for key in keys))
    if len(snapshots) != 1:
        raise ValueError(
            "release archives come from different source snapshots")
    text = "".join(f"{digest(path)}  {path.name}\n" for path in paths)
    (directory / "SHA256SUMS.txt").write_text(
        text, encoding="utf-8", newline="\n")
    print(f"checksums: {len(paths)} archives", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=MACHINES)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--tag")
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--include-source", action="store_true")
    parser.add_argument("--verify", type=Path)
    parser.add_argument("--checksums", type=Path)
    args = parser.parse_args()
    if args.verify:
        result = verify(args.verify)
        print(f"verified: {args.verify.name}, {len(result['files'])} files")
        return
    if args.checksums:
        checksums(inside_root(args.checksums))
        return
    if not args.platform:
        parser.error("--platform is required when packaging")
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?", version):
        raise ValueError("VERSION is not a safe release version")
    vcpkg = json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8"))
    if vcpkg["version-string"] != version.removeprefix("v"):
        raise ValueError("VERSION and vcpkg.json disagree")
    dirty = bool(git("status", "--porcelain", "--untracked-files=all"))
    if dirty and not args.allow_dirty:
        raise ValueError("commit the intended source changes first; "
                         "use --allow-dirty for a local candidate")
    commit = git("rev-parse", "HEAD").decode().strip()
    if args.tag:
        if dirty or args.tag != version:
            raise ValueError("a tagged package needs clean source "
                             "and a matching VERSION")
        target = git("rev-parse",
                     f"refs/tags/{args.tag}^{{commit}}").decode().strip()
        if target != commit:
            raise ValueError("the release tag does not point to this checkout")
    output = inside_root(args.output)
    default_build = ROOT / args.platform / "Release/Sable"
    directory = (args.build_dir or default_build).resolve()
    if output == directory or output.is_relative_to(directory):
        raise ValueError("package output cannot be inside the runtime input")
    files = runtime_files(directory, args.platform)
    files.update(notices(args.platform))
    notes = ROOT / "docs/releases" / f"{version}.md"
    files["RELEASE_NOTES.md"] = notes
    source, tree = source_snapshot()
    local = not args.tag
    label = f"Sable-{version}{'-local' if local else ''}"
    metadata = {"version": version, "platform": args.platform,
                "sourceCommit": commit, "sourceSnapshotSha256": tree,
                "uncommittedChanges": dirty, "localCandidate": local,
                "tag": args.tag,
                "pdfium": (ROOT / ".pdfium_version").read_text().strip()}
    readme = (f"Sable {version} ({args.platform})\n\n"
              "Extract the entire folder before running Sable.exe. Keep all\n"
              "runtime, assets, styles and locale files beside the app.\n"
              "This portable preview has no installer or automatic updates.\n"
              "Settings and logs use Sable's usual application-data folders.\n"
              "Read RELEASE_NOTES.md and THIRD_PARTY_NOTICES.md "
              "before sharing.\n")
    if local:
        readme += "\nLOCAL CANDIDATE: source is not tied to a release tag.\n"
    archive(output / f"{label}-{args.platform}.zip", f"{label}-{args.platform}",
            files, metadata, {"PACKAGE_README.txt": readme.encode("utf-8")})
    if args.include_source:
        archive(output / f"{label}-source.zip", f"{label}-source", source,
                {**metadata, "platform": "source"})
    checksums(output)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError,
            zipfile.BadZipFile) as error:
        print(f"package failed: {error}", file=sys.stderr)
        sys.exit(1)
