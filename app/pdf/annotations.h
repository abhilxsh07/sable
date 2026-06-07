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
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdf {
// Unrotated PDF coordinates, in points.
struct AnnotationBounds {
	float left = 0, bottom = 0, right = 0, top = 0;
};
struct TextBoxSize {
	float width = 0, height = 0;
};
struct ShapeSize {
	float width = 0, height = 0;
};
struct StampImage {
	static constexpr unsigned maxSide = 2048;
	unsigned width = 0, height = 0;
	// tightly packed, top down, with straight alpha
	std::vector<std::uint8_t> bgra;

	bool valid() const {
		return width && height && width <= maxSide && height <= maxSide &&
		       bgra.size() == 4 * width * height;
	}
};
// Unrotated PDF coordinates, in points.
struct InkPoint {
	float x = 0, y = 0;
	bool operator==(const InkPoint&) const = default;
};
using InkStroke = std::vector<InkPoint>;
// Fractions from the top left of the cropped, intrinsically rotated page.
struct AnnotationPoint {
	double x = 0, y = 0;
};
// One drawn stroke, as points on the displayed page.
using DisplayedStroke = std::vector<AnnotationPoint>;

// Fractions from the top left of the cropped, intrinsically rotated page.
struct AnnotationRect {
	double left = 0, top = 0, right = 0, bottom = 0;
};

struct AnnotationTarget {
	int page = -1, index = -1;
	std::string subtype;
	std::uint64_t revision = 0;
	bool operator==(const AnnotationTarget&) const = default;
};
struct AnnotationQuad {
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	float x3 = 0, y3 = 0, x4 = 0, y4 = 0;
};
struct AnnotationColor {
	unsigned int red = 0, green = 0, blue = 0, alpha = 255;
	bool operator==(const AnnotationColor&) const = default;
};
// An opaque stroke color and its width in points.
struct InkStyle {
	AnnotationColor color{0, 0, 0, 255};
	float width = 2;
	bool operator==(const InkStyle&) const = default;
};
enum class HighlightColor { Yellow, Green, LightBlue, Pink, Red };
enum class OutlineShape { Rectangle, Ellipse };

// These snapshots own their text and can outlive the document and engine.
struct Annotation {
	AnnotationTarget target;
	// quads are PDF points; displayQuads keep the same corner order.
	std::vector<AnnotationQuad> quads, displayQuads;
	std::optional<AnnotationRect> displayBounds;
	std::optional<AnnotationColor> color;
	int index = 0, flags = 0;
	// The PDF subtype name, including names this reader doesn't recognize.
	std::string subtype, contents, author;
	std::optional<AnnotationBounds> bounds;
	std::optional<AnnotationPoint> position;
};
struct Annotations {
	int page = 0;
	std::vector<Annotation> entries;
	bool limited = false;
	std::uint64_t revision = 0;
};
} // namespace pdf
