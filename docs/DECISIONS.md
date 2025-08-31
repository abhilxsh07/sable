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
Everything else (nlohmann/json, spdlog, GoogleTest) comes from vcpkg in
manifest mode, with a pinned baseline. No CLI11 in the end: matching yargs'
help output exactly was easier with a small hand-written parser.

## License header (2026-10-06)

Every file keeps Advaith Madhukar's 2021 copyright line from NightPDF and adds
mine below it. First line of the header: "Sable, a native port of NightPDF".

## Font: Manrope (2026-10-07)

NightPDF used Arial everywhere, and the WinUI default would be Segoe UI. I
wanted something with a bit more character that still reads well as small
light text on a dark background, so I went with Manrope. It's a variable
font (wght 200-800), OFL licensed, and ships inside the app with its licence
next to it. Taken from googlefonts/manrope at
`468c0dbe38efa331b80bfe9448256abe27be44c3` (v4.505).

## More than a port (2026-10-07)

Going through NightPDF's screens side by side with Acrobat Reader made it
obvious how much a PDF reader can do that NightPDF only had because PDF.js
came with it, or didn't have at all. So the goal moved: parity first is still
how I'm building it, but the end result should be a reader with Acrobat
Reader's free tools (comments, Fill & Sign, the works) plus organize pages,
combine files and export, all offline. Details in [DESIGN.md](DESIGN.md).

- The preset buttons stay in the top bar. They're the reason this app exists.
- One Menu button instead of the classic menu bar. Same items, same shortcuts.
- Layout ideas from Acrobat, but the icons are Windows' own Fluent icons.

## Drawing surface (2026-10-11)

I tried one VirtualSurfaceImageSource inside a ScrollViewer before wiring up
the viewer. The check repeats one fixture raster over 1,000 page positions,
640 by 880,000 pixels in total. On the hardware path at 100% display scale,
the first and last positions draw, scrolling keeps the page gaps intact,
and all four filter states redraw with the upload counter still at one.

The surface handles XAML's update rectangles in pixels. The drawing origin
accounts for the atlas offset returned by BeginDraw, and each update is
clipped. Its callback holds a weak reference: I can drop the wrapper, leave
the Image holding its source, scroll into an undrawn area and close safely.

That is enough to keep the single-surface approach for the next viewer step.
It doesn't establish a real 1,000-page PDF's memory use. The page cache,
zoom/DPI rerasterization and device-loss recovery still need implementing.

To repeat the check, run these from the repo root in PowerShell:

```powershell
$env:SableSurfaceTest = 'true'
.\scripts\build.ps1 -Config Debug -Platform x64
.\x64\Debug\Sable\Sable.exe
```

Close the test window, run `Remove-Item Env:SableSurfaceTest`, then build
again to return to the home screen. The check only exists in Debug builds;
a Release build stops with an error while the variable is set.

## Surface size limit (2026-10-12)

Microsoft's page for the [VirtualSurfaceImageSource constructor](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.media.imaging.virtualsurfaceimagesource.-ctor)
says Windows checks that the width and height are under 17,000,000 pixels,
and that a ScrollViewer "might further reduce the effective size", with no
number given. DirectComposition's [CreateVirtualSurface](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice2-createvirtualsurface)
caps each side at 16,777,216 (2^24), so I'm treating that as the limit.

The case I care about is 1,000 A4 pages at 400% zoom on a 150% display. An
A4 page is 841.89 points tall: 4,490 DIPs at 400%, 6,735 pixels at 150%.
With 10 DIP gaps and margins the stack is 6,750,133 by 4,792 pixels, so it
fits with about 2.5 times to spare.

I checked it with the surface test window, changing its step locally (100%
display scale, hardware path):

- 6,750,000 pixels: jumps to the last position and draws it in the right
  place, text sharp.
- 16,700,000 pixels: the same.
- 17,100,000 pixels: the surface can't be created, XAML throws and the app
  closes (0xc000027b).

So one surface is enough for the viewer and the per-page fallback can wait.
The limit is still reachable at 400%: the A4 stack passes 2^24 pixels after
about 2,485 pages at 150% and 1,864 at 200%, sooner with bigger pages. The
viewer has to check the height before creating or resizing the surface
instead of letting it throw. I haven't tried a 150% or 200% display yet.
XAML's layout sizes are floats, which only resolve half a DIP at 4.5
million DIPs.

## First viewer slice (2026-10-12)

A PDF passed on the command line now opens in a scrolling ViewerPage at
100%, with the default night filter. `-p` chooses the starting page after
XAML has measured the scroll extent. Applying it earlier silently sent me
back to page 1 on a cold start.

I keep unfiltered page rasters in a 256 MiB cache and GPU copies only near
the viewport. One render runs at a time, with visible pages ahead of the
neighbours. Closing releases the session and drawing resources, and late
callbacks cannot reopen them. A stack over the surface limit shows an
error for now.

The generated `tests/fixtures/long.pdf` has 1,000 numbered pages, alternating
portrait, landscape and rotated sizes. I checked starting at pages 500 and
1,000, rapid scrolling, resizing, and closing. The three-page fixture also
draws in the right order, and a password-protected file shows its error.
Debug and Release x64 and ARM64 build without warnings; the 275 tests pass
in both x64 configurations.

This still opens only the first command-line file. Tabs, full CLI dispatch,
password entry, zoom and display-scale changes are later steps. Preset
controls aren't connected yet; the home screen still opens without a file.

## Display-scale changes (2026-10-12)

The viewer now watches XamlRoot.Changed and reads RasterizationScale after
layout. A change resizes the surface in pixels while keeping the document
layout, zoom and scroll offsets in DIPs. I keep the old page images up until
the new renders are ready, with the same 400 ms settling delay as zoom.
Renders from the old scale are cancelled or rejected when they arrive.
Closing revokes the listener and queued work checks whether the viewer has
already closed.

The surface dimensions are checked before converting to integers, including
non-finite values and overflow. If a larger scale would exceed the single
surface limit, I keep the last working resolution. It can look softer on
that monitor; the larger-document decision below is still open.

The Debug scale probe uses the real viewer and long.pdf at page 500. It
checks 150%, 200%, 125% and 100%, matching GPU raster dimensions after each
change and keeping the page, zoom and offsets. It also changes scale during
a render, rejects invalid scales and can close with callbacks pending.
It injects scale values, so it doesn't prove Windows delivers a real
monitor-change event. That hardware check is still outstanding.

To run it, build with `SableSurfaceTest=true` as above, then set
`$env:SableScaleTest = 'true'` before launching the Debug app from the repo
root. The window reports PASS or FAIL. Close it and clear both variables
with `Remove-Item Env:SableScaleTest, Env:SableSurfaceTest`, then rebuild.
The ordinary surface/device-loss probe stays available when SableScaleTest
isn't set.

## Still undecided

- Whether to import settings from an existing NightPDF install.
- File associations other than .pdf.
- Packaging. Probably Velopack, but that can wait until there's something to
  ship.
- PDFium's license file includes the Apache-2.0 text, see
  THIRD_PARTY_NOTICES.md.
- What the viewer does when a document would be taller than one surface
  allows: cap the zoom, or switch to one surface per page.

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

- The UI font is Manrope, not Arial (see above).
- The classic File / Edit / View / Window / Help menu bar becomes a single
  Menu button. Help points at Sable's repo instead of NightPDF's, and there's
  no Contact item for now.
- The window opens maximized. Restoring it still gives NightPDF's 550x420
  (565 wide really, because of the minimum width).
- The title bar is drawn by the app (WinUI's TitleBar control) so the Menu
  button, and later the tabs and presets, can live in it.
- With nothing open you get a home screen instead of the bare splash: a pane
  with Recent / Starred and the tools, a tools strip, the splash itself as a
  drop card, a preview of the four presets on a sample page, and the recent
  files list. The splash keeps its gradient border and both lines of text.
- Recent files live in `%APPDATA%\Sable\recent.json`, not in config.json, so
  the settings file stays the shape NightPDF wrote. Up to 50 files; starred
  ones are kept first and survive Clear recent.
- Lots of tools NightPDF never had (see DESIGN.md).
- Unknown command-line options are ignored like yargs did, but they don't
  take the next argument as their value. In NightPDF `--foo file.pdf` meant
  the file never opened.
- Configs written by NightPDF get their Ctrl shortcuts back on load (NightPDF
  never actually saved the Ctrl part).
- `pull.yaml` also runs on pushes to main and by hand, not only on pull
  requests. It builds x64 and ARM64 on windows-2022 (windows-2025 only has VS 2026) and
  runs the tests on x64.
- `update_pdfium.py --pinned`: upstream committed pdf.js straight into the
  repo, so `update_pdfjs.py` only ever had to update it. I'm not committing
  the PDFium binaries, so a fresh clone needs a way to download the pinned
  version.
- I generate the test PDFs with `tests/fixtures/make_fixtures.py` instead of
  copying PDF.js's sample, so there's no borrowed fixture licence to sort out.
