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
// port of pdf.js v4.2.67's zoom steps (PDFViewer increaseScale and
// decreaseScale), its "auto" scale and app.js's wheel tick counting, which
// is what NightPDF's viewer did. Scale 1 is 100%, as in PageLayout.
// see:
// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/pdf_viewer.js
// see: https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/ui_utils.js
#pragma once

#include "layout.h"

namespace helpers {
inline constexpr double MIN_ZOOM = 0.1;
inline constexpr double MAX_ZOOM = 10.0;
// "auto" never goes past this, however wide the window
inline constexpr double MAX_AUTO_ZOOM = 1.25;

// steps times 1.1, each rounded up to the next tenth, at most MAX_ZOOM
double zoomIn(double zoom, int steps = 1);
// steps times 1/1.1, each rounded down to a tenth, at least MIN_ZOOM
double zoomOut(double zoom, int steps = 1);

// pdf.js's "auto": the page's width fills the viewport, less 40 for the
// scrollbar and borders, but no more than 125%. A landscape page also has
// to fit the height, less 5. Viewport sizes are in DIPs; 1 if it has none.
double autoZoom(const PageSize& page, double viewportWidth,
                double viewportHeight);

// app.js _accumulateTicks: whole ticks out, the remainder kept for the next
// call, and thrown away when the direction changes
class WheelTicks {
  public:
	int add(double ticks);

  private:
	double m_unused = 0;
};

// ticks for one wheel event. Chromium turns each line Windows scrolls per
// notch into 33.3 pixels (100 at the default 3 lines) and app.js counts 30
// pixels as a tick. linesPerNotch of 0 or less means Windows is set to
// scroll a page, which pdf.js sees as one tick per event.
// see:
// https://groups.google.com/a/chromium.org/g/blink-dev/c/U3kH6_98BuY/m/pUg0LwXyDAAJ
double wheelTicks(int wheelDelta, int linesPerNotch);
} // namespace helpers
