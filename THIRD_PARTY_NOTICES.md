# Third-party notices

Sable is GPL-2.0-only and uses the components below. Pinned versions are in
[docs/DECISIONS.md](docs/DECISIONS.md).

| Component | Used for | License |
|---|---|---|
| [NightPDF](https://github.com/Lunarequest/NightPDF) | what Sable is ported from (icons and artwork included) | GPL-2.0-only |
| [PDFium](https://pdfium.googlesource.com/pdfium/), built by [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) | PDF engine (`pdfium.dll`) | BSD-3-Clause; the downloaded `licenses/pdfium.txt` also contains Apache-2.0 text (see below) |
| bblanchon/pdfium-binaries build scripts | where the binaries come from, not shipped | MIT |
| [Windows App SDK / WinUI 3](https://github.com/microsoft/WindowsAppSDK) | UI and bundled runtime | Source repository: MIT. Restored binary packages also supply Microsoft Software License Terms; the actual package texts are bundled. |
| [C++/WinRT](https://github.com/microsoft/cppwinrt) | WinRT projection | MIT |
| [WIL](https://github.com/microsoft/wil) | Windows helpers | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | settings file | MIT |
| [spdlog](https://github.com/gabime/spdlog) | logging | MIT |
| [fmt](https://github.com/fmtlib/fmt) | formatting used by spdlog | MIT |
| [GoogleTest](https://github.com/google/googletest) | tests only, not shipped | BSD-3-Clause |
| [Manrope](https://github.com/googlefonts/manrope) | UI font, bundled as `app/assets/fonts/Manrope-VF.ttf` with its `OFL.txt` | SIL Open Font License 1.1 |

The PDFium binaries bundle a bunch of other libraries too (FreeType, HarfBuzz,
ICU, lcms, libjpeg-turbo, OpenJPEG, libpng, zlib, abseil and a few more).
The download's root LICENSE covers bblanchon's packaging. PDFium's own text
and the component notices are under `third_party/pdfium/<arch>/licenses/`.
The portable package keeps both, along with the installed fmt, nlohmann/json
and spdlog copyright files, Manrope's OFL and license/notice texts from the
restored runtime and projection packages.

## PDFium and Apache-2.0

The downloaded `licenses/pdfium.txt` contains BSD-3-Clause and the full
Apache-2.0 text. The dependency-license question recorded in DECISIONS.md
remains open. The presence of license texts alone does not settle which
terms apply to the combined distribution. I have not changed Sable's
GPL-2.0-only license or treated NightPDF's former packaging as clearance.

## Windows runtime package terms

The restored Windows App SDK 2.5.1 and WinUI packages include Microsoft
Software License Terms, with sections on distributable code, distribution
requirements and restrictions. These are separate from the source
repository's MIT file. The package includes the supplied texts unchanged
under `licenses/nuget/`; its subpackages and WebView2 have their own texts
there too. Public binary distribution still needs a review of these terms
alongside Sable's license. Packaging the notices does not mark that review
complete.
