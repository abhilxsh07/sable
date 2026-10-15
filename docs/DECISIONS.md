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

## Thumbnails (2026-10-14)

The rail opens a 200 DIP thumbnail panel. DisplayThumbs is read from
config.json when opening a document, with NightPDF's default of showing
it. F4 toggles it outside text fields, and clicking a thumbnail goes to
that page. Page labels appear in the tooltip and accessibility name.
Thumbnails stay unfiltered, as they did in NightPDF.

I only keep thumbnail controls near the viewport, reusing them as the
strip scrolls. Their rasters share the page cache and engine thread, with
the main pages rendered first. A display-scale change refreshes both.
Very tall thumbnail rasters use the same render limits as the main pages;
the strip keeps their proportions. Invalid dimensions are rejected before
converting to integers.

Thumbnail jumps wait until after layout and the initial page fit. An
explicit page jump refreshes the thumbnail position even if that page was
already current. Closing moves focus out of the page box before clearing
the panel, and an error hides the panel along with the main view.

The scale probe now checks thumbnail raster sizes and that page 500's
thumbnail stays in view at each injected scale. Real monitor moves still
need checking.

## Passwords (2026-10-14)

An encrypted PDF asks for its password the way pdf.js's prompt did: the
same two lines of text, a masked box, Cancel and OK. OK does nothing while
the box is empty, Enter means OK and Esc means Cancel. A wrong password asks
again with "Invalid password. Please try again." Each try opens the file in
a new session, and the password only goes to that open; nothing keeps it.

After Cancel, pdf.js logged an error and left the viewer blank. Sable says
"This PDF is protected with a password." in the viewer instead, like its
other open errors. Closing the window while the prompt is up just closes it.

## Viewer errors (2026-10-15)

I show the existing open-error messages in an error InfoBar. It stays open
because dismissing it would leave an empty viewer with no explanation.
The page view, rail and thumbnails are hidden while it is up. Passwords
still go through their prompt; cancelling it uses the same error display.
If creating or queuing an open throws, that failure is shown here too.
Closing clears the bar, and late results leave the closed viewer alone.

The debugger gets the displayed reason through OutputDebugStringW. File
logging is still part of P20.

The fixture generator also writes an empty file, plain text with a PDF
extension and an empty page tree. The Debug viewer error probe opens these
and the damaged/missing files, injects the unsupported-protection and
unknown results, checks cancelled/late results and password cancellation,
then opens a normal document.

Build with `SableSurfaceTest=true`, then launch from the repo root with
`SableErrorTest=true`. Read the window's PASS or FAIL result before closing;
exit 0 only checks shutdown. Clear both variables and rebuild afterwards.

## Files while open (2026-10-15)

The loader reads a PDF into memory before handing it to PDFium. Its file
stream is closed before the session's open callback, so the source can be
renamed, overwritten or deleted while the document stays open. The open
session keeps its original bytes; opening the replacement gets its new
contents. Nothing needed changing in the loader for V09.

Session tests now cover Unicode folders and filenames with spaces,
including a supplementary-plane character, and rendering after renaming,
replacing and deleting the source. Existing tests cover CLI UTF-8 parsing
and the document loader's wide paths.

I also opened the viewer with a quoted absolute path containing accented
letters, Japanese, an emoji, spaces, a leading filename dash, #, % and &.
It opened at page 500, and Windows allowed its isolated source file to be
renamed, overwritten and deleted while the viewer stayed open. The window
closed with exit 0. The later UI navigation check was inconclusive; fresh
renders after deletion are checked by the Session tests.

## First document tabs (2026-10-15)

Each document now gets its own ViewerPage, kept mounted while another tab
is selected so its page, zoom and thumbnail state stay around. Every file
on the command line opens a tab; the last file is selected. A repeated
absolute, lexically normalized path selects the existing tab, as
path.resolve did in NightPDF. It doesn't resolve filesystem links.

The title-bar TabView shows filenames with a leading ellipsis and the full
path in a tooltip. Its add button stays hidden until the open dialog is
wired. The native strip handles selection and close buttons. I handle
Ctrl+W/F4 and next/previous (Ctrl+Tab/PageDown, Ctrl+Shift+Tab/PageUp) on the
window so navigation stops at the ends instead of TabView's usual wrap.
Reopen, tab moves, position shortcuts and drag reorder are still to come.
Tab widths still use WinUI's defaults; matching the 15% limit is left for
the tab-strip pass.

A tab opens its session once loaded, including background tabs. Password
prompts from a background tab wait until selection, and an existing dialog isn't
shown twice. Filters apply to every tab, including one that hasn't opened
yet. Zoom actions use only the selected viewer. Closing the last tab
returns to the home screen and hides the tab strip and presets. Closing the
window releases every viewer before the PDF engine.

The Debug-only SableTabsTest probe exercises duplicate paths, independent
page/zoom, shared filters, an error tab, a deferred password prompt, closing
while opening and returning home. It reports PASS or a failing step in the
window. Build with SableSurfaceTest=true, set `$env:SableTabsTest = 'true'`,
then launch from the repo root. Read the result before closing; exit 0 only
confirms shutdown. Clear both variables and rebuild afterwards.

## Opening PDFs from the shell (2026-10-15)

The File menu, tab strip's +, Ctrl+O, Ctrl+T and home Open controls use the
same multi-select picker, filtered to .pdf, like openNewPDF in app.ts.
Clicking the splash card opens it too. I use the Windows App SDK picker
with this window's AppWindow ID; it returns paths to the existing tab
opening code, so duplicates still select their tab. No extra dependency.

Only one picker runs per window. Cancelling leaves the tabs as they were;
results arriving after the window closes are ignored. A picker failure
gets an InfoBar instead of escaping the event handler. Opened PDFs keep
their own file-error banners. Runtime keybind customization comes later.

## Reopening and moving tabs (2026-10-15)

Ctrl+Shift+T pops the last closed path from this window's history, including
when the home screen is showing. Like NightPDF, it opens a fresh document
at the default page and zoom; I don't keep the old viewer or password.
An already-open path selects its tab and still consumes the history entry.
Closing the window doesn't add its documents to the history.

Ctrl+1 through Ctrl+8 select that position, Ctrl+9 selects the last tab.
Ctrl+Shift+PageUp/PageDown move the current tab left/right, and
Ctrl+Shift+Home/End move it to the first/last position. Missing positions
and moves past either end do nothing. These use the visible strip's order,
not the order documents were opened. Moving a header keeps the same viewer
selected and skips intermediate selection events, so another tab's password
prompt can't open during the move. Drag reordering is still to come.

The SableTabsTest probe also checks fresh reopening from home, LIFO history,
already-open paths, empty history, moves in both directions and invalid
positions. The real keyboard checks cover the key mapping separately.

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
- Cancelling the password prompt shows why the viewer is empty instead of
  leaving it blank.
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
