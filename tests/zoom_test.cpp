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

#include <cstdio>
#include <optional>
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

TEST(PresetZoom, ActualSizeIsAlwaysOneHundredPercent) {
	const helpers::PageSize a4{595.28, 841.89};
	EXPECT_DOUBLE_EQ(
	    helpers::presetZoom(helpers::ZoomPreset::PageActual, a4, 1920, 984), 1);
	EXPECT_DOUBLE_EQ(
	    helpers::presetZoom(helpers::ZoomPreset::PageActual, a4, 0, 0), 1);
}

TEST(PresetZoom, PageWidthAndPageFit) {
	using helpers::ZoomPreset;
	const helpers::PageSize a4{595.28, 841.89};
	// (1880 - 40) / (595.28 * 4/3) and (984 - 5) / (841.89 * 4/3)
	EXPECT_NEAR(helpers::presetZoom(ZoomPreset::PageWidth, a4, 1880, 984),
	            2.31824, 1e-5);
	EXPECT_NEAR(helpers::presetZoom(ZoomPreset::PageFit, a4, 1880, 984),
	            0.87214, 1e-5);
	// a narrow window: the width is what limits page-fit
	EXPECT_NEAR(helpers::presetZoom(ZoomPreset::PageFit, a4, 500, 984), 0.57956,
	            1e-5);
	// "auto" is page-width capped at 125% for this portrait page
	EXPECT_DOUBLE_EQ(helpers::presetZoom(ZoomPreset::Auto, a4, 1880, 984),
	                 1.25);
}

TEST(PresetZoom, KeptWithinTheZoomRange) {
	using helpers::ZoomPreset;
	const helpers::PageSize stamp{20, 20};
	EXPECT_DOUBLE_EQ(
	    helpers::presetZoom(ZoomPreset::PageWidth, stamp, 1880, 984),
	    helpers::MAX_ZOOM);
	const helpers::PageSize a4{595.28, 841.89};
	EXPECT_DOUBLE_EQ(helpers::presetZoom(ZoomPreset::PageFit, a4, 30, 984),
	                 helpers::MIN_ZOOM);
}

TEST(PresetZoom, OnlyTheFittingOnesFollowTheWindow) {
	using helpers::ZoomPreset;
	EXPECT_TRUE(helpers::followsViewport(ZoomPreset::Auto));
	EXPECT_TRUE(helpers::followsViewport(ZoomPreset::PageFit));
	EXPECT_TRUE(helpers::followsViewport(ZoomPreset::PageWidth));
	EXPECT_FALSE(helpers::followsViewport(ZoomPreset::PageActual));
	EXPECT_FALSE(helpers::followsViewport(ZoomPreset::None));
}

TEST(ZoomValue, MenuValuesLikeSetScale) {
	using helpers::ZoomPreset;
	using helpers::ZoomValue;
	EXPECT_EQ(helpers::zoomValue("auto"), (ZoomValue{ZoomPreset::Auto}));
	EXPECT_EQ(helpers::zoomValue("page-actual"),
	          (ZoomValue{ZoomPreset::PageActual}));
	EXPECT_EQ(helpers::zoomValue("page-fit"), (ZoomValue{ZoomPreset::PageFit}));
	EXPECT_EQ(helpers::zoomValue("page-width"),
	          (ZoomValue{ZoomPreset::PageWidth}));
	for (const double choice : helpers::ZOOM_CHOICES) {
		char text[16];
		std::snprintf(text, sizeof(text), "%g", choice);
		EXPECT_EQ(helpers::zoomValue(text),
		          (ZoomValue{ZoomPreset::None, choice}))
		    << text;
	}
}

TEST(ZoomValue, NumbersLikeParseFloat) {
	using helpers::ZoomPreset;
	using helpers::ZoomValue;
	EXPECT_EQ(helpers::zoomValue(" 1.5"), (ZoomValue{ZoomPreset::None, 1.5}));
	EXPECT_EQ(helpers::zoomValue("+2"), (ZoomValue{ZoomPreset::None, 2}));
	EXPECT_EQ(helpers::zoomValue("1.5x"), (ZoomValue{ZoomPreset::None, 1.5}));
	EXPECT_EQ(helpers::zoomValue("50"),
	          (ZoomValue{ZoomPreset::None, helpers::MAX_ZOOM}));
	EXPECT_EQ(helpers::zoomValue("0.01"),
	          (ZoomValue{ZoomPreset::None, helpers::MIN_ZOOM}));
	for (const char* text : {"", "0", "-1", "custom", "page-height", "Auto",
	                         " auto", "nan", "inf", "1e999"}) {
		EXPECT_EQ(helpers::zoomValue(text), std::nullopt) << '"' << text << '"';
	}
}

TEST(ZoomChoiceSelected, PresetFirstThenTheNumber) {
	using helpers::ZoomPreset;
	using helpers::ZoomValue;
	const ZoomValue fit{ZoomPreset::PageFit};
	const ZoomValue hundred{ZoomPreset::None, 1};
	// Page Fit is ticked, not 100%, even when page-fit works out to 100%
	EXPECT_TRUE(helpers::zoomChoiceSelected(fit, ZoomPreset::PageFit, 1));
	EXPECT_FALSE(helpers::zoomChoiceSelected(hundred, ZoomPreset::PageFit, 1));
	// Actual Size is a preset too, so 100% stays unticked
	EXPECT_FALSE(
	    helpers::zoomChoiceSelected(hundred, ZoomPreset::PageActual, 1));
	// a plain 100% ticks the 100% entry
	EXPECT_TRUE(helpers::zoomChoiceSelected(hundred, ZoomPreset::None, 1));
	EXPECT_FALSE(helpers::zoomChoiceSelected(fit, ZoomPreset::None, 1));
	// a custom zoom ticks nothing
	for (const double choice : helpers::ZOOM_CHOICES) {
		EXPECT_FALSE(helpers::zoomChoiceSelected({ZoomPreset::None, choice},
		                                         ZoomPreset::None, 1.1));
	}
}

TEST(ZoomPercent, LikeTheZoomMenu) {
	EXPECT_EQ(helpers::zoomPercent(1.25), "125%");
	EXPECT_EQ(helpers::zoomPercent(1.1), "110%");
	EXPECT_EQ(helpers::zoomPercent(0.1), "10%");
	EXPECT_EQ(helpers::zoomPercent(0.75), "75%");
	EXPECT_EQ(helpers::zoomPercent(10), "1,000%");
	EXPECT_EQ(helpers::zoomPercent(1.2345), "123.45%");
	EXPECT_EQ(helpers::zoomPercent(1.23456), "123.46%");
	EXPECT_EQ(helpers::zoomPercent(0.87214), "87.21%");
	EXPECT_EQ(helpers::zoomPercent(2.5), "250%");
	EXPECT_EQ(helpers::zoomPercent(1.005), "100.5%");
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
