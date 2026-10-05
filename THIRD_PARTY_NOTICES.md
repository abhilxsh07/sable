# Third-party notices

Sable is GPL-2.0-only and uses the components below. Pinned versions are in
[docs/DECISIONS.md](docs/DECISIONS.md).

| Component | Used for | License |
|---|---|---|
| [NightPDF](https://github.com/Lunarequest/NightPDF) | what Sable is ported from (icons and artwork included) | GPL-2.0-only |
| [PDFium](https://pdfium.googlesource.com/pdfium/), built by [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) | PDF engine (`pdfium.dll`) | BSD-3-Clause, and its LICENSE file also has the Apache-2.0 text (see below) |
| bblanchon/pdfium-binaries build scripts | where the binaries come from, not shipped | MIT |
| [Windows App SDK / WinUI 3](https://github.com/microsoft/WindowsAppSDK) | UI | MIT |
| [C++/WinRT](https://github.com/microsoft/cppwinrt) | WinRT projection | MIT |
| [WIL](https://github.com/microsoft/wil) | Windows helpers | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | settings file | MIT |
| [CLI11](https://github.com/CLIUtils/CLI11) | command line parsing (might end up hand-written instead) | BSD-3-Clause |
| [spdlog](https://github.com/gabime/spdlog) | logging | MIT |
| [GoogleTest](https://github.com/google/googletest) | tests only, not shipped | BSD-3-Clause |

The PDFium binaries bundle a bunch of other libraries too (FreeType, HarfBuzz,
ICU, lcms, libjpeg-turbo, OpenJPEG, libpng, zlib, abseil and a few more).
Their licenses come with the download in `third_party/pdfium/<arch>/LICENSE`
and `third_party/pdfium/<arch>/licenses/`, and they have to ship with release
builds.

## PDFium and Apache-2.0

PDFium's LICENSE file has the BSD-3-Clause license and also the full
Apache-2.0 text, and the FSF considers Apache-2.0 incompatible with GPLv2.
NightPDF was in the same spot, with Apache-2.0 PDF.js shipped next to its
GPL-2.0-only code. I haven't worked out yet what, if anything, needs to change
here. Not a lawyer.
