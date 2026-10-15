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
#include "../app/pch.h"
#include "surface_test.h"

#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "../app/pdf/document.h"
#include "../app/pdf/engine.h"
#include "../app/pdf/page_filter.h"
#include "../app/pdf/surface.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {
constexpr int WIDTH = 640;
constexpr int STEP = 880;
constexpr int HEIGHT = STEP * 1000;

struct State {
	com_ptr<ID2D1DeviceContext> m_context;
	std::unique_ptr<pdf::PageFilter> m_filter;
	std::shared_ptr<pdf::Surface> m_surface;
	Image m_image;
	ScrollViewer m_scroll;
	TextBlock m_status;
	std::wstring m_preset = L"default";
	helpers::SliderValues m_values;
	std::shared_ptr<pdf::Bitmap> m_bitmap;
	// set while the surface draws with a device RemoveDevice can take away
	com_ptr<ID3D12Device5> m_removable;
	int m_uploads = 0;
	int m_draws = 0;
	int m_devices = 0;
	int m_losses = 0;
	bool m_closed = false;

	// Direct3D 11 can't remove its own device, so a removable one is
	// Direct3D 11 on 12, with the Direct3D 12 device returned in d3d12.
	// Normal devices are plain hardware Direct3D 11.
	com_ptr<ID2D1Device> makeDevice(com_ptr<ID3D12Device5>* d3d12) {
		com_ptr<ID3D11Device> d3d;
		if (d3d12) {
			check_hresult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
			                                guid_of<ID3D12Device5>(),
			                                d3d12->put_void()));
			D3D12_COMMAND_QUEUE_DESC desc{};
			desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			com_ptr<ID3D12CommandQueue> queue;
			check_hresult((*d3d12)->CreateCommandQueue(
			    &desc, guid_of<ID3D12CommandQueue>(), queue.put_void()));
			IUnknown* queues[] = {queue.get()};
			check_hresult(D3D11On12CreateDevice(
			    d3d12->get(), D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
			    queues, 1, 0, d3d.put(), nullptr, nullptr));
		} else {
			check_hresult(D3D11CreateDevice(
			    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
			    D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
			    d3d.put(), nullptr, nullptr));
		}
		com_ptr<ID2D1Factory1> factory;
		check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
		                                factory.put()));
		com_ptr<ID2D1Device> device;
		check_hresult(
		    factory->CreateDevice(d3d.as<IDXGIDevice>().get(), device.put()));
		++m_devices;
		return device;
	}

	std::unique_ptr<pdf::PageFilter> upload(ID2D1DeviceContext& context) {
		const auto props = D2D1::BitmapProperties1(
		    D2D1_BITMAP_OPTIONS_NONE,
		    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
		                      D2D1_ALPHA_MODE_PREMULTIPLIED));
		com_ptr<ID2D1Bitmap1> input;
		check_hresult(context.CreateBitmap(
		    D2D1::SizeU(m_bitmap->width, m_bitmap->height),
		    m_bitmap->pixels.data(), m_bitmap->stride, props, input.put()));
		++m_uploads;
		auto filter = std::make_unique<pdf::PageFilter>(context);
		filter->setInput(input.get());
		filter->setValues(m_values);
		return filter;
	}

	// what the viewer will do after device loss: new devices and bitmaps,
	// kept aside until the surface has accepted the device
	void rebuild(bool removable) {
		com_ptr<ID3D12Device5> d3d12;
		auto device = makeDevice(removable ? &d3d12 : nullptr);
		com_ptr<ID2D1DeviceContext> context;
		check_hresult(device->CreateDeviceContext(
		    D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context.put()));
		std::unique_ptr<pdf::PageFilter> filter;
		if (m_bitmap) {
			filter = upload(*context);
		}
		m_surface->setDevice(*device);
		m_context = std::move(context);
		m_filter = std::move(filter);
		m_removable = std::move(d3d12);
	}

	void draw(ID2D1DeviceContext& context, const RECT& rect) {
		context.Clear(D2D1::ColorF(0x1e1e1e));
		if (!m_filter) {
			return;
		}
		for (int page = rect.top / STEP; page * STEP < rect.bottom; page++) {
			context.DrawImage(
			    m_filter->output(),
			    D2D1::Point2F(22, static_cast<float>(page * STEP + 19)));
		}
		m_status.Text(L"hardware | uploads=" + std::to_wstring(m_uploads) +
		              L" | draw calls=" + std::to_wstring(++m_draws) +
		              L" | top=" + std::to_wstring(rect.top) + L" | devices=" +
		              std::to_wstring(m_devices) + L" | lost=" +
		              std::to_wstring(m_losses) + L" | scale=" +
		              std::to_wstring(m_image.XamlRoot().RasterizationScale()) +
		              L" | " + m_preset);
	}
};
} // namespace

// build with SableSurfaceTest=true, run from the repo root, then rebuild
// without it. One fixture raster is repeated, not a 1,000-page PDF.
Window surfaceTest(pdf::Engine& engine) {
	wchar_t tabsProbe[5]{};
	if (GetEnvironmentVariableW(L"SableTabsTest", tabsProbe, 5) == 4 &&
	    std::wstring_view(tabsProbe) == L"true") {
		return tabsTest(engine);
	}
	wchar_t errorProbe[5]{};
	if (GetEnvironmentVariableW(L"SableErrorTest", errorProbe, 5) == 4 &&
	    std::wstring_view(errorProbe) == L"true") {
		return viewerErrorTest(engine);
	}
	wchar_t scaleProbe[5]{};
	if (GetEnvironmentVariableW(L"SableScaleTest", scaleProbe, 5) == 4 &&
	    std::wstring_view(scaleProbe) == L"true") {
		return scaleTest(engine);
	}
	Window window;
	window.Title(L"Sable surface test");
	window.AppWindow().Resize({960, 900});
	auto state = std::make_shared<State>();
	std::weak_ptr<State> weak = state;
	auto device = state->makeDevice(nullptr);
	check_hresult(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
	                                          state->m_context.put()));
	state->m_surface = pdf::Surface::create(
	    *device, WIDTH, HEIGHT,
	    [weak](auto& context, const RECT& rect) {
		    if (auto self = weak.lock(); self && !self->m_closed) {
			    self->draw(context, rect);
		    }
	    },
	    [weak] {
		    if (auto self = weak.lock();
		        self && !self->m_closed && self->m_surface) {
			    ++self->m_losses;
			    try {
				    self->rebuild(false);
			    } catch (...) {
				    self->m_status.Text(L"FAIL: " + to_message());
			    }
		    }
	    });
	state->m_image.Source(state->m_surface->source());
	state->m_image.Width(WIDTH);
	state->m_image.Height(HEIGHT);
	state->m_image.Stretch(Media::Stretch::None);
	state->m_scroll.Content(state->m_image);
	state->m_scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Visible);
	state->m_status.Text(L"Loading fixture on the PDFium thread");
	state->m_status.TextWrapping(TextWrapping::Wrap);

	Grid root;
	RowDefinition header, content;
	header.Height({0, GridUnitType::Auto});
	content.Height({1, GridUnitType::Star});
	root.RowDefinitions().Append(header);
	root.RowDefinitions().Append(content);
	StackPanel controls, buttons;
	buttons.Orientation(Orientation::Horizontal);
	for (const wchar_t* label :
	     {L"default", L"sepia", L"redeye", L"original", L"Last page",
	      L"First page", L"Detach", L"Removable device", L"Remove device"}) {
		Button button;
		button.Content(box_value(label));
		button.Click([weak, name = std::wstring(label)](auto&&, auto&&) {
			if (auto self = weak.lock();
			    self && !self->m_closed && self->m_surface) {
				try {
					if (name == L"Detach") {
						self->m_surface.reset();
						self->m_status.Text(L"Detached; source kept by Image");
					} else if (name == L"Last page" || name == L"First page") {
						const double top =
						    name == L"Last page" ? HEIGHT - STEP : 0;
						self->m_scroll.ChangeView(nullptr, top, nullptr, true);
					} else if (name == L"Removable device") {
						self->rebuild(true);
					} else if (name == L"Remove device") {
						if (self->m_removable) {
							self->m_removable->RemoveDevice();
							self->m_surface->invalidate({0, 0, WIDTH, HEIGHT});
						}
					} else if (self->m_filter) {
						helpers::SliderValues values;
						helpers::handlePresetChange(to_string(name), values);
						self->m_filter->setValues(values);
						self->m_values = values;
						self->m_preset = name;
						self->m_surface->invalidate({0, 0, WIDTH, HEIGHT});
					}
				} catch (const hresult_error& error) {
					self->m_status.Text(L"FAIL: " + error.message());
				}
			}
		});
		buttons.Children().Append(button);
	}
	controls.Children().Append(buttons);
	controls.Children().Append(state->m_status);
	root.Children().Append(controls);
	Grid::SetRow(state->m_scroll, 1);
	root.Children().Append(state->m_scroll);
	window.Content(root);
	window.Closed([state](auto&&, auto&&) {
		state->m_closed = true;
		state->m_image.Source(nullptr);
		state->m_surface.reset();
		state->m_filter.reset();
		state->m_context = nullptr;
		state->m_removable = nullptr;
		state->m_image = nullptr;
		state->m_scroll = nullptr;
		state->m_status = nullptr;
	});

	auto queue = window.DispatcherQueue();
	engine.post([weak, queue] {
		std::shared_ptr<pdf::Bitmap> bitmap;
		HRESULT result = S_OK;
		try {
			auto opened = pdf::openFile(L"tests/fixtures/pages.pdf");
			if (!opened.document) {
				throw_hresult(E_FAIL);
			}
			auto raster = opened.document->render(0, 595, 842);
			if (!raster) {
				throw_hresult(E_FAIL);
			}
			bitmap = std::make_shared<pdf::Bitmap>(std::move(*raster));
		} catch (...) {
			result = to_hresult();
		}
		queue.TryEnqueue([weak, bitmap, result] {
			if (auto self = weak.lock();
			    self && !self->m_closed && self->m_surface) {
				try {
					check_hresult(result);
					self->m_bitmap = bitmap;
					self->m_filter = self->upload(*self->m_context);
					self->m_surface->invalidate({0, 0, WIDTH, HEIGHT});
				} catch (const hresult_error& error) {
					self->m_status.Text(L"FAIL: " + error.message());
				}
			}
		});
	});
	return window;
}
