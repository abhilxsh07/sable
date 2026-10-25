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
#include "navigation.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>
#include <utility>

namespace helpers {
namespace {
// how @fluent/bundle prints a number in en-US: 1234 becomes "1,234"
std::string grouped(std::size_t number) {
	const auto digits = std::to_string(number);
	std::string text;
	for (std::size_t i = 0; i < digits.size(); ++i) {
		if (i > 0 && (digits.size() - i) % 3 == 0) {
			text += ',';
		}
		text += digits[i];
	}
	return text;
}

bool isSpace(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
	       c == '\r';
}

// JavaScript's (text | 0) for the numbers someone would type: spaces around
// it, a sign, decimals and exponents, and 0x/0o/0b. NaN and infinities are
// 0, like there. ToInt32's wrap past 2^31 isn't copied, those are just
// nullopt; no document gets near that many pages.
std::optional<long long> jsInt(std::string_view text) {
	while (!text.empty() && isSpace(text.front())) {
		text.remove_prefix(1);
	}
	while (!text.empty() && isSpace(text.back())) {
		text.remove_suffix(1);
	}
	if (text.empty()) {
		return 0; // Number("") is 0
	}
	if (text.size() > 2 && text[0] == '0') {
		int base = 0;
		switch (text[1]) {
		case 'x':
		case 'X':
			base = 16;
			break;
		case 'o':
		case 'O':
			base = 8;
			break;
		case 'b':
		case 'B':
			base = 2;
			break;
		default:
			break;
		}
		if (base != 0) {
			long long value = 0;
			const auto* end = text.data() + text.size();
			const auto [ptr, error] =
			    std::from_chars(text.data() + 2, end, value, base);
			if (error != std::errc{} || ptr != end) {
				return error == std::errc::result_out_of_range
				           ? std::nullopt
				           : std::optional<long long>{0};
			}
			return value;
		}
	}
	bool negative = false;
	if (text.front() == '+' || text.front() == '-') {
		negative = text.front() == '-';
		text.remove_prefix(1);
	}
	// from_chars also takes "inf" and "nan" spelled other ways than JS
	// does, but they all come out as 0 either way
	double value = 0;
	const auto* end = text.data() + text.size();
	const auto [ptr, error] = std::from_chars(text.data(), end, value);
	if (error == std::errc::result_out_of_range) {
		return std::nullopt;
	}
	if (error != std::errc{} || ptr != end || !std::isfinite(value)) {
		return 0;
	}
	value = std::trunc(negative ? -value : value);
	if (std::abs(value) >= 2147483648.0) {
		return std::nullopt;
	}
	return static_cast<long long>(value);
}
} // namespace

std::vector<std::string> pageLabels(std::vector<std::string> labels,
                                    std::size_t count) {
	// pdf_viewer.js setPageLabels drops a list of the wrong length
	if (labels.empty() || labels.size() != count) {
		return {};
	}
	std::size_t standard = 0;
	std::size_t empty = 0;
	for (std::size_t i = 0; i < labels.size(); ++i) {
		if (labels[i] == std::to_string(i + 1)) {
			++standard;
		} else if (labels[i].empty()) {
			++empty;
		} else {
			break;
		}
	}
	if (standard >= labels.size() || empty >= labels.size()) {
		return {};
	}
	return labels;
}

// toolbar.js #updateUIState, with the en-US strings pdfjs-of-pages and
// pdfjs-page-of-pages
// see:
// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/l10n/en-US/viewer.ftl
PageText pageText(std::size_t page, std::size_t count,
                  std::span<const std::string> labels) {
	if (page < labels.size()) {
		return {labels[page],
		        "(" + grouped(page + 1) + " of " + grouped(count) + ")"};
	}
	return {std::to_string(page + 1), "of " + grouped(count)};
}

// pdf_thumbnail_view.js #pageL10nArgs: this.pageLabel ?? this.id
std::string pageName(std::size_t page, std::span<const std::string> labels) {
	if (page < labels.size()) {
		return labels[page];
	}
	return grouped(page + 1);
}

std::optional<std::size_t> pageFromText(std::string_view text,
                                        std::span<const std::string> labels,
                                        std::size_t count) {
	// pdf_viewer.js pageLabelToPageNumber: the first page with that label
	const auto label = std::find(labels.begin(), labels.end(), text);
	if (label != labels.end()) {
		return static_cast<std::size_t>(label - labels.begin());
	}
	const auto number = jsInt(text);
	if (!number || *number < 1 ||
	    static_cast<unsigned long long>(*number) > count) {
		return std::nullopt;
	}
	return static_cast<std::size_t>(*number - 1);
}

std::size_t currentPage(const PageLayout& layout, const Viewport& view,
                        std::size_t current) {
	// ui_utils.js getVisibleElements: how much of each page is in view, as
	// a whole percentage of the page. The browser measured in whole CSS
	// pixels (scrollTop, offsetTop, clientHeight), so a view that stops a
	// fraction past a page's edge still has all of that page.
	struct Seen {
		std::size_t page;
		int percent;
	};
	std::vector<Seen> seen;
	const double left = std::round(view.left);
	const double top = std::round(view.top);
	const double right = std::round(view.left + view.width);
	const double bottom = std::round(view.top + view.height);
	const auto range =
	    layout.visiblePages({left, top, right - left, bottom - top}, 0);
	for (auto page = range.first; page < range.end; ++page) {
		const auto& rect = layout.pages()[page];
		const double pageLeft = std::round(rect.x);
		const double pageTop = std::round(rect.y);
		const double pageRight = std::round(rect.x + rect.width);
		const double pageBottom = std::round(rect.y + rect.height);
		const double width = pageRight - pageLeft;
		const double height = pageBottom - pageTop;
		if (width <= 0 || height <= 0 || pageBottom <= top ||
		    pageTop >= bottom || pageRight <= left || pageLeft >= right) {
			continue;
		}
		const double hiddenHeight = (std::max)(0.0, top - pageTop) +
		                            (std::max)(0.0, pageBottom - bottom);
		const double hiddenWidth = (std::max)(0.0, left - pageLeft) +
		                           (std::max)(0.0, pageRight - right);
		const double fractionHeight = (height - hiddenHeight) / height;
		const double fractionWidth = (width - hiddenWidth) / width;
		seen.push_back(
		    {page, static_cast<int>(fractionHeight * fractionWidth * 100)});
	}
	if (seen.empty()) {
		return current;
	}
	// sortByVisibility: most visible first, and pages are already in order
	std::stable_sort(
	    seen.begin(), seen.end(),
	    [](const Seen& a, const Seen& b) { return a.percent > b.percent; });
	for (const auto& entry : seen) {
		if (entry.percent < 100) {
			break;
		}
		if (entry.page == current) {
			return current;
		}
	}
	return seen.front().page;
}

NavAction navAction(NavKey key, NavModifiers modifiers, NavFocus focus,
                    const NavState& state) {
	if (modifiers.windows) {
		return NavAction::None;
	}
	const bool hasPrevious = state.page > 0;
	const bool hasNext = state.page + 1 < state.count;
	// Ctrl, with or without Shift, works even in the page box (app.js
	// L2928-2945)
	if (modifiers.ctrl && !modifiers.alt) {
		if (key == NavKey::Up && hasPrevious) {
			return NavAction::First;
		}
		if (key == NavKey::Down && hasNext) {
			return NavAction::Last;
		}
		return NavAction::None;
	}
	// Ctrl+Alt+G selects the page box (L2978-2984)
	if (modifiers.ctrl && modifiers.alt) {
		return key == NavKey::G && !modifiers.shift ? NavAction::FocusPageBox
		                                            : NavAction::None;
	}
	// the rest is left to text fields, and Enter and Space to buttons
	// (L2996-3012)
	if (focus == NavFocus::TextBox ||
	    (focus == NavFocus::Button &&
	     (key == NavKey::Enter || key == NavKey::Space))) {
		return NavAction::None;
	}
	if (modifiers.alt) {
		return NavAction::None;
	}
	// app.js webViewerKeyDown uses R and Shift+R outside text fields
	if (key == NavKey::R) {
		return modifiers.shift ? NavAction::RotateCounterClockwise
		                       : NavAction::RotateClockwise;
	}
	// Shift+Space and Shift+Enter go back a page only in page-fit
	// (L3153-3166)
	if (modifiers.shift) {
		return (key == NavKey::Enter || key == NavKey::Space) && state.pageFit
		           ? NavAction::Previous
		           : NavAction::None;
	}
	if (key == NavKey::H) {
		return NavAction::HandTool;
	}
	if (key == NavKey::S) {
		return NavAction::SelectTool;
	}
	// no modifiers (L3014-3151). The scrolling keys only turn pages when
	// there's nothing to scroll that way, or in page-fit.
	int turn = 0;
	bool onlyIfPageFit = false;
	switch (key) {
	case NavKey::Up:
	case NavKey::PageUp:
		onlyIfPageFit = state.verticalScrollbar;
		turn = -1;
		break;
	case NavKey::Backspace:
		onlyIfPageFit = true;
		turn = -1;
		break;
	case NavKey::Left:
		onlyIfPageFit = state.horizontalScrollbar;
		turn = -1;
		break;
	case NavKey::K:
	case NavKey::P:
		turn = -1;
		break;
	case NavKey::Down:
	case NavKey::PageDown:
		onlyIfPageFit = state.verticalScrollbar;
		turn = 1;
		break;
	case NavKey::Enter:
	case NavKey::Space:
		onlyIfPageFit = true;
		turn = 1;
		break;
	case NavKey::Right:
		onlyIfPageFit = state.horizontalScrollbar;
		turn = 1;
		break;
	case NavKey::J:
	case NavKey::N:
		turn = 1;
		break;
	case NavKey::Home:
		return hasPrevious ? NavAction::First : NavAction::None;
	case NavKey::End:
		return hasNext ? NavAction::Last : NavAction::None;
	case NavKey::F4:
		return NavAction::ToggleSidebar;
	default:
		break;
	}
	if (turn == 0 || (onlyIfPageFit && !state.pageFit)) {
		return NavAction::None;
	}
	return turn > 0 ? NavAction::Next : NavAction::Previous;
}
} // namespace helpers
