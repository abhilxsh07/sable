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

#include <algorithm>
#include <cmath>
#include <optional>

namespace pdf {
enum class DrawLoss { None, Contents, Device };

// E_SURFACE_CONTENTS_LOST, which only the XAML interop header defines, and
// as an unsigned number
inline constexpr HRESULT CONTENTS_LOST = static_cast<HRESULT>(0x802B0020);

// What a failed BeginDraw/EndDraw means for a SurfaceImageSource. After a
// contents loss the device still works; after a device loss the Direct3D and
// Direct2D devices have to be created again.
// see:
// https://learn.microsoft.com/en-us/windows/uwp/gaming/directx-and-xaml-interop
inline DrawLoss drawLoss(HRESULT result) {
	switch (result) {
	case DXGI_ERROR_DEVICE_REMOVED:
	case DXGI_ERROR_DEVICE_RESET:
	case D2DERR_RECREATE_TARGET:
		return DrawLoss::Device;
	case CONTENTS_LOST:
		return DrawLoss::Contents;
	default:
		return DrawLoss::None;
	}
}

// DirectComposition's cap on each side of a virtual surface. XAML itself
// checks for under 17,000,000, so this is the one that bites first.
// see:
// https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice2-createvirtualsurface
inline constexpr long long MAX_SURFACE_SIDE = 16'777'216;

inline bool fitsOneSurface(long long width, long long height) {
	return width > 0 && height > 0 && width <= MAX_SURFACE_SIDE &&
	       height <= MAX_SURFACE_SIDE;
}

// check in floating point before converting; a bad scale or an enormous
// document must never overflow the native surface dimensions
inline std::optional<SIZE> scaledSurfaceSize(double width, double height,
                                             double scale) {
	if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(width) ||
	    !std::isfinite(height) || width <= 0 || height <= 0) {
		return std::nullopt;
	}
	width = std::ceil(width * scale);
	height = std::ceil(height * scale);
	if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
	    height <= 0 || width > MAX_SURFACE_SIDE || height > MAX_SURFACE_SIDE) {
		return std::nullopt;
	}
	return SIZE{static_cast<LONG>(width), static_cast<LONG>(height)};
}

// the part of rect that lies inside a width by height surface, if any
inline std::optional<RECT> clipToSurface(const RECT& rect, int width,
                                         int height) {
	const auto clip = [](LONG value, LONG limit) {
		return (std::min)((std::max)(value, 0L), limit);
	};
	const RECT clipped{clip(rect.left, width), clip(rect.top, height),
	                   clip(rect.right, width), clip(rect.bottom, height)};
	if (clipped.left >= clipped.right || clipped.top >= clipped.bottom) {
		return std::nullopt;
	}
	return clipped;
}
} // namespace pdf
