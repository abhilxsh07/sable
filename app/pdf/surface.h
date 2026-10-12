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

#include <d2d1_1.h>
#include <microsoft.ui.xaml.media.dxinterop.h>
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <functional>
#include <memory>

namespace pdf {
// UI thread only. Drawing coordinates and invalidation rectangles are pixels.
// After device loss the surface stops drawing and calls lost from the
// dispatcher queue, outside XAML's update callback. The owner then recreates
// its devices and bitmaps and passes the new device to setDevice, which
// redraws everything. lost shouldn't throw.
class Surface {
  public:
	using Draw = std::function<void(ID2D1DeviceContext&, const RECT&)>;
	using Lost = std::function<void()>;
	static std::shared_ptr<Surface> create(ID2D1Device& device, int width,
	                                       int height, Draw draw, Lost lost);
	~Surface();
	winrt::Microsoft::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource
	source() const;
	void invalidate(const RECT& rect);
	void setDevice(ID2D1Device& device);

  private:
	struct Updates;
	Surface(ID2D1Device& device, int width, int height, Draw draw, Lost lost);
	void drawUpdates();
	bool recover(HRESULT result);
	Draw m_draw;
	Lost m_lost;
	int m_width;
	int m_height;
	// no drawing until setDevice
	bool m_waiting = false;
	// a contents loss was answered with SetDevice and nothing drew since
	bool m_retried = false;
	std::weak_ptr<Surface> m_self;
	winrt::Microsoft::UI::Dispatching::DispatcherQueue m_queue{nullptr};
	winrt::com_ptr<ID2D1Device> m_device;
	winrt::Microsoft::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource
	    m_source{nullptr};
	winrt::com_ptr<IVirtualSurfaceImageSourceNative> m_native;
	winrt::com_ptr<ISurfaceImageSourceNativeWithD2D> m_drawing;
	winrt::com_ptr<IVirtualSurfaceUpdatesCallbackNative> m_updates;
};
} // namespace pdf
