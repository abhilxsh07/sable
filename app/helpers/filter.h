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
// port of app/helpers/private.ts updateCSS() (L118-148)
// see:
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/helpers/private.ts#L118-L148
#pragma once

#include <array>
#include <cstdint>

#include "sliders.h"

namespace helpers {
// What updateCSS() puts into the six css filter functions, in its order:
//   brightness(brightness) grayscale(grayscale) invert(invert) sepia(sepia)
//   hue-rotate(hue deg) brightness(extraBrightness)
// The amounts are 0..1 (extraBrightness can go up to 3), hue is in degrees.
// Careful: `brightness` here is the css amount, not the Darkness slider.
struct FilterParams {
	double brightness;
	double grayscale;
	double invert;
	double sepia;
	double hue;
	double extraBrightness;
};

struct Rgb8 {
	std::uint8_t r;
	std::uint8_t g;
	std::uint8_t b;

	bool operator==(const Rgb8&) const = default;
};

// a pixel in sRGB, 0..1 per channel, not premultiplied
struct Rgba {
	float r;
	float g;
	float b;
	float a;
};

// Same layout as D2D1_MATRIX_5X4_F. A pixel is the row vector [R G B A 1]
// and the output is that times the matrix, so row 0 holds what the input R
// adds to every output channel, and so on. Row 4 is the offset. The spec
// writes its matrices the other way round (one row per output channel), so
// they come out transposed here.
struct Matrix5x4 {
	float m[5][4];
};

// one matrix per css function, in the order of FilterParams
using FilterMatrices = std::array<Matrix5x4, 6>;

// port of updateCSS(), the part that turns the slider values into the
// arguments of the css functions
FilterParams updateFilter(const SliderValues& values);

// The six steps as Direct2D matrices. Each step has to be clamped to 0..1
// before the next one runs, so they can't be multiplied into one matrix.
FilterMatrices buildMatrices(const SliderValues& values);

// One matrix applied to one pixel, with the result clamped to 0..1 (that is
// what D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT does).
Rgba applyMatrix(const Matrix5x4& matrix, Rgba pixel);

// All six matrices in turn on an opaque pixel, rounded to 8 bits at the end.
Rgb8 applyMatrices(const FilterMatrices& matrices, Rgb8 pixel);

// The reference: the css filter chain worked out directly from the spec's
// formulas, without any matrices. Used as the oracle for the tests.
Rgb8 applyFilter(const SliderValues& values, Rgb8 pixel);
} // namespace helpers
