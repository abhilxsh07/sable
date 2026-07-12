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

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "layout.h"

namespace helpers {
// scales are multipliers, with 1 representing actual size.
inline constexpr double MIN_ZOOM = 0.1;
inline constexpr double MAX_ZOOM = 10.0;
inline constexpr double MAX_AUTO_ZOOM = 1.25;

double zoomIn(double zoom, int steps = 1);
double zoomOut(double zoom, int steps = 1);

enum class ZoomPreset { None, Auto, PageActual, PageFit, PageWidth };
inline constexpr std::array<double, 8> ZOOM_CHOICES{0.5, 0.75, 1, 1.25,
                                                    1.5, 2,    3, 4};

// page sizes are PDF points. viewport sizes are device-independent pixels.
double presetZoom(ZoomPreset preset, const PageSize& page, double viewportWidth,
                  double viewportHeight);
double autoZoom(const PageSize& page, double viewportWidth,
                double viewportHeight);
bool followsViewport(ZoomPreset preset);

struct ZoomValue {
	ZoomPreset preset = ZoomPreset::None;
	double zoom = 1;
	bool operator==(const ZoomValue&) const = default;
};

std::optional<ZoomValue> zoomValue(std::string_view value);
bool zoomChoiceSelected(const ZoomValue& choice, ZoomPreset preset,
                        double zoom);
std::string zoomPercent(double zoom);

// i retain fractional ticks until a whole tick or a direction change.
class WheelTicks {
  public:
	int add(double ticks);

  private:
	double m_remainder = 0;
};

double wheelTicks(int wheelDelta, int linesPerNotch);
} // namespace helpers
