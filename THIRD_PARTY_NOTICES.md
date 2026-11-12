# Third-party notices

Sable is GPL-2.0-only and uses the components below. Pinned versions are in
[docs/DECISIONS.md](docs/DECISIONS.md).

| Component | Used for | License |
|---|---|---|
| [NightPDF](https://github.com/Lunarequest/NightPDF) | what Sable is ported from (artwork included; the app icon is Sable's own) | GPL-2.0-only |
| [PDFium](https://pdfium.googlesource.com/pdfium/), built by [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) | PDF engine (`pdfium.dll`) | BSD-3-Clause; the downloaded `licenses/pdfium.txt` also contains Apache-2.0 text (see below) |
| bblanchon/pdfium-binaries build scripts | where the binaries come from, not shipped | MIT |
| [Windows App SDK / WinUI 3](https://github.com/microsoft/WindowsAppSDK) | UI; the runtime is installed separately from Microsoft, only its bootstrapper ships | Source repository: MIT. Restored binary packages also supply Microsoft Software License Terms; the actual package texts are bundled. |
| [C++/WinRT](https://github.com/microsoft/cppwinrt) | WinRT projection | MIT |
| [WIL](https://github.com/microsoft/wil) | Windows helpers | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | settings file | MIT |
| [spdlog](https://github.com/gabime/spdlog) | logging | MIT |
| [fmt](https://github.com/fmtlib/fmt) | formatting used by spdlog | MIT |
| [GoogleTest](https://github.com/google/googletest) | tests only, not shipped | BSD-3-Clause |
| [Manrope](https://github.com/googlefonts/manrope) | UI font, bundled as `app/assets/fonts/Manrope-VF.ttf` with its `OFL.txt` | SIL Open Font License 1.1 |

Some interface labels (page, zoom, password and find messages) match the en-US text of the [pdf.js viewer](https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/l10n/en-US/viewer.ftl) (Mozilla, Apache-2.0). This is a courtesy note; no licence is claimed for them.

The PDFium binaries bundle a bunch of other libraries too (FreeType, HarfBuzz,
ICU, lcms, libjpeg-turbo, OpenJPEG, libpng, zlib, abseil and a few more).
The download's root LICENSE covers bblanchon's packaging. PDFium's own text
and the component notices are under `third_party/pdfium/<arch>/licenses/`.
The portable package keeps both, along with the installed fmt, nlohmann/json
and spdlog copyright files, Manrope's OFL and license/notice texts from the
restored runtime and projection packages.

## What the portable package ships

Checked against a fresh framework-dependent Release x64 package on 12
November 2026 (60 files; the ARM64 package has the same names). Sable.exe
links the Visual C++ runtime statically and uses the Universal CRT that
comes with Windows, so no `vcruntime140`/`msvcp140` DLLs are shipped.
Microsoft's Windows App Runtime isn't in the package: people install it
from Microsoft, under Microsoft's terms, and Sable loads it through the
bootstrapper below.

| Files | Component | Terms in the package |
|---|---|---|
| `Sable.exe`, `Sable.pri`, `Sable.winmd`, `*.xbf`, `styles/`, `assets/` | Sable, with NightPDF's gradient artwork | `LICENSE` (GPL-2.0-only) |
| `assets/fonts/Manrope-VF.ttf` | Manrope | `assets/fonts/OFL.txt` |
| `pdfium.dll` | PDFium and the libraries built into it | `licenses/pdfium/*`, `licenses/pdfium-binaries/LICENSE` |
| `Microsoft.WindowsAppRuntime.Bootstrap.dll` | Windows App SDK Foundation (bootstrapper) | `licenses/nuget/Microsoft.WindowsAppSDK.Foundation.*` |
| `Microsoft.Web.WebView2.Core.winmd` (metadata only, no WebView2 DLLs) | WebView2 | `licenses/nuget/Microsoft.Web.WebView2.*` |
| compiled into Sable.exe | C++/WinRT, WIL, nlohmann/json, spdlog, fmt | `licenses/nuget/Microsoft.Windows.CppWinRT.*`, `licenses/nuget/Microsoft.Windows.ImplementationLibrary.*`, `licenses/{nlohmann-json,spdlog,fmt}/copyright` |

The six Windows App SDK `license.txt` files are the same text; they stay
in the package because the bootstrapper and the build use those packages.
The AI, ML, Search and Widgets parts and the WebView2 runtime were removed
on 11 November, and the self-contained runtime DLLs on 12 November.

## PDFium and Apache-2.0 (D10, closed)

The downloaded `licenses/pdfium.txt` contains BSD-3-Clause and the full
Apache-2.0 text. On 10 November I accepted PDFium's license file as it is,
listed here, the same way NightPDF shipped Apache-2.0 pdf.js. That closes
§15 D10 (see DECISIONS.md, "Review decisions"). The pdf.js-derived zoom
steps, scale and wheel counting were rewritten from a description of
their behavior, so pdf.js isn't listed as a shipped component. Sable's own
license stays GPL-2.0-only.

## Windows runtime package terms

The Windows App SDK packages ship Microsoft Software License Terms with
the binaries, separate from the source repository's MIT file. The package
keeps them unchanged under `licenses/nuget/`. The parts that matter for
handing out the ZIP:

- Section 3(a) allows redistributing every file the NuGet package places
  beside the app, for self-contained apps too. Everything in the table
  above comes from that.
- Section 3(b) requires the app to add significant primary functionality,
  requires distributors and end users to agree to terms that protect the
  code and Microsoft at least as much as the agreement, and asks for
  indemnity for claims about the app.
- Section 3(c) forbids using Microsoft's trademarks in a way that suggests
  endorsement, and forbids making the distributable code subject to a
  license that requires its source to be disclosed or lets others modify
  it.
- Section 2 says the runtime may send usage data to Microsoft.

Since 12 November the package ships only the bootstrapper DLL under these
terms; the runtime itself comes from Microsoft. Whether a GPL-2.0-only
program can ship that DLL beside it, and what notice Sable's users need,
is an owner decision that is still open. Packaging the texts doesn't
settle it.
