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
#include "StampImage.h"
#include "pdf/annotations.h"
#include <filesystem>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

namespace winrt::Sable::implementation {
Windows::Foundation::IAsyncOperation<hstring>
pickStampFile(Microsoft::UI::WindowId owner) {
	Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(owner);
	for (const auto extension : {L".png", L".jpg", L".jpeg"}) {
		picker.FileTypeFilter().Append(extension);
	}
	const auto selected = co_await picker.PickSingleFileAsync();
	co_return selected ? selected.Path() : hstring{};
}

Windows::Foundation::IAsyncAction
decodeStampImage(hstring file, std::shared_ptr<pdf::StampImage> result) {
	using namespace Windows::Graphics::Imaging;
	using Windows::Storage::FileAccessMode;
	using Windows::Storage::Streams::FileRandomAccessStream;

	if (!result) {
		throw hresult_invalid_argument();
	}
	const auto extension = std::filesystem::path(file.c_str()).extension();
	const auto hasExtension = [&](const wchar_t* value) {
		return CompareStringOrdinal(extension.c_str(), -1, value, -1, TRUE) ==
		       CSTR_EQUAL;
	};
	guid codec;
	if (hasExtension(L".png")) {
		codec = BitmapDecoder::PngDecoderId();
	} else if (hasExtension(L".jpg") || hasExtension(L".jpeg")) {
		codec = BitmapDecoder::JpegDecoderId();
	} else {
		throw hresult_invalid_argument();
	}

	const auto stream =
	    co_await FileRandomAccessStream::OpenAsync(file, FileAccessMode::Read);
	constexpr std::uint64_t limit = 32 * 1024 * 1024;
	const auto bytes = stream.Size();
	if (!bytes || bytes > limit) {
		throw hresult_invalid_argument();
	}
	// i use the decoder for the extension so a renamed file can't slip in.
	const auto decoder = co_await BitmapDecoder::CreateAsync(codec, stream);
	const auto width = decoder.OrientedPixelWidth();
	const auto height = decoder.OrientedPixelHeight();
	if (!width || !height || width > pdf::StampImage::maxSide ||
	    height > pdf::StampImage::maxSide) {
		throw hresult_invalid_argument();
	}
	const auto data = co_await decoder.GetPixelDataAsync(
	    BitmapPixelFormat::Bgra8, BitmapAlphaMode::Straight, BitmapTransform{},
	    ExifOrientationMode::RespectExifOrientation,
	    ColorManagementMode::ColorManageToSRgb);
	const auto pixels = data.DetachPixelData();
	if (pixels.size() != 4 * width * height) {
		throw hresult_invalid_argument();
	}
	*result = pdf::StampImage{width, height, {pixels.begin(), pixels.end()}};
}
} // namespace winrt::Sable::implementation
