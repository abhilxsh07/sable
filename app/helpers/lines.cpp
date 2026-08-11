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
#include "lines.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace helpers {
std::optional<std::vector<pdf::DisplayedStroke>>
lineStrokes(pdf::AnnotationPoint from, pdf::AnnotationPoint to, double width,
            double height, double pen, bool arrow) {
	const double inset = pen / 2 + .5;
	if (!std::isfinite(width) || !std::isfinite(height) ||
	    !std::isfinite(pen) || pen <= 0 || 2 * inset >= width ||
	    2 * inset >= height) {
		return {};
	}
	// points on the displayed page, y down
	const double x1 = from.x * width, y1 = from.y * height;
	const double x2 = to.x * width, y2 = to.y * height;
	const double dx = x2 - x1, dy = y2 - y1;
	const double length = std::hypot(dx, dy);
	if (!std::isfinite(length) || length < 4) {
		return {};
	}
	const auto fraction = [&](double x, double y) {
		return pdf::AnnotationPoint{std::clamp(x, inset, width - inset) / width,
		                            std::clamp(y, inset, height - inset) /
		                                height};
	};
	std::vector<pdf::DisplayedStroke> strokes{
	    {fraction(x1, y1), fraction(x2, y2)}};
	if (arrow) {
		// two strokes back from the tip, 30 degrees either side: four pens
		// long, at least 8 pt, and never more than a third of the line.
		const double head = (std::min)((std::max)(8., 4 * pen), length / 3);
		const double ux = dx / length, uy = dy / length;
		const double c = std::cos(std::numbers::pi / 6);
		const double s = std::sin(std::numbers::pi / 6);
		for (const double side : {1., -1.}) {
			const double hx = -ux * c + side * uy * s;
			const double hy = -uy * c - side * ux * s;
			strokes.push_back(
			    {fraction(x2, y2), fraction(x2 + head * hx, y2 + head * hy)});
		}
	}
	return strokes;
}
} // namespace helpers
