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
#include <fstream>
#include <limits>
#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf_test_files.h"

namespace {
std::vector<std::uint8_t> annotationBytes(const char* name) {
	std::ifstream file(fixturePath(name), std::ios::binary);
	return {std::istreambuf_iterator<char>(file), {}};
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
