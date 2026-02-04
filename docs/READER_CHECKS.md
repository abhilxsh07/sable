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
Notification display and click activation need a separate Windows check with
the opt-in EASTER_EGG setting described in the README.

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
