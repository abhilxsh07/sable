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
TextRect unite(const TextRect& a, const TextRect& b) {
	return {std::min(a.left, b.left), std::min(a.top, b.top),
	        std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
}
int axis(const TextChar& c) {
	const auto& b = c.baseline;
	if (!std::isfinite(b.x) || !std::isfinite(b.y) ||
	    !std::isfinite(b.height) || b.height <= 0 || !std::isfinite(b.dx) ||
	    !std::isfinite(b.dy)) {
		return 0;
	}
	if (std::abs(b.dx) > .99 && std::abs(b.dy) < .01) {
		return 1;
	}
	if (std::abs(b.dy) > .99 && std::abs(b.dx) < .01) {
		return 2;
	}
	return -1; // i leave skewed characters separate.
}
bool sameLine(const TextPage& page, const TextChar& start,
              const TextChar& previous, const TextChar& current,
              const TextRect& bounds) {
	const int direction = axis(start);
	if (direction != axis(current) || direction < 0) {
		return false;
	}
	const auto& a = previous.box;
	const auto& b = current.box;
	if (!direction) {
		const double height =
		    std::max(bounds.bottom - bounds.top, b.bottom - b.top);
		const double width =
		    std::max(bounds.right - bounds.left, b.right - b.left);
		return (std::abs(bounds.top - b.top) < height * .15 &&
		        std::abs(bounds.bottom - b.bottom) < height * .15 &&
		        std::max(a.left, b.left) - std::min(a.right, b.right) <=
		            height * .3) ||
		       (std::abs(bounds.left - b.left) < width * .15 &&
		        std::abs(bounds.right - b.right) < width * .15 &&
		        std::max(a.top, b.top) - std::min(a.bottom, b.bottom) <=
		            width * .3);
	}
	const auto& base = start.baseline;
	const auto& next = current.baseline;
	const double height = std::min(base.height, next.height);
	const bool horizontal = direction == 1;
	const double offset = horizontal ? (base.y - next.y) * page.height
	                                 : (base.x - next.x) * page.width;
	const double gap =
	    horizontal ? (std::max(a.left, b.left) - std::min(a.right, b.right)) *
	                     page.width
	               : (std::max(a.top, b.top) - std::min(a.bottom, b.bottom)) *
	                     page.height;
	return base.dx * next.dx + base.dy * next.dy > .99 &&
	       std::abs(offset) <= height * .05 &&
	       gap <= std::min(previous.baseline.height, next.height) * .75;
}
std::vector<int> lineOrder(const TextPage& page) {
	std::vector<int> order, block;
	order.reserve(page.characters.size());
	const auto kind = [&](int index) {
		const auto& c = page.characters[index];
		return axis(c) == 1 ? (c.baseline.dx < 0 ? 1 : 0)
		                    : (c.baseline.dy < 0 ? 3 : 2);
	};
	const auto cross = [&](int index) {
		const auto& c = page.characters[index];
		return axis(c) == 1 ? c.baseline.y * page.height
		                    : c.baseline.x * page.width;
	};
	const auto finish = [&] {
		// PDFium can reverse font runs without reversing their characters.
		std::sort(block.begin(), block.end(), [&](int a, int b) {
			if (kind(a) != kind(b)) {
				return kind(a) < kind(b);
			}
			return cross(a) != cross(b) ? cross(a) < cross(b) : a < b;
		});
		for (std::size_t start = 0; start < block.size();) {
			std::size_t end = start + 1;
			double height = page.characters[block[start]].baseline.height;
			for (; end < block.size(); ++end) {
				height = std::min(height,
				                  page.characters[block[end]].baseline.height);
				if (kind(block[start]) != kind(block[end]) ||
				    cross(block[end]) - cross(block[start]) > height * .05) {
					break;
				}
			}
			const bool horizontal = axis(page.characters[block[start]]) == 1;
			std::sort(block.begin() + start, block.begin() + end,
			          [&](int a, int b) {
				          const auto& x = page.characters[a].box;
				          const auto& y = page.characters[b].box;
				          const double p = horizontal ? x.left : x.top;
				          const double q = horizontal ? y.left : y.top;
				          return p != q ? p < q : a < b;
			          });
			order.insert(order.end(), block.begin() + start,
			             block.begin() + end);
			order.push_back(-1);
			start = end;
		}
		block.clear();
	};
	for (int i = 0; i < static_cast<int>(page.characters.size()); ++i) {
		const auto& c = page.characters[i];
		if (valid(c) && axis(c) > 0) {
			block.push_back(i);
		} else if ((c.unicode != U' ' && c.unicode != U'\t') || valid(c)) {
			finish();
			order.push_back(i);
		}
	}
	finish();
	return order;
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
	if (first == end || !std::isfinite(page.width) || page.width <= 0 ||
	    !std::isfinite(page.height) || page.height <= 0) {
		return result;
	}
	int start = -1, previous = -1;
	int selectedIndex = end;
	std::vector<std::pair<int, TextRect>> bands;
	TextRect bounds;
	std::optional<TextRect> selected;
	const auto finish = [&] {
		if (selected) {
			const int direction = axis(page.characters[start]);
			if (direction == 1) {
				selected->top = bounds.top;
				selected->bottom = bounds.bottom;
			} else if (direction == 2) {
				selected->left = bounds.left;
				selected->right = bounds.right;
			}
			bands.push_back({selectedIndex, *selected});
			selected.reset();
			selectedIndex = end;
		}
		start = previous = -1;
	};
	for (const int i : lineOrder(page)) {
		if (i < 0) {
			finish();
			continue;
		}
		const auto& c = page.characters[i];
		if (!valid(c)) {
			if (c.unicode != U' ' && c.unicode != U'\t') {
				finish();
			}
			continue;
		}
		if (start >= 0 && !sameLine(page, page.characters[start],
		                            page.characters[previous], c, bounds)) {
			finish();
		}
		if (start < 0) {
			start = i;
			bounds = c.box;
		} else {
			bounds = unite(bounds, c.box);
		}
		previous = i;
		if (i >= first && i < end) {
			selected = selected ? unite(*selected, c.box) : c.box;
			selectedIndex = std::min(selectedIndex, i);
		}
	}
	finish();
	std::sort(bands.begin(), bands.end(),
	          [](const auto& a, const auto& b) { return a.first < b.first; });
	for (const auto& band : bands) {
		result.push_back(band.second);
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
