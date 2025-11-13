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
#include "layout.h"

namespace helpers {
struct PagePoint {
	double x = 0, y = 0;
	bool operator==(const PagePoint&) const = default;
};
int quarterTurns(int value) noexcept;
PageSize rotatedSize(PageSize size, int turns) noexcept;
// Clockwise around the unit square, with the origin at the top left.
// Coordinates outside the page stay outside; nothing is clamped.
PagePoint rotatePoint(PagePoint point, int turns) noexcept;
PageRect rotateRect(PageRect rect, int turns) noexcept;
} // namespace helpers
