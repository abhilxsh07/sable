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
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/helpers/private.ts#L118-L148
#include "filter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace helpers {
namespace {
// The css filter functions work in sRGB, on the 8 bit values divided by 255
// (no linearization), and every step is clamped to 0..1.
// see: https://www.w3.org/TR/filter-effects-1/#FilterProperty
// see: https://www.w3.org/TR/filter-effects-1/#FilterPrimitivesOverviewIntro
struct Color {
	double r;
	double g;
	double b;
};

// as the spec writes it: one row per output channel, one column per input
using Spec3 = std::array<std::array<double, 3>, 3>;

double clamp01(double value) {
	return std::clamp(value, 0.0, 1.0);
}

// Some results sit exactly on a .5 (white through the sepia preset does).
// float noise would round those up or down depending on the path taken, so
// ties are pushed up a little.
std::uint8_t toByte(double value) {
	return static_cast<std::uint8_t>(
	    std::floor(clamp01(value) * 255 + 0.5 + 1e-4));
}

// see: https://www.w3.org/TR/filter-effects-1/#grayscaleEquivalent
Spec3 grayscaleSpec(double amount) {
	const double n = 1 - amount;
	return {{
	    {0.2126 + 0.7874 * n, 0.7152 - 0.7152 * n, 0.0722 - 0.0722 * n},
	    {0.2126 - 0.2126 * n, 0.7152 + 0.2848 * n, 0.0722 - 0.0722 * n},
	    {0.2126 - 0.2126 * n, 0.7152 - 0.7152 * n, 0.0722 + 0.9278 * n},
	}};
}

// see: https://www.w3.org/TR/filter-effects-1/#sepiaEquivalent
Spec3 sepiaSpec(double amount) {
	const double n = 1 - amount;
	return {{
	    {0.393 + 0.607 * n, 0.769 - 0.769 * n, 0.189 - 0.189 * n},
	    {0.349 - 0.349 * n, 0.686 + 0.314 * n, 0.168 - 0.168 * n},
	    {0.272 - 0.272 * n, 0.534 - 0.534 * n, 0.131 + 0.869 * n},
	}};
}

// hue-rotate() is feColorMatrix type="hueRotate", the matrix is
// L + cos(t) * C + sin(t) * S
// see: https://www.w3.org/TR/filter-effects-1/#huerotateEquivalent
// see: https://www.w3.org/TR/filter-effects-1/#feColorMatrixElement
Spec3 hueRotateSpec(double degrees) {
	static constexpr Spec3 L = {{
	    {0.213, 0.715, 0.072},
	    {0.213, 0.715, 0.072},
	    {0.213, 0.715, 0.072},
	}};
	static constexpr Spec3 C = {{
	    {0.787, -0.715, -0.072},
	    {-0.213, 0.285, -0.072},
	    {-0.213, -0.715, 0.928},
	}};
	static constexpr Spec3 S = {{
	    {-0.213, -0.715, 0.928},
	    {0.143, 0.140, -0.283},
	    {-0.787, 0.715, 0.072},
	}};
	const double radians = degrees * std::numbers::pi / 180;
	const double c = std::cos(radians);
	const double s = std::sin(radians);
	Spec3 out{};
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 3; col++) {
			out[row][col] = L[row][col] + c * C[row][col] + s * S[row][col];
		}
	}
	return out;
}

Color applySpec(const Spec3& a, Color in) {
	return {
	    clamp01(a[0][0] * in.r + a[0][1] * in.g + a[0][2] * in.b),
	    clamp01(a[1][0] * in.r + a[1][1] * in.g + a[1][2] * in.b),
	    clamp01(a[2][0] * in.r + a[2][1] * in.g + a[2][2] * in.b),
	};
}

// see: https://www.w3.org/TR/filter-effects-1/#brightnessEquivalent
Color brightness(double amount, Color in) {
	return {clamp01(amount * in.r), clamp01(amount * in.g),
	        clamp01(amount * in.b)};
}

// the spec's table [amount, 1 - amount] is a + (1 - 2a) * C
// see: https://www.w3.org/TR/filter-effects-1/#invertEquivalent
Color invert(double amount, Color in) {
	const double k = 1 - 2 * amount;
	return {clamp01(amount + k * in.r), clamp01(amount + k * in.g),
	        clamp01(amount + k * in.b)};
}

Matrix5x4 zeroMatrix() {
	return Matrix5x4{};
}

// spec element (row, col) goes to m[col][row], alpha stays as it is
Matrix5x4 fromSpec(const Spec3& spec) {
	Matrix5x4 out = zeroMatrix();
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 3; col++) {
			out.m[col][row] = static_cast<float>(spec[row][col]);
		}
	}
	out.m[3][3] = 1;
	return out;
}

Matrix5x4 brightnessMatrix(double amount) {
	Matrix5x4 out = zeroMatrix();
	for (int i = 0; i < 3; i++) {
		out.m[i][i] = static_cast<float>(amount);
	}
	out.m[3][3] = 1;
	return out;
}

Matrix5x4 invertMatrix(double amount) {
	Matrix5x4 out = zeroMatrix();
	for (int i = 0; i < 3; i++) {
		out.m[i][i] = static_cast<float>(1 - 2 * amount);
		// the offset goes into the fifth row
		out.m[4][i] = static_cast<float>(amount);
	}
	out.m[3][3] = 1;
	return out;
}
} // namespace

// port of app/helpers/private.ts updateCSS() (L118-148)
FilterParams updateFilter(const SliderValues& values) {
	return {
	    (100 - values.brightness) / 100,
	    values.grayness / 100,
	    values.inversion / 100,
	    values.sepia / 100,
	    values.hue,
	    (static_cast<double>(mathRound(values.extraBrightness)) + 100.0) / 100,
	};
}

// port of app/helpers/private.ts updateCSS() (L118-148)
FilterMatrices buildMatrices(const SliderValues& values) {
	const FilterParams p = updateFilter(values);
	return {
	    brightnessMatrix(p.brightness), fromSpec(grayscaleSpec(p.grayscale)),
	    invertMatrix(p.invert),         fromSpec(sepiaSpec(p.sepia)),
	    fromSpec(hueRotateSpec(p.hue)), brightnessMatrix(p.extraBrightness),
	};
}

Rgba applyMatrix(const Matrix5x4& matrix, Rgba pixel) {
	const float in[5] = {pixel.r, pixel.g, pixel.b, pixel.a, 1};
	float out[4];
	for (int col = 0; col < 4; col++) {
		float sum = 0;
		for (int row = 0; row < 5; row++) {
			sum += in[row] * matrix.m[row][col];
		}
		out[col] = std::clamp(sum, 0.0f, 1.0f);
	}
	return {out[0], out[1], out[2], out[3]};
}

Rgb8 applyMatrices(const FilterMatrices& matrices, Rgb8 pixel) {
	Rgba current = {pixel.r / 255.0f, pixel.g / 255.0f, pixel.b / 255.0f, 1};
	for (const Matrix5x4& matrix : matrices) {
		current = applyMatrix(matrix, current);
	}
	return {toByte(current.r), toByte(current.g), toByte(current.b)};
}

// port of app/helpers/private.ts updateCSS() (L118-148)
Rgb8 applyFilter(const SliderValues& values, Rgb8 pixel) {
	const FilterParams p = updateFilter(values);
	Color c = {pixel.r / 255.0, pixel.g / 255.0, pixel.b / 255.0};
	c = brightness(p.brightness, c);
	c = applySpec(grayscaleSpec(p.grayscale), c);
	c = invert(p.invert, c);
	c = applySpec(sepiaSpec(p.sepia), c);
	c = applySpec(hueRotateSpec(p.hue), c);
	c = brightness(p.extraBrightness, c);
	return {toByte(c.r), toByte(c.g), toByte(c.b)};
}
} // namespace helpers
