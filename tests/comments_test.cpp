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
#include <utility>
#include "comments.h"

namespace {
pdf::Annotation note(std::string type = "Text", std::string text = {}) {
	pdf::Annotation entry;
	entry.index = 7;
	entry.subtype = std::move(type);
	entry.contents = std::move(text);
	return entry;
}

TEST(CommentScan, SequentialPagesRetryAndReset) {
	helpers::CommentScan scan(3);
	EXPECT_FALSE(scan.append({1, {note()}}));
	ASSERT_TRUE(scan.append({0, {note()}}));
	scan.fail();
	EXPECT_TRUE(scan.failed());
	EXPECT_FALSE(scan.append({1, {note()}}));
	scan.retry();
	ASSERT_TRUE(scan.append({1, {note("Highlight")}}));
	EXPECT_FALSE(scan.append({1, {note()}}));
	ASSERT_TRUE(scan.append({2, {}}));
	EXPECT_TRUE(scan.done());
	EXPECT_EQ(scan.nextPage(), 3);
	ASSERT_EQ(scan.rows().size(), 2u);
	EXPECT_EQ(scan.rows()[1].page, 1);
	EXPECT_EQ(scan.rows()[1].index, 7);
	EXPECT_FALSE(scan.append({3, {note()}}));
	scan.reset(1);
	EXPECT_TRUE(scan.rows().empty());
	EXPECT_FALSE(scan.done());
	EXPECT_FALSE(scan.failed());
	EXPECT_FALSE(scan.limited());
	EXPECT_EQ(scan.nextPage(), 0);
	EXPECT_EQ(scan.pageCount(), 1);
	EXPECT_TRUE(helpers::CommentScan().done());
	EXPECT_EQ(helpers::CommentScan(-1).pageCount(), 0);
}

TEST(CommentScan, TypesAndTextDetermineCommentRows) {
	pdf::Annotations page;
	for (const auto* type :
	     {"Text", "FreeText", "Line", "Square", "Circle", "Polygon", "PolyLine",
	      "Highlight", "Underline", "Squiggly", "StrikeOut", "Stamp", "Caret",
	      "Ink", "FileAttachment", "Sound"}) {
		page.entries.push_back(note(type));
	}
	for (const auto* type : {"Link", "Widget", "XFAWidget", "Popup"}) {
		page.entries.push_back(note(type, "excluded"));
	}
	page.entries.push_back(note("Future"));
	page.entries.push_back(note("Future", "kept"));
	page.entries.push_back(note("", "fallback"));
	helpers::CommentScan scan(1);
	ASSERT_TRUE(scan.append(std::move(page)));
	ASSERT_EQ(scan.rows().size(), 18u);
	EXPECT_EQ(scan.rows()[16].subtype, "Future");
	EXPECT_EQ(scan.rows()[17].subtype, "Annotation");
}

TEST(CommentScan, SanitizesControlsWithoutChangingUnicode) {
	auto entry = note("Future\nType", "caf\xc3\xa9\n\r\n\r\t\x7f");
	entry.author = std::string("A\0B\nC", 5);
	entry.contents.push_back('\0');
	helpers::CommentScan scan(1);
	ASSERT_TRUE(scan.append({0, {entry}}));
	ASSERT_EQ(scan.rows().size(), 1u);
	EXPECT_EQ(scan.rows()[0].subtype, "Future Type");
	EXPECT_EQ(scan.rows()[0].author, "A B C");
	EXPECT_EQ(scan.rows()[0].text, "caf\xc3\xa9\n\n\n   ");
}

TEST(CommentScan, PageLimitIsReportedButLaterPagesStillLoad) {
	helpers::CommentScan scan(2);
	ASSERT_TRUE(scan.append({0, {}, true}));
	EXPECT_TRUE(scan.limited());
	EXPECT_FALSE(scan.done());
	ASSERT_TRUE(scan.append({1, {note()}}));
	EXPECT_TRUE(scan.done());
	EXPECT_TRUE(scan.limited());
}

TEST(CommentScan, GlobalRowLimitStopsAcrossPages) {
	helpers::CommentScan scan(3);
	ASSERT_TRUE(scan.append({0, std::vector<pdf::Annotation>(1999, note())}));
	EXPECT_FALSE(scan.done());
	ASSERT_TRUE(scan.append({1, {note(), note()}}));
	EXPECT_EQ(scan.rows().size(), 2000u);
	EXPECT_TRUE(scan.done());
	EXPECT_TRUE(scan.limited());
	scan.retry();
	EXPECT_FALSE(scan.append({2, {note()}}));
}

TEST(CommentScan, GlobalByteLimitKeepsWholeUnicodeStrings) {
	constexpr std::size_t limit = 4 * 1024 * 1024;
	helpers::CommentScan scan(3);
	ASSERT_TRUE(scan.append({0, {note("Text", std::string(limit - 10, 'a'))}}));
	ASSERT_TRUE(scan.append({1, {note("Text", "\xe2\x82\xac")}}));
	EXPECT_EQ(scan.rows().size(), 1u);
	EXPECT_TRUE(scan.done());
	EXPECT_TRUE(scan.limited());
	scan.reset(2);
	ASSERT_TRUE(
	    scan.append({0, {note("Text", std::string(limit - 5, 'a') + "\r\n")}}));
	EXPECT_EQ(scan.rows()[0].text.size(), limit - 4);
	EXPECT_TRUE(scan.done());
	EXPECT_TRUE(scan.limited());
}
} // namespace
