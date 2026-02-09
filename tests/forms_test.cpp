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
#include <cstdio>
#include <algorithm>
#include <array>
#include "pdf/document.h"
#include "pdf/engine.h"
#include "rotation.h"
#include "pdf_test_files.h"

namespace pdf {
struct DocumentFormTest {
	static bool hasAppearance(const Document& document) {
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		const auto annotation = FPDFPage_GetAnnot(page, 0);
		const bool result = FPDFAnnot_HasKey(annotation, "AP") != 0;
		FPDFPage_CloseAnnot(annotation);
		FPDF_ClosePage(page);
		return result;
	}
};
} // namespace pdf

namespace {
struct FormPageOptions {
	int rotation = 90;
	bool appearances = true, reference = false;
	std::string boxes = "/MediaBox [0 0 200 300] /CropBox [20 40 180 260]";
};
std::vector<std::uint8_t> formBytes(std::vector<std::string> fields,
                                    std::string parent = {}, int repeat = 1,
                                    FormPageOptions view = {}) {
	std::string refs;
	for (std::size_t i = 0; i < fields.size(); ++i) {
		for (int copy = 0; copy < repeat; ++copy) {
			refs += std::to_string(i + 9) + " 0 R ";
		}
	}
	const auto roots = parent.empty() ? refs : "5 0 R";
	std::vector<std::string> objects{
	    "<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R >>",
	    "<< /Type /Pages /Kids [4 0 R] /Count 1 >>",
	    "<< /Fields [" + roots + "] /NeedAppearances " +
	        (view.appearances ? "true " : "false ") +
	        "/DA (/Helv 10 Tf 0 g) /DR << /Font << /Helv "
	        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >> >>",
	    "<< /Type /Page /Parent 2 0 R " + view.boxes + " /Rotate " +
	        std::to_string(view.rotation) + " /Annots [" + refs + "] " +
	        (view.reference
	             ? "/Contents " + std::to_string(fields.size() + 9) + " 0 R "
	             : "") +
	        ">>",
	    "<< " + parent + " /Kids [" + refs + "] >>",
	    "<FEFF004E03A9D83DDE00>",
	    "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] /Length 0 >>\n"
	    "stream\nendstream",
	    "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] /Length 0 >>\n"
	    "stream\nendstream"};
	for (const auto& field : fields) {
		objects.push_back("<< /Type /Annot /P 4 0 R " + field +
		                  (parent.empty() ? "" : " /Parent 5 0 R") + " >>");
	}
	if (view.reference) {
		const std::string paint = "0 0 0 rg 40 50 50 20 re f";
		objects.push_back("<< /Length " + std::to_string(paint.size()) +
		                  " >>\nstream\n" + paint + "\nendstream");
	}
	std::string data = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	for (const auto& object : objects) {
		offsets.push_back(data.size());
		data +=
		    std::to_string(offsets.size()) + " 0 obj\n" + object + "\nendobj\n";
	}
	const auto xref = data.size();
	data += "xref\n0 " + std::to_string(objects.size() + 1) +
	        "\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		data += line;
	}
	data += "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
	        " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
	return {data.begin(), data.end()};
}
const std::string WIDGET = "/Subtype /Widget /Rect [40 50 90 70] ";
} // namespace

TEST(FormFields, InheritedSnapshotsOutliveTheDocumentAndEngine) {
	std::optional<pdf::FormFields> result;
	{
		pdf::Engine engine;
		result = engine
		             .run([] {
			             const auto opened = pdf::openDocument(formBytes(
			                 {WIDGET, WIDGET},
			                 "/FT /Tx /T 6 0 R /Ff 3 /V <FEFF007600E9>"));
			             return opened.document ? opened.document->formFields(0)
			                                    : std::nullopt;
		             })
		             .get();
	}
	ASSERT_TRUE(result);
	EXPECT_FALSE(result->limited);
	ASSERT_EQ(result->entries.size(), 2u);
	EXPECT_EQ(result->page, 0);
	EXPECT_NE(result->revision, 0u);
	for (int i = 0; i < 2; ++i) {
		const auto& field = result->entries[i];
		EXPECT_EQ(field.index, i);
		EXPECT_EQ(field.type, pdf::FormFieldType::Text);
		EXPECT_EQ(field.flags, 3);
		EXPECT_EQ(field.name, "N\u03a9\U0001f600");
		EXPECT_EQ(field.value, "v\u00e9");
		ASSERT_TRUE(field.bounds);
		EXPECT_FLOAT_EQ(field.bounds->left, 40);
		EXPECT_FLOAT_EQ(field.bounds->top, 70);
	}
}

TEST(FormFields, KeepsControlTypesAndAnnotationIndices) {
	pdf::Engine engine;
	engine
	    .run([] {
		    std::vector<std::string> fields{"/Subtype /Text /Rect [1 2 3 4]"};
		    for (const auto& type :
		         {"/Btn /Ff 65536",
		          "/Btn /V /Yes /AS /Yes "
		          "/AP << /N << /Yes 8 0 R /Off 7 0 R >> >>",
		          "/Btn /Ff 32768 /V /ChoiceA /AS /ChoiceA "
		          "/AP << /N << /ChoiceA 8 0 R /Off 7 0 R >> >>",
		          "/Ch /Ff 131072 /V (combo) /Opt [(combo)]",
		          "/Ch /V (list) /Opt [(list)]", "/Tx /V (text)", "/Sig"}) {
			    fields.push_back(WIDGET + "/FT " + type + " /T (field" +
			                     std::to_string(fields.size()) + ")");
		    }
		    const auto opened = pdf::openDocument(formBytes(fields));
		    ASSERT_TRUE(opened.document);
		    const auto result = opened.document->formFields(0);
		    ASSERT_TRUE(result);
		    ASSERT_EQ(result->entries.size(), 7u);
		    const char* values[] = {"",     "Yes",  "ChoiceA", "combo",
		                            "list", "text", ""};
		    for (int i = 0; i < 7; ++i) {
			    EXPECT_EQ(result->entries[i].index, i + 1);
			    EXPECT_EQ(static_cast<int>(result->entries[i].type), i + 1);
			    EXPECT_EQ(result->entries[i].value, values[i]);
		    }
		    const auto annotations = opened.document->annotations(0);
		    ASSERT_TRUE(annotations);
		    ASSERT_TRUE(opened.document->removeAnnotation(
		        annotations->entries[0].target));
		    const auto shifted = opened.document->formFields(0);
		    ASSERT_TRUE(shifted);
		    ASSERT_FALSE(shifted->entries.empty());
		    EXPECT_EQ(shifted->entries.front().index, 0);
		    EXPECT_NE(shifted->revision, result->revision);
	    })
	    .get();
}

TEST(FormFields, BoundsStringsAndPageScan) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(formBytes(
		        {WIDGET + "/FT /Tx /T (" + std::string(33000, 'a') + ") /V ()",
		         "/Subtype /Widget /Rect [0 0 0 10] /FT /Tx /T (bad)"}));
		    const auto result = opened.document->formFields(0);
		    ASSERT_TRUE(result);
		    EXPECT_TRUE(result->limited);
		    ASSERT_EQ(result->entries.size(), 2u);
		    EXPECT_TRUE(result->entries[0].name.empty());
		    EXPECT_FALSE(result->entries[1].bounds);
		    const auto repeated = pdf::openDocument(
		        formBytes({WIDGET + "/FT /Tx /T (repeat)"}, {}, 4097));
		    const auto scan = repeated.document->formFields(0);
		    ASSERT_TRUE(scan);
		    EXPECT_TRUE(scan->limited);
		    EXPECT_EQ(scan->entries.size(), 4096u);
		    std::vector<std::string> large;
		    for (int i = 0; i < 18; ++i) {
			    large.push_back(WIDGET + "/FT /Tx /T (" +
			                    std::string(31000, char('a' + i)) + ")");
		    }
		    const auto aggregate = pdf::openDocument(formBytes(large));
		    const auto limited = aggregate.document->formFields(0);
		    ASSERT_TRUE(limited);
		    EXPECT_TRUE(limited->limited);
		    ASSERT_EQ(limited->entries.size(), 18u);
		    EXPECT_EQ(limited->entries.front().name.size(), 31000u);
		    EXPECT_TRUE(limited->entries.back().name.empty());
	    })
	    .get();
}

TEST(FormFields, RejectsInvalidPagesAndCancellationWithoutInventingFields) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto plain = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(plain.document);
		    EXPECT_FALSE(plain.document->formFields(-1));
		    EXPECT_FALSE(plain.document->formFields(3));
		    const auto empty = plain.document->formFields(0);
		    ASSERT_TRUE(empty);
		    EXPECT_TRUE(empty->entries.empty());
		    const auto opened =
		        pdf::openDocument(formBytes({WIDGET + "/FT /Tx"}));
		    EXPECT_FALSE(opened.document->formFields(0, [] { return true; }));
		    int calls = 0;
		    EXPECT_FALSE(
		        opened.document->formFields(0, [&] { return ++calls > 6; }));
	    })
	    .get();
}

TEST(FormFields, DoesNotRegenerateLiveAppearancesOrChangeCopies) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes({WIDGET}, "/FT /Tx /T 6 0 R /V (value)");
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    EXPECT_FALSE(
		        pdf::DocumentFormTest::hasAppearance(*opened.document));
		    for (int repeat = 0; repeat < 3; ++repeat) {
			    ASSERT_TRUE(opened.document->formFields(0));
		    }
		    EXPECT_FALSE(
		        pdf::DocumentFormTest::hasAppearance(*opened.document));
		    EXPECT_FALSE(opened.document->modified());
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(FormFields, DisplayBoundsMatchCropRotationAndRenderedReference) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::array<pdf::AnnotationRect, 4> expected{
		        {{1. / 8, 19. / 22, 7. / 16, 21. / 22},
		         {1. / 22, 1. / 8, 3. / 22, 7. / 16},
		         {9. / 16, 1. / 22, 7. / 8, 3. / 22},
		         {19. / 22, 9. / 16, 21. / 22, 7. / 8}}};
		    for (int intrinsic = 0; intrinsic < 4; ++intrinsic) {
			    SCOPED_TRACE(intrinsic);
			    const auto opened = pdf::openDocument(
			        formBytes({WIDGET + "/FT /Tx /T (field) /V (value)"}, {}, 1,
			                  {.rotation = intrinsic * 90, .reference = true}));
			    ASSERT_TRUE(opened.document);
			    const auto fields = opened.document->formFields(0);
			    ASSERT_TRUE(fields);
			    EXPECT_FALSE(fields->limited);
			    ASSERT_EQ(fields->entries.size(), 1u);
			    ASSERT_TRUE(fields->entries[0].displayBounds);
			    const auto rect = *fields->entries[0].displayBounds;
			    const auto base = expected[intrinsic];
			    EXPECT_NEAR(rect.left, base.left, 2e-6);
			    EXPECT_NEAR(rect.top, base.top, 2e-6);
			    EXPECT_NEAR(rect.right, base.right, 2e-6);
			    EXPECT_NEAR(rect.bottom, base.bottom, 2e-6);
			    const auto page = opened.document->pageSize(0);
			    ASSERT_TRUE(page);
			    for (int extra = 0; extra < 4; ++extra) {
				    SCOPED_TRACE(extra);
				    const auto shown = helpers::rotateRect(
				        {rect.left, rect.top, rect.right - rect.left,
				         rect.bottom - rect.top},
				        extra);
				    const auto size = helpers::rotatedSize(
				        {page->width, page->height}, extra);
				    for (const int scale : {1, 3}) {
					    SCOPED_TRACE(scale);
					    const int width = static_cast<int>(size.width) * scale;
					    const int height =
					        static_cast<int>(size.height) * scale;
					    const auto bitmap =
					        opened.document->render(0, width, height, extra);
					    ASSERT_TRUE(bitmap);
					    int left = width, top = height, right = 0, bottom = 0;
					    for (int y = 0; y < height; ++y) {
						    for (int x = 0; x < width; ++x) {
							    const auto* p = bitmap->pixels.data() +
							                    y * bitmap->stride + x * 4;
							    if (p[0] < 32 && p[1] < 32 && p[2] < 32) {
								    left = (std::min)(left, x);
								    top = (std::min)(top, y);
								    right = (std::max)(right, x + 1);
								    bottom = (std::max)(bottom, y + 1);
							    }
						    }
					    }
					    ASSERT_LT(left, right);
					    ASSERT_LT(top, bottom);
					    EXPECT_NEAR(left, shown.x * width, 2);
					    EXPECT_NEAR(top, shown.y * height, 2);
					    EXPECT_NEAR(right, (shown.x + shown.width) * width, 2);
					    EXPECT_NEAR(bottom, (shown.y + shown.height) * height,
					                2);
				    }
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, ClipsBeforeConvertingRemoteOrOffPageRectangles) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(formBytes(
		        {"/Subtype /Widget /FT /Tx /T (partial) /Rect [-100 30 40 70]",
		         "/Subtype /Widget /FT /Tx /T (outside) /Rect [180 50 190 70]",
		         "/Subtype /Widget /FT /Tx /T (remote) /Rect ["
		         "-300000000000000000000000000000000000000.0 "
		         "-300000000000000000000000000000000000000.0 "
		         "300000000000000000000000000000000000000.0 "
		         "300000000000000000000000000000000000000.0]"},
		        {}, 1, {.rotation = 0, .appearances = false}));
		    ASSERT_TRUE(opened.document);
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields);
		    EXPECT_FALSE(fields->limited);
		    ASSERT_EQ(fields->entries.size(), 3u);
		    const auto& partial = fields->entries[0];
		    ASSERT_TRUE(partial.bounds && partial.displayBounds);
		    EXPECT_FLOAT_EQ(partial.bounds->left, -100);
		    EXPECT_NEAR(partial.displayBounds->left, 0, 2e-6);
		    EXPECT_NEAR(partial.displayBounds->right, 1. / 8, 2e-6);
		    EXPECT_NEAR(partial.displayBounds->top, 19. / 22, 2e-6);
		    EXPECT_NEAR(partial.displayBounds->bottom, 1, 2e-6);
		    EXPECT_TRUE(fields->entries[1].bounds);
		    EXPECT_FALSE(fields->entries[1].displayBounds);
		    const auto& remote = fields->entries[2];
		    ASSERT_TRUE(remote.bounds && remote.displayBounds);
		    EXPECT_LT(remote.bounds->left, -1e30f);
		    EXPECT_GT(remote.bounds->right, 1e30f);
		    EXPECT_NEAR(remote.displayBounds->left, 0, 2e-6);
		    EXPECT_NEAR(remote.displayBounds->top, 0, 2e-6);
		    EXPECT_NEAR(remote.displayBounds->right, 1, 2e-6);
		    EXPECT_NEAR(remote.displayBounds->bottom, 1, 2e-6);
	    })
	    .get();
}

TEST(FormFields, VisibilityFlagsStaySeparateFromFieldFlags) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::array<std::pair<int, bool>, 11> flags{{{0, true},
		                                                      {1, false},
		                                                      {2, false},
		                                                      {4, true},
		                                                      {8, true},
		                                                      {16, true},
		                                                      {32, false},
		                                                      {36, false},
		                                                      {64, true},
		                                                      {128, true},
		                                                      {256, true}}};
		    std::vector<std::string> entries;
		    for (const auto& item : flags) {
			    const int flag = item.first;
			    entries.push_back(WIDGET + "/FT /Tx /Ff 3 /T (field" +
			                      std::to_string(flag) + ") /F " +
			                      std::to_string(flag));
		    }
		    const auto opened = pdf::openDocument(formBytes(entries));
		    ASSERT_TRUE(opened.document);
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields);
		    EXPECT_FALSE(fields->limited);
		    ASSERT_EQ(fields->entries.size(), flags.size());
		    for (std::size_t i = 0; i < flags.size(); ++i) {
			    const auto& field = fields->entries[i];
			    EXPECT_EQ(field.flags, 3);
			    EXPECT_EQ(field.annotationFlags, flags[i].first);
			    EXPECT_TRUE(field.bounds);
			    EXPECT_EQ(field.displayBounds.has_value(), flags[i].second);
		    }
	    })
	    .get();
}

TEST(FormFields, InvalidPageOrFieldBoundsHaveNoDisplayRectangle) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::array<std::pair<std::string, bool>, 3> pages{
		        {{"/MediaBox [0 0 200 300] /CropBox [20 40 180 260]", true},
		         {"/MediaBox [0 0 0 0] /CropBox [0 0 0 0]", true},
		         {"/MediaBox [0 0 200 300] /CropBox [220 340 280 380]",
		          false}}};
		    // an empty media box falls back to Letter; a disjoint crop is
		    // empty. see:
		    // https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/page/cpdf_page.cpp
		    for (const auto& [boxes, usable] : pages) {
			    SCOPED_TRACE(boxes);
			    const auto opened = pdf::openDocument(formBytes(
			        {"/Subtype /Widget /FT /Tx /T (string) /Rect (bad)",
			         "/Subtype /Widget /FT /Tx /T (empty) /Rect [40 50 40 70]",
			         WIDGET + "/FT /Tx /T (valid)"},
			        {}, 1, {.appearances = false, .boxes = boxes}));
			    ASSERT_TRUE(opened.document);
			    const auto fields = opened.document->formFields(0);
			    ASSERT_TRUE(fields);
			    EXPECT_TRUE(fields->limited);
			    ASSERT_EQ(fields->entries.size(), 3u);
			    EXPECT_FALSE(fields->entries[0].bounds);
			    EXPECT_FALSE(fields->entries[0].displayBounds);
			    EXPECT_FALSE(fields->entries[1].bounds);
			    EXPECT_FALSE(fields->entries[1].displayBounds);
			    ASSERT_TRUE(fields->entries[2].bounds);
			    EXPECT_EQ(fields->entries[2].displayBounds.has_value(), usable);
		    }
	    })
	    .get();
}
