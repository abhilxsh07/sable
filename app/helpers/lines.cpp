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
pdf::LineKind lineShape(const std::vector<pdf::InkStroke>& strokes) {
	if (strokes.empty() || strokes[0].size() != 2 ||
	    strokes[0][0] == strokes[0][1]) {
		return pdf::LineKind::None;
	}
	if (strokes.size() == 1) {
		return pdf::LineKind::Line;
	}
	if (strokes.size() != 3) {
		return pdf::LineKind::None;
	}
	for (std::size_t i = 1; i < 3; ++i) {
		if (strokes[i].size() != 2 || strokes[i][0] != strokes[0][1]) {
			return pdf::LineKind::None;
		}
	}
	return pdf::LineKind::Arrow;
}

std::optional<std::vector<pdf::InkStroke>>
resizedLine(const std::vector<pdf::InkStroke>& strokes, double length,
            double pen) {
	const auto kind = lineShape(strokes);
	if (kind == pdf::LineKind::None || !std::isfinite(length) || length < 4 ||
	    !std::isfinite(pen) || pen <= 0) {
		return {};
	}
	const auto start = strokes[0][0], tip = strokes[0][1];
	const double dx = double(tip.x) - start.x, dy = double(tip.y) - start.y;
	const double current = std::hypot(dx, dy);
	if (!std::isfinite(current) || current <= 0) {
		return {};
	}
	const double ux = dx / current, uy = dy / current;
	const pdf::InkPoint end{static_cast<float>(start.x + ux * length),
	                        static_cast<float>(start.y + uy * length)};
	std::vector<pdf::InkStroke> out{{start, end}};
	if (kind == pdf::LineKind::Arrow) {
		// lineStrokes' head on the displayed page, where y runs down
		const double head = (std::min)((std::max)(8., 4 * pen), length / 3);
		const double vy = -uy;
		const double c = std::cos(std::numbers::pi / 6);
		const double s = std::sin(std::numbers::pi / 6);
		std::vector<pdf::InkPoint> ends;
		for (const double side : {1., -1.}) {
			const double hx = -ux * c + side * vy * s;
			const double hy = -vy * c - side * ux * s;
			ends.push_back({static_cast<float>(end.x + head * hx),
			                static_cast<float>(end.y - head * hy)});
		}
		// the head strokes keep the order they were written in
		const double ox = double(strokes[1][1].x) - tip.x;
		const double oy = double(strokes[1][1].y) - tip.y;
		const auto along = [&](const pdf::InkPoint& p) {
			return (p.x - end.x) * ox + (p.y - end.y) * oy;
		};
		if (along(ends[1]) > along(ends[0])) {
			std::swap(ends[0], ends[1]);
		}
		out.push_back({end, ends[0]});
		out.push_back({end, ends[1]});
	}
	return out;
}
} // namespace helpers
