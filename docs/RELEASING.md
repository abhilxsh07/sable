# Releasing Sable

The first distribution is a portable alpha ZIP for each architecture,
unsigned and without file associations (DECISIONS.md, 12 November). An
installer and signing are for a later release. The release workflow creates a draft prerelease; publication is
an owner step after the checks below.

## Prepare the source

Review and commit all intended changes before making a release tag. A tag
only contains committed source. Keep the existing push queue for branch
commits; no script here commits, pushes, changes remotes or rewrites tags.

VERSION currently says v1.0.0-alpha.1. vcpkg.json must have the same version
without the leading v, and docs/releases/<version>.md must exist. If the
version has already been used, choose the next version and update those
three files before building. Never move an existing public tag.

Use `git status --short` to review the checkout and `git diff --check` to
catch whitespace errors. Keep feature commits separate and verify each exact
staged tree from an archive outside the repo, so it cannot rely on
uncommitted build fixes.

## Build and package

From the repository root, with the pinned PDFium download present:

```powershell
python update_pdfium.py --pinned
./scripts/build.ps1 -Config Release -Platform x64
./scripts/test.ps1 -Config Release -Platform x64
./scripts/build.ps1 -Config Release -Platform ARM64
python -m unittest discover -s scripts -p test_package.py
```

The package script uses Python 3.12's standard library. It reads an existing
Release output, checks the executable/runtime architecture and pinned
PDFium DLL, adds the actual license/notice texts and writes ZIPs without a
second runtime staging copy. Tests, fixtures, PDBs and linker files are
excluded. ZIPs have stable ordering/timestamps and a manifest with SHA-256
hashes. This is not a claim that separate compiler builds are byte-identical.

Before committing, local review packages can be made explicitly:

```powershell
python scripts/package.py --platform x64 --allow-dirty --include-source
python scripts/package.py --platform ARM64 --allow-dirty
```

These produce `-local-` filenames and record the uncommitted source status.
They are review candidates. The manifest identifies the base commit and a
hash of the actual tracked/untracked source snapshot; the matching source
ZIP includes those file contents. Build outputs, dependencies and ignored
local files are excluded from that source archive. Dependencies are restored
from the pinned versions and manifests when rebuilding.

An alternate build output can be supplied with `--build-dir`. `--output`
must stay inside the repository, outside the input runtime folder. The
script refuses to overwrite ZIPs. Use a fresh output directory for another
candidate. It does not remove old build outputs or archives.

The following checks work for either local or tagged packages:

```powershell
python scripts/package.py --verify dist/Sable-v1.0.0-alpha.1-local-x64.zip
python scripts/package.py --checksums dist
Get-FileHash dist/*.zip -Algorithm SHA256
```

Compare each hash with SHA256SUMS.txt. The ZIP manifest checks integrity and
records provenance; it is not a digital signature or a substitute for
getting the files from a trusted release.

## Check the extracted package

Extract the x64 ZIP into a fresh folder with spaces in its name. From the
repository root, point the startup check at the folder containing Sable.exe:

```powershell
./scripts/smoke.ps1 -Directory 'C:\path\to\extracted\Sable-folder' `
  -WorkDirectory "$PWD/dist/smoke-work"
```

This checks help/version, opening a PDF and normal close from a separate
working directory. It only closes the process it starts, and restores its
temporary diagnostic environment changes. It cannot prove that a second
machine has everything it needs.

The ZIP is framework-dependent (DECISIONS.md, 12 November): it needs
Microsoft's Windows App Runtime, which the build PC already has, so the
smoke check can't show what happens without it.

On a clean Windows PC or VM without Visual Studio or any Windows App
Runtime, use a standard account and extract the ZIP. Run Sable.exe first
without the runtime: it should say which runtime version and architecture
it needs and offer Microsoft's download page, then exit. Record the exact
message. Install that runtime from the page as the standard user, then
repeat opening, rendering, Find/copy, Save a Copy, Settings and print
preview.
Record Windows version/build, architecture, package hash and result. Do not
turn off Windows protections to make the check pass. If Windows blocks an
unsigned build, record the exact message; signing/distribution is an owner
decision. Run the ARM64 package on actual Windows on ARM as a separate check.
Compilation on the x64 host is not an ARM64 runtime check.

## Desktop sign-off

Follow READER_CHECKS.md and record results with the package hash:

1. In Settings, switch Dark to Obsidian (AMOLED), reopen Settings, open a
   second window and restart Sable. The saved choice should persist and PDF
   filter values should stay the same. Check narrow layouts, hover/focus,
   shortcut capture, cancellation, clearing and Save.
2. On text.pdf, check page drafts, Ctrl+Alt+G, J/K, zoom aliases, R/Shift+R,
   H/S, Find and sidebar focus. Test presentation entry/exit, clicks/wheel,
   first/last page and state restoration. Touch/pen checks need that hardware.
3. On attachments.pdf, cancel Save a Copy, then save to a new path and compare
   SHA-256 with the source. Check source-path rejection and Windows overwrite
   confirmation using a disposable destination. Copy text into another app.
4. On print.pdf, inspect the real preview and save with Microsoft Print to
   PDF. On pages.pdf, select a range, try landscape and cancel a pending job.
   The automated driver-output check already passes, but it supplies options
   itself. Physical printer output and copies/collation need a suitable device.
5. With Spotify, then Apple Music, change tracks, pause, resume, seek and
   use next/previous. With both players open, check the playing session wins
   and stale artwork/text do not return. Test bright, dark and missing art.
6. Move the window between monitors with different scaling and return it.
   Check fit, clarity, selection alignment and pointer hit testing.
7. Press Shift+Alt+M a few times on home and in a document. Green MORTY!
   alerts pile up at the top, newest first, and fade out one by one.

If a device/player is unavailable, record that check as unverified and keep
it in the release notes. Do not count simulated checks as physical passes.

## License review before sharing binaries

The package carries Sable's GPL-2.0-only license, PDFium's downloaded
notices, font/license texts and the actual restored package terms. It no
longer ships Microsoft's runtime, only its bootstrapper DLL; people install
the runtime from Microsoft under Microsoft's terms.
THIRD_PARTY_NOTICES.md maps every shipped DLL to its component and terms.
Re-check that table against a fresh package whenever a dependency changes.

PDFium's Apache-2.0 text (D10) was accepted on 10 November. One question
is still open: the Windows App SDK packages' Microsoft Software License
Terms (sections 2 and 3, summarized in THIRD_PARTY_NOTICES.md) next to
Sable's GPL-2.0-only license. Have the intended distribution reviewed by
someone qualified to assess those terms and keep the result in
DECISIONS.md. Do not relicense Sable, remove notices or assume that the
former Electron app's distribution settles this question. Packaging is
complete when it preserves the supplied texts; distribution clearance is a
separate decision.

## Create and review the draft

After committing, let the push queue deliver the source branch. Confirm
GitHub has the intended commit and its build/test CI has passed. No release
can include the local working changes until they are committed and available
at the selected tag.

Create an annotated tag at that exact commit on your machine, after checking
that it does not already exist locally or on GitHub:

```powershell
git tag -a v1.0.0-alpha.1 -m "Sable v1.0.0-alpha.1"
```

The tag must then reach GitHub through your approved queue/tag-push process.
The repository scripts do not push it. A tag matching `v*.*.*` runs
.github/workflows/release.yaml. It validates VERSION, builds x64/ARM64,
runs x64 and packaging tests, verifies the ZIPs, performs the x64 extraction
startup check, and collects both portable ZIPs plus the source ZIP and hashes.
It then creates a draft prerelease with the prepared notes. An existing
release makes creation fail rather than replacing its assets.

The workflow can also be started manually with an existing tag, once the
workflow is on the default branch. Check both matrix jobs and the draft job.
Local static validation does not replace this first GitHub Actions run.

In GitHub Releases, review the draft's commit, version, files, checksums and
known limitations. Download and check the actual draft assets rather than
substituting the local candidates. Keep it a draft until desktop/clean-PC
checks and the licensing decision are recorded. Keep "pre-release" selected
when publishing this alpha. Publication is a separate owner action.
