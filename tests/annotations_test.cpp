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
#include <fpdf_annot.h>
#include <fpdfview.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <utility>
#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf_test_files.h"

namespace {
std::vector<std::uint8_t> annotationBytes(const char* name) {
	std::ifstream file(fixturePath(name), std::ios::binary);
	return {std::istreambuf_iterator<char>(file), {}};
}
std::vector<std::uint8_t> editableNoteBytes(const std::string& fields,
                                            std::string_view type = "Text") {
	std::string data = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	for (const auto& body : std::vector<std::string>{
	         "<< /Type /Catalog /Pages 2 0 R >>",
	         "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
	         "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
	         "/Annots [4 0 R] >>",
	         "<< /Type /Annot /Subtype /" + std::string(type) +
	             " /Rect [40 50 60 70] " + fields + " >>"}) {
		offsets.push_back(data.size());
		data +=
		    std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
	}
	const auto xref = data.size();
	data += "xref\n0 5\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		data += line;
	}
	data += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" +
	        std::to_string(xref) + "\n%%EOF\n";
	return {data.begin(), data.end()};
}
std::vector<std::uint8_t>
sharedHighlightBytes(int count, std::string_view secondBox = "0 0 200 300",
                     bool shared = true, std::string_view type = "Text",
                     bool independent = false) {
	const std::string contents = "BT /F1 20 Tf 40 230 Td (Alpha beta) Tj ET";
	std::string refs = "[";
	for (int i = 0; i < count; ++i) {
		refs += "8 0 R ";
	}
	refs += "]";
	std::string data = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	for (const auto& body : std::vector<std::string>{
	         "<< /Type /Catalog /Pages 2 0 R >>",
	         "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
	         "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
	         "/Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R /Annots " +
	             std::string(shared        ? "7 0 R"
	                         : independent ? "[8 0 R]"
	                                       : "[]") +
	             " >>",
	         "<< /Type /Page /Parent 2 0 R /MediaBox [" +
	             std::string(secondBox) +
	             "] /Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R "
	             "/Annots 7 0 R >>",
	         "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
	         "<< /Length " + std::to_string(contents.size()) + " >>\nstream\n" +
	             contents + "\nendstream",
	         refs,
	         "<< /Type /Annot /Subtype /" + std::string(type) +
	             " /Rect [100 100 120 120] /Contents (Existing) >>"}) {
		offsets.push_back(data.size());
		data +=
		    std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
	}
	const auto xref = data.size();
	data += "xref\n0 9\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		data += line;
	}
	data += "trailer\n<< /Size 9 /Root 1 0 R >>\nstartxref\n" +
	        std::to_string(xref) + "\n%%EOF\n";
	return {data.begin(), data.end()};
}
} // namespace

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

TEST(Annotations, NewNotesKeepUnicodeAndRenderWithoutChangingTheSource) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto source = fixturePath("annotations.pdf");
		    const auto bytes = [&] {
			    std::ifstream file(source, std::ios::binary);
			    return std::string(std::istreambuf_iterator<char>(file), {});
		    };
		    const auto original = bytes();
		    ASSERT_FALSE(original.empty());
		    const auto opened = pdf::openFile(source);
		    ASSERT_TRUE(opened.document);
		    auto& document = *opened.document;
		    ASSERT_TRUE(document.canAnnotate());
		    const auto before = document.render(1, 360, 280);
		    ASSERT_TRUE(before);
		    const auto added = document.addTextNote(
		        1, 40, 50, u"New \u03a9\U0001f600\nsecond line",
		        u"Abhilash \u03a9");
		    ASSERT_EQ(added, 0);
		    const auto notes = document.annotations(1);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 1u);
		    const auto& note = notes->entries[0];
		    EXPECT_EQ(note.index, *added);
		    EXPECT_EQ(note.subtype, "Text");
		    EXPECT_EQ(note.contents, "New \u03a9\U0001f600\nsecond line");
		    EXPECT_EQ(note.author, "Abhilash \u03a9");
		    EXPECT_EQ(note.flags, 4);
		    ASSERT_TRUE(note.bounds);
		    EXPECT_FLOAT_EQ(note.bounds->left, 40);
		    EXPECT_FLOAT_EQ(note.bounds->bottom, 50);
		    EXPECT_FLOAT_EQ(note.bounds->right, 60);
		    EXPECT_FLOAT_EQ(note.bounds->top, 70);
		    const auto after = document.render(1, 360, 280);
		    ASSERT_TRUE(after);
		    EXPECT_NE(before->pixels, after->pixels);
		    const auto again = document.annotations(1);
		    ASSERT_TRUE(again);
		    ASSERT_EQ(again->entries.size(), 1u);
		    EXPECT_EQ(again->entries[0].contents, note.contents);
		    EXPECT_EQ(document.addTextNote(1, 80, 100, u"Another"), 1);
		    const auto otherPage = document.annotations(0);
		    ASSERT_TRUE(otherPage);
		    EXPECT_EQ(otherPage->entries.size(), 6u);
		    const auto reopened = pdf::openFile(source);
		    ASSERT_TRUE(reopened.document);
		    const auto unchanged = reopened.document->annotations(1);
		    ASSERT_TRUE(unchanged);
		    EXPECT_TRUE(unchanged->entries.empty());
		    EXPECT_EQ(bytes(), original);
	    })
	    .get();
}

TEST(Annotations, NewNoteLocationsSurviveCroppingRotationAndRendering) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened =
		        pdf::openFile(fixturePath("comment_locations.pdf"));
		    ASSERT_TRUE(opened.document);
		    const pdf::AnnotationPoint expected[] = {
		        {.2, 23. / 30}, {7. / 30, .2}, {.8, 7. / 30}, {23. / 30, .8}};
		    for (int page = 0; page < 4; ++page) {
			    const auto added =
			        opened.document->addTextNote(page, 50, 100, u"Here");
			    ASSERT_TRUE(added);
			    for (int pass = 0; pass < 2; ++pass) {
				    const auto notes = opened.document->annotations(page);
				    ASSERT_TRUE(notes);
				    ASSERT_EQ(notes->entries.size(), std::size_t(*added + 1));
				    const auto& note = notes->entries.back();
				    ASSERT_TRUE(note.position);
				    EXPECT_NEAR(note.position->x, expected[page].x, 1e-6);
				    EXPECT_NEAR(note.position->y, expected[page].y, 1e-6);
				    ASSERT_TRUE(note.bounds);
				    EXPECT_FLOAT_EQ(note.bounds->left, 50);
				    EXPECT_FLOAT_EQ(note.bounds->bottom, 100);
				    EXPECT_FLOAT_EQ(note.bounds->right, 70);
				    EXPECT_FLOAT_EQ(note.bounds->top, 120);
				    EXPECT_TRUE(opened.document->render(page, 200, 300));
			    }
		    }
	    })
	    .get();
}

TEST(Annotations, CenteredNotesRespectCropRotationAndSaveRestrictions) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened =
		        pdf::openFile(fixturePath("comment_locations.pdf"));
		    ASSERT_TRUE(opened.document);
		    for (int page = 0; page < 4; ++page) {
			    ASSERT_TRUE(
			        opened.document->addCenteredTextNote(page, u"Center"));
			    const auto notes = opened.document->annotations(page);
			    ASSERT_TRUE(notes && !notes->entries.empty());
			    const auto& note = notes->entries.back();
			    ASSERT_TRUE(note.position);
			    EXPECT_NEAR(note.position->x, .5, 1e-6);
			    EXPECT_NEAR(note.position->y, .5, 1e-6);
		    }
		    const auto bytes = opened.document->serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    EXPECT_EQ(saved.document->annotations(0)->entries.back().contents,
		              "Center");
		    for (const auto name :
		         {"attachments.pdf", "annotations.pdf", "notes_denied.pdf"}) {
			    const auto blocked = pdf::openFile(fixturePath(name));
			    ASSERT_TRUE(blocked.document);
			    EXPECT_FALSE(blocked.document->addCenteredTextNote(0, u"No"));
			    EXPECT_FALSE(blocked.document->modified());
		    }
	    })
	    .get();
}

TEST(Annotations, PlacedNotesMapDisplayedPointsThroughBothRotations) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened =
		        pdf::openFile(fixturePath("comment_locations.pdf"));
		    ASSERT_TRUE(opened.document);
		    const pdf::AnnotationPoint expected[] = {
		        {.25, .7}, {.7, .75}, {.75, .3}, {.3, .25}};
		    for (int page = 0; page < 4; ++page) {
			    for (int rotation = 0; rotation < 4; ++rotation) {
				    ASSERT_TRUE(opened.document->addTextNoteAt(
				        page, .25, .7, rotation, u"Chosen \u03a9", u"Author"));
				    const auto notes = opened.document->annotations(page);
				    ASSERT_TRUE(notes && !notes->entries.empty());
				    const auto& note = notes->entries.back();
				    ASSERT_TRUE(note.position);
				    EXPECT_NEAR(note.position->x, expected[rotation].x, 2e-6);
				    EXPECT_NEAR(note.position->y, expected[rotation].y, 2e-6);
				    EXPECT_EQ(note.author, "Author");
			    }
		    }
		    const auto bytes = opened.document->serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto reopened = pdf::openDocument(*bytes);
		    ASSERT_TRUE(reopened.document);
		    const auto saved = reopened.document->annotations(3);
		    ASSERT_TRUE(saved && !saved->entries.empty());
		    EXPECT_EQ(saved->entries.back().contents, "Chosen \u03a9");
	    })
	    .get();
}

TEST(Annotations, PlacedNotesClampEdgesAndRefuseInvalidOrUnsafeRequests) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened =
		        pdf::openFile(fixturePath("comment_locations.pdf"));
		    ASSERT_TRUE(opened.document);
		    for (int rotation = 0; rotation < 4; ++rotation) {
			    for (const auto point :
			         {pdf::AnnotationPoint{0, 0}, {1, 1}, {0, 1}, {1, 0}}) {
				    ASSERT_TRUE(opened.document->addTextNoteAt(
				        0, point.x, point.y, rotation, u"Edge"));
				    const auto notes = opened.document->annotations(0);
				    ASSERT_TRUE(notes && notes->entries.back().bounds);
				    const auto box = *notes->entries.back().bounds;
				    EXPECT_GE(box.left, 20);
				    EXPECT_LE(box.right, 220);
				    EXPECT_GE(box.bottom, 40);
				    EXPECT_LE(box.top, 340);
				    EXPECT_FLOAT_EQ(box.right - box.left, 20);
				    EXPECT_FLOAT_EQ(box.top - box.bottom, 20);
			    }
		    }
		    for (const auto name : {"comment_locations.pdf", "attachments.pdf",
		                            "notes_denied.pdf", "note_offsets.pdf"}) {
			    const auto clean = pdf::openFile(fixturePath(name));
			    ASSERT_TRUE(clean.document);
			    for (const auto point :
			         {pdf::AnnotationPoint{-.01, .5},
			          {1.01, .5},
			          {.5, -1},
			          {.5, 2},
			          {std::numeric_limits<double>::quiet_NaN(), .5},
			          {.5, std::numeric_limits<double>::infinity()}}) {
				    EXPECT_FALSE(clean.document->addTextNoteAt(
				        0, point.x, point.y, 0, u"No"));
			    }
			    EXPECT_FALSE(
			        clean.document->addTextNoteAt(0, .5, .5, -1, u"No"));
			    EXPECT_FALSE(
			        clean.document->addTextNoteAt(0, .5, .5, 4, u"No"));
			    if (std::string_view(name) != "comment_locations.pdf") {
				    EXPECT_FALSE(
				        clean.document->addTextNoteAt(0, .5, .5, 0, u"No"));
			    }
			    EXPECT_FALSE(clean.document->modified());
		    }
	    })
	    .get();
}

TEST(Annotations, InvalidNotesLeaveThePageUnchanged) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("annotations.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& document = *opened.document;
		    const float nan = std::numeric_limits<float>::quiet_NaN();
		    const float inf = std::numeric_limits<float>::infinity();
		    for (const auto point : {pdf::AnnotationPoint{nan, 50},
		                             {40, inf},
		                             {9, 50},
		                             {40, 19},
		                             {271, 50},
		                             {40, 361},
		                             {inf, nan}}) {
			    EXPECT_FALSE(document.addTextNote(1, float(point.x),
			                                      float(point.y), u"Rejected"));
		    }
		    EXPECT_FALSE(document.addTextNote(-1, 40, 50, u"Rejected"));
		    EXPECT_FALSE(document.addTextNote(document.pageCount(), 40, 50,
		                                      u"Rejected"));
		    for (const auto& text :
		         {std::u16string{}, std::u16string(32768, u'a'),
		          std::u16string{u'a', 0, u'b'}, std::u16string{0xD800},
		          std::u16string{0xDC00}, std::u16string{0xD800, u'a'}}) {
			    EXPECT_FALSE(document.addTextNote(1, 40, 50, text));
			    if (!text.empty()) {
				    EXPECT_FALSE(
				        document.addTextNote(1, 40, 50, u"Note", text));
			    }
		    }
		    const auto notes = document.annotations(1);
		    ASSERT_TRUE(notes);
		    EXPECT_TRUE(notes->entries.empty());
		    // the whole icon fits exactly at either visible corner.
		    EXPECT_EQ(document.addTextNote(1, 10, 20, u"Corner"), 0);
		    EXPECT_EQ(document.addTextNote(1, 270, 360, u"Corner"), 1);
	    })
	    .get();
}

TEST(Annotations, NewNotesRespectPermissionsAndOwnerAccess) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto restricted =
		        pdf::openFile(fixturePath("restricted.pdf"));
		    ASSERT_TRUE(restricted.document);
		    EXPECT_FALSE(restricted.document->canAnnotate());
		    EXPECT_FALSE(
		        restricted.document->addTextNote(0, 40, 50, u"Denied"));
		    const auto notes = restricted.document->annotations(0);
		    ASSERT_TRUE(notes);
		    EXPECT_TRUE(notes->entries.empty());
		    const auto owner =
		        pdf::openFile(fixturePath("restricted.pdf"), "nightpdf");
		    ASSERT_TRUE(owner.document);
		    EXPECT_TRUE(owner.document->canAnnotate());
		    EXPECT_EQ(owner.document->addTextNote(0, 40, 50, u"Allowed"), 0);
		    for (const bool allowed : {false, true}) {
			    const auto file =
			        allowed ? "notes_allowed.pdf" : "notes_denied.pdf";
			    const auto permissions = pdf::openFile(fixturePath(file));
			    ASSERT_TRUE(permissions.document);
			    EXPECT_EQ(permissions.document->canAnnotate(), allowed);
			    EXPECT_EQ(permissions.document->canCopy(), !allowed);
			    EXPECT_EQ(permissions.document->addTextNote(0, 40, 50, u"Note")
			                  .has_value(),
			              allowed);
		    }
	    })
	    .get();
}

TEST(Annotations, NewNotesRejectIconsCollapsedByFloatPrecision) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("note_offsets.pdf"));
		    ASSERT_TRUE(opened.document);
		    constexpr float offset = 536870912;
		    EXPECT_FALSE(
		        opened.document->addTextNote(0, offset, offset, u"Invisible"));
		    const auto notes = opened.document->annotations(0);
		    ASSERT_TRUE(notes);
		    EXPECT_TRUE(notes->entries.empty());
	    })
	    .get();
}

TEST(Annotations, NewNotesStayWithinSnapshotFieldAndCountLimits) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("annotations.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& document = *opened.document;
		    const std::u16string text(32767, u'a');
		    EXPECT_EQ(document.addTextNote(1, 40, 50, text, text), 0);
		    const auto notes = document.annotations(1);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 1u);
		    EXPECT_FALSE(notes->limited);
		    EXPECT_EQ(notes->entries[0].contents, std::string(32767, 'a'));
		    EXPECT_EQ(notes->entries[0].author, notes->entries[0].contents);
		    EXPECT_FALSE(document.addTextNote(3, 40, 50, u"Full"));
		    // i fill an empty page to check the exact boundary, too.
		    for (int i = 1; i < 4096; ++i) {
			    ASSERT_EQ(document.addTextNote(1, 40, 50, u"Note"), i);
		    }
		    EXPECT_FALSE(document.addTextNote(1, 40, 50, u"Full"));
		    const auto full = document.annotations(1);
		    ASSERT_TRUE(full);
		    EXPECT_EQ(full->entries.size(), 4096u);
		    EXPECT_FALSE(full->limited);
	    })
	    .get();
}

TEST(Annotations, UntouchedSerializationKeepsExactOwnedBytes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto name : {"attachments.pdf", "comment_locations.pdf",
		                            "password.pdf", "restricted.pdf"}) {
			    SCOPED_TRACE(name);
			    const auto original = annotationBytes(name);
			    ASSERT_FALSE(original.empty());
			    const auto opened = pdf::openDocument(
			        original,
			        std::string_view(name) == "password.pdf" ? "sable" : "");
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_TRUE(opened.document->render(0, 100, 100));
			    auto bytes = opened.document->serialize(original.size());
			    ASSERT_TRUE(bytes);
			    EXPECT_EQ(*bytes, original);
			    bytes->at(0) = 0;
			    EXPECT_EQ(opened.document->serialize(original.size()),
			              original);
			    EXPECT_FALSE(opened.document->serialize(original.size() - 1));
			    EXPECT_FALSE(opened.document->modified());
		    }
	    })
	    .get();
}

TEST(Annotations, SerializationKeepsNewNotesBeforeAnyRendering) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("pages.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& document = *opened.document;
		    ASSERT_EQ(document.addTextNote(
		                  1, 40, 50, u"Saved \u03a9\U0001f600\nsecond line",
		                  u"Abhilash \u03a9"),
		              0);
		    EXPECT_TRUE(document.modified());
		    const auto before = document.annotations(1);
		    ASSERT_TRUE(before);
		    ASSERT_EQ(before->entries.size(), 1u);
		    ASSERT_TRUE(before->entries[0].position);
		    const auto bytes = document.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    EXPECT_NE(*bytes, original);
		    const auto reopened = pdf::openDocument(*bytes);
		    ASSERT_TRUE(reopened.document);
		    EXPECT_FALSE(reopened.document->modified());
		    EXPECT_EQ(reopened.document->pageCount(), document.pageCount());
		    const auto notes = reopened.document->annotations(1);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 1u);
		    const auto& note = notes->entries[0];
		    EXPECT_EQ(note.contents, "Saved \u03a9\U0001f600\nsecond line");
		    EXPECT_EQ(note.author, "Abhilash \u03a9");
		    EXPECT_EQ(note.subtype, "Text");
		    EXPECT_EQ(note.flags, 4);
		    ASSERT_TRUE(note.position);
		    EXPECT_NEAR(note.position->x, before->entries[0].position->x, 1e-6);
		    EXPECT_NEAR(note.position->y, before->entries[0].position->y, 1e-6);
		    EXPECT_TRUE(reopened.document->render(1, 360, 280));
		    const auto existing = reopened.document->annotations(0);
		    ASSERT_TRUE(existing);
		    EXPECT_TRUE(existing->entries.empty());
		    EXPECT_TRUE(document.modified());
		    EXPECT_EQ(annotationBytes("pages.pdf"), original);
		    ASSERT_EQ(document.addTextNote(1, 80, 100, u"Later"), 1);
		    const auto newer = document.serialize(1024 * 1024);
		    ASSERT_TRUE(newer);
		    const auto latest = pdf::openDocument(*newer);
		    ASSERT_TRUE(latest.document);
		    const auto both = latest.document->annotations(1);
		    ASSERT_TRUE(both);
		    EXPECT_EQ(both->entries.size(), 2u);
	    })
	    .get();
}

TEST(Annotations, SerializationRetainsEncryptionAndUserPermissions) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto name :
		         {"password.pdf", "notes_allowed.pdf", "restricted.pdf"}) {
			    SCOPED_TRACE(name);
			    const std::string user =
			        std::string_view(name) == "password.pdf" ? "sable" : "";
			    const auto original = annotationBytes(name);
			    const auto access = pdf::openDocument(original, user);
			    const auto opened = pdf::openDocument(
			        original, std::string_view(name) == "restricted.pdf"
			                      ? "nightpdf"
			                      : user);
			    ASSERT_TRUE(access.document && opened.document);
			    ASSERT_EQ(opened.document->addTextNote(0, 40, 50, u"Saved"), 0);
			    const auto bytes = opened.document->serialize(1024 * 1024);
			    ASSERT_TRUE(bytes);
			    if (!user.empty()) {
				    EXPECT_EQ(pdf::openDocument(*bytes).error,
				              pdf::Error::PasswordRequired);
				    EXPECT_EQ(pdf::openDocument(*bytes, "wrong").error,
				              pdf::Error::WrongPassword);
			    }
			    const auto reopened = pdf::openDocument(*bytes, user);
			    ASSERT_TRUE(reopened.document);
			    EXPECT_EQ(reopened.document->canCopy(),
			              access.document->canCopy());
			    EXPECT_EQ(reopened.document->canAnnotate(),
			              access.document->canAnnotate());
			    const auto notes = reopened.document->annotations(0);
			    ASSERT_TRUE(notes);
			    ASSERT_EQ(notes->entries.size(), 1u);
			    EXPECT_EQ(notes->entries[0].contents, "Saved");
		    }
	    })
	    .get();
}

TEST(Annotations, SerializationDiscardsLimitedCanceledAndThrowingWrites) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("comments.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& document = *opened.document;
		    ASSERT_TRUE(document.addTextNote(0, 40, 50, u"Saved"));
		    const auto full = document.serialize(1024 * 1024);
		    ASSERT_TRUE(full && !full->empty());
		    EXPECT_FALSE(document.serialize(0));
		    EXPECT_FALSE(document.serialize(full->size() - 1));
		    EXPECT_TRUE(document.serialize(full->size()));
		    EXPECT_FALSE(document.serialize(1024 * 1024, [] { return true; }));
		    for (const int stop : {2, 3}) {
			    int checks = 0;
			    EXPECT_FALSE(document.serialize(
			        1024 * 1024, [&] { return ++checks >= stop; }));
			    EXPECT_GE(checks, stop);
		    }
		    int checks = 0;
		    EXPECT_FALSE(document.serialize(1024 * 1024, [&]() -> bool {
			    if (++checks == 2) {
				    throw 1;
			    }
			    return false;
		    }));
		    EXPECT_TRUE(document.modified());
		    EXPECT_TRUE(document.serialize(1024 * 1024));
		    EXPECT_TRUE(document.render(0, 100, 100));
	    })
	    .get();
}

TEST(Annotations, RejectedNotesLeaveSerializationOnTheExactBytePath) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto name : {"annotations.pdf", "notes_denied.pdf"}) {
			    const auto original = annotationBytes(name);
			    const auto opened = pdf::openDocument(original);
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(
			        opened.document->addTextNote(0, -1, -1, u"Outside"));
			    if (std::string_view(name) == "notes_denied.pdf") {
				    EXPECT_FALSE(
				        opened.document->addTextNote(0, 40, 50, u"Denied"));
			    }
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->serialize(original.size()),
			              original);
			    int checks = 0;
			    EXPECT_FALSE(opened.document->serialize(
			        original.size(), [&] { return ++checks == 2; }));
		    }
	    })
	    .get();
}

TEST(Annotations, EditedAttachmentsRefuseSerializationWithoutLosingNotes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("attachments.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    ASSERT_TRUE(opened.document->addTextNote(0, 40, 50, u"Saved"));
		    EXPECT_FALSE(opened.document->serialize(1024 * 1024));
		    EXPECT_TRUE(opened.document->modified());
		    const auto notes = opened.document->annotations(0);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 1u);
		    EXPECT_EQ(notes->entries[0].contents, "Saved");
		    EXPECT_EQ(opened.document->attachment(1),
		              std::vector<std::uint8_t>{});
		    EXPECT_EQ(annotationBytes("attachments.pdf"), original);
	    })
	    .get();
}

TEST(Annotations, SerializationAlsoRejectsAnnotationOnlyAttachments) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("note_attachment.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    const auto files = opened.document->attachments();
		    ASSERT_TRUE(files);
		    EXPECT_TRUE(files->entries.empty());
		    EXPECT_EQ(opened.document->serialize(original.size()), original);
		    ASSERT_EQ(opened.document->addTextNote(0, 80, 100, u"Keep me"), 1);
		    EXPECT_FALSE(opened.document->serialize(1024 * 1024));
		    EXPECT_TRUE(opened.document->modified());
		    const auto notes = opened.document->annotations(0);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 2u);
		    EXPECT_EQ(notes->entries[0].subtype, "FileAttachment");
		    EXPECT_EQ(notes->entries[1].contents, "Keep me");
		    EXPECT_EQ(annotationBytes("note_attachment.pdf"), original);
	    })
	    .get();
}

TEST(Annotations, SerializationRefusesOversizedAnnotationInventories) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("annotations.pdf"));
		    ASSERT_TRUE(opened.document);
		    ASSERT_EQ(opened.document->addTextNote(1, 40, 50, u"Keep me"), 0);
		    EXPECT_FALSE(opened.document->serialize(1024 * 1024));
		    const auto notes = opened.document->annotations(1);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 1u);
		    EXPECT_EQ(notes->entries[0].contents, "Keep me");
		    EXPECT_TRUE(opened.document->modified());
	    })
	    .get();
}

TEST(Annotations, ExistingNotesKeepExactContentsAndRejectStaleEdits) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto source = editableNoteBytes(
		        "/Contents <FEFF0041000D000A000900420001> /T (Author) /F 132");
		    const auto opened = pdf::openDocument(source);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const std::u16string original = u"A\r\n\tB\x01";
		    EXPECT_EQ(doc.textNote(0, 0), original);
		    EXPECT_EQ(doc.updateTextNote(0, 0, original, original), false);
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.serialize(1024 * 1024), source);
		    EXPECT_FALSE(doc.updateTextNote(0, 0, u"A\n B ", u"Wrong"));
		    EXPECT_FALSE(doc.modified());
		    const std::u16string changed =
		        u"Edited \u03a9\U0001f600\nsecond line";
		    ASSERT_EQ(doc.updateTextNote(0, 0, original, changed), true);
		    EXPECT_TRUE(doc.modified());
		    EXPECT_FALSE(doc.updateTextNote(0, 0, original, u"Stale"));
		    EXPECT_EQ(doc.textNote(0, 0), changed);
		    const auto bytes = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto reopened = pdf::openDocument(*bytes);
		    ASSERT_TRUE(reopened.document);
		    EXPECT_EQ(reopened.document->textNote(0, 0), changed);
		    const auto notes = reopened.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 1);
		    const auto& note = notes->entries[0];
		    EXPECT_EQ(note.flags, 132);
		    EXPECT_EQ(note.author, "Author");
		    ASSERT_TRUE(note.bounds);
		    EXPECT_FLOAT_EQ(note.bounds->left, 40);
		    EXPECT_FLOAT_EQ(note.bounds->bottom, 50);
		    EXPECT_FLOAT_EQ(note.bounds->right, 60);
		    EXPECT_FLOAT_EQ(note.bounds->top, 70);
	    })
	    .get();
}

TEST(Annotations, ExistingNotesRefuseProtectedRichMalformedAndUnsafeContents) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto* fields :
		         {"/Contents (Note) /F 64", "/Contents (Note) /F 512",
		          "/Contents (Note) /RC (Rich)", "/Contents /Note", "",
		          "/Contents <FEFFD800>", "/Contents <FEFF004100000042>"}) {
			    const auto source = editableNoteBytes(fields);
			    const auto opened = pdf::openDocument(source);
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(opened.document->textNote(0, 0));
			    EXPECT_FALSE(
			        opened.document->updateTextNote(0, 0, u"Note", u"Edit"));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), source);
		    }
		    const auto opened =
		        pdf::openDocument(editableNoteBytes("/Contents ()"));
		    ASSERT_TRUE(opened.document);
		    EXPECT_EQ(opened.document->textNote(0, 0), u"");
		    EXPECT_EQ(opened.document->updateTextNote(0, 0, u"", u"Now filled"),
		              true);
		    for (const auto& text :
		         {std::u16string{}, std::u16string(32768, u'a'),
		          std::u16string(1, 0xD800), std::u16string(u"A\0B", 3)}) {
			    EXPECT_FALSE(
			        opened.document->updateTextNote(0, 0, u"Now filled", text));
		    }
		    for (const auto* file : {"annotations.pdf", "note_attachment.pdf",
		                             "notes_denied.pdf"}) {
			    const auto unsafe = pdf::openFile(fixturePath(file));
			    ASSERT_TRUE(unsafe.document);
			    EXPECT_FALSE(unsafe.document->textNote(0, 0));
			    EXPECT_FALSE(
			        unsafe.document->updateTextNote(0, 0, u"Note", u"Edit"));
			    EXPECT_FALSE(unsafe.document->modified());
		    }
	    })
	    .get();
}

TEST(Annotations,
     ExistingNoteEditsPreserveCustomAppearanceAndOtherAnnotations) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened =
		        pdf::openFile(fixturePath("comment_locations.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto before = doc.render(0, 200, 300);
		    ASSERT_TRUE(before);
		    EXPECT_FALSE(doc.textNote(-1, 0));
		    EXPECT_FALSE(doc.textNote(0, -1));
		    EXPECT_FALSE(doc.textNote(0, 99));
		    ASSERT_EQ(doc.updateTextNote(0, 0, u"Location 1", u"Edited"), true);
		    const auto bytes = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    EXPECT_EQ(saved.document->textNote(0, 0), u"Edited");
		    EXPECT_EQ(saved.document->textNote(0, 1), u"Location 2");
		    const auto after = saved.document->render(0, 200, 300);
		    ASSERT_TRUE(after);
		    EXPECT_EQ(before->pixels, after->pixels);
	    })
	    .get();
}

TEST(Annotations, HighlightsPersistTextQuadsAcrossCropAndPageRotations) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto source = annotationBytes("text.pdf");
		    const auto opened = pdf::openDocument(source);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto before = doc.render(0, 360, 240);
		    ASSERT_TRUE(before);
		    EXPECT_EQ(doc.highlightText({{3, INT_MAX}, {0, 0}}), 4);
		    EXPECT_TRUE(doc.modified());
		    const auto bytes = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    std::unique_ptr<fpdf_document_t__, decltype(&FPDF_CloseDocument)>
		        raw(FPDF_LoadMemDocument64(bytes->data(), bytes->size(),
		                                   nullptr),
		            &FPDF_CloseDocument);
		    ASSERT_TRUE(raw);
		    for (int index = 0; index < 4; ++index) {
			    std::unique_ptr<fpdf_page_t__, decltype(&FPDF_ClosePage)> page(
			        FPDF_LoadPage(raw.get(), index), &FPDF_ClosePage);
			    ASSERT_TRUE(page);
			    EXPECT_EQ(FPDFPage_GetAnnotCount(page.get()), 1);
			    std::unique_ptr<fpdf_annotation_t__,
			                    decltype(&FPDFPage_CloseAnnot)>
			        annot(FPDFPage_GetAnnot(page.get(), 0),
			              &FPDFPage_CloseAnnot);
			    ASSERT_TRUE(annot);
			    EXPECT_EQ(FPDFAnnot_GetSubtype(annot.get()),
			              FPDF_ANNOT_HIGHLIGHT);
			    EXPECT_EQ(FPDFAnnot_GetFlags(annot.get()),
			              FPDF_ANNOT_FLAG_PRINT);
			    ASSERT_EQ(FPDFAnnot_CountAttachmentPoints(annot.get()), 2u);
			    for (std::size_t quad = 0; quad < 2; ++quad) {
				    FS_QUADPOINTSF points{};
				    ASSERT_TRUE(FPDFAnnot_GetAttachmentPoints(annot.get(), quad,
				                                              &points));
				    EXPECT_EQ(points.x1, points.x3);
				    EXPECT_EQ(points.x2, points.x4);
				    EXPECT_EQ(points.y1, points.y2);
				    EXPECT_EQ(points.y3, points.y4);
				    EXPECT_GE(points.x1, 20);
				    EXPECT_LE(points.x2, 380);
				    EXPECT_GE(points.y3, 30);
				    EXPECT_LE(points.y1, 270);
				    EXPECT_LT(points.x1, points.x2);
				    EXPECT_LT(points.y3, points.y1);
			    }
			    for (int rotation = 0; rotation < 4; ++rotation) {
				    const auto a = doc.render(index, 360, 240, rotation);
				    const auto b =
				        saved.document->render(index, 360, 240, rotation);
				    ASSERT_TRUE(a && b);
				    EXPECT_EQ(a->pixels, b->pixels);
			    }
		    }
		    const auto after = saved.document->render(0, 360, 240);
		    ASSERT_TRUE(after);
		    EXPECT_NE(before->pixels, after->pixels);
		    bool yellow = false;
		    for (std::size_t i = 0; i < after->pixels.size(); i += 4) {
			    yellow = yellow ||
			             (after->pixels[i] < 30 && after->pixels[i + 1] > 220 &&
			              after->pixels[i + 2] > 220);
		    }
		    EXPECT_TRUE(yellow);
		    EXPECT_TRUE(saved.document->annotations(4)->entries.empty());
		    EXPECT_EQ(doc.highlightText({{4, 0}, {4, INT_MAX}}), 1);
		    EXPECT_EQ(annotationBytes("text.pdf"), source);
	    })
	    .get();
}

TEST(Annotations, HighlightFailuresPreflightAndRollBackSharedArrays) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bad = sharedHighlightBytes(4096, "0 0 200 300", false);
		    const auto invalid = pdf::openDocument(bad);
		    ASSERT_TRUE(invalid.document);
		    EXPECT_FALSE(invalid.document->highlightText({{0, 0}, {1, 4}}));
		    EXPECT_FALSE(invalid.document->modified());
		    EXPECT_EQ(invalid.document->serialize(1024 * 1024), bad);
		    for (const int count : {0, 1, 4095}) {
			    for (const bool dirty : {false, true}) {
				    if (dirty && count == 0) {
					    continue;
				    }
				    const auto source = sharedHighlightBytes(count);
				    const auto opened = pdf::openDocument(source);
				    ASSERT_TRUE(opened.document);
				    auto& doc = *opened.document;
				    if (dirty) {
					    ASSERT_EQ(
					        doc.updateTextNote(0, 0, u"Existing", u"Changed"),
					        true);
				    }
				    // i undo appends that would also appear on an unselected
				    // page.
				    EXPECT_FALSE(doc.highlightText({{0, 0}, {0, 4}}));
				    EXPECT_FALSE(doc.highlightText({{0, 0}, {1, 4}}));
				    EXPECT_EQ(doc.modified(), dirty);
				    for (int page = 0; page < 2; ++page) {
					    const auto notes = doc.annotations(page);
					    ASSERT_TRUE(notes);
					    ASSERT_EQ(notes->entries.size(),
					              static_cast<std::size_t>(count));
					    if (count > 0) {
						    EXPECT_EQ(notes->entries.back().subtype, "Text");
						    EXPECT_EQ(notes->entries.front().contents,
						              dirty ? "Changed" : "Existing");
					    }
				    }
				    if (!dirty) {
					    EXPECT_EQ(doc.serialize(1024 * 1024), source);
				    }
			    }
		    }
	    })
	    .get();
}

TEST(Annotations,
     HighlightPermissionsAreIndependentOfCopyAndRefuseUnsafeFiles) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto allowed =
		        pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(allowed.document);
		    EXPECT_TRUE(allowed.document->canAnnotate());
		    EXPECT_FALSE(allowed.document->canCopy());
		    EXPECT_EQ(allowed.document->highlightText({{0, 0}, {0, 4}}), 1);
		    const auto bytes = allowed.document->serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    EXPECT_FALSE(saved.document->canCopy());
		    const auto notes = saved.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 1);
		    EXPECT_TRUE(notes->entries[0].contents.empty());
		    for (const auto* file :
		         {"notes_denied.pdf", "restricted.pdf", "annotations.pdf",
		          "note_attachment.pdf"}) {
			    const auto opened = pdf::openFile(fixturePath(file));
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(opened.document->highlightText({{0, 0}, {0, 4}}));
			    EXPECT_FALSE(opened.document->modified());
		    }
		    const auto opened = pdf::openFile(fixturePath("text.pdf"));
		    ASSERT_TRUE(opened.document);
		    for (const auto& selection :
		         std::vector<pdf::TextSelection>{{{-1, 0}, {0, 1}},
		                                         {{0, -1}, {0, 1}},
		                                         {{0, 1}, {9, 1}},
		                                         {{0, 9999}, {1, 1}},
		                                         {{0, 10}, {0, 11}}}) {
			    EXPECT_FALSE(opened.document->highlightText(selection));
			    EXPECT_FALSE(opened.document->modified());
		    }
	    })
	    .get();
}

TEST(Annotations, HighlightsClipGlyphsAndKeepCharacterIndexSemantics) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened =
		        pdf::openDocument(sharedHighlightBytes(0, "0 0 50 300", false));
		    ASSERT_TRUE(opened.document);
		    EXPECT_EQ(opened.document->highlightText({{0, 0}, {1, INT_MAX}}),
		              2);
		    const auto notes = opened.document->annotations(1);
		    ASSERT_TRUE(notes && notes->entries.size() == 1 &&
		                notes->entries[0].bounds);
		    EXPECT_FLOAT_EQ(notes->entries[0].bounds->right, 50);
		    EXPECT_GE(notes->entries[0].bounds->left, 40);
		    const auto unicode = pdf::openFile(fixturePath("text.pdf"));
		    ASSERT_TRUE(unicode.document);
		    const auto text = unicode.document->textPage(4);
		    ASSERT_TRUE(text && text->characters.size() >= 4);
		    EXPECT_EQ(text->characters[1].unicode, 0xD83Du);
		    EXPECT_EQ(text->characters[2].unicode, 0xDE00u);
		    EXPECT_EQ(unicode.document->highlightText({{4, 1}, {4, 2}}), 1);
		    const auto selection = unicode.document->annotations(4);
		    ASSERT_TRUE(selection && selection->entries.size() == 1 &&
		                selection->entries[0].position);
		    const auto& box = text->characters[1].box;
		    EXPECT_GE(selection->entries[0].position->x, box.left);
		    EXPECT_LE(selection->entries[0].position->x, box.right);
	    })
	    .get();
}

TEST(Annotations, NewTextNotesRefuseSharedArraysWithoutSpreadingEdits) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const int count : {0, 1, 4095}) {
			    for (const bool dirty : {false, true}) {
				    if (dirty && count == 0) {
					    continue;
				    }
				    const auto source = sharedHighlightBytes(count);
				    const auto opened = pdf::openDocument(source);
				    ASSERT_TRUE(opened.document);
				    auto& doc = *opened.document;
				    if (dirty) {
					    ASSERT_EQ(
					        doc.updateTextNote(0, 0, u"Existing", u"Changed"),
					        true);
				    }
				    for (int page = 0; page < 2; ++page) {
					    EXPECT_FALSE(doc.addTextNote(page, 40, 50, u"Raw"));
					    EXPECT_FALSE(
					        doc.addCenteredTextNote(page, u"Centered"));
					    EXPECT_FALSE(
					        doc.addTextNoteAt(page, .4, .6, 3, u"Placed"));
					    EXPECT_EQ(doc.modified(), dirty);
					    const auto notes = doc.annotations(page);
					    ASSERT_TRUE(notes &&
					                notes->entries.size() ==
					                    static_cast<std::size_t>(count));
					    if (count > 0) {
						    EXPECT_EQ(notes->entries.back().contents,
						              dirty ? "Changed" : "Existing");
					    }
				    }
				    if (!dirty) {
					    EXPECT_EQ(doc.serialize(1024 * 1024), source);
				    }
			    }
		    }
	    })
	    .get();
}

TEST(Annotations, LineMarkupPersistsQuadsAndStrokesAcrossPageRotations) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto markup :
		         {pdf::TextMarkup::Underline, pdf::TextMarkup::StrikeOut}) {
			    const auto source = annotationBytes("text.pdf");
			    const auto opened = pdf::openDocument(source);
			    const auto plain = pdf::openDocument(source);
			    ASSERT_TRUE(opened.document && plain.document);
			    auto& doc = *opened.document;
			    ASSERT_EQ(doc.markText({{3, INT_MAX}, {0, 0}}, markup), 4);
			    // i save before rendering so reopening has to make its own
			    // strokes.
			    const auto bytes = doc.serialize(1024 * 1024);
			    ASSERT_TRUE(bytes);
			    const auto saved = pdf::openDocument(*bytes);
			    ASSERT_TRUE(saved.document);
			    std::unique_ptr<fpdf_document_t__,
			                    decltype(&FPDF_CloseDocument)>
			        raw(FPDF_LoadMemDocument64(bytes->data(), bytes->size(),
			                                   nullptr),
			            &FPDF_CloseDocument);
			    ASSERT_TRUE(raw);
			    for (int pageIndex = 0; pageIndex < 4; ++pageIndex) {
				    std::unique_ptr<fpdf_page_t__, decltype(&FPDF_ClosePage)>
				        page(FPDF_LoadPage(raw.get(), pageIndex),
				             &FPDF_ClosePage);
				    ASSERT_TRUE(page);
				    ASSERT_EQ(FPDFPage_GetAnnotCount(page.get()), 1);
				    std::unique_ptr<fpdf_annotation_t__,
				                    decltype(&FPDFPage_CloseAnnot)>
				        note(FPDFPage_GetAnnot(page.get(), 0),
				             &FPDFPage_CloseAnnot);
				    ASSERT_TRUE(note);
				    EXPECT_EQ(FPDFAnnot_GetSubtype(note.get()),
				              markup == pdf::TextMarkup::Underline
				                  ? FPDF_ANNOT_UNDERLINE
				                  : FPDF_ANNOT_STRIKEOUT);
				    EXPECT_EQ(FPDFAnnot_GetFlags(note.get()),
				              FPDF_ANNOT_FLAG_PRINT);
				    ASSERT_EQ(FPDFAnnot_CountAttachmentPoints(note.get()), 2u);
				    for (std::size_t index = 0; index < 2; ++index) {
					    FS_QUADPOINTSF quad{};
					    ASSERT_TRUE(FPDFAnnot_GetAttachmentPoints(
					        note.get(), index, &quad));
					    EXPECT_GE(quad.x1, 20);
					    EXPECT_LE(quad.x4, 380);
					    EXPECT_GE(quad.y4, 30);
					    EXPECT_LE(quad.y1, 270);
					    EXPECT_LT(quad.x1, quad.x4);
					    EXPECT_LT(quad.y4, quad.y1);
				    }
				    for (int rotation = 0; rotation < 4; ++rotation) {
					    const auto before = plain.document->render(
					        pageIndex, 720, 480, rotation);
					    const auto a =
					        doc.render(pageIndex, 720, 480, rotation);
					    const auto b = saved.document->render(pageIndex, 720,
					                                          480, rotation);
					    ASSERT_TRUE(before && a && b);
					    EXPECT_NE(before->pixels, b->pixels);
					    EXPECT_EQ(a->pixels, b->pixels);
				    }
			    }
			    const auto withAppearance = doc.serialize(1024 * 1024);
			    ASSERT_TRUE(withAppearance);
			    const auto rendered = pdf::openDocument(*withAppearance);
			    ASSERT_TRUE(rendered.document);
			    const auto final = rendered.document->render(0, 720, 480);
			    const auto current = doc.render(0, 720, 480);
			    ASSERT_TRUE(final && current);
			    EXPECT_EQ(final->pixels, current->pixels);
			    EXPECT_EQ(doc.markText({{4, 1}, {4, 2}}, markup), 1);
			    const auto unicode = doc.annotations(4);
			    ASSERT_TRUE(unicode && unicode->entries.size() == 1);
			    EXPECT_TRUE(unicode->entries[0].contents.empty());
			    EXPECT_EQ(annotationBytes("text.pdf"), source);
		    }
	    })
	    .get();
}

TEST(Annotations, LineMarkupKeepsPermissionsAndRollsBackSharedArrays) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto markup :
		         {pdf::TextMarkup::Underline, pdf::TextMarkup::StrikeOut}) {
			    const auto allowed =
			        pdf::openFile(fixturePath("notes_allowed.pdf"));
			    ASSERT_TRUE(allowed.document);
			    EXPECT_FALSE(allowed.document->canCopy());
			    EXPECT_EQ(allowed.document->markText({{0, 0}, {0, 4}}, markup),
			              1);
			    for (const auto* file :
			         {"notes_denied.pdf", "restricted.pdf", "annotations.pdf",
			          "note_attachment.pdf"}) {
				    const auto opened = pdf::openFile(fixturePath(file));
				    ASSERT_TRUE(opened.document);
				    EXPECT_FALSE(
				        opened.document->markText({{0, 0}, {0, 4}}, markup));
				    EXPECT_FALSE(opened.document->modified());
			    }
			    for (const int count : {0, 1, 4095}) {
				    for (const bool dirty : {false, true}) {
					    if (dirty && count == 0) {
						    continue;
					    }
					    const auto source = sharedHighlightBytes(count);
					    const auto opened = pdf::openDocument(source);
					    ASSERT_TRUE(opened.document);
					    auto& doc = *opened.document;
					    if (dirty) {
						    ASSERT_EQ(doc.updateTextNote(0, 0, u"Existing",
						                                 u"Changed"),
						              true);
					    }
					    EXPECT_FALSE(
					        doc.markText({{0, 0}, {0, INT_MAX}}, markup));
					    EXPECT_FALSE(
					        doc.markText({{1, INT_MAX}, {0, 0}}, markup));
					    EXPECT_EQ(doc.modified(), dirty);
					    for (int page = 0; page < 2; ++page) {
						    const auto notes = doc.annotations(page);
						    ASSERT_TRUE(notes);
						    EXPECT_EQ(notes->entries.size(),
						              static_cast<std::size_t>(count));
						    if (count > 0) {
							    EXPECT_EQ(notes->entries.back().contents,
							              dirty ? "Changed" : "Existing");
						    }
					    }
					    if (!dirty) {
						    EXPECT_EQ(doc.serialize(1024 * 1024), source);
					    }
				    }
			    }
			    const auto clipped = pdf::openDocument(
			        sharedHighlightBytes(0, "0 0 50 300", false));
			    ASSERT_TRUE(clipped.document);
			    ASSERT_EQ(
			        clipped.document->markText({{0, 0}, {1, INT_MAX}}, markup),
			        2);
			    const auto notes = clipped.document->annotations(1);
			    ASSERT_TRUE(notes && notes->entries.size() == 1 &&
			                notes->entries[0].bounds);
			    EXPECT_FLOAT_EQ(notes->entries[0].bounds->right, 50);
		    }
	    })
	    .get();
}

TEST(Annotations, InvalidMarkupLeavesTheDocumentUntouched) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto source = annotationBytes("text.pdf");
		    const auto opened = pdf::openDocument(source);
		    ASSERT_TRUE(opened.document);
		    EXPECT_FALSE(opened.document->markText(
		        {{0, 0}, {0, 4}}, static_cast<pdf::TextMarkup>(-1)));
		    EXPECT_FALSE(opened.document->modified());
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), source);
	    })
	    .get();
}

TEST(Annotations, MarkupCommentsPreserveAppearanceAndExactContents) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (auto type :
		         {pdf::TextMarkup::Highlight, pdf::TextMarkup::Underline,
		          pdf::TextMarkup::StrikeOut}) {
			    const auto created = pdf::openFile(fixturePath("text.pdf"));
			    ASSERT_TRUE(created.document);
			    ASSERT_EQ(created.document->markText({{0, 0}, {0, 4}}, type),
			              1);
			    const auto source = created.document->serialize(1024 * 1024);
			    ASSERT_TRUE(source);
			    const auto opened = pdf::openDocument(*source);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    const auto empty = doc.markupComment(0, 0, type);
			    ASSERT_TRUE(empty);
			    EXPECT_FALSE(empty->present);
			    EXPECT_TRUE(empty->text.empty());
			    EXPECT_FALSE(
			        doc.updateMarkupComment(0, 0, type, {{}, true}, u"Stale"));
			    EXPECT_FALSE(doc.updateMarkupComment(0, 0, type, *empty, {}));
			    EXPECT_FALSE(doc.modified());
			    EXPECT_EQ(doc.serialize(1024 * 1024), source);
			    const auto before = doc.render(0, 720, 480);
			    ASSERT_TRUE(before);
			    const auto inventory = doc.annotations(0);
			    ASSERT_TRUE(inventory && inventory->entries.size() == 1);
			    const std::u16string raw =
			        u"Comment \u03a9\U0001f600\r\nline\tend";
			    ASSERT_EQ(doc.updateMarkupComment(0, 0, type, *empty, raw),
			              true);
			    EXPECT_FALSE(
			        doc.updateMarkupComment(0, 0, type, *empty, u"Stale"));
			    const auto current = doc.markupComment(0, 0, type);
			    ASSERT_TRUE(current);
			    EXPECT_TRUE(current->present);
			    EXPECT_EQ(current->text, raw);
			    EXPECT_EQ(doc.updateMarkupComment(0, 0, type, *current, raw),
			              false);
			    const auto after = doc.render(0, 720, 480);
			    ASSERT_TRUE(after);
			    EXPECT_EQ(before->pixels, after->pixels);
			    const auto bytes = doc.serialize(1024 * 1024);
			    ASSERT_TRUE(bytes);
			    const auto saved = pdf::openDocument(*bytes);
			    ASSERT_TRUE(saved.document);
			    EXPECT_EQ(saved.document->markupComment(0, 0, type), current);
			    const auto rendered = saved.document->render(0, 720, 480);
			    ASSERT_TRUE(rendered);
			    EXPECT_EQ(rendered->pixels, before->pixels);
			    EXPECT_EQ(saved.document->updateMarkupComment(0, 0, type,
			                                                  *current, raw),
			              false);
			    EXPECT_FALSE(saved.document->modified());
			    EXPECT_EQ(saved.document->serialize(1024 * 1024), bytes);
			    const auto final = saved.document->annotations(0);
			    ASSERT_TRUE(final && final->entries.size() == 1 &&
			                final->entries[0].bounds);
			    EXPECT_EQ(final->entries[0].flags, inventory->entries[0].flags);
			    EXPECT_EQ(final->entries[0].subtype,
			              inventory->entries[0].subtype);
			    EXPECT_EQ(final->entries[0].bounds->left,
			              inventory->entries[0].bounds->left);
		    }
	    })
	    .get();
}

TEST(Annotations, MarkupCommentsRefuseProtectedMalformedAndSharedTargets) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto& [type, name] :
		         std::vector<std::pair<pdf::TextMarkup, std::string>>{
		             {pdf::TextMarkup::Highlight, "Highlight"},
		             {pdf::TextMarkup::Underline, "Underline"},
		             {pdf::TextMarkup::StrikeOut, "StrikeOut"}}) {
			    for (const auto& fields : std::vector<std::string>{
			             "/F 64 /Contents (Old)", "/F 512 /Contents (Old)",
			             "/RC (rich) /Contents (Old)", "/Contents 123",
			             "/Contents <FEFFD800>",
			             "/Contents (" + std::string(32768, 'x') + ")"}) {
				    const auto source = editableNoteBytes(fields, name);
				    const auto opened = pdf::openDocument(source);
				    ASSERT_TRUE(opened.document);
				    EXPECT_FALSE(opened.document->markupComment(0, 0, type));
				    EXPECT_FALSE(opened.document->updateMarkupComment(
				        0, 0, type, {u"Old", true}, u"New"));
				    EXPECT_FALSE(opened.document->modified());
				    EXPECT_EQ(opened.document->serialize(1024 * 1024), source);
			    }
			    for (int mode = 0; mode < 3; ++mode) {
				    const auto source =
				        sharedHighlightBytes(mode == 2 ? 2 : 1, "0 0 200 300",
				                             mode == 0, name, mode == 1);
				    const auto opened = pdf::openDocument(source);
				    ASSERT_TRUE(opened.document);
				    for (int page = 0; page < 2; ++page) {
					    EXPECT_FALSE(
					        opened.document->markupComment(page, 0, type));
					    EXPECT_FALSE(opened.document->updateMarkupComment(
					        page, 0, type, {u"Existing", true}, u"New"));
				    }
				    EXPECT_FALSE(opened.document->modified());
				    EXPECT_EQ(opened.document->serialize(1024 * 1024), source);
			    }
			    const auto blank =
			        pdf::openDocument(editableNoteBytes("/Contents ()", name));
			    ASSERT_TRUE(blank.document);
			    const auto empty = blank.document->markupComment(0, 0, type);
			    ASSERT_TRUE(empty);
			    EXPECT_TRUE(empty->present && empty->text.empty());
			    EXPECT_FALSE(blank.document->updateMarkupComment(
			        0, 0, type, {{}, false}, u"Stale"));
			    EXPECT_EQ(blank.document->updateMarkupComment(0, 0, type,
			                                                  *empty, u"Added"),
			              true);
		    }
	    })
	    .get();
}

TEST(Annotations, MarkupCommentPermissionsAndTypeAreIndependentOfCopy) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto allowed =
		        pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(allowed.document);
		    ASSERT_EQ(allowed.document->markText({{0, 0}, {0, 4}},
		                                         pdf::TextMarkup::Highlight),
		              1);
		    EXPECT_FALSE(allowed.document->canCopy());
		    const auto snapshot = allowed.document->markupComment(
		        0, 0, pdf::TextMarkup::Highlight);
		    ASSERT_TRUE(snapshot);
		    EXPECT_EQ(
		        allowed.document->updateMarkupComment(
		            0, 0, pdf::TextMarkup::Highlight, *snapshot, u"Allowed"),
		        true);
		    EXPECT_FALSE(allowed.document->markupComment(
		        0, 0, pdf::TextMarkup::Underline));
		    EXPECT_FALSE(allowed.document->markupComment(
		        0, 0, static_cast<pdf::TextMarkup>(-1)));
		    const auto denied = pdf::openFile(fixturePath("notes_denied.pdf"));
		    ASSERT_TRUE(denied.document);
		    EXPECT_FALSE(denied.document->markupComment(
		        0, 0, pdf::TextMarkup::Highlight));
	    })
	    .get();
}
