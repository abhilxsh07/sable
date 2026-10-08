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

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "filter.h"

using helpers::Rgb8;
using helpers::Rgba;
using helpers::SliderValues;

namespace {
SliderValues preset(const std::string& name) {
	SliderValues values;
	helpers::handlePresetChange(name, values);
	return values;
}

// the four custom combinations make_fixtures.py renders as well
std::vector<SliderValues> customValues() {
	return {
	    {35, 40, 60, 30, 90, 10},
	    {0, 0, 0, 0, 200, 200},
	    {60, 0, 100, 0, 60, -100},
	    {20, 100, 50, 100, 330, 60},
	};
}

std::vector<SliderValues> allValues() {
	std::vector<SliderValues> all;
	for (const char* name : {"default", "sepia", "redeye", "original"}) {
		all.push_back(preset(name));
	}
	for (const SliderValues& values : customValues()) {
		all.push_back(values);
	}
	return all;
}

int maxDiff(Rgb8 a, Rgb8 b) {
	return std::max(
	    {std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
}

std::string show(Rgb8 c) {
	return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," +
	       std::to_string(c.b) + ")";
}

// walks up from the working directory, which is the repo root or the folder
// the exe is in (<repo>/<Platform>/<Config>)
std::filesystem::path findGolden() {
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::path dir = fs::current_path(ec);
	for (int i = 0; i < 8 && !dir.empty(); i++) {
		for (const char* sub : {"tests/golden", "golden", "."}) {
			const fs::path candidate = dir / sub / "filter_expected.json";
			if (fs::is_regular_file(candidate, ec)) {
				return candidate;
			}
		}
		if (dir == dir.parent_path()) {
			break;
		}
		dir = dir.parent_path();
	}
	return {};
}
} // namespace

TEST(UpdateFilter, DefaultPreset) {
	const helpers::FilterParams p = helpers::updateFilter(preset("default"));
	EXPECT_DOUBLE_EQ(p.brightness, 0.93);
	EXPECT_DOUBLE_EQ(p.grayscale, 0.95);
	EXPECT_DOUBLE_EQ(p.invert, 0.95);
	EXPECT_DOUBLE_EQ(p.sepia, 0.55);
	EXPECT_DOUBLE_EQ(p.hue, 180);
	EXPECT_DOUBLE_EQ(p.extraBrightness, 1);
}

TEST(UpdateFilter, ExtraBrightnessIsRoundedFirst) {
	SliderValues v;
	v.extraBrightness = -30;
	EXPECT_DOUBLE_EQ(helpers::updateFilter(v).extraBrightness, 0.7);
	v.extraBrightness = 200;
	EXPECT_DOUBLE_EQ(helpers::updateFilter(v).extraBrightness, 3);
	v.extraBrightness = 2.5;
	EXPECT_DOUBLE_EQ(helpers::updateFilter(v).extraBrightness, 1.03);
	v.extraBrightness = -2.5;
	EXPECT_DOUBLE_EQ(helpers::updateFilter(v).extraBrightness, 0.98);
}

// the values from the table of reference outputs, +-2 for the rounding
TEST(ApplyFilter, ReferenceTable) {
	struct Row {
		const char* name;
		Rgb8 expected[4]; // white, black, red, gray 128
	};
	const Row rows[] = {
	    {"default",
	     {{30, 32, 37}, {252, 252, 255}, {209, 220, 249}, {141, 152, 172}}},
	    {"sepia",
	     {{178, 161, 125}, {60, 54, 42}, {95, 85, 66}, {121, 108, 84}}},
	    {"redeye",
	     {{54, 40, 43}, {240, 230, 227}, {240, 203, 217}, {194, 143, 156}}},
	    {"original",
	     {{255, 255, 255}, {0, 0, 0}, {255, 0, 0}, {128, 128, 128}}},
	};
	const Rgb8 input[4] = {
	    {255, 255, 255}, {0, 0, 0}, {255, 0, 0}, {128, 128, 128}};
	const char* colorName[4] = {"white", "black", "red", "gray128"};
	for (const Row& row : rows) {
		const SliderValues values = preset(row.name);
		const helpers::FilterMatrices matrices = helpers::buildMatrices(values);
		for (int i = 0; i < 4; i++) {
			const Rgb8 cpu = helpers::applyFilter(values, input[i]);
			const Rgb8 d2d = helpers::applyMatrices(matrices, input[i]);
			EXPECT_LE(maxDiff(cpu, row.expected[i]), 2)
			    << row.name << " " << colorName[i] << " got " << show(cpu)
			    << " want " << show(row.expected[i]);
			EXPECT_LE(maxDiff(d2d, row.expected[i]), 2)
			    << row.name << " " << colorName[i] << " matrices got "
			    << show(d2d) << " want " << show(row.expected[i]);
		}
	}
}

// Default turns white paper into the color of the Default button
TEST(ApplyFilter, DefaultPresetOnWhiteIsDark) {
	const Rgb8 out = helpers::applyFilter(preset("default"), {255, 255, 255});
	EXPECT_LE(maxDiff(out, {30, 32, 36}), 2) << show(out);
}

TEST(ApplyFilter, OriginalChangesNothing) {
	const SliderValues original = preset("original");
	const helpers::FilterMatrices matrices = helpers::buildMatrices(original);
	for (int r = 0; r <= 255; r += 17) {
		for (int g = 0; g <= 255; g += 51) {
			for (int b = 0; b <= 255; b += 85) {
				const Rgb8 in = {static_cast<std::uint8_t>(r),
				                 static_cast<std::uint8_t>(g),
				                 static_cast<std::uint8_t>(b)};
				EXPECT_EQ(helpers::applyFilter(original, in), in) << show(in);
				EXPECT_EQ(helpers::applyMatrices(matrices, in), in) << show(in);
			}
		}
	}
}

// a full turn of the hue is no turn at all
TEST(ApplyFilter, HueOf360ChangesNothing) {
	SliderValues v = preset("original");
	v.hue = 360;
	const helpers::FilterMatrices matrices = helpers::buildMatrices(v);
	for (int c = 0; c <= 255; c += 15) {
		const Rgb8 in = {static_cast<std::uint8_t>(c),
		                 static_cast<std::uint8_t>(255 - c),
		                 static_cast<std::uint8_t>(c / 2)};
		EXPECT_EQ(helpers::applyFilter(v, in), in) << show(in);
		EXPECT_EQ(helpers::applyMatrices(matrices, in), in) << show(in);
	}
}

// the rows of the hue matrix each add up to 1 and the offsets to 0, so a
// gray stays gray whatever the angle is
TEST(ApplyFilter, HueRotateKeepsGraysGray) {
	for (double hue : {0.0, 45.0, 90.0, 180.0, 295.0, 360.0}) {
		SliderValues v = preset("original");
		v.hue = hue;
		const Rgba out = helpers::applyMatrix(helpers::buildMatrices(v)[4],
		                                      {0.5f, 0.5f, 0.5f, 1});
		EXPECT_NEAR(out.r, 0.5f, 1e-3) << hue;
		EXPECT_NEAR(out.g, 0.5f, 1e-3) << hue;
		EXPECT_NEAR(out.b, 0.5f, 1e-3) << hue;
	}
}

// The matrices are for a row vector, so the spec's rows become columns. A
// single color per channel shows that: with sepia(1) a pure red pixel comes
// out as the first column of the spec's matrix.
TEST(BuildMatrices, SpecRowsBecomeColumns) {
	SliderValues v = preset("original");
	v.sepia = 100;
	const helpers::Matrix5x4 sepia = helpers::buildMatrices(v)[3];
	const Rgba red = helpers::applyMatrix(sepia, {1, 0, 0, 1});
	EXPECT_NEAR(red.r, 0.393f, 1e-6);
	EXPECT_NEAR(red.g, 0.349f, 1e-6);
	EXPECT_NEAR(red.b, 0.272f, 1e-6);
	const Rgba green = helpers::applyMatrix(sepia, {0, 1, 0, 1});
	EXPECT_NEAR(green.r, 0.769f, 1e-6);
	EXPECT_NEAR(green.g, 0.686f, 1e-6);
	EXPECT_NEAR(green.b, 0.534f, 1e-6);
	const Rgba blue = helpers::applyMatrix(sepia, {0, 0, 1, 1});
	EXPECT_NEAR(blue.r, 0.189f, 1e-6);
	EXPECT_NEAR(blue.g, 0.168f, 1e-6);
	EXPECT_NEAR(blue.b, 0.131f, 1e-6);
	// and the same in the matrix itself: m[input][output]
	EXPECT_NEAR(sepia.m[0][0], 0.393f, 1e-6);
	EXPECT_NEAR(sepia.m[0][1], 0.349f, 1e-6);
	EXPECT_NEAR(sepia.m[1][0], 0.769f, 1e-6);
	EXPECT_NEAR(sepia.m[2][1], 0.168f, 1e-6);
}

TEST(BuildMatrices, HueRotateIsTransposedToo) {
	SliderValues v = preset("original");
	v.hue = 90; // cos = 0, sin = 1, so the matrix is L + S
	const helpers::Matrix5x4 hue = helpers::buildMatrices(v)[4];
	// spec row 0 is 0.213 - 0.213, 0.715 - 0.715, 0.072 + 0.928 = 0 0 1
	// spec row 1 is 0.213 + 0.143, 0.715 + 0.140, 0.072 - 0.283
	EXPECT_NEAR(hue.m[2][0], 1.0f, 1e-6);
	EXPECT_NEAR(hue.m[0][1], 0.356f, 1e-6);
	EXPECT_NEAR(hue.m[1][1], 0.855f, 1e-6);
	EXPECT_NEAR(hue.m[2][1], -0.211f, 1e-6);
	EXPECT_NEAR(hue.m[0][2], -0.574f, 1e-6);
}

TEST(BuildMatrices, BrightnessIsADiagonal) {
	SliderValues v = preset("original");
	v.brightness = 50; // brightness(0.5)
	const helpers::Matrix5x4 m = helpers::buildMatrices(v)[0];
	for (int row = 0; row < 5; row++) {
		for (int col = 0; col < 4; col++) {
			float want = 0;
			if (row == col && row < 3) {
				want = 0.5f;
			} else if (row == 3 && col == 3) {
				want = 1;
			}
			EXPECT_FLOAT_EQ(m.m[row][col], want) << row << "," << col;
		}
	}
}

// invert(a) is a + (1 - 2a) * C, and the a goes into the fifth row
TEST(BuildMatrices, InvertHasItsOffsetInTheFifthRow) {
	SliderValues v = preset("original");
	v.inversion = 25;
	const helpers::Matrix5x4 m = helpers::buildMatrices(v)[2];
	for (int i = 0; i < 3; i++) {
		EXPECT_FLOAT_EQ(m.m[i][i], 0.5f) << i;
		EXPECT_FLOAT_EQ(m.m[4][i], 0.25f) << i;
	}
	const Rgba out = helpers::applyMatrix(m, {0, 0.5f, 1, 1});
	EXPECT_FLOAT_EQ(out.r, 0.25f);
	EXPECT_FLOAT_EQ(out.g, 0.5f);
	EXPECT_FLOAT_EQ(out.b, 0.75f);
	EXPECT_FLOAT_EQ(out.a, 1);
}

TEST(BuildMatrices, AlphaIsIdentityInEveryStep) {
	for (const SliderValues& values : allValues()) {
		for (const helpers::Matrix5x4& m : helpers::buildMatrices(values)) {
			for (int row = 0; row < 5; row++) {
				// the alpha column only has the 1 on the diagonal
				EXPECT_EQ(m.m[row][3], row == 3 ? 1.0f : 0.0f) << row;
			}
			for (int col = 0; col < 3; col++) {
				EXPECT_EQ(m.m[3][col], 0.0f) << col;
			}
			const Rgba out = helpers::applyMatrix(m, {0.2f, 0.4f, 0.6f, 0.5f});
			EXPECT_FLOAT_EQ(out.a, 0.5f);
		}
	}
}

TEST(ApplyMatrix, ClampsTheResult) {
	SliderValues v = preset("original");
	v.sepia = 100;
	// white gives 1.351 for red and 1.203 for green before the clamp
	const Rgba out =
	    helpers::applyMatrix(helpers::buildMatrices(v)[3], {1, 1, 1, 1});
	EXPECT_FLOAT_EQ(out.r, 1);
	EXPECT_FLOAT_EQ(out.g, 1);
	EXPECT_FLOAT_EQ(out.b, 0.937f);

	v = preset("original");
	v.inversion = 100;
	const Rgba flipped =
	    helpers::applyMatrix(helpers::buildMatrices(v)[2], {1, 1, 1, 1});
	EXPECT_FLOAT_EQ(flipped.r, 0);
}

// The css steps are clamped one by one, so multiplying them into a single
// matrix would give another result. Sepia turns white into (1.351, 1.203,
// 0.937) and the clamp makes it (1, 1, 0.937) before the hue step. Without
// the clamp blue would end up at 1.08 -> 255, with it at 0.991 -> 253.
TEST(ApplyFilter, ClampsAfterEveryStep) {
	SliderValues v = preset("original");
	v.sepia = 100;
	v.hue = 90;
	const Rgb8 want = {239, 255, 253};
	const Rgb8 cpu = helpers::applyFilter(v, {255, 255, 255});
	const Rgb8 d2d =
	    helpers::applyMatrices(helpers::buildMatrices(v), {255, 255, 255});
	EXPECT_LE(maxDiff(cpu, want), 1) << show(cpu);
	EXPECT_LE(maxDiff(d2d, want), 1) << show(d2d);
}

// the Direct2D path against the direct formulas, for a grid of colors
TEST(ApplyMatrices, EqualsTheCpuPathForAGridOfColors) {
	const int levels[] = {0, 51, 102, 128, 153, 204, 255};
	for (const SliderValues& values : allValues()) {
		const helpers::FilterMatrices matrices = helpers::buildMatrices(values);
		for (int r : levels) {
			for (int g : levels) {
				for (int b : levels) {
					const Rgb8 in = {static_cast<std::uint8_t>(r),
					                 static_cast<std::uint8_t>(g),
					                 static_cast<std::uint8_t>(b)};
					const Rgb8 cpu = helpers::applyFilter(values, in);
					const Rgb8 d2d = helpers::applyMatrices(matrices, in);
					EXPECT_EQ(d2d, cpu)
					    << "in " << show(in) << " cpu " << show(cpu)
					    << " matrices " << show(d2d) << " with "
					    << values.brightness << "/" << values.grayness << "/"
					    << values.inversion << "/" << values.sepia << "/"
					    << values.hue << "/" << values.extraBrightness;
				}
			}
		}
	}
}

// Chromium's own output, made by tests/golden/make_fixtures.py
TEST(Golden, MatchesChromium) {
	const std::filesystem::path path = findGolden();
	if (path.empty()) {
		GTEST_SKIP() << "tests/golden/filter_expected.json not found, make it "
		                "with: uv run tests/golden/make_fixtures.py";
	}
	std::ifstream file(path);
	ASSERT_TRUE(file.is_open()) << path;
	const nlohmann::json golden = nlohmann::json::parse(file, nullptr, false);
	ASSERT_FALSE(golden.is_discarded()) << path << " is not valid json";
	ASSERT_TRUE(golden.is_array() && !golden.empty()) << path;

	int checked = 0;
	int failures = 0;
	for (const auto& entry : golden) {
		const std::string name = entry.at("name").get<std::string>();
		const auto& json = entry.at("values");
		SliderValues values;
		values.brightness = json.at("brightness").get<double>();
		values.grayness = json.at("grayness").get<double>();
		values.inversion = json.at("inversion").get<double>();
		values.sepia = json.at("sepia").get<double>();
		values.hue = json.at("hue").get<double>();
		values.extraBrightness = json.at("extraBrightness").get<double>();
		const helpers::FilterMatrices matrices = helpers::buildMatrices(values);

		for (const auto& c : entry.at("cases")) {
			const Rgb8 in = {c.at("in").at(0).get<std::uint8_t>(),
			                 c.at("in").at(1).get<std::uint8_t>(),
			                 c.at("in").at(2).get<std::uint8_t>()};
			const Rgb8 want = {c.at("out").at(0).get<std::uint8_t>(),
			                   c.at("out").at(1).get<std::uint8_t>(),
			                   c.at("out").at(2).get<std::uint8_t>()};
			const Rgb8 cpu = helpers::applyFilter(values, in);
			const Rgb8 d2d = helpers::applyMatrices(matrices, in);
			checked++;
			if (maxDiff(cpu, want) > 2 || maxDiff(d2d, want) > 2) {
				failures++;
				// the first few are enough to see what is going on
				if (failures <= 10) {
					ADD_FAILURE() << name << " in " << show(in) << " chromium "
					              << show(want) << " cpu " << show(cpu)
					              << " matrices " << show(d2d);
				}
			}
		}
	}
	EXPECT_GT(checked, 0);
	EXPECT_EQ(failures, 0) << failures << " of " << checked
	                       << " differ by more than 2";
}
