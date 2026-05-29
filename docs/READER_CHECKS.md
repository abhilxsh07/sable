# Reader checks

The automated results, historical passes and remaining limits are tracked in
[TEST_PLAN.md](TEST_PLAN.md). The 26 October x64 Debug/Release suites each
pass 556 tests; twelve current native reader workflows pass. Microsoft's PDF
driver passes the explicit output check. Physical devices, Windows dialogs,
real players and display changes still need the checks below.

Start the normal Release build from the repository root:

```powershell
& .\x64\Release\Sable\Sable.exe .\tests\fixtures\text.pdf
```

## First checks on the new build

1. Extract the whole Sable-x64-qa.zip into a new folder. Open Sable.exe from
   that folder so an older running copy cannot be mistaken for this build.
2. Open the same book that showed the problem and go to its final page.
   Pinch in, pinch out, then immediately scroll. Repeat ten times, including
   sideways and diagonal scrolling. Check that the page stays nearby, words
   remain visible and the percentage changes with the pinch.
3. Switch to another tab and return. Use the zoom buttons, Ctrl+wheel and
   fit options. Try going to the last page during a pinch; it should still
   be the chosen page after zoom settles. Rotation, layout and presentation
   commands are available again after the gesture, rather than being queued.
4. Select Page scrolling with odd spreads, enable Click pages to turn
   spreads, and click the left/right page. Repeat with even spreads. Check
   the first and last spread, PDF links and dragging a text selection.
5. Follow the settings, saving, printing and music sections below. These
   exercise the real dialogs and players that native probes substitute.
6. If a failure appears, note the PDF/page, zoom, scrolling/spread mode and
   exact actions. A screenshot or short recording helps distinguish a
   transient scrollbar from content that failed to repaint.

## Reading and keys

Use J/K or N/P to move between pages. Focus a tab header, press Ctrl+Alt+G,
type a page number and press Enter. Typing alone should leave the current
page unchanged. R typed inside the page box should edit the draft; R outside
it should rotate the reading view. Shift+R restores the previous rotation.

Check Ctrl-minus while the page box has focus: the PDF should zoom out and
the field should not gain a soft hyphen. Check keypad +/- and Ctrl+0 too.
Use Ctrl+F, type Alpha, then Ctrl+G and Ctrl+Shift+G. Escape should close Find.
A button keeps Enter and Space; sidebars and selectors keep their own keys.

Select mixed-font text and use Select All. The line bands should stay even,
and the action menu should open after selection finishes. Try all five
highlight colors, underline and strikethrough. Click existing markup: its
outline and menu should appear, and Delete should remove that selection.
Delete in a text field must keep editing the text instead.

In Comments, add a note and check that no icon covers the page. Right-click
its row for Edit/Delete; clearing the editor should change Apply to Delete.
For markup, Remove comment should keep the visible mark. Save a copy and
reopen it to check notes, colors and markup, including pages not rendered
before saving. The source PDF should remain unchanged. Older stored shapes
stay as saved; recreate a malformed mark to use the corrected line bands.

The 26 Oct Acrobat check displayed the saved green mixed-font highlight,
underline and strikethrough, and listed all five comments including both
Text notes. After a Font Capture error cleared, the full-page recheck
confirmed both note icons stay hidden. Other colors, rotated pages and
printing from Acrobat remain unchecked. TEST_PLAN.md keeps the pass scope.

Enter presentation with Ctrl+Alt+P. Check click/Shift+click, wheel movement,
Ctrl+Up/Down, Escape and F11. Try a touch swipe if a touch display is present.
The normal layout and page reached should survive exit. Check mouse/pen
Hand drags and native touch scrolling separately.

On a precision trackpad, scroll with two fingers vertically and horizontally
over a magnified page, then pinch in and out without holding Ctrl. Try short
and long gestures, repeated direction changes, the zoom limits and a page
narrower than the viewport. The point being read should stay in view, the
percentage should match, and text should sharpen after release. Check Ctrl+
mouse wheel, zoom keys, fit presets, selection, Find and links afterwards.
Repeat after rotation, in each scroll/spread layout and after switching tabs.
Presentation should keep its fitted page, with normal pinch restored on exit.
The native gesture probe checks real ScrollViewer transforms and PDF layout
synchronization; physical trackpad recognition and feel still need this check.

For the long-document regression, go to the final page of a book with a few
hundred pages. Pinch out and in, scroll immediately, and repeat with the zoom
buttons. The last page should remain nearby, its text should repaint, and the
percentage should follow each gesture. Check the bottom and right edges too.

A thin scrollbar at the outer edge that appears only during panning is normal
WinUI feedback. A persistent seam through the PDF or a blank strip that hides
content should be recorded as a rendering failure.

Select View > Scrolling > Page and choose odd/even spreads, then enable
View > Click pages to turn spreads. Click the left page to turn back a spread
and the right page to turn forward. Check the cover, first and last spreads,
PDF links, text selection, Hand drags and disabling the option. A lone page
uses its left/right halves; clicks in the margins should do nothing.

## Appearance and settings

Open Settings with Alt+S. Move through General, Shortcuts and About with both
pointer and keyboard. Check hover/focus visibility, narrow/short windows,
shortcut capture, Escape to cancel capture, Save and clearing a binding.

Switch Dark to Obsidian (AMOLED). Resting app surfaces should become black;
text, focus, hover and pressed states should remain readable. Open a second
window, reopen Settings and restart the app to check the saved choice. Switch
back to Dark and check that the original palette returns. Changing the app
theme should not change PDF colors or the selected night-filter values.

## Saving and dialogs

Open a plain checkbox field and confirm that its saved checked state appears
in the editor. Space should toggle the focused box; Apply should update the
page, while Cancel leaves it alone. Apply without changing it should leave
the savepoint clean. Save a copy, reopen it and check the original too.
For a refused field, the choice should remain in the dialog with an error.

Form edits currently refuse PDFs containing actions or scripts, including
ordinary link actions. Refusal should keep the text or choice available
in the field dialog and leave the document unchanged.

Build the opt-in native check with SableSurfaceTest=true, then run with
SableTabsTest=true and SableCheckBoxTest=true. It exercises native controls
through their automation peers and supplies its own save destination. Physical
keyboard/pointer delivery and the real save picker still need a desktop pass.

Tap a radio option and check that its saved selection appears in the dialog.
An unselected option should enable Apply only after selecting it. Apply
should clear the previous option in that group; applying the selected option
again should leave the savepoint clean. Cancel and refused writes should
retain the saved group state. Save a copy and check both options on reopen.
SableRadioTest=true with SableTabsTest=true runs the native radio check,
including a refused target that keeps the selected draft available.
The 28 Oct desktop pass opened a selected option by pointer and kept it
selected after another click and Space. Apply closed without a save prompt.

Open a plain combo or single-choice list field. Check the saved selection,
option order, duplicate labels, arrow keys and pointer selection. Apply
without a change should leave the savepoint clean; changing it should update
the page. Save a copy and reopen it, then check Cancel and a refused draft.
An unset selection should keep Apply disabled until an option is chosen.
SableChoiceTest=true with SableTabsTest=true runs the native combo check;
add SableChoiceListTest=true for the list check. Buttons use automation peers,
while option changes use SelectedIndex; use a desktop pass for real input.

The 28 Oct desktop pass read the open combo popup and list through UI
Automation with repeated labels. Combo Down/Enter, list keyboard/pointer
selection and Cancel passed. A text-field no-op and clean app close passed;
all three source hashes stayed unchanged. The real save picker and
mixed-field interoperability still need a pass.

SablePasswordFormTest=true with SableTabsTest=true runs the encrypted text
form check. It exercises wrong/correct password prompts, native no-op,
length refusal/retry, modal shortcuts, Cancel and saved-copy reopening.
Build with SableSurfaceTest=true. The 2 Nov desktop pass also saved through
the Windows picker and reopened the accented value in a fresh process.
The saved file still needed its password; the source stayed unchanged.
Other encryption revisions and viewer interoperability remain open.

SableMixedFormTest=true with SableTabsTest=true runs all five supported
field types in one document. It checks page hit testing, Apply, Cancel,
no-op revisions and dirty state, then reopens the saved copy in a new tab
and checks each native editor again. A second tab makes modal tab shortcuts
observable; close, Find, zoom, rotate, Save and full-screen commands must
leave the draft and reader alone. Buttons and checkbox/radio changes use
automation peers; text and option drafts are set on the native controls.
The check supplies its save destination. Actual keyboard/pointer delivery,
the Windows picker and Acrobat appearances/interoperability need a desktop
pass separately.

SableClearListTest=true with SableTabsTest=true invokes the list's Clear
selection button, checks Cancel and Apply, then saves and reopens an empty
selection with unchanged Apply enabled. The button must be absent from
combos and disabled during writes. SableDefaultListTest=true implies this
mode and adds a default value: clear must refuse with its draft retained;
Cancel restores the live selection, and selecting an option retries Apply.
Both modes compose with Sort, text and inherited-checkbox checks. Native
automation supplies drafts and the save path; physical input needs desktop QA.

The 2 Nov default-list desktop pass retained an empty draft after refused
Apply; Ctrl+W left the modal and error intact. Cancel kept the live Zulu
selection. Clearing again refused, then choosing the first Alpha retried
successfully. Combo Down/Enter selected the second Alpha in stored order.
Windows Save As saved to a path with spaces; a picker owner mismatch was
resolved by refocusing the reader and pressing Return. Fresh Sable restored
both duplicate positions. Acrobat showed the selected list row, combo value
and original note without field focus. Source and saved-copy hashes stayed
unchanged. The desktop interruption occurred after the saved Sable check;
the resumed Acrobat check completed without a Font Capture error.

SableSortedChoiceTest=true with SableTabsTest=true runs the mixed-field
check with Sort on the combo and list. Both native editors must keep the
stored Zulu, Alpha, Alpha order and give repeated labels distinct items.
Cancel, unchanged Apply, positional edits and save/reopen must retain the
selected duplicate's index. Combine it with the text modes, including
SableNoSpellCheckTest, or the inherited-checkbox refusal mode. Buttons and
drafts use native automation; physical input and picker QA remain separate.

SableInheritedCheckBoxTest=true with SableTabsTest=true runs the mixed-field
check with a pure checkbox widget inheriting /Ff 6, /V /Other and
/Opt [(Approved now)]. It verifies one control at index zero and checks
unchanged and checked Apply refusals, retained editable drafts and errors,
modal commands, Cancel and the live unchecked state.
Supported text, radio, combo and list edits still apply and survive a saved
copy beside the untouched checkbox, note and bookmark. On reopen, the
checkbox editor is canceled; the other editors check unchanged Apply.
The source bytes must stay intact. This diagnostic takes precedence over
the inherited-text, multiline and comb modes. It uses native automation
peers and a supplied save path; actual input and the picker need desktop QA.

The 2 Nov inherited-checkbox desktop pass refused unchanged Apply, then
refused a checked draft and retained its check and error. Ctrl+W left the
modal intact; Cancel discarded the draft. A neighboring text edit to
Neighbor café applied and saved through Windows Save As to a spaced path.
The picker lost cached filename-element access; typing into its visibly
selected filename and pressing Return completed the save. Fresh Sable
restored the text and showed the checkbox unchecked. Acrobat showed the
accented text, unchecked required field, original note and unchanged radio
and choice appearances without field focus. Source and saved hashes stayed
unchanged. Font Capture again reported 0xc06d007e; dismissing it allowed the
appearance check. This verifies refusal and preservation, not parent writes.

SableNoSpellCheckTest=true with SableTabsTest=true checks text flag bit 22
and the native editor's disabled spell checking on every open, including
the saved copy. It checks retained overlength drafts, Cancel, no-op, Apply,
modal commands and save/reopen beside the other fields. Combine it with
SableMultilineTest, SableCombTest or SableInheritedTextTest for those text
variants, or SableInheritedCheckBoxTest for checkbox refusals beside it.
Without the flag, the same checks require spell checking to stay enabled.

The 2 Nov desktop comparison typed teh qqqzx café and a trailing space into
the unflagged and DoNotSpellCheck editors. The unflagged draft showed red
spelling underlines; the flagged draft showed none. Neither changed the
typed words, so this does not establish an autocorrection difference. The
unflagged draft was canceled. The flagged value applied and saved through
Windows Save As to a path with spaces. Fresh Sable restored the accented
text; unchanged Apply closed cleanly. Acrobat displayed it without field
focus beside the original note and untouched controls. Source and saved
hashes stayed unchanged. Font Capture reported exception 0xc06d007e;
after dismissing it, the appearance check passed. Its cause is unconfirmed.

SableMultilineTest=true with SableTabsTest=true uses the same mixed-field
check with a taller multiline field and a length limit. It checks wrapping,
initial raw LF no-ops, Unicode and blank paragraphs, retained overlong
drafts, Cancel, modal commands and save/reopen with all five field types.
The native text draft is assigned directly. In a desktop pass, press Enter
to insert paragraphs, include an empty line, and check that Enter leaves
the dialog open. Apply and save a copy, reopen it, and compare unfocused
paragraph appearances in another reader.

The 29 Oct desktop pass inserted two real Enter presses between accented
text and a third line. Enter left the editor open, and Ctrl+Tab/Ctrl+W kept
the draft. Apply displayed the empty paragraph on the page. The Windows
picker canceled once, then saved to a path with spaces. A fresh Sable
session restored the paragraphs; unchanged Apply closed without a save
prompt. Original bytes stayed unchanged. Acrobat displayed the accented
text and empty paragraph without field focus, beside the original note.
Its recurring Font Capture application error also appeared on this copy;
the cause remains unconfirmed.

SableCombTest=true with SableTabsTest=true runs the mixed-field check with
a four-cell text field. It checks a retained five-character refusal, corrected
draft cancellation, exact-length accented text, no-op/Apply and save/reopen
beside the other field types. The native dialog stays single-line. In a
desktop pass, type a boxed code, exceed its cells once, correct the draft,
save a copy and compare unfocused glyph placement and cell borders in
another reader. Stored Unicode values alone do not prove glyph appearance.

The 29 Oct comb desktop pass refused 12345 in the four-cell field and kept
the draft editable. Correcting it to café in the same dialog then applied
successfully; Ctrl+Tab/Ctrl+W retained the corrected draft. Windows Save As
canceled once, then saved to a path with spaces. A fresh Sable session
restored one glyph per cell; unchanged Apply stayed clean. Acrobat showed
all four accented-text glyphs and cell borders without field focus, beside
the original note. Source and saved-file hashes stayed unchanged during
reopen/viewing. Font Capture still reported its recurring application error;
its cause remains unconfirmed.

SableInheritedTextTest=true with SableTabsTest=true runs the mixed-field
check with a pure text widget inheriting its name, value, flags and MaxLen.
It checks retained overlength drafts, single-line input, field identity,
Cancel/no-op/Apply and save/reopen beside every other supported field type.
The existing native dialog supplies the edit; no separate parent editor is
needed. For desktop QA, type an accented value, save a copy, reopen it and
check unfocused appearances in another reader. Shared widgets and inherited
Comb remain outside this path.

Also set SableMultilineTest=true to run inherited paragraphs in the existing
wrapping editor. This combined mode checks raw no-ops, blank lines, Unicode,
retained parent-limit refusals and saved identity beside the other fields.
For desktop QA, insert real Enter presses, save and reopen a copy, and check
unfocused paragraph appearances in another reader.

The 29 Oct inherited-paragraph desktop pass kept a refused 33-character
draft editable against the parent's limit of 32. In the same dialog I typed
Inherited café, pressed Enter twice, and added third. Enter left the editor
open; Ctrl+Tab/Ctrl+W kept the draft. Apply and picker Cancel/spaced-path save
passed. A fresh Sable session restored the blank paragraph; unchanged Apply
closed without a save prompt. Acrobat displayed the accented paragraphs
with focus on the page, beside the original note and untouched fields.
Source and saved-copy hashes stayed intact. Font Capture failed again,
this time reporting unreadable memory; its cause remains unconfirmed.

The 29 Oct inherited-text desktop pass refused a 33-character draft against
the parent's 32-character limit and kept it editable. Correcting the same
draft to Inherited café applied successfully; Ctrl+Tab/Ctrl+W retained it.
Windows Save As canceled once, then saved to a path with spaces. A fresh
Sable session restored the value; unchanged Apply closed without a save
prompt. Acrobat showed the accented text with focus on the page, beside
the original note and untouched field states. Source and saved-file hashes
stayed unchanged during reopen/viewing. Font Capture reported its recurring
application error again; its cause remains unconfirmed.

The 29 Oct mixed-field desktop pass applied Unicode text, Space on the
checkbox and an unselected radio option, Down on the combo, and a pointer
selection of the third list option. Ctrl+Tab and Ctrl+W kept the text draft.
The Windows Save As dialog canceled once, then saved to a path with spaces.
The copy reopened in Sable with the edits; the source hash stayed unchanged.

Acrobat read the saved values and original note, but text and list contents
appeared only while focused. Their unfocused appearances remain unverified.
Its Font Capture process reported an application error on both the saved
copy and the unedited fixture. I haven't established whether the fixture's
font resources or this machine account for that behavior. Check persisted
appearances with explicit font resources before calling interoperability done.

The same-day follow-up uses an indirect Helvetica font with explicit
WinAnsiEncoding in the mixed fixture. Its saved accented text and list
selection stay visible in Acrobat without field focus. The old direct-font
fixture remains a comparison case. A regression reads saved AP font objects
before form setup/rendering and checks later choice edits keep them intact.
This corrects the synthetic fixture; the writer guards are unchanged.
Font Capture still reports an application error with the corrected file,
so that machine/application issue remains separate from the visible fields.

Open tests/fixtures/attachments.pdf and use Ctrl+S. Cancel once, then save to
a new name. The original tab and reading state should remain open. Compare
the file hashes to check the copy is exact:

```powershell
Get-FileHash .\tests\fixtures\attachments.pdf -Algorithm SHA256
Get-FileHash 'C:\path\to\your\copy.pdf' -Algorithm SHA256
```

Try the source path as the destination: Sable should ask for another name.
Check the Windows overwrite confirmation using a disposable copy. Open
Document properties and check Escape to dismiss; reader shortcuts should
not act on the PDF behind the dialog. Check actual text copy/paste with a
scratch text editor, using a short selection from text.pdf.

## Printing

The 26 Oct desktop follow-up checked the extracted e01a5a2 package's real
Windows preview on print.pdf. Portrait/landscape retained original colors,
included the printable annotation and omitted the non-printing annotation.
Cancel and reopening passed. Print reached the PDF driver's output picker;
that picker was canceled after the desktop runner lost filename focus.
Completed output and custom ranges through the modern dialog remain open.
This later pass narrows the historical dialog limit below; it does not
replace the earlier automated driver-output check.

A later attempt set the output filename, but the desktop runner could not
activate Save because of unavailable geometry and a window-owner mismatch.
The pages.pdf dialog also accepted range 2-3 and landscape. No PDF was
created; completed output and its range/orientation results remain open.

The 19 Oct reliability pass isolated two causes of the earlier crashes. A
retained print renderer could release empty session state through a destroyed
engine. That state now releases without touching the old engine. Separately,
the test runner shut down COM between tests while WinRT still cached print
factories. It now keeps its apartment for the full run and waits for complete
job teardown. The sanitizer reproduced both failures before the fixes.

Fifty shuffled print repetitions passed after the fixes, followed by three
complete sanitizer suites and five complete runs each of Debug and Release.
Each full suite passed 453 tests with the existing hard-link skip. No print
tests were excluded. The earlier printing blocker has passed its regression
checks; physical printing and dialog interaction remain unverified.

The opt-in driver check passed again on 19 Oct in Debug and Release. Real
Direct2D/XPS output went through Microsoft's installed PDF driver and was
reopened with PDFium. Portrait/landscape sizes, selected pages, original
colors, annotations and rotation matched. This checks file output; it does
not exercise input in the Windows print dialog.

Open tests/fixtures/print.pdf. Use Ctrl+P and check the preview before printing.
Choose Microsoft Print to PDF explicitly, save to a new file, then reopen it.
Check original colors, annotations and page fit. With pages.pdf, try a custom
range, landscape and cancellation. Copies/collation need a suitable printer;
keep those separate from a virtual PDF output check.

To repeat the driver check explicitly, run the test executable with:

```powershell
& .\x64\Release\Sable.Tests.exe `
  --gtest_also_run_disabled_tests `
  --gtest_filter=PrintJobTest.DISABLED_MicrosoftPdfOutputKeepsColorsRangesAndOrientation
```

The check targets only Microsoft Print to PDF and uses a unique temp folder.
It prints the folder path and keeps colors.pdf and range.pdf for review.
The normal suite does not run printer jobs. Sable's Direct2D/XPS output is
fed to the driver through the older XPS job API so the diagnostic can supply
an explicit filename; the reader keeps its modern Windows print UI. The
options are supplied by the test, so actual dialog choices, cancellation and
copies/collation still need the desktop pass above.

## Music and device changes

Play Spotify, change tracks, pause and seek, then repeat with Apple Music.
With both open, the playing session should win; old metadata/art should not
replace a newer track. Check controls, seeking, progress and readable acrylic
with bright, dark and missing artwork. A missing broker/player hides the
music button.

Move a reading window between monitors with different scaling. Check page
fit, text clarity, pointer/selection alignment and return to the first screen.
Press Shift+Alt+M a few times on home and in a document: the green MORTY!
alerts pile up and fade out one by one.

Press Shift+Alt+T to open Night Run. Space or Up starts the run and hops.
Steer with Left / Right or A / D. Collect three pages for a shield, then
hit a rock: the shield goes away and the run continues. A second rock
ends it. Check Restart clears distance, pages and shield; Mute silences
jump and crash. Switch to a PDF or another window and return to the paused
run. Press Shift+Alt+T to select the same tab, then Ctrl+W to close it.

## Automated input check

The native diagnostic build has SableKeyboardInputTest=true together with
SableTabsTest=true. It sends complete key chords through Windows only while
its own reader window is in the foreground and no input modifier/button is
held. Losing the foreground stops the check. This covers input delivery,
page drafts, zoom, Find, tools, presentation and modal isolation. It uses a
cancelled save-picker callback; the real save dialog still needs the pass
above. Windows may reject synthetic input in a restricted session. Such a
run is blocked, not evidence that keyboard delivery passed.

The restricted-session run on 18 Oct accepted 0/2 events with access denied
(5) before the first key reached the reader. The desktop input pass remains
pending; the separate native command checks pass.

The full Tabs check on 19 Oct passed steps 0-13, then Windows denied access
to the storage objects needed for its drop fixture at step 14. Explorer
drops and file-picker input still need a desktop run. SableWindowsTest=true
with SableTabsTest=true runs the independent window checks; these pass
startup sizing, window isolation, pending close, settings ownership and
shared preferences without claiming that the blocked drop passed.
