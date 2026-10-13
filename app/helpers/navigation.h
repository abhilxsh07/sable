/*
Sable, a native port of NightPDF
Copyright (C) 2021  Advaith Madhukar
Copyright (C) 2026  Abhilash Kar

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
// what pdf.js v4.2.67 did to move between pages, which is what NightPDF's
// viewer did: the page box and its page labels, which page counts as the
// current one while scrolling, and which keys turn pages.
// see: https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/app.js
// see:
// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/pdf_viewer.js
// see: https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/toolbar.js
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "layout.h"

namespace helpers {
// the PDF's page labels (UTF-8, one per page), or none when pdf.js would
// have ignored them: a count that doesn't match, or labels that are only
// "1", "2", "3"... or only empty (app.js _initializePageLabels)
std::vector<std::string> pageLabels(std::vector<std::string> labels,
                                    std::size_t count);

// what the page box and the text next to it say for page (0-based)
struct PageText {
	std::string box;
	std::string total;
	bool operator==(const PageText&) const = default;
};

// "3" and "of 128", or with labels "iii" and "(3 of 128)". Counts are
// grouped like en-US numbers in pdf.js's strings ("of 1,000"), the box
// isn't.
PageText pageText(std::size_t page, std::size_t count,
                  std::span<const std::string> labels);

// the page (0-based) that what was typed in the page box goes to: a label
// first, then a number the way JavaScript's (text | 0) reads it, so " 7 "
// and "7.9" are page 7. nullopt if it's neither or past the last page
// (pdf_link_service.js goToPage).
std::optional<std::size_t> pageFromText(std::string_view text,
                                        std::span<const std::string> labels,
                                        std::size_t count);

// the viewport in the same DIPs as PageLayout
struct Viewport {
	double left = 0;
	double top = 0;
	double width = 0;
	double height = 0;
};

// pdf_viewer.js update(): the page with the largest share of itself in view,
// lowest first on a tie, except that current stays while it's still all in
// view. current is kept as well when nothing is in view. Edges are rounded
// to whole DIPs first, like the browser's CSS pixels.
std::size_t currentPage(const PageLayout& layout, const Viewport& view,
                        std::size_t current);

// the keys app.js webViewerKeyDown turns pages with
enum class NavKey {
	Other,
	Up,
	Down,
	Left,
	Right,
	PageUp,
	PageDown,
	Home,
	End,
	Space,
	Enter,
	Backspace,
	G,
	J,
	K,
	N,
	P,
};

struct NavModifiers {
	bool ctrl = false;
	bool alt = false;
	bool shift = false;
};

// where the keyboard focus is: the pages, a button or a text field
enum class NavFocus { Viewer, Button, TextBox };

struct NavState {
	std::size_t page = 0; // 0-based
	std::size_t count = 0;
	bool verticalScrollbar = false;
	bool horizontalScrollbar = false;
	// pdf.js's "page-fit" zoom, where the scrolling keys turn pages
	bool pageFit = false;
};

enum class NavAction { None, Previous, Next, First, Last, FocusPageBox };

// None leaves the key to the scroller. Previous on the first page and Next
// on the last still take the key, like pdf.js.
NavAction navAction(NavKey key, NavModifiers modifiers, NavFocus focus,
                    const NavState& state);
} // namespace helpers
