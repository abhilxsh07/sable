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
#include "music_capture.h"
#include <stdexcept>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
namespace {
struct Capture {
	HWND window;
	HDC source = nullptr, memory = nullptr;
	HBITMAP bitmap = nullptr;
	HGDIOBJ previous = nullptr;
	~Capture() {
		if (previous) {
			SelectObject(memory, previous);
		}
		if (bitmap) {
			DeleteObject(bitmap);
		}
		if (memory) {
			DeleteDC(memory);
		}
		if (source) {
			ReleaseDC(window, source);
		}
	}
};
} // namespace
void captureMusicWindow(HWND window, const std::filesystem::path& path,
                        bool screen) {
	RECT bounds{};
	if (!GetWindowRect(window, &bounds)) {
		throw std::runtime_error("music capture bounds");
	}
	Capture capture{screen ? nullptr : window};
	capture.source = screen ? GetDC(nullptr) : GetWindowDC(window);
	capture.memory = CreateCompatibleDC(capture.source);
	capture.bitmap = CreateCompatibleBitmap(
	    capture.source, bounds.right - bounds.left, bounds.bottom - bounds.top);
	if (!capture.source || !capture.memory || !capture.bitmap) {
		throw std::runtime_error("music capture bitmap");
	}
	capture.previous = SelectObject(capture.memory, capture.bitmap);
	const bool captured =
	    screen ? BitBlt(capture.memory, 0, 0, bounds.right - bounds.left,
	                    bounds.bottom - bounds.top, capture.source, bounds.left,
	                    bounds.top, SRCCOPY | CAPTUREBLT) != FALSE
	           : PrintWindow(window, capture.memory, 2) != FALSE;
	if (!captured) {
		throw std::runtime_error("music capture window");
	}
	winrt::com_ptr<IWICImagingFactory> factory;
	winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
	                                      CLSCTX_INPROC_SERVER,
	                                      IID_PPV_ARGS(factory.put())));
	winrt::com_ptr<IWICBitmap> image;
	winrt::check_hresult(factory->CreateBitmapFromHBITMAP(
	    capture.bitmap, nullptr, WICBitmapIgnoreAlpha, image.put()));
	winrt::com_ptr<IWICStream> stream;
	winrt::check_hresult(factory->CreateStream(stream.put()));
	winrt::check_hresult(
	    stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
	winrt::com_ptr<IWICBitmapEncoder> encoder;
	winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,
	                                            nullptr, encoder.put()));
	winrt::check_hresult(
	    encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
	winrt::com_ptr<IWICBitmapFrameEncode> frame;
	winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
	winrt::check_hresult(frame->Initialize(nullptr));
	UINT width{}, height{};
	winrt::check_hresult(image->GetSize(&width, &height));
	winrt::check_hresult(frame->SetSize(width, height));
	auto format = GUID_WICPixelFormat32bppBGRA;
	winrt::check_hresult(frame->SetPixelFormat(&format));
	winrt::check_hresult(frame->WriteSource(image.get(), nullptr));
	winrt::check_hresult(frame->Commit());
	winrt::check_hresult(encoder->Commit());
}
