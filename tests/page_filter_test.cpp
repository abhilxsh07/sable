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
#include <gtest/gtest.h>

#include <d3d11.h>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "filter.h"
#include "pdf/page_filter.h"

namespace {
class PageFilterTest : public testing::Test {
  protected:
	void SetUp() override {
		// WARP runs the real effects without needing a GPU on the test runner
		// see:
		// https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice
		winrt::com_ptr<ID3D11Device> d3d;
		winrt::check_hresult(
		    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		                      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
		                      D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr));
		winrt::com_ptr<ID2D1Factory1> factory;
		winrt::check_hresult(D2D1CreateFactory(
		    D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()));
		winrt::com_ptr<ID2D1Device> device;
		winrt::check_hresult(
		    factory->CreateDevice(d3d.as<IDXGIDevice>().get(), device.put()));
		winrt::check_hresult(device->CreateDeviceContext(
		    D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_context.put()));
		m_context->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
		m_filter = std::make_unique<pdf::PageFilter>(*m_context);
	}

	void upload(const std::vector<helpers::Rgb8>& colors) {
		std::vector<std::uint8_t> bgra;
		for (const auto color : colors) {
			bgra.insert(bgra.end(), {color.b, color.g, color.r, 255});
		}
		m_width = static_cast<UINT32>(colors.size());
		const auto properties = D2D1::BitmapProperties1(
		    D2D1_BITMAP_OPTIONS_NONE,
		    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
		                      D2D1_ALPHA_MODE_PREMULTIPLIED));
		winrt::com_ptr<ID2D1Bitmap1> bitmap;
		winrt::check_hresult(m_context->CreateBitmap(D2D1::SizeU(m_width, 1),
		                                             bgra.data(), m_width * 4,
		                                             properties, bitmap.put()));
		m_filter->setInput(bitmap.get());
	}

	std::vector<std::uint8_t> draw() {
		const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
		                                      D2D1_ALPHA_MODE_PREMULTIPLIED);
		const auto targetProperties =
		    D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format);
		winrt::com_ptr<ID2D1Bitmap1> target;
		winrt::check_hresult(
		    m_context->CreateBitmap(D2D1::SizeU(m_width, 1), nullptr, 0,
		                            targetProperties, target.put()));
		m_context->SetTarget(target.get());
		m_context->BeginDraw();
		m_context->Clear(D2D1::ColorF(0, 0, 0, 0));
		m_context->DrawImage(m_filter->output());
		const HRESULT result = m_context->EndDraw();
		m_context->SetTarget(nullptr);
		winrt::check_hresult(result);

		const auto readProperties = D2D1::BitmapProperties1(
		    D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		    format);
		winrt::com_ptr<ID2D1Bitmap1> readback;
		winrt::check_hresult(m_context->CreateBitmap(D2D1::SizeU(m_width, 1),
		                                             nullptr, 0, readProperties,
		                                             readback.put()));
		winrt::check_hresult(
		    readback->CopyFromBitmap(nullptr, target.get(), nullptr));
		std::vector<std::uint8_t> pixels(m_width * 4);
		D2D1_MAPPED_RECT mapped{};
		winrt::check_hresult(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
		std::memcpy(pixels.data(), mapped.bits, pixels.size());
		winrt::check_hresult(readback->Unmap());
		return pixels;
	}

	std::unique_ptr<pdf::PageFilter> m_filter;
	winrt::com_ptr<ID2D1DeviceContext> m_context;
	UINT32 m_width = 0;
};
} // namespace

TEST_F(PageFilterTest, StartsAsIdentityAndKeepsOpaqueBgra) {
	upload({{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {19, 117, 203}});
	const std::vector<std::uint8_t> expected = {
	    0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 203, 117, 19, 255};
	EXPECT_EQ(draw(), expected);
}

TEST_F(PageFilterTest, ClampsSepiaBeforeHueRotation) {
	upload({{255, 255, 255}});
	helpers::SliderValues values{0, 0, 0, 100, 90, 0};
	m_filter->setValues(values);
	const auto pixels = draw();
	EXPECT_NEAR(pixels[2], 239, 1);
	EXPECT_NEAR(pixels[1], 255, 1);
	EXPECT_NEAR(pixels[0], 253, 1);
	EXPECT_EQ(pixels[3], 255);
}

TEST_F(PageFilterTest, CanReplaceThePageWithoutRebuildingTheChain) {
	m_filter->setValues({0, 0, 100, 0, 0, 0});
	upload({{255, 255, 255}});
	EXPECT_EQ(draw(), (std::vector<std::uint8_t>{0, 0, 0, 255}));
	upload({{0, 0, 0}});
	EXPECT_EQ(draw(), (std::vector<std::uint8_t>{255, 255, 255, 255}));
}

TEST_F(PageFilterTest, PresetChangesMatchChromiumWithoutUploadingAgain) {
	std::ifstream file("tests/golden/filter_expected.json");
	ASSERT_TRUE(file.is_open());
	const auto golden = nlohmann::json::parse(file);
	ASSERT_EQ(golden.size(), 8);
	std::vector<helpers::Rgb8> colors;
	for (const auto& c : golden.front().at("cases")) {
		colors.push_back({c.at("in")[0], c.at("in")[1], c.at("in")[2]});
	}
	ASSERT_EQ(colors.size(), 24);
	upload(colors);
	// revisit the presets to catch stale effect output after a slider change
	for (int pass = 0; pass < 2; pass++) {
		for (const auto& entry : golden) {
			SCOPED_TRACE(entry.at("name").get<std::string>());
			const auto& v = entry.at("values");
			const helpers::SliderValues values = {
			    v.at("brightness"), v.at("grayness"), v.at("inversion"),
			    v.at("sepia"),      v.at("hue"),      v.at("extraBrightness")};
			m_filter->setValues(values);
			const auto pixels = draw();
			ASSERT_EQ(entry.at("cases").size(), colors.size());
			for (size_t i = 0; i < colors.size(); i++) {
				SCOPED_TRACE(i);
				const auto& c = entry.at("cases")[i];
				EXPECT_EQ(c.at("in"), nlohmann::json({colors[i].r, colors[i].g,
				                                      colors[i].b}));
				const auto cpu = helpers::applyFilter(values, colors[i]);
				EXPECT_NEAR(pixels[i * 4 + 2], c.at("out")[0].get<int>(), 2);
				EXPECT_NEAR(pixels[i * 4 + 1], c.at("out")[1].get<int>(), 2);
				EXPECT_NEAR(pixels[i * 4], c.at("out")[2].get<int>(), 2);
				EXPECT_NEAR(pixels[i * 4 + 2], cpu.r, 2);
				EXPECT_NEAR(pixels[i * 4 + 1], cpu.g, 2);
				EXPECT_NEAR(pixels[i * 4], cpu.b, 2);
				EXPECT_EQ(pixels[i * 4 + 3], 255);
			}
		}
	}
}
