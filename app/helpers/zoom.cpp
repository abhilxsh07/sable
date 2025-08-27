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
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

double autoZoom(const PageSize& page, double viewportWidth,
                double viewportHeight) {
	if (!(page.width > 0) || !(page.height > 0) || !(viewportWidth > 0) ||
	    !(viewportHeight > 0)) {
		return 1;
	}
	const double widthZoom = (viewportWidth - 40) / (page.width * PDF_TO_DIPS);
	const double heightZoom =
	    (viewportHeight - 5) / (page.height * PDF_TO_DIPS);
	const bool portrait = page.width <= page.height;
	const double zoom =
	    portrait ? widthZoom : (std::min)(widthZoom, heightZoom);
	return std::clamp(zoom, MIN_ZOOM, MAX_AUTO_ZOOM);
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
