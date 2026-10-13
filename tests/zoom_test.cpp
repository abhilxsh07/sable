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

#include <vector>

#include "zoom.h"

namespace {
// what pdf.js's own formula gives, run through Node: Math.ceil((s * 1.1)
// .toFixed(2) * 10) / 10 and the matching Math.floor for zooming out
const std::vector<double> UP_FROM_100 = {1.1, 1.3, 1.5, 1.7, 1.9, 2.1, 2.4,
                                         2.7, 3,   3.3, 3.7, 4.1, 4.6, 5.1,
                                         5.7, 6.3, 7,   7.7, 8.5, 9.4, 10};
const std::vector<double> DOWN_FROM_100 = {0.9, 0.8, 0.7, 0.6, 0.5,
                                           0.4, 0.3, 0.2, 0.1};

TEST(Zoom, StepsInLikePdfJs) {
	double zoom = 1;
	for (const double expected : UP_FROM_100) {
		zoom = helpers::zoomIn(zoom);
		EXPECT_DOUBLE_EQ(zoom, expected);
	}
	EXPECT_DOUBLE_EQ(helpers::zoomIn(zoom), helpers::MAX_ZOOM);
}

TEST(Zoom, StepsOutLikePdfJs) {
	double zoom = 1;
	for (const double expected : DOWN_FROM_100) {
		zoom = helpers::zoomOut(zoom);
		EXPECT_DOUBLE_EQ(zoom, expected);
	}
	EXPECT_DOUBLE_EQ(helpers::zoomOut(zoom), helpers::MIN_ZOOM);
}

TEST(Zoom, StepsFromInBetweenValues) {
	// "auto" usually lands on something like 125%
	EXPECT_DOUBLE_EQ(helpers::zoomIn(1.25), 1.4);
	EXPECT_DOUBLE_EQ(helpers::zoomOut(1.25), 1.1);
	EXPECT_DOUBLE_EQ(helpers::zoomIn(0.37), 0.5);
	EXPECT_DOUBLE_EQ(helpers::zoomOut(0.37), 0.3);
}

TEST(Zoom, SeveralStepsAtOnce) {
	EXPECT_DOUBLE_EQ(helpers::zoomIn(1, 3), 1.5);
	EXPECT_DOUBLE_EQ(helpers::zoomOut(1, 2), 0.8);
	EXPECT_DOUBLE_EQ(helpers::zoomIn(9.4, 5), helpers::MAX_ZOOM);
	EXPECT_DOUBLE_EQ(helpers::zoomOut(1, 20), helpers::MIN_ZOOM);
}

TEST(AutoZoom, PortraitFillsTheWidthUpTo125) {
	const helpers::PageSize a4{595.28, 841.89};
	EXPECT_DOUBLE_EQ(helpers::autoZoom(a4, 1920, 984), 1.25);
	EXPECT_NEAR(helpers::autoZoom(a4, 800, 600), 0.95753, 1e-5);
}

TEST(AutoZoom, LandscapeAlsoFitsTheHeight) {
	const helpers::PageSize landscape{841.89, 595.28};
	EXPECT_NEAR(helpers::autoZoom(landscape, 1920, 984), 1.23345, 1e-5);
	EXPECT_DOUBLE_EQ(helpers::autoZoom(landscape, 1920, 1200), 1.25);
}

TEST(AutoZoom, NoViewportYetMeansOneHundredPercent) {
	const helpers::PageSize a4{595.28, 841.89};
	EXPECT_DOUBLE_EQ(helpers::autoZoom(a4, 0, 0), 1);
	EXPECT_DOUBLE_EQ(helpers::autoZoom({0, 0}, 800, 600), 1);
}

TEST(WheelTicks, ANotchIsThreeAndAThirdTicks) {
	const double notch = helpers::wheelTicks(120, 3);
	EXPECT_NEAR(notch, 10.0 / 3.0, 1e-12);
	helpers::WheelTicks ticks;
	EXPECT_EQ(ticks.add(notch), 3);
	EXPECT_EQ(ticks.add(notch), 3);
	// the thirds add up to an extra tick
	EXPECT_EQ(ticks.add(notch), 4);
}

TEST(WheelTicks, SmallDeltasAddUp) {
	helpers::WheelTicks ticks;
	const double small = helpers::wheelTicks(10, 3);
	EXPECT_EQ(ticks.add(small), 0);
	EXPECT_EQ(ticks.add(small), 0);
	EXPECT_EQ(ticks.add(small), 0);
	EXPECT_EQ(ticks.add(small), 1);
}

TEST(WheelTicks, ChangingDirectionDropsTheRemainder) {
	helpers::WheelTicks ticks;
	EXPECT_EQ(ticks.add(helpers::wheelTicks(120, 3)), 3);
	EXPECT_EQ(ticks.add(helpers::wheelTicks(-120, 3)), -3);
	EXPECT_EQ(ticks.add(helpers::wheelTicks(-120, 3)), -3);
}

TEST(WheelTicks, MoreLinesPerNotchMeansMoreTicks) {
	EXPECT_NEAR(helpers::wheelTicks(120, 6), 20.0 / 3.0, 1e-12);
	EXPECT_NEAR(helpers::wheelTicks(120, 1), 10.0 / 9.0, 1e-12);
}

TEST(WheelTicks, PageScrollingIsOneTickPerEvent) {
	EXPECT_DOUBLE_EQ(helpers::wheelTicks(120, 0), 1);
	EXPECT_DOUBLE_EQ(helpers::wheelTicks(-240, -1), -1);
	EXPECT_DOUBLE_EQ(helpers::wheelTicks(0, 0), 0);
}
} // namespace
