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
#include <string>
#include "signature.h"
#include "lines.h"
#include <cmath>

namespace {
struct Extent {
	double left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
};
Extent extent(const std::vector<pdf::DisplayedStroke>& strokes,
              helpers::PageSize size) {
	Extent box;
	for (const auto& stroke : strokes) {
		for (const auto& point : stroke) {
			box.left = (std::min)(box.left, point.x * size.width);
			box.right = (std::max)(box.right, point.x * size.width);
			box.top = (std::min)(box.top, point.y * size.height);
			box.bottom = (std::max)(box.bottom, point.y * size.height);
		}
	}
	return box;
}
} // namespace

TEST(Signature, NamesAreOneLineOfPrintableText) {
	EXPECT_TRUE(helpers::validSignatureName(u"Abhilash Kar"));
	EXPECT_TRUE(helpers::validSignatureName(u"Zoë Ångström"));
	EXPECT_TRUE(helpers::validSignatureName(std::u16string(64, u'a')));
	EXPECT_FALSE(helpers::validSignatureName(std::u16string(65, u'a')));
	EXPECT_FALSE(helpers::validSignatureName(u""));
	EXPECT_FALSE(helpers::validSignatureName(u"   "));
	EXPECT_FALSE(helpers::validSignatureName(u"Two\nlines"));
	EXPECT_FALSE(helpers::validSignatureName(u"Tab\there"));
	EXPECT_FALSE(
	    helpers::validSignatureName(std::u16string(1, char16_t(0xd800))));
	EXPECT_TRUE(helpers::validSignatureName(u"\U0001F58A"));
}

TEST(Signature, TypedNamesBecomeBlackInkOnTransparentPixels) {
	const auto image = helpers::typedSignature(u"Abhilash Kar");
	ASSERT_TRUE(image && image->valid());
	EXPECT_GT(image->width, 2 * image->height);
	bool solid = false, clear = false, black = true;
	for (std::size_t i = 0; i < image->bgra.size(); i += 4) {
		solid = solid || image->bgra[i + 3] == 255;
		clear = clear || image->bgra[i + 3] == 0;
		black = black && !image->bgra[i] && !image->bgra[i + 1] &&
		        !image->bgra[i + 2];
	}
	EXPECT_TRUE(solid && clear && black);
	// the longest name shrinks to fit one stamp image.
	const auto longest = helpers::typedSignature(std::u16string(64, u'W'));
	ASSERT_TRUE(longest && longest->valid());
	EXPECT_LE(longest->width, pdf::StampImage::maxSide);
	EXPECT_FALSE(helpers::typedSignature(u""));
	EXPECT_FALSE(helpers::typedSignature(u" "));
	EXPECT_FALSE(helpers::signatureFont().family.empty());
}

TEST(Signature, TypedBoxesAreThirtySixPointsTallAndStayOnThePage) {
	const helpers::PageSize letter{612, 792};
	const auto box = helpers::typedSignatureBox(4, letter, {.5, .5});
	ASSERT_TRUE(box);
	EXPECT_NEAR((box->second.x - box->first.x) * 612, 144, 1e-6);
	EXPECT_NEAR((box->second.y - box->first.y) * 792, 36, 1e-6);
	EXPECT_NEAR((box->first.x + box->second.x) / 2, .5, 1e-9);
	const auto wide = helpers::typedSignatureBox(10, letter, {.5, .5});
	ASSERT_TRUE(wide);
	EXPECT_NEAR((wide->second.x - wide->first.x) * 612, 216, 1e-6);
	EXPECT_NEAR((wide->second.y - wide->first.y) * 792, 21.6, 1e-6);
	const auto corner = helpers::typedSignatureBox(4, letter, {0, 1});
	ASSERT_TRUE(corner);
	EXPECT_NEAR(corner->first.x, 0, 1e-12);
	EXPECT_NEAR(corner->second.y, 1, 1e-12);
	const auto tiny = helpers::typedSignatureBox(4, {60, 20}, {.5, .5});
	ASSERT_TRUE(tiny);
	EXPECT_NEAR((tiny->second.x - tiny->first.x) * 60, 54, 1e-6);
	EXPECT_FALSE(helpers::typedSignatureBox(0, letter, {.5, .5}));
	EXPECT_FALSE(helpers::typedSignatureBox(4, {0, 792}, {.5, .5}));
}

TEST(Signature, DrawnStrokesFitTheirBoxAndKeepTheirShape) {
	const helpers::PageSize letter{612, 792};
	const std::vector<helpers::PadStroke> pad{{{0, 0}, {180, 60}},
	                                          {{180, 60}, {360, 120}}};
	const auto strokes = helpers::drawnSignatureStrokes(pad, letter, {.5, .5});
	ASSERT_EQ(strokes.size(), 2u);
	ASSERT_EQ(strokes[0].size(), 2u);
	auto box = extent(strokes, letter);
	EXPECT_NEAR(box.right - box.left, 150, 1e-6);
	EXPECT_NEAR(box.bottom - box.top, 50, 1e-6);
	EXPECT_NEAR((box.left + box.right) / 2, 306, 1e-6);
	EXPECT_NEAR((box.top + box.bottom) / 2, 396, 1e-6);
	// a click at the corner keeps the 2 pt line on the page.
	box = extent(helpers::drawnSignatureStrokes(pad, letter, {0, 0}), letter);
	EXPECT_NEAR(box.left, 1.5, 1e-6);
	EXPECT_NEAR(box.top, 1.5, 1e-6);
	const auto flat = helpers::drawnSignatureStrokes({{{0, 10}, {100, 10}}},
	                                                 letter, {.5, .5});
	box = extent(flat, letter);
	EXPECT_NEAR(box.right - box.left, 150, 1e-6);
	EXPECT_NEAR(box.bottom - box.top, 0, 1e-6);
	const auto dot =
	    helpers::drawnSignatureStrokes({{{5, 5}}}, letter, {.25, .25});
	ASSERT_EQ(dot.size(), 1u);
	EXPECT_NEAR(dot[0][0].x, .25, 1e-9);
	EXPECT_TRUE(helpers::drawnSignatureStrokes({}, letter, {.5, .5}).empty());
	EXPECT_TRUE(
	    helpers::drawnSignatureStrokes(
	        std::vector<helpers::PadStroke>(257, {{1, 1}}), letter, {.5, .5})
	        .empty());
	EXPECT_TRUE(helpers::drawnSignatureStrokes({{}}, letter, {.5, .5}).empty());
}

TEST(LineStrokes, LinesAndArrowsStayOnThePageAndRefuseShortClicks) {
	// a 400 by 200 pt page, so x and y fractions have different scales
	EXPECT_FALSE(
	    helpers::lineStrokes({.5, .5}, {.505, .505}, 400, 200, 2, false));
	EXPECT_FALSE(helpers::lineStrokes({.1, .1}, {.9, .9}, 2, 2, 2, false));
	const auto line =
	    helpers::lineStrokes({.25, .5}, {.75, .5}, 400, 200, 2, false);
	ASSERT_TRUE(line && line->size() == 1 && (*line)[0].size() == 2);
	EXPECT_DOUBLE_EQ((*line)[0][0].x, .25);
	EXPECT_DOUBLE_EQ((*line)[0][1].x, .75);
	// the head is 8 pt at 2 pt, 30 degrees either side of the shaft
	const auto arrow =
	    helpers::lineStrokes({.25, .5}, {.75, .5}, 400, 200, 2, true);
	ASSERT_TRUE(arrow && arrow->size() == 3);
	for (std::size_t i = 1; i < 3; ++i) {
		const auto& head = (*arrow)[i];
		ASSERT_EQ(head.size(), 2u);
		EXPECT_DOUBLE_EQ(head[0].x, .75);
		const double dx = (head[1].x - head[0].x) * 400;
		const double dy = (head[1].y - head[0].y) * 200;
		EXPECT_NEAR(std::hypot(dx, dy), 8, 1e-9);
		EXPECT_NEAR(dx, -8 * std::cos(3.14159265358979 / 6), 1e-9);
		EXPECT_NEAR(std::abs(dy), 4, 1e-9);
	}
	EXPECT_NEAR((*arrow)[1][1].y + (*arrow)[2][1].y, 1, 1e-12);
	// thick pens get longer heads, short arrows shorter ones
	const auto thick =
	    helpers::lineStrokes({.25, .5}, {.75, .5}, 400, 200, 4, true);
	ASSERT_TRUE(thick);
	EXPECT_NEAR(((*thick)[1][0].x - (*thick)[1][1].x) * 400,
	            16 * std::cos(3.14159265358979 / 6), 1e-9);
	const auto small =
	    helpers::lineStrokes({.5, .5}, {.53, .5}, 400, 200, 2, true);
	ASSERT_TRUE(small);
	EXPECT_NEAR(std::hypot(((*small)[1][1].x - (*small)[1][0].x) * 400,
	                       ((*small)[1][1].y - (*small)[1][0].y) * 200),
	            4, 1e-9);
	// an end off the page is pulled back half the pen and a little more
	const auto edge =
	    helpers::lineStrokes({.5, .5}, {1, .5}, 400, 200, 2, false);
	ASSERT_TRUE(edge);
	EXPECT_DOUBLE_EQ((*edge)[0][1].x, (400 - 1.5) / 400);
}
