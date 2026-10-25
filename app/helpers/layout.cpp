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

PageRange spreadPages(std::size_t count, std::size_t page, SpreadMode spread) {
	if (page >= count || spread < SpreadMode::None ||
	    spread > SpreadMode::Even) {
		return {};
	}
	if (spread == SpreadMode::None ||
	    (spread == SpreadMode::Even && page == 0)) {
		return {page, page + 1};
	}
	const auto first =
	    spread == SpreadMode::Odd ? page - page % 2 : page - (page - 1) % 2;
	return {first, first + (std::min)(std::size_t{2}, count - first)};
}
PageSize spreadSize(std::span<const PageSize> sizes, std::size_t page,
                    SpreadMode spread) {
	const auto range = spreadPages(sizes.size(), page, spread);
	PageSize result{};
	for (auto i = range.first; i < range.end; ++i) {
		result.width += sizes[i].width;
		result.height = (std::max)(result.height, sizes[i].height);
	}
	return result;
}
PageLayout::PageLayout(std::span<const PageSize> sizes, double zoom,
                       double viewportWidth, double gap, double margin,
                       LayoutOptions options) {
	if (!std::isfinite(zoom) || zoom <= 0 || !nonnegative(viewportWidth) ||
	    !nonnegative(gap) || !nonnegative(margin) ||
	    !nonnegative(options.visibleWidth) ||
	    options.scroll < ScrollMode::Vertical ||
	    options.scroll > ScrollMode::Page ||
	    options.spread < SpreadMode::None ||
	    options.spread > SpreadMode::Even) {
		throw std::invalid_argument("invalid page layout dimensions");
	}
	m_width = viewportWidth;
	if (sizes.empty()) {
		return;
	}
	if (options.current >= sizes.size()) {
		throw std::invalid_argument("invalid current page");
	}
	m_horizontal = options.scroll == ScrollMode::Horizontal;
	const double scale = zoom * (96.0 / 72.0);
	const auto checked = [](double value) {
		if (!std::isfinite(value)) {
			throw std::overflow_error("page layout is out of range");
		}
		return value;
	};
	m_pages.reserve(sizes.size());
	for (const auto& size : sizes) {
		if (!std::isfinite(size.width) || size.width <= 0 ||
		    !std::isfinite(size.height) || size.height <= 0) {
			throw std::invalid_argument("invalid PDF page size");
		}
		const double width = checked(size.width * scale);
		const double height = checked(size.height * scale);
		if (width <= 0 || height <= 0) {
			throw std::overflow_error("page layout is out of range");
		}
		m_pages.push_back({margin, margin, width, height});
	}
	const auto active =
	    options.scroll == ScrollMode::Page
	        ? spreadPages(sizes.size(), options.current, options.spread)
	        : PageRange{0, sizes.size()};
	double x = margin, y = margin, rowHeight = 0;
	std::size_t rowFirst = active.first;
	const auto finishRow = [&](std::size_t end) {
		const double bottom = checked(y + rowHeight);
		if (bottom <= y) {
			throw std::overflow_error("page layout loses precision");
		}
		m_bands.push_back({y, bottom, {rowFirst, end}});
		m_width = (std::max)(m_width, checked(x - gap + margin));
		m_height = checked(y + rowHeight + margin);
		y = checked(y + rowHeight + gap);
		x = margin;
		rowHeight = 0;
		rowFirst = end;
	};
	for (auto first = active.first; first < active.end;) {
		const auto group = spreadPages(sizes.size(), first, options.spread);
		double width = 0, height = 0;
		for (auto i = group.first; i < group.end; ++i) {
			width =
			    checked(width + m_pages[i].width + (i > group.first ? gap : 0));
			height = (std::max)(height, m_pages[i].height);
		}
		if (options.scroll == ScrollMode::Wrapped && first > rowFirst &&
		    checked(x + width + margin) > viewportWidth) {
			finishRow(first);
		}
		const double groupStart = x;
		for (auto i = group.first; i < group.end; ++i) {
			m_pages[i].x = x;
			m_pages[i].y = y;
			x = checked(x + m_pages[i].width + gap);
		}
		if (m_horizontal) {
			const double right = checked(groupStart + width);
			if (right <= groupStart) {
				throw std::overflow_error("page layout loses precision");
			}
			m_bands.push_back({groupStart, right, group});
			m_width = (std::max)(m_width, checked(x - gap + margin));
			m_height = (std::max)(m_height, checked(height + 2 * margin));
		} else {
			rowHeight = (std::max)(rowHeight, height);
			if (options.scroll != ScrollMode::Wrapped ||
			    group.end == active.end) {
				finishRow(group.end);
			}
		}
		first = group.end;
	}
	// viewer.css: pages and spreads sit in their row with vertical-align:
	// middle, so a shorter page is centred against the tallest one
	const double strip = m_height - 2 * margin;
	for (const auto& band : m_bands) {
		const double top = m_horizontal ? margin : band.start;
		const double height = m_horizontal ? strip : band.end - band.start;
		for (auto i = band.pages.first; i < band.pages.end; ++i) {
			m_pages[i].y = checked(top + (height - m_pages[i].height) / 2);
		}
	}
	if (!m_horizontal) {
		// A row is centred in the width the reader shows (auto margins, and
		// text-align: center for spreads and wrapped rows). One wider than
		// that starts at the left edge instead, as overflowing inline content
		// does, so a wide page doesn't push the narrow ones out of view.
		const double shown =
		    options.visibleWidth > 0 ? options.visibleWidth : viewportWidth;
		const double span = shown > 0 ? (std::min)(m_width, shown) : m_width;
		for (const auto& band : m_bands) {
			const auto& last = m_pages[band.pages.end - 1];
			const double shift =
			    (std::max)(0.0, (span - (last.x + last.width + margin)) / 2);
			for (auto i = band.pages.first; i < band.pages.end; ++i) {
				m_pages[i].x += shift;
			}
		}
	}
}

PageRange PageLayout::visiblePages(double top, double height,
                                   std::size_t overscan) const {
	return visiblePages({0, top, m_width, height}, overscan);
}
PageRange PageLayout::visiblePages(PageRect viewport,
                                   std::size_t overscan) const {
	const double start = m_horizontal ? viewport.x : viewport.y;
	const double length = m_horizontal ? viewport.width : viewport.height;
	const double end = start + length;
	if (m_bands.empty() || !std::isfinite(viewport.x) ||
	    !std::isfinite(viewport.y) || !std::isfinite(viewport.width) ||
	    !std::isfinite(viewport.height) || viewport.width <= 0 ||
	    viewport.height <= 0 || !std::isfinite(end) ||
	    !std::isfinite(viewport.x + viewport.width) ||
	    !std::isfinite(viewport.y + viewport.height) || viewport.x >= m_width ||
	    viewport.y >= m_height || viewport.x + viewport.width <= 0 ||
	    viewport.y + viewport.height <= 0 || end <= 0 ||
	    start >= (m_horizontal ? m_width : m_height)) {
		return {};
	}
	const auto first = std::partition_point(
	    m_bands.begin(), m_bands.end(),
	    [start](const Band& band) { return band.end <= start; });
	const auto last =
	    std::partition_point(first, m_bands.end(), [end](const Band& band) {
		    return band.start < end;
	    });
	const auto beginPage =
	    first == m_bands.end() ? m_bands.back().pages.end : first->pages.first;
	const auto endPage =
	    last == m_bands.end() ? m_bands.back().pages.end : last->pages.first;
	const auto lower = m_bands.front().pages.first,
	           upper = m_bands.back().pages.end;
	return {beginPage - (std::min)(overscan, beginPage - lower),
	        endPage + (std::min)(overscan, upper - endPage)};
}
PageRange PageLayout::rowPages(std::size_t page) const {
	if (!contains(page)) {
		return {};
	}
	const auto band = std::partition_point(
	    m_bands.begin(), m_bands.end(),
	    [page](const Band& item) { return item.pages.end <= page; });
	return band->pages;
}
bool PageLayout::contains(std::size_t page) const {
	return !m_bands.empty() && page >= m_bands.front().pages.first &&
	       page < m_bands.back().pages.end;
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
	return pageAt(0, y);
}
std::size_t PageLayout::pageAt(double x, double y) const {
	if (m_bands.empty()) {
		throw std::out_of_range("empty page layout");
	}
	const double at = m_horizontal ? x : y;
	auto band =
	    std::partition_point(m_bands.begin(), m_bands.end(),
	                         [at](const Band& item) { return item.end <= at; });
	if (band == m_bands.end()) {
		band = m_bands.end() - 1;
	}
	auto best = band->pages.first;
	double distance = std::numeric_limits<double>::infinity();
	for (auto i = band->pages.first; i < band->pages.end; ++i) {
		const auto& rect = m_pages[i];
		const double dx = (std::max)({rect.x - x, 0., x - rect.x - rect.width});
		const double dy =
		    (std::max)({rect.y - y, 0., y - rect.y - rect.height});
		const double next = std::hypot(dx, dy);
		if (next < distance) {
			distance = next;
			best = i;
		}
	}
	return best;
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
