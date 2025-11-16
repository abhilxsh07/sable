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
#include <cmath>
#include <limits>
#include <random>
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
	// neither page fits in 50, so both start at the left like pdf.js
	EXPECT_DOUBLE_EQ(layout.pages()[0].x, 5);
	EXPECT_DOUBLE_EQ(layout.pages()[1].x, 5);
}

TEST(PageLayout, NarrowPagesCentreInTheViewNotOnTheWidestPage) {
	// a 96 DIP page and a 1920 DIP one, read in a 400 DIP wide window
	const std::array<PageSize, 2> sizes{{{72, 144}, {1440, 72}}};
	const PageLayout layout(
	    sizes, 1, 0, 10, 5,
	    {helpers::ScrollMode::Vertical, helpers::SpreadMode::None, 0, 400});
	EXPECT_DOUBLE_EQ(layout.width(), 1930);
	EXPECT_DOUBLE_EQ(layout.pages()[0].x, 152);
	EXPECT_DOUBLE_EQ(layout.pages()[1].x, 5);
	// without a visible width the rows still centre on the widest one
	const PageLayout unknown(sizes, 1, 0, 10, 5);
	EXPECT_DOUBLE_EQ(unknown.pages()[0].x, 917);
	// and a window wider than every page centres all of them
	const PageLayout wide(
	    sizes, 1, 0, 10, 5,
	    {helpers::ScrollMode::Vertical, helpers::SpreadMode::None, 0, 3000});
	EXPECT_DOUBLE_EQ(wide.width(), 1930);
	EXPECT_DOUBLE_EQ(wide.pages()[0].x, 917);
	EXPECT_THROW(PageLayout(sizes, 1, 0, 10, 5,
	                        {helpers::ScrollMode::Vertical,
	                         helpers::SpreadMode::None, 0, NAN_VALUE}),
	             std::invalid_argument);
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

TEST(PageLayout, PageAtFindsThePageOrTheNextOneDown) {
	// pages at 5..197 and 207..303
	const PageLayout layout(SIZES, 1, 400, 10, 5);
	EXPECT_EQ(layout.pageAt(0), 0u);
	EXPECT_EQ(layout.pageAt(100), 0u);
	EXPECT_EQ(layout.pageAt(197), 1u);
	EXPECT_EQ(layout.pageAt(200), 1u);
	EXPECT_EQ(layout.pageAt(250), 1u);
	EXPECT_EQ(layout.pageAt(1000), 1u);
}

TEST(RenderSize, SmallPagesRenderAsTheyAre) {
	EXPECT_EQ(helpers::renderSize(794, 1123, 1LL << 25, 16384),
	          (PixelRect{0, 0, 794, 1123}));
}

TEST(RenderSize, BigPagesStayUnderTheCanvasLimit) {
	// A4 at 1000%: pdf.js would render it smaller and stretch it
	const auto size = helpers::renderSize(7937, 11225, 1LL << 25, 16384);
	EXPECT_EQ(size, (PixelRect{0, 0, 4870, 6888}));
	EXPECT_LE(static_cast<long long>(size.width) * size.height, 1LL << 25);
}

TEST(RenderSize, LongSidesStayUnderTheMaximum) {
	EXPECT_EQ(helpers::renderSize(20000, 100, 1LL << 40, 16384),
	          (PixelRect{0, 0, 16384, 81}));
}

TEST(RenderSize, EmptyStaysEmpty) {
	EXPECT_EQ(helpers::renderSize(0, 100, 1LL << 25, 16384),
	          (PixelRect{0, 0, 0, 100}));
}

TEST(ReadingLayout, OddAndEvenSpreadsHandleCoverAndLastPage) {
	using helpers::SpreadMode;
	EXPECT_EQ(helpers::spreadPages(5, 0, SpreadMode::Odd), (PageRange{0, 2}));
	EXPECT_EQ(helpers::spreadPages(5, 1, SpreadMode::Odd), (PageRange{0, 2}));
	EXPECT_EQ(helpers::spreadPages(5, 4, SpreadMode::Odd), (PageRange{4, 5}));
	EXPECT_EQ(helpers::spreadPages(5, 0, SpreadMode::Even), (PageRange{0, 1}));
	EXPECT_EQ(helpers::spreadPages(5, 1, SpreadMode::Even), (PageRange{1, 3}));
	EXPECT_EQ(helpers::spreadPages(5, 2, SpreadMode::Even), (PageRange{1, 3}));
	EXPECT_EQ(helpers::spreadPages(0, 0, SpreadMode::Odd), (PageRange{}));
	EXPECT_EQ(helpers::spreadPages(5, 5, SpreadMode::None), (PageRange{}));
	const std::array<helpers::PageSize, 2> sizes{{{72, 144}, {144, 72}}};
	const auto spread = helpers::spreadSize(sizes, 1, SpreadMode::Odd);
	EXPECT_DOUBLE_EQ(spread.width, 216);
	EXPECT_DOUBLE_EQ(spread.height, 144);
}
TEST(ReadingLayout, HorizontalVisibilityAndHitTestingUseTheXAxis) {
	const std::vector<helpers::PageSize> sizes(1000, {72, 144});
	const PageLayout layout(sizes, 1, 0, 10, 5,
	                        {helpers::ScrollMode::Horizontal});
	EXPECT_DOUBLE_EQ(layout.width(), 106000);
	EXPECT_DOUBLE_EQ(layout.height(), 202);
	EXPECT_DOUBLE_EQ(layout.pages()[500].x, 53005);
	EXPECT_DOUBLE_EQ(layout.pages()[500].y, 5);
	EXPECT_EQ(layout.visiblePages({53005, 5, 96, 192}, 0),
	          (PageRange{500, 501}));
	EXPECT_EQ(layout.visiblePages({53005, 5, 96, 192}), (PageRange{499, 502}));
	EXPECT_EQ(layout.pageAt(53006, 10), 500u);
	EXPECT_EQ(layout.pageAt(106500, 10), 999u);
}
TEST(ReadingLayout, MixedHeightSpreadKeepsTallPageVisible) {
	const std::array<helpers::PageSize, 3> sizes{
	    {{72, 36}, {72, 144}, {72, 72}}};
	const PageLayout layout(
	    sizes, .75, 0, 10, 5,
	    {helpers::ScrollMode::Vertical, helpers::SpreadMode::Odd});
	// the short page sits in the middle of its spread
	EXPECT_DOUBLE_EQ(layout.pages()[0].y, 59);
	EXPECT_DOUBLE_EQ(layout.pages()[1].y, 5);
	EXPECT_DOUBLE_EQ(layout.pages()[2].y, 159);
	EXPECT_EQ(layout.visiblePages({0, 100, 200, 30}, 0), (PageRange{0, 2}));
	EXPECT_EQ(layout.pageAt(layout.pages()[1].x + 10, 100), 1u);
	EXPECT_EQ(layout.pageAt(layout.pages()[0].x + 10, 10), 0u);
}
TEST(ReadingLayout, WrappedRowsAreCenteredAndKeepSpreadsTogether) {
	const std::vector<helpers::PageSize> sizes(5, {72, 72});
	const PageLayout layout(
	    sizes, .75, 190, 10, 5,
	    {helpers::ScrollMode::Wrapped, helpers::SpreadMode::Odd});
	EXPECT_DOUBLE_EQ(layout.pages()[0].y, 5);
	EXPECT_DOUBLE_EQ(layout.pages()[1].y, 5);
	EXPECT_DOUBLE_EQ(layout.pages()[2].y, 87);
	EXPECT_DOUBLE_EQ(layout.pages()[3].y, 87);
	EXPECT_DOUBLE_EQ(layout.pages()[4].y, 169);
	EXPECT_DOUBLE_EQ(layout.pages()[0].x, 18);
	EXPECT_DOUBLE_EQ(layout.pages()[4].x, 59);
	EXPECT_EQ(layout.pageAt(110, 10), 1u);
	EXPECT_EQ(layout.rowPages(3), (PageRange{2, 4}));
	EXPECT_EQ(layout.visiblePages({0, 87, 190, 72}, 0), (PageRange{2, 4}));
	const PageLayout narrow(
	    sizes, .75, 80, 10, 5,
	    {helpers::ScrollMode::Wrapped, helpers::SpreadMode::Odd});
	EXPECT_EQ(narrow.pages()[0].y, narrow.pages()[1].y);
	EXPECT_GT(narrow.width(), 80);
}
TEST(ReadingLayout, ShortPagesCentreInTheirRowInEveryMode) {
	// 96x192 and 96x96 DIPs at 100%
	const std::array<helpers::PageSize, 2> sizes{{{72, 144}, {72, 72}}};
	const PageLayout strip(sizes, 1, 0, 10, 5,
	                       {helpers::ScrollMode::Horizontal});
	EXPECT_DOUBLE_EQ(strip.pages()[0].y, 5);
	EXPECT_DOUBLE_EQ(strip.pages()[1].y, 53);
	EXPECT_DOUBLE_EQ(strip.height(), 202);
	const PageLayout wrapped(sizes, 1, 400, 10, 5,
	                         {helpers::ScrollMode::Wrapped});
	EXPECT_DOUBLE_EQ(wrapped.pages()[1].y, 53);
	EXPECT_DOUBLE_EQ(wrapped.pages()[1].x - wrapped.pages()[0].x, 106);
	const PageLayout spread(
	    sizes, 1, 0, 10, 5,
	    {helpers::ScrollMode::Page, helpers::SpreadMode::Odd, 1});
	EXPECT_DOUBLE_EQ(spread.pages()[0].y, 5);
	EXPECT_DOUBLE_EQ(spread.pages()[1].y, 53);
	EXPECT_EQ(spread.pageAt(spread.pages()[1].x + 10, 60), 1u);
	EXPECT_EQ(spread.visiblePages({0, 5, 300, 20}, 0), (PageRange{0, 2}));
}
TEST(ReadingLayout, WrappedSinglePagesReflowWithViewportWidth) {
	const std::vector<helpers::PageSize> sizes(5, {72, 72});
	const PageLayout wide(sizes, .75, 270, 10, 5,
	                      {helpers::ScrollMode::Wrapped});
	const PageLayout narrow(sizes, .75, 170, 10, 5,
	                        {helpers::ScrollMode::Wrapped});
	EXPECT_EQ(wide.pages()[2].y, wide.pages()[0].y);
	EXPECT_GT(narrow.pages()[2].y, narrow.pages()[0].y);
	EXPECT_DOUBLE_EQ(narrow.pages()[3].x - narrow.pages()[2].x, 82);
	EXPECT_EQ(wide.visiblePages({0, 5, 270, 72}, 0), (PageRange{0, 3}));
}
TEST(ReadingLayout, PageModeExposesOnlyTheCurrentSpread) {
	const std::vector<helpers::PageSize> sizes(10, {72, 144});
	const PageLayout layout(
	    sizes, 1, 0, 10, 5,
	    {helpers::ScrollMode::Page, helpers::SpreadMode::Even, 5});
	EXPECT_EQ(layout.pages().size(), 10u);
	EXPECT_TRUE(layout.contains(5));
	EXPECT_TRUE(layout.contains(6));
	EXPECT_FALSE(layout.contains(4));
	EXPECT_FALSE(layout.contains(7));
	EXPECT_EQ(layout.rowPages(5), (PageRange{5, 7}));
	EXPECT_EQ(layout.rowPages(4), (PageRange{}));
	EXPECT_EQ(layout.visiblePages({0, 0, 500, 500}, 100), (PageRange{5, 7}));
	EXPECT_EQ(layout.pageAt(10000, 10000), 6u);
	EXPECT_DOUBLE_EQ(layout.height(), 202);
}
TEST(ReadingLayout, RejectsInvalidModesAndNonfiniteViewports) {
	const std::array<helpers::PageSize, 1> sizes{{{72, 72}}};
	EXPECT_THROW(
	    PageLayout(sizes, 1, 0, 10, 5, {static_cast<helpers::ScrollMode>(99)}),
	    std::invalid_argument);
	EXPECT_THROW(
	    PageLayout(sizes, 1, 0, 10, 5,
	               {helpers::ScrollMode::Page, helpers::SpreadMode::None, 1}),
	    std::invalid_argument);
	const PageLayout layout(sizes, 1, 0, 10, 5,
	                        {helpers::ScrollMode::Horizontal});
	const auto inf = std::numeric_limits<double>::infinity();
	EXPECT_EQ(layout.visiblePages({inf, 0, 100, 100}), (PageRange{}));
	EXPECT_EQ(layout.visiblePages({0, 0, inf, 100}), (PageRange{}));
	EXPECT_EQ(layout.visiblePages({0, 300, 100, 100}), (PageRange{}));
}

TEST(ReadingLayout, MixedDocumentsKeepPagesVisibleAndSeparateAcrossModes) {
	std::minstd_rand random(19026);
	for (int trial = 0; trial < 120; ++trial) {
		SCOPED_TRACE(trial);
		std::vector<PageSize> sizes(1 + random() % 64);
		for (auto& size : sizes) {
			size = {10. + random() % 1200, 10. + random() % 1600};
		}
		const PageLayout layout(
		    sizes, .1 + (random() % 991) / 100., 100. + random() % 2000, 10, 5,
		    {static_cast<helpers::ScrollMode>(trial % 4),
		     static_cast<helpers::SpreadMode>((trial / 4) % 3),
		     random() % sizes.size()});
		ASSERT_TRUE(std::isfinite(layout.width()));
		ASSERT_TRUE(std::isfinite(layout.height()));
		for (std::size_t page = 0; page < sizes.size(); ++page) {
			if (!layout.contains(page)) {
				continue;
			}
			SCOPED_TRACE(page);
			const auto& rect = layout.pages()[page];
			EXPECT_GE(rect.x, 0);
			EXPECT_GE(rect.y, 0);
			EXPECT_LE(rect.x + rect.width, layout.width() + 1e-7);
			EXPECT_LE(rect.y + rect.height, layout.height() + 1e-7);
			const double x = rect.x + rect.width / 2;
			const double y = rect.y + rect.height / 2;
			EXPECT_EQ(layout.pageAt(x, y), page);
			const auto visible = layout.visiblePages({x, y, .5, .5}, 0);
			EXPECT_LE(visible.first, page);
			EXPECT_GT(visible.end, page);
			EXPECT_LE(visible.end, sizes.size());
			for (auto other = page + 1; other < sizes.size(); ++other) {
				if (!layout.contains(other)) {
					continue;
				}
				const auto& next = layout.pages()[other];
				EXPECT_TRUE(rect.x + rect.width <= next.x + 1e-7 ||
				            next.x + next.width <= rect.x + 1e-7 ||
				            rect.y + rect.height <= next.y + 1e-7 ||
				            next.y + next.height <= rect.y + 1e-7)
				    << "overlaps page " << other;
			}
		}
	}
}
