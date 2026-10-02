# Decisions

Why things are the way they are, so I don't have to remember it all later.

## Drawing from Comments (2026-11-07)

"Draw on page" arms a drawing tool that stays on until Stop drawing, Esc,
another placement tool, hidden Comments or closing. Each mouse or pen
stroke, from press to release, is written as its own Ink annotation; a
click without moving leaves a dot. Touch keeps scrolling the pages, as it
does for selection, so drawing needs a mouse or pen for now.

Points are kept as fractions of the page the stroke started on. I clamp
them 1.5 pt inside that page so the 2 pt line never crosses an edge and
the guarded writer doesn't refuse the whole stroke. Points closer than
half a DIP are skipped, and a stroke stops collecting at 4096 points.
The live preview is a XAML polyline over the scroller, drawn in the theme
text color, and stays until the write finishes. Presses while a stroke is
being written are ignored rather than queued. Drawings list as "Drawing"
and use the guarded Delete; editing their strokes isn't offered yet.

## Drawing style (2026-11-07)

Comments has a Drawing style submenu next to Draw on page. The colors are
pen colors that read on white paper: black, blue (#2563EB), red, green
(#1F9D55) and yellow. Red (#EB4949) and yellow (#FFF066) keep the
approved highlight values; the pale highlight green, light blue and pink
were tried first but fade as thin lines, so they stay highlight-only.
Widths are Thin (1 pt), Medium (2 pt) and Thick (4 pt); black at Medium
is the default. Picking either starts drawing, and the hint names the
current style.

Each color shows as a filled dot inside a grey ring, drawn into a small
bitmap. The ring keeps black visible on the dark menu and pale colors on
the light one; the highlight menu uses the same swatch.

The style is shared by every tab and window while Sable runs, and each
menu shows the current choice when Comments refreshes. It isn't saved,
so NightPDF's settings keys stay as they were and a restart begins at
black 2 pt. Each stroke keeps the style it started with. Points keep half
the width plus 0.5 pt from the page edges. The preview runs the chosen
color through the same CSS filter math as the page, so it matches what
the saved line will look like under the current preset.

## Freehand ink strokes (2026-11-07)

A drawing is one Ink annotation holding all its strokes in PDF points.
The appearance is a stroke with round caps and joins, so a single tap
still draws a dot; it defaults to black at 2 pt. The Rect covers half the
line width around every point, and the InkList keeps the same points so
other editors can rebuild the stroke.

Since 7 November a drawing also takes an opaque color and a width from
0.5 to 12 pt. I write /C and a Border of [0 0 width] before the
appearance, because PDFium refuses to set a color once an appearance
exists and drops the appearance when the border changes. The saved-copy
check reads the border back; PDFium won't read /C while an appearance is
present, so the tests read it from a copy with the appearance stripped.
Translucent colors and other widths refuse.

The writer shares the outline shapes' path: a canonical scratch copy,
the signature scan, a saved and reopened check of the subtype, flags,
Rect, appearance and every InkList point, then the swap. I cap a drawing
at 256 strokes, 4096 points a stroke and 16384 points in all; empty
strokes, non-finite points and anything past the page box refuse.

## Resizing outline shapes (2026-11-05)

I resize plain Square and Circle outlines around their original PDF top-left
corner. The old shape must match the existing black two-point model, both
in its appearance commands and in a rendered comparison on an isolated
page. Matching dimensions keep the exact old coordinates and revision.
A form's transformed BBox is fitted to the Rect, so a plain translation
/Matrix draws the same pixels; it passes and gets the standard stream.
A rotated matrix or a stretched BBox moves the outline and refuses.

Only the target's stream is held to 64 KiB. Neighbor snapshots share the
128 MiB copy budget instead, since explicit-line text boxes draw glyph
outlines and passed that limit on the first desktop page I tried (7 Nov).

The transaction checks the snapshot, permissions, unique ownership and
annotation relationships before writing. Custom styles, metadata, alternate
appearances and protected or shared targets refuse. I reopen the saved copy
and check the new bounds, appearance, annotation counts and neighboring
annotations before replacing the live document. Color, border width and
moving the anchor remain separate work.

## Placing rectangles from Comments (2026-11-04)

I use two page clicks for an outline rectangle. The first click captures
the page and revision. A click on another page keeps that corner. Rotating
the view clears it so the next click starts a new pair. Escape, changing
tools or hiding Comments drops the placement. A committed rectangle uses
the existing Save notes flow.

Comments calls PDF Square entries rectangles. I reuse the guarded Delete
action for independent rectangles, with the same sharing, thread and
permission checks as notes. The shape has no text editor.

## Queued rectangle corners (2026-11-04)

I queue the two displayed corners by value and convert them together on
the engine thread. The crop and both rotations determine their PDF points.
Either corner can come first. A stale revision or out-of-page point refuses.

## Rectangle appearance (2026-11-03)

I use a Square annotation with Print and an explicit even-odd fill for
the outline. The coordinates are PDF page points. The pinned writer keeps
the four edges and the empty interior after a save in both screen and
print rasters.

I add rectangles on an isolated copy. The live document changes only
after a saved reopen confirms annotation counts, bounds, Print and the
exact appearance stream. A thin or off-page rectangle refuses. I use a
two-point black border until the shape tools expose their own settings.

## Independent text-box removal (2026-11-03)

I allow FreeText through the existing annotation removal checks. Deleting
the page reference does not need the appearance proof used to edit a box.
Protected, shared and threaded boxes still refuse, and the revision keeps
older targets from acting on a neighbor after an index shift.

I use the existing Delete button, row menu and focused Delete key in
Comments. Removing a box marks the document dirty for Save notes.

## Multiline text-box previews (2026-11-03)

I use the same eight-line limit for text boxes with explicit breaks in
Comments. A blank row still takes a line, and wrapping in a narrow
panel can still trim the preview. Other entries keep three body lines.

## Text-box draft limits (2026-11-03)

I use the same text rules before queueing a draft and in the PDF writer.
The dialog can point out an unsupported character, too many rows, too
many characters or an empty multiline draft while keeping the input.
A blank row counts toward the limit. The writer still checks the
actual box and page geometry and proves the saved appearance.

## Multiline text boxes in Comments (2026-11-03)

Enter adds a line in the shared text-box editor. I turn CR and CRLF into
LF before queueing a draft, and count the normalized text against the
128-character limit. The editor can wrap long text for display. The PDF
keeps the actual breaks and refuses a line that is too wide. I keep the
draft in the dialog so it can be shortened or the existing box resized.

New boxes start with the same placement and size. The edit dialog can
grow a box for more lines if the page has room. The existing snapshot
proofs still decide whether a saved box can be edited.

## Isolated writes for explicit text-box lines (2026-11-03)

I opt into explicit lines at the engine boundary. Creation, replacement
and resizing still use the saved-copy transaction and preserve the index,
author, anchor, permissions and neighboring annotations. Existing editor
requests use the single-line layout until they can preserve LF drafts.

An old multiline appearance must match the complete generated stream and
its bounded paths. I also compare its drawing to a fresh stream on a
throwaway page with unrelated drawing removed. Matching stream bytes alone
cannot prove placement because the appearance dictionary can scale them.
Both layout and render comparisons can refuse unsupported older boxes.

## Explicit lines in text-box appearances (2026-11-03)

The pinned FreeText generator flattens paragraph breaks. I build a bounded
vector appearance from the standard Helvetica glyphs and keep exact text in
Contents. SetAP replaces the stream and its resources, so the vector stream
uses no font name. An inset filled border also avoids the extra stroke bounds
reported by the path parser.

I keep LF breaks, including empty rows, and refuse any line that would need
wrapping. The proof accepts up to eight rows and 128 ASCII characters and
checks every path against the box, exact saved stream bytes and saved raster.
The 12-point face and the original single-line writer stay in place.

## Scrollable text-box fields (2026-11-03)

I put the text-box fields in a scroll viewer sized to the dialog host.
Short windows can reveal sizes and refusal text by scrolling while the
dialog keeps Apply and Cancel below the fields. Keyboard focus brings the
size inputs into view.

## Native text-box width and height (2026-11-03)

Comments > Edit shows width/height in PDF points for supported boxes. Size
follows the original page orientation, with a fixed PDF top-left corner and
author. I show sizes to two decimal places and retain the original float
when its displayed text stays unchanged. Tiny positive sizes keep enough
digits to avoid showing zero. Unchanged Apply keeps the exact rectangle.
The parser consumes a complete positive finite number; invalid text stays
in the controls with an explanation.

Apply owns dimensions with the text snapshot. Pending controls are read-only;
refusal restores them with their drafts, and Cancel leaves the box alone.
The existing dirty-state, page/Comments refresh and copy-save path handle
accepted changes. New-box placement keeps its existing initial size/drafts.

## Queued text-box sizes (2026-11-03)

The existing text-box queue accepts owned width/height together with the
proved snapshot and replacement text. Both entry points use the same write
completion, cache refresh and revision handling. Read cancellation leaves
size writes alone; close can cancel before the isolated document swap.
Matching dimensions and contents keep the cache and revision. Changed sizes
save through the existing copy path and preserve neighboring annotations.

## Bounded text-box resizing (2026-11-03)

I extend the existing text transaction with width and height in PDF points.
The PDF top-left anchor stays fixed; width grows right and height grows down
in unrotated page coordinates. Matching sizes keep the exact old rectangle.
The old appearance still needs the complete standard-box proof before a new
reference proves the requested text and size. I update Rect and regenerate
AP on the isolated copy, then verify saved geometry before the document swap.

Invalid sizes, crop overflow and clipped glyphs refuse without changing live
bytes or revision. The fixed author, single ASCII line, 12-point Helvetica
and protected/shared/style refusals stay in place. Native controls follow.

## Editing text boxes from Comments (2026-11-03)

I enable Edit for FreeText rows and ask the engine to prove support before
opening the shared native text-box dialog. Existing boxes start with their
saved text and a read-only author. Apply queues the owned snapshot; refusal
restores editing with the draft intact. Cancel leaves the box alone.

Accepted no-ops close without marking the tab dirty. Changed text refreshes
the page and Comments through the existing save path. Editing never replaces
the new-box draft or author. Size, position, deletion, Unicode and multiline
text remain outside this step; unsupported styles show a refusal notice.

## Queued existing text-box edits (2026-11-03)

I return an owned, proved Annotation snapshot for the native editor and queue
that snapshot with replacement text. Box reads have a separate generation so
Comments inventory reads do not cancel them. Canceling annotation reads also
cancels box reads; writes only observe close before the document swap.

Accepted no-ops return the original index without changing the revision or
cache. Changed writes refresh the form cache and session annotation revision.
Callbacks run outside the lock and a committed result stays committed if its
callback closes the session or throws. The engine's style limits still apply.

## Native text-box placement and creation (2026-11-02)

Comments > More now offers Place text box on page. It shares note placement's
page hit, gap handling and cancellation. The native dialog keeps its own
text/author drafts, reads the current revision before opening and queues the
displayed point. Refusals retain the draft; queued controls and closing stay
blocked until the result arrives. Accepted boxes use the existing comments,
dirty-state and Save a Copy path.

The dialog describes the single-line, 128-character English-text boundary.
The fixed initial rectangle and 12-point font keep the earlier backend proof.
Resizing, Unicode/multiline and editing existing boxes remain separate work.

## Displayed text-box placement (2026-11-02)

I map the displayed page fraction through PDFium with the same crop and
extra-rotation convention as note placement. The first box is 200 by 60 PDF
points, reduced to the visible page if necessary, centered on the click and
clamped inside the visible bounds. The saved glyph-fit proof still decides
whether the text fits. Resizing stays separate from this placement step.

The queue owns the displayed point and rotation with the draft and revision.
Raw-coordinate requests keep their existing path; both share commit, cache,
cancellation and close behavior.

## Independent text-box font resources (2026-11-02)

I choose a SableTextBoxFont name absent from the fully rewritten PDF before
creating each box. The rewrite expands object streams and canonicalizes
escaped names; an absent name cannot resolve to an existing DR font. PDFium
then creates its standard fallback font under that name. I leave existing
fonts alone and repeat the saved literal, Helvetica and glyph-bound checks.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_generateap.cpp

The search is bounded to 4096 names. Matching strings/streams can skip names
conservatively; exhausting the names refuses without changing live bytes.
Multiple boxes and existing AcroForm fields now use the same isolated path.
Malformed resources, signatures and the earlier text/geometry guards remain.

## Queued first text-box writes (2026-11-02)

I queue the rectangle, text, author and explicit snapshot revision as owned
values. Successful writes refresh the form cache and the session's annotation
revision. Read cancellation does not cancel writes. Closing can cancel the
isolated transaction before commit; a committed result remains a change even
if its callback closes the session. Callbacks run outside the session lock.

The queue retains the writer's first-box and appearance restrictions. Saved
copies contain real FreeText annotations, while the native editor and page
placement are still separate work.

## Isolated first text-box writes (2026-11-02)

I create the first bounded FreeText box on an isolated rewritten document.
Printable ASCII is limited to 128 characters on one line. A 12-point
Helvetica appearance must contain the complete escaped literal, exactly one
text object and finite glyph bounds inside the requested page rectangle.
I repeat those checks on the saved copy before replacing the live document.

The snapshot revision, annotation permission, signature scan and shared
annotation-array checks run before commit. Cancellation and every refusal
keep live bytes and revision intact. Notes, bookmarks and password protection
follow the existing saved-copy path. Existing AcroForm resources stay refused,
including those generated by a previous box: further boxes need a resource
ownership proof. This is a backend foundation; native placement/editing,
Unicode and multiline boxes remain open.

## First text-box appearance proof (2026-11-02)

I use PDFium's FreeText appearance generator on an isolated copy. Its
standard-font stream survives saving, but line breaks collapse and narrow
boxes clip. The page text extractor does not decode annotation text
objects, so it cannot prove the requested text survived in the appearance.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_generateap.cpp
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_edittext.cpp

The first writer is bounded to printable single-line ASCII. I verify its
literal appearance text and text-object bounds before and after saving.
Existing AcroForm dictionaries stay refused so their font resources cannot
replace the generated standard font. This also means another box after a
generated AcroForm needs a later resource-ownership proof. Multiline,
Unicode, font choices, placement and a native editor remain separate work.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_view.cpp

## Password-protected form copies (2026-11-02)

I retain the supplied credential only on encrypted documents and clear its
owned storage on destruction. Snapshots, drawing, canonical writes and
saved ownership proofs reopen through the same helper. Every reopened copy
must match the original security-handler revision, effective permissions
and user permissions, including copies opened with an owner password.

Save flags stay unchanged; no security-removal flag is used. Permission,
action, signature and field-shape refusals still run before editing. The
password stays out of session results, logs and settings. The saved copy
still asks for its password, and both user and owner roles retain their
access. This does not add password creation, removal or replacement tools.

## Remaining form proof gaps (2026-11-02)

I checked the remaining E01 limits against chromium/8086. Ordinary field
parents are dictionaries, not annotations. GetLinkedAnnot requires Type
Annot, so it cannot expose their raw V/type, Opt or MaxLen. Effective field
values still cannot prove an inherited checkbox's stored value, and the
number API cannot prove an inherited Comb cell count. Both stay refused.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp

DoNotScroll changes the editor's scrolling and overflow behavior. The
public form API has no overflow query; a complete stored value alone does
not prove it fits the field. I keep this flag refused too.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_textfield.cpp

Password forms open successfully, but snapshots, drawing and transactions
reopen their scratch copies without the supplied password. I can carry that
credential in the document's lifetime and keep the existing save flags:
PDFium retains security unless explicitly asked to remove it. This is the
next bounded slice, with saved encryption/permission checks and the earlier
field, action and signature guards intact. No password belongs in settings,
logs or a temporary file. The fixtures use the existing RC4 test writer.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/edit/cpdf_creator.cpp
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/public/fpdf_save.h

## Reader save repairs (2026-10-25)

Save a Copy and Ctrl+S use Save notes while a tab has unsaved edits. Cancel
and failure retain those edits; only successful output clears the UI
savepoint. A modified document also serializes its edits when copied through
the lower-level API. Untouched documents still keep their exact loaded bytes.
This replaces the original-byte-only behavior recorded in the earlier entries.

I serialize on the engine, write owned bytes through the atomic writer on
its file worker, and finish callbacks on the engine. Shutdown drains both
parts before PDFium is torn down. Source and alias protection stay in place.
This replaces the engine-thread file writing recorded on 24 October.

Serialization preflight, annotation counts and verified dictionary identity
are cached per document. A page's first append still checks all page counts:
an empty shared annotation array has no public identity to compare. Once
that page is proven independent, later edits recheck only affected pages.

PDFium creates its default markup appearances during rendering. I use a
one-pixel render on edited pages before saving, then check the normal AP
stream. Saved Highlight, Underline and StrikeOut entries no longer depend
on those pages having appeared on screen first.

## Editing markup comments (2026-10-25)

I open the existing editor for Highlight, Underline and StrikeOut rows in
Comments. It reads exact text before opening and owns its chosen type and
Contents presence through Apply. An untouched field keeps raw line endings.
Refusals keep the draft and show an error; successful writes use the same
cache refresh, Save notes and close review. Text-note editing stays as it was.

## Markup comment contents (2026-10-25)

I edit only plain Contents on Highlight, Underline and StrikeOut. Snapshots
distinguish a missing key from an empty string, and Apply checks both exact
text and presence before writing. Read-only, locked-contents, rich-text,
malformed, oversized and unsafe-to-save targets refuse. Empty replacements
stay refused; adding text to a markup without Contents is supported.

I refuse markup dictionaries referenced on another page or more than once
on their own page. The pinned public GetAnnotIndex identity behavior has
regressions for shared and independent annotation arrays. Updates keep
author, flags, quads and appearance. A failed setter restores an existing
string; when the original key was absent I keep the modified state rather
than pretend an empty string restored absence. Legacy Text-note APIs keep
their existing behavior.

## Underline and strikethrough selections (2026-10-25)

I add Underline and Strikethrough beside Highlight in the selection menu.
All three share permission, busy, save and close guards. Completed writes
stay dirty when hidden; newer and refused selections stay available.
Comments, Save notes and close review handle the completed edits. The plain
Text-note editor stays separate. Colour controls and other tools stay for
later.

## Line markup transactions (2026-10-25)

I use the same bounded page transaction, clipped PDF text quads, annotation
permission and shared-array rollback for Highlight, Underline and StrikeOut.
The engine queue owns the chosen type along with the selection.

I keep PDFium's default appearances: yellow Multiply for Highlight, black
one-point lines for Underline and StrikeOut. Saving before the first render
still leaves enough geometry to recreate those strokes on reopening.
Page rotations work through the usual page transforms. Quads follow the
axis-aligned text rectangles, so individually rotated glyph baselines are
still a limitation.

## Highlight selection action (2026-10-25)

I add Highlight to the text selection menu. It captures the current PDFium
text indices and holds the existing note write/save/close guards while the
engine works. A completed write stays dirty even if its viewer was hidden.
I clear only the selection that submitted a successful write; a newer or
refused selection stays available.

Highlights refresh the existing page, thumbnail and Comments caches and
use Save notes and its unsaved-close review. The plain Text-note editor
stays limited to Text annotations. Colour controls, extracted comment text
and other markup tools can follow separately.

## Persistent selection highlights (2026-10-25)

I derive highlight quads from the selected PDFium text indices on the
engine thread, clip them to the media/crop intersection and add one
printable Highlight per page with visible rectangles. PDFium supplies its
default yellow and multiply-blended appearance. I omit extracted Contents
so highlighting depends on annotation permission alone.

I preflight the complete selection and refuse unsafe attachment rewrites.
The bounded transaction accepts at most 128 pages, 200,000 text indices
per page, 1,000,000 selected indices and 4,096 quads in all. Each page keeps
the existing 4,096-annotation limit. Empty/outside rectangles are skipped;
a selection with no visible rectangle refuses without an edit.

I verify every page's expected annotation count before accepting the
appends. Shared annotation arrays that change another page force reverse
rollback, as do failed setters. Successful rollback restores the earlier
modified flag; incomplete rollback keeps the document dirty. I apply the
same count guard to new Text notes so raw, centered and placed notes stay
on their chosen page too.

## Existing-note editor (2026-10-24)

I edit the selected Text note's contents from Comments. Author, position
and flags stay as they were. The editor reads exact UTF-16 from the engine;
the cleaned Comments text is only for display. If the text box normalizes
line endings, applying an untouched field keeps the original contents.

Apply holds the dialog while the engine works. A refusal leaves the text
editable with an inline error; a successful change refreshes comments,
pages and thumbnails and uses the existing Save notes and close review.
An unchanged edit keeps the current savepoint. Cancel discards this edit.

## Existing text-note contents (2026-10-24)

I update only the Contents string of an existing Text annotation. Each
write checks its exact previous UTF-16 contents on the engine thread,
so an old editor cannot replace a newer edit. An identical value succeeds
without modifying the document. I restore the previous string after a
setter failure and keep the document modified if restoration fails.

I refuse ReadOnly or LockedContents notes, rich-text RC entries, missing
or non-string contents, invalid or oversized UTF-16, restricted documents
and files whose attachments cannot survive PDFium's rewrite. Locked alone
protects properties and still permits changing contents. These checks
keep author, geometry, flags and existing appearances intact.

## Note placement interaction (2026-10-24)

Place note on page arms a one-shot page tap while centered Add note stays
available. Choosing a page opens the editor; only Add changes the PDF.
The target keeps its page, normalized position and render-key rotation
through later zoom or navigation. Canceling the editor keeps its draft.

Placement taps bypass links and selection. Mouse presses stop the custom
hand-pan path; touch presses keep native scrolling. Escape or the placement
button cancels, as does hiding the viewer, Comments or its rail. Saving and
closing wait for placement to finish or be canceled. This does not add a
stored setting or change an existing shortcut.

## Displayed note positions (2026-10-24)

I convert normalized page positions on the engine thread with PDFium's
device-to-page transform, including crop, intrinsic rotation and extra
view rotation. A 20-point icon stays centered on the chosen position until
an edge needs it clamped inside the visible bounds. Invalid coordinates,
rotations, undersized bounds and unsafe rewrites refuse before mutation.
Raw-position and centered APIs retain their existing meaning. The session
keeps the same owned-text, queue-order and started-edit close boundary.

## Unsaved note close handling (2026-10-24)

I review unsaved notes before removing a tab or accepting a native window
close. Save notes writes a separate PDF, Discard closes without writing,
and Cancel keeps the document open. A canceled picker or failed save also
keeps the document open. Pending edits, saves and note dialogs hold closing
until they finish; other dialogs hold review only when notes are unsaved.
Clean tab closing keeps its previous dialog-dismissal behavior.
Window review holds tab removal, document opening and new edits while it
checks each document. Discard choices do not clear notes in tabs retained
when a later document cancels the window close.

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
rejects old render replies before refreshing page and thumbnail caches
and comments. The temporary edited-file limit is 1 GiB.

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
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/edit/cpdf_creator.cpp

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

## Reader reliability checks (2026-10-19)

The printing stress run found a real shutdown bug. A print renderer can
outlive its closed session and engine, so releasing an already empty state
must not post work through that old engine. I now delete empty state directly;
state that still owns a PDFium document keeps the engine-thread cleanup.
The regression retains a renderer through session and engine shutdown.

There was also a separate crash in the test runner: cached WinRT print
factories survived the temporary COM apartments used by individual tests.
The runner now keeps one apartment alive through the suite, clears cached
factories before shutdown and waits for asynchronous print jobs to finish.
That makes repeated printing checks useful without changing the print UI.

The gesture checks caught two more transitions. Returning to a tab now
clears a completed pan flag, and page navigation accounts for the native
scale while a pinch is active. Layout, rotation and presentation commands
are ignored during that transform and become available once it settles;
they are not queued. This avoids changing page geometry under an active
gesture. TEST_PLAN.md records the regressions, bounded stress runs and the
device checks that still need a person at the app.

## Trackpad stability and spread clicks (2026-10-19)

The first gesture pass could stop updating visible pages while waiting for a
native zoom reset. It also resized the document before removing the native
scale, temporarily applying both changes to the scroll extent. That left room
for blank tiles, stale percentages and jumps in long documents.

I now keep rendering through a scaled viewport during the gesture, update the
percentage from ViewChanging, and wait for direct manipulation to finish.
The native scale resets before the PDF layout changes. The anchor comes from
the scroll offsets in document coordinates, with a bounded reset timeout.
Programmatic jumps use the new document extent and retry after layout so an
old compositor extent cannot clamp a last-page zoom to an earlier page.
Nullable link coordinates also carry a pending horizontal position forward
instead of restoring an older compositor offset.

View > Click pages to turn spreads is an optional choice per document tab.
It is available in Page scrolling with odd/even spreads and starts off.
The left page turns back one whole spread; the right page turns forward.
A lone cover or final page uses its left/right halves. Page margins do not
turn pages, selection and Hand drags stay intact, and PDF links take priority.
The choice lasts for that tab; it does not add a saved global preference.

## Trackpad scrolling and pinch zoom (2026-10-19)

I enabled the reader's native ScrollViewer zoom for two-finger trackpad
pinches and kept scrolling available on both axes without rail locking.
Windows handles gesture motion and inertia. Once zoom settles, I map the
viewport center back to its page position, rebuild the PDF layout at that
scale and reset the native transform and offsets together. This keeps the
zoom controls, sharp page renders, selection and Find in the same space.
The relative native limits follow the existing 10–1,000% PDF zoom range.

Ctrl+wheel now uses the same native zoom path, so one wheel event cannot
apply both Windows' zoom and the old stepped wheel handler. Keyboard and
button zoom steps keep their existing behavior. Presentation disables
native zoom and restores it on exit. No new dependency or setting is needed.
The native probe checks the real transforms and synchronization; physical
trackpad input still needs a desktop check.

## Portable alpha preparation (2026-10-18)

The first review builds use portable ZIPs for x64 and ARM64. This keeps the
existing self-contained runtime folder intact and adds no installer or
update dependency. Choosing Velopack or MSIX, registering file associations
and adding an updater remain separate decisions.

scripts/package.py streams an existing Release folder into a ZIP. It checks
the executable, PDFium and WinUI architectures, compares PDFium with the
pinned input, includes the supplied license/notice texts and excludes test
and linker outputs. File hashes, the base commit and a hash of the actual
source snapshot go in a manifest. A separate source ZIP preserves that
snapshot. Local candidates are named `-local-`; tagged packages require a
clean checkout and a tag matching VERSION and HEAD. Existing ZIPs are never
overwritten, and checksum collection rejects mixed source snapshots.

release.yaml builds both targets, runs x64 and packaging checks, verifies
the archives, checks an extracted x64 package and creates a draft prerelease.
It does not publish, replace existing release assets, create tags or push.
The source still has to be committed and reach GitHub through the existing
queue before this workflow can build it. Local script checks do not count
as a GitHub Actions run or a clean-machine/ARM64 runtime pass.

The actual Windows App SDK binary package texts include Microsoft Software
License Terms. THIRD_PARTY_NOTICES now distinguishes these from the source
repository's MIT license, and identifies PDFium's text at its real path in
the downloaded bundle. Both distribution questions remain open. Preserving
the texts in a ZIP does not settle them or change Sable's GPL-2.0-only
license. Keep public binary publication pending that review and the desktop
checks in RELEASING.md.

## Virtual printer verification (2026-10-18)

The installed Microsoft Print to PDF driver can be checked without changing
the default printer or sending paper jobs. An explicit opt-in test sends
Sable's real Direct2D/XPS output to that named driver and gives it a new file
in a unique temp folder. It reopens the result on the PDFium engine thread
and checks selected pages, portrait/landscape sizes, original colors,
print-visible annotations and rotated content.

The diagnostic uses the older XPS job API only to supply the output filename.
That API is deprecated, so its warning is scoped to this test helper. The
reader still uses the modern Windows print UI; no production printing path
or dependency changed. The test passes in Debug and Release, and keeps the
two output files for review. Test-supplied options and an in-memory XPS target
remain part of this setup, so I am keeping actual dialog input, cancellation,
physical printers and copies/collation on the desktop checklist.

## Release build host and desktop checks (2026-10-18)

The 32-bit compiler host kept failing while writing WinUI's LTO temporary
files, even with a fresh TEMP folder. Using the installed 64-bit host lets
both x64 and ARM64 Release builds finish with whole-program optimization.
scripts/build.ps1 now sets PreferredToolArchitecture=x64. The project keeps
its normal Release optimization settings; the local non-LTO workaround is
no longer needed for these script builds.

The desktop input check sends key chords through Windows rather than calling
the command router. It only sends while its own reader is in the foreground
and no modifier or pointer button is held. Chords include their key-up events;
a partial batch releases the keys it sent. The check covers drafts, zoom,
Find, rotation, tools, presentation, Save dispatch and modal isolation.

This session rejects SendInput with access denied (5), accepting 0 of the
first 2 events. That is a blocked input check, not a reader pass. The native
command checks remain valid. README and READER_CHECKS.md now describe the
implemented features and the remaining desktop, player, printer and monitor
checks. No input-delivery claim is based on a direct handler call.

## V21: Reader shortcuts (2026-10-18)

I checked the key list against webViewerKeyDown() in NightPDF's bundled PDF.js
viewer. Window commands now use one route, so page navigation and Ctrl+Alt+G
also work while a tab header has focus. Text fields keep their editing keys;
buttons keep Enter and Space. Sidebar lists, zoom selectors and sliders keep
their own navigation. Properties and password dialogs keep reader commands out
of the document behind them.

Ctrl with =/+, -, or keypad +/- changes zoom. Ctrl+0 or keypad 0 resets zoom;
Shift variants work too. Find uses Ctrl+F, Ctrl+G and Ctrl+Shift+G, with F3
and Shift+F3 retained. R/Shift+R rotates, H/S changes tools, F4 toggles the
sidebar, and the existing page-turn keys retain PDF.js's page-fit rules.
Windows-key chords stay with Windows. Configured tab bindings still run ahead
of document keys, while the fixed menu commands remain fixed.

Ctrl+Alt+P enters presentation mode. Ctrl+Up/Down reaches the first or last
page there, including with Shift. Open, Print and Save leave presentation
before opening their dialogs. F11 toggles full screen, with repeat presses
ignored; Ctrl+M minimizes. Native command checks cover routing and focus, but
physical key delivery and keyboard-layout checks remain separate. Caret
browsing and editing shortcuts wait for their corresponding features.

Initial V21 validation used local non-LTO Release builds after temporary-file
access denials. The later build-host change above restores normal LTO.

## V20: Save a Copy (2026-10-18)

File > Save a copy, or Ctrl+S, opens a save dialog owned by the reading
window. It suggests the PDF's name with " copy" added. Saving leaves the
current tab open and keeps its page, zoom, rotation and night filter. Cancel
is silent; a failed save leaves any previous destination intact. The source
PDF cannot be chosen as the destination, including path aliases. Switching
tabs or closing the document cancels a pending dialog result.

The guide suggested FPDF_SaveAsCopy. A preservation test found that PDFium
rewrites a valid empty attachment into an eight-byte compressed payload. Sable
does not edit documents yet, so I save the bytes already loaded in memory
instead. This follows the bundled viewer's downloadOrSave() path for an
unchanged PDF. Attachments, passwords, restrictions and signature bytes stay
exact, even if the original file has since moved or disappeared. Editing will
need a separate save path when it arrives.

The engine writes chunks to a new temporary file beside the destination,
flushes it, then replaces the destination in one move. Cancellation and write
failures remove only that temporary file. Saving shares the session's engine
queue and rejects duplicate requests. No dependency or preference was added.
Actual save-dialog input and overwrite confirmation still need a physical
check.

## V19: Presentation mode (2026-10-18)

View > Presentation mode, or Ctrl+Alt+P, puts the current document on a black
canvas with the title bar, rail and side panels hidden. Pages fit the window.
Equal-sized pages keep their spread setting; mixed sizes use single pages,
as in the bundled viewer. The normal reading zoom, layout and side panels
come back on exit, at the page reached during the presentation. Entering
from an already full-screen window returns to that same window state.

Escape, F11 and the Exit button leave presentation mode. The small page
controls hide after three idle seconds, unless a control has keyboard focus.
The usual page-turn keys work, along with click/Shift+click, the wheel and
single-finger swipes. Internal links still navigate, while other page clicks
advance instead of opening a browser. Reading zoom, rotation, selection and
hand dragging stay inactive until exit. Find closes with its query retained;
selected text clears on entry, matching the bundled viewer.

Changing or closing the tab ends the presentation. Old link replies cannot
change a later page or a restored reading view. A display change that makes
the old continuous layout too large falls back to fitted single pages and
explains why. PDF pages keep their current night filter, and printing is
unchanged. No dependency or stored preference was added.

## V18: Scroll and spread layouts (2026-10-18)

View > Scrolling offers Vertical, Horizontal, Wrapped and Page. View > Spreads
pairs odd pages (1-2, 3-4) or even pages (a single cover, then 2-3, 4-5), or
keeps pages separate. Each tab starts with vertical scrolling and no spread;
its choices live with that tab. Changing layouts retains the current page,
selected text, Find matches, rotation, filters and numeric zoom. Fitted presets
use the combined spread size, including the fixed DIP gap between its pages.

Layout uses ordered rows, or ordered columns in horizontal mode. A row's
height comes from its tallest page. Wrapped rows pack whole spreads, center
within the viewport and reflow on resize even at a numeric zoom. Visibility
uses the scrolling axis; hit testing and zoom anchors use both coordinates.
Conservative ranges include complete rows/spreads and nearby pages, keeping
render scheduling bounded without scanning the entire document on a scroll.

Page mode exposes only the current page or spread to rendering and prefetch.
Other pages retain their raster dimensions for late replies and cached pixels.
Page-number edits, thumbnails, Find and PDF destinations can change the active
spread. Layout changes resize/invalidate the surface before committing, cancel
active drags and reject old link replies. A layout beyond the surface limit
keeps the readable layout and reports a notice. No dependency was added.

Next/Previous advances across a fitting spread or wrapped row; horizontally it
advances one page. A spread wider than the viewport advances one page so its
other page remains reachable. Horizontal mode maps an ordinary wheel to the
horizontal axis. Page mode turns at vertical wheel boundaries with a short
repeat guard; zoomed pages can scroll within the spread. Native touch remains
scrolling within the current surface, with reader buttons for changing pages.
Physical wheel, keyboard, touch and pointer routing remain owner checks.

## V17: Hand and text-selection tools (2026-10-18)

View > Cursor tool offers Text selection (S) and Hand tool (H), matching the
bundled viewer's unmodified shortcuts outside text fields. Text selection is
the default for each new tab; tool choice stays with that tab for its lifetime.
It does not add a stored preference. Switching tools preserves selected text.

Hand mode captures a left mouse or pen press in the document viewport,
including its gutters. Motion beyond three DIPs starts a drag. Every update
uses the original viewport point and scroll offset, clamped to the current
extent; hitting an edge cannot accumulate drift. Zoom and rotation stay fixed.
Touch uses native scrolling. Scrollbars and their buttons retain native input.
The resting cursor uses Windows Hand and a drag uses SizeAll, since the native
system cursor set has no grab/grabbing pair. Leaving the viewport restores the
normal inherited cursor.

An unmoved hand tap resolves a PDF link once. A drag suppresses link activation,
including when it starts over a link. This is a deliberate difference from the
HTML viewer, which can exclude link elements before starting a drag: this
raster viewer resolves PDF links asynchronously. Beginning a new gesture
invalidates older link replies so they cannot navigate during a drag or clear
a newer pending lookup. Touch link taps retain their normal path.

Capture loss, missing left button, focus loss, deactivation, hidden/closed tabs,
zoom, rotation, Find and document properties stop the gesture. Text selection
keeps its existing pointer handling when selected. Helper tests cover threshold,
clamping, anchor reversal and invalid inputs; the native probe checks both scroll
axes, selection retention, menus, per-tab tools, links and cancellation. Physical
pointer/capture and H/S keyboard routing remain owner checks. No dependency was
added.

## V16: Document properties (2026-10-18)

File > Document properties opens a native, scrollable dialog for the active
document. Its selectable values cover the bundled viewer's Info metadata,
file size, PDF version, page count, current page size and fast-web-view flag.
The current page number explains mixed-page sizes; encryption is also shown.
Page size follows the crop, intrinsic rotation and current view rotation,
without changing the PDF. Dimensions use millimetres and familiar paper names.

PDFium metadata reads run lazily on the engine thread, then cache per tab.
Size comes from the loaded byte snapshot, so renaming/deleting the source
cannot change the reported document. Each metadata field is capped at 128 KiB
of UTF-16 data; oversized fields or inconsistent reads leave a note while
other details remain available.
The availability API supplies linearization when it can determine it. Its
32-bit file-length limit and sub-1-KiB unknown result display as Unavailable.
No file is reopened or locked and no dependency was added.

Dates use an unambiguous year-month-day form and retain their explicit UTC
offset. Missing timezones stay unspecified; invalid dates say Unavailable.
Missing metadata uses an em dash. This avoids silently changing or guessing
an author-supplied timestamp. Malformed calendar dates and offsets are tested.

A failed read offers Retry. Duplicate opens keep one dialog, tab changes hide
it, and closing invalidates queued results. A late reply may populate a live
tab's cache but cannot reopen its dialog. The dialog uses shared WinUI fills
and follows Dark/Obsidian, including a theme switch while it is open.

## Obsidian appearance (2026-10-18)

I wanted a true-black option that lets the app fade into the background.
It's called Obsidian (AMOLED), under Settings > General > Theme. Dark stays
as the default, with NightPDF's original greys.

Obsidian uses solid #000000 for the reader canvas and the app's resting
surfaces, including Settings, side panels, menus and dialogs. Text, icons,
borders and hover, focus and selected states still need enough contrast to
be easy to read and use. The music pane uses the same black background,
without acrylic or the artwork tint. Switching back to Dark brings its
current artwork tint back.

PDF pages keep their colours and the chosen night filter; album art and
filter previews keep theirs too. Print output is unchanged.

The choice saves immediately and updates open windows as well as new ones.
If saving fails, the app keeps the previous theme. The optional
`general.Theme` setting accepts `dark` or `obsidian`; a missing or invalid
value falls back to Dark without resetting anything else. Keeping Dark also
keeps the original settings file shape. No dependency was added.

## Settings appearance (2026-10-18)

The owner asked for a quieter, more refined settings window. General,
Shortcuts and About now have a tonal icon sidebar, page headings and shared
button styles. The default window is 840 by 640 DIPs. The three stored
section keys and the initial About page stay the same; this changes their
presentation, not the settings format or defaults.

General uses aligned cards with short descriptions and the existing native
checkboxes. Shortcut rows share column widths and button heights; an empty
slot says Not set. Assigned shortcuts keep a quiet clear icon visible,
instead of hiding the old red X until hover. Narrow windows trim the chord
with an ellipsis and retain its full value in a tooltip. All pages can scroll
when shortened. About shows the app version first; selectable diagnostic
versions sit inside Build details.

The capture panel, Cancel and Save use the same restrained surfaces. Capture
still requires one ordinary key with a modifier, and saving, clearing, focus
return, failed-write recovery and owner-window lifetime use the existing
handlers. No dependency was added. The appearance code is separate from the
window lifetime and persistence code.

## V15: View rotation (2026-10-18)

Rotate Clockwise and Rotate Counterclockwise are in View, with R and Shift+R
outside text fields. Like the bundled viewer, this rotates every page in the
current tab, reapplies a named zoom mode and restores the current page.
Numeric zoom stays fixed. A different tab keeps its own rotation; reopening
a document starts from its PDF rotation. This does not edit or save pages.

I keep PDFium's page sizes and text geometry unchanged. The view swaps sizes
at odd quarter turns, rotates highlight rectangles and maps text/link hit
coordinates back before querying the engine. Link and outline destinations
rotate their optional axes and fit rectangles too. Character selections and
Find results survive rotation. Raster keys already include extra quarter
turns; I clear GPU copies and rebuild the thumbnail strip even for a half
turn or a square page. Late link replies from the old view are discarded.

The new layout, thumbnail geometry and surface bounds are prepared before
changing the view. A document that exceeds the existing surface limit keeps
its previous orientation. Printing uses the original page sizes and PDF
rotation, matching the bundled viewer's separate print-page overview.
All PDFium work remains on the engine thread; no dependency was added.

## I12: Native print preview (2026-10-17)

This is the approved third part of the music/print repair. NightPDF hands
printing to Electron's webContents.print; the first native path used the
Win32 dialog and couldn't provide a preview. I now use the reader HWND with
IPrintManagerInterop and supply my own IPrintDocumentPageSource. The old
dialog remains a fallback for unsupported modern APIs, with the chosen path
logged. Cancel and ordinary print failures do not open another dialog.

Preview publishes the document's page count again when options change, and
keeps document page numbers intact so custom ranges are validated against
the whole PDF. Only requested pages render. A newer page, changed options or
a closed tab discards old work; repeated requests for the same page work too.
The preview is an unfiltered PDFium print raster in a DXGI surface, never a
capture of the dark viewer. Each page fits and centres inside the printer's
imageable area; PDFium's intrinsic rotation remains in effect.

The final job applies custom ranges once and renders at the reported printer
DPI in tiles of at most 2048 square pixels. Direct2D records each paper page
into a command list for ID2D1PrintControl. The system-owned job target carries
copies and printer settings, so I submit each logical page once. This is
raster output; the current PDFium path doesn't export Direct2D vectors.
Workers have their own MTA and GPU resources; all PDFium calls still use the
one engine thread. Both paths keep P18's original colours and print annotations.

Cancellation skips queued tiles and drops in-flight results. It does not
interrupt a blocking PDFium tile render or a printer call. The reader remains
busy until both task submission and the asynchronous writer finish. Closing
the tab cancels its source before releasing the PDF session. No dependency
or project optimization setting changes are needed.

The native checks substitute print UI completion and exercise reader state.
The source checks use DXGI surfaces and real Direct2D output to an in-memory
XPS package, without contacting a printer. An opt-in SablePrintUiTest request
also reached the real system preview, supplied a DXGI page and closed after
canceling; it submits no job. Print to PDF, ranges, landscape, copies and
driver cancellation still need physical checks.

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

## Rounded music flyout (2026-10-17)

This is the approved second part of the music/print repair batch. The music
surface is 340 px wide with 12 px corners, 16 px inset content and a raised
shadow. In-app acrylic uses the artwork tint at 0.62 opacity and 0.92
luminosity opacity, with a subtle same-hue gradient. The existing brightness
clamp stays in helpers; missing art keeps NightPDF's #2d2d2d fallback.

The title bar button opens relative to the window content, below the whole
title bar with an 8 px gap. The position is clamped inside the window; the
button's right edge is the preferred alignment. The transport row uses
Fluent glyphs, with a white circular play button and quiet shuffle/repeat
toggles. Unsupported toggles are hidden. Native slider input and automation
remain available through its horizontal template parts.

Opening fades and slides by 8 px; closing is shorter. Artwork crossfades
and tint changes last 300 ms. These effects read Windows' Animation effects
setting; the no-animation path applies the final state immediately. Artwork
completion still checks the current generation before touching the surface.

I use 4 px corners for the preset and shared controls, 8 px for the
configurator and right-rail panels, and 12 px for the music flyout. This is
an approved visual departure from NightPDF's preset/configurator radii.
The PDF page surface is unchanged. Printing remains a separate I12 step.

The native music check can save bright, dark and missing-art captures using
synthetic artwork when SableMusicCaptureDirectory names an output directory.
That capture code is only part of the surface-test build; it neither connects
to nor controls a real player.

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

- I removed the `EASTER_EGG` switch, its Shift+Alt+T notification and the
  marker comments that went with it.
- Shift+Alt+M shows the MORTY! alert inside the reader.
- Shift+Alt+T opens Night Run in a tab. The run pauses when I switch tabs
  or the window loses focus.
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

## Music tracking repair (2026-10-17)

The old policy ranked Windows' current session above playback status and
kept the previous target on ties. A paused current session could beat a
playing one, and a second Spotify session changing tracks was not observed.
Reopening the flyout only redrew the cached snapshot. Seeking released its
thumb lock on a timer before the player had confirmed the new position.
These defects explain how stale state can persist; identifying the exact
abandoned session in the reported run still requires live player output.

Selection now prefers Playing, then the most recent media/playback activity.
Windows current and the previous target only break exact ties. Candidate
media/playback subscriptions remain connected while the pane is closed;
the followed session has three separate auto-revoked handlers. Opening the
pane rereads the session list, media and timeline. Timeline ticks do not
change activity priority. Broker calls run in the background; the dispatcher
receives strings, numbers and bounded artwork bytes, with ordered replies.
Session/request generations and an event epoch reject late metadata/artwork.

Position advances from LastUpdatedTime using PlaybackRate, clamps to the
reported bounds and freezes when paused. The pane ticks at 250 ms only while
open and playing. Track changes reject the previous timeline sample until
a newer one arrives. A seek keeps its target through busy and artwork updates
until a fresh timeline is near the target, a failure occurs, or five seconds
elapse. A track change clears it. Debug builds log every enumerated session,
Windows current, the selected identity/status and metadata changes under
LocalAppData/Sable/logs, even without DEBUG set. No periodic broker polling
was added. Real Spotify/Apple Music behavior remains a physical check.

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

## Text selection and markup bands (2026-10-26)

The approved reader changes use one line-band calculation for selection and
saved Highlight, Underline and StrikeOut quads. PDFium's rectangle list splits
at font changes, which left uneven patches inside a single line. I keep the
whole line's cross-axis bounds for partial selections, with separate runs for
columns, baselines and text directions. Cropped and rotated pages map those
same bands back to PDF coordinates. Skewed runs retain individual bounds.

## Annotation removal (2026-10-26)

The approved delete action captures page, index, subtype and a document
revision. Removing an annotation advances the revision and drops index-based
edit caches. Stale editors and menus cannot redirect to a shifted index.
I refuse protected, shared or threaded annotation removal until those
relationships can be preserved. Removing comment text keeps the markup,
its appearance and its popup references, using the existing edit checks.

## Sidebar notes and highlight colors (2026-10-26)

The approved note presentation keeps Text contents in Comments and hides the
page icon, including print output. New and edited notes store NoView without
Print; existing notes are masked only during rendering and their original
flags are restored. The five highlight choices use the supplied reference:
yellow #FFF066, green #7DF066, light blue #8FDEF9, pink #F799D1 and red #EB4949.
Line markup keeps its existing black appearance. Saving copies follows the
same note-visibility policy without altering the opened document's flags.

## Empty notes and markup comments (2026-10-26)

The approved editor treats empty existing Text contents as Delete and labels
that action explicitly. It keeps the draft when removal is refused. Markup
has a separate Remove comment action, so clearing its text keeps the visible
highlight or line. Both actions retain the editor's concrete revision.

## Selecting page annotations (2026-10-26)

The approved page action hits the actual markup quads and shows a dashed
outline with Edit comment and Delete. A page selection clears the sidebar
row; choosing a sidebar row clears the page selection. Delete stays out of
text-entry controls and repeated keys. Late hit replies recheck interaction,
visibility, layout and session state before opening a menu.

## Comments toolbar and rows (2026-10-26)

The approved Comments refresh puts count, save state and the primary Add note
and Save actions together. Placement stays in More actions. Readable type
labels, native icons and short previews replace raw subtype-only rows;
contextual Edit/Delete controls and row menus retain concrete targets.

## Selection action menus (2026-10-26)

The approved selection menu opens after a completed mouse drag or Select All,
including when text extraction finishes later. Canceled drags do not open it.
The highlight submenu shows the five reference colors. Right-clicking page
markup selects that annotation first; otherwise it keeps the text menu.

## Reusing deletion proofs (2026-10-26)

I cache the pages with popup or reply references during the document's first
serialization check, and prove imported dictionary identities together for
an edited page. Safe removal shifts the cached indices and counts instead
of discarding them. It cannot introduce sharing or references. Repeated
removal revisits the edited page and existing relationship pages, keeping
shared, protected and threaded targets refused.

## Form widget metadata (2026-10-26)

The first E01 step reads page widgets on the engine thread and returns plain
name/value/type/flags snapshots, raw PDF bounds and annotation indices with
the live document revision. It does not add editing controls or run actions.

I inspect a scratch document because PDFium's form setup can normalize field
dictionaries and regenerate appearances. The live document stays outside
that environment. Current bytes come through the existing serialization
guards, capped at 128 MiB. A copy that cannot serialize or reopen without a
password returns failure; encrypted-form inspection needs a later step.
Non-form pages return an empty list; XFA remains unsupported.
Choice options and multi-select selections need a later step; multi-select
fields mark the snapshot limited rather than claiming one complete value.

The scan examines at most 4,096 page annotations. Individual UTF-16 buffers
are capped at 64 KiB and their page total at 1 MiB; incomplete metadata is
marked limited. Those caps bound returned data, not PDFium's internal form
tree or appearance allocations. The page hooks and form environment close
before the scratch document, with all handles staying on the engine thread.

## Form metadata session requests (2026-10-26)

Form reads use their own session generation. New reads and cancelForms
supersede pending form reads without canceling rendering, Find or annotation
reads. Accepted callbacks run once on the engine thread outside session
locks, including failure and cancellation. Callers still guard completed
replies against a later cancellation or a changed view/session.

I retain one immutable page snapshot, keyed by page and annotation revision,
within the existing Document metadata limits. Switching pages replaces it;
failed reads do not displace it. Returned snapshots can outlive the cache.
Every annotation edit path clears it before the attempted mutation, even
when refused or unchanged, because revision alone does not cover all edits.
Close discards the cache with the document on the engine thread. This step
adds the request/cache path; form controls and field editing follow later.

## Form field display geometry (2026-10-26)

Field snapshots now carry annotation flags separately from form-field flags
and a clipped visible rectangle in displayed-page fractions. The conversion
uses the scratch page's media/crop intersection and intrinsic rotation;
viewer rotation, zoom and display scale remain outside the cached snapshot.
Raw PDF bounds stay intact, including partial and fully off-page fields.

I intersect rectangles before integer conversion so remote coordinates
cannot overflow device points. Hidden, Invisible and NoView fields keep
their metadata without exposing a visible footprint. Malformed rectangles,
unusable page boxes and failed conversions mark the snapshot limited;
ordinary off-page placement does not. Other annotation flags are retained
for later native-control behavior. The clipped rectangle describes the
visible footprint; an inline editor also needs the raw bounds for layout.

## Plain text form transactions (2026-10-27)

I edit plain single-line text fields on an isolated copy, commit through
PDFium focus loss, and require the complete requested value before and
after serialization/reopening. A truncated or refused edit leaves live
bytes, dirty state and revision intact. Existing unsaved notes survive a
successful replacement. I rebuild bookmark handles before replacing the
document, clear identity proofs and advance the annotation revision.

The pinned signature counter misses nested fields. I check a forced full
rewrite for canonical /Sig or /DocMDP tokens before form setup, including
names originally escaped or stored in object streams. Matching unrelated
data can conservatively refuse a safe document.

Filling requires annotation permission, or the separate fill permission
for security revisions 3 and later. I require one control per field until
appearance updates across unloaded pages are supported. Signed forms,
protected widgets, inherited fields, specialized text flags, actions and
invalid text are refused. Existing scratch-reopen and safe-serialization
limits still apply;
password-protected reopening remains unfinished. This is the engine write
path; session requests, native controls and form drawing follow separately.

## Queued form writes (2026-10-27)

Session form writes own their strings and require an explicit nonzero
snapshot revision. They retain queue order independently of form reads and
cancelForms. Close can cancel while the isolated transaction is running;
an accepted replacement keeps its outcome if close arrives afterward.
Replies distinguish rejection, accepted unchanged text and changed text.

Every attempted write clears the cached field snapshot. Accepted replies
carry the document revision and synchronize the session's annotation
revision so later note edits use current targets. Callbacks run once on the
engine thread outside session locks and can queue more work. Open results
now expose fill permission independently of annotation/copy permission;
that permission alone does not promise a supported editable field.

## Widget raster output (2026-10-27)

Screen/thumbnail and print rasters draw widgets after the base page using
the same geometry. I retain one isolated form document, bounded by the
existing 128 MiB copy limit, and refresh it after annotation edits,
widget/index revisions or edit-safety invalidation. AcroForm base rendering
also uses the copy because even the page renderer generates appearances.

The pinned form renderer ignores print mode and signature widgets bypass
part of its visibility check. I reapply live widget flags to the copy for
every draw, mapping excluded widgets to Hidden and allowing NoView|Print
only on print. Optional-content widgets stay out of print until layer print
usage is supported. No-view/hidden text-note behavior stays unchanged.

Legacy form printing transfers bounded 4096-pixel raster tiles to its HDC;
other PDFs keep the vector path. Widget scans are capped at 4096 annotations
per page. Copies that cannot serialize/reopen, including password-protected
forms, retain the previous base-page output without a widget overlay.
That fallback keeps the existing renderer and its appearance side effects.

## Native text-field editor (2026-10-27)

A page tap checks field snapshots before the existing comment/link path.
I reverse the viewer rotation and discard replies after session, tab,
layout, zoom, scroll or edit changes. Plain single-line fields use a native
dialog; password, readonly and specialized flags are excluded. NoZoom,
NoRotate and ToggleNoView widgets wait for matching hit geometry.

The editor shares the existing edit/dialog/dirty guards. Rejected or
unqueued writes retain the draft; only accepted replies close it. Changed
values refresh page/thumbnail caches and join Save a Copy and unsaved-close
handling. Form changes use generic save wording without renaming the
existing note APIs. The transaction still validates structural support;
signed, inherited or otherwise unsupported fields can refuse an edit.
Viewer shortcuts wait while an edit dialog is open; native text keys keep
their normal behavior.

## Checkbox form transactions (2026-10-27)

I reuse the isolated text transaction for single terminal checkboxes and
keep their checked state separate from the field value in snapshots.
PDFium's export value supplies the on name, including custom names; I
require matching name-valued V/AS entries and verify the value and checked
state after saving and reopening before replacing the live document.

Checkboxes keep the text transaction's permission, signature, action,
shared-control and cancellation guards. Inherited fields, Opt arrays and
inconsistent states stay refused. I require nonempty printable ASCII checked
appearance content, allowing PDF whitespace, before leaving or accepting
the on state. The pinned API can return compressed bytes for an empty
stream. I conservatively refuse binary appearance content, including
unescaped non-ASCII glyph strings and inline image data, and use the
existing 64 KiB string limit. This does not validate drawing operators
inside a text stream. Session requests and native checkbox controls follow
separately.

## Queued checkbox writes (2026-10-28)

Checkbox requests share the text-write queue and result handling. I own the
expected field value and requested checked state, require a nonzero snapshot
revision, and clear the cached form snapshot before each attempt. A false
checked state still takes the checkbox path; it is not a text request.

Accepted writes synchronize the session revision for later note edits.
Unchanged writes keep the revision, refused writes keep the document, and
form-read cancellation does not cancel queued writes. Close retains an
already accepted outcome and cancels requests that have not started.
The engine's structural and appearance restrictions remain unchanged.

## Native checkbox editor (2026-10-28)

Page taps offer a two-state checkbox through the existing field dialog.
I carry the raw snapshot value and revision into Apply, while the native
control holds only the checked state. Refused or unqueued writes retain
the choice and restore the control; accepted no-ops close without marking
the document dirty. Changed values use the existing page/thumbnail refresh,
save-copy and unsaved-close handling.

Checkboxes retain the text editor's visibility, flag, geometry, modal and
stale-request guards. Structural and checked-appearance support is still
validated by the engine, so a field offered by the dialog can be refused.

## Button control metadata (2026-10-28)

Checkbox and radio snapshots carry PDFium's group count, group index and
export value separately from the checked state and shared field value.
These are owned values for the snapshot revision, not a persistent group
identity or proof that every member has been read. Other field types have
no control metadata. Invalid group coordinates or failed export reads mark
the page limited and omit the control metadata; valid exports share the
existing 64 KiB string and 1 MiB aggregate UTF-16 budgets. Groups above
4096 controls are marked limited before index lookup or export reads.

PDFium reports radio values from checked controls. With Opt arrays, the
stored V may be an index while the reported field value and control export
are labels. Matching exports alone cannot identify the selected control.
The public linked-annotation API cannot inspect an ordinary parent field
dictionary without Type Annot, so these reads do not establish parent
action safety. Radio writes still need a separate membership and safety
proof; this step does not change the writer or add radio controls.

## Radio group transactions (2026-10-28)

Radio selections use the same isolated transaction as text fields and
checkboxes. A request names one control by its annotation index and carries
the snapshot's shared value, group position, group size and export. Older
revisions, a different size or position, or a changed export are refused.
Only selecting is supported; the usual radio UI can't clear a group either.

I write a group only when every control PDFium counts for the field is a
widget on that page, listed once, with a unique nonempty export. Groups
spanning pages, or with a member missing from the page's annotations, are
refused rather than proven. Duplicate exports are refused because the
shared value can't say which control it selects, and only the Radio,
NoToggleToOff, Required and NoExport flags are accepted, so unison and
read-only groups stay refused.

Members must be plain widgets without their own actions or hidden, no-view,
read-only or locked flags. Each AS is a name that agrees with its checked
state, at most one member is checked, and a checked member passes the
checkbox appearance check. Kids under a parent can't carry field keys (T,
FT, Ff, V, DV, Opt, Kids). A radio without a parent follows the checkbox
rules and refuses Opt.

The linked-annotation API can't open an ordinary parent field, but the form
API reports inherited field scripts, so any keystroke, format, validate or
calculate JavaScript refuses the write. PDFium also merges a second parent
with the same name into the first field, and that parent's scripts never
reach the form API, so radio writes refuse documents whose canonical copy
contains any script. Such a merged group can still be written without
scripts; the second parent's own V, Opt and flags aren't examined, which
stays a limit of the public API. The parent's V isn't readable
directly; the current choice comes from member AS states, and the write
replaces V. With Opt arrays the stored V can be an option index while
snapshots show labels, so I compare labels together with positions. After
the write, and again after saving and
reopening, every member's checked state, position, export and the shared
value must match the request before the live document is replaced. The
tests also find the new name in the saved parent V.
Session requests and native radio controls follow separately.

## Queued radio selections (2026-10-28)

I queue radio selections with owned copies of the snapshot's shared value,
group position, group size and export. The document writer still proves
membership and safety; accepting a request into the queue doesn't promise
an editable group. Radio requests select one control and cannot clear it.

Selections share the existing form-write queue, cache invalidation and
revision handling. Refused requests keep the document, accepted no-ops
keep the revision, and form-read cancellation doesn't cancel writes.
Callbacks run once on the engine thread outside session locks and may
queue another selection. Close cancels pending work but retains an already
accepted outcome. Native radio controls follow separately.

## Native radio option editor (2026-10-28)

A page tap opens the shared field dialog for that radio option. I use a
native radio button with the saved checked state; an unselected option
enables Apply only after selection. The dialog selects the tapped control,
and cannot clear a group. Applying an already selected option is a no-op.

The request keeps the snapshot value, revision and control metadata. The
engine still proves complete membership and safe appearances, so an offered
option can be refused. Refused or unqueued writes retain the selection;
accepted changes share page/thumbnail refresh, save-copy and unsaved-close
handling. Text/checkbox visibility, geometry and modal guards still apply.

## Choice option snapshots (2026-10-28)

I keep each combo/list option's position, display label and SDK-reported
selection in an owned snapshot. The field value stays separate; the pinned
option API has no choice-export getter. Duplicate exports can report more
than one selected position without valid I indices, and editable combos can
hold text outside the option list. I don't infer a unique choice from either.

Missing or non-array Opt entries report zero options, while malformed option
labels can look like valid empty strings. A complete SDK scan doesn't prove
the raw options or parent field are safe to edit. Multi-select fields still
mark the page limited even though their reported selections are retained.

I read at most 4096 options across a page. Labels share the existing 64 KiB
per-string and 1 MiB aggregate UTF-16 budgets with names, values and button
exports. An incomplete choice scan omits its whole option vector and marks
the page limited; a successful empty list stays distinct from an omitted
list. Cancellation discards the snapshot, and all reads use the isolated
document on the engine thread. Choice writes and native controls follow.

## Single-choice transactions (2026-10-28)

I select one option in a plain terminal combo/list through the isolated
form transaction. The snapshot's name, value, flags, option positions,
labels and selection states must still match. I allow at most 256 options
with nonempty labels, Required/NoExport and the combo flag. Editable,
multi-select, sorted, inherited/shared, protected and action-bearing fields
stay refused, along with signatures and documents containing any script.
Choice writes also refuse canonical A/AA name tokens anywhere in the PDF:
a same-named non-widget field can hide a ResetForm action from the widget
while reporting one control. The conservative token scan can also refuse
matching content in streams/strings, and ordinary link actions are excluded.

The field needs its own Opt array and Ch type. An existing V must be a
string matching the reported value, with one unambiguous selection; an
empty value may have no selection. The public SDK still cannot prove every
unused option structurally valid. Blank labels are conservatively refused
because malformed entries can produce the same report.

I select by position and commit through focus loss. The resulting export
must be nonempty valid text in V, with only the requested position selected.
Saved/reopened values, labels and all selection states must match before
replacing the live document. Duplicate exports can work with valid indices;
moving between them is a change even when V stays the same. An already
selected option is a no-op. Clearing stays unsupported; session requests
and native controls follow separately.

## Action guards for all form writes (2026-10-28)

I apply the choice transaction's canonical action/script guard to text,
checkbox and radio writes too. A terminal field without a widget can
register first; a same-named widget then reports one control but inherits
the earlier field's actions. ResetForm can run during focus commit without
JavaScript or an action key on the target widget.

All form writes now refuse A/AA tokens and script material before starting
form interaction, including no-op requests. The scan recognizes name
endings, so AcroForm, AP and AS remain usable. Ordinary link actions and
matching content in strings/streams can also cause a conservative refusal.
The existing dialog keeps a refused field draft available. Action-free
merged fields still follow the earlier writer restrictions; the stricter
checkbox value check can refuse an edit while accepting its no-op.

## Queued single-choice writes (2026-10-28)

I keep the whole choice-field snapshot in the queued request, including its
value, option labels, positions and selection states. The chosen position
and explicit revision travel with it. The engine checks that snapshot again
before selecting through the existing isolated transaction.

Choice requests share form-write ordering, cache invalidation, annotation
revision updates and close handling. Form reads can be canceled without
canceling the write. Refused requests and no-ops use the same result as the
earlier field writers; native combo/list controls follow separately.

The session tests hit MSVC's object section limit with the extra callback
cases. I enable /bigobj for session_test.cpp alone so the rest of the build
keeps its existing compiler options.

## Native single-choice field controls (2026-10-28)

I use a ComboBox for plain combo fields and a ListBox for single-choice
lists in the shared field dialog. Labels stay in PDF option order; the
selected position travels with the original snapshot because labels and
exports can repeat. Apply stays disabled until an option is selected.

The selector stays disabled while its request is queued. Refused writes
keep the chosen position and restore the controls; accepted no-ops leave
the savepoint alone. Apply, Cancel and Save a Copy use the existing field
flow. Editable/multi-select fields and the earlier writer restrictions
remain outside this step.

Equal boxed labels hung the UI thread during a desktop automation walk of
the open combo popup. The hot stack stayed in WinUI/UI Automation. I give
each option its own ComboBoxItem or ListBoxItem, keeping duplicate labels
unchanged while separating their native identities. The native checks also
open and close the real combo popup across dispatcher turns.
// see: https://github.com/microsoft/microsoft-ui-xaml/issues/11226

## Plain multiline form transactions (2026-10-29)

I allow the Multiline flag on plain terminal text fields through the same
isolated transaction. Permissions, signatures, actions, inherited/shared
fields, protected widgets and other specialized text flags keep their
earlier restrictions. The page editor follows as a separate step.

Expected values stay exact snapshots. An unchanged raw value keeps its
original line endings; changed replacements use the pinned editor's CRLF
paragraphs. CR, LF and CRLF inputs each describe one break. The normalized
value still has to fit the snapshot limit and survive focus commit and
save/reopen unchanged. MaxLen truncation refuses the isolated copy rather
than accepting partial text. Tabs keep the earlier full-value check too.

## Native multiline field editor (2026-10-29)

I open plain multiline text fields in the existing field dialog with return
input, wrapping and a bounded height. Other text flags keep their earlier
restrictions. Apply still compares the opened control value with its initial
value, so an unchanged editor sends the exact PDF snapshot even if WinUI
normalizes its line endings. The same queued transaction handles changed
paragraphs, refusals and savepoints.

## Plain comb text transactions (2026-10-29)

I allow the Comb flag on terminal text fields with one control and a numeric
MaxLen reported as an integral cell count from 1 to 32767. Missing, invalid
or unbounded counts refuse before no-op handling. Multiline, password,
file-select, rich text, other specialized flags, inheritance and shared
controls retain their earlier restrictions.

The isolated focus-commit transaction still requires the complete requested
value before and after save/reopen. It refuses truncation and transformed
text rather than committing a partial code. Unicode values are checked as
stored text; native input and saved cell appearances follow separately.

## Native comb field editing (2026-10-29)

I use the existing single-line field dialog for plain comb text. Apply goes
through the same queued transaction and retains refused drafts; the engine
validates the cell count and complete value. Comb combined with Multiline
stays outside the page gate. The dialog does not imitate the PDF's boxes;
the saved page appearance remains PDFium's comb layout.

## Plain inherited text transactions (2026-10-29)

I allow a plain single-line text widget to inherit its field data when the
SDK reports one control and identifies that actual widget at control index
zero. The child cannot carry field attributes or an annotation parent, and
the page snapshot must contain its full field name only once. Effective
flags are limited to Required and NoExport; inherited Multiline and Comb
stay refused, along with shared controls and the earlier safety guards.

I recheck ownership on an isolated saved copy, then require the reopened
name, field flags, annotation flags and complete value to match. Parent
MaxLen still goes through the full-value check, so truncation refuses the
transaction. The public API resolves field ownership but cannot inspect an
ordinary parent's raw dictionary or prove the whole parent tree well formed.
This allowance follows that SDK model; it is not a raw tree validator.

## Inherited multiline text transactions (2026-10-29)

I allow Multiline through the same inherited single-widget guard. Child
field attributes, shared controls, actions and conflicting flags still
refuse. Raw no-ops keep their original line endings; changed paragraphs use
the existing CRLF transaction and complete saved-value check. Parent MaxLen
truncation leaves the live document unchanged and allows a corrected retry.

Inherited Comb stays refused: its numeric cell count belongs to the parent,
which the annotation number API cannot inspect. The SDK ownership checks
still do not prove the raw parent tree well formed. Native mixed-field and
desktop paragraph checks follow separately.

## Inherited checkbox value proof (2026-10-30)

I keep inherited checkbox writes refused even when the SDK reports one
control with matching export, checked state and value. That value comes
from control appearances, so it can hide a parent's mismatched name-valued,
string-valued or numeric V. An inherited Opt array also changes how the
parent stores its value while leaving the same export visible.

The regressions keep both check states and no-ops refused before and after
save/reopen, with exact bytes and revisions intact. The inherited-text
ownership allowance does not supply the raw parent V/type and Opt proof
needed here; I won't use it to relax the checkbox guard.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_formfield.cpp
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_formcontrol.cpp

## Text fields without spelling checks (2026-10-30)

I allow DoNotSpellCheck on the existing terminal text paths and the bounded
inherited single-line/multiline paths. The bit stays specific to text;
checkboxes, radio groups and choice fields keep their earlier flag guards.
DoNotScroll, conflicting text flags and inherited Comb stay refused.

The same full-value transaction retains raw no-ops, paragraph normalization,
limits, cancellation and saved identity. The pinned field/editor code does
not use this flag to change values. The native editor must disable its
spelling property for flagged text before I call the feature complete.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/constants/form_flags.h
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_textfield.cpp

## Native text spelling settings (2026-10-30)

I open the supported DoNotSpellCheck text fields through the existing native
gate and set IsSpellCheckEnabled to false on their text box. Unflagged fields
keep WinUI's default true. Text prediction keeps its existing setting.
The flag stays text-only, and the queued writer retains its earlier guards.
// see: https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.controls.textbox.isspellcheckenabled?view=windows-app-sdk-1.8

## Choice fields carrying Sort (2026-10-30)

I allow Sort on the existing terminal single-choice combo/list transaction.
Table 230 assigns sorting to writers; readers display the stored Opt order.
I keep each option's index and label, including repeated labels and exports,
instead of reordering them. The pinned combo/list editors do the same.

The field still needs one control, its own Ch type and Opt array, matching
snapshot metadata and an unambiguous selection. Editable, multi-select,
CommitOnSelChange, inherited/shared and action-bearing choices stay refused.
Sort stays out of text and button masks. The saved-copy transaction and
cancellation checks keep their existing bounds; native editing follows.
// see: https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_combobox.cpp
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_listbox.cpp

## Native Sort-bearing choices (2026-10-30)

I open supported Sort-bearing combo/list fields through the existing choice
gate. Their native items keep the PDF's order and separate containers for
repeated labels. Apply still uses the selected option index, so equal
exports do not turn a positional change into a no-op. The same dialog keeps
Cancel, modal commands, saved-copy reopen and earlier refusal behavior.
Sort remains specific to choices; no field list is reordered.

## Clearing a single-selection list (2026-11-02)

I use option -1 to clear an existing terminal single-selection list through
the same isolated focus-loss transaction. It deselects the current option,
requires an empty effective value with no selected options, and proves V/I
absent both before saving and on a separately reopened copy. Raw empty
no-ops keep their original bytes and revision.

I require DV absent: PDFium can report the default when V is removed.
Combos cannot be deselected through this API. Inherited/shared, multi-select,
protected and action-bearing lists keep their existing refusals, as do
ambiguous snapshots and unsupported flags. Native clearing follows.
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_listbox.cpp
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_formfield.cpp
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/public/fpdf_formfill.h

## Native list clearing (2026-11-02)

I add Clear selection to single-list dialogs and allow Apply with no item
selected. Clearing only changes the draft until Apply; Cancel keeps the
live selection. The clear button and selector are disabled while the queued
write runs, then become usable again if it refuses the request.

Lists with DV still refuse clearing through the engine guard. The empty
draft stays available for Cancel or a valid selection retry. Combo dialogs
keep their selection requirement and have no clear action. Saved empty
lists reopen with unchanged Apply accepted through the same transaction.

## Editing a plain text box (2026-11-03)

I accept existing FreeText only when its complete appearance matches a fresh
standard box at the same rectangle: one 12-point Helvetica line, matching
font bytes and rendered glyphs. A familiar resource name alone proves nothing.
The text stays printable ASCII, at most 128 characters, with fixed author and
geometry. Styled, linked, protected and ambiguous boxes stay refused.

I replace Contents and its generated appearance on an isolated copy, keep the
original annotation index, and check the saved copy before swapping documents.
Unchanged Apply still proves the old appearance but leaves live bytes and
revision alone. Cancellation and overflow discard the isolated work.

## Local redraw verification (2026-11-04)

The normal Debug build stalls at parallel worker startup here. A serial
retry reaches the compiler but returns C1902, "Program database manager
mismatch", on unchanged files too. I use a temporary wrapper outside the
repo with one MSBuild worker and /Z7 debug information in the objects.
ARM64 compiler work uses two processes. The project files keep their
normal settings.

Restore uses the cached packages with NuGetAudit=false after its network
lookup fails. I do not change package versions. Both Debug targets build
without warnings this way, and the enabled x64 suite passes. This records
the local verification exception; the normal build invocation and ARM64
execution remain separate checks.

The format hook's Git shell also fails to create its local named object
with access denied. I run scripts/format.ps1 -Check directly and use an
empty hook path only for these commits. The saved hook setting stays as is.

The stall, C1902 and the hook failure only happen inside that restricted
session. From a normal desktop shell the unchanged build script builds in
parallel with PDB debug information and no warnings, and the format hook
runs on commit. The wrapper isn't needed there.

## Ellipse outline creation (2026-11-05)

I store ellipses as Circle annotations, which also allow unequal radii.
The normal appearance has two closed cubic contours with an even-odd
fill: a black outer oval and an empty inset oval. I keep the rectangle's
2-point inset and the same printable flag. Color and width controls stay
marked for later work.

The writer shares the rectangle transaction, bounds and revision guards.
It checks the appearance and annotation count on a reopened copy before
replacing the live document. Cancellation, protected documents and unsafe
relationships keep their earlier refusals. Existing rectangle appearance
commands stay the same.

## New easter eggs (2026-11-08)

I dropped NightPDF's easter egg: the EASTER_EGG variable, its Shift+Alt+T
notification and the "trans rights" marker comments. This is a deliberate
change from the original, which the earlier notification entry ported
exactly.

Shift+Alt+R now sends a "MORTY!" notification, with no variable needed.
Shift+R on its own still rotates the page counter-clockwise like pdf.js, so
the chord needs Alt. Repeat keydown is ignored and it works while a text box
has focus, like the old chord. The notification service, registration and
click activation are unchanged. Shift+Alt+T is kept for a small game tab
that comes in a later commit.

## Morty as an in-app alert (2026-11-08)

Shift+Alt+R never reached Sable on my PC. Another program registers Alt+R
and Shift+Alt+R system-wide (NVIDIA Overlay owns Alt+R), so Windows eats
the key. The shortcut is Shift+Alt+M now; Shift+Alt+T is still free for the
game.

The Windows notification didn't show either. AppNotificationManager's
Register() fails in self-contained unpackaged apps on Windows App SDK 2.5.1
with 0x8007007E, and my logs show it failing on every launch since at least
2026-11-02.
see: https://github.com/microsoft/WindowsAppSDK/issues/6774

So the egg is a green InfoBar inside the window instead: title "MORTY!",
the same line, portal green #97CE4C with black text in every theme. It
closes after four seconds, and pressing again restarts that. Nothing else
used Windows notifications, so I removed the notification service, its
startup registration and the notification activation path.

Each press now adds its own alert on top of the pile instead of restarting
one. An alert stays at least four seconds, and they leave oldest first, at
least 0.7 s apart, so a burst of presses still fades one at a time. Each
fade takes 0.8 s, and the rest slide up to fill the gap. A full pile of
eight lets its oldest go straight away, and the close button removes just
that alert.

## Night Run game logic and sound (2026-11-10)

I kept Night Run separate from the reader. The runner has one jump, an
obstacle at a time and a distance score. A fixed step drives collisions
and the pace picks up until it reaches a cap. A paused run keeps its place.
The jump and crash sounds are synthesized locally and use Windows XAudio2
with the game-effects category. The sounds need no bundled assets or
packages.

## Night Run page (2026-11-10)

The page draws a night track in WinUI and scales it to the tab. Play
starts the run, Pause holds it and Restart resets the score. Space and Up
jump. Mute stops the cue and stays in effect for that open game. A
stalled dispatcher catches up only a few steps so a stall can't run the
whole game in the background.

Shift+Alt+T opens one game tab in the current window and selects it again
on later presses. I pause the run when its tab is hidden or the window
loses activation. Closing the game discards the run. I select the next
PDF tab by item before removing the game, since it can take the game's
old numeric index.

## Paper Tide (2026-11-10)

I changed the track into a three-lane surf game after the Edge-style game
request. Left / Right or A / D move one lane per press; Space / Up still
hop. Rocks and pages approach together in different lanes. Every three
pages earn a shield that absorbs one rock, once for that pass. Restart
clears the pages and shield as well as the distance.

The sea, board, rocks and pages are WinUI shapes, with no copied assets
or new packages. The playfield takes focus when it loads and again after
the tab selection settles, so the opening chord can hand over to the
game's keys.

## Night Run waves, hazards and the bookworm (2026-11-10)

The single rock made every pass the same choice, so the track now carries
rows of three lanes, and harder patterns join in as the distance grows: one
rock, staples right across, rock and ink, an ink field, a two-rock wall,
two walls with neighbouring gaps, and a rock beside two staple lanes. Every
row leaves at least one way through, and a staple row leaves time to land
before it.

Rocks can't be hopped any more; you steer round them, or the shield takes
one. Staples need a hop. Ink only slows the board for a second. A bookworm
follows the board: it creeps closer on its own and faster later on, ink and
a broken shield let it close in, and pages push it back. It ends the run
when it catches up.

The best distance is kept in night-run.json beside config.json rather than
in config.json, so the settings file keeps the shape NightPDF wrote. A
missing or unreadable file counts as 0. The page and ink cues are made in
code like the jump and crash ones.

## Image stamps in Comments (2026-11-10)

I use the same two corners as the outline tools to size a stamp. The
image fits at the top left of that area, keeps its aspect ratio and
appears upright at the rotation used to place it.

The Windows picker starts with PNG and JPEG. I cap the selected file at
32 MiB and each decoded side at 2048 pixels. PDFium writes a printable
Stamp with an image appearance, including PNG transparency. Comments
lists it and offers the guarded Delete action. Save writes a PDF copy.

## Fill & Sign marks in Comments (2026-11-10)

I put the first Fill & Sign commands in Comments under More. A single
click places a check mark or cross with a black 2 pt stroke. The center
moves inward near a page edge so the stroke caps stay inside the crop.
I use printable Ink annotations, so Comments lists them as drawings
and offers the same Delete and Save a Copy path.

## Debug ARM64 build settings (2026-11-10)

The ARM64 compiler ran out of memory mapping the WinUI pch with the
default parallel build. I use one MSBuild worker and two compiler
workers for Debug ARM64, with /Z7 debug info in the object files.
Other configurations default to four MSBuild workers. The build
script's -MaxCpu option overrides the MSBuild worker count.

## Log retention (2026-11-10)

I keep the current process log and the nine newest other sets, with their
rotation backups. Startup leaves active or locked sets for a later run.
Successful saves log at Info and keep the success banner.

## Replacing saved files (2026-11-11)

I use ReplaceFileW for existing destinations so their permissions and
named streams carry over. I keep the old file beside the destination
until the replacement succeeds or I can restore it. New files still
use a write-through move. Before saving on a local drive I clear old
save temps whose process has exited.

## Review decisions (2026-11-10)

Save a copy keeps the exact loaded bytes for any untouched document, even
when it holds notes from other apps. This keeps "Untouched copies remain
exact" (24 Oct) and replaces the copy rule in "Sidebar notes and highlight
colors". When edits force a rewrite, the copy hides only notes Sable made.

I rewrite the zoom steps, the "auto" scale, the wheel tick counting and
the fixed-key comment from a written description of the behavior they
match. Their comments describe that behavior instead of calling them
ports, so pdf.js stays out of the notices. I accept PDFium's license file,
BSD-3-Clause with the Apache-2.0 text, as recorded in
THIRD_PARTY_NOTICES.md. NightPDF shipped Apache-2.0 pdf.js the same way.
This closes D10. The Microsoft runtime terms are reviewed once the unused
packages are gone.

The app references only the Windows App SDK parts it uses: Base,
Foundation, InteractiveExperiences, WinUI, DWrite and Runtime. The AI, ML,
Search, Widgets and WebView2 packages go. The smaller package has to start
on a clean Windows install before a release.

The six Home tool buttons stay visible but disabled, with a "Coming later"
tooltip. Each one is enabled when its tool is wired up.

FEATURE_CHECKLIST.md is tracked and the README links to it. PORTING_STATUS
stays a local working log.

Python files keep four spaces, as .editorconfig says. The tabs rule covers
every other file.

## One write guard for signed PDFs (2026-11-11)

Every edit asks one guard before it touches the document. Notes,
highlights, line markup, comment edits and Delete used to skip the
signature check that shapes, ink, stamps, text boxes and form fields had.
A signed PDF now refuses all of them and says why: "This PDF is signed;
edits would break the signature." Other refusals keep their old text.

PDFium's signature counter reads only the top-level fields, and its public
headers can't reach /Perms, so I still read a full rewrite of the file. I
read it as tokens instead of raw bytes: stream bodies, strings and comments
are skipped and escaped names are decoded. /Sig counts as a field type, a
dictionary type or a bare object, and /DocMDP and /UR3 count anywhere.
/SigFlags, a font named /Signika and compressed data no longer block edits.

An empty signature field still counts as signed, as it did for shapes. The
first edit on a document pays for one rewrite and the answer is kept. A
file over the 128 MiB rewrite limit refuses notes too. A file Sable can't
rewrite at all, such as one with attachments, is left to that refusal.

## Save a copy after the guard (2026-11-11)

Save a copy no longer masks notes at all. An untouched document is copied
byte for byte. A rewrite only happens after edits, and the notes I add or
edit already store NoView without Print, so other apps' notes keep their
flags. Rendering and printing still mask every note icon.

I left the "Choose a writable folder" message alone. With the guard and
the exact copy in place, the tools refuse a file with attachments before
anything changes, so Save a copy can't reach that refusal from the app.

## Windows App SDK parts in the portable ZIP (2026-11-11)

I dropped the metapackage and reference the runtime and the five SDK parts
the reader uses. WinUI's projection still needs the WebView2 metadata, so
I keep that build reference and leave its runtime DLLs out of the ZIP.

## Empty signature fields (2026-11-11)

An unsigned signature field can sit beside notes and editable form fields.
I check its signature contents before refusing an edit. The token reader
follows direct and indirect values and inherited field types, so a blank
field or an unused signature dictionary leaves the document editable.
DocMDP or /Perms still refuses edits, including an empty /Perms dictionary.
Signed files keep the same refusal message. This replaces the empty-field
rule in "One write guard for signed PDFs" above.

## Zoom behavior rewrite (2026-11-11)

I rewrote the zoom calculations from the saved behavior description.
Steps use decimal hundredths to choose their next tenth, and wheel ticks
keep their remainder until the direction changes. Presets, menu values
and fixed-key priority follow the same description. The comments describe
that behavior, and pdf.js stays out of the shipped notices.

## Recovering failed page rasters (2026-11-11)

An empty raster gets one retry for its page, size and rotation. Another
failure waits for an edit or rotation, keeping a bad page from spinning
the render queue. A bitmap upload failure queues one device rebuild for
that raster outside drawing. Cached page images supply the new device.

## Page images during rotation (2026-11-11)

I keep each displayed raster's rotation with its pixels. Rotation draws
those pixels through the new quarter-turn transform until a matching
raster arrives. A half turn also asks for fresh pixels when its size is
unchanged. Notes and other edit refreshes keep their revision checks.

## Canonical copies per revision (2026-11-11)

The write guard and the edit transaction now read the same full rewrite.
I keep it until the document changes, and a verified edit's saved bytes
serve as the copy for the next edit. The first edit pays two rewrites
instead of three and each later edit pays one. Note edits drop the copy
but keep the signature answer, since nothing Sable writes adds a
signature, DocMDP or /Perms. The copy can hold one extra file's worth of
memory until the next change.

## Edit size notice and page comments (2026-11-11)

A file over 128 MiB now says why edits are refused: "This PDF is over
128 MiB, which is too large to edit." I check the size before any
rewrite, and a rewrite that grows past the limit gives the same answer.
After an edit on one page, Comments lists that page again and keeps the
other rows at the new revision. Form edits, highlights and scans that
reached a row or byte limit still list every page.

## Probe runner (2026-11-11)

scripts/probes.ps1 builds the Debug surface check, runs each probe mode
with SableProbeAutoClose and prints its exit code and first error lines,
then rebuilds the normal Debug app. Two forced-fault modes must fail, so a
pass can't come from lost exit codes. Shortcut and the plain surface check
stay manual because they wait for keys and buttons. build.ps1 takes extra
MSBuild properties through -Property for this.

## Fill & Sign text (2026-11-11)

Comments > More > Fill & Sign now offers Place text. It uses the text-box
writer and its dialog, titled "Add text", so the text is black 12 pt
Helvetica with no border, up to 8 lines and 128 characters, placed where
I click. Editing, resizing and Delete work as they do for text boxes.

## Typed signatures (2026-11-11)

Place typed signature asks for a name of up to 64 characters and turns it
into an image stamp. The text-box writer only proves plain 12 pt
Helvetica, so a script look needs an image. DirectWrite draws the name in
black on transparent pixels, four per point, in Segoe Script, then Ink
Free, then italic Segoe UI, whichever Windows has installed; nothing is
bundled. The stamp is 36 pt tall and up to 216 pt wide, centred on the
click and kept on the page. The name lasts until the app closes and is
never written to settings. This links dwrite.lib from the Windows SDK.

## Drawn signatures (2026-11-11)

Place drawn signature opens a 360 by 120 white pad where I sign with the
mouse; Clear empties it. The strokes become one printable Ink annotation,
black and 2 pt like check marks, scaled to fit 150 by 50 pt with their
shape kept, centred on the click and kept on the page. Ink keeps the
signature as vectors and reuses the drawing path for Comments, Delete
and Save a Copy. The pad follows the document's ink limits, and the
drawing lasts until the app closes.

## Page edits in the saved PDF (2026-11-11)

Rotating, deleting and moving pages run through the shared transaction:
the signed-PDF guard, the 128 MiB limit and a scratch copy come first, and
the result is swapped in only after a rewrite reopens with every page
where the edit says. I compare each page's sides, rotation, annotation
count and text length to check that. The transaction now takes the page
count a page edit expects instead of the source's. A file needs the modify
or assemble permission, and the last page can't be deleted. PDFium leaves
form fields and bookmarks that point at a deleted page in place; they
lose their target rather than being removed.

## Inserting, extracting and combining pages (2026-11-11)

Insert copies another PDF's pages into the open document through the same
page-edit transaction, so a signed file refuses it; the source file is
only read and one that needs a password is refused. Extract and Combine
build a new PDF with FPDF_CreateNewDocument and FPDF_ImportPagesByIndex,
then reopen it and check every page before handing the bytes back. They
don't modify anything, so they work on signed files too. Combine refuses
inputs that already add up to more than 128 MiB before loading any of
them, and an output that would grow past it. New PDFs are written only
through the Save a copy picker and the same replace-after-flush file
helper, which refuses to overwrite a source.

## Organize pages (2026-11-12)

Organize lays every page out as a 160 px tile over the reader, with Rotate
left, Rotate right, Delete, Insert from file..., Extract selection... and
Done above them. Ctrl and Shift select several tiles, and dragging moves
them. A drag only becomes an edit when the moved tiles form one block,
which is what FPDF_MovePages does in one step. Each command changes the
open document in memory and marks it unsaved, so the edits are kept
through Save a copy and the usual prompt on close. Inserted pages go
after the last selected tile, or at the end. Extract writes a new PDF
through the Save a copy picker and never over the open file. The tiles
render one at a time, since the session renders one page at a time.
Home's Organize pages button and the pane's entry open it, asking for a
PDF first when none is open.

## Combine files (2026-11-12)

Home's Combine files button and the pane's entry open a Combine files
dialog: Add files..., Remove, Move up and Move down around a list that can
also be dragged into order, then Combine and save... once there are two
or more files. The engine joins them, the Save a copy picker chooses the
new file, and the same replace-after-flush helper writes it before it
opens in a new tab. The new file can't replace one of its inputs, and a
file that needs a password, can't be read or would push the result past
128 MiB is named in the error.

## Export pages (2026-11-12)

Export pages offers PNG images, JPEG images or Text (.txt), all pages or
a range like 1-3, 5, and 72, 150 or 300 dpi. The images are the pages as
they print: original colors, print-visible annotations and no night
filter, since a filtered export would be an improvement of its own. WIC
encodes them through Windows.Graphics.Imaging into memory (JPEG at
quality 0.9), and the Save a copy file helper writes each as "<name> -
page 001.png" into the chosen folder. A page stays within 12000 px a side
and 80 megapixels. Text needs the copy permission, follows PDFium's
reading order and puts a form feed between pages. Signed files export
like any other, since nothing is written to them.

## Unicode text boxes (2026-11-12)

Text boxes now take letters past ASCII: accented Latin, Greek, Cyrillic,
punctuation and symbols, CJK and Hangul. Anything with a line break or
a character past ASCII is drawn as glyph outlines in the appearance,
the way explicit-line boxes already were. Plain one-line ASCII keeps
PDFium's Helvetica appearance and its bytes.

The outlines come from the installed Windows fonts through DirectWrite:
Segoe UI first, then the system fallback for each run, in the user's
locale. Nothing is bundled and no font goes into the PDF, only paths. I
still read each font's OS/2 fsType and refuse a font marked Restricted
License or bitmap-only, since the outlines are its glyph data. I went
to DirectWrite for the outlines rather than loading the font into
PDFium: FPDFText_LoadFont reads only the first face of a collection and
maps characters to CIDs through its generated ToUnicode, and most CJK
fonts on Windows are collections.
// see: https://learn.microsoft.com/en-us/typography/opentype/spec/os2
// see: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_edittext.cpp

Characters are drawn one at a time with no shaping, so right-to-left and
complex scripts (Hebrew, Arabic, Indic including Tamil, Thai), combining
marks, emoji and anything outside the BMP refuse. The dialog composes
typed text to NFC first, so an accent typed as a separate mark still
works. It says which way a refusal went: a script it can't draw, no
installed font for a character, or a font that doesn't allow embedding.
A Unicode box is recognised for editing by drawing it again from its
contents, so a box made with fonts this machine doesn't have reads as
not editable rather than as broken. /Contents keeps the text and DA is
unchanged, so another app that rebuilds the appearance from DA may not
show these characters.

## Editing drawings (2026-11-12)

Clicking a drawing on the page now selects it by its outline box, the way
highlights are picked by their quads. A selected drawing moves with the
mouse: a press inside its
outline (or within 4 DIPs of it) drags the dashed outline, the cursor
turns into the move arrows, and the release writes the move. Esc or a
lost capture drops it, and a press that travels less than 3 DIPs moves
nothing. Edit, from the page menu or the Comments row, opens "Edit
drawing" with Color and Width, each starting at "Keep current" and
offering the Drawing style pens. Delete stays as it was. Check marks,
crosses and drawn signatures are drawings too, so they move and restyle
the same way. Pen input stays untested here.

Only a drawing Sable could have written is edited: its appearance has
to be exactly what addInk writes for its InkList, width and color, and
its Rect has to match. The color is read from the appearance, since
PDFium won't read /C while one exists, and rebuilding the stream proves
the reading. Other keys refuse, except the CA of 1 that SetColor adds.
The old appearance is also rendered on an isolated copy, so an odd BBox
or Matrix can't move what the edit replaces.

The edit happens in place inside the shared transaction, after the
signed-PDF guard: the old appearance goes first so the color and border
can change, then InkList, Rect and the new appearance. The saved copy is
reopened and the drawing's points, width, Rect and appearance checked,
along with every other annotation and its appearances. A drawing that
would end up past the page box refuses. The pen palette moved to
InkPalette.h so Drawing style and the editor share it.

## Moving and resizing stamps (2026-11-12)

A click on an image stamp selects it by its box, like a drawing, and a
drag moves it. Edit opens "Resize stamp" with Width (pt); the height is
shown and follows the width, so the image keeps its shape, and the
image's top-left corner on the turn it was placed at stays put.
Widths under 8 pt refuse.

The edit only changes the stamp's Rect. Its appearance keeps the BBox it
was made with, and a viewer fits that BBox to the Rect (PDF 1.7, 12.5.5),
so a new Rect moves and scales the image uniformly without touching it,
and a stamp placed on a turned view stays upright. I tried rewriting the
image object's matrix first: PDFium regenerates the stream but keeps the
old BBox, so the image was scaled twice. With the Rect alone the image
data, its matrix and its transparency are left exactly as written.

Only Sable's stamps are edited: one image object, no other keys, a Rect
with the same shape as the image's own box, and an appearance that
renders the same as the one PDFium would regenerate from the image
object, checked on an isolated copy. The write goes through the shared
transaction after the signed-PDF guard, and the saved copy is reopened
to check the Rect, the unchanged matrix and pixels, and every other
annotation.

## Lines and arrows (2026-11-12)

Comments > More has Place line on page and Place arrow on page after the
ellipse. Like the outline shapes they take two clicks, the start and the
end, and Esc or another tool drops a pending start. A second click less
than 4 pt from the first keeps the start and says to click farther away.

PDFium can't create a Line annotation or set /L and /LE (chromium/8086
fpdf_annot.h), so both are Ink in the current Drawing style. A line is
one two-point stroke. An arrow adds two strokes back from the tip, 30
degrees either side, four pens long, at least 8 pt and never more than a
third of the line. The points are worked out on the displayed page in
points, so a turned view or a page that isn't square keeps the angles,
and they keep half the pen and a little more inside the page. Other apps
see them as drawings.

Being drawings, they list as Drawing in Comments and move, restyle and
delete with the drawing tools. A Line or Arrow label in the list and a
resize dialog for them would need the InkList in the snapshot, so they
wait.

## Annotation edit probes (2026-11-12)

Four probe modes cover the annotation batch from the UI: TextBoxUnicode
(the text-box dialog with Greek, CJK and an accent typed as a separate
mark, and a right-to-left refusal), InkEdit (pick, drag, Esc, Edit
drawing and Delete), StampEdit (pick, drag, Resize stamp and Delete) and
LineArrow (two-click line and arrow, a too-short click, Delete). They
drive the same calls the pointer and dialogs do, not real input, so the
pen and physical mouse checks stay with me. Each feature also has a
save-and-reopen test that edits the reopened copy again, which is what
proves the result is still Sable's own model.

## Hindi and Bengali text boxes (2026-11-12)

Text boxes now take Hindi (Devanagari) and Bengali. A line with either
script is shaped through DirectWrite before it becomes outlines: the
line is split into script runs with AnalyzeScript, each run into the
fonts the system fallback picks (Nirmala UI here), and each piece goes
through GetGlyphs and GetGlyphPlacements with the script's default
features. A cluster's glyphs become one outline on its first character,
with the advances and offsets DirectWrite gave, so conjuncts, reordered
matras and reph come out joined. The later characters of a cluster draw
nothing. ZWJ and ZWNJ are allowed with them. The OS/2 fsType check is
the same as before.
// see: https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextanalyzer-getglyphs
// see: https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextanalyzer-getglyphplacements

Nirmala's vowel signs and reph reach past the 12 pt the first boxes left
above the baseline, so a box with shaped text puts its first baseline
15 pt below the top and spaces lines 18 pt apart instead of 13 and 15.
Text without Devanagari or Bengali takes exactly the old path, one glyph
at a time over the whole text, so existing Unicode boxes keep their
bytes and stay editable. Spaces and Latin between Hindi words stay
unshaped. Tamil, other Indic scripts, right-to-left scripts and Thai
still refuse with the script message.

The tests check the glyph ids and positions against what DirectWrite's
own text layout draws for the same words, and render the saved, reopened
boxes in PDFium against a Direct2D render of the same layouts with
outline rendering.

## Line and Arrow labels, and resizing them (2026-11-12)

Lines and arrows are still Ink, but addInk now writes /Subj (Line) or
/Subj (Arrow) on them. A drawing is read as a line or an arrow only when
that subject matches its InkList: one two-point stroke for a line, or
that plus two two-point strokes starting at its tip for an arrow. Then
Comments lists it as Line or Arrow and the snapshot carries its length.
Anything else, including lines drawn before this, stays a Drawing.
Acrobat shows the subject as the comment's title; readers that ignore
/Subj still show an ink drawing, as before.

The page menu on a line or arrow adds Resize line or Resize arrow, with
Length (pt). The start and direction stay put and an arrow's head is
drawn again for the new length with the same rule as placing one: four
pens, at least 8 pt, at most a third of the line, its two strokes in the
order they were written. Under 4 pt refuses. The write goes through the
drawing editor's proof, the signed-PDF guard and the shared transaction,
so it only touches drawings Sable wrote; that proof now accepts the Line
or Arrow subject when the strokes have the matching shape.

## Read Aloud (2026-11-12)

Read aloud on Home and in the nav rail, and the readaloud tool, start
reading at the page in view. Each page's text comes from PDFium on its
thread through the same documentText call Export uses, flattened into
one run (line breaks become spaces, words hyphenated across a line are
joined). Windows' SpeechSynthesizer turns it into a stream and a
MediaPlayer plays it, so Pause and Play pick up mid-sentence. When a
page ends the next one starts and the view goes to it; a page without
text is skipped with a notice, and the last page ends with one. No
dependency was added: both are Windows APIs.
// see: https://learn.microsoft.com/en-us/uwp/api/windows.media.speechsynthesis.speechsynthesizer

The bar sits at the bottom of the document: Previous page, Play/Pause,
Stop reading, Next page, "Reading page N of M", Voice (every installed
Windows voice) and Speed (0.5x to 2x). An accent outline around the
page being read follows scrolling and zoom. Switching to another tab
pauses; closing the tab stops. A machine without voices gets a notice
pointing at Windows' speech settings.

The voice and speed persist in read-aloud.json next to config.json,
following night-run.json, so config.json keeps the shape NightPDF
wrote. A missing or bad file gives Windows' default voice at 1x, and a
saved speed snaps to one the bar offers. The ReadAloud probe runs with
speech turned off and checks the text it would have spoken.

## Command bar, All tools and quick tools (2026-11-12)

Every document now has DESIGN.md's command bar: a 40 px row above the
pages with All tools on the left and Find (Ctrl+F), Save a copy (Ctrl+S),
Print (Ctrl+P) and More on the right. More has Document properties,
Rotate Clockwise and Counterclockwise, Presentation mode and Show quick
tools. Everything calls what the Menu already calls, with the Menu's
shortcuts shown; no keybinding was added or changed. The bar, the panel
and the strip all step out of the way in presentation mode.

All tools opens a 230 px panel over the left of the pages, listing every
tool that exists in groups: Pages (Organize pages, Combine files),
Comment & sign (Comment, Fill & Sign), Export, and Read (Read aloud,
Find). It overlays the pages rather than narrowing them, so opening it
doesn't move the reading position.

The quick-tools strip floats at the left of the pages: Text selection,
Hand tool, Sticky note, Highlight, Draw, Text box, Fill & Sign and Read
aloud. The cursor tools and the placing tools show as checked while
they're on, and a second press on a placing tool cancels it. Highlight
marks the current text selection, or says to select text first. The
strip moves aside when the panel opens. Show quick tools hides or shows
it for every tab until Sable closes; it isn't saved, so config.json keeps
NightPDF's shape. The toggles act on Checked and Unchecked rather than
Click, so keyboard and UI Automation toggles work too.

## Print to PDF through the dialog (2026-11-12)

The earlier note that Save had "unavailable geometry and a window-owner
mismatch" was the Microsoft Print to PDF driver's own Save Print Output
As dialog, not Sable. Sable gives the modern print UI its reader window
through IPrintManagerInterop, and the job target is owned by Windows.
The driver then opens its file dialog on Sable's UI thread with no owner
window, at the top left of the screen, and its Save button and file name
box don't expose UI Automation patterns. Set through Win32 messages it
saves normally, and the output was a one-page PDF from the driver. There
is no owner for Sable to hand over, so I changed nothing; the check is
recorded in READER_CHECKS.md.

## Read-only and hard-linked save destinations (2026-11-12)

New tests pin down how saves treat awkward destinations, with no change
to the code. A read-only file isn't overwritten: the save fails, the
file keeps its bytes and its read-only flag, and no temporary or backup
file is left beside it. Clearing the flag stays the user's call. A
destination that is one name of a hard-linked file gets the new PDF
under that name; ReplaceFileW gives it a new file, so the file's other
names keep the old bytes, as when Explorer replaces a file. The source
PDF and its own hard links are still refused as destinations. An
ordinary overwrite keeps the old file's attributes and creation time,
along with the named streams and permissions already covered.

## Installer, signing and file associations for the alpha (2026-11-12)

The alpha ships as the portable ZIP only. MSIX would make Sable a packaged
app, moving its settings and logs and needing a trusted signature before
it installs at all. Velopack is still the likely installer and updater
for a beta, once signing is settled. It stays the pre-approved choice
from §13 but isn't added now.

The alpha is unsigned. Windows SmartScreen shows "Windows protected your
PC" for it, and a downloaded ZIP may need Unblock in its properties. The
release notes say so. For later releases I'll apply to SignPath
Foundation's free signing for open-source projects; Azure Artifact
Signing's individual validation isn't open everywhere, and a bought
certificate costs a few hundred dollars a year.

Sable doesn't register file associations. Portable folders get moved, and
a registered path would go stale. Open with, Choose another app, then
Sable.exe already works. File associations come with an installer, .pdf
only (§15 D7).

This settles §15 D8 for the alpha only.

## Startup update check (2026-11-12)

NightPDF called electron-updater on every new window. Sable checks once
per start and only tells you: one HTTPS GET to GitHub's release list for
abhilxsh07/sable, off the UI thread, with a "Sable/<version>" user agent.
GitHub sees the IP address and that header. No ID, settings or file
names are sent. Drafts never count. Prereleases count only while the
installed build is a prerelease. Only this repository's releases/tag/<tag>
page can be opened. A newer release shows an InfoBar at the top right of
the window with a Release page link; nothing is downloaded. Failures,
including the 404 GitHub gives while the repository is private, are only
logged at Info.

It's on by default, like NightPDF. Settings > General has a "Check for
updates" box. The choice lives in update-check.json beside config.json,
like read-aloud.json, so config.json keeps NightPDF's keys (I11). Unit
tests use canned responses, and probe builds never make the call.

## Accessibility pass (2026-11-12)

Windows contrast themes switch Sable to a HighContrast theme dictionary.
Every Sable colour, the accent gradient and the fixed control colours map
to the theme's system colours. Code-made surface and text brushes follow
the same switch. The pages keep the night filter the user picked, because
that filter is the reason to use Sable. The preset chips keep their
colours because they preview the filter. Home's tool-card tints and the
music pane's code-set colours aren't converted yet.

The Accessibility probe checks every tool surface for unnamed or
unfocusable controls and for anything Tab can't reach. Home's tool
buttons now carry their labels as automation names. Nothing else needed
fixing. No access keys or shortcuts were added, so the keybinds stay
NightPDF's.

Screen readers see the current page as a UI Automation document named
"Page N of M", with a text pattern over the page's text. The Image can't
make its own peer, so a small Grid (PageTextHost) holds it. Text is
fetched on the PDFium thread only after a UI Automation client asks for
the peer. Ranges move by character, word, line, paragraph and page and
support FindText. Selection, text attributes and on-screen character
boxes aren't reported yet. A page that forbids copying gives no text.

## Contrast themes on the chips, Home tools and music pane (2026-11-12)

Under a Windows contrast theme the filter preset chips now take button
colours: button face with button text, and the selected chip the theme's
highlight with highlight text. This replaces the line in "Accessibility
pass" that kept their own colours. Each chip's label already names its
preset, and a contrast theme asks for its own colours everywhere.

Home's tool tiles drop their tint and draw the icon in button text. The
night mode icon uses window text and the drop card's dashed border the
highlight. Recent files use window text, with the PDF badge and a
starred file's star in the highlight. The music pane uses the window
colour instead of the album-art tint, window text for its border, button
face for an empty cover, and the highlight for the playing dot and the
play button's hover and press. The sample page on Home keeps the chosen
filter, like the pages. Without a contrast theme nothing changes.

## pdf.js review of navigation and layout (2026-11-12)

A reviewer compared the pdf.js-cited code in helpers/navigation.*,
helpers/layout.*, ViewerKeyboard.cpp and ViewerPage.xaml.cpp with the
pinned pdf.js v4.2.67 files they cite. Four parts follow pdf.js statement
by statement:

- `pageLabels` (navigation.cpp:117-137) mirrors app.js
  `_initializePageLabels` (1564-1597): the same counting loop and checks.
- `currentPage` (navigation.cpp:177-231) mirrors ui_utils.js
  `getVisibleElements` (540-590) and the current-page choice in
  pdf_viewer.js (1606-1630): the same hidden-share sums, percentage and
  sort.
- `navAction` (navigation.cpp:233-336) mirrors app.js `webViewerKeyDown`
  (2925-3166): the same key groups, page-fit gate and order of checks.
- The focus return at the end of ViewerKeyboard.cpp (about 198-222)
  restructures app.js's `ensureViewerFocused` cases (3184-3200) but keeps
  its key list; I count it with the others to be safe.

Each now has a behaviour-only description outside the repo
(D:\NightPdf\pdfjs-behavior-page-labels.md, -current-page.md,
-nav-keys.md, -viewer-focus.md). A later session rewrites them from those
descriptions alone, as the zoom code was. Nothing was changed here.

The rest matches behaviour with its own structure: `pageText`,
`pageName`, `pageFromText` and its number parsing, the key mapping and
Space scrolling in ViewerKeyboard.cpp, all of layout.cpp (pdf.js lays
pages out in CSS), `renderSize`, the password dialog, the zoom reset,
`scrollToPage`, the page box commit, thumbnail scrolling, the start
page and the loading placeholder. Three values are plain constants: the
viewer background rgb(42,42,46), the 2^25 pixel canvas cap and the
400 ms zoom delay.

UI strings that come from pdf.js's en-US viewer.ftl: "of {pagesCount}"
and "({pageNumber} of {pagesCount})" (navigation.cpp:149-152); "Enter the
password to open this PDF file." and "Invalid password. Please try
again." (ViewerPage.xaml.cpp:233-234); "Page {page}" and "Thumbnail of
Page {page}" (ViewerPage.xaml.cpp:1341-1343); "Previous Page", "Next
Page", "Automatic Zoom", "Actual Size", "Page Fit", "Page Width", "Show
Thumbnails", "Thumbnails", "Document Outline", "Show Attachments",
"Attachments" and "Find in document…" (ViewerPage.xaml); "Phrase not
found" (ViewerFind.cpp:292); "Zoom In", "Zoom Out", "Rotate Clockwise",
"Rotate Counterclockwise" (MainWindow.xaml); and the page size names
"Letter", "Legal", "portrait" and "landscape" (helpers/properties.cpp).
Single common words (OK, Cancel, Close, Print, Tools, Open File and the
editor colour names) also appear in the file, but they are ordinary UI
words and aren't counted as taken. Short interface strings are facts
about what NightPDF showed; whether they need any notice stays an
owner call.

## Framework-dependent ZIP for the alpha (2026-11-12)

The portable ZIP no longer carries Microsoft's Windows App SDK runtime.
Sable is built framework-dependent, and people install Microsoft's
Windows App Runtime themselves, from Microsoft's downloads page, for
their architecture. The installer doesn't need administrator rights; it
falls back to installing for the current user. The x64 ZIP shrinks from
30.1 MB (292 files) to 6.1 MB (60 files).

Sable starts the runtime itself (RuntimeCheck.cpp) instead of the SDK's
auto-initializer. If no matching runtime is installed, Sable says which
version and architecture it needs and offers to open Microsoft's download
page, then exits. That also happens for --help and --version, because the
check runs before anything else. With the runtime installed, the command
line works as before.

Only Microsoft's small bootstrapper DLL (and the metadata files the build
copies) still ship beside Sable's GPL-2.0-only code, so the open question
about the Microsoft Software License Terms is narrower but not settled.
The package script refuses a build folder that still holds self-contained
runtime DLLs. The unit tests don't use the Windows App SDK, so they
don't change.

## Page operations and closing (2026-11-12)

I'm keeping Export and Organize exclusive, as in the audit's approved
plan. Page operations also hold off save and close, including a clean
tab and the window's close button. Extract keeps that hold through its
destination picker and write. A plain Save a copy holds the same edit
lock as a notes save, so pages can't change behind its picker.
Closing a clean tab still cancels a plain copy and ignores a late picker
result, as before.

## Refused page moves (2026-11-12)

The grid moves its tiles before the PDF changes. If an edit can't start,
can't be queued or is cancelled, I rebuild it from the document's page
order. A failed drag mustn't leave the next Rotate or Delete aimed at
another page.

## Organize tile geometry and rotation (2026-11-12)

Every tile reserves the same cell for its page and caption. The page
keeps its shape and sits in the centre, so a landscape first page can't
crop the next portrait page. Rotation updates the selected tiles in
place and only renders the images it cleared; selection stays put.

## Organize owns the canvas (2026-11-12)

Organize remembers the open navigation panel, launcher and Find, closes
them and hides the quick strip until Done. Its grid stays in the canvas
column beside the rail. Panel toggles and reader input stay off while it
is open; Done restores the previous surfaces. Read Aloud pauses and its
overlays hide. Presentation leaves Organize and stops speech. Navigation
panels now have the same opaque themed background as Comments.

This is the Organize portion of the audit layout plan. The docked tool
pane host and command-bar replacement remain separate work.

## Organize keyboard routing (2026-11-12)

While Organize owns the canvas, Ctrl+A selects its tiles, Delete uses
the same refusal checks as its button and Esc returns to reading.
GridView keeps its native arrows, Home, End and Space. Reader bindings
are held off; save, print, open, tab switching and closing still belong
to the window. NightPDF's bindings are unchanged in reading mode.

## Names for unsaved page edits (2026-11-12)

I track page edits separately from notes. Rotate, Delete, Move and Insert
use "Save your changes?", "Save changes" and an "edited.pdf" copy name,
as form edits already do. Note-only documents keep their existing
wording. Both kinds of dirty state clear only after a successful save.

## Organize teardown and changed-page caches (2026-11-12)

A hidden Organize tab stops its tile timer and releases its images. It
keeps the selected page numbers and rebuilds the tiles when shown again.
Closing releases them without reopening any of the remembered panels.
Render callbacks check their generation before changing queue state.

Page edits invalidate Properties, including an older request still in
flight. Read Aloud drops the previous text, clamps its page to the new
count and fetches it again, keeping the paused state in Organize. The
large-document tile cache while Organize is visible is separate work.

## Known layout faults in the Panels probe (2026-11-13)

The Panels probe goes in before the layout fixes, so it would fail until
the tool pane exists. I list the faults that are still open in the probe
instead. A check that finds one reports it and goes on; anything else
fails. Once a fix lands, its fault stops showing up and the probe fails
until I take it off the list, so the list can't outlive the bugs.

## Grouped page counts (2026-11-13)

The rail already prints "of 1,000" the way pdf.js's en-US strings do.
Organize's count, the Comments loading line, Export's range message and
Read Aloud's position now use the same grouping. The page box itself
still takes and shows plain numbers, as in pdf.js.

## Failed and still-loading tabs (2026-11-13)

A tab that is still opening, or that didn't open, shows no command bar,
quick strip or rail. A failed one keeps just its message. The command bar
and the strip appear when the document has opened, and a failure after
that hides them again.

## Notices get their own row (2026-11-13)

A notice no longer floats over the page and the command bar. It sits in a
row of its own under the command bar and the pages move down to make room;
with nothing to say, the row has no height. One notice shows at a time and
a new one replaces the last. A success closes itself after five seconds.
An error stays until it is closed, replaced, or what it described changes:
Organize opening or closing, or a page edit going through. The message of a
tab that didn't open is that tab's only content, so nothing replaces it.

## All tools gets its own column (2026-11-13)

All tools used to open as a 230 px panel lying over the left of the page,
and the quick strip floated over it too. The panel is now a column of its
own, 320 px wide, left of the pages, and the pages move over to make room,
the way they already do for the thumbnails and the other right-hand
panels. The quick strip keeps a 56 px margin beside the pages; hiding the
strip gives that room back. All tools and a right-hand panel can be open
together, unless that would leave the pages narrower than 360 px: then
opening one closes the other.

## The tool pane gets a header (2026-11-13)

The pane left of the pages now has a header: the name of what it shows, a
back arrow to the list of tools (hidden while the list itself is showing)
and a Close button that gives focus back to the pages. The viewer keeps
one record of what the pane holds, so the pane, its header and the All
tools button can't disagree. Asking for the pane that is already open
moves focus into it instead of opening it again.

## Document properties open in the tool pane (2026-11-13)

Document properties are a pane beside the pages instead of a dialog, so
they no longer cover the document or stop the reader's keys, and the Current
page and Page size rows follow you as you read. The values are read by a
screen reader with their label ("File name: report.pdf"), and the labels take
their colour from the theme. All tools lists Document properties in a group of
its own and no longer lists Find; the command bar's Find button and Ctrl+F
stay. Menu > File and the command bar's More menu open the same pane. Retry,
when the details can't be loaded, is a button in the pane. Opening properties
while Organize is open says to finish organizing first, and Organize gives back
the pane that was open when it ends. Esc does not close the pane yet; the Esc
order is a later step of this batch.

## The tool pane can be resized (2026-11-13)

The tool pane has a grip on its edge beside the pages. Dragging it, or
Left and Right while it has focus (16 px a press), makes the pane anything
from 280 to 480 px wide; it opens at 320. Each pane keeps the last width it
was given in panes.json next to config.json, so config.json keeps
NightPDF's keys. Widening the pane stops where the pages would drop under
360 px beside an open side panel instead of closing the panel; opening the
pane at a remembered width still closes a side panel that won't fit, as
before.

## Esc steps back one thing at a time (2026-11-13)

Esc closes Find first, then drops an armed tool (a note, a shape or a
drawing being dragged), then closes Organize, then closes the tool pane
and hands focus back to the pages. Before, an armed tool went before Find
and Esc left the pane open. Clearing a selected annotation or text
selection still happens on Esc once none of those is open. Each tab keeps
its own pane, width and armed tool; switching tabs changes none of them.

## Organize's buttons go in the command bar (2026-11-13)

While Organize is open its actions (Rotate left, Rotate right, Delete,
Insert from file, Extract selection) and its status sit in the middle of
the command bar, and Done replaces find, save, print and More on the
right. The row of buttons above the tiles is gone. The button labels
don't change here.

## Export opens in the tool pane (2026-11-13)

Export is a pane beside the pages instead of a dialog: Format, Pages,
the page range and Resolution, with Export at the foot. Pages gains
Current page. The choices stay with the tab while it's open, so a second
export starts from the last one, and the pane keeps its own width in
panes.json. Export still asks where to save each time; the folder line
comes next. A range that doesn't parse says so under the fields instead
of keeping a dialog open.

## Export keeps its folder and selected pages (2026-11-14)

I keep the output folder with the tab. The first export asks for a folder;
later exports use it until Change picks another one. Cancel leaves the
previous folder alone. Text goes there as <stem>.txt, like the page images.
Selection uses the highlighted thumbnail or the pages kept from Organize
on Done. Page edits clear that saved selection, since page numbers may
now refer to different pages. Export and Change stay disabled while either
an export or its folder picker is running.

## Export previews the current page (2026-11-14)

The Export pane shows a small unfiltered preview of the page I'm reading,
even when the output range is All or Selection. PNG and JPEG use their own
encoders; Text shows the page's first lines. The page and format label names
the image for Narrator. Loading a later page or format discards an older
reply, and closing the tab stops it from touching the pane.

## Export can stop after the current page (2026-11-14)

Export shows the page being worked on as n of m, with Cancel below it.
Cancel finishes that page and keeps the files already written. Text is
read a page at a time too; the completed pages go into one text file.
The end notice gives the file count and full output folder, including
when I stop early. The choices stay disabled until the work finishes.
Encoding and file writes run on a worker; the engine still owns every
PDFium call. Organize and tab close keep their existing busy refusal.

## Combine keeps its input list beside the document (2026-11-14)

From a document, Combine opens a pane whose draft stays with that tab.
It starts with the current file; each row shows its name, page count and
Remove, with Add files, the total and Combine below. Password-protected
inputs must be removed; signed inputs are labelled before starting.
The engine reads those details. The window keeps the existing save picker
and opens the new PDF. Home's picker flow is a later step of this batch.

## Combine input order and result name (2026-11-14)

I can drop PDFs on the list, drag its rows or use Alt+Up/Down to move the
selected row. Plain arrows stay with the list. The saved order follows
the rows, including repeated inputs. Other dropped file types are skipped.
The result name starts as <stem> (combined).pdf and stays with the tab;
the save picker suggests that name and adds .pdf if I left it off.

## Combine progress, cancellation and writes (2026-11-14)

Combine reports completed inputs out of the total, then shows the save
phase. Cancel uses the engine's predicate and the file writer's check;
it stops at the next check and leaves the destination alone. The tab
stays busy through the picker and write, and I can reopen its pane to
reach Cancel. Each run owns its flag so old progress can't reach a new
run. Combine and Extract both write on workers; PDFium stays on the
engine thread. Home's picker flow follows in the next task.

## Home opens Combine on the first picked PDF (2026-11-14)

Home's Combine entry uses the multi-file picker, then opens one viewer
on the first PDF with every selected path in its Combine draft. I keep
those paths until the viewer has its engine and layout, then read their
details. Regular Open still makes one tab per file. A picker owns its
tool request while it waits, so Cancel, a busy picker or an error can't
start that tool on an unrelated file later. The old Home dialog is gone.

## Shell failure recovery (2026-11-15)

I catch Print setup and tab move failures in the window and show an
error notice. A failed move puts a removed header back before releasing
the move guard, so the mounted viewer still has its tab. Drag cleanup
clears its saved selection even when restoring that selection fails.
The XAML exception fallback marks the exception handled, logs its detail
and asks the reader windows to show a retry notice. Debug probes still
fail on an unexpected XAML exception, so recovery cannot hide a broken
check. This follows A-25 in the approved shell audit queue.

## Form action names in the rewritten copy (2026-11-15)

I use the signature guard's token reader for the form action check too.
Only decoded A, AA, JS and JavaScript names outside strings, comments
and streams refuse a form transaction. Longer names do not match.
This keeps the existing refusal for actions elsewhere in the form tree
without treating compressed stream bytes or field text as actions.
Signature checks keep their existing field/value and permission rules.
This is A-29 in the approved shell audit queue.

## Closing a background tab (2026-11-15)

I confirm the requested document without selecting its tab first.
Cancel keeps the active tab, and removing a clean background tab leaves
the active viewer alone. Busy close/save/note refusals use the window's
notice so they remain visible even when the requested tab is hidden.
The existing per-document save/discard confirmation and save-copy
cancellation still apply. This follows A-31 in the approved audit queue.

## Document commands wait for a ready PDF (2026-11-15)

I keep the tab's viewer visible while it loads or shows a password/error,
but document commands need its page layout. Opening a PDF refreshes the
window's command state as soon as that layout is ready. Zoom, rotation,
Find, presentation, properties, print, save and layout/cursor menus now
follow that state. Print and save completion cannot enable a command
over a failed or pending tab. This follows A-32 in the approved queue.

## PDF drops across the reader (2026-11-15)

I handle file drops at the window root, across Home and the document
surface. Local PDFs open as tabs; other file types and folders are
skipped. Existing paths still select their tab instead of adding a copy.
A document opening while the source supplies its files no longer
invalidates the drop. Combine's list handles its own file drags, even
when busy, so they cannot turn into window-level opens. Non-file drags
are left to the native controls for their own reordering. This is A-33
in the approved audit queue; Home's Tools group stays in place.

## Shortcut edits in open readers (2026-11-15)

Before routing a key, each reader compares its active binding snapshot
with the shared settings store and rebuilds the table if it changed.
Only saved edits reach the table; failed saves keep the old bindings.
I keep the table builder separate from event registration so edits do
not add handlers. The startup settings stay const. Fixed menu keys,
modifier requirements and keypad aliases keep their existing precedence
and mapping. This follows A-41 in the approved audit queue.

## Home presets and recent history (2026-11-15)

Home uses the window's exact preset and slider values for its chips and
sample page. A Home preset pick changes the window's filter too, without
opening the document configurator; a repeated pick keeps the current
values. The title-bar preset/configurator behavior stays as before.

Clear recent is shown only when Recent contains an unstarred entry.
Each row also has a named Remove from recent action. It removes that
history entry, including its star if present, through the existing
locked recent-file update. It never deletes or opens the source PDF.
This follows A-47; the Home Tools group remains in place.

## Tab dragging stays inside the strip (2026-11-15)

I keep CanReorderTabs on and turn CanDragTabs and CanTearOutTabs off.
Reordering remains the native strip interaction, with the existing
selection guard; no external tab payload or new-window tear-out is
offered without a destination. This follows A-48 in the approved queue.
The API distinguishes these behaviors:
https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.controls.tabview.candragtabs?view=windows-app-sdk-2.0
https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.controls.tabview.canreordertabs?view=windows-app-sdk-2.0
https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.controls.tabview.cantearouttabs?view=windows-app-sdk-2.0
