<!--
Sable, a native port of NightPDF
Copyright (C) 2021  Advaith Madhukar
Copyright (C) 2026  Abhilash Kar

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
-->

# Feature checklist

Reviewed 2 November 2026, through a88510c. Runtime checkpoint: 2d88cf2.

5 November follow-up: outline rectangle and ellipse placement, guarded
removal and Windows picker round trips are recorded in TEST_PLAN.md.
The broader shape editor remains unfinished.

11 November follow-up: record the verified text-box, drawing, shape and
stamp work below.

I tick an item when it is implemented and has recorded verification for
the scope named. An empty box means work remains. Partial features list
their working pieces separately; device checks and release approval have
their own boxes. A visible tool button alone does not count as a feature.

The scope comes from [DESIGN.md](DESIGN.md). Detailed behavior and limits
are in [DECISIONS.md](DECISIONS.md); checks are recorded in
[TEST_PLAN.md](TEST_PLAN.md) and [READER_CHECKS.md](READER_CHECKS.md).
The older test counts in those documents are historical checkpoints.

## Next steps

- [x] Audit the remaining E01 form limits: parent V/type and Opt proof,
  DoNotScroll overflow, inherited Comb and password-protected scratch copies.
  Record which can be proved with the pinned API; retain refusals otherwise.
- [x] Implement the next form slice that passes that audit, with isolated
  writes, saved-copy identity checks and engine/queue regressions. If no safe
  slice is available, record the deferral and move to free-text annotations.
- [x] Add the native editor and desktop save/reopen checks for that slice.
  Keep broader form support unchecked until its remaining cases are covered.
- [x] Extend text-box resource ownership and native placement/creation checks.
- [x] Edit and resize supported existing boxes and add bounded multiline text.
- [x] Add verified mouse drawing, outline shapes and image stamp placement
  to Comments.
- [ ] Continue with page organizing, combine/export and Read Aloud.

## App, files and tabs

- [x] Native Windows reader using WinUI 3, PDFium and Direct2D.
- [x] PDFium work owned by one engine thread.
- [x] Open local PDFs without retaining a source-file lock.
- [x] Unicode and spaced paths; loaded bytes survive source-file changes.
- [x] Multi-select PDF picker, Open commands and tab-strip + action.
- [x] Home splash/drop card, original splash text and preset preview.
- [x] Recent files, starring, reopening and clearing unstarred recents.
- [x] Splash storage-item drop handling and duplicate-path detection.
- [x] Multiple windows with independent tabs and reading state.
- [x] Last-window shutdown and pending-work lifetime handling.
- [x] Startup sizing, minimum size and MaximizeOnOpen preference.
- [x] Tabs: open, close, reopen last closed, select and move by shortcut.
- [x] Drag-reorder implementation, bounded titles and full-path tooltips.
- [x] Last-tab close restores home and disables document commands.
- [x] Positional PDF CLI, starting pages, help/version and exit codes.
- [x] Missing, damaged and unsupported-file error handling.
- [x] Password prompts, retry and cancellation for reading protected PDFs.
- [x] Document properties: metadata, sizes, version and permissions.
- [x] Rotating diagnostics and DEBUG behavior.
- [x] Shift+Alt+M Morty alert inside the reader.
- [ ] Explorer-to-reader drops and physical tab-drag sign-off.
- [ ] Automatic startup update check (P19); updater choice is still open.

## Reading, navigation and page layout

- [x] Continuous centered pages and virtualized rendering.
- [x] Bounded raster/cache work and 1,000-page native regressions.
- [x] Zoom buttons, keyboard aliases, reset and fit/percentage presets.
- [x] Pinch/Ctrl+wheel handling, anchor preservation and settled sharpness.
- [x] Zoom-reset freeze frame while the scroller and layout settle.
- [x] Page box drafts, page labels, total, boundaries and starting page.
- [x] Previous/next, first/last and reader navigation shortcuts.
- [x] Thumbnails, selected-page state and DisplayThumbs preference.
- [x] Existing outline/bookmark hierarchy and destination navigation.
- [x] Attachment listing, metadata and saving an attachment.
- [x] Internal/named destinations and safe external-link routing.
- [x] Text selection, Select All and permission-aware text copy.
- [x] Find next/previous, result counts, wrap and highlight all.
- [x] Find case, whole-word and diacritic options; Unicode normalization.
- [x] View rotation in both directions.
- [x] Vertical, Horizontal, Wrapped and Page scrolling.
- [x] No/odd/even spreads and optional spread-click page turning.
- [x] Hand tool and text-selection tool with per-tab state.
- [x] Full screen and presentation with restored reading state.
- [x] DPI/scale-change handling and simulated surface-loss recovery.
- [ ] Physical pinch, diagonal pan and Ctrl+wheel retest on a long book.
- [ ] Full physical keyboard/clipboard and presentation input pass.
- [ ] Touch/pen behavior, mixed-DPI monitor moves and actual GPU loss.
- [ ] PDF layer visibility controls (V14), deferred by the pinned API.
- [ ] Read Aloud with Windows voices, navigation and playback controls.

## Appearance, settings and shell layout

- [x] Default, Sepia, Redeye and Custom with original preset colors.
- [x] Six live sliders, ranges, tooltips and shared window-tab filtering.
- [x] Golden filter math and GPU page filtering.
- [x] Unfiltered UI, thumbnails and print output.
- [x] Original Dark palette and live, persisted Obsidian theme.
- [x] Shared theme resources across windows, menus, dialogs and music pane.
- [x] Manrope text and Windows Fluent icons.
- [x] Title-bar tabs, Menu and preset controls.
- [x] Right rail for thumbnails, outlines, comments and attachments.
- [x] General, Shortcuts and About settings pages.
- [x] Settings defaults, validation, migration and atomic save/rollback.
- [x] Shortcut capture, primary/secondary bindings, cancel, clear and Save.
- [x] Configured shortcuts after restart, with text-field focus guards.
- [x] Native/sampled checks for narrow Settings and visible control bounds.
- [ ] Complete the document command bar and grouped All tools panel.
- [ ] Complete the quick-tools strip as its tools become available.
- [ ] Full keyboard-only, Narrator and high-contrast accessibility pass.

## Comments and annotations (E02: partial)

- [x] Comments list, page navigation and existing annotation snapshots.
- [x] Add a sticky note at the current page or a chosen page position.
- [x] Edit and delete supported existing text notes.
- [x] Highlight selected text, including the five offered colors.
- [x] Underline and strikethrough selected text.
- [x] Select existing supported markup and delete it from the page.
- [x] Add/edit markup comment text and remove a comment while keeping its mark.
- [x] Saved markup appearances and hidden text-note icons.
- [x] Dirty-state tracking, unsaved-close review and saved edited copies.
- [x] Preserve other annotations, supported attachments and bookmarks.
- [x] Refuse protected/shared/threaded or unsafe annotation rewrites.
- [x] Acrobat check for green highlight, underline, strikeout and hidden notes.
- [ ] Acrobat appearance/printing matrix for other colors and rotated pages.
- [x] First-box appearance proof, isolated ASCII writer and queued requests.
- [x] Native single-line text-box placement, retained refusals and copy saving.
- [x] Supported existing text-box editing and bounded resizing.
- [x] Independent text-box resources, multiple boxes and adding after reopen.
- [x] Bounded multiline ASCII text boxes with picker and Acrobat checks.
- [ ] Unicode text boxes.
- [x] Freehand mouse drawing, color and width, copy saving and guarded Delete.
- [ ] Pen input and editing existing ink strokes.
- [x] Outline rectangles and ellipses: placement, guarded Delete and copy saving.
- [x] Resize supported outline rectangles and ellipses, and save copies.
- [ ] Broader shape creation and editing.
- [x] PNG/JPEG image stamp placement, guarded Delete and copy saving.
- [ ] Move and resize existing stamps.
- [ ] Full annotation editor suite; existing supported tools are only a subset.

## Form filling (E01: partial)

- [x] Bounded widget snapshots, field metadata and visible hit geometry.
- [x] Widget raster overlay for supported screen, thumbnail and print cases.
- [x] Plain single-line text editing and clearing.
- [x] Multiline text, real Enter input, wrapping and blank paragraphs.
- [x] Terminal comb text with bounded integral cell counts.
- [x] Bounded single-widget inherited single-line and multiline text.
- [x] Unicode values, length refusals and corrected-draft retries.
- [x] DoNotSpellCheck metadata, writer handling and native spelling setting.
- [x] Desktop spelling comparison: underlines only in the unflagged editor.
- [x] Plain checkbox editing with a validated export/state.
- [x] Supported radio-group selection and saved member states.
- [x] Terminal single-choice combo and list editing.
- [x] Stored option order, repeated labels and duplicate-export positions.
- [x] Sort-bearing choices retain stored order and selected index.
- [x] Clear a single-selection list when no default value is present.
- [x] Default-bearing list clear refusal, retained draft and valid retry.
- [x] Native dialogs: Cancel, no-op, queued controls and modal isolation.
- [x] Stale-request checks, cancellation, revision/cache and savepoint handling.
- [x] Mixed supported edits preserve other fields, notes and bookmarks.
- [x] Real picker save, fresh Sable reopen and sampled Acrobat appearances.
- [x] Inherited-checkbox unchanged/checked refusal and saved-neighbor checks.
- [x] Refuse scripts/actions, signatures, protected and unproved writes.
- [ ] Raw inherited-checkbox parent V/type and Opt proof; writes stay refused.
- [ ] DoNotScroll geometric overflow proof; writes stay refused.
- [ ] Inherited Comb cell-count proof; writes stay refused.
- [x] Password-protected scratch copies and bounded text edits (RC4 fixtures).
- [ ] Other encryption revisions and encrypted mixed-field interoperability.
- [ ] Shared/multiple-control field editing beyond the supported radio path.
- [ ] Editable combos, multi-select and inherited/shared choice editing.
- [ ] Clear combo selections or default-bearing lists safely.
- [ ] Remaining specialized text flags, richer form behaviors and actions.
- [ ] Broader PDF/font interoperability beyond the checked fixtures.
- [ ] XFA (E03), deferred pending build-size, dependency and license review.

Autocorrection did not occur in either spelling sample; a difference has
not been established. Form safety refusals are working behavior, but they
do not make the refused field type supported. Certificate signing is not
part of the planned visual signature tool.

## Fill & Sign tools beyond existing fields

- [x] Place text independently of an existing form field.
- [x] Place ticks and crosses as printable Ink annotations.
- [x] Create and place a typed visual signature.
- [x] Create and place a drawn visual signature.
- [x] Save/reopen, print raster and Acrobat checks for check marks and crosses.
- [ ] Save/reopen and external-reader checks for standalone text and signatures.

## Saving and printing

- [x] Untouched Save a Copy preserves the loaded PDF's exact bytes.
- [x] Edited copies include supported notes, markup and form values.
- [x] Source-path/alias protection, atomic writing and failed-save retention.
- [x] Real Save As cancellation and successful paths with spaces.
- [x] Unfiltered Windows print preview and asynchronous job handling.
- [x] Print layout, paper sizes, ranges, rotation and cancellation logic.
- [x] Microsoft PDF-driver output, reopened pages and pixel checks.
- [x] Desktop preview orientation, range controls and cancellation.
- [ ] Complete PDF output selected through the real modern print dialog.
- [ ] Physical printer output, copies and collation.
- [ ] Complete real overwrite/read-only destination and hard-link coverage.

## Music (implemented; real-player QA open)

- [x] Spotify/Apple Music session discovery and conditional music button.
- [x] Track/artist/player text, artwork and missing-art fallback.
- [x] Previous, play/pause, next and capability-gated shuffle/repeat.
- [x] Progress/remaining time and supported seeking.
- [x] Playing-session preference and stale metadata/art rejection.
- [x] Dark artwork tint, Obsidian black surface and reduced-motion handling.
- [x] Native checks with supplied sessions, artwork and command results.
- [ ] Actual Spotify playback, pause, seek, track/art and control checks.
- [ ] Actual Apple Music checks and switching with both players open.

## Organize, combine and export

- [x] Rotate pages in the saved PDF; view rotation already works.
- [x] Delete pages.
- [x] Reorder pages.
- [x] Insert pages.
- [x] Extract pages into a new PDF.
- [x] Combine multiple PDFs into one.
- [x] Export pages as PNG.
- [x] Export pages as JPEG.
- [x] Export document text to a file; selection copy already works.
- [x] Native controls and saved-result checks for these operations.

## Build, reliability and distribution

- [x] Pinned dependencies, restore/build/test/format scripts and CI workflow.
- [x] Debug x64/ARM64 and native x64 warning-free runtime checkpoint.
- [x] All 649 enabled x64 tests at that checkpoint; one existing disabled test.
- [x] Earlier Release x64/ARM64, sanitizer and repeated-suite checkpoints.
- [x] Bounded malformed-input, cache, cancellation and lifetime regressions.
- [x] Portable x64/ARM64/source packaging and integrity/architecture checks.
- [x] Extracted x64 startup/close smoke checks from paths with spaces.
- [x] Draft-release workflow and bundled license/notice inventory.
- [ ] Fresh Release validation and packages for the eventual release commit.
- [ ] First hosted release-workflow run and artifact review.
- [ ] Clean Windows PC/VM without development runtimes.
- [ ] Actual Windows on ARM execution.
- [ ] Long-duration leak/resource and real device-loss/sleep/resume checks.
- [ ] Resolve or explain recurring external-reader Font Capture errors.
- [ ] Distribution license review for actual PDFium and runtime binaries.
- [ ] Installer/signing/file-association decisions and implementation.
- [ ] Update delivery and startup checking.
- [ ] Public alpha sign-off/publication after release checks.

## Optional backlog, separate from the approved tool list

These ideas remain separate decisions unless already recorded as approved.
An unchecked idea is not a commitment to change NightPDF behavior.

- [ ] Persist the last preset and custom sliders across launches (I01).
- [ ] Apply keybinding edits to existing readers without restart (I02).
- [ ] Detect conflicting/duplicate shortcuts in Settings (I03).
- [ ] Route new Explorer opens to the last active instance (I04).
- [ ] Drop PDFs anywhere and tear tabs into new windows (I05).
- [x] Tabs in the title bar, the implemented part of I06.
- [ ] Mica backdrop, the separate unfinished part of I06.
- [ ] Optional filtering of thumbnails (I07).
- [ ] Restore window size/position and optional sessions (I08).
- [x] Recent/Starred home, the implemented part of I09.
- [ ] Recent-files menu/taskbar Jump List, the remaining part of I09.
- [ ] Per-tab night-filter overrides (I10).
- [ ] Update-check opt-out preference (I11).
- [x] Modern unfiltered print implementation, the implemented part of I12.
- [ ] Optional filtered print output, the separate part of I12.
- [ ] Import an existing Electron NightPDF settings file (I13).
- [ ] Complete accessibility review (I14); native names/focus guards exist.
- [ ] Record startup/memory/scroll budgets against Electron (I15).

## Outside the current scope

OCR, redaction, adding passwords, PDF compression, cloud sharing and
e-signature requests are excluded by the design. They are not unfinished
commitments in this checklist. Existing password-protected PDFs can be read.

## Keeping this current

Update the affected boxes when a feature or its named check is finished.
Keep partial support and deferred decisions explicit. Use
[READER_CHECKS.md](READER_CHECKS.md) for exact desktop results and
[RELEASING.md](RELEASING.md) for publication gates. Do not derive a completion
percentage from these boxes: a new annotation editor and a single QA check
are different amounts of work.
