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
// the geometry of pdf.js v4.2.67's thumbnail sidebar, which is what
// NightPDF's DisplayThumbs showed: every thumbnail 98 CSS px wide and as
// tall as its page's shape makes it, in a 7 px border with 1 px of padding,
// 5 px apart, with 10 px above the first and below the last.
// see:
// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/pdf_thumbnail_view.js
// see: https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/viewer.css
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "layout.h"

namespace helpers {
inline constexpr double THUMBNAIL_WIDTH = 98;
inline constexpr double THUMBNAIL_BORDER = 7;
inline constexpr double THUMBNAIL_PADDING = 1;
inline constexpr double THUMBNAIL_GAP = 5;
// #thumbnailView's top padding and the last thumbnail's bottom margin
inline constexpr double THUMBNAIL_EDGE = 10;

// thumbnails stacked in a strip, in DIPs from the top of the strip
class ThumbnailStrip {
  public:
	// page sizes in points; invalid sizes throw invalid_argument
	explicit ThumbnailStrip(std::span<const PageSize> sizes);

	std::size_t size() const {
		return m_heights.size();
	}
	// the image: 98 wide, (98 / (width / height)) | 0 tall, at least 1
	double imageHeight(std::size_t page) const {
		return m_heights[page];
	}
	// the top of the thumbnail's border box
	double top(std::size_t page) const {
		return m_tops[page];
	}
	// the border box: the image, its padding and its border
	double outerHeight(std::size_t page) const;
	// the whole strip, edges included
	double height() const {
		return m_height;
	}

	// the thumbnails whose border boxes overlap top..top + height
	PageRange pagesIn(double top, double height) const;

	// pdf_thumbnail_viewer.js scrollThumbnailIntoView: the offset to scroll
	// to so the page's thumbnail is in view, 19 px under the top, or nullopt
	// when it already is. A thumbnail at either end of what's visible, or
	// only partly visible, counts as not in view. Nothing in view (no
	// layout yet) means nullopt too, as there.
	std::optional<double> scrollTo(std::size_t page, double viewTop,
	                               double viewHeight) const;

	// the pixels to render the image at for a display scale, like
	// #getPageDrawContext: (98 * scale) | 0 by (height * scale) | 0
	PixelRect renderSize(std::size_t page, double scale) const;

  private:
	std::vector<double> m_heights;
	std::vector<double> m_tops;
	double m_height = 0;
};
} // namespace helpers
