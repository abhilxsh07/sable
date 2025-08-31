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

#include <array>
#include <optional>
#include <string>
#include <string_view>

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

// pdf.js's named zoom values ("auto", "page-actual", "page-fit" and
// "page-width"); None means a plain number
enum class ZoomPreset { None, Auto, PageActual, PageFit, PageWidth };

// the percentages in pdf.js's zoom menu (viewer.html #scaleSelect)
// see: https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/viewer.html
inline constexpr std::array<double, 8> ZOOM_CHOICES = {0.5, 0.75, 1, 1.25,
                                                       1.5, 2,    3, 4};

// pdf_viewer.js #setScale for a named value, for the current page in a
// viewport in DIPs: "page-actual" is 100%, "page-width" fills the width
// less 40 for the scrollbar and borders, "page-fit" also fits the height
// less 5, and "auto" is page-width for a portrait page and page-fit for a
// landscape one, but no more than 125%. Kept within MIN_ZOOM..MAX_ZOOM,
// which pdf.js didn't do (a window under 40 wide gave it a negative
// scale). 1 if the viewport or the page has no size yet.
double presetZoom(ZoomPreset preset, const PageSize& page, double viewportWidth,
                  double viewportHeight);

// presetZoom for "auto", what a document opens at
double autoZoom(const PageSize& page, double viewportWidth,
                double viewportHeight);

// app.js webViewerResize works "auto", "page-fit" and "page-width" out
// again when the viewer changes size; "page-actual" stays at 100%
bool followsViewport(ZoomPreset preset);

// a value from pdf.js's zoom menu
struct ZoomValue {
	ZoomPreset preset = ZoomPreset::None;
	double zoom = 1; // when preset is None
	bool operator==(const ZoomValue&) const = default;
};

// #setScale's reading of a value: a number the way parseFloat reads it
// ("1.5" is 150%) if it's above 0, else one of the preset names. Numbers
// are kept within MIN_ZOOM..MAX_ZOOM. nullopt for an unknown value.
std::optional<ZoomValue> zoomValue(std::string_view value);

// toolbar.js #updateUIState: whether a menu entry is the selected one, the
// entry for the preset if there is one, else the entry with the same number.
// A zoom that matches no entry ("custom") leaves every entry unselected.
bool zoomChoiceSelected(const ZoomValue& choice, ZoomPreset preset,
                        double zoom);

// what the zoom menu shows for a zoom that isn't one of its entries:
// toolbar.js rounds to a hundredth of a percent and pdfjs-page-scale-percent
// prints it like an en-US number, so "125%", "123.45%" or "1,000%"
std::string zoomPercent(double zoom);

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
