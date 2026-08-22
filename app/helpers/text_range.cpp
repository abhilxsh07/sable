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
#include "text_range.h"

#include <algorithm>
#include <cwctype>

namespace helpers {
namespace {
bool space(wchar_t c) {
	return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == 0xa0 ||
	       c == 0x3000;
}

bool lowSurrogate(wchar_t c) {
	return c >= 0xdc00 && c <= 0xdfff;
}

// whether a unit starts at i, for 0 < i < size
bool boundary(std::wstring_view text, std::size_t i, TextUnit unit) {
	switch (unit) {
	case TextUnit::Character:
		return !lowSurrogate(text[i]);
	case TextUnit::Word:
		// a word keeps the spaces after it, as UI Automation expects
		return !space(text[i]) && space(text[i - 1]);
	case TextUnit::Line:
		return text[i - 1] == L'\n';
	case TextUnit::Paragraph:
		// a blank line ends a paragraph; PDFium rarely gives one, so a
		// page is often one paragraph
		return i >= 2 && text[i - 1] == L'\n' && text[i - 2] == L'\n' &&
		       text[i] != L'\n';
	default:
		return false;
	}
}

std::size_t previous(std::wstring_view text, std::size_t pos, TextUnit unit) {
	while (pos > 0 && !boundary(text, pos, unit)) {
		--pos;
	}
	return pos;
}

std::size_t next(std::wstring_view text, std::size_t pos, TextUnit unit) {
	++pos;
	while (pos < text.size() && !boundary(text, pos, unit)) {
		++pos;
	}
	return std::min(pos, text.size());
}
} // namespace

std::wstring screenReaderText(std::wstring_view text) {
	std::wstring out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); ++i) {
		const wchar_t c = text[i];
		if (c == L'\r') {
			if (i + 1 < text.size() && text[i + 1] == L'\n') {
				continue;
			}
			out += L'\n';
		} else if (c == L'\f' || c == 0) {
			continue;
		} else {
			out += c;
		}
	}
	return out;
}

std::pair<std::size_t, std::size_t> unitAround(std::wstring_view text,
                                               std::size_t pos, TextUnit unit) {
	if (text.empty()) {
		return {0, 0};
	}
	if (unit == TextUnit::Format || unit == TextUnit::Page ||
	    unit == TextUnit::Document) {
		return {0, text.size()};
	}
	pos = std::min(pos, text.size() - 1);
	const auto start = previous(text, pos, unit);
	return {start, next(text, start, unit)};
}

std::size_t moveByUnit(std::wstring_view text, std::size_t pos, TextUnit unit,
                       int count, int& moved) {
	moved = 0;
	pos = std::min(pos, text.size());
	const bool whole = unit == TextUnit::Format || unit == TextUnit::Page ||
	                   unit == TextUnit::Document;
	while (count > 0 && pos < text.size()) {
		pos = whole ? text.size() : next(text, pos, unit);
		++moved;
		--count;
	}
	while (count < 0 && pos > 0) {
		pos = whole ? 0 : previous(text, pos - 1, unit);
		--moved;
		++count;
	}
	return pos;
}

std::optional<std::pair<std::size_t, std::size_t>>
findText(std::wstring_view text, std::size_t start, std::size_t end,
         std::wstring_view needle, bool backward, bool ignoreCase) {
	end = std::min(end, text.size());
	if (needle.empty() || start > end || end - start < needle.size()) {
		return std::nullopt;
	}
	const auto same = [&](wchar_t a, wchar_t b) {
		return ignoreCase ? std::towlower(a) == std::towlower(b) : a == b;
	};
	const auto at = [&](std::size_t i) {
		for (std::size_t k = 0; k < needle.size(); ++k) {
			if (!same(text[i + k], needle[k])) {
				return false;
			}
		}
		return true;
	};
	const auto last = end - needle.size();
	if (backward) {
		for (std::size_t i = last + 1; i-- > start;) {
			if (at(i)) {
				return std::pair{i, i + needle.size()};
			}
		}
	} else {
		for (std::size_t i = start; i <= last; ++i) {
			if (at(i)) {
				return std::pair{i, i + needle.size()};
			}
		}
	}
	return std::nullopt;
}
} // namespace helpers
