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

#include <vector>

namespace pdf {
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
			return winrt::to_hresult();
		}
	}
	// XAML can keep the source after its owner has gone away
	std::weak_ptr<Surface> m_owner;
};

std::shared_ptr<Surface> Surface::create(ID2D1Device& device, int width,
                                         int height, Draw draw) {
	auto surface = std::shared_ptr<Surface>(
	    new Surface(device, width, height, std::move(draw)));
	surface->m_updates = winrt::make_self<Updates>(surface);
	winrt::check_hresult(
	    surface->m_native->RegisterForUpdatesNeeded(surface->m_updates.get()));
	return surface;
}

Surface::Surface(ID2D1Device& device, int width, int height, Draw draw)
    : m_draw(std::move(draw)), m_source(width, height, true),
      m_native(m_source.as<IVirtualSurfaceImageSourceNative>()),
      m_drawing(m_source.as<ISurfaceImageSourceNativeWithD2D>()) {
	winrt::check_hresult(m_drawing->SetDevice(&device));
}

winrt::Microsoft::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource
Surface::source() const {
	return m_source;
}

void Surface::invalidate(const RECT& rect) {
	winrt::check_hresult(m_native->Invalidate(rect));
}

void Surface::drawUpdates() {
	ULONG count = 0;
	winrt::check_hresult(m_native->GetUpdateRectCount(&count));
	if (count == 0) {
		return;
	}
	std::vector<RECT> rects(count);
	winrt::check_hresult(m_native->GetUpdateRects(rects.data(), count));
	for (const auto& rect : rects) {
		winrt::com_ptr<ID2D1DeviceContext> context;
		POINT offset{};
		winrt::check_hresult(m_drawing->BeginDraw(
		    rect, __uuidof(ID2D1DeviceContext), context.put_void(), &offset));
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
		winrt::check_hresult(result);
		winrt::check_hresult(end);
	}
}
} // namespace pdf
