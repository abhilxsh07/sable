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
#include <optional>
#include <string>
#include <vector>

namespace pdf {
// Unrotated PDF coordinates, in points.
struct AnnotationBounds {
	float left = 0, bottom = 0, right = 0, top = 0;
};
// Fractions from the top left of the cropped, intrinsically rotated page.
struct AnnotationPoint {
	double x = 0, y = 0;
};

// These snapshots own their text and can outlive the document and engine.
struct Annotation {
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
};
} // namespace pdf
