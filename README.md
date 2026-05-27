# Sable

A dark-mode PDF reader for Windows. It's a native port of
[NightPDF](https://github.com/Lunarequest/NightPDF), which was an Electron app
built on PDF.js and has since been archived. Sable is C++/WinRT with WinUI 3
(Windows App SDK) and PDFium, and the dark filter is done in Direct2D.

The plan is parity first: same features, presets, colours, keybinds, settings
and command line as NightPDF. After that it grows into a full reader, with
comments, Fill & Sign, page organizing and more, all offline. See
[docs/DESIGN.md](docs/DESIGN.md).

The native reader opens PDFs in tabs with page labels, thumbnails, password
prompts, night filters and internal/web links. Text selection, Find, document
outlines and attachment saving work across tabs. Reading tools include view
rotation, hand panning, scroll/spread layouts, presentation mode, document
properties and Save a Copy. Untouched copies keep the loaded PDF's bytes;
edited copies include notes and markup. Printing uses original colors.

Settings has General, Shortcuts and About pages. General offers the original
Dark theme and **Obsidian (AMOLED)**, with pure-black resting app surfaces.
The theme changes live and is saved for the next launch. The PDF's night
filter stays separate from the app theme.

Comments > More > Place text box on page adds a single-line box where you
click. Use English letters, numbers and standard punctuation. Save a copy
to keep it. Select a supported box in Comments and choose Edit to change
its text and width/height in PDF points. Its author and PDF top-left corner
stay fixed. Unsupported styles and sizes that clip text or leave the page
are refused; the draft stays available to correct.

Spotify and Apple Music sessions add a music button with playback controls,
track information, artwork and a seek bar when the player supports it. Real
players, print-dialog input, physical printers and monitor moves still need
review. The Windows PDF driver passes the opt-in
output check. PDF layers are deferred because the pinned
PDFium API lacks visibility controls. See the [test record](docs/TEST_PLAN.md)
for current results, [reader checks](docs/READER_CHECKS.md) for the remaining
desktop pass and [current status](docs/PORTING_STATUS.md) for the work history.

Portable alpha packaging and the draft-release workflow are described in
[releasing Sable](docs/RELEASING.md). Public binary publication still needs
desktop/clean-machine sign-off and the dependency-license review recorded
in THIRD_PARTY_NOTICES.md.

| Keys | Action |
|---|---|
| Ctrl+F | Open Find |
| Enter, F3 or Ctrl+G | Next Find match; add Shift for the previous match |
| Ctrl+Alt+G | Select the page-number box |
| Ctrl++ / Ctrl+- / Ctrl+0 | Zoom in, out or reset; keypad aliases work too |
| R / Shift+R | Rotate the reading view clockwise / counterclockwise |
| H / S | Hand tool / text selection |
| F4 | Hide or reopen the last sidebar |
| Ctrl+S / Ctrl+P | Save a copy / print |
| Ctrl+Alt+P | Presentation mode; Escape leaves it |
| F11 | Toggle full screen |

Text fields retain their editing keys. Each tab keeps its Find query/options,
sidebar state, reading layout and cursor tool. The View menu offers Vertical,
Horizontal, Wrapped and Page scrolling, plus no/odd/even spreads.
Use two fingers on a precision trackpad to scroll, or pinch to zoom. The
reader sharpens the PDF after the gesture; Ctrl+mouse wheel works too.
In Page scrolling with odd/even spreads, enable View > Click pages to turn
spreads for left-page/back and right-page/forward navigation.

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

The build script uses the 64-bit compiler host for both targets. Release
keeps whole-program optimization enabled.

Run `uv run update_pdfium.py` without `--pinned` to move to the latest
[pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) release.

The pre-commit hook checks formatting. Turn it on with
`git config core.hooksPath .githooks`.

## Diagnostics

Logs are written under `%LOCALAPPDATA%\Sable\logs`, with one
`Sable-<process-id>.log` per process. Each file rotates at 2 MiB with three
backups. Old process logs are retained. Set `DEBUG` to a nonempty value for
debug-level entries; even `DEBUG=0` enables them, matching NightPDF.
An unavailable log folder does not stop the reader.

## Licenses

-   [Sable](LICENSE) is under [GPLv2 only](LICENSE)
-   [NightPDF](https://github.com/Lunarequest/NightPDF), which Sable is derived from, is under [GPLv2 only](https://github.com/Lunarequest/NightPDF/blob/mistress/LICENSE)
-   [Windows App SDK](https://github.com/microsoft/WindowsAppSDK) is under [MIT](https://github.com/microsoft/WindowsAppSDK/blob/main/LICENSE)
-   [PDFium](https://pdfium.googlesource.com/pdfium/) is under [BSD-3-Clause, with the Apache License 2.0 text included](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/LICENSE)
-   [Manrope](https://github.com/googlefonts/manrope), the UI font, is under the [SIL Open Font License 1.1](app/assets/fonts/OFL.txt)
-   Everything else is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
