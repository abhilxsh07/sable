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
#include "layout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace helpers {
namespace {
bool nonnegative(double value) {
	return std::isfinite(value) && value >= 0;
}
} // namespace

PageLayout::PageLayout(std::span<const PageSize> sizes, double zoom,
                       double viewportWidth, double gap, double margin) {
	if (!std::isfinite(zoom) || zoom <= 0 || !nonnegative(viewportWidth) ||
	    !nonnegative(gap) || !nonnegative(margin)) {
		throw std::invalid_argument("invalid page layout dimensions");
	}
	m_width = viewportWidth;
	if (sizes.empty()) {
		return;
	}
	// points use 72 units per inch, DIPs use 96, just like CSS pixels
	// see:
	// https://github.com/mozilla/pdf.js/blob/v4.2.67/src/display/display_utils.js
	const double scale = zoom * (96.0 / 72.0);
	double top = margin;
	m_pages.reserve(sizes.size());
	for (const auto& size : sizes) {
		if (!std::isfinite(size.width) || size.width <= 0 ||
		    !std::isfinite(size.height) || size.height <= 0) {
			throw std::invalid_argument("invalid PDF page size");
		}
		const double width = size.width * scale;
		const double height = size.height * scale;
		const double outerWidth = width + 2 * margin;
		const double bottom = top + height;
		if (!std::isfinite(outerWidth) || !std::isfinite(bottom) ||
		    width <= 0 || height <= 0 || bottom <= top) {
			throw std::overflow_error("page layout is out of range");
		}
		m_pages.push_back({0, top, width, height});
		m_width = std::max(m_width, outerWidth);
		m_height = bottom + margin;
		top = bottom + gap;
	}
	if (!std::isfinite(m_height)) {
		throw std::overflow_error("page layout is out of range");
	}
	for (auto& page : m_pages) {
		page.x = (m_width - page.width) / 2;
	}
}

PageRange PageLayout::visiblePages(double top, double height,
                                   std::size_t overscan) const {
	const double bottom = top + height;
	if (m_pages.empty() || !std::isfinite(top) || !std::isfinite(height) ||
	    !std::isfinite(bottom) || height <= 0 || bottom <= 0 ||
	    top >= m_height) {
		return {};
	}
	// the stack is sorted at both edges, so scrolling doesn't scan every page
	const auto first = std::partition_point(
	    m_pages.begin(), m_pages.end(),
	    [top](const PageRect& page) { return page.y + page.height <= top; });
	const auto end = std::partition_point(
	    first, m_pages.end(),
	    [bottom](const PageRect& page) { return page.y < bottom; });
	const auto firstIndex = static_cast<std::size_t>(first - m_pages.begin());
	const auto endIndex = static_cast<std::size_t>(end - m_pages.begin());
	return {firstIndex - std::min(overscan, firstIndex),
	        endIndex + std::min(overscan, m_pages.size() - endIndex)};
}

PixelRect toPixels(const PageRect& rect, double scale) {
	const auto edge = [scale](double dips) {
		const double pixels = std::round(dips * scale);
		if (!std::isfinite(pixels) ||
		    pixels < std::numeric_limits<int>::min() ||
		    pixels > std::numeric_limits<int>::max()) {
			throw std::overflow_error("page edge doesn't fit in an int");
		}
		return static_cast<int>(pixels);
	};
	const int left = edge(rect.x);
	const int top = edge(rect.y);
	return {left, top, edge(rect.x + rect.width) - left,
	        edge(rect.y + rect.height) - top};
}

std::vector<std::size_t> renderOrder(PageRange visible, PageRange overscanned) {
	std::vector<std::size_t> order;
	for (auto page = visible.first; page < visible.end; ++page) {
		order.push_back(page);
	}
	for (auto page = visible.end; page < overscanned.end; ++page) {
		order.push_back(page);
	}
	for (auto page = visible.first; page > overscanned.first; --page) {
		order.push_back(page - 1);
	}
	return order;
}

std::size_t PageLayout::pageAt(double y) const {
	const auto below = std::partition_point(
	    m_pages.begin(), m_pages.end(),
	    [y](const PageRect& page) { return page.y + page.height <= y; });
	if (below == m_pages.end()) {
		return m_pages.size() - 1;
	}
	return static_cast<std::size_t>(below - m_pages.begin());
}

PixelRect renderSize(int width, int height, long long maxPixels, int maxSide) {
	if (width <= 0 || height <= 0) {
		return {0, 0, (std::max)(width, 0), (std::max)(height, 0)};
	}
	double scale = 1;
	const double pixels = static_cast<double>(width) * height;
	if (pixels > static_cast<double>(maxPixels)) {
		scale = std::sqrt(static_cast<double>(maxPixels) / pixels);
	}
	scale = (std::min)({scale, static_cast<double>(maxSide) / width,
	                    static_cast<double>(maxSide) / height});
	if (scale >= 1) {
		return {0, 0, width, height};
	}
	return {0, 0, (std::max)(1, static_cast<int>(std::floor(width * scale))),
	        (std::max)(1, static_cast<int>(std::floor(height * scale)))};
}
} // namespace helpers
