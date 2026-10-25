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
#include "pch.h"
#include "ViewerPage.xaml.h"
#include <algorithm>
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::Sable::implementation {
void ViewerPage::updateGestureLimits() {
	if (m_resettingGesture || std::abs(Scroller().ZoomFactor() - 1.f) > 1e-5f) {
		return;
	}
	// The native transform is relative to the already rendered PDF zoom.
	// WinUI rejects a native minimum below 0.1; each pinch gets up to 10x.
	const float minimum =
	    (std::max)(.1f, static_cast<float>(helpers::MIN_ZOOM / m_zoom));
	const float maximum =
	    (std::min)(10.f, static_cast<float>(helpers::MAX_ZOOM / m_zoom));
	if (Scroller().MinZoomFactor() != minimum) {
		Scroller().MinZoomFactor(minimum);
	}
	if (Scroller().MaxZoomFactor() != maximum) {
		Scroller().MaxZoomFactor(maximum);
	}
}

// ViewChanged can finish before direct manipulation releases the viewport.
// I keep rendering in native coordinates until Windows finishes the gesture.
// see:
// https://learn.microsoft.com/windows/apps/develop/ui/controls/scroll-controls
void ViewerPage::nativeViewChanged(bool intermediate) {
	if (m_closed || !m_layout || !m_surface) {
		return;
	}
	const double factor = Scroller().ZoomFactor();
	if (m_resettingGesture) {
		if (std::abs(factor - 1) < 1e-5) {
			applyNativeZoom();
		}
		updateVisible();
		return;
	}
	if (std::abs(factor - 1) > 1e-5) {
		stopPan();
		cancelSelectionDrag();
		m_landingAt.reset();
		if (m_land) {
			m_land.Stop();
		}
		ZoomText().Text(to_hstring(helpers::zoomPercent(std::clamp(
		    m_zoom * factor, helpers::MIN_ZOOM, helpers::MAX_ZOOM))));
		if (!intermediate && !m_directManipulation) {
			queueNativeZoom();
		}
	}
	updateVisible();
}

void ViewerPage::queueNativeZoom() {
	if (!m_closed && m_layout && !m_directManipulation &&
	    std::abs(Scroller().ZoomFactor() - 1.f) < 1e-5f) {
		// A completed pan still needs to update the page number.
		updateVisible();
		return;
	}
	if (m_nativeZoomQueued || m_directManipulation || m_resettingGesture ||
	    m_closed || !m_layout ||
	    std::abs(Scroller().ZoomFactor() - 1.f) < 1e-5f) {
		return;
	}
	m_nativeZoomQueued = DispatcherQueue().TryEnqueue([weak = get_weak()] {
		if (const auto self = weak.get()) {
			self->m_nativeZoomQueued = false;
			self->finishNativeZoom();
		}
	});
}

void ViewerPage::finishNativeZoom() {
	if (m_closed || !m_layout || m_directManipulation || m_resettingGesture ||
	    std::abs(Scroller().ZoomFactor() - 1.f) < 1e-5f) {
		return;
	}
	try {
		const auto view = viewport();
		const double x = view.x + view.width / 2, y = view.y + view.height / 2;
		const auto page = m_layout->pageAt(x, y);
		const auto before = m_layout->pages()[page];
		m_nativeZoom =
		    NativeZoom{std::clamp(m_zoom * Scroller().ZoomFactor(),
		                          helpers::MIN_ZOOM, helpers::MAX_ZOOM),
		               (x - before.x) / before.width,
		               (y - before.y) / before.height, page};
		m_resettingGesture = true;
		m_holdCurrent = true;
		// Reset the compositor before changing the PDF extent. Resizing while
		// the old native scale is applied doubles the extent and clamps
		// offsets.
		const bool accepted =
		    Scroller().ChangeView(nullptr, nullptr, 1.f, true);
		if (std::abs(Scroller().ZoomFactor() - 1.f) < 1e-5f) {
			applyNativeZoom();
		} else if (!accepted) {
			// A rejected request must never suspend rendering or zoom labels.
			m_nativeZoom.reset();
			m_resettingGesture = false;
			m_holdCurrent = false;
		}
		if (m_nativeZoom) {
			if (!m_nativeResetTimeout) {
				m_nativeResetTimeout = DispatcherQueue().CreateTimer();
				m_nativeResetTimeout.Interval(std::chrono::milliseconds(200));
				m_nativeResetTimeout.IsRepeating(false);
				m_nativeResetTimeout.Tick([weak = get_weak()](auto&&, auto&&) {
					if (const auto self = weak.get(); self && !self->m_closed) {
						if (std::abs(self->Scroller().ZoomFactor() - 1.f) <
						    1e-5f) {
							self->applyNativeZoom();
						} else {
							self->m_nativeZoom.reset();
							self->m_resettingGesture = false;
							self->m_holdCurrent = false;
							self->showPosition();
							self->updateVisible();
						}
					}
				});
			}
			m_nativeResetTimeout.Start();
		}
	} catch (...) {
		m_nativeZoom.reset();
		m_resettingGesture = false;
		m_holdCurrent = false;
		showNotice(L"Couldn't finish zooming this page.");
	}
}

void ViewerPage::applyNativeZoom() {
	if (m_nativeResetTimeout) {
		m_nativeResetTimeout.Stop();
	}
	const auto target = std::exchange(m_nativeZoom, std::nullopt);
	m_resettingGesture = false;
	if (!target || m_closed || !m_layout) {
		m_holdCurrent = false;
		return;
	}
	try {
		if (!m_presentation && relayout(target->zoom)) {
			m_preset = helpers::ZoomPreset::None;
			settle();
		} else {
			showNotice(L"This layout is too large at the requested zoom.");
		}
		Scroller().UpdateLayout();
		const auto& page = m_layout->pages()[target->page];
		jumpTo(page.x + target->across * page.width + imageLeft() -
		           Scroller().ViewportWidth() / 2,
		       page.y + target->down * page.height -
		           Scroller().ViewportHeight() / 2);
		m_holdCurrent = false;
		showPosition();
		updateVisible();
	} catch (...) {
		m_holdCurrent = false;
		showNotice(L"Couldn't finish zooming this page.");
	}
}
} // namespace winrt::Sable::implementation
