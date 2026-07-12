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
#include "zoom.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace helpers {
namespace {
// i round to decimal hundredths before choosing the tenth for this step.
double stepScale(double value, bool increasing) {
	char text[64];
	const auto formatted = std::to_chars(text, text + sizeof(text), value,
	                                     std::chars_format::fixed, 2);
	if (formatted.ec != std::errc{}) {
		return value;
	}
	const auto point = std::find(text, formatted.ptr, '.');
	std::int64_t whole{};
	const auto parsed = std::from_chars(text, point, whole);
	if (parsed.ec != std::errc{} || parsed.ptr != point ||
	    formatted.ptr - point != 3 ||
	    whole >= (std::numeric_limits<std::int64_t>::max)() / 100 ||
	    whole <= (std::numeric_limits<std::int64_t>::min)() / 100) {
		return value;
	}
	const int fraction = (point[1] - '0') * 10 + point[2] - '0';
	const auto hundredths = whole * 100 + (value < 0 ? -fraction : fraction);
	auto tenths = hundredths / 10;
	if (hundredths % 10 != 0) {
		if (increasing && hundredths > 0) {
			++tenths;
		} else if (!increasing && hundredths < 0) {
			--tenths;
		}
	}
	return static_cast<double>(tenths) / 10;
}
} // namespace

double zoomIn(double zoom, int steps) {
	const int count = (std::max)(1, steps);
	for (int step = 0; step < count; ++step) {
		zoom = stepScale(zoom * 1.1, true);
		if (!(zoom < MAX_ZOOM)) {
			return (std::min)(MAX_ZOOM, zoom);
		}
	}
	return zoom;
}

double zoomOut(double zoom, int steps) {
	const int count = (std::max)(1, steps);
	for (int step = 0; step < count; ++step) {
		zoom = stepScale(zoom / 1.1, false);
		if (!(zoom > MIN_ZOOM)) {
			return (std::max)(MIN_ZOOM, zoom);
		}
	}
	return zoom;
}

double presetZoom(ZoomPreset preset, const PageSize& page, double viewportWidth,
                  double viewportHeight) {
	if (preset == ZoomPreset::None || preset == ZoomPreset::PageActual ||
	    !(page.width > 0 && page.height > 0 && viewportWidth > 0 &&
	      viewportHeight > 0)) {
		return 1;
	}
	const double pointsToPixels = 96.0 / 72.0;
	const double width = (viewportWidth - 40) / (page.width * pointsToPixels);
	const double height = (viewportHeight - 5) / (page.height * pointsToPixels);
	double scale = width;
	switch (preset) {
	case ZoomPreset::PageFit:
		scale = (std::min)(width, height);
		break;
	case ZoomPreset::Auto:
		scale = page.width <= page.height ? width : (std::min)(width, height);
		scale = (std::min)(MAX_AUTO_ZOOM, scale);
		break;
	default:
		break;
	}
	return std::clamp(scale, MIN_ZOOM, MAX_ZOOM);
}

double autoZoom(const PageSize& page, double viewportWidth,
                double viewportHeight) {
	return presetZoom(ZoomPreset::Auto, page, viewportWidth, viewportHeight);
}

bool followsViewport(ZoomPreset preset) {
	switch (preset) {
	case ZoomPreset::Auto:
	case ZoomPreset::PageFit:
	case ZoomPreset::PageWidth:
		return true;
	default:
		return false;
	}
}

std::optional<ZoomValue> zoomValue(std::string_view value) {
	auto numberText = value;
	const auto start = numberText.find_first_not_of(" \t\r\n");
	numberText.remove_prefix(start == std::string_view::npos ? numberText.size()
	                                                         : start);
	if (numberText.starts_with('+')) {
		numberText.remove_prefix(1);
	}
	double scale{};
	const auto parsed = std::from_chars(
	    numberText.data(), numberText.data() + numberText.size(), scale);
	if (parsed.ptr != numberText.data() && parsed.ec == std::errc{} &&
	    std::isfinite(scale) && scale > 0) {
		return ZoomValue{ZoomPreset::None,
		                 std::clamp(scale, MIN_ZOOM, MAX_ZOOM)};
	}
	if (value == "auto") {
		return ZoomValue{ZoomPreset::Auto};
	}
	if (value == "page-actual") {
		return ZoomValue{ZoomPreset::PageActual};
	}
	if (value == "page-fit") {
		return ZoomValue{ZoomPreset::PageFit};
	}
	if (value == "page-width") {
		return ZoomValue{ZoomPreset::PageWidth};
	}
	return {};
}

bool zoomChoiceSelected(const ZoomValue& choice, ZoomPreset preset,
                        double zoom) {
	if (preset != choice.preset) {
		return false;
	}
	if (preset == ZoomPreset::None) {
		return std::abs(zoom - choice.zoom) < 1e-9;
	}
	return true;
}

std::string zoomPercent(double zoom) {
	const auto hundredths = std::llround(zoom * 10000);
	std::string result = std::to_string(hundredths / 100);
	for (auto remaining = result.size(); remaining > 3; remaining -= 3) {
		result.insert(remaining - 3, 1, ',');
	}
	const auto fraction = hundredths % 100;
	if (fraction) {
		result += '.';
		if (fraction < 10) {
			result += '0';
		}
		result += std::to_string(fraction);
		if (result.back() == '0') {
			result.pop_back();
		}
	}
	result += '%';
	return result;
}

int WheelTicks::add(double ticks) {
	if (ticks != 0 && m_remainder != 0 &&
	    std::signbit(ticks) != std::signbit(m_remainder)) {
		m_remainder = 0;
	}
	double whole{};
	m_remainder = std::modf(m_remainder + ticks, &whole);
	return static_cast<int>(whole);
}

double wheelTicks(int wheelDelta, int linesPerNotch) {
	if (linesPerNotch <= 0) {
		if (wheelDelta == 0) {
			return 0;
		}
		return wheelDelta > 0 ? 1 : -1;
	}
	const double notches = wheelDelta / 120.0;
	const double pixels = notches * linesPerNotch * (100.0 / 3.0);
	return pixels / 30;
}
} // namespace helpers
