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
// port of app/helpers/sliders.ts handlePresetChange() (L1-57) and of the
// slider options in app/helpers/private.ts setupSliders() (L150-322)
// see:
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/helpers/sliders.ts
#pragma once

#include <array>
#include <string>
#include <string_view>

namespace helpers {
// the six slider values, in the order upstream lists them. `brightness` is
// the "Darkness" slider, `extraBrightness` the one labelled "Brightness".
// The initial values are the start values from setupSliders().
struct SliderValues {
	double brightness = 7;
	double grayness = 95;
	double inversion = 95;
	double sepia = 55;
	double hue = 180;
	double extraBrightness = 0;

	bool operator==(const SliderValues&) const = default;
};

// port of app/helpers/private.ts setupSliders() (L150-296), the options
// passed to create() for each slider. connect is always "lower".
struct SliderSpec {
	std::string_view label; // text of the <label> in index.html
	double min;
	double max;
	double start;
	double step;
	std::string_view unit; // what the tooltip appends, % or the degree sign
};

// the unit is UTF-8 on purpose, so it doesn't depend on the compiler's
// execution charset
inline constexpr std::string_view PERCENT = "%";
inline constexpr std::string_view DEGREE = "\xC2\xB0";

// same order as the `sliders` array in setupSliders(): darkness, grayscale,
// inversion, sepia, hue, brightness
inline constexpr std::array<SliderSpec, 6> SLIDERS = {{
    {"Darkness", 0, 100, 7, 1, PERCENT},
    {"Grayscale", 0, 100, 95, 1, PERCENT},
    {"Inversion", 0, 100, 95, 1, PERCENT},
    {"Sepia", 0, 100, 55, 1, PERCENT},
    // upstream showed % on the hue tooltip, but its parser expects the degree
    // sign. Fixed here.
    {"Hue", 0, 360, 180, 1, DEGREE},
    {"Brightness", -100, 200, 0, 1, PERCENT},
}};

// JavaScript's Math.round: halves go towards +infinity, so -2.5 gives -2
long long mathRound(double value);

// the tooltip's `to` function: `${Math.round(value)}${unit}`
std::string sliderTooltip(const SliderSpec& spec, double value);

// sets the six values for a preset. Any other name only sets the darkness
// to 9 and leaves the rest alone, like upstream's `default:` branch.
void handlePresetChange(const std::string& preset, SliderValues& values);
} // namespace helpers
