# Decisions

Why things are the way they are, so I don't have to remember it all later.

## Text note editor and saves (2026-10-24)

Comments now has Add note and Save notes under the approved reader scope.
This first editor adds a Text annotation at the current page's center and
says where it will go. Author is optional. Placement by clicking a page,
moving notes and editing existing notes still follow.

The source stays loaded and untouched. Save notes writes an edited copy;
Save a Copy keeps its original-byte behavior. The UI tracks unsaved notes
separately from PDFium's persistent modified flag, and holds further edits
until a save finishes. Cancel and failure keep the unsaved state. Failed
creation retains the editor text for another attempt. A document revision
rejects old render replies before refreshing page and thumbnail caches,
comments and properties. The temporary edited-file limit is 1 GiB.


## Centered note requests (2026-10-24)

I keep page geometry and the preservation scan on the engine thread. The
editor entry point centers the 20-point icon in the media/crop intersection
and uses the existing bounds checks. It refuses known unsafe attachment
rewrites before changing the document. The raw-position API remains useful
for lower-level edits; serialization still applies the same restrictions.
Note replies distinguish changed requests from ordinary refusals, so failed
rollback cannot look clean and a refused add keeps the existing savepoint.


## Edited file saves (2026-10-24)

Document saveEdits serializes owned bytes, then uses the atomic file writer.
It refuses the source and aliases, keeps the loaded backing bytes and leaves
the modified flag set. The current tab still refers to its original file.
Unsupported serialization and failed writes leave an existing target alone.
Cancellation stays latched across serialization and writing.

Session saveEdits shares the save slot with Save a Copy, without involving
render, Find or annotation-read generations. Work and callbacks run on the
engine thread; no session lock stays held through PDFium or file I/O. Close
is checked before replacement; successful writes keep Saved after a later
close. Callbacks may queue another save or throw.
Creation UI, cache refresh and close/unsaved-state handling follow.

## Atomic file writes (2026-10-24)

I share the attachment writer's temporary-file and replacement path with
document saves. FileSaveResult distinguishes saved, canceled, failed and
source-alias results. The attachment wrapper keeps its 128 MiB limit.

Writes use 1 MiB chunks, flush and close before replacing the destination.
Cancellation and predicate exceptions discard the temporary output. I
recheck source aliases after flushing and never query cancellation after a
successful replacement. A failed CREATE_NEW never removes an unowned file.

## Edited document bytes (2026-10-24)

I serialize on the engine thread into an owned, bounded byte buffer.
Untouched documents return their exact loaded bytes, even after rendering.
Creating a note marks the document modified; rejected notes do not. A failed
setter restores the old flag only after a successful rollback. Serialization
does not clear that flag or replace the buffer PDFium originally opened.

Edited documents use FPDF_SaveAsCopy with no security-removal flags. The
writer catches callback exceptions and permanently records failed writes,
limits and cancellation, including the final flush. No partial output is
returned. Passwords and user permissions survive the round-trip checks.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/main/core/fpdfapi/edit/cpdf_creator.cpp

The attachment regression reproduces the known empty-stream rewrite: Sable
reads eight compressed bytes where it previously read an empty file.
Edited documents with document-level or page FileAttachment entries refuse
serialization for now, keeping their notes available in memory. The page
scan is cancelable and refuses unreadable pages/annotations or more than
4,096 entries on a page. Untouched copies remain exact. I do not fall back
to original bytes after an edit.
File writing, session save requests, close handling and creation UI follow.

## Queued text notes (2026-10-24)

The session owns queued note text and reports the requested page and new
annotation index from the engine thread. Open results expose annotation
permission separately from copying. Oversized fields refuse to queue;
document validation failures return no index without claiming cancellation.

Writes keep engine queue order and ignore annotation-read, Find and render
cancellation. Closing before a job starts cancels it. After the start check,
the write keeps its actual result even if close races; document destruction
waits behind it in the same engine queue. I do not hold the session lock
through PDFium work. Callbacks run outside locks and may queue more work.

Mutation exceptions are logged and return no index. A failed rollback may
leave an incomplete note, as documented by the engine operation. This adds
no creation UI, cache refresh, unsaved-state handling or serialization.

## Text note creation (2026-10-24)

I create Text annotations in memory on the engine thread. The operation
returns the new page annotation index; it does not write the source file.
Adding a note needs annotation permission, independently of copying text.
Owner access uses the permissions PDFium reports after opening the file.

The caller supplies the lower left in unrotated PDF coordinates. I use the
20-point icon PDFium generates, with Print set, and require the full icon
inside the visible media/crop intersection. Nonfinite or collapsed float
rectangles are rejected before creation.
// see: https://pdfium.googlesource.com/pdfium/+/63155a9b3873593a0f0d06bede4046ba7914b3ac/core/fpdfdoc/cpdf_generateap.cpp

Contents cannot be empty. Both text fields reject embedded NUL and unpaired
surrogates, and fit the snapshot reader's 64 KiB field limit including the
terminator. Pages with 4,096 annotations refuse another. Existing snapshot
text budgets still apply. I allocate strings before mutation and remove the
appended annotation if a setter fails; a failed rollback throws explicitly.
The asynchronous editing path, creation UI, unsaved-state handling and
serialization follow separately. Save a Copy still copies the loaded bytes.

## Comment locations (2026-10-23)

I keep an optional displayed-page position with each annotation snapshot.
I clip the rectangle to the media/crop intersection, then ask PDFium to
convert its center on the engine thread, including the page's intrinsic
rotation. Raw PDF bounds stay intact. Missing, empty or off-page rectangles
have no target.

For a Text note with no appearance dictionary I use PDFium's generated
20-point icon bounds for its target. This keeps a cached target stable if
rendering later creates the icon; the snapshot keeps the raw bounds it read.
Existing appearance dictionaries keep their supplied rectangles, including
empty or incomplete containers. I don't decode appearance streams to list
comments.
// see: https://pdfium.googlesource.com/pdfium/+/63155a9b3873593a0f0d06bede4046ba7914b3ac/core/fpdfdoc/cpdf_generateap.cpp

Comment items carry row indices: two notes on the same page can have
different targets. Clicking uses the existing destination path, with the
current view rotation applied once, and centers the target within scroll
limits. It keeps the zoom policy and accounts for a temporary native zoom
factor. Unusable positions still navigate to the page. Ordinary links keep
their existing alignment; this adds no editing or saving behavior.

## Read-only Comments sidebar (2026-10-22)

Comments opens from the right rail beside the other document panels. It
loads one page at a time only while visible, shows page/type/author/text,
and navigates to the row's page. Loading, empty, retry and limited results
stay distinct. Cached rows survive hiding and retries without duplicates.

I watch the viewer and rail visibility: tab changes and presentation pause
the scan, and showing the panel resumes its next unfinished page. Closing
unregisters those callbacks and clears the list. Replies check the weak
viewer, session, request version, requested page and visibility before use.
Editing, saving and precise annotation-position navigation come later.

## Comments list scan (2026-10-22)

The comments list walks the document one page at a time. I keep up to 2,000
rows and 4 MiB of their combined text. Reaching either bound stops the scan
and reports limited results; a limited page still allows later pages to load.
Retry keeps completed pages and resumes the failed page without duplicates.

Links, form widgets and popup containers stay out of this list. Common
comment types remain visible even without text; unknown types stay when
they have contents. Rows keep their page and PDF annotation index, subtype,
author and text. Display controls are replaced with spaces while Unicode
survives; CR and CRLF text breaks become LF. Editing and saving are separate
steps.

## Annotation session requests (2026-10-22)

The session can now request an annotation snapshot without waiting on the
UI thread. Each reply keeps its requested page, including failed and
canceled reads. An empty inventory is a successful read; a missing inventory
without cancellation means the page could not be read.

I give these requests their own generation. A newer request or explicit
cancel supersedes older reads without touching rendering or Find, and
another document's session has its own generation. Close and destruction
cancel queued reads and discard their data. Accepted callbacks run on the
engine thread once; work that already finished can still arrive after a
later cancel, so the future panel must guard its current request and tab.
Callbacks may queue another read; callback exceptions stay inside the engine.

## Existing annotation snapshots (2026-10-22)

I'm starting comments with a per-page inventory of the annotations already
in the PDF. The result owns its UTF-8 text, flags and rectangle; no PDFium
handle leaves the engine thread. Subtype names stay as PDF names, including
unknown ones. Links and widgets stay in this inventory too, so a later
comments panel can choose what to show without losing the page's indices.

Rectangles use unrotated PDF coordinates in points. Missing or wrongly
typed rectangles have no bounds. Crop, page rotation and view rotation
belong to the later navigation/UI step. Missing fields and values other than
strings or names come back empty, matching the pinned public API.

I cap each page at 4,096 records, each field at 64 KiB of UTF-16 and the
page's combined field reads at 1 MiB. An incomplete inventory sets limited;
failure or cancellation returns no inventory. Cancellation releases the
page and current annotation before returning. This step only reads existing
annotations; editing, the comments panel and saving changes come later.

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
failure, exception or closing before completion exits non-zero. Clear both
variables and rebuild afterwards.

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
then launch from the repo root. Read the result before closing; the process
exits non-zero on a failure, exception or early close. Clear both variables
and rebuild afterwards.

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
prompt can't open during the move. Drag reordering followed in P08 below.

The SableTabsTest probe also checks fresh reopening from home, LIFO history,
already-open paths, empty history, moves in both directions and invalid
positions. The real keyboard checks cover the key mapping separately.

## Configured window shortcuts (2026-10-15)

I load the keybind store once per window and dispatch its action/data from
PreviewKeyDown, ahead of document keys. Changes take effect after restart
(Q7). Cleared bindings stay cleared. Ctrl+1-9 and the menu's Ctrl+O/P and
Alt+S stay fixed; Print and Settings still wait for their UI. The settings
window isn't needed to read an existing config.json. Windows resolves
character keys using the active layout when the window is created.
For a focus check, build with SableSurfaceTest=true and run with both
SableTabsTest=true and SableShortcutTest=true. The two focus buttons and
status use WinUI FocusManager, so the page box cannot mask the document
focus in an accessibility snapshot. Clear all three variables afterwards.

## Probe exit codes (2026-10-15)

The automated tabs, viewer-error and scale checks now fail the process on a
failed check or exception. Closing before completion also fails. Their
completion state is pure C++ and checked by GoogleTest; XAML, dialog, bitmap
and focus checks stay in the probes. Error-message and surface-size maths
already have GoogleTests, so those are not dependent on a UI run.

Set SableProbeAutoClose=true for an unattended success exit. For each probe,
SableProbeFault=check exercises its failed check and =exception exercises
its exception path. Both must exit non-zero. Clear these variables after
use. The interactive shortcut-focus mode has no automatic pass result.

For local verification, a temporary user property sheet can put IntDir
outside a synced folder. Google Drive held PDBs through upload-queue hard
links even after the compiler helper stopped. The tracked solution and CI
keep their existing paths, and the same build/test scripts still run.

## Music pane (2026-10-15)

Neither NightPDF nor Acrobat has this; it's an extra. A button in the title
bar opens a glass pane for Spotify and Apple Music: previous, play/pause,
next, seek, and shuffle and repeat where the app allows them. It goes in the
build order right after the shell, since it lives in the title bar. Details
in [DESIGN.md](DESIGN.md).

- It uses Windows.Media.Control
  (GlobalSystemMediaTransportControlsSessionManager), which has been part of
  Windows since 1809, so there's no new dependency and nothing new to
  license.
- It only follows Spotify's and Apple Music's sessions, picked by app ID.
  Anything else that plays is ignored.
- Seeking is up to the app. When an app ignores it, the seek bar is
  read-only. I'm not reaching for Spotify's Web API to get around that: it
  needs a Spotify login and a network connection, and Sable doesn't do
  accounts.
- The tint is worked out in helpers/ so it can be tested: the main colour of
  a small copy of the art, clamped in lightness so white text stays
  readable.
- No new setting. The button only shows while one of the two apps has a
  session, so there's nothing to switch off.

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

- Tab headers keep U+202A/U+202C around the filename and right-to-left flow
  to put the ellipsis at the start. This departs from Q11's no-control-marks
  rule: WinUI's [TextTrimming enum](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.texttrimming?view=windows-app-sdk-2.0)
  has no start-ellipsis option. I use the proper closing U+202C rather than
  NightPDF's second U+202A. Only the display text has these marks; paths,
  tooltips and accessible names keep the plain filename/path. The header's
  former 200 px MaxWidth was a temporary stand-in; the whole tab now has a
  15% window-width cap (P08).
- The UI font is Manrope, not Arial (see above).
- The classic File / Edit / View / Window / Help menu bar becomes a single
  Menu button. Help points at Sable's repo instead of NightPDF's, and there's
  no Contact item for now.
- With `general.MaximizeOnOpen=true`, new windows open maximized, including
  the home screen. NightPDF only maximized when opening a file. I keep the
  existing home-screen choice, but false now leaves new windows restored.
  Opening a file later doesn't change the window state; NightPDF's picker
  could maximize an existing window. This is a startup preference here, so
  opening another tab doesn't undo a manual restore. Startup sizing and tab
  bindings share one settings snapshot; edits apply after a restart (Q7).
  Restoring still gives NightPDF's 550x420 (565 wide really, because of the
  minimum width).
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

## Asynchronous picker and password errors (2026-10-15)

I catch WinRT, standard and unknown exceptions around the whole picker and
password coroutine, including setup and completion. Each catch logs first,
then tries to show its banner without letting a reporting error escape.
Completion still uses a weak reference, so a closed window isn't revived.
The password box is cleared before trying the supplied password again.

A failed password dialog now reports its actual failure instead of looking
like Cancel. Cancelling normally still shows the protected-document message.

## Dragging and tab width (2026-10-15)

I use TabView's native reordering within a window. The visible strip is the
order used by position and move shortcuts; the document viewers stay mounted.
Selection changes during a drag are held until completion, including a
cancelled drag, so a passing password tab doesn't activate its prompt.

The whole tab, including the close button, is capped at 15% of the window's
logical width and updates on resize. Equal sizing can make tabs smaller when
the strip is crowded. The old 200 px limit on the title text is gone.
TabView's built-in Ctrl+F4/Tab accelerators are disabled: settings own those
shortcuts with focus in the header too, including cleared bindings.

The TitleBar treated the filename TextBlocks as window-drag regions, so the
first real mouse drag moved the window. I explicitly make the tab strip
interactive with TitleBar.IsDragRegion=False and refresh regions when its
size changes. The unused title bar space still moves the window.

The tab probe checks laid-out widths at 900, 1200 and 565 physical pixels,
plus reorder completion and cancellation with the same mounted viewer.
Its interactive shortcut mode has a button to clear only that window's
bindings and focus the header. Ctrl+F4/Tab/Shift+Tab must then leave it alone;
fixed Ctrl+1-9 still works. This doesn't write the settings file.

The interactive tab probe also counts native drag-start/completion events and
shows the native drop result. This distinguishes an accepted reorder from a
cancelled or rejected drag; seeing a drag finish alone isn't confirmation.

## Files dropped on the splash (2026-10-15)

The home splash card accepts storage-item drops while no document is open,
then sends every local path through the same tab opener as the picker and
command line. Like index.ts/openFile, it tries the dropped files without an
extension filter, so a non-PDF gets the existing viewer error tab. Duplicate
paths still select the existing tab. Text and other data formats are ignored.

The source supplies its file list asynchronously. I hold a weak window
reference and check that it is still open and on the home screen before
opening anything. A source error uses the open-error banner; the whole
coroutine catches WinRT, standard and unknown exceptions. Virtual items with
no local path get an explanatory banner, since this reader opens local files.

The tab probe supplies a real storage-item data package with a duplicate PDF
and a non-PDF, checks the mounted viewers, ignores text/late results and checks
closure during retrieval. This covers data handling separately from physical
Explorer-to-window dragging.

## Command-line output (2026-10-15)

Help and version requests now print and exit before creating a window or
starting PDFium. Help wins when both switches are present, as in yargs;
file/page arguments don't open documents in either output mode. The help
uses Sable as the script name and the current local year in its epilog.
The version text comes from the existing versionString helper.

I preserve inherited pipe/file stdout handles. If no usable handle exists,
I attach to the parent console as described in guide 6.9. Console output uses
WriteConsoleW; redirected output is UTF-8 with LF and one trailing newline.
A failed write exits with code 1 instead of opening a window or reporting
success. Normal output requests shut down through Application.Exit.

## Reader window lifetime (2026-10-15)

App keeps each reader window until its Closed event and removes only that
window (Q5). The common createWindow path gives each window its own tabs,
closed-tab history, preset and sliders. Windows in the same process share
the single PDFium engine, which stays alive until all windows are released.
Failed setup closes the partial window before passing the exception back.

Each launch still starts a separate process and window (Q14). Ctrl+T still
opens the multi-select picker in the current window; it isn't a new-window
shortcut. The internal factory replaces upstream's createWindow/newWindow
path without adding a menu item or tab tear-out. WinUI handles shutdown when
the last XAML window closes, rather than calling Exit when any reader closes.

The tab check uses the same factory as normal startup. It opens the same PDF
at different pages in two windows, changes one window's preset and slider,
closes that window with an open pending, then zooms and navigates in the
survivor. It also opens a fresh home window after closing another reader.

## Shared settings store (2026-10-15)

Settings windows share one SettingsStore per app. General edits save the
whole validated config through the existing atomic writer, keeping keybinds
and the other general value. A failed save leaves the store unchanged so the
checkbox can return to its saved value rather than claiming an unsaved edit
worked. Reader windows keep their own startup/shortcut snapshots (Q7).

## Settings window frame (2026-10-16)

Settings opens from Menu, the home footer and fixed Alt+S. Each reader owns
one settings window; opening it again activates that window. Its native
owner is set before IsModal, following [Microsoft's window guidance](https://learn.microsoft.com/en-us/windows/apps/develop/ui/manage-app-windows).
The native check found the owner still enabled, so I also disable it while
Settings is open and restore its previous enabled state on close (guide 6.8).
Closing the reader closes its settings child before releasing the readers.
The version panel and lowercase menu follow render/settings.ts and the
reference screenshot. Keybinds stays disabled until its controls are added.

## General settings controls (2026-10-16)

The two checkboxes keep NightPDF's names and save immediately through the
shared store. Reopening Settings shows the saved values. A failed write
restores the checkbox and opens an error banner; NightPDF didn't explain a
failed save here. This avoids showing a value that won't survive a restart.
Changes don't resize an existing reader or change its shortcut snapshot.

## Saving keybind edits (2026-10-16)

The shared store validates a keybind command before saving the whole config.
Edits and clears use the same atomic writer as General. I only replace the
in-memory settings after the write succeeds, so a failed save keeps the old
bindings. Readers keep their startup snapshots; edits apply after restart.

## Keybind settings controls (2026-10-16)

Each action has NightPDF's primary and secondary slots and hover clear
buttons, using its #991900 red. The overlay keeps a completed chord after
release. Escape, Cancel, an outside click or losing activation cancels it.
I disable the rows while capturing and restore focus when the overlay closes.
Unmodified Tab, Enter and Space still operate the overlay buttons.

Save requires one normal key and at least one modifier. NightPDF checked the
display text instead and could save an invalid or modifier-only chord. A
failed write keeps the old binding and leaves the captured chord for retry.
Printable keypad keys keep the browser's character names in JSON. I install
equivalent keypad shortcuts too, so a saved keypad chord works after restart;
the fixed top-row number shortcuts keep their existing behavior.

The debug tabs check also accepts SableSettingsTest=true to run its settings
checks without opening documents or making a WinRT storage-item drop.

## Configurator thumb tooltips (2026-10-16)

The preset buttons and live sliders were already wired. I enabled the native
thumb tooltips and send their values through the existing slider formatter.
Percentages and the earlier hue degree-unit fix now apply to the thumb too.

SableFilterTest=true with SableTabsTest=true runs the preset/configurator
checks without making a WinRT storage-item drop. It checks retained edits,
slider limits/tooltips, shared tabs, new-tab filters and independent windows.

## PDF links (2026-10-16)

A page tap sends displayed-page fractions to the PDFium engine thread. Its
page/device transforms account for crop boxes and page rotation in both hit
testing and destinations. Only plain link data returns to the UI; no PDFium
handles cross threads. Closing a session cancels queued lookups, and the UI
also ignores results for closed or hidden viewers.

Internal links resolve direct and named destinations, including GoTo actions.
XYZ, Fit, FitH, FitV and FitR use the viewer's existing zoom and scroll paths.
FitB/BH/BV use the corresponding page fit because the viewer lays out whole
pages. URI actions accept http/https and use Windows' default browser launcher;
JavaScript, Launch and remote-file actions are ignored. The action type is
checked before trusting a destination: PDFium can return one for GoToR too.

The generated links.pdf fixture has cropped and rotated pages, internal and
external targets, null XYZ values and unsupported actions. SableLinkTest=true
with SableTabsTest=true runs the native check without WinRT drop-file access.
That build intercepts browser launches so the check cannot open web pages.

## Unfiltered printing (2026-10-16)

Menu > File > Print and the fixed Ctrl+P shortcut print the active tab. The
classic PrintDlgExW dialog runs on the UI thread with the reader as owner;
PDFium rendering and GDI spooling run on the engine thread. This keeps the
native modal dialog on its owning UI thread and every PDFium call serialized.
The selected document and current page are captured before the dialog opens.

All pages, current page and up to 32 native page ranges are supported. Ranges
use physical 1-based page numbers and overlapping ranges print each page once.
The returned printer DC handles copies and collation through
[PD_USEDEVMODECOPIESANDCOLLATE](https://learn.microsoft.com/en-us/windows/win32/api/commdlg/ns-commdlg-printdlgexw).
Each page fits and centers inside the printable area, preserving physical
aspect even when horizontal and vertical printer DPI differ. Paper size and
orientation come from the printer dialog; mixed-size pages fit that paper.

FPDF_RenderPage uses FPDF_ANNOT | FPDF_PRINTING directly on the printer DC.
Neither the GPU filter nor the screen cache enters this path. The print.pdf
fixture verifies original red/green colors and excludes a screen-only blue
annotation; another check covers page rotation. Render, StartPage, EndPage
and EndDoc failures abort the job. Closing the document cancels queued jobs
and aborts an active job between pages; it cannot interrupt a blocking driver
call. A long print job occupies the shared engine until it finishes or aborts.

SablePrintTest=true with SableTabsTest=true checks menu/accelerator wiring,
active-document snapshots, cancel/reentry, error recovery and home state.
It substitutes dialog results and never submits a printer job. Windows denies
printer discovery through CIM in this sandbox, so actual dialog interaction,
copies/collation, paper output and virtual PDF-printer output remain unverified.

## File diagnostics and notification shortcut (2026-10-17)

The reader uses spdlog's thread-safe rotating file sink under
%LOCALAPPDATA%\Sable\logs. Files are separate per process so independent
reader launches cannot race each other's rotation. Each process keeps a
2 MiB file and three backups; old process logs are retained. Nonempty DEBUG
enables debug entries, including the value "0", matching Node's environment
truthiness. Debug builds also use the debugger sink. Writes flush immediately
and failures remain nonfatal. Startup, reader lifetime, session failures,
settings saves, viewer notices and uncaught engine/XAML exceptions are logged.

Only logging.cpp includes spdlog, in header-only mode with std::format and
wide filenames. This supports Unicode log paths without mixing the installed
spdlog binary's narrow-filename configuration into the app. CLI help/version
return before log or notification initialization.

Nonempty EASTER_EGG adds Shift+Alt+T to each reader. Repeat keydown is ignored,
and the original title/body, including the flag's exact Unicode code points,
are retained. The Windows App SDK service is shared by the application. It
registers before activation arguments are read, so notification activation
opens or focuses a reader rather than treating activation arguments as files.
Live activation returns through the UI dispatcher; shutdown unregisters the
handler. Registration/display failures are logged without terminating the app.
Native checks substitute the send callback and verify the real SDK payload;
actual Windows notification delivery and click activation remain unverified.

Printing and browser-launch failures now use a closable notice. The previous
fatal viewer error path hid the pages and rail. Fatal open/render failures
retain that behavior; native checks verify that recoverable failures leave
the PDF visible, and the last-tab state restores home with Print disabled.

## Focused Edit menu commands (2026-10-17)

Undo, Redo, Cut, Copy and Paste use the focused WinUI TextBox's native
commands. Opening the menu captures a weak target; commands reject disabled,
detached, hidden or closed fields. Selection, read-only state, undo/redo
history and clipboard text availability determine which commands are enabled.
Native Ctrl+Z/Y/X/C/V keep priority over configured tab shortcuts in text fields.
PDF text selection remains a later reader feature.

The menu button preserves pointer focus, and the page-number field defers its
normal focus-loss commit while the menu is open. Position updates preserve
the draft, and returning focus preserves the selection/caret. Choosing another
menu command commits the draft before that command reads the current page.
Closed-menu cleanup is deferred until item Click has dispatched, with a
generation guard so an older close cannot clear a newly opened menu target.

SableEditTest=true with SableTabsTest=true checks draft/caret preservation,
native copy/cut dispatch, undo/redo availability, read-only/non-text targets,
other menu actions and a tab closed while its menu is open. Clipboard events
are intercepted so this probe does not replace the user's clipboard.

## Music pane (2026-10-17)

The title-bar music button follows Windows' GlobalSystemMediaTransportControls
sessions. I accept Spotify and Apple Music's known desktop/store identifiers;
other players, browser sessions and inactive sessions stay out of the pane.
Windows' current supported session wins. Otherwise a playing session wins,
with the previous target breaking ties so background events do not make the
button switch players repeatedly. Each reader owns its subscriptions and
removes them when closed. Metadata and artwork requests keep weak owners and
generation guards, so an old result cannot restore a closed or changed track.

The flyout shows title, artist, player, artwork, previous/play-pause/next,
shuffle and repeat. Playback capability flags gate every command; unknown
shuffle/repeat state remains disabled. Timelines extrapolate from Windows'
last-updated UTC timestamp and the reported playback rate, then use a steady
clock between updates. Seeking is clamped to the player's reported seek range.
The slider remains readable when seeking is unsupported, and seeks are
debounced while adjusted. Rejected commands show an inline message.

Artwork is decoded to a 16 by 16 BGRA sample for its average tint. Transparent
pixels do not contribute, and tint channels are capped at 80 to keep white
text readable. The in-app acrylic brush fades to the new tint over 300 ms;
missing or invalid art uses the existing grey. Windows controls acrylic's
solid fallback. Encoded artwork is capped at 8 MiB and 16 megapixels decoded.
The popup uses WinUI's normal light dismiss and Escape handling.

SableMusicTest=true with SableTabsTest=true injects metadata/capabilities and
in-memory artwork. It checks timeline labels, enabled states, artwork/tint,
stale results, dismissal, and closing while the media broker starts. It never
sends commands to a real player. Actual Spotify/Apple Music transport,
hardware acrylic appearance and physical pointer/keyboard input remain open.
The read-only media-manager probe returns access denied (0x80070005) in this
environment. Broker discovery is therefore unverified with live players here;
the reader leaves the button hidden and logs an unavailable service nonfatally.

## Text selection and copy (2026-10-17)

PDFium loads text pages on the engine thread, then returns character codes
and loose glyph boxes as plain data. Boxes use fractions of the displayed
page after cropping and intrinsic rotation, independent of zoom and display
scale. PDFium's character order is retained, including generated spaces and
line breaks; rotated pages can have a different order. Unicode mappings,
including supplementary characters, become UTF-8 at the engine boundary.

Mouse and pen drags select inclusive character ranges across pages. Touch
continues to pan. Pointer capture keeps a drag alive outside the page, with
edge scrolling while the pointer is held. A plain click keeps link behavior;
a selection drag suppresses its subsequent tap. Zoom cancels an unfinished
drag but retains completed character ranges. Blue highlight rectangles draw
after the page filter, leaving cached rasters, thumbnails and printing intact.

Ctrl+C, Edit > Copy and the page context menu copy a selection. Ctrl+A and
context Select All select the document; Escape clears the selection. Native
text fields keep their edit keys. PDF copy permissions gate copying without
preventing selection. Copy runs asynchronously and checks the session,
selection version, visibility and close state before writing the clipboard.
Clipboard data is text only. No text editing or OCR is involved.

Character geometry is loaded on demand, with a 64-page/24 MiB data cache and
selection endpoints retained. Geometry is limited to 200,000 characters per
page, extraction for copy to 1,000,000 per page, and copied text to 16 MiB.
Visible selection pages are fetched once per viewport range to prevent a
dense selection from continually evicting and reloading itself.

The text.pdf fixture has cropped pages at every quarter-turn and a ToUnicode
mapping for Greek, accented text and an emoji. SableSelectionTest=true with
SableTabsTest=true checks native drag/copy, menu/context gating, zoom, reverse
page ranges, Select All, permissions and stale callbacks. It intercepts copied
text; actual clipboard transfer and physical pointer/keyboard input remain open.

## Find (2026-10-17)

Find runs one page per engine job, yielding to rendering and other document
work between pages. A separate search generation cancels old queries without
canceling rendering. Results arrive in batches of sixteen pages and stop at
50,000 matches. Queries are limited to 4,096 UTF-16 units, and extraction to
1,000,000 characters per page. Search does not require copy permission.

Windows Unicode normalization decomposes text and query canonically. Invariant
lowercasing implements case-insensitive matching; optional diacritic removal
keeps source character mappings, including combining marks and surrogate pairs.
Whitespace runs collapse to one space. Whole words use letter/digit/mark and
underscore boundaries. Matches do not overlap and do not cross page boundaries.
This is text search, without OCR, linguistic stemming or full Unicode case folding.

Ctrl+F and Edit > Find open a per-tab bar. Enter/F3/Ctrl+G move forward;
Shift reverses direction, and Escape closes Find before clearing PDF selection.
A new query starts near the current page and wraps. Counts update while scanning.
Navigation wraps only after the scan finishes; unknown neighbors stay disabled.
Closing retains the query/options but cancels work and removes highlights.
Session/version guards discard stale callbacks; hidden tabs never auto-scroll.

Highlights reuse the bounded character-geometry cache and draw after the page
filter but before text selection. Navigation reveals the first matched glyph.
Find changes invalidate the virtual surface, including offscreen cached tiles.
Geometry retains selection's 200,000-character page limit; denser pages can be
searched but navigation falls back to the page when geometry is unavailable.

SableFindTest=true with SableTabsTest=true exercises counts, current-page start,
wraparound, options, highlight gating, zoom, reopen and stale callbacks. Physical
keyboard/mouse interaction and screen appearance remain separate checks.

Local validation uses fresh output directories because existing compiler outputs
can become unwritable here. Debug/Release x64 and Debug ARM64 pass normally.
Release ARM64's link-time code generation cannot write its temporary file in
this sandbox; a local build with WholeProgramOptimization=false passes. The
project keeps its normal optimization settings. Its standard ARM64 Release
link still needs verification outside this environment.

## Layer API check (2026-10-17)

V14 needs layer enumeration and per-view optional-content visibility. The
pinned chromium/8086 public PDFium headers expose the UseOC page-mode value,
but no API for enumerating optional-content groups or changing their render
visibility. A functional layers panel would require a different supported
interface or a maintained PDFium extension. This remains deferred; no layer
controls are shown. View rotation (V15) is the next reading tool.

## Attachment Save As (2026-10-17)

Invoking an attachment row opens a Save As picker owned by the reader window.
The suggested name drops PDF-supplied paths and replaces Windows filename
characters and device names. The original Unicode name remains in the list.
No attachment bytes are read before a destination is selected, and only one
save can be pending per tab. Cancel leaves the target alone. Closing the tab
or hiding it before extraction completes discards the reply.

Writes run off the UI thread. A temporary file beside the chosen destination
is flushed and renamed into place; a failed write leaves an existing target
intact and removes the temporary file. The source PDF and its hard-link aliases
cannot be selected as the target. The extraction limit remains 128 MiB. Read
and write failures leave the document usable and offer another attempt. Files
are saved as bytes; Sable does not launch an attachment.

SableAttachmentSaveTest=true with SableTabsTest=true substitutes the picker
and saves synthetic files under the test temp directory. It checks Unicode,
cancel, binary/empty data, read/write errors and a picker returning after tab
close. Actual picker input and overwrite confirmation remain physical checks.

## Attachment sidebar (2026-10-17)

The right rail switches between thumbnails, outline and a native attachment
list. Each tab loads names on demand and retains the list through zoom and
sidebar changes. F4 remembers the last panel. Native list keys stay with the
list, and closing it moves focus back to the reader. Load errors offer Retry;
an empty document has a separate message. Each row keeps its PDF index, so
duplicate names will not select the wrong embedded file. Save As follows in
the next step; this step only lists the files.

SableAttachmentsTest=true with SableTabsTest=true checks names, cached rows,
panel switching, empty/error/retry and late replies after close. Physical
pointer/keyboard interaction and appearance remain separate checks.

## Attachment extraction (2026-10-17)

Attachment handles stay on the engine thread. The viewer receives names and
indices first, with file contents extracted only on request. Empty files and
unreadable streams have different results. Closing the session discards pending
metadata and bytes. The source PDF is never changed.

I cap the list at 10,000 entries, individual UTF-16 name buffers at 8 KiB and
their total at 2 MiB. Invalid names get a numbered fallback, controls become
spaces, and a partial-list flag records missing metadata. Extraction rejects
files larger than 128 MiB after PDFium reports their decoded size. PDFium can
still allocate internally while decoding; this bounds the returned buffer,
not the decoder's memory. PDFium normalizes name separators on Windows, so
the returned name is display metadata rather than an output path.

## Document outlines (2026-10-17)

Outline extraction returns a flat preorder snapshot of titles, parent indices,
initial expansion flags and potential destinations. PDFium bookmark handles
stay private to the document on the engine thread. Targets resolve only when
selected, through the same internal-destination and http(s) handler as page
links. Opening a large outline therefore loads neither every destination page
nor every URI. New outline navigation supersedes older queued navigation.

Traversal is iterative, with a visited-handle set for malformed cycles. It
stops at 10,000 nodes or 64 levels. Each UTF-16 title buffer is capped at 8 KiB,
and their total at 2 MiB. Invalid UTF-16 becomes replacement characters;
ASCII controls become spaces and empty titles become Untitled. Truncated
or malformed trees remain usable and show a short partial-outline message.

The right rail switches between thumbnails and a native TreeView. PDF count
signs set initial expansion; each tab retains its tree and later expansion
changes. F4 toggles the last sidebar. The tree keeps its native arrows and
type-ahead, while F4 and Ctrl+Alt+G retain their viewer actions. Empty outlines
and load errors have distinct messages, with Retry for errors.

Metadata loads once on demand and can finish into the cache while hidden.
Tree nodes are built when the sidebar is open and visible. Load/navigation generations,
session identity, visibility and close checks reject stale callbacks. Closing
a sidebar cancels its pending navigation without discarding its cached tree.
PDF files are never changed by expansion or selection.

Synthetic fixtures cover Unicode titles, nested open/closed nodes, rotated XYZ
and named destinations, URI/unsupported actions, circular siblings and deep
trees. SableOutlineTest=true with SableTabsTest=true checks the native tree,
selection/navigation, cache/zoom, panel switching, empty/retry and stale results.
Browser launches are intercepted; physical tree/keyboard appearance and input
remain unverified. Release ARM64 validation uses the local non-LTO variant
noted above; the source project keeps its normal optimization settings.
