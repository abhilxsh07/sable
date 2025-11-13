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
#include <limits>
#include "pan.h"

TEST(Pan, ThresholdDistinguishesClicksFromDrags) {
	helpers::PanGesture pan;
	EXPECT_FALSE(pan.move({10, 10}, {100, 100}));
	ASSERT_TRUE(pan.begin({20, 20}, {50, 60}));
	EXPECT_FALSE(pan.move({23, 20}, {100, 100}));
	EXPECT_FALSE(pan.moved());
	EXPECT_EQ(pan.move({24, 20}, {100, 100}), (helpers::PanPoint{46, 60}));
	EXPECT_TRUE(pan.moved());
	// Once dragging, returning to the anchor must not become a link click.
	EXPECT_EQ(pan.move({20, 20}, {100, 100}), (helpers::PanPoint{50, 60}));
	pan.end();
	EXPECT_FALSE(pan.active());
	EXPECT_TRUE(pan.moved());
	EXPECT_FALSE(pan.move({10, 10}, {100, 100}));
}
TEST(Pan, OriginalAnchorAvoidsDriftAndAllowsReversalAtEdges) {
	helpers::PanGesture pan;
	ASSERT_TRUE(pan.begin({100, 100}, {20, 30}));
	EXPECT_EQ(pan.move({0, 0}, {80, 90}), (helpers::PanPoint{80, 90}));
	EXPECT_EQ(pan.move({90, 80}, {80, 90}), (helpers::PanPoint{30, 50}));
	EXPECT_EQ(pan.move({150, 160}, {80, 90}), (helpers::PanPoint{0, 0}));
	EXPECT_EQ(pan.move({90, 80}, {-1, -1}), (helpers::PanPoint{0, 0}));
	ASSERT_TRUE(pan.begin({0, 0}, {0, 0}));
	EXPECT_FALSE(pan.moved());
	EXPECT_EQ(pan.move({-10, -15}, {80, 90}), (helpers::PanPoint{10, 15}));
}
TEST(Pan, InvalidCoordinatesCannotMoveOrStartAGesture) {
	const double inf = std::numeric_limits<double>::infinity();
	const double nan = std::numeric_limits<double>::quiet_NaN();
	helpers::PanGesture pan;
	EXPECT_FALSE(pan.begin({nan, 0}, {0, 0}));
	EXPECT_FALSE(pan.active());
	EXPECT_FALSE(pan.begin({0, 0}, {0, inf}));
	ASSERT_TRUE(pan.begin({0, 0}, {0, 0}));
	EXPECT_FALSE(pan.move({inf, 0}, {10, 10}));
	EXPECT_FALSE(pan.move({0, 5}, {nan, 10}));
	EXPECT_FALSE(pan.moved());
	EXPECT_EQ(pan.move({0, -5}, {10, 10}), (helpers::PanPoint{0, 5}));
	EXPECT_FALSE(pan.begin({inf, 0}, {0, 0}));
	EXPECT_FALSE(pan.active());
	EXPECT_FALSE(pan.moved());
}
