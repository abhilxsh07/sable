# Decisions

Why things are the way they are, so I don't have to remember it all later.

## Name: Sable (2026-10-06)

This one wasn't really my call. When NightPDF was archived, its maintainer
posted a goodbye note ([issue #73, "The End"](https://github.com/Lunarequest/NightPDF/issues/73))
that says forking is fine, with one condition: "do not use the NightPDF name."
Fair enough, so the port needed a name of its own.

It helps in practice too. The Electron app keeps its config in
`%APPDATA%\NightPDF`, and with the same name both apps would be fighting over
that folder. I also looked at Vesper, Umbra, Nocturne, DuskPDF and NightOwl
PDF, but a few of those are already used by other apps.

- App data goes in `%APPDATA%\Sable`, logs in `%LOCALAPPDATA%\Sable\logs`
- AppUserModelID `Sable.App`, exe `Sable.exe`, solution `Sable.sln`
- Window title and version string say Sable
  (`Sable: <VERSION> PDFium: ... WindowsAppSDK: ...`)

The rename stops at the product name. Names that come from upstream code
(`NightPDFSettings`, `nightpdf_default_settings` and so on), settings keys,
keybind action strings and the CLI stay exactly as they were. That way I can
grep both codebases for the same thing.

## New repo, not a fork (2026-10-06)

NightPDF is archived, so a fork didn't buy much. This is a fresh repo. The
README credits NightPDF, and every ported function gets a `// port of ...`
comment pointing at the file it came from.

## Versioning (2026-10-06)

New app, so it starts at v1.0.0-alpha.1. `VERSION` is the only place the
version lives.

## Dependencies (2026-10-06)

Microsoft packages (Windows App SDK, C++/WinRT, WIL) come from NuGet.
Everything else (nlohmann/json, CLI11, spdlog, GoogleTest) comes from vcpkg in
manifest mode, with a pinned baseline.

## License header (2026-10-06)

Every file keeps Advaith Madhukar's 2021 copyright line from NightPDF and adds
mine below it. First line of the header: "Sable, a native port of NightPDF".

## Still undecided

- Help menu links. Upstream is archived and its issues are closed, so pointing
  there makes no sense.
- Whether to import settings from an existing NightPDF install.
- Font: Segoe UI Variable (the WinUI default) or Arial like upstream.
- File associations other than .pdf.
- Packaging. Probably Velopack, but that can wait until there's something to
  ship.
- PDFium's license file includes the Apache-2.0 text, see
  THIRD_PARTY_NOTICES.md.

## Pinned versions

- PDFium `chromium/8086` (bblanchon/pdfium-binaries)
- vcpkg baseline `c76c06644034521fb761a39f8f52d8e87d1103d5` (the 2026.07.29
  release)
- Windows App SDK 2.5.1 (meta package; its sub-packages are pinned in
  `app/packages.config`, which is the actual source of truth)
- C++/WinRT 3.0.260818.1, WIL 1.0.260126.7, Windows SDK build tools
  10.0.28000.2705
- Built with VS 2022 17.14 (MSVC v143)

## Differences from NightPDF

- `update_pdfium.py --pinned`: upstream committed pdf.js straight into the
  repo, so `update_pdfjs.py` only ever had to update it. I'm not committing
  the PDFium binaries, so a fresh clone needs a way to download the pinned
  version.
