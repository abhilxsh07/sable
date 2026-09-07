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
#include <chrono>
#include <cmath>

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include "helpers/logging.h"
#include "pdf/surface.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using XVisibility = Microsoft::UI::Xaml::Visibility;
namespace {
// how long a freeze frame waits to be drawn, and then for the new layout
constexpr std::chrono::milliseconds FREEZE_WAIT{300};
} // namespace
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
		// a new zoom has started under the last one's freeze frame
		if (m_freezeStage == FreezeStage::Landing) {
			hideFreeze();
		}
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
		// the reset waits for the freeze frame to reach the screen; without
		// one it goes ahead, and the frame in between can show
		if (!showFreeze()) {
			resetNativeZoom();
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not finish page zoom");
		dropNativeZoom();
		showNotice(L"Couldn't finish zooming this page.");
	}
}

// Reset the compositor before changing the PDF extent. Resizing while the old
// native scale is applied doubles the extent and clamps offsets.
void ViewerPage::resetNativeZoom() {
	if (m_freezeStage == FreezeStage::Showing) {
		m_freezeStage = FreezeStage::Resetting;
	}
	if (!m_nativeZoom || m_closed || !m_layout) {
		dropNativeZoom();
		return;
	}
	try {
		const bool accepted =
		    Scroller().ChangeView(nullptr, nullptr, 1.f, true);
		const bool landed = std::abs(Scroller().ZoomFactor() - 1.f) < 1e-5f;
		diagnostics::write(diagnostics::Level::Debug,
		                   landed ? "native zoom reset landed at once"
		                          : "native zoom reset waits for ViewChanged");
		if (landed) {
			applyNativeZoom();
		} else if (!accepted) {
			// A rejected request must never suspend rendering or zoom labels.
			dropNativeZoom();
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
							self->dropNativeZoom();
							self->showPosition();
							self->updateVisible();
						}
					}
				});
			}
			m_nativeResetTimeout.Start();
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not reset native zoom");
		dropNativeZoom();
		showNotice(L"Couldn't finish zooming this page.");
	}
}

// the gesture's zoom given up: rendering and the zoom label carry on
void ViewerPage::dropNativeZoom() {
	m_nativeZoom.reset();
	m_resettingGesture = false;
	m_holdCurrent = false;
	hideFreeze();
}

void ViewerPage::applyNativeZoom() {
	if (m_nativeResetTimeout) {
		m_nativeResetTimeout.Stop();
	}
	const auto target = std::exchange(m_nativeZoom, std::nullopt);
	m_resettingGesture = false;
	if (!target || m_closed || !m_layout) {
		m_holdCurrent = false;
		hideFreeze();
		return;
	}
	try {
		if (!presenting() && relayout(target->zoom)) {
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
		// the freeze frame comes down once the new layout is on screen
		if (m_freezeStage != FreezeStage::None) {
			m_freezeStage = FreezeStage::Landing;
			m_freezeSince = std::chrono::steady_clock::now();
			m_freezeFrames = 0;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not restore the zoom surface");
		m_holdCurrent = false;
		hideFreeze();
		showNotice(L"Couldn't finish zooming this page.");
	}
}

void ViewerPage::nativeGestureStarted() {
	hideFreeze();
	m_nativeZoom.reset();
	m_resettingGesture = false;
	m_holdCurrent = false;
	if (m_nativeResetTimeout) {
		m_nativeResetTimeout.Stop();
	}
	m_directManipulation = true;
	m_landingAt.reset();
	++m_linkVersion;
	m_linkPending = false;
}

// The freeze frame: a surface the size of the viewport over the scroller,
// drawn by the page drawing at the gesture's scale and offsets, so it matches
// the last frame of the gesture.
bool ViewerPage::showFreeze() {
	hideFreeze();
	if (presenting() || !m_surface || !m_device.device) {
		return false;
	}
	try {
		const double factor = Scroller().ZoomFactor();
		const double width = Scroller().ViewportWidth();
		const double height = Scroller().ViewportHeight();
		const int wide = static_cast<int>(std::lround(width * m_scale));
		const int high = static_cast<int>(std::lround(height * m_scale));
		if (factor <= 0 || wide <= 0 || high <= 0) {
			return false;
		}
		m_freeze = Freeze{
		    (Scroller().HorizontalOffset() / factor - imageLeft()) * m_scale,
		    Scroller().VerticalOffset() / factor * m_scale, factor};
		m_freezeSurface = pdf::Surface::create(
		    *m_device.device, wide, high,
		    [weak = get_weak()](ID2D1DeviceContext& context, const RECT& rect) {
			    if (auto self = weak.get()) {
				    self->drawFreeze(context, rect);
			    }
		    },
		    [weak = get_weak()] {
			    if (auto self = weak.get()) {
				    self->dropFreeze();
			    }
		    });
		m_freezeSurface->invalidate({0, 0, wide, high});
		auto image = GestureFreeze();
		image.Source(m_freezeSurface->source());
		image.Width(width);
		image.Height(height);
		image.Visibility(XVisibility::Visible);
		m_freezeStage = FreezeStage::Showing;
		m_freezeSince = std::chrono::steady_clock::now();
		m_freezeFrames = 0;
		m_freezeRendering = Media::CompositionTarget::Rendering(
		    auto_revoke, [weak = get_weak()](auto&&, auto&&) {
			    if (auto self = weak.get()) {
				    self->freezeFrame();
			    }
		    });
#ifdef SABLE_SURFACE_TEST
		++m_freezeShows;
#endif
		return true;
	} catch (...) {
		hideFreeze();
		return false;
	}
}

// never from inside drawFreeze: this destroys the surface that's drawing
void ViewerPage::hideFreeze() {
	m_freezeRendering.revoke();
	m_freezeStage = FreezeStage::None;
	m_freeze.reset();
	if (m_freezeSurface) {
		auto image = GestureFreeze();
		image.Visibility(XVisibility::Collapsed);
		image.Source(nullptr);
		m_freezeSurface.reset();
	}
}

// the freeze frame lost its device; a reset waiting for it goes ahead
void ViewerPage::dropFreeze() {
	const bool waiting = m_freezeStage == FreezeStage::Showing;
	hideFreeze();
	if (waiting) {
		resetNativeZoom();
	}
}

// Rendering runs on the UI thread before each frame, so a freeze frame drawn
// while one frame was made is on screen by the next call. The handler keeps
// the UI thread busy every frame, so it only lives as long as the freeze.
// see:
// https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.media.compositiontarget.rendering
void ViewerPage::freezeFrame() {
	if (m_closed || m_freezeStage == FreezeStage::None) {
		hideFreeze();
		return;
	}
	++m_freezeFrames;
	const bool late =
	    std::chrono::steady_clock::now() - m_freezeSince > FREEZE_WAIT;
	if (m_freezeStage == FreezeStage::Showing) {
		// a freeze frame that never gets drawn (a hidden window) mustn't
		// hold the zoom up
		if ((m_freeze && m_freeze->drawn) || late) {
			resetNativeZoom();
		}
	} else if (m_freezeStage == FreezeStage::Landing) {
		const auto landing = m_landingAt;
		const bool landed =
		    !landing ||
		    (std::abs(Scroller().HorizontalOffset() - landing->first) < 1 &&
		     std::abs(Scroller().VerticalOffset() - landing->second) < 1);
		// a frame for the new layout first, then down once it's in place
		if ((m_freezeFrames > 1 && landed) || late) {
			hideFreeze();
		}
	}
}
} // namespace winrt::Sable::implementation
