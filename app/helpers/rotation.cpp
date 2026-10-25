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
#include "rotation.h"
#include <algorithm>

namespace helpers {
int quarterTurns(int value) noexcept {
	return (value % 4 + 4) % 4;
}
PageSize rotatedSize(PageSize size, int turns) noexcept {
	return quarterTurns(turns) % 2 ? PageSize{size.height, size.width} : size;
}
PagePoint rotatePoint(PagePoint point, int turns) noexcept {
	switch (quarterTurns(turns)) {
	case 1:
		return {1 - point.y, point.x};
	case 2:
		return {1 - point.x, 1 - point.y};
	case 3:
		return {point.y, 1 - point.x};
	default:
		return point;
	}
}
PageRect rotateRect(PageRect rect, int turns) noexcept {
	const auto first = rotatePoint({rect.x, rect.y}, turns);
	const auto last =
	    rotatePoint({rect.x + rect.width, rect.y + rect.height}, turns);
	const auto left = (std::min)(first.x, last.x);
	const auto top = (std::min)(first.y, last.y);
	return {left, top, (std::max)(first.x, last.x) - left,
	        (std::max)(first.y, last.y) - top};
}
} // namespace helpers
