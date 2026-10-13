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
#include <span>
#include <vector>

namespace helpers {
// plain copies of the displayed page sizes, in points (1/72 inch)
struct PageSize {
	double width;
	double height;
};

// page bounds in DIPs, relative to the document's top left
struct PageRect {
	double x;
	double y;
	double width;
	double height;
};

struct PageRange {
	std::size_t first = 0;
	std::size_t end = 0; // exclusive; first == end means no pages
	bool operator==(const PageRange&) const = default;
};

// a page's bounds in physical pixels
struct PixelRect {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	bool operator==(const PixelRect&) const = default;
};

// DIPs to pixels at a display scale. Each edge is rounded on its own, so
// neighbouring rectangles never overlap or leave a hairline between them.
// Edges past int's range throw overflow_error.
PixelRect toPixels(const PageRect& rect, double scale);

// the size to render a width x height page at: as is, unless it's over
// maxPixels or a side is over maxSide, then scaled down to fit and
// stretched when drawn. pdf.js caps its canvases at 2^25 pixels the same
// way (maxCanvasPixels).
// see:
// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/app_options.js
PixelRect renderSize(int width, int height, long long maxPixels, int maxSide);

// which pages to render first: the visible ones top to bottom, then the
// overscan pages below them, then the ones above
std::vector<std::size_t> renderOrder(PageRange visible, PageRange overscanned);

class PageLayout {
  public:
	// zoom 1 means 100%. Viewport width, gap and outer margin are in DIPs;
	// only the pages scale with zoom. Invalid input throws invalid_argument,
	// and geometry that can't fit in a double throws overflow_error.
	PageLayout(std::span<const PageSize> sizes, double zoom,
	           double viewportWidth, double gap, double margin);

	std::span<const PageRect> pages() const {
		return m_pages;
	}
	double width() const {
		return m_width;
	}
	double height() const {
		return m_height;
	}

	// vertical viewport in DIPs. Touching a page edge isn't an intersection.
	// Overscan adds neighbours, including either side of a gap. A viewport
	// outside the document or with invalid dimensions returns an empty range.
	PageRange visiblePages(double top, double height,
	                       std::size_t overscan = 1) const;
	// the page at y, or the next one down when y is in a gap or the top
	// margin, or the last page past the end. Needs at least one page.
	std::size_t pageAt(double y) const;

  private:
	std::vector<PageRect> m_pages;
	double m_width = 0;
	double m_height = 0;
};
} // namespace helpers
