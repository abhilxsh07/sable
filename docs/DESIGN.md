# Design

Sable started as a straight port of NightPDF. It's aiming a bit higher now: a
clean, fast desktop reader with the tools you'd expect from a proper PDF app
(roughly what Acrobat Reader gives you for free, plus a few things it charges
for), without losing NightPDF's whole point, the night filter.

Everything runs locally. No accounts, no cloud, no upsells.

## What stays from NightPDF

- The four presets (Default, Sepia, Redeye, Custom) and the six sliders, with
  the same values and the same filter math. The filter only touches the page,
  never the UI, thumbnails or printouts.
- Tabs and how they behave, the keyboard shortcuts, the settings file keys and
  the command line.
- The splash screen when nothing is open.

## Layout

Loosely inspired by Acrobat's current layout, built from WinUI controls and
Windows' own Fluent icons. No copied icons or branding.

```
+-----------------------------------------------------------------------+
| [Menu]  [tab][tab][+]          [music] Default Sepia Redeye Custom _ □ x|  title bar
+-----------------------------------------------------------------------+
| [All tools]                          find  save  print   ...          |  command bar
+------+----+--------------------------------------------------+--------+
| All  |sel |                                                  | thumbs |
| tools|hand|                                                  | marks  |
| list |note|                 page(s)                          | notes  |  right rail
|      |hl  |                                                  | files  |
|      |draw|                                                  |        |
|      |text|                                                  | 36/128 |
|      |sign|                                                  |  - +   |
+------+----+--------------------------------------------------+--------+
```

- **Title bar**: a Menu button (replaces NightPDF's File / Edit / View /
  Window / Help bar, same items inside, same shortcuts), the tabs, the music
  button, and the preset buttons on the right where they always were.
- **Command bar**: the All tools toggle on the left, find / save / print and an
  overflow menu on the right. Only shows while a document is open.
- **All tools panel** (left, collapsible): every tool in one list, grouped.
- **Quick tools strip**: a slim floating column next to the page with the tools
  you reach for constantly (select, hand, sticky note, highlight, draw, text,
  sign).
- **Right rail**: thumbnails, bookmarks, comments, attachments and layers open
  a side panel. Page number and zoom live at the bottom of the rail.

Manrope, small corner radii and quiet hover states carry through the app.

## Appearance

Dark is the default, with NightPDF's greys (#1e1e1e and #2d2d2d). Settings >
General > Theme also offers **Obsidian (AMOLED)** for a true-black look.

Obsidian should feel quiet and consistent across the whole app. Use solid
#000000 for resting backgrounds: the reader canvas, title and command bars,
side panels, Settings, menus, dialogs and the music pane. Avoid grey cards,
artwork gradients and acrylic that make the background glow again.

Black surfaces still need clear controls. Use spacing and subtle borders to
separate groups, and keep text and icons readable. Hover, focus and selected
states should be visible without becoming bright blocks. Buttons should
share simple shapes, consistent heights and restrained styling.

The theme changes the app around the document. PDF page colours, night
filters, filter previews, album art and print output keep their existing
behaviour. A theme change should appear in open windows straight away and
carry through to windows opened later, without reopening the document.

Settings follows the same approach: a quiet sidebar, clear page headings,
short descriptions and aligned controls. Keep the pages uncluttered and let
them scroll in smaller windows. New surfaces should use the shared theme
colours so they stay consistent in both themes.

## Tools

The reader supports two-finger trackpad scrolling and pinch to zoom. Let
Windows handle the motion and inertia, including sideways movement across
a magnified page. Pinching should keep the reading position steady and
leave the PDF sharp when the gesture ends. The displayed percentage,
zoom buttons and fit presets should agree with the gesture. Presentation
keeps its fitted page view.

Choosing a page during a pinch should still land on that page when zoom
settles. Rotation, layout changes and presentation entry are available once
the gesture finishes; they are not queued during it. Returning to a tab must
not leave its zoom controls blocked by a completed pan.

In Page scrolling with odd/even spreads, View > Click pages to turn spreads
offers optional page turning. Click the left page to go back or the right
page to go forward a whole spread. A single cover or last page uses its two
halves. Keep links and text selection usable, and ignore clicks in the margins.
The option starts off and stays with its document tab.

| Group | Tools |
|---|---|
| View | zoom presets (fit page, fit width, actual size, 50-400%), single and two-page view, scroll modes, rotate view, full screen, presentation mode |
| Navigate | thumbnails, bookmarks, page labels, attachments, layers, go to page, find (match case, whole words, highlight all) |
| Read | text selection and copy, links, Read Aloud (Windows' built-in voices) |
| Comment | sticky notes, highlight, underline, strikethrough, text box, freehand drawing, shapes, stamps, a comments list |
| Fill & Sign | fill form fields, add text, ticks and crosses, a drawn or typed signature (a visual one, not a certificate signature) |
| Organize pages | rotate, delete, reorder, insert, extract |
| Combine files | merge several PDFs into one |
| Export | pages as PNG or JPEG, the document's text |
| File | save (annotations included), save as, print, document properties, password-protected files |

Not doing, at least for now: OCR, redaction, adding passwords, compression,
cloud sharing and e-signature requests. Either the licences don't fit a GPLv2
app, PDFium can't do them properly, or they need a server.

## Music

Pausing or skipping a song shouldn't mean leaving the document. A music
button sits in the title bar, just left of the presets, and only shows up
while Spotify or Apple Music has something playing or paused. Clicking it
drops down a small pane with:

- the album art, the song, the artist and which app is playing
- previous, play/pause and next
- a seek bar with the elapsed and remaining time
- shuffle and repeat, when the app lets them be changed from outside

In Dark, the pane is frosted glass tinted with the colour of the current
song's art, and the tint fades over when the track changes. The colour is
adjusted to keep the text readable, with NightPDF's grey as the fallback
when a song has no art. In Obsidian, the background is solid #000000; the
artwork stays visible, but its colours don't tint the pane.
Clicking anywhere else or pressing Esc closes it, like the configurator.

It goes through Windows' own media controls (the ones behind the media
flyout), so it can only do what the player exposes there. If an app doesn't
take seek requests, the seek bar still shows the time but can't be dragged.
No volume slider: Windows doesn't offer per-app volume through the same
controls. Other players, like a browser tab or VLC, are left alone for now.

## Build order

1. Core logic: settings, keybinds, presets, filter math, command line
2. PDF engine: PDFium on its own thread, opening files, rendering pages
3. Viewer: layout, zoom, navigation, the night filter on the GPU
4. Shell: title bar, tabs, Menu, presets, settings window, splash, drag and drop
5. Music pane: Spotify and Apple Music controls in the title bar
6. Reading tools: find, text selection, thumbnails, bookmarks, properties, print
7. Comments, then Fill & Sign, then saving annotations back to the file
8. Organize pages, combine files, export
9. Read Aloud, presentation mode, the rest of the polish
