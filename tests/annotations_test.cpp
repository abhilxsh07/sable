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
#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf_test_files.h"

TEST(Annotations, SnapshotsOutliveTheDocumentAndEngine) {
	std::optional<pdf::Annotations> result;
	{
		pdf::Engine engine;
		result = engine
		             .run([] {
			             const auto opened =
			                 pdf::openFile(fixturePath("annotations.pdf"));
			             return opened.document
			                        ? opened.document->annotations(0)
			                        : std::nullopt;
		             })
		             .get();
	}
	ASSERT_TRUE(result);
	EXPECT_EQ(result->page, 0);
	EXPECT_FALSE(result->limited);
	ASSERT_EQ(result->entries.size(), 6u);
	const auto& note = result->entries[0];
	EXPECT_EQ(note.index, 0);
	EXPECT_EQ(note.subtype, "Text");
	EXPECT_EQ(note.contents, "Notes \u03a9\U0001f600\nsecond line");
	EXPECT_EQ(note.author, "Abhilash \u03a9");
	EXPECT_EQ(note.flags, 68);
	ASSERT_TRUE(note.bounds);
	// Crop and /Rotate do not change the stored PDF rectangle.
	EXPECT_FLOAT_EQ(note.bounds->left, 40);
	EXPECT_FLOAT_EQ(note.bounds->bottom, 50);
	EXPECT_FLOAT_EQ(note.bounds->right, 60);
	EXPECT_FLOAT_EQ(note.bounds->top, 70);
	EXPECT_EQ(result->entries[1].subtype, "Highlight");
	EXPECT_EQ(result->entries[1].contents, "Review");
	EXPECT_TRUE(result->entries[1].author.empty());
	EXPECT_EQ(result->entries[2].subtype, "Link");
	EXPECT_EQ(result->entries[3].subtype, "Widget");
	EXPECT_EQ(result->entries[4].subtype, "FutureNote");
	EXPECT_EQ(result->entries[4].contents, "Keep me");
	EXPECT_FALSE(result->entries[4].bounds);
	EXPECT_TRUE(result->entries[5].contents.empty());
	EXPECT_TRUE(result->entries[5].author.empty());
	EXPECT_FALSE(result->entries[5].bounds);
}

TEST(Annotations, EmptyPagesErrorsAndCancellationLeaveTheDocumentUsable) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("annotations.pdf"));
		    ASSERT_TRUE(opened.document);
		    const auto& document = *opened.document;
		    const auto empty = document.annotations(1);
		    ASSERT_TRUE(empty);
		    EXPECT_EQ(empty->page, 1);
		    EXPECT_TRUE(empty->entries.empty());
		    EXPECT_FALSE(empty->limited);
		    EXPECT_FALSE(document.annotations(-1));
		    EXPECT_FALSE(document.annotations(document.pageCount()));
		    EXPECT_FALSE(document.annotations(0, [] { return true; }));
		    int checks = 0;
		    EXPECT_FALSE(
		        document.annotations(1, [&] { return ++checks == 2; }));
		    checks = 0;
		    EXPECT_FALSE(
		        document.annotations(0, [&] { return ++checks == 3; }));
		    const auto again = document.annotations(0);
		    ASSERT_TRUE(again);
		    ASSERT_EQ(again->entries.size(), 6u);
		    EXPECT_EQ(again->entries[0].contents,
		              "Notes \u03a9\U0001f600\nsecond line");
		    EXPECT_TRUE(document.render(0, 100, 100));
	    })
	    .get();
}

TEST(Annotations, OversizedFieldsAndPageLimitsAreReported) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("annotations.pdf"));
		    ASSERT_TRUE(opened.document);
		    const auto field = opened.document->annotations(2);
		    ASSERT_TRUE(field);
		    EXPECT_TRUE(field->limited);
		    ASSERT_EQ(field->entries.size(), 1u);
		    EXPECT_TRUE(field->entries[0].contents.empty());
		    EXPECT_EQ(field->entries[0].author, "Still here");
		    const auto count = opened.document->annotations(3);
		    ASSERT_TRUE(count);
		    EXPECT_TRUE(count->limited);
		    ASSERT_EQ(count->entries.size(), 4096u);
		    EXPECT_EQ(count->entries.back().index, 4095);
		    const auto budget = opened.document->annotations(4);
		    ASSERT_TRUE(budget);
		    EXPECT_TRUE(budget->limited);
		    ASSERT_EQ(budget->entries.size(), 40u);
		    EXPECT_EQ(budget->entries[0].contents.size(), 16384u);
		    EXPECT_TRUE(budget->entries.back().contents.empty());
	    })
	    .get();
}
