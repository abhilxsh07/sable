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
#if __has_include("ViewerPage.g.cpp")
#include "ViewerPage.g.cpp"
#endif

#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "helpers/filter.h"
#include "pdf/page_filter.h"
#include "pdf/session.h"
#include "pdf/surface.h"
#include "pdf/surface_util.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

// Visibility is also a property of the page
using XVisibility = Microsoft::UI::Xaml::Visibility;

namespace {
// pdf.js gives every page a 9px transparent border and a "1px auto -8px"
// margin, so the pages sit 11px apart and 10px in from the top
// see:
// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/pdf_viewer.css
constexpr double PAGE_GAP = 11;
constexpr double PAGE_MARGIN = 10;
// pdf.js's dark --body-bg-color, around and between the pages
// see: https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/viewer.css
constexpr D2D1_COLOR_F BACKGROUND{42 / 255.0f, 42 / 255.0f, 46 / 255.0f, 1};
// unfiltered rasters kept for scrolling back. An A4 page at 100% is about
// 3.5 MB, so this is a few dozen pages.
constexpr std::size_t CACHE_BYTES = std::size_t{256} << 20;
} // namespace

namespace winrt::Sable::implementation {
ViewerPage::ViewerPage() : m_cache(CACHE_BYTES) {}

// out of line, where Session and Surface are complete types
ViewerPage::~ViewerPage() = default;

void ViewerPage::open(pdf::Engine& engine, const std::filesystem::path& path,
                      std::optional<int> page) {
	if (m_closed || m_session) {
		return;
	}
	m_startPage = page;
	m_session = std::make_unique<pdf::Session>(engine);
	m_session->open(
	    path, {},
	    [weak = get_weak(), queue = DispatcherQueue()](pdf::SessionInfo info) {
		    auto result = std::make_shared<pdf::SessionInfo>(std::move(info));
		    queue.TryEnqueue([weak, result] {
			    if (auto self = weak.get()) {
				    self->opened(std::move(*result));
			    }
		    });
	    });
}

void ViewerPage::close() {
	m_closed = true;
	m_viewChanged.revoke();
	m_sizeChanged.revoke();
	m_layoutUpdated.revoke();
	m_session.reset();
	Pages().Source(nullptr);
	m_surface.reset();
	m_layout.reset();
	m_device = {};
	m_cache.clear();
}

void ViewerPage::opened(pdf::SessionInfo info) {
	if (m_closed || info.canceled) {
		return;
	}
	if (info.error != pdf::Error::None) {
		showMessage(to_hstring(pdf::errorMessage(info.error)));
		return;
	}
	if (info.pages.empty()) {
		showMessage(L"This PDF has no pages.");
		return;
	}
	try {
		std::vector<helpers::PageSize> sizes;
		sizes.reserve(info.pages.size());
		for (const auto& size : info.pages) {
			sizes.push_back({size.width, size.height});
		}
		m_scale = XamlRoot() ? XamlRoot().RasterizationScale() : 1.0;
		m_layout.emplace(sizes, 1.0, 0.0, PAGE_GAP, PAGE_MARGIN);
		const double width = std::ceil(m_layout->width() * m_scale);
		const double height = std::ceil(m_layout->height() * m_scale);
		if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
		    height <= 0 || width > pdf::MAX_SURFACE_SIDE ||
		    height > pdf::MAX_SURFACE_SIDE) {
			m_layout.reset();
			showMessage(L"This PDF is too long to show in one piece yet.");
			return;
		}
		m_device = makeDevice();
		m_surface = pdf::Surface::create(
		    *m_device.device, static_cast<int>(width), static_cast<int>(height),
		    [weak = get_weak()](ID2D1DeviceContext& context, const RECT& rect) {
			    if (auto self = weak.get()) {
				    self->draw(context, rect);
			    }
		    },
		    [weak = get_weak()] {
			    if (auto self = weak.get()) {
				    self->rebuildDevice();
			    }
		    });
		Pages().Source(m_surface->source());
		Pages().Width(width / m_scale);
		Pages().Height(height / m_scale);
		m_viewChanged = Scroller().ViewChanged(
		    auto_revoke, [this](auto&&, auto&&) { updateVisible(); });
		m_sizeChanged = SizeChanged(
		    auto_revoke, [this](auto&&, auto&&) { updateVisible(); });
		// opening can finish before the window's first layout. Wait for the
		// full extent before applying -p, rather than clamping it to zero.
		m_layoutUpdated = LayoutUpdated(
		    auto_revoke, [this](auto&&, auto&&) { updateVisible(); });
		updateVisible();
	} catch (...) {
		m_layout.reset();
		m_surface.reset();
		showMessage(L"Couldn't show this PDF: " + to_message());
	}
}

void ViewerPage::showMessage(const hstring& text) {
	Message().Text(text);
	Message().Visibility(XVisibility::Visible);
	Scroller().Visibility(XVisibility::Collapsed);
}

ViewerPage::Device ViewerPage::makeDevice() {
	com_ptr<ID3D11Device> d3d;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
	                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
	                             D3D11_SDK_VERSION, d3d.put(), nullptr,
	                             nullptr))) {
		// no usable GPU: the software rasterizer still shows the pages
		check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		                                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		                                nullptr, 0, D3D11_SDK_VERSION,
		                                d3d.put(), nullptr, nullptr));
	}
	com_ptr<ID2D1Factory1> factory;
	check_hresult(
	    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()));
	Device device;
	check_hresult(factory->CreateDevice(d3d.as<IDXGIDevice>().get(),
	                                    device.device.put()));
	check_hresult(device.device->CreateDeviceContext(
	    D2D1_DEVICE_CONTEXT_OPTIONS_NONE, device.context.put()));
	device.filter = std::make_unique<pdf::PageFilter>(*device.context);
	device.filter->setValues(m_values);
	// what pdf.js shows while a page loads: a white page, filtered
	const auto blank = helpers::applyFilter(m_values, {255, 255, 255});
	check_hresult(device.context->CreateSolidColorBrush(
	    D2D1::ColorF(blank.r / 255.0f, blank.g / 255.0f, blank.b / 255.0f),
	    device.placeholder.put()));
	return device;
}

// the surface lost its device: new devices, new GPU copies of the pages near
// the viewport, and only then hand the device over
void ViewerPage::rebuildDevice() {
	if (!m_surface) {
		return;
	}
	try {
		auto device = makeDevice();
		for (auto page = m_wanted.first; page < m_wanted.end; ++page) {
			if (const auto* bitmap = m_cache.find(key(page))) {
				upload(device, page, *bitmap);
			}
		}
		// kept first: if SetDevice fails the surface reports the loss again
		// and this runs again, instead of drawing old bitmaps on a new device
		m_device = std::move(device);
		m_surface->setDevice(*m_device.device);
		requestNext();
	} catch (...) {
		showMessage(L"The graphics device stopped working: " + to_message());
	}
}

void ViewerPage::updateVisible() {
	if (!m_layout || !m_surface) {
		return;
	}
	if (Scroller().ViewportHeight() > 0 &&
	    Scroller().ExtentHeight() >= Pages().Height() - 1) {
		m_layoutUpdated.revoke();
		const auto start = std::exchange(m_startPage, std::nullopt);
		if (start && *start >= 1 &&
		    static_cast<std::size_t>(*start) <= m_layout->pages().size()) {
			Scroller().ChangeView(
			    nullptr,
			    (std::max)(0.0, m_layout->pages()[*start - 1].y - PAGE_MARGIN),
			    nullptr, true);
		}
	}
	const double top = Scroller().VerticalOffset();
	double height = Scroller().ViewportHeight();
	if (height <= 0) {
		height = ActualHeight();
	}
	m_visible = m_layout->visiblePages(top, height, 0);
	m_wanted = m_layout->visiblePages(top, height, 1);
	// GPU copies only for the pages near the viewport; the rest stay cached
	std::erase_if(m_device.bitmaps, [this](const auto& entry) {
		return entry.first < m_wanted.first || entry.first >= m_wanted.end;
	});
	// this runs from XAML events, where an exception would end the app
	try {
		requestNext();
	} catch (...) {
		showMessage(L"Couldn't show this PDF: " + to_message());
	}
}

void ViewerPage::requestNext() {
	if (!m_session || !m_layout) {
		return;
	}
	for (const auto page : helpers::renderOrder(m_visible, m_wanted)) {
		if (m_device.bitmaps.contains(page)) {
			continue;
		}
		const auto wanted = key(page);
		if (const auto* bitmap = m_cache.find(wanted)) {
			if (upload(m_device, page, *bitmap)) {
				invalidate(page);
			}
			continue;
		}
		if (m_rendering || m_failed.contains(wanted)) {
			continue;
		}
		m_rendering = m_session->render(
		    wanted, [weak = get_weak(),
		             queue = DispatcherQueue()](pdf::RenderResult result) {
			    auto rendered =
			        std::make_shared<pdf::RenderResult>(std::move(result));
			    queue.TryEnqueue([weak, rendered] {
				    if (auto self = weak.get()) {
					    self->rendered(std::move(*rendered));
				    }
			    });
		    });
	}
}

void ViewerPage::rendered(pdf::RenderResult result) {
	if (m_closed) {
		return;
	}
	m_rendering = false;
	try {
		if (m_layout && !result.canceled && result.key.page >= 0) {
			const auto page = static_cast<std::size_t>(result.key.page);
			// only if it's still the size the page is drawn at
			if (page < m_layout->pages().size() && result.key == key(page)) {
				if (!result.bitmap) {
					m_failed.insert(result.key);
				} else {
					const bool shown = page >= m_wanted.first &&
					                   page < m_wanted.end &&
					                   upload(m_device, page, *result.bitmap);
					if (shown) {
						invalidate(page);
					}
					// kept nowhere means rendering it again would go the
					// same way, round and round
					if (!m_cache.put(result.key, std::move(*result.bitmap)) &&
					    !shown) {
						m_failed.insert(result.key);
					}
				}
			}
		}
		requestNext();
	} catch (...) {
		showMessage(L"Couldn't show this PDF: " + to_message());
	}
}

bool ViewerPage::upload(Device& device, std::size_t page,
                        const pdf::Bitmap& bitmap) {
	const auto props = D2D1::BitmapProperties1(
	    D2D1_BITMAP_OPTIONS_NONE,
	    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
	                      D2D1_ALPHA_MODE_PREMULTIPLIED));
	com_ptr<ID2D1Bitmap1> copy;
	// a lost device shows up here too; the surface notices it on its next
	// draw, until then the page stays a placeholder
	if (FAILED(device.context->CreateBitmap(
	        D2D1::SizeU(static_cast<UINT32>(bitmap.width),
	                    static_cast<UINT32>(bitmap.height)),
	        bitmap.pixels.data(), static_cast<UINT32>(bitmap.stride), props,
	        copy.put()))) {
		return false;
	}
	device.bitmaps[page] = std::move(copy);
	return true;
}

void ViewerPage::invalidate(std::size_t page) {
	const auto rect = pixels(page);
	m_surface->invalidate(
	    {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height});
}

// runs inside the surface's update callback, in pixels
void ViewerPage::draw(ID2D1DeviceContext& context, const RECT& rect) {
	context.Clear(BACKGROUND);
	if (!m_layout) {
		return;
	}
	const auto pages = m_layout->visiblePages(
	    rect.top / m_scale, (rect.bottom - rect.top) / m_scale, 1);
	for (auto page = pages.first; page < pages.end; ++page) {
		const auto at = pixels(page);
		const auto found = m_device.bitmaps.find(page);
		if (found != m_device.bitmaps.end()) {
			const auto size = found->second->GetPixelSize();
			if (static_cast<int>(size.width) == at.width &&
			    static_cast<int>(size.height) == at.height) {
				m_device.filter->setInput(found->second.get());
				context.DrawImage(m_device.filter->output(),
				                  D2D1::Point2F(static_cast<float>(at.x),
				                                static_cast<float>(at.y)));
				continue;
			}
		}
		context.FillRectangle(D2D1::RectF(static_cast<float>(at.x),
		                                  static_cast<float>(at.y),
		                                  static_cast<float>(at.x + at.width),
		                                  static_cast<float>(at.y + at.height)),
		                      m_device.placeholder.get());
	}
	// the effect mustn't keep the last page alive after the GPU cache drops it
	m_device.filter->setInput(nullptr);
}

helpers::PixelRect ViewerPage::pixels(std::size_t page) const {
	return helpers::toPixels(m_layout->pages()[page], m_scale);
}

pdf::PageKey ViewerPage::key(std::size_t page) const {
	const auto rect = pixels(page);
	return {static_cast<int>(page), rect.width, rect.height, 0};
}
} // namespace winrt::Sable::implementation
