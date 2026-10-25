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

namespace helpers {
struct PanPoint {
	double x = 0, y = 0;
	bool operator==(const PanPoint&) const = default;
};
// Viewport coordinates and scroll offsets are DIPs. Each update uses the
// original anchor, so fractional motion and clamping cannot accumulate drift.
class PanGesture {
  public:
	bool begin(PanPoint pointer, PanPoint offset);
	std::optional<PanPoint> move(PanPoint pointer, PanPoint maximum);
	void end();
	bool active() const {
		return m_active;
	}
	bool moved() const {
		return m_moved;
	}

  private:
	PanPoint m_pointer, m_offset;
	bool m_active = false, m_moved = false;
};
} // namespace helpers
