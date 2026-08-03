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
#include "export.h"

TEST(Export, PageListsAcceptRangesAndSingles) {
	EXPECT_EQ(helpers::parsePageList(L"1-3, 5", 6),
	          (std::vector<int>{0, 1, 2, 4}));
	EXPECT_EQ(helpers::parsePageList(L" 4 - 2 ,2", 6),
	          (std::vector<int>{1, 2, 3}));
	EXPECT_EQ(helpers::parsePageList(L"6", 6), (std::vector<int>{5}));
	EXPECT_EQ(helpers::parsePageList(L"2–4", 6), (std::vector<int>{1, 2, 3}));
	EXPECT_FALSE(helpers::parsePageList(L"", 6));
	EXPECT_FALSE(helpers::parsePageList(L"0", 6));
	EXPECT_FALSE(helpers::parsePageList(L"7", 6));
	EXPECT_FALSE(helpers::parsePageList(L"1,,2", 6));
	EXPECT_FALSE(helpers::parsePageList(L"1-", 6));
	EXPECT_FALSE(helpers::parsePageList(L"a", 6));
	EXPECT_FALSE(helpers::parsePageList(L"1", 0));
}

TEST(Export, FileNamesPadToTheLastPage) {
	EXPECT_EQ(helpers::exportName(L"Report", 7, 12, L".png"),
	          L"Report - page 007.png");
	EXPECT_EQ(helpers::exportName(L"Book", 42, 1200, L".jpg"),
	          L"Book - page 0042.jpg");
}

TEST(Export, PixelSizesFollowTheResolutionWithinLimits) {
	EXPECT_EQ(helpers::exportPixels(612, 792, 150),
	          (std::pair<int, int>{1275, 1650}));
	EXPECT_EQ(helpers::exportPixels(612, 792, 72),
	          (std::pair<int, int>{612, 792}));
	const auto huge = helpers::exportPixels(14400, 14400, 300);
	EXPECT_LE((std::max)(huge.first, huge.second), 12000);
	EXPECT_LE(static_cast<double>(huge.first) * huge.second, 80e6 + 12000);
	EXPECT_EQ(helpers::exportPixels(0, 792, 150), (std::pair<int, int>{0, 0}));
}
