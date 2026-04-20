# Reader test plan

Started: 19 October 2026. Target: the current local alpha source.

## Text-box removal in Sable (3 November 2026)

I open a disposable PDF with one multiline box and two single-line boxes.
I select the multiline entry in Comments and click Delete. The box leaves
the page and Comments, the two other boxes stay in place, and Save notes
becomes available.

I save to a new path through the Windows picker and open that copy in a
fresh reader. The page and Comments show only the two surviving boxes.
Save notes is disabled. The input hash matches before and after the edit,
and reopening leaves the saved copy's hash intact.

## Text-box draft limits and preview (3 November 2026)

I open a disposable copy of the saved four-row box below. In Edit I
append an unsupported character. Apply explains the character rule
and leaves the full draft available. I cancel and Save stays disabled.

I reopen the copy and go straight to Comments. The preview keeps the
blank second row and shows the fourth line without opening Edit. The
two single-line boxes are still listed. Save stays disabled and the
copy hash stays the same.

## Multiline text boxes in Acrobat (3 November 2026)

I open the same Windows picker copy from the Sable check below. Acrobat
shows all three boxes with no comment selected. The multiline box keeps
the blank second row and the complete fourth row. Comments shows all the
text and both neighboring entries.

Print with Document and Markups shows all three boxes at the starting
scale and at 198% with Fit. The blank row and fourth line are visible in
the enlarged preview. I restore the sizing setting and cancel the dialog.
The Font Capture application error recurs on open. Page, Comments and
print preview work after dismissing it. The input and copy hashes match
the Sable checkpoint.

## Multiline text boxes in Sable (3 November 2026)

I place a 200 by 60 point text box above two existing boxes. Two Enter
presses between the first and third lines leave a blank row on the page.
Edit refuses a height of 30 points and keeps the draft. At 550 by 413,
scrolling reveals the retained height and refusal, with both actions in
reach. Escape cancels and opening Edit again shows the original height.

I add a fourth row and grow the height to 72 points. The top-left anchor
and both neighbors stay in place. The Windows picker saves a new copy.
A fresh reader displays all three boxes and Edit restores the four rows,
including the blank one. Unchanged Apply leaves Save disabled. The Comments
list preview caps the contents at three lines. Page and Edit show all rows.

Evidence is in `D:\NightPdf\verify-textbox-multiline-20261103-917d9bb`.
The input hash matches the original fixture. The saved copy has SHA-256
`019c2a2fcc1a7e8e4433de2d7518d525de454dc79b2ac2b36fb2d60060629b15`.

## Narrow text-box editor (3 November 2026)

At 550 by 413 the dialog clipped the size inputs and wheel input did not
scroll its content. I put the fields in a scroll viewer sized to the host.
Tab now reveals the complete width input and height remains visible beside
it. Apply refuses a one-point width and wheel scrolling reveals the full
message and retained draft. Both actions remain visible and Escape returns
to the selected Comments row with Save disabled. The verification copy
keeps its hash. The short reader rail can still overlap the page box.
Debug x64 and ARM64 build without warnings; all 687 enabled tests pass.

## Resized text boxes in Acrobat (3 November 2026)

The 240 by 80 point copy and the subsequent 150 by 55 point copy both
show complete unfocused lines, the resized border and both Comments rows.
The grown copy also shows both appearances after Rotate view right. Acrobat
Print with Document and Markups previews both boxes in both files. I cancel
at preview. The earlier Font Capture application error recurs and the
viewer and preview remain usable after dismissing it.

Input and grown-copy hashes match the sizing checkpoint. The shrunk copy
keeps SHA-256 `dd73dc9c84c92bd3e121a99e585def053528fd87c4c3f72c294be40063b8489e`.
Evidence is in `D:\NightPdf\verify-textbox-next-20261103-ca82772`.
Debug x64 and ARM64 build without warnings; all 687 enabled tests pass.

## Bounded text-box sizing (3 November 2026)

The engine keeps a box's PDF top-left anchor, index, author and neighbors
while changing width/height. Exact and fractional no-ops retain live bytes.
Growth/shrink, invalid sizes, clipped glyphs, crop edges and every intrinsic
and view rotation pass. Saved print rasters match; encrypted copies retain
the password. Every cancellation checkpoint keeps the original savepoint.
Owned queue sizes survive caller changes and independent read cancellation;
no-op/refused caches, stale requests, saved neighbors, close/destruction and
callback reentry/exception boundaries pass.

Comments > Edit exposes width/height in PDF points with a read-only author.
An invalid width remains editable with a validation message; Cancel keeps
Save disabled. A one-point width refuses, restores both size fields and
keeps the draft through Ctrl+W. Corrected native width input and height
produce a size-only 240 by 80 point change, preserving both lines and the
neighbor's rectangle. The Windows picker saves to a path with spaces.
Fresh-process reopen retains the two appearances, Comments entries and
sizes; unchanged Apply stays clean. A rotated view retains both appearances.

The first native pass exposed float noise in displayed widths. The final
build shows readable two-decimal point values while keeping the original
float for unchanged text; tiny positive sizes never display zero. Both the
original 200 by 60 display and the saved 240 by 80 display pass clean Apply.
The final build repeats the size edit, picker save and fresh-process reopen.
Source and saved hashes stay unchanged through the final no-op, and all
disposable readers close. Final saved SHA-256:
`87c0839a58a494c86c40fdbfc6cb7a1131e5ad55ad9ce8505a0efebc930ffc9c`.

Debug x64/ARM64 build without warnings; all 687 enabled x64 tests and format
pass. One test stays disabled; ARM64 execution needs Windows on ARM.
Evidence: `D:\NightPdf\verify-textbox-resize-20261103-1fb474f`,
`D:\NightPdf\verify-textbox-size-queue-20261103-22af457` and
`D:\NightPdf\verify-textbox-size-native-20261103-988533b`.
Resized-box Acrobat/printing, narrow-dialog accessibility, Unicode/multiline
and wider styles remain open.

## Text-box Acrobat appearance check (3 November 2026)

Acrobat opens the saved Edited desktop box and untouched Second desktop box.
Both complete lines and rectangles display without selecting annotations;
Comments contains the two matching entries. The file keeps SHA-256
`53e5e5676afe5b64d5920f54b6ad1f8ec4f77d23d72faf65b0dbb598776821ee`.

Maximizing the viewer also shows both complete lines. Its Font Capture
process raises the same application error observed in earlier form checks.
Dismissing that helper and returning to Acrobat keeps both appearances.
The cause remains unconfirmed. I close the disposable viewer without saving.
This checks these two normal appearances, not printing or wider styles.

Debug x64/ARM64 build without warnings; all 676 enabled x64 tests and format
pass. Evidence: `D:\NightPdf\verify-textbox-acrobat-20261103-084dd48`.

## Existing text-box editing (3 November 2026)

The engine accepts a box only after its complete old appearance, font bytes
and rendered glyphs match a fresh standard box. Edits keep the annotation
index, rectangle, author and neighbors. Saved copies retain notes/bookmarks;
no-op, stale, overflow, changed appearance and cancellation keep live bytes.
The queue owns snapshots and drafts, refreshes caches after changes and keeps
inventory reads separate from editor reads. Close/destruction and callback
reentry/exception checks pass.

Native Comments > Edit opens the supported box with a read-only author.
Unchanged Apply keeps Save disabled. Overflow refuses with an editable draft;
Ctrl+W retains the dialog, and Cancel keeps the original text and clean state.
Native typing replaces the first line with Edited desktop box. The page and
Comments update while Second desktop box keeps its text and rectangle.
The Windows picker saves to a path with spaces. A fresh process displays both
lines, lists both comments and opens the saved box for an unchanged Apply
without making it dirty. Both disposable readers close and source bytes stay
unchanged. Saved SHA-256:
`53e5e5676afe5b64d5920f54b6ad1f8ec4f77d23d72faf65b0dbb598776821ee`.

Debug x64/ARM64 build without warnings; all 676 enabled x64 tests and format
pass. One test stays disabled and ARM64 is compiled, not executed.
Evidence: `D:\NightPdf\verify-textbox-edit-20261103-3908811`,
`D:\NightPdf\verify-textbox-session-20261103-d5116ff` and
`D:\NightPdf\verify-textbox-editor-20261103-3ee9064`.
Resize, Unicode/multiline, broader styles and other-viewer QA remain open.

## Multiple text boxes and native placement (2 November 2026)

Each box chooses a canonical font-resource name absent from the rewritten
PDF. Escaped names, string collisions, malformed/exhausted resources,
multiple boxes and adding after reopen pass. Mixed form values, notes and
bookmarks survive. Appearance generation now renders the actual isolated
transaction rather than sending it through another form-rendering copy.

Displayed placement passes all 16 intrinsic/view rotation combinations on
cropped pages, centers and edges, reduced small-page boxes, invalid inputs,
stale queued requests and saved neighbors. Default boxes are 200 by 60 PDF
points and stay inside the visible page; resizing is still open.

The native Comments > More command opens the text-box dialog after a page
click. A typed overflowing line refused, restored editing and retained its
draft; Ctrl+W kept the modal open. Cancel left zero comments and Save disabled.
The next placement retained that draft, a corrected native value added the
first box, and another placement added the second. The Windows picker saved
both via its keyboard path after cached element indexes failed. A fresh
process displayed both complete lines, both Comments rows and disabled Save.
The source hash stayed unchanged and both disposable readers closed.

Saved SHA-256:
`b7dbed8dfd6b5e7fc5e195feef5218f115cadbc7bc443a27e1107de3903e23d8`.
Evidence: `D:\NightPdf\verify-textbox-fonts-20261102-6ca50c4`,
`D:\NightPdf\verify-textbox-placement-20261102-9b771ac` and
`D:\NightPdf\verify-textbox-desktop-20261102-321902e`.
Unicode, multiline, resize/existing-box editing, other viewers and broader
desktop/accessibility checks remain open. ARM64 is compiled, not executed.

## First text-box backend (2 November 2026)

The appearance checks establish a bounded single-line FreeText path.
Paragraph breaks collapse and narrow boxes clip; page text extraction cannot
decode annotation text objects. The writer accepts up to 128 printable ASCII
characters before any AcroForm font resources exist. Existing resources and
a second box remain refused until resource ownership can be proved.

The isolated transaction checks the complete escaped literal, one 12-point
Helvetica text object, finite glyph bounds inside the requested rectangle,
metadata and annotation counts. The saved/reopened copy passes the same
proof before replacing live bytes. Tests cover PDF-string escaping,
permissions, stale revisions, signatures, shared arrays, invalid geometry,
all cancellation checkpoints, note/bookmark preservation, encrypted input
and identical saved raster/print output. Source fixtures stay unchanged.

Queue checks cover owned rectangles/drafts/authors, stale requests,
independent read cancellation, refreshed form snapshots, neighboring note
edits, save/reopen, session close/destruction, callback reentry and exceptions.
Native placement, an editor, Unicode, multiline and other viewer checks
remain open. This backend checkpoint does not complete the text-box tool.

Evidence: `D:\NightPdf\verify-textbox-appearance-20261102-c983d1d`,
`D:\NightPdf\verify-textbox-write-20261102-482768f` and
`D:\NightPdf\verify-textbox-session-20261102-58b87a1`.

## Password-protected forms (2 November 2026)

The encrypted form fixtures cover user and owner access, permission and
action refusals, full-value length checks, cancellation, stale queued writes,
no-ops and corrected retries. Saved copies still reject missing/wrong
passwords. Both roles retain their permissions, values and note access.

SablePasswordFormTest runs the native password prompt, including a wrong
attempt, then field hit testing, unchanged Apply, a retained length refusal,
corrected Apply, queued controls, modal keys and Cancel. It saves a copy,
opens a fresh tab through the password prompt and checks both field values
and a clean no-op savepoint. The source bytes stay unchanged.

The desktop pass used the normal 225a28b runtime. Real pointer input opened
Name, an overlong Apply retained its draft, Ctrl+W kept the modal open and
the corrected value `Café desktop` applied. The real Windows picker saved
`desktop password copy.pdf`; its Save accelerator worked after the pointer
helper reported a window-bounds mismatch. A fresh process required the
password, displayed the accented value and unchanged neighbor, and accepted
unchanged Apply. The source matches its committed blob. The saved SHA-256
is `dd21346be078420f40e6195284b9a03dc363568903330941d1e84a713cd9fad0`.

Evidence: `D:\NightPdf\verify-password-native-20261102-225a28b`.
These fixtures use the existing revision-3 RC4 writer. Other encryption
revisions and broader viewer interoperability need separate checks.

## Saved markup in Acrobat (26 October 2026)

I opened the disposable saved copy from the e01a5a2 package in the installed
Acrobat desktop viewer. Its green highlight is one even band across the
mixed-font line. Underline and strikethrough appear on their intended text.
Comments lists all five entries, including both saved Text-note contents.
The full page has no Text-note icons. The saved copy's SHA-256 is
`7e5a5327d83f52492031434bf6ec6e6a1fb7201462e83e0b3051d5489bde797d`.

A Font Capture application error, 0xc06d007e, initially covered part of
Acrobat's page. It cleared during cleanup, and I rechecked the entire
unobstructed page. Other colors, rotated pages,
printing from Acrobat and other viewers still need separate checks.

The package's Windows print dialog again showed the expected red content
and green printable annotation, with the blue non-printing annotation absent.
I reached Save Print Output As and set a disposable filename. Save could
not be activated: the desktop runner reported unavailable geometry and a
window-owner mismatch. A fresh pages.pdf attempt accepted the custom range
2-3 and landscape, then reached the output picker. No output PDF was created
in either attempt. I closed the disposable readers to clear the blocked
jobs. Completed modern-dialog output, including range/orientation results,
remains open; runtime code and packages are unchanged.

## Windows preview follow-up (26 October 2026)

Checked the extracted e01a5a2 x64 Release package, SHA-256
`9f50839dd4167d0ffcaff5838515abf9c1dc5d3911eb521d5d6b80810981685a`.
The runtime source and package contents were unchanged for this pass.

- On print.pdf, Ctrl+P opened the real Windows preview with Microsoft Print
  to PDF selected. Portrait and landscape both showed the original red
  content and green printable annotation. The blue annotation has its Print
  flag cleared and was correctly absent. The night filter stayed off output.
- Cancel returned to the reader; Ctrl+P opened another preview. Selecting
  Print reached the driver's Save Print Output As dialog. Its filename
  field could not be kept focused by the desktop runner, so that output was
  canceled. No output PDF was created and no physical printer was selected.
- The output cancellation returned to the print preview, which was then
  canceled. This checks the visible cancel paths, not a completed print job.
- The installed Acrobat cross-viewer check stopped at a desktop-access
  approval timeout. Firefox input is unsupported by the desktop runner's URL
  policy. Neither attempt verifies another viewer's annotation rendering.

Custom ranges and completed output through the modern dialog remain open.
The earlier automated driver-output checks retain their original scope.

## Annotation repair snapshot (26 October 2026)

The runtime source at 1ad57c0 passes 556 tests in each x64 Debug/Release
configuration. All four x64/ARM64 builds have zero warnings. Twelve native
reader workflows pass; annotation actions and saving passed again after
the deletion-cache change. Earlier sanitizer/stress results below retain
their original scope and are not fresh passes for this snapshot.

Windows mouse input checked selection menus, highlight creation/selection,
popup Delete, sidebar-only notes, empty-note deletion and note placement.
Saved appearances and hidden note icons passed raw PDFium reload checks.
The 200-page deletion regression keeps repeated checks off the whole book.
Other PDF viewers, physical input devices and ARM64 execution remain open.

## Reader repair regressions (25 October 2026)

- Dirty Save a Copy: notes and all three markup types survive reopening.
  Ctrl+S, pending edits, canceled/failed output and success-only savepoint
  clearing pass unit and native checks.
- Repeated edits reuse document guards; hostile shared-array refusals and
  canceled-preflight retries pass. The first append on a new page retains
  the full count check needed to detect empty shared annotation arrays.
- File writes leave the engine available. Snapshot ordering, close/cancel,
  callback thread, exactly-once completion and worker shutdown pass.
- UTC dates accept Z00'00' and reject malformed offsets. Print UI false
  results show a notice. Close-review exceptions retain edits, show a
  notice and reset close permission; later close attempts still work.
- Never-rendered markup is saved to disk and reopened through raw PDFium.
  Every Highlight, Underline and StrikeOut has a normal AP stream before
  any viewer render. No cross-viewer interoperability pass is implied.
- Mouse press/release opens the placement editor in Select and Hand modes;
  both drafts were canceled. This checks Windows mouse input routing,
  not a physical mouse or touch device. No placement code change was needed.
- Hidden smoke startup succeeds; the reader activates its window. Explicit
  process-handle acquisition also makes the full smoke pass in Windows
  PowerShell 5 and pwsh. The original historical check groups follow.

I want a repeatable record of what works, what fails and what still needs a
real device. Passing this list reduces risk; it cannot prove the absence of
bugs. Previous results are useful baselines, but are not fresh passes here.

## Working plan

- Goal: find crashes, data loss, rendering faults and reader regressions
  before calling this build ready for wider use.
- Read: reader checks, build/test scripts, native probes, and the code behind
  each failure. Start with the known printing lifetime failure.
- Change: fix confirmed defects within the current reader features, add
  regression checks where needed, and keep this record current.
- Steps: inventory, reproduce, isolate, fix, retest, run the full matrix,
  then verify portable packages from the final source.
- Verify: retain commands, exit codes, counts and failure logs. Test both
  Debug and Release; compile both architectures. Use bounded stress runs.
- Risks: this host cannot stand in for a physical trackpad, another GPU,
  a clean Windows installation or an ARM64 machine. Do not send jobs to
  physical printers or change the owner's files/settings for testing.

## Status key

The earlier reader pass recorded 86 groups: 65 Pass, 9 Partial, 11 Manual and
1 Blocked. There are no queued checks left in this local pass. Partial and
Manual rows remain open for the specific input, device or duration named.

Pending = queued; Running = in progress; Pass = checked in this pass;
Fail = reproduced; Blocked = the environment prevents the check;
Manual = needs the owner's input or another device. Partial records the
covered part and explicitly names the missing part. Each row is a group
of related checks, not a claim that every possible input has been tested.

## Checklist

| ID | What I will try | Method | Status |
|---|---|---|---|
| B01 | x64 Debug build, warnings treated as errors | Build | Pass: 0 warnings |
| B02 | x64 Release with normal link-time optimization | Build | Pass: LTO, 0 warnings |
| B03 | ARM64 Debug build | Build | Pass: 0 warnings |
| B04 | ARM64 Release with normal link-time optimization | Build | Pass: LTO, 0 warnings |
| B05 | C++ formatting and whitespace errors | Static | Pass: all 150 C++ files and diff whitespace |
| B06 | Complete Debug test suite, including printing | Unit/integration | Pass: 453 passed, 1 hard-link skip |
| B07 | Complete Release test suite, including printing | Unit/integration | Pass: 453 passed, 1 hard-link skip |
| B08 | Repeated shuffled suites, fixed recorded seeds | Stress | Pass: 5 Debug, 5 Release and 3 ASan full-suite repetitions |
| F01 | Valid PDFs, missing files, empty files, non-PDFs, damaged PDFs | Unit/integration | Pass: ordinary fixtures plus 192 damaged inputs |
| F02 | Password failure, retry, cancellation and closed tabs | Engine/native | Pass: engine and native lifecycle; physical typing in N08 |
| F03 | Unicode and spaced paths; loaded bytes survive source changes | Unit/integration | Pass |
| F04 | Restricted documents preserve copy/print permissions | Unit/integration | Pass |
| F05 | Mixed page sizes, crop boxes, rotations, page labels | Unit/native | Pass |
| F06 | Long documents; first, middle and final pages | Engine/native | Pass: includes 1,000-page native regression |
| R01 | Raster bounds, invalid dimensions and failed renders | Unit/integration | Pass |
| R02 | Filter golden colors; original colors in thumbnails and print | Unit/GPU | Pass |
| R03 | Tile boundaries, partial redraws and visible page coverage | Unit/native/visual | Partial: tile/coverage checks and sampled captures pass; reported seam needs physical retest |
| R04 | Cache budget, eviction, stale work, rerender after cache clear | Unit/native | Pass |
| R05 | Surface recreation, simulated device loss and scale changes | Unit/native | Pass: simulated draw loss, native Error and Scale; actual device loss in R06 |
| R06 | Actual GPU reset, remote desktop and sleep/resume | Device | Manual |
| Z01 | Native zoom label updates and reset completion | Native | Pass |
| Z02 | Repeated zoom in/out, limits, fit presets and rejected layouts | Unit/native | Pass |
| Z03 | Last page of 1,000-page PDF; pinch then immediate scroll | Native | Pass: two native runs |
| Z04 | Last-page words remain visible and anchor stays near that page | Native | Pass: two native runs |
| Z05 | Sideways pan and zoom around centered/narrow pages | Native/physical | Partial: native offsets/centering pass; actual trackpad pan in Z07 |
| Z06 | Interrupt zoom with tab switch, close, rotation or navigation | Review/native | Pass: activation and in-gesture navigation regressions fixed; geometry commands guarded |
| Z07 | Real two-finger pinch, diagonal scrolling and quick reversals | Trackpad | Manual |
| Z08 | Zoom buttons, keyboard and Ctrl+wheel after a pinch | Native/physical | Partial: native zoom commands pass; physical Ctrl+wheel in Z07 |
| L01 | Vertical, Horizontal, Wrapped, Page times no/odd/even spreads | Unit/native | Pass: all 12 combinations |
| L02 | Layout switching preserves the current page and valid extents | Unit/native | Pass |
| L03 | Spread-click option, both directions, cover and final spread | Native | Pass |
| L04 | Links, gaps, disabled option, selection and Hand drags with clicks | Native/physical | Partial: handlers/guards pass; physical drag and click feel remain |
| N01 | Page number draft, invalid input, first/last boundaries | Unit/native | Pass |
| N02 | Internal/named links, null coordinates, rotated destinations | Unit/native | Pass |
| N03 | Safe external links and unsupported PDF actions | Unit/native/review | Pass: action policy and native routing; browser launch intercepted |
| N04 | Thumbnails, selection, stale replies, rotation and zoom | Unit/native | Pass |
| N05 | Outline hierarchy, malformed/deep/cyclic outlines, navigation | Unit/native | Pass |
| N06 | Find: Unicode, case, whole words, wrap, no match and cancellation | Unit/native | Pass |
| N07 | Selection/copy across pages, permissions and late results | Unit/native | Pass: copy callback intercepted; actual clipboard in N08 |
| N08 | Actual clipboard and physical keyboard delivery | Desktop | Manual |
| T01 | Duplicate opens, multiple tabs, close/reopen and active tab | Native | Pass: Tabs steps 0-13 |
| T02 | Reorder, move shortcuts and tab overflow | Native/physical | Partial: reorder/lifecycle/width checks pass; physical drag remains |
| T03 | Window isolation and closing during background work | Engine/native | Pass: independent Windows mode, close during pending work |
| T04 | Explorer drops, file picker and canceled dialogs | Desktop | Blocked: storage broker denies drop fixture access at Tabs step 14; desktop picker/drop remains |
| S01 | Defaults, corrupt settings, migration and range validation | Unit | Pass |
| S02 | Atomic saves, failed-write rollback and shared state | Unit/native | Pass |
| S03 | General, Shortcuts, About; repeated open/close and modality | Native | Pass: Settings and Windows modes |
| S04 | Shortcut capture, cancel, clear, conflicts and focus guards | Unit/native | Pass: native capture/guards; physical focus in S07 |
| S05 | Dark/Obsidian persistence, live changes and PDF isolation | Unit/native/visual | Pass: native persistence/live/startup and sampled captures |
| S06 | Narrow/short settings windows, labels and clipped controls | Native/visual | Pass: narrow/short bounds and sampled captures; long bindings use ellipsis |
| S07 | Keyboard-only use, focus visibility, Narrator and high contrast | Desktop | Manual |
| P01 | Presentation entry/exit, fit, controls and state restoration | Native | Pass |
| P02 | Presentation links, endpoints, tab changes and close | Native | Pass |
| P03 | Physical keys, clicks, wheel and touch presentation | Desktop | Manual |
| A01 | Attachment metadata, Unicode names, empty/damaged streams | Engine/native | Pass |
| A02 | Attachment save cancellation, replacement and closed source tab | Unit/native | Pass |
| A03 | Save a Copy preserves loaded bytes exactly | Unit/native | Pass |
| A04 | Source alias, same path, hard links, atomic failures and late picker | Unit/native | Partial: all available cases pass; Windows denies hard-link fixture creation |
| A05 | Properties: metadata, permissions, huge/missing values and close | Unit/native | Pass: native metadata/lifecycle and sampled captures |
| A06 | Real save/overwrite dialogs and writable/read-only destinations | Desktop | Manual |
| J01 | Reproduce and isolate intermittent print-job crash/hang | Debug/Release stress | Pass: both causes reproduced, fixed and stress-tested; Q01/Q03 |
| J02 | Print preview, original colors, annotations and selected pages | Unit/native | Pass |
| J03 | Printer DPI, ranges, rotation, paper sizes and layout limits | Unit/integration | Pass |
| J04 | Cancel before/during a tile; close while pending; failing target | Integration/stress | Pass |
| J05 | Repeat jobs and wait for complete asynchronous teardown | Integration/stress | Pass: 50 shuffled ASan iterations, 250 checks |
| J06 | Microsoft Print to PDF, reopen and verify output pixels/pages | Explicit virtual driver | Pass: real Microsoft PDF driver in Debug and Release |
| J07 | Actual print dialog, physical printer, copies and collation | Desktop/device | Manual |
| M01 | Playing/recent session choice and session removal | Unit/native | Pass: injected sessions |
| M02 | Stale metadata/art rejection; seek/progress/control state | Unit/native | Pass: injected metadata/artwork |
| M03 | Bright/dark/no-art pane, bounds and reduced motion | Native/visual | Pass: native bounds/motion and bright/dark/no-art captures |
| M04 | Real Spotify/Apple Music, both players, pause/seek/switch | Media broker/device | Manual |
| X01 | Help/version, invalid CLI, redirection and exit codes | Unit/package smoke | Pass: CLI suite; final package smoke recorded below |
| X02 | Diagnostics, log rotation, unavailable storage and debug flags | Unit/native | Pass |
| X03 | Coroutine lifetimes, event teardown and engine-thread ownership | Targeted review | Pass: targeted session/print and gesture lifecycle review; not a whole-app audit |
| X04 | Repeat native gesture/layout/tab/close cycles | Stress | Partial: repeated gestures/layouts, window/close checks pass; OS drop remains blocked |
| X05 | Memory/cache bounds and process/resource cleanup after stress | Stress | Partial: sampled working sets, cache assertions and clean exits; no long-duration leak proof |
| X06 | Sanitizer or debugger diagnostics, if installed and compatible | Diagnostics | Pass: 3 full ASan suites and 50 focused repetitions; limits below |
| X07 | Bounded malformed-input and layout boundary exploration | Unit/generated cases | Pass: 120 mixed layouts and 192 damaged PDFs, deterministic seed 19026 |
| X08 | Dependency versions, license files and signing inventory | Static/package | Pass: pinned inputs/notices inventoried; unsigned EXEs and distribution review remain |
| D01 | Packaging tests: corruption, unsafe paths, mixed snapshots | Packaging | Pass: all 8 tests |
| D02 | x64/ARM64/source manifests, SHA-256 and architecture checks | Packaging | Pass: all three preflight archives |
| D03 | Fresh extraction; CLI and PDF startup/close | Package smoke | Pass: x64 preflight extraction, path with spaces |
| D04 | Release workflow/script parsing and source/package exclusions | Static | Partial: 5 scripts and 9 workflow blocks parse; first hosted run pending |
| D05 | Clean Windows PC/VM and actual ARM64 execution | Separate machine | Manual |
| D06 | Different DPI monitors, touch/pen, accessibility settings | Device | Manual |
| D07 | Opt-in notification display and click activation | Desktop | Manual |

## Findings

| ID | Severity | Reproduction / evidence | State |
|---|---|---|---|
| Q01 | Crash | ASan caught a retained print renderer releasing session state through an already freed Engine. Empty state now releases without touching the old engine; PDFium documents still close on the engine thread. | Fixed; focused regression and 50 shuffled print runs pass |
| Q02 | Needs physical confirmation | The last delivered build fixes reported long-document zoom/rendering behavior in native probes; the same book still needs a real trackpad retest. | Manual |
| Q03 | Test runner crash | Repeated COM shutdown invalidated cached PrintPageRange factories between tests. The runner now keeps its apartment for the whole suite, clears factories on final shutdown and waits for print-job completion. | Fixed; 50 shuffled print runs pass |
| Q04 | Reader regression | Native Gestures failed when a reactivated tab retained a pan flag at native 1x. Activation now clears that stale flag so zoom commands can work. | Fixed; extended Gestures passed twice |
| Q05 | Wrong page | Native Gestures reproduced navigation to page 3 after requesting the final page during native zoom. Navigation now uses scaled offsets. Layout, rotation and presentation commands are ignored during the native transform; they work again after it settles. | Fixed; extended Gestures passed twice |
| Q06 | Environment limit | The full Tabs check reaches step 14, then StorageFile access for the drop fixture fails with Access is denied. Steps 0-13 pass. The independent Windows mode checks the later window/settings cases without starting a drop. | Drop check blocked; Windows check passed |

## Run record

Local logs are in `.tmp-gesture-fix/qa-20261019/` (excluded from packages).

- Baseline print repetition: failed on iteration 2 with an access violation.
- `asan-lifetime-before.err`: heap-use-after-free in `Engine::post` from
  the session-state deleter, reached by the new retained-renderer regression.
- `asan-print-uncaught.err`: the second independent failure points to the
  cached `PrintPageRange` factory on iteration 2.
- `asan-print-apartment-fixed-50`: 50 shuffled iterations, seed 19026 and
  successive seeds, all 250 checks passed in 43.88 seconds. Peak sampled
  working set: 659.5 MiB. No sanitizer memory error was reported.
- ASan uses embedded compiler symbols (`/Z7`); `/Zi` failed with C1902 on
  this host. Allocator-pair mismatch checking uses its Windows default of
  off because the prebuilt GoogleTest DLL crosses an uninstrumented
  allocator boundary. Use-after-free and bounds checks remain enabled.
- `package-tests.out`: all eight package tests passed.
- Four normal builds passed with zero warnings. Debug logs are
  `.tmp-print-preview/reader-parity-{x64,ARM64}-Debug.log`; Release logs are
  `.tmp-print-preview/lto-host64-{x64,ARM64}.log`. Release retains normal LTO
  with the 64-bit compiler host. ARM64 was compiled, not executed.
- The complete x64 Debug and Release suites each ran 454 tests: 453 passed
  and the existing hard-link alias check skipped with access denied.
  The virtual-printer test is disabled by default and was run separately.
- `debug-full-shuffled-5`: five complete passes, initial seed 7519;
  `release-full-shuffled-5`: five complete passes, initial seed 19026.
  Each repetition uses the next seed. Peak sampled working sets were
  234.0 MiB and 249.3 MiB respectively; both processes exited normally.
- `asan-full-shuffled-3`: three complete passes, initial seed 19026,
  453 passes and one skip per repetition. No sanitizer error; peak sampled
  working set 738.8 MiB. These checks instrument Sable's test-linked C++;
  prebuilt PDFium, GoogleTest and system DLLs are not fully instrumented.
- The new robustness tests cover 120 generated mixed-size documents across
  all 12 layouts and 192 damaged/truncated PDF buffers. Seed 19026 makes
  these bounded cases repeatable. This is not a sustained fuzzing campaign.
- Twenty-five native modes passed: Gestures, LongGestures, Link,
  Presentation, Rotation, Pan, Layout, Selection, Find, Keyboard, Edit,
  Filter, Settings, Properties, SaveCopy, AttachmentSave, Attachments,
  Outline, Finish, Print, Music, Scale, Error, Obsidian and Windows.
  Logs use `native-final-*`, with Windows in `native-repeat2-Windows`.
  Gestures and LongGestures passed again in `native-repeat2-*` after the
  independent Windows mode was added. The full Tabs result remains Q06.
- Native modes invoke handlers and real WinUI layout/transforms. They do
  not prove physical keyboard, pointer or trackpad event delivery. Settings,
  music and save operations use isolated stores or injected test callbacks.
- Sampled captures were inspected for General at narrow/short sizes,
  shortcut rows and capture, Obsidian About/PDF/layout, narrow properties,
  and music with bright, dark and missing artwork. Controls fit and text
  stayed readable. Narrow shortcut bindings intentionally use ellipsis.
- `debug-virtual-print` and `release-virtual-print` both passed the explicit
  Microsoft Print to PDF test. Generated output was reopened with PDFium
  and checked for page range/count, paper sizes, colors, annotations and
  rotation. No physical printer was used.
- Five repository PowerShell scripts and nine release-workflow run blocks
  parsed without errors. The official action READMEs were checked against
  the workflow versions. This is not a hosted workflow execution.
- Cache bounds and clean child-process exits passed. Working-set samples
  above are observations, not a leak detector or a long-duration guarantee.
- Fresh x64, ARM64 and source preflight archives passed manifest, content
  hash, source-snapshot, architecture and notice checks. The extracted x64
  app passed help/version and PDF startup/close from a fresh path containing
  spaces (`preflight-smoke.out`). These use the final Release binaries.
  Documentation was completed after preflight; delivery archives are rebuilt
  and verified from that completed source snapshot.

## Completion boundary

Do not mark a public release ready with unresolved crashes, data loss,
missing page content, incorrect output or a failing required build. Keep
skips and environment limitations visible. Manual checks are described in
[READER_CHECKS.md](READER_CHECKS.md). Git commits, tags, publishing and the
first hosted workflow run remain outside this local test pass.

The next manual pass should start with the same book and trackpad that
showed the zoom problem, then cover OS dialogs/clipboard/drop, real music
players and display changes. A fresh Windows PC and an ARM64 device are
separate release checks. Keep actual results and device details alongside
this record instead of turning an unavailable check into a pass.
