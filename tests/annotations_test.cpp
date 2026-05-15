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
#include <fpdf_edit.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <utility>
#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf/text_box_appearance.h"
#include "pdf_test_files.h"

namespace pdf {
struct DocumentEditTest {
	static const std::vector<std::uint8_t>& rawBytes(const Document& document) {
		return document.m_data;
	}
	static auto multiline(const Document& document, const FS_RECTF& rect,
	                      std::u16string_view text) {
		return multilineBoxAppearance(document.m_handle, rect, text);
	}
	static void checkMultiline(Document& document, const FS_RECTF& rect,
	                           const MultilineBoxAppearance& model,
	                           bool install) {
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		ASSERT_TRUE(page);
		const auto note = FPDFPage_GetAnnot(page, 0);
		EXPECT_TRUE(note);
		if (install) {
			EXPECT_TRUE(FPDFAnnot_SetAP(
			    note, 0,
			    reinterpret_cast<FPDF_WIDESTRING>(model.stream.c_str())));
		}
		EXPECT_TRUE(matchesMultilineBoxAppearance(note, rect, model));
		for (int i = 0; i < FPDFAnnot_GetObjectCount(note); ++i) {
			float x, y, r, t;
			if (FPDFPageObj_GetBounds(FPDFAnnot_GetObject(note, i), &x, &y, &r,
			                          &t)) {
				EXPECT_GE(x, rect.left) << i;
				EXPECT_GE(y, rect.bottom) << i;
				EXPECT_LE(r, rect.right) << i;
				EXPECT_LE(t, rect.top) << i;
			}
		}
		float first = 0, last = 0, left = 0, bottom = 0, right = 0;
		EXPECT_TRUE(FPDFPageObj_GetBounds(FPDFAnnot_GetObject(note, 1), &left,
		                                  &bottom, &right, &first));
		EXPECT_TRUE(FPDFPageObj_GetBounds(FPDFAnnot_GetObject(note, 2), &left,
		                                  &bottom, &right, &last));
		EXPECT_NEAR(first - last, 30.f, .01f);
		FPDFPage_CloseAnnot(note);
		FPDF_ClosePage(page);
	}
	static void replaceAppearance(Document& document, int index) {
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		const auto note = FPDFPage_GetAnnot(page, index);
		ASSERT_TRUE(FPDFAnnot_SetAP(note, 0,
		                            reinterpret_cast<FPDF_WIDESTRING>(
		                                u"q 1 0 0 rg 40 100 200 60 re f Q")));
		FPDFPage_CloseAnnot(note);
		FPDF_ClosePage(page);
		document.m_modified = true;
	}
	static bool freeText(Document& document, const FS_RECTF& rect,
	                     std::u16string_view contents) {
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		const auto note = FPDFPage_CreateAnnot(page, FPDF_ANNOT_FREETEXT);
		const std::u16string text(contents);
		const bool result =
		    note && FPDFAnnot_SetRect(note, &rect) &&
		    FPDFAnnot_SetStringValue(
		        note, "Contents",
		        reinterpret_cast<FPDF_WIDESTRING>(text.c_str())) &&
		    FPDFAnnot_SetStringValue(
		        note, "DA",
		        reinterpret_cast<FPDF_WIDESTRING>(u"/Helv 12 Tf 0 g")) &&
		    FPDFAnnot_SetFlags(note, FPDF_ANNOT_FLAG_PRINT);
		FPDFPage_CloseAnnot(note);
		FPDF_ClosePage(page);
		document.m_modified = true;
		return result;
	}
	static bool square(Document& document, const FS_RECTF& rect,
	                   const char16_t* appearance) {
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		if (!page) {
			return false;
		}
		const auto note = FPDFPage_CreateAnnot(page, FPDF_ANNOT_SQUARE);
		const bool result =
		    note && FPDFAnnot_SetRect(note, &rect) &&
		    FPDFAnnot_SetFlags(note, FPDF_ANNOT_FLAG_PRINT) &&
		    FPDFAnnot_SetAP(note, FPDF_ANNOT_APPEARANCEMODE_NORMAL,
		                    reinterpret_cast<FPDF_WIDESTRING>(appearance));
		if (note) {
			FPDFPage_CloseAnnot(note);
		}
		FPDF_ClosePage(page);
		document.m_modified = result;
		return result;
	}
	static std::u16string appearance(const Document& document, bool& fits,
	                                 int& count, int index = 0) {
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		const auto note = FPDFPage_GetAnnot(page, index);
		FS_RECTF rect{};
		fits = FPDFAnnot_GetRect(note, &rect) != 0;
		std::vector<FPDF_WCHAR> ap(FPDFAnnot_GetAP(note, 0, nullptr, 0) / 2);
		if (ap.empty() ||
		    FPDFAnnot_GetAP(note, 0, ap.data(),
		                    static_cast<unsigned long>(ap.size() * 2)) !=
		        ap.size() * 2) {
			fits = false;
		}
		count = FPDFAnnot_GetObjectCount(note);
		for (int i = 0; i < count; ++i) {
			const auto object = FPDFAnnot_GetObject(note, i);
			if (FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_TEXT) {
				continue;
			}
			float left, bottom, right, top;
			fits =
			    fits &&
			    FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top) &&
			    left >= rect.left && bottom >= rect.bottom &&
			    right <= rect.right && top <= rect.top;
		}
		std::u16string result(ap.begin(), ap.empty() ? ap.end() : ap.end() - 1);
		FPDFPage_CloseAnnot(note);
		FPDF_ClosePage(page);
		return result;
	}
	static std::vector<InkStroke> inkList(const Document& document, int index) {
		std::vector<InkStroke> result;
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		const auto note = page ? FPDFPage_GetAnnot(page, index) : nullptr;
		const auto paths = note ? FPDFAnnot_GetInkListCount(note) : 0;
		for (unsigned long i = 0; i < paths; ++i) {
			std::vector<FS_POINTF> points(
			    FPDFAnnot_GetInkListPath(note, i, nullptr, 0));
			FPDFAnnot_GetInkListPath(note, i, points.data(),
			                         static_cast<unsigned long>(points.size()));
			auto& stroke = result.emplace_back();
			for (const auto& point : points) {
				stroke.push_back({point.x, point.y});
			}
		}
		if (note) {
			FPDFPage_CloseAnnot(note);
		}
		if (page) {
			FPDF_ClosePage(page);
		}
		return result;
	}
	static std::size_t pageLoads(const Document& document) {
		return document.m_editPageLoads;
	}
	static bool preflight(const Document& document,
	                      const std::function<bool()>& canceled) {
		return document.canSerializeEdits(canceled);
	}
};
} // namespace pdf

TEST(Annotations, FreeTextAppearanceSurvivesSavedReload) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(pdf::DocumentEditTest::freeText(
		        doc, {40, 160, 240, 100}, u"Plain text box"));
		    ASSERT_TRUE(doc.render(0, 400, 400));
		    bool fits = false;
		    int count = 0;
		    EXPECT_NE(pdf::DocumentEditTest::appearance(doc, fits, count)
		                  .find(u"(Plain text box) Tj"),
		              std::u16string::npos);
		    EXPECT_TRUE(fits);
		    EXPECT_GT(count, 0);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    EXPECT_NE(
		        pdf::DocumentEditTest::appearance(*saved.document, fits, count)
		            .find(u"(Plain text box) Tj"),
		        std::u16string::npos);
		    EXPECT_TRUE(fits);
		    EXPECT_GT(count, 0);
	    })
	    .get();
}

TEST(Annotations, FreeTextAppearanceExposesOverflowAndCollapsedParagraphs) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const bool narrow : {false, true}) {
			    const auto opened =
			        pdf::openFile(fixturePath("notes_allowed.pdf"));
			    ASSERT_TRUE(opened.document);
			    ASSERT_TRUE(pdf::DocumentEditTest::freeText(
			        *opened.document, {40, 160, narrow ? 60.f : 240.f, 100},
			        narrow ? u"WWWWWWWWWWWW" : u"first\nsecond"));
			    ASSERT_TRUE(opened.document->render(0, 400, 400));
			    bool fits = false;
			    int count = 0;
			    const auto ap = pdf::DocumentEditTest::appearance(
			        *opened.document, fits, count);
			    if (narrow) {
				    EXPECT_FALSE(fits);
			    } else {
				    EXPECT_EQ(ap.find(u"first\nsecond"), std::u16string::npos);
				    EXPECT_NE(ap.find(u"(firstsecond) Tj"),
				              std::u16string::npos);
			    }
		    }
	    })
	    .get();
}

namespace {
std::vector<std::uint8_t> annotationBytes(const char* name) {
	std::ifstream file(fixturePath(name), std::ios::binary);
	return {std::istreambuf_iterator<char>(file), {}};
}
std::vector<std::uint8_t> editableNoteBytes(
    const std::string& fields, std::string_view type = "Text",
    std::string catalog = {}, std::string pageBox = "/MediaBox [0 0 200 300]",
    std::string rect = "[40 50 60 70]", std::string appearance = {}) {
	std::string data = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	std::vector<std::string> bodies{
	    "<< /Type /Catalog /Pages 2 0 R " + catalog + " >>",
	    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
	    "<< /Type /Page /Parent 2 0 R " + pageBox + " /Annots [4 0 R] >>",
	    "<< /Type /Annot /Subtype /" + std::string(type) + " /Rect " + rect +
	        " " + fields + " >>"};
	if (!appearance.empty()) {
		bodies.push_back(std::move(appearance));
	}
	for (const auto& body : bodies) {
		offsets.push_back(data.size());
		data +=
		    std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
	}
	const auto xref = data.size();
	const auto size = std::to_string(offsets.size() + 1);
	data += "xref\n0 " + size + "\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		data += line;
	}
	data += "trailer\n<< /Size " + size + " /Root 1 0 R >>\nstartxref\n" +
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
std::vector<std::uint8_t>
sharedShapeAppearanceBytes(const std::string& stream) {
	std::string data = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	for (const auto& body : std::vector<std::string>{
	         "<< /Type /Catalog /Pages 2 0 R >>",
	         "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
	         "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] "
	         "/Annots [4 0 R 6 0 R] >>",
	         "<< /Type /Annot /Subtype /Square /Rect [40 100 120 160] "
	         "/F 4 /AP 7 0 R >>",
	         "<< /Type /XObject /Subtype /Form /BBox [40 100 120 160] "
	         "/Length " +
	             std::to_string(stream.size()) + " >>\nstream\n" + stream +
	             "\nendstream",
	         "<< /Type /Annot /Subtype /Square /Rect [40 100 120 160] "
	         "/F 4 /AP 7 0 R >>",
	         "<< /N 5 0 R >>"}) {
		offsets.push_back(data.size());
		data +=
		    std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
	}
	const auto xref = data.size();
	data += "xref\n0 8\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		data += line;
	}
	data += "trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n" +
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
		    EXPECT_EQ(note.flags, FPDF_ANNOT_FLAG_NOVIEW);
		    ASSERT_TRUE(note.bounds);
		    EXPECT_FLOAT_EQ(note.bounds->left, 40);
		    EXPECT_FLOAT_EQ(note.bounds->bottom, 50);
		    EXPECT_FLOAT_EQ(note.bounds->right, 60);
		    EXPECT_FLOAT_EQ(note.bounds->top, 70);
		    const auto after = document.render(1, 360, 280);
		    ASSERT_TRUE(after);
		    EXPECT_EQ(before->pixels, after->pixels);
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
		    EXPECT_EQ(note.flags, FPDF_ANNOT_FLAG_NOVIEW);
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
		    EXPECT_EQ(note.flags, 160);
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
			    yellow = yellow || (after->pixels[i] < 130 &&
			                        after->pixels[i + 1] > 220 &&
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

TEST(Annotations, RepeatedEditsOnlyRecheckTheirOwnPages) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("text.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_EQ(doc.addCenteredTextNote(0, u"First"), 0);
		    const auto before = pdf::DocumentEditTest::pageLoads(doc);
		    ASSERT_EQ(doc.addCenteredTextNote(0, u"Second"), 1);
		    EXPECT_EQ(pdf::DocumentEditTest::pageLoads(doc) - before, 1u);
		    const auto notes = pdf::DocumentEditTest::pageLoads(doc);
		    ASSERT_EQ(doc.updateTextNote(0, 0, u"First", u"Changed"), true);
		    EXPECT_EQ(pdf::DocumentEditTest::pageLoads(doc), notes);
		    for (const auto type :
		         {pdf::TextMarkup::Highlight, pdf::TextMarkup::Underline,
		          pdf::TextMarkup::StrikeOut}) {
			    const auto start = pdf::DocumentEditTest::pageLoads(doc);
			    ASSERT_EQ(doc.markText({{0, 0}, {0, 4}}, type), 1);
			    EXPECT_EQ(pdf::DocumentEditTest::pageLoads(doc) - start, 1u);
		    }
		    const auto bytes = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    for (int index = 2; index < 5; ++index) {
			    const auto type = static_cast<pdf::TextMarkup>(index - 2);
			    auto contents = saved.document->markupComment(0, index, type);
			    ASSERT_TRUE(contents);
			    const auto first =
			        pdf::DocumentEditTest::pageLoads(*saved.document);
			    for (int edit = 0; edit < 8; ++edit) {
				    const auto replacement = edit % 2 ? u"Second" : u"First";
				    ASSERT_EQ(saved.document->updateMarkupComment(
				                  0, index, type, *contents, replacement),
				              true);
				    contents = saved.document->markupComment(0, index, type);
				    ASSERT_TRUE(contents);
				    EXPECT_EQ(contents->text, replacement);
			    }
			    EXPECT_EQ(pdf::DocumentEditTest::pageLoads(*saved.document),
			              first);
		    }
	    })
	    .get();
}

TEST(Annotations, CanceledPreflightDoesNotCacheARefusal) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("text.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    int calls = 0;
		    EXPECT_FALSE(pdf::DocumentEditTest::preflight(
		        doc, [&] { return ++calls == 3; }));
		    EXPECT_GT(pdf::DocumentEditTest::pageLoads(doc), 0u);
		    ASSERT_EQ(doc.addTextNote(0, 40, 40, u"Note"), 0);
		    EXPECT_TRUE(doc.serialize(1024 * 1024));
		    EXPECT_EQ(doc.updateTextNote(0, 0, u"Note", u"Still editable"),
		              true);
	    })
	    .get();
}

TEST(Annotations, SavedMarkupHasNormalAppearancesBeforeAnyPageIsRendered) {
	const auto directory = makeTempDir("markup_appearance");
	const auto cleanup = std::shared_ptr<void>(nullptr, [directory](void*) {
		std::error_code ec;
		std::filesystem::remove_all(directory, ec);
	});
	pdf::Engine engine;
	engine
	    .run([&] {
		    for (const auto type :
		         {pdf::TextMarkup::Highlight, pdf::TextMarkup::Underline,
		          pdf::TextMarkup::StrikeOut}) {
			    const auto source = fixturePath("text.pdf");
			    const auto opened = pdf::openFile(source);
			    ASSERT_TRUE(opened.document);
			    ASSERT_EQ(
			        opened.document->markText({{0, 0}, {3, INT_MAX}}, type), 4);
			    const auto target =
			        directory /
			        (std::to_string(static_cast<int>(type)) + ".pdf");
			    ASSERT_EQ(
			        opened.document->saveEdits(target, source, 1024 * 1024),
			        pdf::SaveResult::Saved);
			    std::ifstream file(target, std::ios::binary);
			    const std::vector<std::uint8_t> bytes{
			        std::istreambuf_iterator<char>(file), {}};
			    ASSERT_FALSE(bytes.empty());
			    std::unique_ptr<fpdf_document_t__,
			                    decltype(&FPDF_CloseDocument)>
			        raw(FPDF_LoadMemDocument64(bytes.data(), bytes.size(),
			                                   nullptr),
			            &FPDF_CloseDocument);
			    ASSERT_TRUE(raw);
			    for (int index = 0; index < 4; ++index) {
				    std::unique_ptr<fpdf_page_t__, decltype(&FPDF_ClosePage)>
				        page(FPDF_LoadPage(raw.get(), index), &FPDF_ClosePage);
				    ASSERT_TRUE(page);
				    ASSERT_EQ(FPDFPage_GetAnnotCount(page.get()), 1);
				    std::unique_ptr<fpdf_annotation_t__,
				                    decltype(&FPDFPage_CloseAnnot)>
				        note(FPDFPage_GetAnnot(page.get(), 0),
				             &FPDFPage_CloseAnnot);
				    ASSERT_TRUE(note);
				    EXPECT_EQ(FPDFAnnot_GetValueType(note.get(), "AP"),
				              FPDF_OBJECT_DICTIONARY);
				    EXPECT_GT(FPDFAnnot_GetAP(note.get(),
				                              FPDF_ANNOT_APPEARANCEMODE_NORMAL,
				                              nullptr, 0),
				              2u);
			    }
		    }
	    })
	    .get();
}

TEST(Annotations, MixedFontMarkupUsesTheSelectionBandsAfterSaving) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto type :
		         {pdf::TextMarkup::Highlight, pdf::TextMarkup::Underline,
		          pdf::TextMarkup::StrikeOut}) {
			    const auto opened =
			        pdf::openFile(fixturePath("text_bands.pdf"));
			    ASSERT_TRUE(opened.document);
			    std::vector<std::vector<pdf::TextRect>> bands;
			    for (int index = 0; index < 4; ++index) {
				    const auto text = opened.document->textPage(index);
				    ASSERT_TRUE(text);
				    bands.push_back(pdf::textRects(
				        *text, 0, static_cast<int>(text->characters.size())));
			    }
			    ASSERT_EQ(
			        opened.document->markText({{0, 0}, {3, INT_MAX}}, type), 4);
			    const auto bytes = opened.document->serialize(1024 * 1024);
			    ASSERT_TRUE(bytes);
			    std::unique_ptr<fpdf_document_t__,
			                    decltype(&FPDF_CloseDocument)>
			        raw(FPDF_LoadMemDocument64(bytes->data(), bytes->size(),
			                                   nullptr),
			            &FPDF_CloseDocument);
			    ASSERT_TRUE(raw);
			    for (int index = 0; index < 4; ++index) {
				    std::unique_ptr<fpdf_page_t__, decltype(&FPDF_ClosePage)>
				        page(FPDF_LoadPage(raw.get(), index), &FPDF_ClosePage);
				    ASSERT_TRUE(page);
				    std::unique_ptr<fpdf_annotation_t__,
				                    decltype(&FPDFPage_CloseAnnot)>
				        note(FPDFPage_GetAnnot(page.get(), 0),
				             &FPDFPage_CloseAnnot);
				    ASSERT_TRUE(note);
				    ASSERT_EQ(FPDFAnnot_CountAttachmentPoints(note.get()),
				              bands[index].size());
				    EXPECT_EQ(FPDFAnnot_GetValueType(note.get(), "AP"),
				              FPDF_OBJECT_DICTIONARY);
				    EXPECT_GT(FPDFAnnot_GetAP(note.get(),
				                              FPDF_ANNOT_APPEARANCEMODE_NORMAL,
				                              nullptr, 0),
				              2u);
				    for (std::size_t n = 0; n < bands[index].size(); ++n) {
					    FS_QUADPOINTSF q{};
					    ASSERT_TRUE(
					        FPDFAnnot_GetAttachmentPoints(note.get(), n, &q));
					    const std::pair<float, float> points[] = {{q.x1, q.y1},
					                                              {q.x2, q.y2},
					                                              {q.x3, q.y3},
					                                              {q.x4, q.y4}};
					    constexpr int grid = 1 << 20;
					    pdf::TextRect actual{1, 1, 0, 0};
					    for (const auto& [x, y] : points) {
						    int dx = 0, dy = 0;
						    ASSERT_TRUE(FPDF_PageToDevice(page.get(), 0, 0,
						                                  grid, grid, 0, x, y,
						                                  &dx, &dy));
						    actual.left =
						        (std::min)(actual.left, dx / double(grid));
						    actual.right =
						        (std::max)(actual.right, dx / double(grid));
						    actual.top =
						        (std::min)(actual.top, dy / double(grid));
						    actual.bottom =
						        (std::max)(actual.bottom, dy / double(grid));
					    }
					    const auto& selected = bands[index][n];
					    EXPECT_NEAR(actual.left, selected.left, .00001);
					    EXPECT_NEAR(actual.right, selected.right, .00001);
					    EXPECT_NEAR(actual.top, selected.top, .00001);
					    EXPECT_NEAR(actual.bottom, selected.bottom, .00001);
					    // the quad keeps its top above its baseline in PDF
					    // space.
					    EXPECT_GT((q.x2 - q.x1) * (q.y1 - q.y3) -
					                  (q.y2 - q.y1) * (q.x1 - q.x3),
					              0);
				    }
			    }
		    }
	    })
	    .get();
}

TEST(AnnotationTargets, DisplayQuadsFollowIntrinsicCropAndRotation) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("text_bands.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    for (int page = 0; page < doc.pageCount(); ++page) {
			    ASSERT_EQ(doc.markText({{page, 0}, {page, 10}},
			                           pdf::TextMarkup::Highlight),
			              1);
			    const auto notes = doc.annotations(page);
			    ASSERT_TRUE(notes && notes->entries.size() == 1);
			    const auto& note = notes->entries[0];
			    ASSERT_FALSE(note.quads.empty());
			    ASSERT_EQ(note.displayQuads.size(), note.quads.size());
			    ASSERT_TRUE(note.displayBounds);
			    const auto& bounds = *note.displayBounds;
			    EXPECT_LT(bounds.left, bounds.right);
			    EXPECT_LT(bounds.top, bounds.bottom);
			    const auto text = doc.textPage(page);
			    ASSERT_TRUE(text && !text->characters.empty());
			    const auto& first = text->characters[0].box;
			    const double cx = (first.left + first.right) / 2;
			    const double cy = (first.top + first.bottom) / 2;
			    EXPECT_LE(bounds.left, cx);
			    EXPECT_GE(bounds.right, cx);
			    EXPECT_LE(bounds.top, cy);
			    EXPECT_GE(bounds.bottom, cy);
			    for (const auto& quad : note.displayQuads) {
				    for (const auto x : {quad.x1, quad.x2, quad.x3, quad.x4}) {
					    EXPECT_GE(x, bounds.left - .00001);
					    EXPECT_LE(x, bounds.right + .00001);
				    }
				    for (const auto y : {quad.y1, quad.y2, quad.y3, quad.y4}) {
					    EXPECT_GE(y, bounds.top - .00001);
					    EXPECT_LE(y, bounds.bottom + .00001);
				    }
			    }
		    }
	    })
	    .get();
}

namespace {
std::vector<std::uint8_t> deletionBook(int notes) {
	constexpr int pages = 200;
	const int firstNote = pages + 3;
	const int linkedNote = firstNote + notes;
	std::string kids = "[", noteRefs = "[";
	for (int page = 0; page < pages; ++page) {
		kids += std::to_string(page + 3) + " 0 R ";
	}
	for (int note = 0; note < notes; ++note) {
		noteRefs += std::to_string(firstNote + note) + " 0 R ";
	}
	kids += "]";
	noteRefs += "]";
	std::vector<std::string> objects{"<< /Type /Catalog /Pages 2 0 R >>",
	                                 "<< /Type /Pages /Count 200 /Kids " +
	                                     kids + " >>"};
	for (int page = 0; page < pages; ++page) {
		const auto refs = page == 0 ? noteRefs
		                  : page == pages - 1
		                      ? "[" + std::to_string(linkedNote) + " 0 R " +
		                            std::to_string(linkedNote + 1) + " 0 R]"
		                      : "[]";
		objects.push_back("<< /Type /Page /Parent 2 0 R "
		                  "/MediaBox [0 0 200 300] /Annots " +
		                  refs + " >>");
	}
	for (int note = 0; note < notes; ++note) {
		objects.push_back("<< /Type /Annot /Subtype /Text /F 32 "
		                  "/Rect [40 50 60 70] /Contents (Imported) >>");
	}
	objects.push_back("<< /Type /Annot /Subtype /Text /Rect [40 50 60 70] "
	                  "/Contents (Keep thread) /Popup " +
	                  std::to_string(linkedNote + 1) + " 0 R >>");
	objects.push_back("<< /Type /Annot /Subtype /Popup /Rect [40 50 60 70] "
	                  "/Contents (Keep popup) /Parent " +
	                  std::to_string(linkedNote) + " 0 R >>");
	std::string data = "%PDF-1.7\n";
	std::vector<std::size_t> offsets{0};
	for (const auto& body : objects) {
		offsets.push_back(data.size());
		data += std::to_string(offsets.size() - 1) + " 0 obj\n" + body +
		        "\nendobj\n";
	}
	const auto xref = data.size();
	data +=
	    "xref\n0 " + std::to_string(offsets.size()) + "\n0000000000 65535 f \n";
	for (std::size_t index = 1; index < offsets.size(); ++index) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offsets[index]);
		data += line;
	}
	data += "trailer\n<< /Size " + std::to_string(offsets.size()) +
	        " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
	return {data.begin(), data.end()};
}
} // namespace

TEST(AnnotationTargets, DeletionProofsSurviveIndexShiftsWithoutBookScans) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const bool imported : {false, true}) {
			    const auto opened =
			        pdf::openDocument(deletionBook(imported ? 12 : 0));
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    if (!imported) {
				    for (int note = 0; note < 12; ++note) {
					    ASSERT_EQ(doc.addTextNote(0, 40, 50, u"Created"), note);
				    }
			    }
			    for (int removed = 0; removed < 12; ++removed) {
				    const auto notes = doc.annotations(0);
				    ASSERT_TRUE(notes);
				    ASSERT_EQ(notes->entries.size(),
				              static_cast<std::size_t>(12 - removed));
				    const auto target = removed % 2
				                            ? notes->entries.back().target
				                            : notes->entries.front().target;
				    const auto before = pdf::DocumentEditTest::pageLoads(doc);
				    ASSERT_EQ(doc.removeAnnotation(target), true);
				    const auto loads =
				        pdf::DocumentEditTest::pageLoads(doc) - before;
				    if (!removed) {
					    EXPECT_LE(loads, imported ? 401u : 201u);
				    } else {
					    // only the unrelated thread's page needs another look.
					    EXPECT_EQ(loads, 1u);
				    }
			    }
			    const auto before = pdf::DocumentEditTest::pageLoads(doc);
			    ASSERT_EQ(doc.addTextNote(0, 80, 100, u"After removal"), 0);
			    EXPECT_LE(pdf::DocumentEditTest::pageLoads(doc) - before, 1u);
			    const auto saved = doc.serialize(1024 * 1024);
			    ASSERT_TRUE(saved);
			    const auto reopened = pdf::openDocument(*saved);
			    ASSERT_TRUE(reopened.document);
			    const auto kept = reopened.document->annotations(199);
			    ASSERT_TRUE(kept && kept->entries.size() == 2);
			    EXPECT_EQ(kept->entries[0].contents, "Keep thread");
			    EXPECT_EQ(kept->entries[1].contents, "Keep popup");
		    }
	    })
	    .get();
}

TEST(Annotations, PdfiumSquareAppearanceSurvivesSaveAndPrint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(pdf::DocumentEditTest::square(
		        doc, {300, 160, 460, 100},
		        u"0 g 300 100 160 60 re 302 102 156 56 re f*"));
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto notes = saved.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 1);
		    EXPECT_EQ(notes->entries[0].subtype, "Square");
		    EXPECT_EQ(notes->entries[0].flags, FPDF_ANNOT_FLAG_PRINT);
		    const auto screen = saved.document->render(0, 595, 842);
		    const auto print =
		        saved.document->printRaster(0, 595, 842, 0, 0, 595, 842);
		    ASSERT_TRUE(screen && print);
		    for (const auto* image : {&*screen, &*print}) {
			    const auto blue = [&](int x, int y) {
				    return image->pixels[(841 - y) * image->stride + 4 * x];
			    };
			    EXPECT_EQ(blue(301, 130), 0);
			    EXPECT_EQ(blue(380, 158), 0);
			    EXPECT_EQ(blue(458, 130), 0);
			    EXPECT_EQ(blue(380, 101), 0);
			    EXPECT_EQ(blue(380, 130), 255);
			    EXPECT_EQ(blue(298, 130), 255);
		    }
	    })
	    .get();
}

TEST(Annotations, RectangleWritesKeepNeighborsAndRejectUnsafeTargets) {
	pdf::Engine engine;
	engine
	    .run([] {
		    constexpr pdf::AnnotationBounds bounds{300, 100, 360, 160};
		    const auto original = annotationBytes("outline.pdf");
		    const auto seed = pdf::openDocument(original);
		    ASSERT_TRUE(seed.document);
		    const auto note =
		        seed.document->addTextNote(0, 30, 40, u"Keep note");
		    ASSERT_TRUE(note);
		    const auto before = seed.document->serialize(1024 * 1024);
		    ASSERT_TRUE(before);
		    const auto opened = pdf::openDocument(*before);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.outline());
		    const auto link = doc.outlineLink(0);
		    ASSERT_TRUE(link);
		    const auto revision = doc.annotationRevision();
		    EXPECT_FALSE(doc.addRectangle(0, bounds, revision + 1));
		    EXPECT_FALSE(
		        doc.addRectangle(0, bounds, revision, [] { return true; }));
		    EXPECT_FALSE(doc.addRectangle(0, {300, 100, 304, 160}, revision));
		    EXPECT_FALSE(doc.addRectangle(0, {-1, 100, 360, 160}, revision));
		    EXPECT_EQ(doc.serialize(1024 * 1024), before);
		    EXPECT_EQ(doc.annotationRevision(), revision);
		    const auto added = doc.addRectangle(0, bounds, revision);
		    ASSERT_TRUE(added);
		    EXPECT_EQ(doc.annotationRevision(), revision + 1);
		    EXPECT_EQ(doc.textNote(0, *note, revision + 1), u"Keep note");
		    const auto kept = doc.outlineLink(0);
		    ASSERT_TRUE(kept);
		    EXPECT_EQ(kept->uri, link->uri);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto notes = saved.document->annotations(0);
		    ASSERT_TRUE(notes &&
		                *added < static_cast<int>(notes->entries.size()));
		    const auto& rectangle = notes->entries[*added];
		    ASSERT_TRUE(rectangle.bounds);
		    EXPECT_EQ(rectangle.subtype, "Square");
		    EXPECT_EQ(rectangle.flags, FPDF_ANNOT_FLAG_PRINT);
		    EXPECT_FLOAT_EQ(rectangle.bounds->left, bounds.left);
		    EXPECT_FLOAT_EQ(rectangle.bounds->top, bounds.top);
		    EXPECT_EQ(saved.document->textNote(0, *note), u"Keep note");
		    EXPECT_EQ(annotationBytes("outline.pdf"), original);

		    for (const auto& source :
		         {annotationBytes("notes_denied.pdf"),
		          editableNoteBytes("/Contents (Keep) /FT /Sig"),
		          sharedHighlightBytes(2)}) {
			    const auto refused = pdf::openDocument(source);
			    ASSERT_TRUE(refused.document);
			    EXPECT_FALSE(refused.document->addRectangle(0, bounds, 1));
			    EXPECT_EQ(refused.document->annotationRevision(), 1u);
			    EXPECT_EQ(refused.document->serialize(1024 * 1024), source);
		    }
	    })
	    .get();
}

TEST(Annotations, EllipseWritesKeepNeighborsAndSavedScreenAndPrintPixels) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("outline.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.outline());
		    const auto link = doc.outlineLink(0);
		    ASSERT_TRUE(link);
		    ASSERT_EQ(doc.addTextNote(0, 30, 40, u"Keep note"), 0);
		    const auto revision = doc.annotationRevision();
		    ASSERT_EQ(doc.addRectangle(0, {40, 100, 120, 160}, revision), 1);
		    ASSERT_EQ(doc.addEllipse(0, {200, 100, 360, 160}, revision + 1), 2);
		    EXPECT_EQ(doc.annotationRevision(), revision + 2);
		    EXPECT_EQ(doc.textNote(0, 0, doc.annotationRevision()),
		              u"Keep note");
		    const auto kept = doc.outlineLink(0);
		    ASSERT_TRUE(kept);
		    EXPECT_EQ(kept->uri, link->uri);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto notes = saved.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 3);
		    EXPECT_EQ(notes->entries[1].subtype, "Square");
		    const auto& ellipse = notes->entries[2];
		    ASSERT_TRUE(ellipse.bounds);
		    EXPECT_EQ(ellipse.subtype, "Circle");
		    EXPECT_EQ(ellipse.flags, FPDF_ANNOT_FLAG_PRINT);
		    EXPECT_FLOAT_EQ(ellipse.bounds->left, 200);
		    EXPECT_FLOAT_EQ(ellipse.bounds->bottom, 100);
		    EXPECT_FLOAT_EQ(ellipse.bounds->right, 360);
		    EXPECT_FLOAT_EQ(ellipse.bounds->top, 160);
		    const auto screen = saved.document->render(0, 360, 240);
		    const auto print =
		        saved.document->printRaster(0, 360, 240, 0, 0, 360, 240);
		    ASSERT_TRUE(screen && print);
		    for (const auto* image : {&*screen, &*print}) {
			    const auto blue = [&](int x, int y) {
				    return image
				        ->pixels[(269 - y) * image->stride + 4 * (x - 20)];
			    };
			    EXPECT_EQ(blue(201, 130), 0);
			    EXPECT_EQ(blue(280, 158), 0);
			    EXPECT_EQ(blue(358, 130), 0);
			    EXPECT_EQ(blue(280, 101), 0);
			    EXPECT_EQ(blue(280, 130), 255);
			    EXPECT_EQ(blue(201, 158), 255);
			    EXPECT_EQ(blue(358, 101), 255);
			    EXPECT_EQ(blue(198, 130), 255);
			    EXPECT_EQ(blue(41, 130), 0);
		    }
		    EXPECT_EQ(annotationBytes("outline.pdf"), original);
	    })
	    .get();
}

TEST(Annotations, EllipseRefusalsAndCancellationKeepLiveBytesAndRevision) {
	pdf::Engine engine;
	engine
	    .run([] {
		    constexpr pdf::AnnotationBounds bounds{300, 100, 460, 160};
		    const auto original = annotationBytes("pages.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    EXPECT_FALSE(doc.addEllipse(-1, bounds, 1));
		    EXPECT_FALSE(doc.addEllipse(doc.pageCount(), bounds, 1));
		    EXPECT_FALSE(doc.addEllipse(0, bounds, 0));
		    EXPECT_FALSE(doc.addEllipse(0, bounds, 2));
		    for (const auto& bad :
		         {pdf::AnnotationBounds{300, 100, 304, 160},
		          pdf::AnnotationBounds{460, 100, 300, 160},
		          pdf::AnnotationBounds{-1, 100, 460, 160},
		          pdf::AnnotationBounds{300, 100, 600, 160},
		          pdf::AnnotationBounds{300, 100, 460,
		                                std::numeric_limits<float>::infinity()},
		          pdf::AnnotationBounds{300, 100,
		                                std::numeric_limits<float>::quiet_NaN(),
		                                160}}) {
			    EXPECT_FALSE(doc.addEllipse(0, bad, 1));
		    }
		    EXPECT_EQ(doc.serialize(1024 * 1024), original);
		    EXPECT_EQ(doc.annotationRevision(), 1u);
		    int checkpoints = 0;
		    const auto baseline = pdf::openDocument(original);
		    ASSERT_TRUE(baseline.document);
		    ASSERT_TRUE(baseline.document->addEllipse(0, bounds, 1, [&] {
			    ++checkpoints;
			    return false;
		    }));
		    EXPECT_GT(checkpoints, 5);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    SCOPED_TRACE(stop);
			    const auto candidate = pdf::openDocument(original);
			    ASSERT_TRUE(candidate.document);
			    int calls = 0;
			    EXPECT_FALSE(candidate.document->addEllipse(
			        0, bounds, 1, [&] { return ++calls == stop; }));
			    EXPECT_EQ(candidate.document->annotationRevision(), 1u);
			    EXPECT_FALSE(candidate.document->modified());
			    EXPECT_EQ(candidate.document->serialize(1024 * 1024), original);
		    }
		    for (const auto& source :
		         {annotationBytes("notes_denied.pdf"),
		          editableNoteBytes("/Contents (Keep) /FT /Sig"),
		          sharedHighlightBytes(2)}) {
			    const auto refused = pdf::openDocument(source);
			    ASSERT_TRUE(refused.document);
			    EXPECT_FALSE(refused.document->addEllipse(0, bounds, 1));
			    EXPECT_EQ(refused.document->annotationRevision(), 1u);
			    EXPECT_EQ(refused.document->serialize(1024 * 1024), source);
		    }
	    })
	    .get();
}

TEST(Annotations, InkStrokesKeepNeighborsAndSavedScreenAndPrintPixels) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("outline.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.outline());
		    const auto link = doc.outlineLink(0);
		    ASSERT_TRUE(link);
		    ASSERT_EQ(doc.addTextNote(0, 30, 40, u"Keep note"), 0);
		    const auto revision = doc.annotationRevision();
		    const std::vector<pdf::InkStroke> strokes{
		        {{60, 100}, {110, 100}, {160, 100}},
		        {{200, 80}, {200, 180}},
		        {{300.25f, 200.5f}}};
		    ASSERT_EQ(doc.addInk(0, strokes, revision), 1);
		    EXPECT_EQ(doc.annotationRevision(), revision + 1);
		    EXPECT_EQ(doc.textNote(0, 0, doc.annotationRevision()),
		              u"Keep note");
		    const auto kept = doc.outlineLink(0);
		    ASSERT_TRUE(kept);
		    EXPECT_EQ(kept->uri, link->uri);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto notes = saved.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 2);
		    const auto& ink = notes->entries[1];
		    ASSERT_TRUE(ink.bounds);
		    EXPECT_EQ(ink.subtype, "Ink");
		    EXPECT_EQ(ink.flags, FPDF_ANNOT_FLAG_PRINT);
		    EXPECT_TRUE(ink.contents.empty() && ink.author.empty());
		    EXPECT_FLOAT_EQ(ink.bounds->left, 59);
		    EXPECT_FLOAT_EQ(ink.bounds->bottom, 79);
		    EXPECT_FLOAT_EQ(ink.bounds->right, 301.25f);
		    EXPECT_FLOAT_EQ(ink.bounds->top, 201.5f);
		    EXPECT_EQ(pdf::DocumentEditTest::inkList(*saved.document, 1),
		              strokes);
		    const auto screen = saved.document->render(0, 360, 240);
		    const auto print =
		        saved.document->printRaster(0, 360, 240, 0, 0, 360, 240);
		    ASSERT_TRUE(screen && print);
		    for (const auto* image : {&*screen, &*print}) {
			    const auto blue = [&](int x, int y) {
				    return image
				        ->pixels[(269 - y) * image->stride + 4 * (x - 20)];
			    };
			    // each pixel covers [x, x + 1] by [y, y + 1] in page points.
			    EXPECT_EQ(blue(100, 100), 0);
			    EXPECT_EQ(blue(159, 100), 0);
			    EXPECT_EQ(blue(200, 130), 0);
			    EXPECT_LT(blue(300, 200), 128);
			    EXPECT_EQ(blue(130, 140), 255);
			    EXPECT_EQ(blue(163, 100), 255);
			    EXPECT_EQ(blue(100, 104), 255);
			    EXPECT_EQ(blue(304, 204), 255);
		    }
		    EXPECT_EQ(annotationBytes("outline.pdf"), original);
	    })
	    .get();
}

TEST(Annotations, InkRefusalsAndCancellationKeepLiveBytesAndRevision) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::vector<pdf::InkStroke> strokes{
		        {{100, 100}, {150, 140}, {180, 120}}, {{220, 160}}};
		    const auto original = annotationBytes("pages.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    EXPECT_FALSE(doc.addInk(-1, strokes, 1));
		    EXPECT_FALSE(doc.addInk(doc.pageCount(), strokes, 1));
		    EXPECT_FALSE(doc.addInk(0, strokes, 0));
		    EXPECT_FALSE(doc.addInk(0, strokes, 2));
		    const auto nan = std::numeric_limits<float>::quiet_NaN();
		    const auto inf = std::numeric_limits<float>::infinity();
		    std::vector<std::vector<pdf::InkStroke>> bad;
		    bad.push_back({});
		    bad.push_back({pdf::InkStroke{}});
		    bad.push_back({pdf::InkStroke{{100, 100}}, pdf::InkStroke{}});
		    bad.push_back({pdf::InkStroke{{nan, 100}}});
		    bad.push_back({pdf::InkStroke{{100, inf}}});
		    // half the line width would cross the page's left edge.
		    bad.push_back({pdf::InkStroke{{0.5f, 100}}});
		    bad.push_back({pdf::InkStroke{{100, 100}, {100, 5000}}});
		    bad.push_back({pdf::InkStroke(4097, pdf::InkPoint{100, 100})});
		    bad.push_back(
		        std::vector<pdf::InkStroke>(257, pdf::InkStroke{{100, 100}}));
		    bad.push_back(std::vector<pdf::InkStroke>(
		        5, pdf::InkStroke(4000, pdf::InkPoint{100, 100})));
		    for (const auto& refused : bad) {
			    EXPECT_FALSE(doc.addInk(0, refused, 1));
		    }
		    EXPECT_EQ(doc.serialize(1024 * 1024), original);
		    EXPECT_EQ(doc.annotationRevision(), 1u);
		    int checkpoints = 0;
		    const auto baseline = pdf::openDocument(original);
		    ASSERT_TRUE(baseline.document);
		    ASSERT_TRUE(baseline.document->addInk(0, strokes, 1, [&] {
			    ++checkpoints;
			    return false;
		    }));
		    EXPECT_GT(checkpoints, 5);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    SCOPED_TRACE(stop);
			    const auto candidate = pdf::openDocument(original);
			    ASSERT_TRUE(candidate.document);
			    int calls = 0;
			    EXPECT_FALSE(candidate.document->addInk(
			        0, strokes, 1, [&] { return ++calls == stop; }));
			    EXPECT_EQ(candidate.document->annotationRevision(), 1u);
			    EXPECT_FALSE(candidate.document->modified());
			    EXPECT_EQ(candidate.document->serialize(1024 * 1024), original);
		    }
		    for (const auto& source :
		         {annotationBytes("notes_denied.pdf"),
		          editableNoteBytes("/Contents (Keep) /FT /Sig"),
		          sharedHighlightBytes(2)}) {
			    const auto refused = pdf::openDocument(source);
			    ASSERT_TRUE(refused.document);
			    EXPECT_FALSE(refused.document->addInk(0, strokes, 1));
			    EXPECT_EQ(refused.document->annotationRevision(), 1u);
			    EXPECT_EQ(refused.document->serialize(1024 * 1024), source);
		    }
	    })
	    .get();
}

TEST(Annotations,
     EllipseRemovalKeepsNeighborsAndRefusesProtectedOrSharedShapes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("pages.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_EQ(doc.addTextNote(0, 30, 40, u"Keep note"), 0);
		    ASSERT_EQ(doc.addRectangle(0, {40, 100, 120, 160}, 1), 1);
		    ASSERT_EQ(doc.addEllipse(0, {300, 100, 460, 160}, 2), 2);
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 3);
		    const auto target = notes->entries[2].target;
		    const auto before = pdf::DocumentEditTest::rawBytes(doc);
		    auto stale = target;
		    --stale.revision;
		    EXPECT_FALSE(doc.removeAnnotation(stale));
		    auto wrong = target;
		    wrong.subtype = "Square";
		    EXPECT_FALSE(doc.removeAnnotation(wrong));
		    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), before);
		    EXPECT_EQ(doc.annotationRevision(), target.revision);
		    ASSERT_EQ(doc.removeAnnotation(target), true);
		    EXPECT_EQ(doc.annotationRevision(), target.revision + 1);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto reopened = pdf::openDocument(*bytes);
		    ASSERT_TRUE(reopened.document);
		    const auto kept = reopened.document->annotations(0);
		    ASSERT_TRUE(kept && kept->entries.size() == 2);
		    EXPECT_EQ(kept->entries[0].contents, "Keep note");
		    EXPECT_EQ(kept->entries[1].subtype, "Square");
		    EXPECT_EQ(annotationBytes("pages.pdf"), original);
		    std::vector<std::vector<std::uint8_t>> protectedShapes{
		        sharedHighlightBytes(1, "0 0 200 300", true, "Circle")};
		    for (int flags :
		         {FPDF_ANNOT_FLAG_READONLY, FPDF_ANNOT_FLAG_LOCKED, 512}) {
			    protectedShapes.push_back(editableNoteBytes(
			        "/Contents (Keep) /F " + std::to_string(flags), "Circle"));
		    }
		    for (const auto& source : protectedShapes) {
			    const auto candidate = pdf::openDocument(source);
			    ASSERT_TRUE(candidate.document);
			    const auto entries = candidate.document->annotations(0);
			    ASSERT_TRUE(entries && entries->entries.size() == 1);
			    EXPECT_FALSE(candidate.document->removeAnnotation(
			        entries->entries[0].target));
			    EXPECT_EQ(candidate.document->annotationRevision(), 1u);
			    EXPECT_FALSE(candidate.document->modified());
			    EXPECT_EQ(candidate.document->serialize(1024 * 1024), source);
		    }
	    })
	    .get();
}

TEST(Annotations, ResizedShapesKeepAnchorsNeighborsAndSavedPixels) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("outline.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.outline());
		    const auto link = doc.outlineLink(0);
		    ASSERT_TRUE(link);
		    ASSERT_EQ(doc.addTextNote(0, 30, 40, u"Keep note"), 0);
		    ASSERT_EQ(doc.addRectangle(0, {40, 100, 120, 160}, 1), 1);
		    ASSERT_EQ(doc.addEllipse(0, {200, 100, 360, 160}, 2), 2);
		    for (int index : {1, 2}) {
			    const auto notes = doc.annotations(0);
			    ASSERT_TRUE(notes && notes->entries.size() == 3);
			    const auto shape = notes->entries[index];
			    const auto raw = pdf::DocumentEditTest::rawBytes(doc);
			    const auto& rect = *shape.bounds;
			    EXPECT_EQ(doc.resizeShape(shape, {rect.right - rect.left,
			                                      rect.top - rect.bottom}),
			              false);
			    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
			    EXPECT_EQ(doc.annotationRevision(), shape.target.revision);
			    EXPECT_EQ(doc.resizeShape(shape, {170, 90}), true);
			    EXPECT_FALSE(doc.resizeShape(shape, {180, 100}));
			    const auto changed = doc.annotations(0);
			    ASSERT_TRUE(changed && changed->entries.size() == 3);
			    const auto& grown = changed->entries[index];
			    EXPECT_EQ(grown.index, shape.index);
			    EXPECT_EQ(grown.subtype, shape.subtype);
			    EXPECT_FLOAT_EQ(grown.bounds->left, rect.left);
			    EXPECT_FLOAT_EQ(grown.bounds->top, rect.top);
			    EXPECT_FLOAT_EQ(grown.bounds->right, rect.left + 170);
			    EXPECT_FLOAT_EQ(grown.bounds->bottom, rect.top - 90);
			    EXPECT_EQ(doc.resizeShape(grown, {70, 50}), true);
		    }
		    EXPECT_EQ(doc.textNote(0, 0, doc.annotationRevision()),
		              u"Keep note");
		    const auto keptLink = doc.outlineLink(0);
		    ASSERT_TRUE(keptLink);
		    EXPECT_EQ(keptLink->uri, link->uri);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto notes = saved.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 3);
		    EXPECT_EQ(notes->entries[0].contents, "Keep note");
		    for (int i : {1, 2}) {
			    EXPECT_FLOAT_EQ(notes->entries[i].bounds->right -
			                        notes->entries[i].bounds->left,
			                    70);
			    EXPECT_FLOAT_EQ(notes->entries[i].bounds->top -
			                        notes->entries[i].bounds->bottom,
			                    50);
		    }
		    for (bool printing : {false, true}) {
			    const auto a =
			        printing ? doc.printRaster(0, 360, 240, 0, 0, 360, 240)
			                 : doc.render(0, 360, 240);
			    const auto b = printing ? saved.document->printRaster(
			                                  0, 360, 240, 0, 0, 360, 240)
			                            : saved.document->render(0, 360, 240);
			    ASSERT_TRUE(a && b);
			    EXPECT_EQ(a->pixels, b->pixels);
		    }
		    EXPECT_EQ(annotationBytes("outline.pdf"), original);
	    })
	    .get();
}

TEST(Annotations,
     ShapeResizeRefusesForgedSnapshotsStylesAndAppearanceMatrices) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(annotationBytes("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_EQ(doc.addRectangle(0, {40, 100, 120, 160}, 1), 0);
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 1);
		    const auto shape = notes->entries[0];
		    const auto bytes = pdf::DocumentEditTest::rawBytes(doc);
		    for (int change = 0; change < 5; ++change) {
			    auto forged = shape;
			    if (change == 0) {
				    forged.bounds->left += 1;
			    }
			    if (change == 1) {
				    forged.contents = "forged";
			    }
			    if (change == 2) {
				    forged.author = "forged";
			    }
			    if (change == 3) {
				    forged.flags = 0;
			    }
			    if (change == 4) {
				    forged.target.subtype = "Circle";
			    }
			    EXPECT_FALSE(doc.resizeShape(forged, {100, 50}));
		    }
		    for (auto size :
		         {pdf::ShapeSize{4, 50}, pdf::ShapeSize{100, 0},
		          pdf::ShapeSize{1000, 50}, pdf::ShapeSize{100, 1000},
		          pdf::ShapeSize{std::numeric_limits<float>::infinity(), 50},
		          pdf::ShapeSize{100,
		                         std::numeric_limits<float>::quiet_NaN()}}) {
			    EXPECT_FALSE(doc.resizeShape(shape, size));
		    }
		    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), bytes);
		    EXPECT_EQ(doc.annotationRevision(), shape.target.revision);
		    bool fits = false;
		    int count = 0;
		    const auto ap = pdf::DocumentEditTest::appearance(doc, fits, count);
		    std::string stream;
		    std::transform(ap.begin(), ap.end(), std::back_inserter(stream),
		                   [](char16_t c) { return static_cast<char>(c); });
		    const auto appearance = "<< /Type /XObject /Subtype /Form /BBox "
		                            "[40 100 120 160] /Length " +
		                            std::to_string(stream.size()) +
		                            " >>\nstream\n" + stream + "\nendstream";
		    for (const auto* fields :
		         {"", "/C [1 0 0]", "/IC [0 1 0]", "/BS << /W 4 >>",
		          "/Border [0 0 4]", "/BE << /S /C >>", "/CA 0.5",
		          "/BM /Multiply", "/RD [1 1 1 1]", "/Rotate 90", "/OC << >>",
		          "/A << >>", "/AA << >>", "/Contents ()", "/T ()",
		          "/NM (custom)"}) {
			    const auto source = editableNoteBytes(
			        std::string("/F 4 /AP << /N 5 0 R >> ") + fields, "Square",
			        {}, "/MediaBox [0 0 600 800]", "[40 100 120 160]",
			        appearance);
			    const auto candidate = pdf::openDocument(source);
			    ASSERT_TRUE(candidate.document);
			    const auto entries = candidate.document->annotations(0);
			    ASSERT_TRUE(entries && entries->entries.size() == 1);
			    const auto result = candidate.document->resizeShape(
			        entries->entries[0], {100, 50});
			    if (std::string_view(fields).empty()) {
				    EXPECT_EQ(result, true);
			    } else {
				    EXPECT_FALSE(result) << fields;
				    EXPECT_EQ(candidate.document->serialize(1024 * 1024),
				              source);
			    }
		    }
		    // the transformed BBox is fitted to Rect, so a translation alone
		    // draws the same pixels and is replaced by the standard stream.
		    // rotation or a stretched box moves the outline and must refuse.
		    for (const auto& [extra, accepted] :
		         {std::pair{std::string("/Matrix [1 0 0 1 10 0]"), true},
		          std::pair{std::string("/Matrix [0 1 -1 0 0 0]"), false},
		          std::pair{std::string("/BBox [40 100 200 200]"), false}}) {
			    const auto source = editableNoteBytes(
			        "/F 4 /AP << /N 5 0 R >>", "Square", {},
			        "/MediaBox [0 0 600 800]", "[40 100 120 160]",
			        "<< /Type /XObject /Subtype /Form /BBox [40 100 120 160] " +
			            extra + " /Length " + std::to_string(stream.size()) +
			            " >>\nstream\n" + stream + "\nendstream");
			    const auto candidate = pdf::openDocument(source);
			    ASSERT_TRUE(candidate.document);
			    const auto entries = candidate.document->annotations(0);
			    ASSERT_TRUE(entries && entries->entries.size() == 1);
			    const auto result = candidate.document->resizeShape(
			        entries->entries[0], {100, 50});
			    if (accepted) {
				    EXPECT_EQ(result, true) << extra;
			    } else {
				    EXPECT_FALSE(result) << extra;
				    EXPECT_EQ(candidate.document->serialize(1024 * 1024),
				              source);
			    }
		    }
		    for (const auto& source :
		         {sharedHighlightBytes(1, "0 0 200 300", true, "Square"),
		          sharedShapeAppearanceBytes(stream),
		          editableNoteBytes("/F 132", "Square"),
		          editableNoteBytes("/F 4 /C [1 0 0]", "Square"),
		          editableNoteBytes("/F 4 /Border [0 0 4]", "Square"),
		          editableNoteBytes("/F 4 /Popup 5 0 R", "Square"),
		          editableNoteBytes("/F 4 /A << /S /JavaScript >>",
		                            "Square")}) {
			    const auto candidate = pdf::openDocument(source);
			    ASSERT_TRUE(candidate.document);
			    const auto entries = candidate.document->annotations(0);
			    ASSERT_TRUE(entries && !entries->entries.empty());
			    EXPECT_FALSE(candidate.document->resizeShape(
			        entries->entries[0], {100, 50}));
			    EXPECT_EQ(candidate.document->serialize(1024 * 1024), source);
		    }
		    pdf::DocumentEditTest::replaceAppearance(doc, 0);
		    EXPECT_FALSE(doc.resizeShape(shape, {100, 50}));
	    })
	    .get();
}

TEST(Annotations, ShapeResizeKeepsLargeNeighborAppearances) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("outline.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_EQ(doc.addRectangle(0, {40, 100, 120, 160}, 1), 0);
		    // explicit-line boxes draw glyph outlines, so their stream can
		    // pass the 64 KiB a plain outline needs.
		    ASSERT_EQ(doc.addTextBox(0, {130, 120, 370, 260},
		                             u"First explicit line\n\nThird line "
		                             u"after blank\nFourth glyph row",
		                             {}, 2, {},
		                             pdf::TextBoxLayout::ExplicitLines),
		              1);
		    bool fits = false;
		    int count = 0;
		    const auto box =
		        pdf::DocumentEditTest::appearance(doc, fits, count, 1);
		    ASSERT_GT(box.size(), 32768u);
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 2);
		    EXPECT_EQ(doc.resizeShape(notes->entries[0], {100, 40}), true);
		    const auto bytes = doc.serializeForCopy(4 * 1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto kept = saved.document->annotations(0);
		    ASSERT_TRUE(kept && kept->entries.size() == 2);
		    ASSERT_TRUE(kept->entries[0].bounds);
		    EXPECT_FLOAT_EQ(kept->entries[0].bounds->right, 140);
		    EXPECT_FLOAT_EQ(kept->entries[0].bounds->bottom, 120);
		    EXPECT_EQ(pdf::DocumentEditTest::appearance(*saved.document, fits,
		                                                count, 1),
		              box);
		    EXPECT_EQ(annotationBytes("outline.pdf"), original);
	    })
	    .get();
}

TEST(Annotations, ShapeResizeCancellationLeavesEverySavepointIntact) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(annotationBytes("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    ASSERT_EQ(opened.document->addEllipse(0, {40, 100, 120, 160}, 1),
		              0);
		    const auto source = opened.document->serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(source);
		    int checkpoints = 0;
		    const auto baseline = pdf::openDocument(*source);
		    ASSERT_TRUE(baseline.document);
		    const auto notes = baseline.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 1);
		    ASSERT_EQ(baseline.document->resizeShape(notes->entries[0],
		                                             {100, 50},
		                                             [&] {
			                                             ++checkpoints;
			                                             return false;
		                                             }),
		              true);
		    EXPECT_GT(checkpoints, 10);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    SCOPED_TRACE(stop);
			    const auto candidate = pdf::openDocument(*source);
			    ASSERT_TRUE(candidate.document);
			    int calls = 0;
			    EXPECT_FALSE(candidate.document->resizeShape(
			        notes->entries[0], {100, 50},
			        [&] { return ++calls == stop; }));
			    EXPECT_EQ(candidate.document->annotationRevision(), 1u);
			    EXPECT_FALSE(candidate.document->modified());
			    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(*candidate.document),
			              *source);
		    }
	    })
	    .get();
}

TEST(Annotations, ShapeResizeKeepsFractionalNoOpsAndCroppedRotationBounds) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (int turn = 0; turn < 4; ++turn) {
			    const auto source =
			        editableNoteBytes("/Contents (Keep)", "Text", {},
			                          "/MediaBox [0 0 400 600] /CropBox [20 40 "
			                          "380 560] /Rotate " +
			                              std::to_string(turn * 90));
			    const auto opened = pdf::openDocument(source);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    ASSERT_EQ(
			        doc.addEllipse(0, {40.25f, 100.125f, 120.375f, 160.5f}, 1),
			        1);
			    const auto notes = doc.annotations(0);
			    ASSERT_TRUE(notes && notes->entries.size() == 2);
			    const auto shape = notes->entries[1];
			    const auto bytes = pdf::DocumentEditTest::rawBytes(doc);
			    EXPECT_EQ(doc.resizeShape(shape, {80.125f, 60.375f}), false);
			    EXPECT_FALSE(doc.resizeShape(shape, {340, 60}));
			    EXPECT_FALSE(doc.resizeShape(shape, {100, 121}));
			    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), bytes);
			    ASSERT_EQ(doc.resizeShape(shape, {339.75f, 120.5f}), true);
			    const auto saved = doc.serializeForCopy(1024 * 1024);
			    ASSERT_TRUE(saved);
			    const auto reopened = pdf::openDocument(*saved);
			    ASSERT_TRUE(reopened.document);
			    const auto kept = reopened.document->annotations(0);
			    ASSERT_TRUE(kept && kept->entries.size() == 2);
			    EXPECT_EQ(kept->entries[0].contents, "Keep");
			    EXPECT_FLOAT_EQ(kept->entries[1].bounds->right, 380);
			    EXPECT_FLOAT_EQ(kept->entries[1].bounds->bottom, 40);
		    }
	    })
	    .get();
}

TEST(Annotations, TextBoxesCommitSavedAppearanceAndKeepSourceBytes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto* file :
		         {"notes_allowed.pdf", "pages.pdf", "password.pdf"}) {
			    SCOPED_TRACE(file);
			    const bool encrypted = std::string_view(file) == "password.pdf";
			    const auto original = annotationBytes(file);
			    const auto opened =
			        pdf::openDocument(original, encrypted ? "sable" : "");
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    ASSERT_TRUE(doc.addTextBox(0, {40, 100, 240, 160},
			                               u"Box (one) \\ two",
			                               u"Author \u03a9", 1));
			    EXPECT_TRUE(doc.modified());
			    EXPECT_EQ(doc.annotationRevision(), 2u);
			    const auto notes = doc.annotations(0);
			    ASSERT_TRUE(notes && notes->entries.size() == 1);
			    EXPECT_EQ(notes->entries[0].subtype, "FreeText");
			    EXPECT_EQ(notes->entries[0].contents, "Box (one) \\ two");
			    EXPECT_EQ(notes->entries[0].author, "Author \u03a9");
			    EXPECT_EQ(notes->entries[0].flags, FPDF_ANNOT_FLAG_PRINT);
			    const auto raster = doc.render(0, 400, 400);
			    ASSERT_TRUE(raster);
			    const auto bytes = doc.serializeForCopy(1024 * 1024);
			    ASSERT_TRUE(bytes);
			    const auto saved =
			        pdf::openDocument(*bytes, encrypted ? "sable" : "");
			    ASSERT_TRUE(saved.document);
			    const auto after = saved.document->render(0, 400, 400);
			    ASSERT_TRUE(after);
			    EXPECT_EQ(raster->pixels, after->pixels);
			    EXPECT_EQ(annotationBytes(file), original);
			    EXPECT_TRUE(
			        doc.addTextBox(0, {40, 180, 240, 240}, u"Another", {}, 2));
			    EXPECT_EQ(doc.annotationRevision(), 3u);
			    if (encrypted) {
				    EXPECT_EQ(pdf::openDocument(*bytes).error,
				              pdf::Error::PasswordRequired);
			    }
		    }
	    })
	    .get();
}

TEST(Annotations, TextBoxRefusalsLeaveTheLiveDocumentIntact) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("notes_allowed.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    for (const auto* text : {u"", u"first\nsecond", u"Caf\u00e9", u"\t",
		                             u"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"}) {
			    EXPECT_FALSE(
			        doc.addTextBox(0, {40, 100, 60, 120}, text, {}, 1));
		    }
		    for (const auto& box :
		         {pdf::AnnotationBounds{-1, 100, 240, 160},
		          pdf::AnnotationBounds{40, 100, 40, 160},
		          pdf::AnnotationBounds{40, 160, 240, 100},
		          pdf::AnnotationBounds{
		              40, 100, 240, std::numeric_limits<float>::infinity()}}) {
			    EXPECT_FALSE(doc.addTextBox(0, box, u"Text", {}, 1));
		    }
		    EXPECT_FALSE(
		        doc.addTextBox(0, {40, 100, 240, 160}, u"Text", {}, 0));
		    EXPECT_FALSE(
		        doc.addTextBox(0, {40, 100, 240, 160}, u"Text", {}, 2));
		    EXPECT_FALSE(
		        doc.addTextBox(-1, {40, 100, 240, 160}, u"Text", {}, 1));
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.annotationRevision(), 1u);
		    EXPECT_EQ(doc.serialize(1024 * 1024), original);
		    for (const auto& bytes :
		         {annotationBytes("notes_denied.pdf"),
		          editableNoteBytes("/Contents (Keep) /FT /Sig"),
		          sharedHighlightBytes(2)}) {
			    const auto refused = pdf::openDocument(bytes);
			    ASSERT_TRUE(refused.document);
			    EXPECT_FALSE(refused.document->addTextBox(
			        0, {40, 100, 180, 160}, u"Text", {}, 1));
			    EXPECT_EQ(refused.document->serialize(1024 * 1024), bytes);
			    EXPECT_FALSE(refused.document->modified());
		    }
	    })
	    .get();
}

TEST(Annotations, TextBoxCancellationKeepsEveryLiveCheckpoint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("notes_allowed.pdf");
		    const auto completed = pdf::openDocument(original);
		    ASSERT_TRUE(completed.document);
		    int checkpoints = 0;
		    ASSERT_TRUE(completed.document->addTextBox(0, {40, 100, 240, 160},
		                                               u"Text", {}, 1, [&] {
			                                               ++checkpoints;
			                                               return false;
		                                               }));
		    ASSERT_GT(checkpoints, 10);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    const auto opened = pdf::openDocument(original);
			    ASSERT_TRUE(opened.document);
			    int calls = 0;
			    EXPECT_FALSE(opened.document->addTextBox(
			        0, {40, 100, 240, 160}, u"Text", {}, 1,
			        [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), original);
		    }
	    })
	    .get();
}

TEST(Annotations, TextBoxesKeepNotesBookmarksAndPrintableAppearances) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("outline.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto outline = doc.outline();
		    ASSERT_TRUE(outline && !outline->entries.empty());
		    const auto link = doc.outlineLink(0);
		    ASSERT_TRUE(link);
		    const auto note = doc.addTextNote(0, 30, 40, u"Keep note");
		    ASSERT_TRUE(note);
		    ASSERT_TRUE(
		        doc.addTextBox(0, {40, 100, 240, 160}, u"Printed box", {}, 1));
		    EXPECT_EQ(doc.textNote(0, *note, 2), u"Keep note");
		    const auto kept = doc.outlineLink(0);
		    ASSERT_TRUE(kept);
		    EXPECT_EQ(kept->uri, link->uri);
		    const auto print = doc.printRaster(0, 400, 400, 0, 0, 400, 400);
		    ASSERT_TRUE(print);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto after =
		        saved.document->printRaster(0, 400, 400, 0, 0, 400, 400);
		    ASSERT_TRUE(after);
		    EXPECT_EQ(print->pixels, after->pixels);
		    EXPECT_EQ(saved.document->textNote(0, *note), u"Keep note");
		    const auto savedOutline = saved.document->outline();
		    ASSERT_TRUE(savedOutline);
		    EXPECT_EQ(savedOutline->entries.size(), outline->entries.size());
	    })
	    .get();
}

TEST(Annotations, TextBoxFontsSkipExistingAndEscapedResourceNames) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = editableNoteBytes(
		        "/Contents (Keep)", "Text",
		        "/AcroForm << /Fields [] /DR << /Font << "
		        "/SableTextBoxFont#30 << /Type /Font /Subtype /Type1 /BaseFont "
		        "/Courier >> "
		        "/Helv << /Type /Font /Subtype /Type1 /BaseFont /Courier >> "
		        ">> >> >> /Probe (/SableTextBoxFont1)");
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_EQ(
		        doc.addTextBox(0, {20, 100, 180, 160}, u"Independent", {}, 1),
		        1);
		    bool fits = false;
		    int count = 0;
		    const auto ap =
		        pdf::DocumentEditTest::appearance(doc, fits, count, 1);
		    EXPECT_NE(ap.find(u"/SableTextBoxFont2 12 Tf"),
		              std::u16string::npos);
		    EXPECT_TRUE(fits);
		    EXPECT_EQ(doc.textNote(0, 0, 2), u"Keep");
		    const auto saved = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(saved);
		    const auto reopened = pdf::openDocument(*saved);
		    ASSERT_TRUE(reopened.document);
		    EXPECT_EQ(reopened.document->addTextBox(0, {20, 180, 180, 240},
		                                            u"Next", {}, 1),
		              2);
		    const auto next = pdf::DocumentEditTest::appearance(
		        *reopened.document, fits, count, 2);
		    EXPECT_NE(next.find(u"/SableTextBoxFont3 12 Tf"),
		              std::u16string::npos);
		    EXPECT_TRUE(fits);
	    })
	    .get();
}

TEST(Annotations, TextBoxesPreserveMixedFieldsAcrossMultipleWritesAndReopen) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto pdf = mixedFormPdf({.explicitFont = true});
		    const std::vector<std::uint8_t> original(pdf.begin(), pdf.end());
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto before = doc.formFields(0);
		    ASSERT_TRUE(before && before->entries.size() == 6);
		    ASSERT_TRUE(doc.outline());
		    ASSERT_EQ(
		        doc.addTextBox(0, {40, 100, 240, 160}, u"First box", {}, 1), 7);
		    ASSERT_EQ(
		        doc.addTextBox(0, {40, 200, 240, 260}, u"Second box", {}, 2),
		        8);
		    const auto fields = doc.formFields(0);
		    ASSERT_TRUE(fields && fields->entries.size() == 6);
		    for (std::size_t i = 0; i < 6; ++i) {
			    EXPECT_EQ(fields->entries[i].value, before->entries[i].value);
			    EXPECT_EQ(fields->entries[i].checked,
			              before->entries[i].checked);
			    EXPECT_EQ(fields->entries[i].flags, before->entries[i].flags);
		    }
		    EXPECT_EQ(doc.textNote(0, 0, 3), u"Original");
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    ASSERT_EQ(saved.document->addTextBox(0, {40, 300, 240, 360},
		                                         u"Third box", {}, 1),
		              9);
		    const auto notes = saved.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 10);
		    EXPECT_EQ(notes->entries[7].contents, "First box");
		    EXPECT_EQ(notes->entries[8].contents, "Second box");
		    EXPECT_EQ(notes->entries[9].contents, "Third box");
		    const auto kept = saved.document->formFields(0);
		    ASSERT_TRUE(kept && kept->entries.size() == 6);
		    for (std::size_t i = 0; i < 6; ++i) {
			    EXPECT_EQ(kept->entries[i].value, before->entries[i].value);
		    }
		    ASSERT_TRUE(saved.document->outline());
		    const auto link = saved.document->outlineLink(0);
		    ASSERT_TRUE(link && link->destination);
		    EXPECT_EQ(link->destination->page, 0);
	    })
	    .get();
}

TEST(Annotations, TextBoxMalformedOrExhaustedFontResourcesKeepLiveBytes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    std::string occupied = "/Probe (";
		    for (int i = 0; i < 4096; ++i) {
			    occupied += "/SableTextBoxFont" + std::to_string(i) + " ";
		    }
		    occupied += ")";
		    for (const auto& catalog :
		         {occupied,
		          std::string(
		              "/AcroForm << /Fields [] /DR << /Font 123 >> >>")}) {
			    const auto bytes =
			        editableNoteBytes("/Contents (Keep)", "Text", catalog);
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(opened.document->addTextBox(0, {20, 100, 180, 160},
			                                             u"Refused", {}, 1));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(Annotations, DisplayedRectanglesFollowCropAndAllPageRotations) {
	pdf::Engine engine;
	engine
	    .run([] {
		    constexpr pdf::AnnotationBounds expected[]{{65, 170, 245, 430},
		                                               {110, 105, 290, 365},
		                                               {155, 170, 335, 430},
		                                               {110, 235, 290, 495}};
		    for (int intrinsic = 0; intrinsic < 4; ++intrinsic) {
			    const auto source = editableNoteBytes(
			        "/Contents (Keep)", "Text", {},
			        "/MediaBox [0 0 400 600] /CropBox [20 40 380 560] "
			        "/Rotate " +
			            std::to_string(intrinsic * 90));
			    for (int rotation = 0; rotation < 4; ++rotation) {
				    SCOPED_TRACE(intrinsic * 4 + rotation);
				    const auto opened = pdf::openDocument(source);
				    ASSERT_TRUE(opened.document);
				    auto& doc = *opened.document;
				    ASSERT_EQ(doc.addRectangleAt(0, {.625, .75}, {.125, .25},
				                                 rotation, 1),
				              1);
				    const auto notes = doc.annotations(0);
				    ASSERT_TRUE(notes && notes->entries.size() == 2);
				    ASSERT_TRUE(notes->entries[1].bounds);
				    const auto& actual = *notes->entries[1].bounds;
				    const auto& box = expected[(intrinsic + rotation) % 4];
				    EXPECT_FLOAT_EQ(actual.left, box.left);
				    EXPECT_FLOAT_EQ(actual.bottom, box.bottom);
				    EXPECT_FLOAT_EQ(actual.right, box.right);
				    EXPECT_FLOAT_EQ(actual.top, box.top);
				    EXPECT_EQ(notes->entries[0].contents, "Keep");
			    }
		    }
	    })
	    .get();
}

TEST(Annotations, DisplayedEllipsesFollowCropAndAllPageRotations) {
	pdf::Engine engine;
	engine
	    .run([] {
		    constexpr pdf::AnnotationBounds expected[]{{65, 170, 245, 430},
		                                               {110, 105, 290, 365},
		                                               {155, 170, 335, 430},
		                                               {110, 235, 290, 495}};
		    for (int intrinsic = 0; intrinsic < 4; ++intrinsic) {
			    const auto source = editableNoteBytes(
			        "/Contents (Keep)", "Text", {},
			        "/MediaBox [0 0 400 600] /CropBox [20 40 380 560] "
			        "/Rotate " +
			            std::to_string(intrinsic * 90));
			    for (int rotation = 0; rotation < 4; ++rotation) {
				    SCOPED_TRACE(intrinsic * 4 + rotation);
				    const auto opened = pdf::openDocument(source);
				    ASSERT_TRUE(opened.document);
				    auto& doc = *opened.document;
				    ASSERT_EQ(doc.addEllipseAt(0, {.625, .75}, {.125, .25},
				                               rotation, 1),
				              1);
				    const auto bytes = doc.serializeForCopy(1024 * 1024);
				    ASSERT_TRUE(bytes);
				    const auto saved = pdf::openDocument(*bytes);
				    ASSERT_TRUE(saved.document);
				    const auto notes = saved.document->annotations(0);
				    ASSERT_TRUE(notes && notes->entries.size() == 2);
				    ASSERT_TRUE(notes->entries[1].bounds);
				    EXPECT_EQ(notes->entries[1].subtype, "Circle");
				    const auto& actual = *notes->entries[1].bounds;
				    const auto& box = expected[(intrinsic + rotation) % 4];
				    EXPECT_FLOAT_EQ(actual.left, box.left);
				    EXPECT_FLOAT_EQ(actual.bottom, box.bottom);
				    EXPECT_FLOAT_EQ(actual.right, box.right);
				    EXPECT_FLOAT_EQ(actual.top, box.top);
				    EXPECT_EQ(notes->entries[0].contents, "Keep");
			    }
		    }
	    })
	    .get();
}

TEST(Annotations, DisplayedEllipsesRefuseInvalidCornersAndStaleRevisions) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = annotationBytes("pages.pdf");
		    const auto opened = pdf::openDocument(original);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    for (const auto& point :
		         {pdf::AnnotationPoint{-.001, .5},
		          pdf::AnnotationPoint{1.001, .5},
		          pdf::AnnotationPoint{.5, -.001},
		          pdf::AnnotationPoint{.5, 1.001},
		          pdf::AnnotationPoint{std::numeric_limits<double>::quiet_NaN(),
		                               .5},
		          pdf::AnnotationPoint{
		              .5, std::numeric_limits<double>::infinity()}}) {
			    EXPECT_FALSE(doc.addEllipseAt(0, point, {.5, .5}, 0, 1));
			    EXPECT_FALSE(doc.addEllipseAt(0, {.5, .5}, point, 0, 1));
		    }
		    EXPECT_FALSE(doc.addEllipseAt(0, {.2, .2}, {.7, .7}, -1, 1));
		    EXPECT_FALSE(doc.addEllipseAt(0, {.2, .2}, {.7, .7}, 4, 1));
		    EXPECT_FALSE(doc.addEllipseAt(0, {.2, .2}, {.2, .2}, 0, 1));
		    EXPECT_FALSE(doc.addEllipseAt(0, {.2, .2}, {.7, .7}, 0, 2));
		    EXPECT_FALSE(doc.addEllipseAt(0, {.2, .2}, {.7, .7}, 0, 1,
		                                  [] { return true; }));
		    EXPECT_EQ(doc.annotationRevision(), 1u);
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.serialize(1024 * 1024), original);
	    })
	    .get();
}

TEST(Annotations, DisplayedTextBoxesFollowCropAndAllPageRotations) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (int intrinsic = 0; intrinsic < 4; ++intrinsic) {
			    for (int rotation = 0; rotation < 4; ++rotation) {
				    const auto bytes =
				        editableNoteBytes("/Contents (Keep)", "Text", {},
				                          "/MediaBox [0 0 400 600] /CropBox "
				                          "[20 40 380 560] /Rotate " +
				                              std::to_string(intrinsic * 90));
				    for (bool center : {false, true}) {
					    const auto opened = pdf::openDocument(bytes);
					    ASSERT_TRUE(opened.document);
					    ASSERT_EQ(opened.document->addTextBoxAt(
					                  0, center ? .5 : 0, center ? .5 : 0,
					                  rotation, u"Placed", {}, 1),
					              1);
					    const auto notes = opened.document->annotations(0);
					    ASSERT_TRUE(notes && notes->entries.size() == 2 &&
					                notes->entries[1].bounds);
					    const auto& box = *notes->entries[1].bounds;
					    const int turn = (intrinsic + rotation) % 4;
					    const float left =
					        center ? 100.f : (turn < 2 ? 20.f : 180.f);
					    const float bottom =
					        center ? 270.f
					               : (turn == 0 || turn == 3 ? 500.f : 40.f);
					    EXPECT_FLOAT_EQ(box.left, left);
					    EXPECT_FLOAT_EQ(box.bottom, bottom);
					    EXPECT_FLOAT_EQ(box.right, left + 200);
					    EXPECT_FLOAT_EQ(box.top, bottom + 60);
					    const auto saved =
					        opened.document->serialize(1024 * 1024);
					    ASSERT_TRUE(saved);
					    const auto reopened = pdf::openDocument(*saved);
					    ASSERT_TRUE(reopened.document);
					    const auto kept = reopened.document->annotations(0);
					    ASSERT_TRUE(kept && kept->entries.size() == 2 &&
					                kept->entries[1].bounds);
					    EXPECT_FLOAT_EQ(kept->entries[1].bounds->left, left);
					    EXPECT_FLOAT_EQ(kept->entries[1].bounds->bottom,
					                    bottom);
				    }
			    }
		    }
	    })
	    .get();
}

TEST(Annotations, DisplayedTextBoxRefusalsAndSmallPagesKeepLiveBytes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = editableNoteBytes("/Contents (Keep)");
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    for (const auto& point :
		         {pdf::AnnotationPoint{-1, .5}, pdf::AnnotationPoint{.5, 2},
		          pdf::AnnotationPoint{std::numeric_limits<double>::quiet_NaN(),
		                               .5}}) {
			    EXPECT_FALSE(opened.document->addTextBoxAt(0, point.x, point.y,
			                                               0, u"Text", {}, 1));
		    }
		    for (int turn : {-1, 4}) {
			    EXPECT_FALSE(opened.document->addTextBoxAt(0, .5, .5, turn,
			                                               u"Text", {}, 1));
		    }
		    EXPECT_FALSE(opened.document->addTextBoxAt(
		        0, .5, .5, 0, u"Text", {}, 1, [] { return true; }));
		    EXPECT_FALSE(
		        opened.document->addTextBoxAt(0, .5, .5, 0, u"Text", {}, 2));
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    for (const auto* media :
		         {"/MediaBox [0 0 100 40]", "/MediaBox [0 0 3 3]"}) {
			    const auto smallBytes =
			        editableNoteBytes("/Contents (Keep)", "Text", {}, media);
			    const auto doc = pdf::openDocument(smallBytes);
			    ASSERT_TRUE(doc.document);
			    const auto result =
			        doc.document->addTextBoxAt(0, 1, 1, 0, u"Small", {}, 1);
			    if (std::string_view(media).find("100") !=
			        std::string_view::npos) {
				    ASSERT_EQ(result, 1);
				    const auto notes = doc.document->annotations(0);
				    ASSERT_TRUE(notes && notes->entries[1].bounds);
				    EXPECT_FLOAT_EQ(notes->entries[1].bounds->left, 0);
				    EXPECT_FLOAT_EQ(notes->entries[1].bounds->right, 100);
			    } else {
				    EXPECT_FALSE(result);
				    EXPECT_EQ(doc.document->serialize(1024 * 1024), smallBytes);
			    }
		    }
	    })
	    .get();
}

TEST(Annotations, ExistingTextBoxesKeepTheirIndexGeometryAndNeighbors) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("outline.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.outline());
		    ASSERT_EQ(doc.addTextNote(0, 20, 30, u"Keep note"), 0);
		    ASSERT_EQ(doc.addTextBox(0, {40, 100, 240, 160}, u"Original",
		                             u"Author", 1),
		              1);
		    ASSERT_EQ(
		        doc.addTextBox(0, {40, 200, 240, 260}, u"Neighbor", {}, 2), 2);
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 3);
		    const auto box = notes->entries[1];
		    const auto raw = pdf::DocumentEditTest::rawBytes(doc);
		    ASSERT_TRUE(doc.textBox(box.target));
		    EXPECT_EQ(doc.updateTextBox(box, u"Original"), false);
		    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
		    ASSERT_EQ(doc.updateTextBox(box, u"Edited (box)"), true);
		    EXPECT_EQ(doc.annotationRevision(), 4u);
		    EXPECT_FALSE(doc.updateTextBox(box, u"Stale"));
		    const auto after = doc.annotations(0);
		    ASSERT_TRUE(after && after->entries.size() == 3);
		    EXPECT_EQ(after->entries[1].contents, "Edited (box)");
		    EXPECT_EQ(after->entries[1].author, "Author");
		    ASSERT_TRUE(after->entries[1].bounds);
		    EXPECT_FLOAT_EQ(after->entries[1].bounds->left, 40);
		    EXPECT_EQ(after->entries[2].contents, "Neighbor");
		    EXPECT_EQ(doc.textNote(0, 0, 4), u"Keep note");
		    const auto print = doc.printRaster(0, 400, 400, 0, 0, 400, 400);
		    ASSERT_TRUE(print);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto reopened = pdf::openDocument(*bytes);
		    ASSERT_TRUE(reopened.document);
		    const auto saved = reopened.document->annotations(0);
		    ASSERT_TRUE(saved);
		    ASSERT_TRUE(reopened.document->textBox(saved->entries[1].target));
		    const auto raster =
		        reopened.document->printRaster(0, 400, 400, 0, 0, 400, 400);
		    ASSERT_TRUE(raster);
		    EXPECT_EQ(print->pixels, raster->pixels);
		    ASSERT_TRUE(reopened.document->outline());
		    EXPECT_EQ(reopened.document->textNote(0, 0), u"Keep note");
	    })
	    .get();
}

TEST(Annotations, TextBoxEditsRefuseUnsupportedSnapshotsAndKeepBytes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_EQ(
		        doc.addTextBox(0, {40, 100, 240, 160}, u"Original", {}, 1), 0);
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes);
		    const auto box = notes->entries[0];
		    const auto raw = pdf::DocumentEditTest::rawBytes(doc);
		    for (const auto* value : {u"", u"Caf\u00e9", u"first\nsecond",
		                              u"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"}) {
			    EXPECT_FALSE(doc.updateTextBox(box, value));
			    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
		    }
		    auto wrong = box;
		    wrong.bounds->left += 1;
		    EXPECT_FALSE(doc.updateTextBox(wrong, u"Text"));
		    wrong = box;
		    wrong.contents = "Different";
		    EXPECT_FALSE(doc.updateTextBox(wrong, u"Text"));
		    wrong = box;
		    wrong.author = "Different";
		    EXPECT_FALSE(doc.updateTextBox(wrong, u"Text"));
		    EXPECT_EQ(doc.annotationRevision(), 2u);
		    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
		    for (const auto* extra :
		         {"/RC (rich)", "/F 132", "/IRT 4 0 R", "/BS << /W 5 >>"}) {
			    const auto bytes = editableNoteBytes(
			        std::string("/Contents (Original) /DA (/Helv 12 Tf 0 g) ") +
			            extra,
			        "FreeText");
			    const auto styled = pdf::openDocument(bytes);
			    ASSERT_TRUE(styled.document);
			    const auto found = styled.document->annotations(0);
			    ASSERT_TRUE(found && !found->entries.empty());
			    EXPECT_FALSE(
			        styled.document->textBox(found->entries[0].target));
			    EXPECT_FALSE(styled.document->updateTextBox(found->entries[0],
			                                                u"Original"));
			    EXPECT_EQ(styled.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(Annotations, TextBoxEditCancellationPreservesEveryCheckpoint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto seed = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(seed.document);
		    ASSERT_TRUE(seed.document->addTextBox(0, {40, 100, 240, 160},
		                                          u"Original", {}, 1));
		    const auto bytes = seed.document->serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto completed = pdf::openDocument(*bytes);
		    ASSERT_TRUE(completed.document);
		    const auto snapshot = completed.document->annotations(0);
		    ASSERT_TRUE(snapshot);
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateTextBox(snapshot->entries[0],
		                                                u"Edited",
		                                                [&] {
			                                                ++checkpoints;
			                                                return false;
		                                                }),
		              true);
		    ASSERT_GT(checkpoints, 10);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    const auto opened = pdf::openDocument(*bytes);
			    ASSERT_TRUE(opened.document);
			    const auto notes = opened.document->annotations(0);
			    ASSERT_TRUE(notes);
			    int calls = 0;
			    EXPECT_FALSE(opened.document->updateTextBox(
			        notes->entries[0], u"Edited",
			        [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(Annotations,
     TextBoxEditRejectsChangedAppearanceBehindAGenuineResourceName) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(
		        doc.addTextBox(0, {40, 100, 240, 160}, u"Original", {}, 1));
		    pdf::DocumentEditTest::replaceAppearance(doc, 0);
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes);
		    const auto raw = pdf::DocumentEditTest::rawBytes(doc);
		    EXPECT_FALSE(doc.textBox(notes->entries[0].target));
		    EXPECT_FALSE(doc.updateTextBox(notes->entries[0], u"Original"));
		    EXPECT_FALSE(doc.updateTextBox(notes->entries[0], u"Edited"));
		    EXPECT_EQ(doc.annotationRevision(), 2u);
		    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
	    })
	    .get();
}

TEST(Annotations, TextBoxResizingKeepsTopLeftIndexAuthorAndNeighbors) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("outline.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.outline());
		    ASSERT_EQ(doc.addTextNote(0, 20, 30, u"Keep note"), 0);
		    ASSERT_EQ(doc.addTextBox(0, {40, 200, 240, 260}, u"Original",
		                             u"Author", 1),
		              1);
		    ASSERT_EQ(
		        doc.addTextBox(0, {40, 100, 240, 160}, u"Neighbor", {}, 2), 2);
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes);
		    const auto box = notes->entries[1];
		    const auto raw = pdf::DocumentEditTest::rawBytes(doc);
		    EXPECT_EQ(
		        doc.updateTextBox(box, u"Original", pdf::TextBoxSize{200, 60}),
		        false);
		    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
		    EXPECT_EQ(doc.annotationRevision(), 3u);
		    ASSERT_EQ(doc.updateTextBox(box, u"Wider replacement line",
		                                pdf::TextBoxSize{300, 90}),
		              true);
		    EXPECT_FALSE(
		        doc.updateTextBox(box, u"Stale", pdf::TextBoxSize{250, 60}));
		    const auto after = doc.annotations(0);
		    ASSERT_TRUE(after && after->entries.size() == 3);
		    const auto& changed = after->entries[1];
		    ASSERT_TRUE(changed.bounds);
		    EXPECT_EQ(changed.index, 1);
		    EXPECT_EQ(changed.author, "Author");
		    EXPECT_FLOAT_EQ(changed.bounds->left, 40);
		    EXPECT_FLOAT_EQ(changed.bounds->top, 260);
		    EXPECT_FLOAT_EQ(changed.bounds->right, 340);
		    EXPECT_FLOAT_EQ(changed.bounds->bottom, 170);
		    EXPECT_EQ(after->entries[2].contents, "Neighbor");
		    EXPECT_FLOAT_EQ(after->entries[2].bounds->top, 160);
		    const auto print = doc.printRaster(0, 400, 400, 0, 0, 400, 400);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes && print);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto kept = saved.document->annotations(0);
		    ASSERT_TRUE(kept && kept->entries.size() == 3);
		    EXPECT_EQ(kept->entries[1].contents, "Wider replacement line");
		    EXPECT_FLOAT_EQ(kept->entries[1].bounds->right, 340);
		    EXPECT_FLOAT_EQ(kept->entries[1].bounds->bottom, 170);
		    EXPECT_EQ(saved.document->textNote(0, 0), u"Keep note");
		    const auto outline = saved.document->outline();
		    ASSERT_TRUE(outline);
		    EXPECT_FALSE(outline->entries.empty());
		    const auto printed =
		        saved.document->printRaster(0, 400, 400, 0, 0, 400, 400);
		    ASSERT_TRUE(printed);
		    EXPECT_EQ(printed->pixels, print->pixels);
		    EXPECT_TRUE(saved.document->textBox(kept->entries[1].target));
		    EXPECT_EQ(saved.document->updateTextBox(kept->entries[1], u"Short",
		                                            pdf::TextBoxSize{180, 45}),
		              true);
	    })
	    .get();
}

TEST(Annotations, TextBoxSizeRefusalsKeepExactLiveBytes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(
		        doc.addTextBox(0, {40, 200, 240, 260}, u"Original", {}, 1));
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes);
		    const auto box = notes->entries[0];
		    const auto raw = pdf::DocumentEditTest::rawBytes(doc);
		    const auto nan = std::numeric_limits<float>::quiet_NaN();
		    const auto inf = std::numeric_limits<float>::infinity();
		    for (const auto size : {pdf::TextBoxSize{0, 60},
		                            {-1, 60},
		                            {200, 0},
		                            {200, -1},
		                            {nan, 60},
		                            {200, inf},
		                            {inf, 60},
		                            {200, nan},
		                            {600, 60},
		                            {200, 900},
		                            {1, 60},
		                            {200, 1}}) {
			    EXPECT_FALSE(doc.updateTextBox(box, u"Original", size));
			    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
			    EXPECT_EQ(doc.annotationRevision(), 2u);
		    }
		    pdf::DocumentEditTest::replaceAppearance(doc, 0);
		    EXPECT_FALSE(
		        doc.updateTextBox(box, u"Original", pdf::TextBoxSize{300, 90}));
		    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
	    })
	    .get();
}

TEST(Annotations, ResizedBoxesStayInsideCroppedRotatedPages) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (int turn = 0; turn < 4; ++turn) {
			    const auto bytes =
			        editableNoteBytes("/Contents (Keep)", "Text", {},
			                          "/MediaBox [0 0 400 600] /CropBox [20 40 "
			                          "380 560] /Rotate " +
			                              std::to_string(turn * 90));
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    ASSERT_EQ(
			        doc.addTextBox(0, {40, 200, 240, 260}, u"Text", {}, 1), 1);
			    const auto notes = doc.annotations(0);
			    ASSERT_TRUE(notes);
			    const auto& box = notes->entries[1];
			    EXPECT_FALSE(
			        doc.updateTextBox(box, u"Text", pdf::TextBoxSize{341, 60}));
			    EXPECT_FALSE(doc.updateTextBox(box, u"Text",
			                                   pdf::TextBoxSize{200, 221}));
			    ASSERT_EQ(
			        doc.updateTextBox(box, u"Text", pdf::TextBoxSize{340, 220}),
			        true);
			    const auto saved = doc.serialize(1024 * 1024);
			    ASSERT_TRUE(saved);
			    const auto reopened = pdf::openDocument(*saved);
			    ASSERT_TRUE(reopened.document);
			    const auto after = reopened.document->annotations(0);
			    ASSERT_TRUE(after && after->entries.size() == 2);
			    EXPECT_FLOAT_EQ(after->entries[1].bounds->left, 40);
			    EXPECT_FLOAT_EQ(after->entries[1].bounds->top, 260);
			    EXPECT_FLOAT_EQ(after->entries[1].bounds->right, 380);
			    EXPECT_FLOAT_EQ(after->entries[1].bounds->bottom, 40);
			    for (int view = 0; view < 4; ++view) {
				    const auto before = doc.render(0, 400, 400, view);
				    const auto kept =
				        reopened.document->render(0, 400, 400, view);
				    ASSERT_TRUE(before && kept);
				    EXPECT_EQ(before->pixels, kept->pixels);
			    }
		    }
	    })
	    .get();
}

TEST(Annotations, TextBoxResizeCancellationKeepsEverySavepoint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto seed = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(seed.document);
		    ASSERT_TRUE(seed.document->addTextBox(0, {40, 200, 240, 260},
		                                          u"Original", {}, 1));
		    const auto bytes = seed.document->serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto completed = pdf::openDocument(*bytes);
		    ASSERT_TRUE(completed.document);
		    const auto notes = completed.document->annotations(0);
		    ASSERT_TRUE(notes);
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateTextBox(
		                  notes->entries[0], u"Original",
		                  pdf::TextBoxSize{300, 90},
		                  [&] {
			                  ++checkpoints;
			                  return false;
		                  }),
		              true);
		    ASSERT_GT(checkpoints, 10);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    const auto opened = pdf::openDocument(*bytes);
			    ASSERT_TRUE(opened.document);
			    const auto snapshot = opened.document->annotations(0);
			    ASSERT_TRUE(snapshot);
			    int calls = 0;
			    EXPECT_FALSE(opened.document->updateTextBox(
			        snapshot->entries[0], u"Original",
			        pdf::TextBoxSize{300, 90},
			        [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(Annotations, ResizedEncryptedBoxesKeepTheirPasswordAndPermissions) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened =
		        pdf::openFile(fixturePath("password.pdf"), "sable");
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(
		        doc.addTextBox(0, {40, 200, 240, 260}, u"Original", {}, 1));
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(doc.updateTextBox(notes->entries.back(), u"Resized",
		                                pdf::TextBoxSize{300, 90}),
		              true);
		    const auto bytes = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    EXPECT_EQ(pdf::openDocument(*bytes).error,
		              pdf::Error::PasswordRequired);
		    const auto saved = pdf::openDocument(*bytes, "sable");
		    ASSERT_TRUE(saved.document && saved.document->canAnnotate());
		    const auto kept = saved.document->annotations(0);
		    ASSERT_TRUE(kept);
		    EXPECT_EQ(kept->entries.back().contents, "Resized");
		    EXPECT_FLOAT_EQ(kept->entries.back().bounds->right, 340);
	    })
	    .get();
}

TEST(Annotations, FractionalTextBoxSizesKeepExactNoOpCoordinates) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.addTextBox(0, {40.1f, 100.2f, 240.3f, 160.4f},
		                               u"Fraction", {}, 1));
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes && notes->entries[0].bounds);
		    const auto box = notes->entries[0];
		    const auto& rect = *box.bounds;
		    const auto raw = pdf::DocumentEditTest::rawBytes(doc);
		    EXPECT_EQ(
		        doc.updateTextBox(box, u"Fraction",
		                          pdf::TextBoxSize{rect.right - rect.left,
		                                           rect.top - rect.bottom}),
		        false);
		    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
		    EXPECT_EQ(doc.annotationRevision(), 2u);
	    })
	    .get();
}

TEST(Annotations, MultilineBoxPathsKeepBlankLinesAndSavedPixels) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const FS_RECTF rect{40, 180, 240, 100};
		    const std::u16string text = u"First (line) \\\n\nFirst (line) \\";
		    const auto model =
		        pdf::DocumentEditTest::multiline(doc, rect, text);
		    ASSERT_TRUE(model);
		    ASSERT_EQ(model->paintedLines, 2);
		    const auto blank = doc.render(0, 400, 400);
		    ASSERT_TRUE(blank);
		    ASSERT_TRUE(pdf::DocumentEditTest::freeText(doc, rect, text));
		    pdf::DocumentEditTest::checkMultiline(doc, rect, *model, true);
		    const auto raster = doc.render(0, 400, 400);
		    const auto printed = doc.printRaster(0, 400, 400, 0, 0, 400, 400);
		    ASSERT_TRUE(raster && printed);
		    EXPECT_NE(raster->pixels, blank->pixels);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto reopened = pdf::openDocument(*bytes);
		    ASSERT_TRUE(reopened.document);
		    pdf::DocumentEditTest::checkMultiline(*reopened.document, rect,
		                                          *model, false);
		    const auto notes = reopened.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 1);
		    EXPECT_EQ(notes->entries[0].contents,
		              "First (line) \\\n\nFirst (line) \\");
		    const auto saved = reopened.document->render(0, 400, 400);
		    const auto savedPrint =
		        reopened.document->printRaster(0, 400, 400, 0, 0, 400, 400);
		    ASSERT_TRUE(saved && savedPrint);
		    EXPECT_EQ(saved->pixels, raster->pixels);
		    EXPECT_EQ(savedPrint->pixels, printed->pixels);
	    })
	    .get();
}

TEST(Annotations, MultilineBoxPathsRefuseWrappingAndVerticalOverflow) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(opened.document);
		    const auto& doc = *opened.document;
		    const FS_RECTF rect{40, 180, 240, 100};
		    EXPECT_FALSE(pdf::DocumentEditTest::multiline(
		        doc, {40, 180, 60, 100}, u"First\nSecond"));
		    EXPECT_FALSE(pdf::DocumentEditTest::multiline(
		        doc, {40, 130, 240, 100}, u"First\n\nSecond"));
		    for (const auto* text :
		         {u" \n ", u"First\r\nSecond", u"First\ncaf\u00e9",
		          u"1\n2\n3\n4\n5\n6\n7\n8\n9"}) {
			    EXPECT_FALSE(pdf::DocumentEditTest::multiline(doc, rect, text));
		    }
	    })
	    .get();
}

TEST(Annotations, ExplicitBoxRowLimitKeepsOriginalBytesOnRefusal) {
	pdf::Engine engine;
	engine
	    .run([] {
		    auto opened = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    constexpr auto layout = pdf::TextBoxLayout::ExplicitLines;
		    const pdf::AnnotationBounds bounds{40, 40, 240, 230};
		    const auto revision = doc.annotationRevision();
		    const auto original = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(original);
		    for (const auto* text :
		         {u" \n ", u"First\r\nSecond", u"First\ncaf\u00e9",
		          u"1\n2\n3\n4\n5\n6\n7\n8\n9"}) {
			    EXPECT_FALSE(
			        doc.addTextBox(0, bounds, text, {}, revision, {}, layout));
			    EXPECT_EQ(doc.annotationRevision(), revision);
			    EXPECT_EQ(doc.serialize(1024 * 1024), original);
		    }
		    ASSERT_TRUE(doc.addTextBox(0, bounds, u"1\n2\n3\n4\n5\n6\n7\n8", {},
		                               revision, {}, layout));
	    })
	    .get();
}

TEST(Annotations, ExplicitLineBoxesEditResizeAndReopenOnCroppedPage) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto source = editableNoteBytes(
		        "/Contents (Keep)", "Text", {},
		        "/MediaBox [0 0 400 300] /CropBox [20 30 380 270] /Rotate 90");
		    const auto opened = pdf::openDocument(source);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    constexpr auto layout = pdf::TextBoxLayout::ExplicitLines;
		    const pdf::AnnotationBounds bounds{40, 100, 240, 180};
		    EXPECT_FALSE(
		        doc.addTextBox(0, bounds, u"First\n\nSecond", u"Author", 1));
		    ASSERT_EQ(doc.addTextBox(0, bounds, u"First\n\nSecond", u"Author",
		                             1, {}, layout),
		              1);
		    ASSERT_EQ(
		        doc.addTextBox(0, {40, 190, 240, 250}, u"Neighbor", {}, 2), 2);
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 3);
		    const auto& box = notes->entries[1];
		    ASSERT_EQ(box.contents, "First\n\nSecond");
		    EXPECT_FALSE(doc.textBox(box.target));
		    EXPECT_TRUE(doc.textBox(box.target, {}, layout));
		    const auto raw = pdf::DocumentEditTest::rawBytes(doc);
		    EXPECT_EQ(doc.updateTextBox(box, u"First\n\nSecond", {}, layout),
		              false);
		    EXPECT_FALSE(doc.updateTextBox(box, u"First\n\nSecond",
		                                   pdf::TextBoxSize{200, 30}, {},
		                                   layout));
		    EXPECT_FALSE(doc.updateTextBox(box, u"First\n\nSecond",
		                                   pdf::TextBoxSize{400, 80}, {},
		                                   layout));
		    EXPECT_EQ(pdf::DocumentEditTest::rawBytes(doc), raw);
		    ASSERT_EQ(doc.updateTextBox(box, u"Updated\n\nLast (line)",
		                                pdf::TextBoxSize{250, 65}, {}, layout),
		              true);
		    EXPECT_FALSE(doc.updateTextBox(box, u"Stale\nText", {}, layout));
		    const auto print = doc.printRaster(0, 400, 400, 0, 0, 400, 400);
		    const auto saved = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(saved && print);
		    const auto reopened = pdf::openDocument(*saved);
		    ASSERT_TRUE(reopened.document);
		    const auto kept = reopened.document->annotations(0);
		    ASSERT_TRUE(kept && kept->entries.size() == 3 &&
		                kept->entries[1].bounds);
		    EXPECT_EQ(kept->entries[0].contents, "Keep");
		    EXPECT_EQ(kept->entries[1].contents, "Updated\n\nLast (line)");
		    EXPECT_EQ(kept->entries[1].author, "Author");
		    EXPECT_EQ(kept->entries[1].index, box.index);
		    EXPECT_FLOAT_EQ(kept->entries[1].bounds->left, 40);
		    EXPECT_FLOAT_EQ(kept->entries[1].bounds->top, 180);
		    EXPECT_FLOAT_EQ(kept->entries[1].bounds->right, 290);
		    EXPECT_FLOAT_EQ(kept->entries[1].bounds->bottom, 115);
		    EXPECT_EQ(kept->entries[2].contents, "Neighbor");
		    const auto raster =
		        reopened.document->printRaster(0, 400, 400, 0, 0, 400, 400);
		    ASSERT_TRUE(raster);
		    EXPECT_EQ(raster->pixels, print->pixels);
		    EXPECT_TRUE(reopened.document->textBox(kept->entries[1].target, {},
		                                           layout));
		    EXPECT_EQ(reopened.document->updateTextBox(kept->entries[1],
		                                               u"One line", {}, layout),
		              true);
	    })
	    .get();
}

TEST(Annotations, ExplicitLineBoxesRefuseChangedAppearancePlacement) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto seed = pdf::openFile(fixturePath("notes_allowed.pdf"));
		    ASSERT_TRUE(seed.document);
		    const FS_RECTF rect{40, 180, 240, 100};
		    const auto model = pdf::DocumentEditTest::multiline(
		        *seed.document, rect, u"First\n\nFirst");
		    ASSERT_TRUE(model);
		    std::string path;
		    for (char16_t c : model->stream) {
			    ASSERT_LE(c, 127);
			    path.push_back(static_cast<char>(c));
		    }
		    const auto bytes = editableNoteBytes(
		        "/F 4 /Contents (First\n\nFirst) /DA (/Helv 12 Tf 0 g) /AP << "
		        "/N 5 0 R >>",
		        "FreeText", {}, "/MediaBox [0 0 400 300]", "[40 100 240 180]",
		        "<< /Type /XObject /Subtype /Form /BBox [40 100 300 180] "
		        "/Length " +
		            std::to_string(path.size()) + " >>\nstream\n" + path +
		            "endstream");
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 1);
		    pdf::DocumentEditTest::checkMultiline(doc, rect, *model, false);
		    constexpr auto layout = pdf::TextBoxLayout::ExplicitLines;
		    EXPECT_FALSE(doc.textBox(notes->entries[0].target, {}, layout));
		    EXPECT_FALSE(doc.updateTextBox(notes->entries[0],
		                                   u"Replacement\nText", {}, layout));
		    EXPECT_EQ(doc.annotationRevision(), 1u);
		    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(Annotations, ExplicitLineBoxCancellationKeepsEncryptedSavepoint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto seed =
		        pdf::openFile(fixturePath("password.pdf"), "sable");
		    ASSERT_TRUE(seed.document);
		    constexpr auto layout = pdf::TextBoxLayout::ExplicitLines;
		    ASSERT_TRUE(seed.document->addTextBox(
		        0, {40, 100, 240, 180}, u"First\nSecond", {}, 1, {}, layout));
		    const auto bytes = seed.document->serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    EXPECT_EQ(pdf::openDocument(*bytes).error,
		              pdf::Error::PasswordRequired);
		    const auto completed = pdf::openDocument(*bytes, "sable");
		    ASSERT_TRUE(completed.document);
		    const auto notes = completed.document->annotations(0);
		    ASSERT_TRUE(notes && !notes->entries.empty());
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateTextBox(
		                  notes->entries.back(), u"Changed\nSecond",
		                  [&] {
			                  ++checkpoints;
			                  return false;
		                  },
		                  layout),
		              true);
		    ASSERT_GT(checkpoints, 10);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    const auto opened = pdf::openDocument(*bytes, "sable");
			    ASSERT_TRUE(opened.document);
			    const auto snapshot = opened.document->annotations(0);
			    ASSERT_TRUE(snapshot && !snapshot->entries.empty());
			    int calls = 0;
			    EXPECT_FALSE(opened.document->updateTextBox(
			        snapshot->entries.back(), u"Changed\nSecond",
			        [&] { return ++calls >= stop; }, layout));
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(Annotations, ExplicitLineBoxesRefusePagesBeyondTheRenderProofBudget) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto source = editableNoteBytes(
		        "/Contents (Keep)", "Text", {}, "/MediaBox [0 0 18000 300]");
		    const auto opened = pdf::openDocument(source);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const pdf::AnnotationBounds bounds{40, 100, 240, 180};
		    EXPECT_FALSE(doc.addTextBox(0, bounds, u"First\nSecond", {}, 1, {},
		                                pdf::TextBoxLayout::ExplicitLines));
		    EXPECT_EQ(doc.serialize(1024 * 1024), source);
		    EXPECT_EQ(doc.annotationRevision(), 1u);
		    EXPECT_EQ(doc.addTextBox(0, bounds, u"One line", {}, 1), 1);
	    })
	    .get();
}
