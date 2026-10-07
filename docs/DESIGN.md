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
| [Menu]  [tab][tab][+]                  Default Sepia Redeye Custom _ □ x|  title bar
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
  Window / Help bar, same items inside, same shortcuts), the tabs, and the
  preset buttons on the right where they always were.
- **Command bar**: the All tools toggle on the left, find / save / print and an
  overflow menu on the right. Only shows while a document is open.
- **All tools panel** (left, collapsible): every tool in one list, grouped.
- **Quick tools strip**: a slim floating column next to the page with the tools
  you reach for constantly (select, hand, sticky note, highlight, draw, text,
  sign).
- **Right rail**: thumbnails, bookmarks, comments, attachments and layers open
  a side panel. Page number and zoom live at the bottom of the rail.

Look: NightPDF's greys (#1e1e1e, #2d2d2d), Manrope, small corner radii, quiet
hover states. Dark by default since that's the point of the app.

## Tools

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

## Build order

1. Core logic: settings, keybinds, presets, filter math, command line
2. PDF engine: PDFium on its own thread, opening files, rendering pages
3. Viewer: layout, zoom, navigation, the night filter on the GPU
4. Shell: title bar, tabs, Menu, presets, settings window, splash, drag and drop
5. Reading tools: find, text selection, thumbnails, bookmarks, properties, print
6. Comments, then Fill & Sign, then saving annotations back to the file
7. Organize pages, combine files, export
8. Read Aloud, presentation mode, the rest of the polish
