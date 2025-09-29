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
#include "text.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace pdf {
std::pair<TextPosition, TextPosition> ordered(const TextSelection& s) {
	return s.focus < s.anchor ? std::pair{s.focus, s.anchor}
	                          : std::pair{s.anchor, s.focus};
}
std::pair<int, int> textSpan(const TextSelection& s, int page, int count) {
	const auto [first, last] = ordered(s);
	if (count <= 0 || page < first.page || page > last.page) {
		return {};
	}
	const int begin =
	    page == first.page ? std::clamp(first.index, 0, count) : 0;
	const int end = page == last.page && last.index < count
	                    ? std::max(0, last.index + 1)
	                    : count;
	return {begin, std::max(begin, end)};
}
namespace {
bool valid(const TextChar& c) {
	const auto& b = c.box;
	return c.unicode && c.unicode != U'\r' && c.unicode != U'\n' &&
	       std::isfinite(b.left) && std::isfinite(b.right) &&
	       std::isfinite(b.top) && std::isfinite(b.bottom) && b.left >= 0 &&
	       b.top >= 0 && b.right <= 1 && b.bottom <= 1 && b.right > b.left &&
	       b.bottom > b.top;
}
} // namespace
std::optional<int> hitText(const TextPage& page, double x, double y,
                           double width, double height, bool nearest) {
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
	    !std::isfinite(height) || width <= 0 || height <= 0) {
		return {};
	}
	std::optional<int> best;
	double distance = nearest ? std::numeric_limits<double>::infinity() : 36.;
	for (std::size_t i = 0; i < page.characters.size(); ++i) {
		const auto& c = page.characters[i];
		if (!valid(c)) {
			continue;
		}
		const auto& b = c.box;
		const double dx = (x - std::clamp(x, b.left, b.right)) * width;
		const double dy = (y - std::clamp(y, b.top, b.bottom)) * height;
		const double squared = dx * dx + dy * dy;
		if (squared <= distance) {
			best = static_cast<int>(i);
			distance = squared;
			if (squared == 0) {
				break;
			}
		}
	}
	return best;
}
std::vector<TextRect> textRects(const TextPage& page, int first, int end) {
	std::vector<TextRect> result;
	first = std::clamp(first, 0, static_cast<int>(page.characters.size()));
	end = std::clamp(end, first, static_cast<int>(page.characters.size()));
	for (int i = first; i < end; ++i) {
		const auto& c = page.characters[i];
		if (!valid(c)) {
			continue;
		}
		const auto& b = c.box;
		if (!result.empty()) {
			auto& a = result.back();
			const double height = std::max(a.bottom - a.top, b.bottom - b.top);
			const double width = std::max(a.right - a.left, b.right - b.left);
			const bool horizontal =
			    std::abs(a.top - b.top) < height * .15 &&
			    std::abs(a.bottom - b.bottom) < height * .15 &&
			    std::max(a.left, b.left) - std::min(a.right, b.right) <=
			        height * .3;
			const bool vertical =
			    std::abs(a.left - b.left) < width * .15 &&
			    std::abs(a.right - b.right) < width * .15 &&
			    std::max(a.top, b.top) - std::min(a.bottom, b.bottom) <=
			        width * .3;
			if (horizontal || vertical) {
				a = {std::min(a.left, b.left), std::min(a.top, b.top),
				     std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
				continue;
			}
		}
		result.push_back(b);
	}
	return result;
}
std::string textUtf8(std::u32string_view text) {
	std::string result;
	for (std::size_t i = 0; i < text.size(); ++i) {
		char32_t c = text[i];
		if (!c) {
			continue; // PDFium could not map this character.
		}
		if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() &&
		    text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
			c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
		}
		if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
			c = 0xFFFD;
		}
		if (c < 0x80) {
			result += static_cast<char>(c);
		} else if (c < 0x800) {
			result += static_cast<char>(0xC0 | (c >> 6));
			result += static_cast<char>(0x80 | (c & 63));
		} else if (c < 0x10000) {
			result += static_cast<char>(0xE0 | (c >> 12));
			result += static_cast<char>(0x80 | ((c >> 6) & 63));
			result += static_cast<char>(0x80 | (c & 63));
		} else {
			result += static_cast<char>(0xF0 | (c >> 18));
			result += static_cast<char>(0x80 | ((c >> 12) & 63));
			result += static_cast<char>(0x80 | ((c >> 6) & 63));
			result += static_cast<char>(0x80 | (c & 63));
		}
	}
	return result;
}
} // namespace pdf
