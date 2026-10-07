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
// see:
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/helpers/sliders.ts
#include "sliders.h"

#include <cmath>

namespace helpers {
long long mathRound(double value) {
	return static_cast<long long>(std::floor(value + 0.5));
}

// port of app/helpers/private.ts setupSliders() (L150-296), tooltips[0].to
std::string sliderTooltip(const SliderSpec& spec, double value) {
	// a long long never prints "-0", which is what `${Math.round(-0.4)}` gives
	return std::to_string(mathRound(value)) + std::string(spec.unit);
}

// port of app/helpers/sliders.ts handlePresetChange() (L1-57)
void handlePresetChange(const std::string& preset, SliderValues& values) {
	// upstream sets the six noUiSliders, here it's the plain values
	if (preset == "default") {
		values.brightness = 7;
		values.grayness = 95;
		values.inversion = 95;
		values.sepia = 55;
		values.hue = 180;
		values.extraBrightness = 0;
	} else if (preset == "original") {
		values.brightness = 0;
		values.grayness = 0;
		values.inversion = 0;
		values.sepia = 0;
		values.hue = 0;
		values.extraBrightness = 0;
	} else if (preset == "redeye") {
		values.brightness = 8;
		values.grayness = 100;
		values.inversion = 92;
		values.sepia = 100;
		values.hue = 295;
		values.extraBrightness = -6;
	} else if (preset == "sepia") {
		values.brightness = 0;
		values.grayness = 0;
		values.inversion = 25;
		values.sepia = 100;
		values.hue = 0;
		values.extraBrightness = -30;
	} else {
		values.brightness = 9;
	}
	// upstream logs `preset, "changed"` here with console.debug
}
} // namespace helpers
