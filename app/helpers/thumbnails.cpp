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
#include "thumbnails.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace helpers {
namespace {
// THUMBNAIL_SCROLL_MARGIN in pdf_thumbnail_viewer.js
constexpr double SCROLL_MARGIN = -19;
constexpr double FRAME = THUMBNAIL_BORDER + THUMBNAIL_PADDING;
} // namespace

ThumbnailStrip::ThumbnailStrip(std::span<const PageSize> sizes) {
	m_heights.reserve(sizes.size());
	m_tops.reserve(sizes.size());
	double top = THUMBNAIL_EDGE;
	for (const auto& size : sizes) {
		if (!std::isfinite(size.width) || size.width <= 0 ||
		    !std::isfinite(size.height) || size.height <= 0) {
			throw std::invalid_argument("invalid PDF page size");
		}
		// this.canvasHeight = (this.canvasWidth / ratio) | 0
		const double ratio = size.width / size.height;
		const double height =
		    (std::max)(1.0, std::floor(THUMBNAIL_WIDTH / ratio));
		if (!std::isfinite(height) ||
		    height > std::numeric_limits<int>::max()) {
			throw std::invalid_argument("thumbnail is too tall");
		}
		m_heights.push_back(height);
		m_tops.push_back(top);
		top += height + 2 * FRAME + THUMBNAIL_GAP;
	}
	m_height = sizes.empty() ? 2 * THUMBNAIL_EDGE
	                         : top - THUMBNAIL_GAP + THUMBNAIL_EDGE;
}

double ThumbnailStrip::outerHeight(std::size_t page) const {
	return m_heights[page] + 2 * FRAME;
}

PageRange ThumbnailStrip::pagesIn(double top, double height) const {
	const double bottom = top + height;
	if (!std::isfinite(top) || !std::isfinite(bottom) || height <= 0) {
		return {};
	}
	// both edges only grow down the strip, so two binary searches do
	std::size_t low = 0;
	std::size_t high = size();
	while (low < high) {
		const auto mid = low + (high - low) / 2;
		if (m_tops[mid] + outerHeight(mid) <= top) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}
	const auto first = low;
	high = size();
	while (low < high) {
		const auto mid = low + (high - low) / 2;
		if (m_tops[mid] < bottom) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}
	return {first, low};
}

std::optional<double> ThumbnailStrip::scrollTo(std::size_t page, double viewTop,
                                               double viewHeight) const {
	if (page >= size() || !std::isfinite(viewTop) ||
	    !std::isfinite(viewHeight) || viewHeight <= 0) {
		return std::nullopt;
	}
	// ui_utils.js getVisibleElements over the images (padding boxes), in
	// whole pixels like the browser's scrollTop and offsetTop
	const double top = std::round(viewTop);
	const double bottom = std::round(viewTop + viewHeight);
	const auto imageTop = [this](std::size_t i) {
		return m_tops[i] + THUMBNAIL_BORDER;
	};
	const auto imageBottom = [this, &imageTop](std::size_t i) {
		return imageTop(i) + m_heights[i] + 2 * THUMBNAIL_PADDING;
	};
	std::size_t first = 0;
	while (first < size() && imageBottom(first) <= top) {
		++first;
	}
	std::size_t last = first;
	int percent = 0;
	bool any = false;
	for (auto i = first; i < size() && imageTop(i) < bottom; ++i) {
		any = true;
		last = i;
		if (i == page) {
			const double height = imageBottom(i) - imageTop(i);
			const double hidden = (std::max)(0.0, top - imageTop(i)) +
			                      (std::max)(0.0, imageBottom(i) - bottom);
			percent = static_cast<int>((height - hidden) / height * 100);
		}
	}
	if (!any) {
		return std::nullopt;
	}
	if (page > first && page < last && percent >= 100) {
		return std::nullopt;
	}
	// scrollIntoView(div, { top: THUMBNAIL_SCROLL_MARGIN }): the inside of
	// the border, less 19
	return (std::max)(0.0, m_tops[page] + THUMBNAIL_BORDER + SCROLL_MARGIN);
}

PixelRect ThumbnailStrip::renderSize(std::size_t page, double scale) const {
	if (page >= size() || !std::isfinite(scale) || scale <= 0) {
		return {};
	}
	const double width = std::floor(THUMBNAIL_WIDTH * scale);
	const double height = std::floor(m_heights[page] * scale);
	if (!std::isfinite(width) || !std::isfinite(height) ||
	    width > std::numeric_limits<int>::max() ||
	    height > std::numeric_limits<int>::max()) {
		return {};
	}
	return {0, 0, (std::max)(1, static_cast<int>(width)),
	        (std::max)(1, static_cast<int>(height))};
}
} // namespace helpers
