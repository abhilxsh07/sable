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
#include <cmath>
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
	ASSERT_TRUE(note.position);
	EXPECT_NEAR(note.position->x, 1. / 9, 1e-6);
	EXPECT_NEAR(note.position->y, 1. / 7, 1e-6);
	EXPECT_EQ(result->entries[1].subtype, "Highlight");
	EXPECT_EQ(result->entries[1].contents, "Review");
	EXPECT_TRUE(result->entries[1].author.empty());
	EXPECT_EQ(result->entries[2].subtype, "Link");
	EXPECT_EQ(result->entries[3].subtype, "Widget");
	EXPECT_EQ(result->entries[4].subtype, "FutureNote");
	EXPECT_EQ(result->entries[4].contents, "Keep me");
	EXPECT_FALSE(result->entries[4].bounds);
	EXPECT_FALSE(result->entries[4].position);
	EXPECT_TRUE(result->entries[5].contents.empty());
	EXPECT_TRUE(result->entries[5].author.empty());
	EXPECT_FALSE(result->entries[5].bounds);
	EXPECT_FALSE(result->entries[5].position);
}

TEST(Annotations, LocationsUseClippedCentersAndIntrinsicRotation) {
	std::vector<pdf::Annotations> pages;
	{
		pdf::Engine engine;
		pages = engine
		            .run([] {
			            std::vector<pdf::Annotations> snapshots;
			            const auto opened =
			                pdf::openFile(fixturePath("comment_locations.pdf"));
			            if (opened.document) {
				            for (int page = 0; page < 4; ++page) {
					            if (auto result =
					                    opened.document->annotations(page)) {
						            snapshots.push_back(std::move(*result));
					            }
				            }
				            EXPECT_TRUE(opened.document->render(0, 200, 300));
				            if (auto result = opened.document->annotations(0)) {
					            snapshots.push_back(std::move(*result));
				            }
			            }
			            return snapshots;
		            })
		            .get();
	}
	ASSERT_EQ(pages.size(), 5u);
	// i use the [20 40 220 340] media/crop intersection: 200 by 300.
	// the first center is (60,115), or (.2,.75) from the top left.
	// i check page zero again after rendering to catch appearance changes.
	const pdf::AnnotationPoint expected[] = {
	    {.2, .75}, {.25, .2}, {.8, .25}, {.75, .8}, {.2, .75}};
	for (std::size_t page = 0; page < pages.size(); ++page) {
		ASSERT_FALSE(pages[page].entries.empty());
		const auto& note = pages[page].entries[0];
		ASSERT_TRUE(note.position);
		EXPECT_NEAR(note.position->x, expected[page].x, 1e-6);
		EXPECT_NEAR(note.position->y, expected[page].y, 1e-6);
		ASSERT_TRUE(note.bounds);
		EXPECT_FLOAT_EQ(note.bounds->left, 50);
		EXPECT_FLOAT_EQ(note.bounds->bottom, 100);
		EXPECT_FLOAT_EQ(note.bounds->right, 70);
		EXPECT_FLOAT_EQ(note.bounds->top, 130);
	}
	const auto& rows = pages[0].entries;
	ASSERT_EQ(rows.size(), 10u);
	ASSERT_TRUE(rows[1].position);
	EXPECT_NEAR(rows[1].position->x, .8, 1e-6);
	EXPECT_NEAR(rows[1].position->y, .25, 1e-6);
	ASSERT_TRUE(rows[2].position);
	EXPECT_NEAR(rows[2].position->x, .1, 1e-6);
	EXPECT_NEAR(rows[2].position->y, .9, 1e-6);
	ASSERT_TRUE(rows[2].bounds);
	EXPECT_FLOAT_EQ(rows[2].bounds->left, 0);
	EXPECT_FLOAT_EQ(rows[2].bounds->bottom, 10);
	for (const auto index : {3, 4, 5, 7}) {
		EXPECT_FALSE(rows[index].position);
	}
	EXPECT_TRUE(rows[3].bounds);
	ASSERT_TRUE(rows[4].bounds);
	EXPECT_FLOAT_EQ(rows[4].bounds->left, rows[4].bounds->right);
	EXPECT_FALSE(rows[5].bounds);
	EXPECT_FALSE(rows[7].bounds);
	ASSERT_TRUE(rows[6].bounds);
	EXPECT_TRUE(std::isfinite(rows[6].bounds->left));
	EXPECT_TRUE(std::isfinite(rows[6].bounds->right));
	EXPECT_TRUE(std::isfinite(rows[6].bounds->bottom));
	EXPECT_TRUE(std::isfinite(rows[6].bounds->top));
	EXPECT_LT(rows[6].bounds->left, -1e29f);
	EXPECT_LT(rows[6].bounds->bottom, -1e29f);
	EXPECT_GT(rows[6].bounds->right, 1e29f);
	EXPECT_GT(rows[6].bounds->top, 1e29f);
	ASSERT_TRUE(rows[6].position);
	EXPECT_NEAR(rows[6].position->x, .5, 1e-6);
	EXPECT_NEAR(rows[6].position->y, .5, 1e-6);
	ASSERT_TRUE(rows[8].bounds);
	EXPECT_FLOAT_EQ(rows[8].bounds->right, 250);
	for (const auto* snapshot : {&pages[0], &pages.back()}) {
		ASSERT_EQ(snapshot->entries.size(), 10u);
		const auto& generated = snapshot->entries[8];
		ASSERT_TRUE(generated.position);
		EXPECT_NEAR(generated.position->x, .2, 1e-6);
		EXPECT_NEAR(generated.position->y, 23. / 30, 1e-6);
		const auto& empty = snapshot->entries[9];
		ASSERT_TRUE(empty.position);
		EXPECT_NEAR(empty.position->x, .5, 1e-6);
		EXPECT_NEAR(empty.position->y, .5, 1e-6);
		ASSERT_TRUE(empty.bounds);
		EXPECT_FLOAT_EQ(empty.bounds->right, 190);
	}
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
		    ASSERT_TRUE(again->entries[0].position);
		    EXPECT_NEAR(again->entries[0].position->x, 1. / 9, 1e-6);
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
