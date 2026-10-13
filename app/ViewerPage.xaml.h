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

#include "ViewerPage.g.h"

#include <d2d1_1.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "helpers/layout.h"
#include "helpers/sliders.h"
#include "helpers/zoom.h"
#include "pdf/page_cache.h"

namespace pdf {
class Engine;
class PageFilter;
class Session;
class Surface;
struct RenderResult;
struct SessionInfo;
} // namespace pdf

namespace winrt::Sable::implementation {
// UI thread only. The session does the PDFium work on the engine thread and
// its callbacks come back here through the dispatcher queue.
struct ViewerPage : ViewerPageT<ViewerPage> {
	ViewerPage();
	~ViewerPage();

	// opens path and, once the pages are laid out, scrolls to page (1-based,
	// like -p). One document per page; tabs will give each its own.
	void open(pdf::Engine& engine, const std::filesystem::path& path,
	          std::optional<int> page);
	// lets go of the document while the engine is still around
	void close();
	// changes only the GPU filter; page rasters stay in the cache
	void setValues(const helpers::SliderValues& values);
	// pdf.js's zoom: about 10% a step, or back to "auto"
	void zoomIn(int steps = 1);
	void zoomOut(int steps = 1);
	void zoomReset();

  private:
	struct Device {
		winrt::com_ptr<ID2D1Device> device;
		winrt::com_ptr<ID2D1DeviceContext> context;
		std::unique_ptr<pdf::PageFilter> filter;
		winrt::com_ptr<ID2D1SolidColorBrush> placeholder;
		std::map<std::size_t, winrt::com_ptr<ID2D1Bitmap1>> bitmaps;
	};

	void opened(pdf::SessionInfo info);
	bool relayout(double zoom);
	bool setZoom(double zoom, winrt::Windows::Foundation::Point anchor);
	void fitAuto();
	void settle();
	double imageLeft();
	void onWheel(
	    const winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs& args);
	void rendered(pdf::RenderResult result);
	void showMessage(const winrt::hstring& text);
	Device makeDevice();
	void rebuildDevice();
	void updateVisible();
	void requestNext();
	bool upload(Device& device, std::size_t page, const pdf::Bitmap& bitmap);
	void invalidate(std::size_t page);
	void draw(ID2D1DeviceContext& context, const RECT& rect);
	helpers::PixelRect pixels(std::size_t page) const;
	pdf::PageKey key(std::size_t page) const;

	std::unique_ptr<pdf::Session> m_session;
	// page sizes in points, to lay them out again at another zoom
	std::vector<helpers::PageSize> m_sizes;
	std::optional<helpers::PageLayout> m_layout;
	double m_zoom = 1;
	// pdf.js opens at "auto" and keeps it until a zoom key or the wheel
	bool m_autoZoom = true;
	// opening can finish before the viewport has a size, so "auto" is
	// worked out again once it has one
	bool m_fitPending = false;
	// right after a zoom the old rasters are stretched; sharp ones are
	// rendered once it has been still for a moment
	bool m_settling = false;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_settle{nullptr};
	helpers::WheelTicks m_wheelTicks;
	std::optional<int> m_startPage;
	double m_scale = 1;
	helpers::SliderValues m_values;
	helpers::PageRange m_visible;
	helpers::PageRange m_wanted;
	pdf::PageCache m_cache;
	// renders that came back empty, so they aren't asked for again
	std::set<pdf::PageKey> m_failed;
	bool m_rendering = false;
	bool m_closed = false;
	Device m_device;
	std::shared_ptr<pdf::Surface> m_surface;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::ViewChanged_revoker
	    m_viewChanged;
	winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker
	    m_sizeChanged;
	winrt::Microsoft::UI::Xaml::FrameworkElement::LayoutUpdated_revoker
	    m_layoutUpdated;
};
} // namespace winrt::Sable::implementation

namespace winrt::Sable::factory_implementation {
struct ViewerPage : ViewerPageT<ViewerPage, implementation::ViewerPage> {};
} // namespace winrt::Sable::factory_implementation
