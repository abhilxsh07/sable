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
#include <compare>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdf {
struct TextRect {
	double left = 0, top = 0, right = 0, bottom = 0;
	bool operator==(const TextRect&) const = default;
};
struct TextChar {
	char32_t unicode = 0;
	TextRect box; // fractions of the displayed page, after crop/rotation
};
struct TextPage {
	std::vector<TextChar> characters;
};
struct TextPosition {
	int page = 0, index = 0;
	auto operator<=>(const TextPosition&) const = default;
};
struct TextSelection {
	TextPosition anchor, focus; // inclusive character indices
	bool operator==(const TextSelection&) const = default;
};
std::pair<TextPosition, TextPosition> ordered(const TextSelection& selection);
// A half-open character range for one page; count clips the end sentinel.
std::pair<int, int> textSpan(const TextSelection& selection, int page,
                             int count);
std::optional<int> hitText(const TextPage& page, double x, double y,
                           double width, double height, bool nearest = false);
std::vector<TextRect> textRects(const TextPage& page, int first, int end);
std::string textUtf8(std::u32string_view text);
} // namespace pdf
