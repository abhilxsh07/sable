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
#include "helpers/text_range.h"
#include <gtest/gtest.h>

namespace {
using helpers::TextUnit;
constexpr std::wstring_view page = L"One two  three\nfour five\n\nNext para";

std::wstring_view part(std::pair<std::size_t, std::size_t> range) {
	return page.substr(range.first, range.second - range.first);
}

TEST(TextRange, PdfiumLineEndsAndPageBreaksAreCleanedUp) {
	EXPECT_EQ(helpers::screenReaderText(L"a\r\nb\rc\fd"), L"a\nb\ncd");
	EXPECT_EQ(helpers::screenReaderText(L""), L"");
}

TEST(TextRange, UnitsAroundAPosition) {
	EXPECT_EQ(part(helpers::unitAround(page, 5, TextUnit::Word)), L"two  ");
	EXPECT_EQ(part(helpers::unitAround(page, 0, TextUnit::Word)), L"One ");
	EXPECT_EQ(part(helpers::unitAround(page, 9, TextUnit::Line)),
	          L"One two  three\n");
	EXPECT_EQ(part(helpers::unitAround(page, 17, TextUnit::Line)),
	          L"four five\n");
	EXPECT_EQ(part(helpers::unitAround(page, 30, TextUnit::Paragraph)),
	          L"Next para");
	EXPECT_EQ(part(helpers::unitAround(page, 3, TextUnit::Paragraph)),
	          L"One two  three\nfour five\n\n");
	EXPECT_EQ(part(helpers::unitAround(page, 4, TextUnit::Page)), page);
	EXPECT_EQ(part(helpers::unitAround(page, 2, TextUnit::Character)), L"e");
	// the end belongs to the last unit
	EXPECT_EQ(part(helpers::unitAround(page, page.size(), TextUnit::Word)),
	          L"para");
	EXPECT_EQ(helpers::unitAround(L"", 3, TextUnit::Line),
	          (std::pair<std::size_t, std::size_t>{0, 0}));
}

TEST(TextRange, SurrogatePairsStayTogether) {
	const std::wstring text = L"a\U0001F600b";
	const auto range = helpers::unitAround(text, 1, TextUnit::Character);
	EXPECT_EQ(range.second - range.first, 2u);
	int moved = 0;
	EXPECT_EQ(helpers::moveByUnit(text, 1, TextUnit::Character, 1, moved), 3u);
}

TEST(TextRange, MovingCountsTheUnitsCrossed) {
	int moved = 0;
	EXPECT_EQ(helpers::moveByUnit(page, 0, TextUnit::Word, 2, moved), 9u);
	EXPECT_EQ(moved, 2);
	EXPECT_EQ(helpers::moveByUnit(page, 9, TextUnit::Word, -1, moved), 4u);
	EXPECT_EQ(moved, -1);
	EXPECT_EQ(helpers::moveByUnit(page, 0, TextUnit::Line, 10, moved),
	          page.size());
	EXPECT_EQ(moved, 4);
	EXPECT_EQ(helpers::moveByUnit(page, 20, TextUnit::Document, -3, moved), 0u);
	EXPECT_EQ(moved, -1);
	EXPECT_EQ(helpers::moveByUnit(page, 0, TextUnit::Line, -1, moved), 0u);
	EXPECT_EQ(moved, 0);
}

TEST(TextRange, FindsTextBothWays) {
	const auto first =
	    helpers::findText(page, 0, page.size(), L"E", false, true);
	ASSERT_TRUE(first);
	EXPECT_EQ(first->first, 2u);
	const auto last =
	    helpers::findText(page, 0, page.size(), L"e", true, false);
	ASSERT_TRUE(last);
	EXPECT_EQ(last->first, 27u);
	EXPECT_FALSE(helpers::findText(page, 0, page.size(), L"E", false, false));
	EXPECT_FALSE(helpers::findText(page, 0, 3, L"two", false, true));
	EXPECT_FALSE(helpers::findText(page, 0, page.size(), L"", false, true));
}
} // namespace
