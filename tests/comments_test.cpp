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
#include <limits>
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

TEST(CommentScan, ReadableLabelsKeepUnknownTypes) {
	EXPECT_EQ(helpers::commentLabel("Text"), "Note");
	EXPECT_EQ(helpers::commentLabel("StrikeOut"), "Strikethrough");
	EXPECT_EQ(helpers::commentLabel("FreeText"), "Text box");
	EXPECT_EQ(helpers::commentLabel("Square"), "Rectangle");
	EXPECT_EQ(helpers::commentLabel("Circle"), "Ellipse");
	EXPECT_EQ(helpers::commentLabel("FileAttachment"), "Attachment");
	EXPECT_EQ(helpers::commentLabel("PolyLine"), "Polyline");
	EXPECT_EQ(helpers::commentLabel("Highlight"), "Highlight");
	EXPECT_EQ(helpers::commentLabel("Underline"), "Underline");
	EXPECT_EQ(helpers::commentLabel("Future"), "Future");
}

TEST(CommentScan, KeepsMutationTargetAndAnnotationColor) {
	auto entry = note("StrikeOut", "preview");
	entry.target = {0, 7, "StrikeOut", 19};
	entry.color = pdf::AnnotationColor{211, 34, 55, 128};
	helpers::CommentScan scan(1);
	ASSERT_TRUE(scan.append({0, {entry, note()}}));
	ASSERT_EQ(scan.rows().size(), 2u);
	const auto& row = scan.rows().front();
	EXPECT_EQ(row.target.page, 0);
	EXPECT_EQ(row.target.index, 7);
	EXPECT_EQ(row.target.subtype, "StrikeOut");
	EXPECT_EQ(row.target.revision, 19u);
	ASSERT_TRUE(row.color);
	EXPECT_EQ(row.color->red, 211u);
	EXPECT_EQ(row.color->green, 34u);
	EXPECT_EQ(row.color->blue, 55u);
	EXPECT_EQ(row.color->alpha, 128u);
	EXPECT_FALSE(scan.rows()[1].color);
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

TEST(CommentScan, KeepsDistinctPositionsOnTheSamePage) {
	auto first = note(), second = note();
	first.position = pdf::AnnotationPoint{.1, .8};
	second.index = 9;
	second.position = pdf::AnnotationPoint{.7, .2};
	helpers::CommentScan scan(1);
	ASSERT_TRUE(scan.append({0, {first, second, note()}}));
	ASSERT_EQ(scan.rows().size(), 3u);
	ASSERT_TRUE(scan.rows()[0].position);
	ASSERT_TRUE(scan.rows()[1].position);
	EXPECT_DOUBLE_EQ(scan.rows()[0].position->y, .8);
	EXPECT_DOUBLE_EQ(scan.rows()[1].position->x, .7);
	EXPECT_EQ(scan.rows()[1].index, 9);
	EXPECT_FALSE(scan.rows()[2].position);
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

TEST(TextBoxDimension, PositiveCompleteNumbersAndInvalidDrafts) {
	EXPECT_EQ(helpers::parseTextBoxDimension("200"), 200.f);
	EXPECT_EQ(helpers::parseTextBoxDimension("  +60.25\t"), 60.25f);
	EXPECT_EQ(helpers::parseTextBoxDimension("2e2"), 200.f);
	for (const auto* text :
	     {"", " ", "+", "0", "-1", "nan", "inf", "1e1000", "1e-1000", "20pt",
	      "20 30", "20,5", "20\n", "--2"}) {
		EXPECT_FALSE(helpers::parseTextBoxDimension(text)) << text;
	}
}

TEST(TextBoxDimension, RoundedDisplayKeepsExactUnchangedSizes) {
	for (const float value : {200.f, 60.f, 200.20001f, 60.199997f, 0.001f,
	                          (std::numeric_limits<float>::max)(),
	                          (std::numeric_limits<float>::min)()}) {
		const auto text = helpers::textBoxDimension(value);
		ASSERT_FALSE(text.empty());
		EXPECT_EQ(helpers::parseTextBoxDimension(text, value), value) << text;
	}
	EXPECT_EQ(helpers::textBoxDimension(199.99998f), "200");
	EXPECT_EQ(helpers::textBoxDimension(239.99998f), "240");
	EXPECT_EQ(helpers::textBoxDimension(60.199997f), "60.2");
	EXPECT_EQ(helpers::textBoxDimension(0.001f), "0.001");
	EXPECT_EQ(helpers::parseTextBoxDimension("250", 239.99998f), 250.f);
	EXPECT_FALSE(helpers::parseTextBoxDimension("", 200.f));
	EXPECT_TRUE(helpers::textBoxDimension(0).empty());
	EXPECT_TRUE(helpers::textBoxDimension(-1).empty());
	EXPECT_TRUE(
	    helpers::textBoxDimension(std::numeric_limits<float>::infinity())
	        .empty());
	EXPECT_TRUE(
	    helpers::textBoxDimension(std::numeric_limits<float>::quiet_NaN())
	        .empty());
}
