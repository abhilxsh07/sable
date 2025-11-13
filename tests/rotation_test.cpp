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
#include "rotation.h"

namespace {
void expectRect(helpers::PageRect actual, helpers::PageRect expected) {
	EXPECT_DOUBLE_EQ(actual.x, expected.x);
	EXPECT_DOUBLE_EQ(actual.y, expected.y);
	EXPECT_DOUBLE_EQ(actual.width, expected.width);
	EXPECT_DOUBLE_EQ(actual.height, expected.height);
}
} // namespace

TEST(Rotation, WrapsTurnsWithoutOverflow) {
	for (int turns = -17; turns <= 17; ++turns) {
		const auto wrapped = helpers::quarterTurns(turns);
		EXPECT_GE(wrapped, 0);
		EXPECT_LT(wrapped, 4);
		EXPECT_EQ(helpers::quarterTurns(turns + 4), wrapped);
	}
	EXPECT_EQ(helpers::quarterTurns(-1), 3);
	EXPECT_EQ(helpers::quarterTurns(-6), 2);
	EXPECT_EQ(helpers::quarterTurns(std::numeric_limits<int>::min()), 0);
	EXPECT_EQ(helpers::quarterTurns(std::numeric_limits<int>::max()), 3);
}

TEST(Rotation, SizesSwapOnlyAtOddTurns) {
	for (int turns = -8; turns <= 8; ++turns) {
		const auto size = helpers::rotatedSize({595, 842}, turns);
		EXPECT_DOUBLE_EQ(size.width, turns % 2 ? 842 : 595);
		EXPECT_DOUBLE_EQ(size.height, turns % 2 ? 595 : 842);
	}
	const auto empty = helpers::rotatedSize({0, 400}, -1);
	EXPECT_DOUBLE_EQ(empty.width, 400);
	EXPECT_DOUBLE_EQ(empty.height, 0);
}

TEST(Rotation, PointsRotateClockwiseInPageCoordinates) {
	const std::array<helpers::PagePoint, 4> expected{
	    {{.125, .375}, {.625, .125}, {.875, .625}, {.375, .875}}};
	for (int turns = -8; turns <= 8; ++turns) {
		EXPECT_EQ(helpers::rotatePoint({.125, .375}, turns),
		          expected[helpers::quarterTurns(turns)]);
	}
	EXPECT_EQ(helpers::rotatePoint({0, 0}, 1), (helpers::PagePoint{1, 0}));
}

TEST(Rotation, CompositionAndInverseReturnTheSamePointAndBounds) {
	const helpers::PagePoint point{-.5, 1.25};
	const helpers::PageRect rect{.125, .375, .25, .5};
	for (int first = -7; first <= 7; ++first) {
		EXPECT_EQ(
		    helpers::rotatePoint(helpers::rotatePoint(point, first), -first),
		    point);
		expectRect(
		    helpers::rotateRect(helpers::rotateRect(rect, first), -first),
		    rect);
		for (int second = -7; second <= 7; ++second) {
			EXPECT_EQ(helpers::rotatePoint(helpers::rotatePoint(point, first),
			                               second),
			          helpers::rotatePoint(point, first + second));
		}
	}
}

TEST(Rotation, RectanglesFollowAllFourOrientations) {
	const std::array<helpers::PageRect, 4> expected{{{.125, .375, .25, .5},
	                                                 {.125, .125, .5, .25},
	                                                 {.625, .125, .25, .5},
	                                                 {.375, .625, .5, .25}}};
	for (int turns = -8; turns <= 8; ++turns) {
		expectRect(helpers::rotateRect({.125, .375, .25, .5}, turns),
		           expected[helpers::quarterTurns(turns)]);
	}
}

TEST(Rotation, BoundsRemainUnclampedForOutsideAndDegenerateRectangles) {
	expectRect(helpers::rotateRect({-.5, 1.25, 2, .5}, 1), {-.75, -.5, .5, 2});
	expectRect(helpers::rotateRect({-.25, 1.25, 0, .5}, 1),
	           {-.75, -.25, .5, 0});
	expectRect(helpers::rotateRect({.25, .75, 0, 0}, 3), {.75, .75, 0, 0});
}
