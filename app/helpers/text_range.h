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
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace helpers {
// UI Automation's text units, in its order, so the values cast across.
enum class TextUnit {
	Character,
	Format,
	Word,
	Line,
	Paragraph,
	Page,
	Document
};

// A page's text for screen readers: PDFium's CRLF line ends become "\n" and
// the form feed it puts between pages is dropped.
std::wstring screenReaderText(std::wstring_view text);

// [start, end) of the unit holding pos. Format, Page and Document are the
// whole text here; a position at the end belongs to the last unit.
std::pair<std::size_t, std::size_t> unitAround(std::wstring_view text,
                                               std::size_t pos, TextUnit unit);
// pos moved over count unit starts, backwards for a negative count,
// stopping at either end. moved says how many it crossed.
std::size_t moveByUnit(std::wstring_view text, std::size_t pos, TextUnit unit,
                       int count, int& moved);
// the first (or with backward, last) match of needle inside [start, end)
std::optional<std::pair<std::size_t, std::size_t>>
findText(std::wstring_view text, std::size_t start, std::size_t end,
         std::wstring_view needle, bool backward, bool ignoreCase);
} // namespace helpers
