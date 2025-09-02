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
#include <gtest/gtest.h>

#include <optional>
#include <limits>
#include <stdexcept>
#include <vector>

#include "thumbnails.h"

namespace {
const helpers::PageSize A4{595.28, 841.89};
const helpers::PageSize LETTER_LANDSCAPE{792, 612};

TEST(ThumbnailStrip, SizesLikePdfJs) {
	const std::vector<helpers::PageSize> sizes = {
	    A4, LETTER_LANDSCAPE, {100, 100}, {1000, 1}};
	const helpers::ThumbnailStrip strip(sizes);
	ASSERT_EQ(strip.size(), 4u);
	// (98 / ratio) | 0
	EXPECT_DOUBLE_EQ(strip.imageHeight(0), 138);
	EXPECT_DOUBLE_EQ(strip.imageHeight(1), 75);
	EXPECT_DOUBLE_EQ(strip.imageHeight(2), 98);
	// never nothing at all
	EXPECT_DOUBLE_EQ(strip.imageHeight(3), 1);
	EXPECT_DOUBLE_EQ(strip.outerHeight(0), 138 + 16);
}

TEST(ThumbnailStrip, StackedWithGapsAndEdges) {
	const std::vector<helpers::PageSize> sizes = {A4, LETTER_LANDSCAPE, A4};
	const helpers::ThumbnailStrip strip(sizes);
	EXPECT_DOUBLE_EQ(strip.top(0), 10);
	EXPECT_DOUBLE_EQ(strip.top(1), 10 + 154 + 5);
	EXPECT_DOUBLE_EQ(strip.top(2), 169 + 91 + 5);
	// 10 below the last one
	EXPECT_DOUBLE_EQ(strip.height(), 265 + 154 + 10);
	EXPECT_DOUBLE_EQ(helpers::ThumbnailStrip({}).height(), 20);
}

TEST(ThumbnailStrip, RejectsBadSizes) {
	const std::vector<helpers::PageSize> zero = {{0, 100}};
	EXPECT_THROW(helpers::ThumbnailStrip{zero}, std::invalid_argument);
	const std::vector<helpers::PageSize> negative = {{100, -1}};
	EXPECT_THROW(helpers::ThumbnailStrip{negative}, std::invalid_argument);
}

TEST(ThumbnailStrip, RenderSizeFollowsTheDisplayScale) {
	const std::vector<helpers::PageSize> sizes = {A4};
	const helpers::ThumbnailStrip strip(sizes);
	EXPECT_EQ(strip.renderSize(0, 1), (helpers::PixelRect{0, 0, 98, 138}));
	EXPECT_EQ(strip.renderSize(0, 1.5), (helpers::PixelRect{0, 0, 147, 207}));
	// rounded down, like | 0
	EXPECT_EQ(strip.renderSize(0, 1.25), (helpers::PixelRect{0, 0, 122, 172}));
	EXPECT_EQ(strip.renderSize(0, 0), (helpers::PixelRect{}));
	EXPECT_EQ(strip.renderSize(1, 1), (helpers::PixelRect{}));
}

TEST(ThumbnailStrip, ExtremeSizesAndScalesCannotOverflow) {
	const std::vector<helpers::PageSize> tinyWidth = {{1e-300, 100}};
	EXPECT_THROW(helpers::ThumbnailStrip{tinyWidth}, std::invalid_argument);
	const std::vector<helpers::PageSize> hugeHeight = {{1, 1e300}};
	EXPECT_THROW(helpers::ThumbnailStrip{hugeHeight}, std::invalid_argument);
	const std::vector<helpers::PageSize> sizes = {A4};
	const helpers::ThumbnailStrip strip(sizes);
	for (double scale : {1e308, 1e9, std::numeric_limits<double>::infinity(),
	                     std::numeric_limits<double>::quiet_NaN()}) {
		EXPECT_EQ(strip.renderSize(0, scale), (helpers::PixelRect{}));
	}
}

// ten A4 thumbnails: borders at 10, 169, 328, 487..., images 7 lower and
// 140 tall
helpers::ThumbnailStrip tenPages() {
	const std::vector<helpers::PageSize> sizes(10, A4);
	return helpers::ThumbnailStrip(sizes);
}

TEST(ThumbnailStrip, PagesInARange) {
	const auto strip = tenPages();
	// borders at 10, 169, 328 and 487, each 154 tall
	EXPECT_EQ(strip.pagesIn(0, 500), (helpers::PageRange{0, 4}));
	// the gap between two thumbnails touches neither
	EXPECT_EQ(strip.pagesIn(164, 5), (helpers::PageRange{1, 1}));
	EXPECT_EQ(strip.pagesIn(170, 1), (helpers::PageRange{1, 2}));
	// starting above the strip is fine, past the end is empty
	EXPECT_EQ(strip.pagesIn(-500, 700), (helpers::PageRange{0, 2}));
	EXPECT_EQ(strip.pagesIn(5000, 500), (helpers::PageRange{10, 10}));
	EXPECT_EQ(strip.pagesIn(0, 0), (helpers::PageRange{}));
}

TEST(ThumbnailScroll, LeftAloneWhenWellInView) {
	const auto strip = tenPages();
	// 0 to 500 shows pages 0-2 and the top of page 3
	EXPECT_EQ(strip.scrollTo(1, 0, 500), std::nullopt);
	EXPECT_EQ(strip.scrollTo(2, 0, 500), std::nullopt);
	// a fraction of a pixel doesn't hide page 2
	EXPECT_EQ(strip.scrollTo(2, 20.4, 500), std::nullopt);
}

TEST(ThumbnailScroll, EitherEndOrPartlyVisibleScrolls) {
	const auto strip = tenPages();
	// the first and last visible ones scroll, 19 above the image's frame
	EXPECT_EQ(strip.scrollTo(0, 0, 500), 0.0);
	EXPECT_EQ(strip.scrollTo(3, 0, 500), 487 + 7 - 19.0);
	// out of view
	EXPECT_EQ(strip.scrollTo(7, 0, 500), 7 * 159 + 10 + 7 - 19.0);
	EXPECT_EQ(strip.scrollTo(0, 600, 500), 0.0);
	// page 2 is the last one in view, a pixel short of all of it
	EXPECT_EQ(strip.scrollTo(2, 0, 474), 328 + 7 - 19.0);
}

TEST(ThumbnailScroll, NothingInViewLeavesItAlone) {
	const auto strip = tenPages();
	EXPECT_EQ(strip.scrollTo(3, 0, 0), std::nullopt);
	EXPECT_EQ(strip.scrollTo(3, 5000, 500), std::nullopt);
	EXPECT_EQ(strip.scrollTo(10, 0, 500), std::nullopt);
}
} // namespace
