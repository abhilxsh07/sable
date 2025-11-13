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
#include "pan.h"
#include <algorithm>
#include <cmath>

namespace helpers {
namespace {
bool finite(PanPoint point) {
	return std::isfinite(point.x) && std::isfinite(point.y);
}
} // namespace
bool PanGesture::begin(PanPoint pointer, PanPoint offset) {
	m_active = false;
	m_moved = false;
	if (!finite(pointer) || !finite(offset)) {
		return false;
	}
	m_pointer = pointer;
	m_offset = offset;
	m_active = true;
	return true;
}
std::optional<PanPoint> PanGesture::move(PanPoint pointer, PanPoint maximum) {
	if (!m_active || !finite(pointer) || !finite(maximum)) {
		return std::nullopt;
	}
	const double dx = pointer.x - m_pointer.x, dy = pointer.y - m_pointer.y;
	if (!std::isfinite(dx) || !std::isfinite(dy)) {
		return std::nullopt;
	}
	if (!m_moved && std::hypot(dx, dy) <= 3) {
		return std::nullopt;
	}
	m_moved = true;
	return PanPoint{std::clamp(m_offset.x - dx, 0., (std::max)(0., maximum.x)),
	                std::clamp(m_offset.y - dy, 0., (std::max)(0., maximum.y))};
}
void PanGesture::end() {
	m_active = false;
}
} // namespace helpers
