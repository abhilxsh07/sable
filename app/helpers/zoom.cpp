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
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace helpers {
namespace {
constexpr double STEP = 1.1;
constexpr double PDF_TO_DIPS = 96.0 / 72.0;

// JavaScript's (value).toFixed(2), read back as a number
double toFixed2(double value) {
	char text[64];
	std::snprintf(text, sizeof(text), "%.2f", value);
	return std::strtod(text, nullptr);
}
} // namespace

double zoomIn(double zoom, int steps) {
	do {
		zoom = std::ceil(toFixed2(zoom * STEP) * 10) / 10;
	} while (--steps > 0 && zoom < MAX_ZOOM);
	return (std::min)(MAX_ZOOM, zoom);
}

double zoomOut(double zoom, int steps) {
	do {
		zoom = std::floor(toFixed2(zoom / STEP) * 10) / 10;
	} while (--steps > 0 && zoom > MIN_ZOOM);
	return (std::max)(MIN_ZOOM, zoom);
}

double presetZoom(ZoomPreset preset, const PageSize& page, double viewportWidth,
                  double viewportHeight) {
	if (preset == ZoomPreset::None || preset == ZoomPreset::PageActual ||
	    !(page.width > 0) || !(page.height > 0) || !(viewportWidth > 0) ||
	    !(viewportHeight > 0)) {
		return 1;
	}
	// SCROLLBAR_PADDING and VERTICAL_PADDING in ui_utils.js
	const double widthZoom = (viewportWidth - 40) / (page.width * PDF_TO_DIPS);
	const double heightZoom =
	    (viewportHeight - 5) / (page.height * PDF_TO_DIPS);
	double zoom = widthZoom;
	if (preset == ZoomPreset::PageFit) {
		zoom = (std::min)(widthZoom, heightZoom);
	} else if (preset == ZoomPreset::Auto) {
		const bool portrait = page.width <= page.height;
		zoom = (std::min)(MAX_AUTO_ZOOM,
		                  portrait ? widthZoom
		                           : (std::min)(widthZoom, heightZoom));
	}
	return std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
}

double autoZoom(const PageSize& page, double viewportWidth,
                double viewportHeight) {
	return presetZoom(ZoomPreset::Auto, page, viewportWidth, viewportHeight);
}

bool followsViewport(ZoomPreset preset) {
	return preset == ZoomPreset::Auto || preset == ZoomPreset::PageFit ||
	       preset == ZoomPreset::PageWidth;
}

std::optional<ZoomValue> zoomValue(std::string_view value) {
	// parseFloat skips leading white space and reads the longest number at
	// the start, so "1.5x" is 1.5 too
	auto text = value;
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
	                         text.front() == '\n' || text.front() == '\r')) {
		text.remove_prefix(1);
	}
	if (!text.empty() && text.front() == '+') {
		text.remove_prefix(1);
	}
	double number = 0;
	const auto [ptr, error] =
	    std::from_chars(text.data(), text.data() + text.size(), number);
	if (ptr != text.data() && error == std::errc{} && std::isfinite(number) &&
	    number > 0) {
		return ZoomValue{ZoomPreset::None,
		                 std::clamp(number, MIN_ZOOM, MAX_ZOOM)};
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
	return std::nullopt;
}

bool zoomChoiceSelected(const ZoomValue& choice, ZoomPreset preset,
                        double zoom) {
	if (choice.preset != preset) {
		return false;
	}
	return preset != ZoomPreset::None || std::abs(choice.zoom - zoom) < 1e-9;
}

std::string zoomPercent(double zoom) {
	// Math.round(pageScale * 10000) / 100
	const double percent = std::round(zoom * 10000) / 100;
	char text[64];
	std::snprintf(text, sizeof(text), "%.2f", percent);
	std::string number = text;
	while (number.back() == '0') {
		number.pop_back();
	}
	if (number.back() == '.') {
		number.pop_back();
	}
	// en-US grouping of the whole part
	const auto point = number.find('.');
	const auto digits = point == std::string::npos ? number.size() : point;
	for (auto at = digits; at > 3; at -= 3) {
		number.insert(at - 3, 1, ',');
	}
	return number + "%";
}

int WheelTicks::add(double ticks) {
	if ((m_unused > 0 && ticks < 0) || (m_unused < 0 && ticks > 0)) {
		m_unused = 0;
	}
	m_unused += ticks;
	const double whole = std::trunc(m_unused);
	m_unused -= whole;
	return static_cast<int>(whole);
}

double wheelTicks(int wheelDelta, int linesPerNotch) {
	if (linesPerNotch <= 0) {
		return wheelDelta > 0 ? 1 : (wheelDelta < 0 ? -1 : 0);
	}
	const double pixels = wheelDelta / 120.0 * linesPerNotch * (100.0 / 3.0);
	return pixels / 30.0;
}
} // namespace helpers
