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
#include "surface.h"

#include <cstdio>

#include "../helpers/logging.h"
#include "surface_util.h"

namespace pdf {
static_assert(CONTENTS_LOST == static_cast<HRESULT>(E_SURFACE_CONTENTS_LOST));

static void logUpdateFailure(HRESULT result) noexcept {
	char message[80]{};
	std::snprintf(message, sizeof(message),
	              "Couldn't update page surface (0x%08X)",
	              static_cast<unsigned>(result));
	diagnostics::write(diagnostics::Level::Error, message);
}

struct Surface::Updates
    : winrt::implements<Updates, IVirtualSurfaceUpdatesCallbackNative> {
	explicit Updates(std::weak_ptr<Surface> owner)
	    : m_owner(std::move(owner)) {}
	HRESULT __stdcall UpdatesNeeded() noexcept override {
		try {
			if (auto owner = m_owner.lock()) {
				owner->drawUpdates();
			}
			return S_OK;
		} catch (...) {
			const auto result = winrt::to_hresult();
			logUpdateFailure(result);
			return result;
		}
	}
	// XAML can keep the source after its owner has gone away
	std::weak_ptr<Surface> m_owner;
};

std::shared_ptr<Surface> Surface::create(ID2D1Device& device, int width,
                                         int height, Draw draw, Lost lost) {
	auto surface = std::shared_ptr<Surface>(
	    new Surface(device, width, height, std::move(draw), std::move(lost)));
	surface->m_self = surface;
	surface->m_updates = winrt::make_self<Updates>(surface);
	winrt::check_hresult(
	    surface->m_native->RegisterForUpdatesNeeded(surface->m_updates.get()));
	return surface;
}

Surface::Surface(ID2D1Device& device, int width, int height, Draw draw,
                 Lost lost)
    : m_draw(std::move(draw)), m_lost(std::move(lost)), m_width(width),
      m_height(height), m_queue(winrt::Microsoft::UI::Dispatching::
                                    DispatcherQueue::GetForCurrentThread()),
      m_source(width, height, true),
      m_native(m_source.as<IVirtualSurfaceImageSourceNative>()),
      m_drawing(m_source.as<ISurfaceImageSourceNativeWithD2D>()) {
	if (!m_queue) {
		throw winrt::hresult_illegal_method_call();
	}
	winrt::check_hresult(m_drawing->SetDevice(&device));
	m_device.copy_from(&device);
}

Surface::~Surface() {
	// the Image can outlive this; a null callback unregisters ours
	// see:
	// https://raw.githubusercontent.com/microsoft/microsoft-ui-xaml/fe9d01273af3382ea570e203ab700772528d98df/dxaml/xcp/dxaml/lib/VirtualSurfaceImageSource_Partial.cpp
	m_native->RegisterForUpdatesNeeded(nullptr);
}

winrt::Microsoft::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource
Surface::source() const {
	return m_source;
}

void Surface::invalidate(const RECT& rect) {
	m_updateRetried = false;
	invalidateClipped(rect);
}

void Surface::invalidateClipped(const RECT& rect) {
	if (const auto clipped = clipToSurface(rect, m_width, m_height)) {
		winrt::check_hresult(m_native->Invalidate(*clipped));
	}
}

void Surface::resize(int width, int height) {
	winrt::check_hresult(m_native->Resize(width, height));
	m_width = width;
	m_height = height;
	invalidate({0, 0, m_width, m_height});
}

void Surface::setDevice(ID2D1Device& device) {
	winrt::check_hresult(m_drawing->SetDevice(&device));
	m_device.copy_from(&device);
	m_waiting = false;
	invalidate({0, 0, m_width, m_height});
}

void Surface::drawUpdates() {
	if (m_waiting) {
		return;
	}
	ULONG count = 0;
	winrt::check_hresult(m_native->GetUpdateRectCount(&count));
	if (count == 0) {
		return;
	}
	std::vector<RECT> rects(count);
	winrt::check_hresult(m_native->GetUpdateRects(rects.data(), count));
	for (auto current = rects.begin(); current != rects.end(); ++current) {
		const auto& rect = *current;
		winrt::com_ptr<ID2D1DeviceContext> context;
		POINT offset{};
		const auto begin = m_drawing->BeginDraw(
		    rect, __uuidof(ID2D1DeviceContext), context.put_void(), &offset);
		// nothing to end after a failed BeginDraw
		if (FAILED(begin)) {
			if (recover(begin)) {
				return;
			}
			rects.erase(rects.begin(), current);
			retryUpdates(std::move(rects));
			winrt::throw_hresult(begin);
		}
		const auto units = context->GetUnitMode();
		D2D1_MATRIX_3X2_F transform;
		context->GetTransform(&transform);
		context->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
		// the atlas offset is not the document's update rectangle origin
		// see:
		// https://learn.microsoft.com/en-us/windows/uwp/gaming/directx-and-xaml-interop
		context->SetTransform(D2D1::Matrix3x2F::Translation(
		    static_cast<float>(offset.x - rect.left),
		    static_cast<float>(offset.y - rect.top)));
		context->PushAxisAlignedClip(
		    D2D1::RectF(static_cast<float>(rect.left),
		                static_cast<float>(rect.top),
		                static_cast<float>(rect.right),
		                static_cast<float>(rect.bottom)),
		    D2D1_ANTIALIAS_MODE_ALIASED);
		HRESULT result = S_OK;
		try {
			m_draw(*context, rect);
		} catch (...) {
			result = winrt::to_hresult();
		}
		context->PopAxisAlignedClip();
		context->SetTransform(transform);
		context->SetUnitMode(units);
		const auto end = m_drawing->EndDraw();
		// a device loss from the draw takes precedence over lost contents
		auto failure = FAILED(result) ? result : end;
		if (drawLoss(end) != DrawLoss::None &&
		    drawLoss(result) != DrawLoss::Device) {
			failure = end;
		}
		if (FAILED(failure)) {
			if (recover(failure)) {
				return;
			}
			rects.erase(rects.begin(), current);
			retryUpdates(std::move(rects));
			winrt::throw_hresult(failure);
		}
	}
	m_retried = false;
	m_updateRetried = false;
}

// XAML already handed these rectangles to us. Give the failed tile and the
// tiles after it one more update, outside the active draw callback.
void Surface::retryUpdates(std::vector<RECT> rects) {
	if (m_updateRetried) {
		return;
	}
	m_updateRetried =
	    m_queue.TryEnqueue([weak = m_self, rects = std::move(rects)] {
		    if (auto self = weak.lock()) {
			    for (const auto& rect : rects) {
				    try {
					    self->invalidateClipped(rect);
				    } catch (...) {
					    logUpdateFailure(winrt::to_hresult());
				    }
			    }
		    }
	    });
}

// XAML ignores what UpdatesNeeded returns, so a lost device would just leave
// the page blank. SetDevice fails in the middle of a draw, so recovery waits
// for the dispatcher queue. Returns false for errors that aren't losses.
bool Surface::recover(HRESULT result) {
	auto loss = drawLoss(result);
	if (loss == DrawLoss::None) {
		return false;
	}
	// losing the contents again straight after SetDevice means the device
	// is gone too
	if (loss == DrawLoss::Contents && m_retried) {
		loss = DrawLoss::Device;
	}
	m_waiting = m_queue.TryEnqueue([weak = m_self, loss] {
		auto self = weak.lock();
		if (!self) {
			return;
		}
		if (loss == DrawLoss::Contents) {
			try {
				self->setDevice(*self->m_device);
				return;
			} catch (...) {
				self->m_retried = false;
			}
		}
		if (self->m_lost) {
			self->m_lost();
		}
	});
	m_retried = m_waiting && loss == DrawLoss::Contents;
	return m_waiting;
}
} // namespace pdf
