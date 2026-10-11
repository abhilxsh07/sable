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
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <functional>
#include <memory>

namespace pdf {
// UI thread only. Drawing coordinates and invalidation rectangles are pixels.
// The owner recreates this surface and its bitmaps after device loss.
class Surface {
  public:
	using Draw = std::function<void(ID2D1DeviceContext&, const RECT&)>;
	static std::shared_ptr<Surface> create(ID2D1Device& device, int width,
	                                       int height, Draw draw);
	winrt::Microsoft::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource
	source() const;
	void invalidate(const RECT& rect);

  private:
	struct Updates;
	Surface(ID2D1Device& device, int width, int height, Draw draw);
	void drawUpdates();
	Draw m_draw;
	winrt::Microsoft::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource
	    m_source{nullptr};
	winrt::com_ptr<IVirtualSurfaceImageSourceNative> m_native;
	winrt::com_ptr<ISurfaceImageSourceNativeWithD2D> m_drawing;
	winrt::com_ptr<IVirtualSurfaceUpdatesCallbackNative> m_updates;
};
} // namespace pdf
