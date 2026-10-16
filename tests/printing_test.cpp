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
#include <windows.h>

#include <array>
#include <limits>

#include "pdf/engine.h"
#include "pdf/printing.h"
#include "pdf_test_files.h"

TEST(Printing, PageSelectionUsesOneBasedRangesAndDeduplicatesOverlaps) {
	EXPECT_EQ(pdf::printPages(3, {}), (std::vector<int>{0, 1, 2}));
	const std::array ranges{pdf::PrintRange{4, 5}, pdf::PrintRange{2, 4}};
	EXPECT_EQ(pdf::printPages(6, ranges), (std::vector<int>{1, 2, 3, 4}));
	const std::array current{pdf::PrintRange{3, 3}};
	EXPECT_EQ(pdf::printPages(6, current), (std::vector<int>{2}));
	for (const auto& range : {pdf::PrintRange{0, 2}, pdf::PrintRange{3, 2},
	                          pdf::PrintRange{1, 7}}) {
		EXPECT_FALSE(pdf::printPages(6, std::span(&range, 1)));
	}
	EXPECT_FALSE(pdf::printPages(0, {}));
}

TEST(Printing, FitsAndCentersInPrintableAreaAtUnequalPrinterDpi) {
	const auto portrait = pdf::fitPrintPage({100, 200}, 600, 600, 300, 300);
	ASSERT_TRUE(portrait);
	EXPECT_EQ(portrait->width, 300);
	EXPECT_EQ(portrait->height, 600);
	EXPECT_EQ(portrait->x, 150);
	EXPECT_EQ(portrait->y, 0);
	const auto landscape = pdf::fitPrintPage({200, 100}, 600, 600, 300, 300);
	ASSERT_TRUE(landscape);
	EXPECT_EQ(landscape->width, 600);
	EXPECT_EQ(landscape->height, 300);
	EXPECT_EQ(landscape->y, 150);
	const auto unequal = pdf::fitPrintPage({100, 200}, 600, 600, 600, 300);
	ASSERT_TRUE(unequal);
	EXPECT_EQ(unequal->width, 600);
	EXPECT_EQ(unequal->height, 600);
	EXPECT_FALSE(pdf::fitPrintPage({0, 200}, 600, 600, 300, 300));
	EXPECT_FALSE(pdf::fitPrintPage(
	    {100, std::numeric_limits<float>::infinity()}, 600, 600, 300, 300));
	EXPECT_FALSE(pdf::fitPrintPage({100, 200}, 0, 600, 300, 300));
	EXPECT_FALSE(pdf::fitPrintPage({100, 200}, 600, 600, 0, 300));
}

namespace {
struct Canvas {
	HDC dc = CreateCompatibleDC(nullptr);
	HBITMAP bitmap = nullptr;
	HGDIOBJ previous = nullptr;
	Canvas() {
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = 200;
		info.bmiHeader.biHeight = -300;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		void* pixels{};
		bitmap =
		    CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
		if (bitmap) {
			previous = SelectObject(dc, bitmap);
		}
		clear();
	}
	void clear() {
		PatBlt(dc, 0, 0, 200, 300, WHITENESS);
	}
	~Canvas() {
		if (previous) {
			SelectObject(dc, previous);
		}
		if (bitmap) {
			DeleteObject(bitmap);
		}
		if (dc) {
			DeleteDC(dc);
		}
	}
};
} // namespace

TEST(Printing, PrinterRenderingKeepsOriginalColorsAndPrintVisibility) {
	pdf::Engine engine;
	const auto colors =
	    engine
	        .run([] {
		        auto opened = pdf::openFile(fixturePath("print.pdf"));
		        Canvas canvas;
		        std::vector<COLORREF> result;
		        if (!opened.document || !canvas.bitmap ||
		            !opened.document->printPage(0, canvas.dc, 0, 0, 100, 100)) {
			        return result;
		        }
		        for (auto point :
		             {POINT{20, 80}, POINT{60, 80}, POINT{20, 30}}) {
			        result.push_back(GetPixel(canvas.dc, point.x, point.y));
		        }
		        return result;
	        })
	        .get();
	ASSERT_EQ(colors.size(), 3u);
	EXPECT_EQ(colors[0], RGB(255, 0, 0));
	EXPECT_EQ(colors[1], RGB(0, 255, 0));
	EXPECT_EQ(colors[2], RGB(255, 255, 255));
}

TEST(Printing, PrinterRenderingHonorsRotationAndRejectsInvalidPages) {
	pdf::Engine engine;
	engine
	    .run([] {
		    auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    Canvas canvas;
		    ASSERT_TRUE(canvas.bitmap);
		    ASSERT_TRUE(
		        opened.document->printPage(2, canvas.dc, 0, 0, 200, 300));
		    EXPECT_EQ(GetPixel(canvas.dc, 100, 40), RGB(0, 0, 0));
		    EXPECT_EQ(GetPixel(canvas.dc, 100, 250), RGB(255, 255, 255));
		    EXPECT_FALSE(
		        opened.document->printPage(-1, canvas.dc, 0, 0, 200, 300));
		    EXPECT_FALSE(
		        opened.document->printPage(3, canvas.dc, 0, 0, 200, 300));
		    EXPECT_FALSE(
		        opened.document->printPage(0, nullptr, 0, 0, 200, 300));
		    EXPECT_FALSE(
		        opened.document->printPage(0, canvas.dc, 0, 0, 0, 300));
	    })
	    .get();
}
