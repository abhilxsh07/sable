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
#include "graphics.h"
#include <d2d1_1helper.h>

#pragma comment(lib, "windowscodecs.lib")

using namespace winrt;
namespace printing {
Graphics::Graphics() {
	auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
	                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
	                                0, D3D11_SDK_VERSION, m_d3d.put(), nullptr,
	                                m_immediate.put());
	if (FAILED(result)) {
		check_hresult(D3D11CreateDevice(
		    nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		    D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
		    m_d3d.put(), nullptr, m_immediate.put()));
	}
	check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
	                                __uuidof(ID2D1Factory1), nullptr,
	                                m_factory.put_void()));
	check_hresult(
	    m_factory->CreateDevice(m_d3d.as<IDXGIDevice>().get(), m_device.put()));
	check_hresult(m_device->CreateDeviceContext(
	    D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_context.put()));
}
void Graphics::draw(const pdf::Bitmap& raster, D2D1_RECT_F rect) {
	com_ptr<ID2D1Bitmap1> bitmap;
	const auto properties = D2D1::BitmapProperties1(
	    D2D1_BITMAP_OPTIONS_NONE,
	    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
	check_hresult(m_context->CreateBitmap(
	    D2D1::SizeU(raster.width, raster.height), raster.pixels.data(),
	    raster.stride, properties, bitmap.put()));
	m_context->DrawBitmap(bitmap.get(), rect, 1,
	                      D2D1_INTERPOLATION_MODE_LINEAR);
}
com_ptr<IDXGISurface> Graphics::preview(const pdf::Bitmap& raster,
                                        pdf::PrintPaper paper,
                                        pdf::PrintLayout layout, int width,
                                        int height) {
	D3D11_TEXTURE2D_DESC description{};
	description.Width = width;
	description.Height = height;
	description.MipLevels = description.ArraySize = 1;
	description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	description.SampleDesc.Count = 1;
	description.BindFlags =
	    D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	com_ptr<ID3D11Texture2D> texture;
	check_hresult(m_d3d->CreateTexture2D(&description, nullptr, texture.put()));
	auto surface = texture.as<IDXGISurface>();
	com_ptr<ID2D1Bitmap1> target;
	const auto properties = D2D1::BitmapProperties1(
	    D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
	    D2D1::PixelFormat(description.Format, D2D1_ALPHA_MODE_IGNORE),
	    96.f * width / paper.width, 96.f * height / paper.height);
	check_hresult(m_context->CreateBitmapFromDxgiSurface(
	    surface.get(), properties, target.put()));
	m_context->SetDpi(properties.dpiX, properties.dpiY);
	m_context->SetTarget(target.get());
	m_context->BeginDraw();
	m_context->Clear(D2D1::ColorF(D2D1::ColorF::White));
	draw(raster, D2D1::RectF(layout.x, layout.y, layout.x + layout.width,
	                         layout.y + layout.height));
	check_hresult(m_context->EndDraw());
	m_context->SetTarget(nullptr);
	m_immediate->Flush();
	return surface;
}
com_ptr<ID2D1PrintControl>
Graphics::control(IPrintDocumentPackageTarget* target, float dpi) {
	com_ptr<IWICImagingFactory> wic;
	check_hresult(CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
	                               CLSCTX_INPROC_SERVER,
	                               IID_PPV_ARGS(wic.put())));
	const D2D1_PRINT_CONTROL_PROPERTIES properties{
	    D2D1_PRINT_FONT_SUBSET_MODE_DEFAULT, dpi, D2D1_COLOR_SPACE_SRGB};
	com_ptr<ID2D1PrintControl> result;
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/direct2d/printing-and-command-lists
	check_hresult(m_device->CreatePrintControl(wic.get(), target, properties,
	                                           result.put()));
	return result;
}
void Graphics::beginPage() {
	check_hresult(m_context->CreateCommandList(m_commands.put()));
	m_context->SetDpi(96, 96);
	m_context->SetTarget(m_commands.get());
	m_context->BeginDraw();
	m_context->Clear(D2D1::ColorF(D2D1::ColorF::White));
}
void Graphics::tile(const pdf::Bitmap& raster, pdf::PrintLayout layout,
                    pdf::PrintRect tile) {
	const auto x = layout.width / layout.rasterWidth;
	const auto y = layout.height / layout.rasterHeight;
	draw(raster, D2D1::RectF(layout.x + tile.x * x, layout.y + tile.y * y,
	                         layout.x + (tile.x + tile.width) * x,
	                         layout.y + (tile.y + tile.height) * y));
}
com_ptr<ID2D1CommandList> Graphics::endPage() {
	check_hresult(m_context->EndDraw());
	m_context->SetTarget(nullptr);
	check_hresult(m_commands->Close());
	return std::exchange(m_commands, {});
}
} // namespace printing
