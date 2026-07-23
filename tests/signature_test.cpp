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
