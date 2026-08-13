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

#include "../pdf/annotations.h"

#include <optional>
#include <vector>

namespace helpers {
// One two-point stroke is a line; that and two two-point strokes starting
// at its end is an arrow. Anything else is neither.
pdf::LineKind lineShape(const std::vector<pdf::InkStroke>& strokes);
// The same line or arrow, length points long from the same start in the
// same direction, with the head drawn again for pen. PDF points, y up;
// nullopt when strokes aren't a line or length is under 4 pt.
std::optional<std::vector<pdf::InkStroke>>
resizedLine(const std::vector<pdf::InkStroke>& strokes, double length,
            double pen);
// A line, or an arrow with an open head at its end, between two displayed
// points, as ink strokes in the same displayed fractions. width and height
// are the displayed page in points and pen is the line width; the points
// keep half the pen and a little more inside the page. nullopt for a line
// under 4 pt.
std::optional<std::vector<pdf::DisplayedStroke>>
lineStrokes(pdf::AnnotationPoint from, pdf::AnnotationPoint to, double width,
            double height, double pen, bool arrow);
} // namespace helpers
