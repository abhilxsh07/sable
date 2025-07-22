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

#include <cmath>
#include <iterator>
#include <string>

#include "sliders.h"

using helpers::SliderValues;

namespace {
SliderValues preset(const std::string& name) {
	SliderValues values;
	helpers::handlePresetChange(name, values);
	return values;
}
} // namespace

// the values are in the order darkness, grayscale, inversion, sepia, hue,
// brightness
TEST(HandlePresetChange, Default) {
	const SliderValues v = preset("default");
	EXPECT_EQ(v.brightness, 7);
	EXPECT_EQ(v.grayness, 95);
	EXPECT_EQ(v.inversion, 95);
	EXPECT_EQ(v.sepia, 55);
	EXPECT_EQ(v.hue, 180);
	EXPECT_EQ(v.extraBrightness, 0);
}

TEST(HandlePresetChange, Sepia) {
	const SliderValues v = preset("sepia");
	EXPECT_EQ(v.brightness, 0);
	EXPECT_EQ(v.grayness, 0);
	EXPECT_EQ(v.inversion, 25);
	EXPECT_EQ(v.sepia, 100);
	EXPECT_EQ(v.hue, 0);
	EXPECT_EQ(v.extraBrightness, -30);
}

TEST(HandlePresetChange, Redeye) {
	const SliderValues v = preset("redeye");
	EXPECT_EQ(v.brightness, 8);
	EXPECT_EQ(v.grayness, 100);
	EXPECT_EQ(v.inversion, 92);
	EXPECT_EQ(v.sepia, 100);
	EXPECT_EQ(v.hue, 295);
	EXPECT_EQ(v.extraBrightness, -6);
}

TEST(HandlePresetChange, Original) {
	const SliderValues v = preset("original");
	EXPECT_EQ(v.brightness, 0);
	EXPECT_EQ(v.grayness, 0);
	EXPECT_EQ(v.inversion, 0);
	EXPECT_EQ(v.sepia, 0);
	EXPECT_EQ(v.hue, 0);
	EXPECT_EQ(v.extraBrightness, 0);
}

// a preset sets all six, whatever the sliders were before
TEST(HandlePresetChange, ReplacesEverySlider) {
	for (const char* name : {"default", "sepia", "redeye", "original"}) {
		SliderValues dirty{1, 2, 3, 4, 5, 6};
		helpers::handlePresetChange(name, dirty);
		EXPECT_EQ(dirty, preset(name)) << name;
	}
}

// upstream's `default:` case only sets the darkness to 9
TEST(HandlePresetChange, UnknownNameOnlySetsDarkness) {
	for (const char* name : {"custom", "", "Default", "REDEYE", "sepia "}) {
		SliderValues v{1, 2, 3, 4, 5, 6};
		helpers::handlePresetChange(name, v);
		EXPECT_EQ(v, (SliderValues{9, 2, 3, 4, 5, 6})) << "'" << name << "'";
	}
}

TEST(HandlePresetChange, UnknownNameKeepsThePreviousPreset) {
	SliderValues v = preset("redeye");
	helpers::handlePresetChange("custom", v);
	EXPECT_EQ(v, (SliderValues{9, 100, 92, 100, 295, -6}));
}

TEST(SliderValues, StartWithTheDefaultPreset) {
	EXPECT_EQ(SliderValues{}, preset("default"));
}

TEST(Sliders, RangesStepsAndStartValues) {
	struct Expected {
		const char* label;
		double min;
		double max;
		double start;
	};
	const Expected expected[] = {
	    {"Darkness", 0, 100, 7},   {"Grayscale", 0, 100, 95},
	    {"Inversion", 0, 100, 95}, {"Sepia", 0, 100, 55},
	    {"Hue", 0, 360, 180},      {"Brightness", -100, 200, 0},
	};
	ASSERT_EQ(helpers::SLIDERS.size(), std::size(expected));
	for (size_t i = 0; i < std::size(expected); i++) {
		const auto& spec = helpers::SLIDERS[i];
		EXPECT_EQ(spec.label, expected[i].label);
		EXPECT_EQ(spec.min, expected[i].min) << spec.label;
		EXPECT_EQ(spec.max, expected[i].max) << spec.label;
		EXPECT_EQ(spec.start, expected[i].start) << spec.label;
		EXPECT_EQ(spec.step, 1) << spec.label;
	}
}

TEST(Sliders, StartValuesAreTheDefaultPreset) {
	const SliderValues v;
	const double start[] = {v.brightness, v.grayness, v.inversion,
	                        v.sepia,      v.hue,      v.extraBrightness};
	for (size_t i = 0; i < helpers::SLIDERS.size(); i++) {
		EXPECT_EQ(start[i], helpers::SLIDERS[i].start) << i;
	}
}

// noUiSlider would snap and clamp, so every preset has to fit the sliders
TEST(Sliders, PresetsStayInsideTheRanges) {
	for (const char* name : {"default", "sepia", "redeye", "original"}) {
		const SliderValues v = preset(name);
		const double value[] = {v.brightness, v.grayness, v.inversion,
		                        v.sepia,      v.hue,      v.extraBrightness};
		for (size_t i = 0; i < helpers::SLIDERS.size(); i++) {
			const auto& spec = helpers::SLIDERS[i];
			EXPECT_GE(value[i], spec.min) << name << " " << spec.label;
			EXPECT_LE(value[i], spec.max) << name << " " << spec.label;
			EXPECT_EQ(std::fmod(value[i] - spec.min, spec.step), 0)
			    << name << " " << spec.label;
		}
	}
}

TEST(SliderTooltip, PercentForEverySliderButHue) {
	const auto& s = helpers::SLIDERS;
	EXPECT_EQ(helpers::sliderTooltip(s[0], 7), "7%");
	EXPECT_EQ(helpers::sliderTooltip(s[1], 95), "95%");
	EXPECT_EQ(helpers::sliderTooltip(s[2], 100), "100%");
	EXPECT_EQ(helpers::sliderTooltip(s[3], 0), "0%");
	EXPECT_EQ(helpers::sliderTooltip(s[5], -100), "-100%");
	EXPECT_EQ(helpers::sliderTooltip(s[5], 200), "200%");
}

// upstream showed "180%" here
TEST(SliderTooltip, HueUsesTheDegreeSign) {
	const auto& hue = helpers::SLIDERS[4];
	EXPECT_EQ(helpers::sliderTooltip(hue, 180), "180\xC2\xB0");
	EXPECT_EQ(helpers::sliderTooltip(hue, 0), "0\xC2\xB0");
	EXPECT_EQ(helpers::sliderTooltip(hue, 360), "360\xC2\xB0");
}

// same as `${Math.round(value)}`
TEST(SliderTooltip, RoundsLikeMathRound) {
	const auto& s = helpers::SLIDERS[0];
	EXPECT_EQ(helpers::sliderTooltip(s, 2.4), "2%");
	EXPECT_EQ(helpers::sliderTooltip(s, 2.5), "3%");
	EXPECT_EQ(helpers::sliderTooltip(s, -2.5), "-2%");
	EXPECT_EQ(helpers::sliderTooltip(s, -2.6), "-3%");
	EXPECT_EQ(helpers::sliderTooltip(s, -0.4), "0%");
	EXPECT_EQ(helpers::sliderTooltip(s, -0.5), "0%");
}
