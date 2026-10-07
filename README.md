# Sable

A dark-mode PDF reader for Windows. It's a native port of
[NightPDF](https://github.com/Lunarequest/NightPDF), which was an Electron app
built on PDF.js and has since been archived. Sable is C++/WinRT with WinUI 3
(Windows App SDK) and PDFium, and the dark filter is done in Direct2D.

The plan is parity first: same features, presets, colours, keybinds, settings
and command line as NightPDF. After that it grows into a full reader, with
comments, Fill & Sign, page organizing and more, all offline. See
[docs/DESIGN.md](docs/DESIGN.md).

Status: very early. Nothing runs yet.

## Credits

Sable is based on [NightPDF](https://github.com/Lunarequest/NightPDF), first
released by Joe Loya and later maintained at Lunarequest/NightPDF (copyright
Advaith Madhukar and contributors). The design, the presets and pretty much
all of the behaviour come from there.

Sable is not an official successor to NightPDF and isn't endorsed by its
maintainer.

## Building

You'll need:

-   Windows 10 or 11 (x64 or ARM64)
-   Visual Studio 2022 (17.10 or later) with the "WinUI application
    development" workload (including "C++ WinUI app development tools") and
    "Desktop development with C++"
-   [uv](https://docs.astral.sh/uv/), which takes care of Python 3.12

```powershell
uv run update_pdfium.py --pinned      # download the pinned PDFium build
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run.ps1 file.pdf
```

Run `uv run update_pdfium.py` without `--pinned` to move to the latest
[pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) release.

The pre-commit hook checks formatting. Turn it on with
`git config core.hooksPath .githooks`.

## Licenses

-   [Sable](LICENSE) is under [GPLv2 only](LICENSE)
-   [NightPDF](https://github.com/Lunarequest/NightPDF), which Sable is derived from, is under [GPLv2 only](https://github.com/Lunarequest/NightPDF/blob/mistress/LICENSE)
-   [Windows App SDK](https://github.com/microsoft/WindowsAppSDK) is under [MIT](https://github.com/microsoft/WindowsAppSDK/blob/main/LICENSE)
-   [PDFium](https://pdfium.googlesource.com/pdfium/) is under [BSD-3-Clause, with the Apache License 2.0 text included](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/LICENSE)
-   [Manrope](https://github.com/googlefonts/manrope), the UI font, is under the [SIL Open Font License 1.1](app/assets/fonts/OFL.txt)
-   Everything else is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
