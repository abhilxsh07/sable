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

#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

#include "layout.h"

using helpers::PageLayout;
using helpers::PageRange;
using helpers::PageSize;
using helpers::PixelRect;

namespace {
constexpr std::array<PageSize, 2> SIZES{{{72, 144}, {144, 72}}};
constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double NAN_VALUE = std::numeric_limits<double>::quiet_NaN();
} // namespace

TEST(PageLayout, EmptyDocumentHasNoPagesOrHeight) {
	const PageLayout layout({}, 1, 400, 10, 5);
	EXPECT_TRUE(layout.pages().empty());
	EXPECT_DOUBLE_EQ(layout.width(), 400);
	EXPECT_DOUBLE_EQ(layout.height(), 0);
	EXPECT_EQ(layout.visiblePages(0, 500), (PageRange{}));
}

TEST(PageLayout, ConvertsPointsAndCentersMixedPageSizes) {
	const PageLayout layout(SIZES, 1, 400, 10, 5);
	ASSERT_EQ(layout.pages().size(), 2u);
	const auto pages = layout.pages();
	EXPECT_DOUBLE_EQ(pages[0].width, 96);
	EXPECT_DOUBLE_EQ(pages[0].height, 192);
	EXPECT_DOUBLE_EQ(pages[0].x, 152);
	EXPECT_DOUBLE_EQ(pages[0].y, 5);
	EXPECT_DOUBLE_EQ(pages[1].width, 192);
	EXPECT_DOUBLE_EQ(pages[1].height, 96);
	EXPECT_DOUBLE_EQ(pages[1].x, 104);
	EXPECT_DOUBLE_EQ(pages[1].y, 207);
	EXPECT_DOUBLE_EQ(layout.width(), 400);
	EXPECT_DOUBLE_EQ(layout.height(), 308);
}

TEST(PageLayout, NarrowViewportStillFitsTheWidestPage) {
	const PageLayout layout(SIZES, 1, 50, 10, 5);
	EXPECT_DOUBLE_EQ(layout.width(), 202);
	EXPECT_DOUBLE_EQ(layout.pages()[0].x, 53);
	EXPECT_DOUBLE_EQ(layout.pages()[1].x, 5);
}

TEST(PageLayout, ZoomScalesPagesButNotSpacing) {
	const PageLayout layout(SIZES, 1.5, 400, 10, 5);
	EXPECT_DOUBLE_EQ(layout.pages()[0].width, 144);
	EXPECT_DOUBLE_EQ(layout.pages()[0].height, 288);
	EXPECT_DOUBLE_EQ(layout.pages()[1].y, 303);
	EXPECT_DOUBLE_EQ(layout.height(), 452);
}

TEST(PageLayout, ReturnsOnlyIntersectingPagesWithoutOverscan) {
	const PageLayout layout(SIZES, 1, 400, 10, 5);
	EXPECT_EQ(layout.visiblePages(5, 192, 0), (PageRange{0, 1}));
	EXPECT_EQ(layout.visiblePages(207, 96, 0), (PageRange{1, 2}));
	EXPECT_EQ(layout.visiblePages(196, 12, 0), (PageRange{0, 2}));
	EXPECT_EQ(layout.visiblePages(-10, 16, 0), (PageRange{0, 1}));
}

TEST(PageLayout, TouchingEdgesAndGapsDoNotCountAsVisible) {
	const PageLayout layout(SIZES, 1, 400, 10, 5);
	EXPECT_EQ(layout.visiblePages(197, 10, 0), (PageRange{1, 1}));
	EXPECT_EQ(layout.visiblePages(0, 5, 0), (PageRange{0, 0}));
	EXPECT_EQ(layout.visiblePages(303, 5, 0), (PageRange{2, 2}));
	EXPECT_EQ(layout.visiblePages(197, 10), (PageRange{0, 2}));
}

TEST(PageLayout, OverscanStopsAtTheDocumentEnds) {
	const PageLayout layout(SIZES, 1, 400, 10, 5);
	EXPECT_EQ(layout.visiblePages(5, 1), (PageRange{0, 2}));
	EXPECT_EQ(layout.visiblePages(302, 1), (PageRange{0, 2}));
	EXPECT_EQ(
	    layout.visiblePages(5, 1, std::numeric_limits<std::size_t>::max()),
	    (PageRange{0, 2}));
}

TEST(PageLayout, AThousandPagesOnlySelectTheNearbyPages) {
	const std::vector<PageSize> sizes(1000, {72, 72});
	const PageLayout layout(sizes, 1, 400, 10, 5);
	EXPECT_DOUBLE_EQ(layout.height(), 106000);
	EXPECT_DOUBLE_EQ(layout.pages()[500].y, 53005);
	EXPECT_EQ(layout.visiblePages(53005, 96, 0), (PageRange{500, 501}));
	EXPECT_EQ(layout.visiblePages(53005, 96), (PageRange{499, 502}));
}

TEST(PageLayout, InvalidOrOffDocumentViewportsReturnNothing) {
	const PageLayout layout(SIZES, 1, 400, 10, 5);
	for (const double top : {-100.0, 308.0, INF, NAN_VALUE}) {
		EXPECT_EQ(layout.visiblePages(top, 10), (PageRange{}));
	}
	for (const double height : {0.0, -1.0, INF, NAN_VALUE}) {
		EXPECT_EQ(layout.visiblePages(5, height), (PageRange{}));
	}
}

TEST(PageLayout, RejectsInvalidDimensions) {
	for (const double bad : {-1.0, INF, NAN_VALUE}) {
		EXPECT_THROW(PageLayout(SIZES, bad, 400, 10, 5), std::invalid_argument);
		EXPECT_THROW(PageLayout(SIZES, 1, bad, 10, 5), std::invalid_argument);
		EXPECT_THROW(PageLayout(SIZES, 1, 400, bad, 5), std::invalid_argument);
		EXPECT_THROW(PageLayout(SIZES, 1, 400, 10, bad), std::invalid_argument);
	}
	EXPECT_THROW(PageLayout(SIZES, 0, 400, 10, 5), std::invalid_argument);
	for (const double bad : {0.0, -1.0, INF, NAN_VALUE}) {
		const std::array<PageSize, 1> wide{{{bad, 72}}};
		const std::array<PageSize, 1> tall{{{72, bad}}};
		EXPECT_THROW(PageLayout(wide, 1, 400, 10, 5), std::invalid_argument);
		EXPECT_THROW(PageLayout(tall, 1, 400, 10, 5), std::invalid_argument);
	}
}

TEST(PageLayout, RejectsOverflowInsteadOfReturningInfiniteGeometry) {
	const double huge = std::numeric_limits<double>::max();
	EXPECT_THROW(PageLayout(SIZES, huge, 400, 10, 5), std::overflow_error);
	EXPECT_THROW(PageLayout(SIZES, 1, 400, 10, huge), std::overflow_error);
}

TEST(PixelRects, OneToOneAtOneHundredPercent) {
	EXPECT_EQ(helpers::toPixels({10, 20, 794, 1123}, 1.0),
	          (PixelRect{10, 20, 794, 1123}));
}

TEST(PixelRects, NeighboursShareTheirEdgeAtFractionalScales) {
	// rounding y and height separately would give 15 + 15 = 30 and leave a
	// one pixel line before the next rectangle starts at 31
	const auto a = helpers::toPixels({10.3, 10.3, 10.3, 10.3}, 1.5);
	const auto b = helpers::toPixels({10.3, 20.6, 10.3, 10.3}, 1.5);
	EXPECT_EQ(a.y, 15);
	EXPECT_EQ(a.y + a.height, 31);
	EXPECT_EQ(b.y, 31);
	EXPECT_EQ(a.x + a.width, b.x + b.width);
}

TEST(PixelRects, TallDocumentsStayExact) {
	// the last of 1,000 A4 pages at 400% on a 150% display
	const auto rect = helpers::toPixels({10, 4'495'589, 3174.8, 4490.08}, 1.5);
	EXPECT_EQ(rect.y, 6'743'384);
	EXPECT_EQ(rect.height, 6'735);
}

TEST(PixelRects, EdgesPastIntThrow) {
	EXPECT_THROW(helpers::toPixels({0, 3e9, 10, 10}, 1.0), std::overflow_error);
	EXPECT_THROW(helpers::toPixels({0, 0, INF, 10}, 1.0), std::overflow_error);
}

TEST(RenderOrder, VisibleFirstThenBelowThenAbove) {
	EXPECT_EQ(helpers::renderOrder({4, 6}, {3, 7}),
	          (std::vector<std::size_t>{4, 5, 6, 3}));
	EXPECT_EQ(helpers::renderOrder({4, 6}, {2, 8}),
	          (std::vector<std::size_t>{4, 5, 6, 7, 3, 2}));
}

TEST(RenderOrder, StopsAtTheDocumentEnds) {
	EXPECT_EQ(helpers::renderOrder({0, 2}, {0, 3}),
	          (std::vector<std::size_t>{0, 1, 2}));
	EXPECT_TRUE(helpers::renderOrder({0, 0}, {0, 0}).empty());
}
