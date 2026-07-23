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
#include "signature.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <type_traits>

namespace helpers {
namespace {
constexpr double TYPED_HEIGHT = 36, TYPED_WIDTH = 216;
constexpr double DRAWN_WIDTH = 150, DRAWN_HEIGHT = 50;
// pixels per point for typed signatures, and the text size in points.
constexpr float PIXELS = 4, TEXT_SIZE = 28;
constexpr int MARGIN = 2;
// matches the ink limits the document enforces.
constexpr std::size_t STROKES = 256, POINTS = 4096, TOTAL = 16384;
// the line is 2 pt wide, so its centre keeps a little more than 1 pt away.
constexpr double INSET = 1.5;

winrt::com_ptr<IDWriteFactory> writeFactory() {
	winrt::com_ptr<IDWriteFactory> factory;
	winrt::check_hresult(DWriteCreateFactory(
	    DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
	    reinterpret_cast<IUnknown**>(factory.put())));
	return factory;
}

SignatureFont fontFrom(IDWriteFactory& factory) {
	winrt::com_ptr<IDWriteFontCollection> fonts;
	winrt::check_hresult(factory.GetSystemFontCollection(fonts.put()));
	for (const auto* family : {L"Segoe Script", L"Ink Free"}) {
		UINT32 index = 0;
		BOOL exists = FALSE;
		if (SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) &&
		    exists) {
			return {family, false};
		}
	}
	return {L"Segoe UI", true};
}

// keeps a box of the given size inside [0, 1] around the centre.
double centred(double centre, double size) {
	return size >= 1 ? .5 : std::clamp(centre, size / 2, 1 - size / 2);
}
} // namespace

bool validSignatureName(std::u16string_view name) {
	if (name.empty() || name.size() > signatureNameLimit) {
		return false;
	}
	bool ink = false;
	for (std::size_t i = 0; i < name.size(); ++i) {
		const auto c = name[i];
		if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xa0) || c == 0x2028 ||
		    c == 0x2029) {
			return false;
		}
		// surrogates only in whole pairs.
		if (c >= 0xd800 && c <= 0xdbff) {
			if (i + 1 >= name.size() || name[i + 1] < 0xdc00 ||
			    name[i + 1] > 0xdfff) {
				return false;
			}
			++i;
		} else if (c >= 0xdc00 && c <= 0xdfff) {
			return false;
		}
		ink = ink || (c != u' ' && c != 0xa0);
	}
	return ink;
}

SignatureFont signatureFont() {
	try {
		return fontFrom(*writeFactory());
	} catch (...) {
		return {L"Segoe UI", true};
	}
}

std::optional<pdf::StampImage> typedSignature(std::u16string_view name) {
	if (!validSignatureName(name)) {
		return {};
	}
	try {
		const auto write = writeFactory();
		const auto font = fontFrom(*write);
		const std::wstring text(name.begin(), name.end());
		winrt::com_ptr<IDWriteTextLayout> layout;
		DWRITE_TEXT_METRICS metrics{};
		DWRITE_OVERHANG_METRICS overhang{};
		float size = TEXT_SIZE * PIXELS;
		const auto measure = [&] {
			winrt::com_ptr<IDWriteTextFormat> format;
			winrt::check_hresult(write->CreateTextFormat(
			    font.family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
			    font.italic ? DWRITE_FONT_STYLE_ITALIC
			                : DWRITE_FONT_STYLE_NORMAL,
			    DWRITE_FONT_STRETCH_NORMAL, size, L"", format.put()));
			winrt::check_hresult(
			    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
			layout = nullptr;
			winrt::check_hresult(write->CreateTextLayout(
			    text.c_str(), static_cast<UINT32>(text.size()), format.get(),
			    16384.f, 4096.f, layout.put()));
			winrt::check_hresult(layout->GetMetrics(&metrics));
			winrt::check_hresult(layout->GetOverhangMetrics(&overhang));
		};
		const auto extent = [&](float length, float before, float after) {
			return length + (std::max)(before, 0.f) + (std::max)(after, 0.f) +
			       2 * MARGIN;
		};
		measure();
		// long names shrink until the whole line fits one stamp image.
		const float limit = pdf::StampImage::maxSide - 4.f;
		const float wide = extent(metrics.widthIncludingTrailingWhitespace,
		                          overhang.left, overhang.right);
		if (wide > limit) {
			size *= limit / wide;
			measure();
		}
		const auto width = static_cast<int>(
		    std::ceil(extent(metrics.widthIncludingTrailingWhitespace,
		                     overhang.left, overhang.right)));
		const auto height = static_cast<int>(
		    std::ceil(extent(metrics.height, overhang.top, overhang.bottom)));
		if (width <= 0 || height <= 0 ||
		    width > static_cast<int>(pdf::StampImage::maxSide) ||
		    height > static_cast<int>(pdf::StampImage::maxSide)) {
			return {};
		}

		// a top-down 32-bit DIB that Direct2D draws into through a DC.
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		const std::unique_ptr<std::remove_pointer_t<HDC>, decltype(&DeleteDC)>
		    dc(CreateCompatibleDC(nullptr), &DeleteDC);
		void* bits = nullptr;
		const std::unique_ptr<std::remove_pointer_t<HBITMAP>,
		                      decltype(&DeleteObject)>
		    bitmap(CreateDIBSection(dc.get(), &info, DIB_RGB_COLORS, &bits,
		                            nullptr, 0),
		           &DeleteObject);
		if (!dc || !bitmap || !bits || !SelectObject(dc.get(), bitmap.get())) {
			return {};
		}
		winrt::com_ptr<ID2D1Factory> factory;
		winrt::check_hresult(D2D1CreateFactory(
		    D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()));
		const auto properties = D2D1::RenderTargetProperties(
		    D2D1_RENDER_TARGET_TYPE_SOFTWARE,
		    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
		                      D2D1_ALPHA_MODE_PREMULTIPLIED),
		    96.f, 96.f);
		winrt::com_ptr<ID2D1DCRenderTarget> target;
		winrt::check_hresult(
		    factory->CreateDCRenderTarget(&properties, target.put()));
		const RECT area{0, 0, width, height};
		winrt::check_hresult(target->BindDC(dc.get(), &area));
		winrt::com_ptr<ID2D1SolidColorBrush> black;
		winrt::check_hresult(target->CreateSolidColorBrush(
		    D2D1::ColorF(0, 0, 0, 1), black.put()));
		target->BeginDraw();
		target->Clear(D2D1::ColorF(0, 0, 0, 0));
		// cleartype needs an opaque background, so the edges stay gray.
		target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
		target->DrawTextLayout(
		    D2D1::Point2F(MARGIN + (std::max)(overhang.left, 0.f),
		                  MARGIN + (std::max)(overhang.top, 0.f)),
		    layout.get(), black.get());
		winrt::check_hresult(target->EndDraw());
		GdiFlush();

		// premultiplied black stays black; only the alpha carries the ink.
		const auto* pixels = static_cast<const std::uint8_t*>(bits);
		int left = width, top = height, right = -1, bottom = -1;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				if (pixels[(static_cast<std::size_t>(y) * width + x) * 4 + 3]) {
					left = (std::min)(left, x);
					right = (std::max)(right, x);
					top = (std::min)(top, y);
					bottom = (std::max)(bottom, y);
				}
			}
		}
		if (right < 0) {
			return {};
		}
		left = (std::max)(left - MARGIN, 0);
		top = (std::max)(top - MARGIN, 0);
		right = (std::min)(right + MARGIN, width - 1);
		bottom = (std::min)(bottom + MARGIN, height - 1);
		pdf::StampImage image;
		image.width = static_cast<unsigned>(right - left + 1);
		image.height = static_cast<unsigned>(bottom - top + 1);
		image.bgra.reserve(4 * static_cast<std::size_t>(image.width) *
		                   image.height);
		for (int y = top; y <= bottom; ++y) {
			for (int x = left; x <= right; ++x) {
				const auto alpha =
				    pixels[(static_cast<std::size_t>(y) * width + x) * 4 + 3];
				image.bgra.insert(image.bgra.end(), {0, 0, 0, alpha});
			}
		}
		if (!image.valid()) {
			return {};
		}
		return image;
	} catch (...) {
		return {};
	}
}

std::optional<SignatureBox> typedSignatureBox(double aspect, PageSize displayed,
                                              pdf::AnnotationPoint center) {
	if (!std::isfinite(aspect) || aspect <= 0 ||
	    !std::isfinite(displayed.width) || !std::isfinite(displayed.height) ||
	    displayed.width <= 0 || displayed.height <= 0 ||
	    !std::isfinite(center.x) || !std::isfinite(center.y)) {
		return {};
	}
	double height = TYPED_HEIGHT, width = height * aspect;
	if (width > TYPED_WIDTH) {
		width = TYPED_WIDTH;
		height = width / aspect;
	}
	// a small page still gets the whole signature, at nine tenths of it.
	const double fit = (std::min)(
	    {1., displayed.width * .9 / width, displayed.height * .9 / height});
	const double across = width * fit / displayed.width;
	const double down = height * fit / displayed.height;
	const double x = centred(center.x, across);
	const double y = centred(center.y, down);
	return SignatureBox{{x - across / 2, y - down / 2},
	                    {x + across / 2, y + down / 2}};
}

std::vector<pdf::DisplayedStroke>
drawnSignatureStrokes(const std::vector<PadStroke>& pad, PageSize displayed,
                      pdf::AnnotationPoint center) {
	if (pad.empty() || pad.size() > STROKES ||
	    !std::isfinite(displayed.width) || !std::isfinite(displayed.height) ||
	    displayed.width <= 4 * INSET || displayed.height <= 4 * INSET ||
	    !std::isfinite(center.x) || !std::isfinite(center.y)) {
		return {};
	}
	double left = std::numeric_limits<double>::infinity(), top = left;
	double right = -left, bottom = -left;
	std::size_t total = 0;
	for (const auto& stroke : pad) {
		total += stroke.size();
		if (stroke.empty() || stroke.size() > POINTS || total > TOTAL) {
			return {};
		}
		for (const auto& point : stroke) {
			if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
				return {};
			}
			left = (std::min)(left, point.x);
			right = (std::max)(right, point.x);
			top = (std::min)(top, point.y);
			bottom = (std::max)(bottom, point.y);
		}
	}
	const double across = right - left, down = bottom - top;
	// a dot or a straight line scales by the side it has.
	double scale = 1;
	if (across > 0 && down > 0) {
		scale = (std::min)(DRAWN_WIDTH / across, DRAWN_HEIGHT / down);
	} else if (across > 0) {
		scale = DRAWN_WIDTH / across;
	} else if (down > 0) {
		scale = DRAWN_HEIGHT / down;
	}
	const double roomX = displayed.width - 2 * INSET;
	const double roomY = displayed.height - 2 * INSET;
	scale *= (std::min)({1., across > 0 ? roomX * .9 / (across * scale) : 1.,
	                     down > 0 ? roomY * .9 / (down * scale) : 1.});
	const double halfX = across * scale / 2, halfY = down * scale / 2;
	const double x = std::clamp(center.x * displayed.width, INSET + halfX,
	                            displayed.width - INSET - halfX);
	const double y = std::clamp(center.y * displayed.height, INSET + halfY,
	                            displayed.height - INSET - halfY);
	const double midX = (left + right) / 2, midY = (top + bottom) / 2;
	std::vector<pdf::DisplayedStroke> strokes;
	strokes.reserve(pad.size());
	for (const auto& stroke : pad) {
		auto& out = strokes.emplace_back();
		out.reserve(stroke.size());
		for (const auto& point : stroke) {
			out.push_back(
			    {std::clamp((x + (point.x - midX) * scale) / displayed.width,
			                0., 1.),
			     std::clamp((y + (point.y - midY) * scale) / displayed.height,
			                0., 1.)});
		}
	}
	return strokes;
}
} // namespace helpers
