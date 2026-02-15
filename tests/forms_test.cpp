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
#include <fpdf_signature.h>
#include <cstdio>
#include <climits>
#include <algorithm>
#include <array>
#include "pdf/document.h"
#include "pdf/engine.h"
#include "rotation.h"
#include "pdf_test_files.h"

namespace pdf {
struct DocumentFormTest {
	static int signatures(const Document& document) {
		return FPDF_GetSignatureCount(document.m_handle);
	}
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
	bool appearances = true, reference = false, outline = false,
	     nestedSignature = false, painted = false, text = false;
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
	auto roots = parent.empty() ? refs : "5 0 R";
	if (view.nestedSignature) {
		roots += " " +
		         std::to_string(fields.size() + 9 + (view.reference ? 1 : 0) +
		                        (view.outline ? 2 : 0)) +
		         " 0 R";
	}
	std::vector<std::string> objects{
	    "<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R " +
	        (view.outline ? "/Outlines " +
	                            std::to_string(fields.size() + 9 +
	                                           (view.reference ? 1 : 0)) +
	                            " 0 R "
	                      : "") +
	        ">>",
	    "<< /Type /Pages /Kids [4 0 R] /Count 1 >>",
	    "<< /Fields [" + roots + "] /NeedAppearances " +
	        (view.appearances ? "true " : "false ") +
	        "/DA (/Helv 10 Tf 0 g) /DR << /Font << /Helv "
	        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >> >>",
	    "<< /Type /Page /Parent 2 0 R " + view.boxes + " /Rotate " +
	        std::to_string(view.rotation) + " /Annots [" + refs + "] " +
	        (view.text ? "/Resources << /Font << /Helv << /Type /Font "
	                     "/Subtype /Type1 /BaseFont /Helvetica >> >> >> "
	                   : "") +
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
	if (view.painted) {
		const std::string paint = "1 0 0 rg 0 0 50 20 re f";
		objects[7] = "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] "
		             "/Length " +
		             std::to_string(paint.size()) + " >>\nstream\n" + paint +
		             "\nendstream";
	}
	if (view.reference) {
		const std::string paint =
		    view.text ? "BT /Helv 10 Tf 30 100 Td (Static text) Tj ET"
		              : "0 0 0 rg 40 50 50 20 re f";
		objects.push_back("<< /Length " + std::to_string(paint.size()) +
		                  " >>\nstream\n" + paint + "\nendstream");
	}
	if (view.outline) {
		const auto root = objects.size() + 1;
		objects.push_back("<< /Type /Outlines /First " +
		                  std::to_string(root + 1) + " 0 R /Last " +
		                  std::to_string(root + 1) + " 0 R /Count 1 >>");
		objects.push_back("<< /Title (Go) /Parent " + std::to_string(root) +
		                  " 0 R /Dest [4 0 R /Fit] >>");
	}
	if (view.nestedSignature) {
		const auto root = objects.size() + 1;
		objects.push_back("<< /T (group) /Kids [" + std::to_string(root + 1) +
		                  " 0 R] >>");
		objects.push_back("<< /T (nested) /FT /S#69g /Parent " +
		                  std::to_string(root) + " 0 R /Kids [" +
		                  std::to_string(root + 2) + " 0 R] >>");
		objects.push_back("<< /T (hidden) /Parent " + std::to_string(root + 1) +
		                  " 0 R /V << /Type /S#69g /Contents <00> "
		                  "/ByteRange [0 1 2 3] /Reference [<< "
		                  "/TransformMethod /D#6fcMDP "
		                  "/TransformParams << /P 1 >> >>] >> >>");
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
const std::array<std::uint8_t, 4> RED{0, 0, 255, 255},
    WHITE{255, 255, 255, 255};
std::array<std::uint8_t, 4> pixel(const pdf::Bitmap& image, int x, int y) {
	const auto p = image.pixels.data() + y * image.stride + x * 4;
	return {p[0], p[1], p[2], p[3]};
}
struct FormCanvas {
	HDC dc = CreateCompatibleDC(nullptr);
	HBITMAP bitmap = nullptr;
	HGDIOBJ previous = nullptr;
	FormCanvas(int width, int height) {
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		void* pixels{};
		bitmap =
		    CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
		if (bitmap) {
			previous = SelectObject(dc, bitmap);
			PatBlt(dc, 0, 0, width, height, WHITENESS);
		}
	}
	~FormCanvas() {
		if (previous) {
			SelectObject(dc, previous);
		}
		if (bitmap) {
			DeleteObject(bitmap);
		}
		if (dc) {
			DeleteDC(dc);
		}
	}
};
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

TEST(FormFields, PlainTextWritesRoundTripAndClearWithoutChangingOldSnapshots) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes({WIDGET + "/FT /Tx /T (field) /V (old)"});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    auto& document = *opened.document;
		    EXPECT_TRUE(document.canFillForms());
		    const auto before = document.formFields(0);
		    ASSERT_TRUE(before);
		    EXPECT_EQ(document.updateTextField(0, 0, u"old", u"old",
		                                       before->revision),
		              false);
		    EXPECT_FALSE(document.modified());
		    EXPECT_EQ(document.serialize(1024 * 1024), bytes);
		    EXPECT_FALSE(pdf::DocumentFormTest::hasAppearance(document));
		    ASSERT_EQ(document.updateTextField(0, 0, u"old", u"caf\u00e9",
		                                       before->revision),
		              true);
		    EXPECT_TRUE(document.modified());
		    EXPECT_GT(document.annotationRevision(), before->revision);
		    EXPECT_EQ(before->entries[0].value, "old");
		    EXPECT_TRUE(pdf::DocumentFormTest::hasAppearance(document));
		    const auto saved = document.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(saved);
		    const auto reopened = pdf::openDocument(*saved);
		    ASSERT_TRUE(reopened.document);
		    const auto fields = reopened.document->formFields(0);
		    ASSERT_TRUE(fields);
		    ASSERT_EQ(fields->entries.size(), 1u);
		    EXPECT_EQ(fields->entries[0].value, "caf\u00e9");
		    EXPECT_FALSE(document.updateTextField(0, 0, u"caf\u00e9", u"stale",
		                                          before->revision));
		    EXPECT_FALSE(document.updateTextField(
		        0, 0, u"wrong", u"stale", document.annotationRevision()));
		    EXPECT_EQ(document.updateTextField(0, 0, u"caf\u00e9", u"",
		                                       document.annotationRevision()),
		              true);
		    const auto cleared = document.formFields(0);
		    ASSERT_TRUE(cleared);
		    EXPECT_EQ(cleared->entries[0].value, "");
	    })
	    .get();
}

TEST(FormFields, RefusedOrTruncatedTextWritesKeepOriginalBytesAndRevision) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const std::string extra :
		         {"/MaxLen 3", "/Ff 1", "/Ff 4096", "/Ff 8192", "/Ff 1048576",
		          "/Ff 16777216", "/Ff 33554432", "/F 2", "/F 32", "/F 64",
		          "/F 128", "/F 512", "/AA << /Fo << /S /ResetForm >> >>",
		          "/A <<>>"}) {
			    SCOPED_TRACE(extra);
			    const auto bytes = formBytes(
			        {WIDGET + "/FT /Tx /T (field) /V (old) " + extra});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(opened.document->updateTextField(
			        0, 0, u"old", u"long value", 1));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
			    EXPECT_FALSE(
			        pdf::DocumentFormTest::hasAppearance(*opened.document));
		    }
		    const auto bytes =
		        formBytes({WIDGET + "/FT /Tx /T (field) /V (old)"});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    for (const auto& text : std::array<std::u16string, 5>{
		             u"line\nbreak", u"line\rbreak", std::u16string(1, 0),
		             std::u16string(1, 0xD800), std::u16string(32768, u'x')}) {
			    EXPECT_FALSE(
			        opened.document->updateTextField(0, 0, u"old", text, 1));
		    }
		    EXPECT_FALSE(
		        opened.document->updateTextField(0, 0, u"old", u"new", 0));
		    EXPECT_FALSE(
		        opened.document->updateTextField(-1, 0, u"old", u"new", 1));
		    EXPECT_FALSE(
		        opened.document->updateTextField(0, 99, u"old", u"new", 1));
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(FormFields, CancelingEveryTransactionCheckpointKeepsLiveBytes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes({WIDGET + "/FT /Tx /T (field) /V (old)"});
		    const auto completed = pdf::openDocument(bytes);
		    ASSERT_TRUE(completed.document);
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateTextField(0, 0, u"old", u"new",
		                                                  1,
		                                                  [&] {
			                                                  ++checkpoints;
			                                                  return false;
		                                                  }),
		              true);
		    ASSERT_GT(checkpoints, 10);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    SCOPED_TRACE(stop);
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    int calls = 0;
			    EXPECT_FALSE(opened.document->updateTextField(
			        0, 0, u"old", u"new", 1, [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
			    EXPECT_FALSE(
			        pdf::DocumentFormTest::hasAppearance(*opened.document));
		    }
	    })
	    .get();
}

TEST(FormFields, TextTransactionPreservesEarlierUnsavedNotes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(
		        formBytes({WIDGET + "/FT /Tx /T (field) /V (old)"}, {}, 1,
		                  {.outline = true}));
		    ASSERT_TRUE(opened.document);
		    const auto outline = opened.document->outline();
		    ASSERT_TRUE(outline);
		    ASSERT_EQ(outline->entries.size(), 1u);
		    ASSERT_TRUE(opened.document->outlineLink(0));
		    ASSERT_EQ(
		        opened.document->addTextNote(0, 50, 60, u"Keep this note"), 1);
		    ASSERT_EQ(opened.document->updateTextField(
		                  0, 0, u"old", u"new",
		                  opened.document->annotationRevision()),
		              true);
		    EXPECT_TRUE(opened.document->modified());
		    const auto notes = opened.document->annotations(0);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 2u);
		    EXPECT_EQ(notes->entries[1].contents, "Keep this note");
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields);
		    EXPECT_EQ(fields->entries[0].value, "new");
		    const auto link = opened.document->outlineLink(0);
		    ASSERT_TRUE(link && link->destination);
		    EXPECT_EQ(link->destination->page, 0);
	    })
	    .get();
}

TEST(FormFields, RefusesSignedFormsAndInheritedActions) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto signedBytes = formBytes(
		        {WIDGET + "/FT /Tx /T (field) /V (old)",
		         WIDGET + "/FT /Sig /T (signature) /Lock << /Action /All >> /V "
		                  "<< /Type /Sig /Contents <00> /ByteRange [0 1 2 3] "
		                  "/Reference [<< /TransformMethod /DocMDP "
		                  "/TransformParams << /P 1 >> >>] >>"});
		    const auto signedDocument = pdf::openDocument(signedBytes);
		    ASSERT_TRUE(signedDocument.document);
		    ASSERT_EQ(
		        pdf::DocumentFormTest::signatures(*signedDocument.document), 1);
		    EXPECT_FALSE(signedDocument.document->updateTextField(0, 0, u"old",
		                                                          u"new", 1));
		    EXPECT_EQ(signedDocument.document->serialize(1024 * 1024),
		              signedBytes);
		    const auto actionBytes =
		        formBytes({WIDGET}, "/FT /Tx /T (field) /V (old) "
		                            "/AA << /Fo << /S /ResetForm >> >>");
		    const auto action = pdf::openDocument(actionBytes);
		    ASSERT_TRUE(action.document);
		    EXPECT_FALSE(
		        action.document->updateTextField(0, 0, u"old", u"new", 1));
		    EXPECT_EQ(action.document->serialize(1024 * 1024), actionBytes);
		    const auto sharedBytes =
		        formBytes({WIDGET, WIDGET}, "/FT /Tx /T (shared) /V (old)");
		    const auto shared = pdf::openDocument(sharedBytes);
		    ASSERT_TRUE(shared.document);
		    EXPECT_FALSE(
		        shared.document->updateTextField(0, 0, u"old", u"new", 1));
		    EXPECT_EQ(shared.document->serialize(1024 * 1024), sharedBytes);
		    const auto nestedBytes =
		        formBytes({WIDGET + "/FT /Tx /T (field) /V (old)"}, {}, 1,
		                  {.nestedSignature = true});
		    const auto nested = pdf::openDocument(nestedBytes);
		    ASSERT_TRUE(nested.document);
		    ASSERT_EQ(pdf::DocumentFormTest::signatures(*nested.document), 0);
		    EXPECT_FALSE(
		        nested.document->updateTextField(0, 0, u"old", u"new", 1));
		    EXPECT_EQ(nested.document->serialize(1024 * 1024), nestedBytes);
		    for (const auto name :
		         {"notes_allowed.pdf", "notes_denied.pdf", "restricted.pdf"}) {
			    const auto opened = pdf::openFile(fixturePath(name));
			    ASSERT_TRUE(opened.document);
			    EXPECT_EQ(opened.document->canFillForms(),
			              std::string_view(name) == "notes_allowed.pdf");
		    }
		    const auto owner =
		        pdf::openFile(fixturePath("restricted.pdf"), "nightpdf");
		    ASSERT_TRUE(owner.document);
		    EXPECT_TRUE(owner.document->canFillForms());
	    })
	    .get();
}

TEST(FormFields, CheckBoxWritesKeepCustomValuesAndSavedAppearances) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto& [name, value] :
		         std::array<std::pair<std::string, std::u16string>, 2>{
		             {{"Yes", u"Yes"}, {"Approved#20now", u"Approved now"}}}) {
			    for (int rotation : {0, 90, 180, 270}) {
				    SCOPED_TRACE(name + std::to_string(rotation));
				    const auto bytes = formBytes(
				        {WIDGET +
				         "/FT /Btn /T (box) /F 4 /Ff 6 /V /Off /AS /Off "
				         "/AP << /N << /" +
				         name + " 8 0 R /Off 7 0 R >> >>"},
				        {}, 1,
				        {.rotation = rotation,
				         .appearances = false,
				         .painted = true});
				    const auto opened = pdf::openDocument(bytes);
				    ASSERT_TRUE(opened.document);
				    auto& doc = *opened.document;
				    const auto old = doc.formFields(0);
				    ASSERT_TRUE(old && old->entries.size() == 1);
				    EXPECT_EQ(old->entries[0].checked, false);
				    EXPECT_EQ(
				        doc.updateCheckBox(0, 0, u"Off", false, old->revision),
				        false);
				    EXPECT_FALSE(doc.modified());
				    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
				    const auto off = doc.render(0, 240, 320);
				    ASSERT_TRUE(off);
				    ASSERT_EQ(
				        doc.updateCheckBox(0, 0, u"Off", true, old->revision),
				        true);
				    EXPECT_TRUE(doc.modified());
				    EXPECT_GT(doc.annotationRevision(), old->revision);
				    EXPECT_EQ(old->entries[0].checked, false);
				    const auto on = doc.render(0, 240, 320);
				    ASSERT_TRUE(on);
				    EXPECT_NE(on->pixels, off->pixels);
				    const auto saved = doc.serializeForCopy(1024 * 1024);
				    ASSERT_TRUE(saved);
				    const auto reopened = pdf::openDocument(*saved);
				    ASSERT_TRUE(reopened.document);
				    const auto fields = reopened.document->formFields(0);
				    ASSERT_TRUE(fields && fields->entries.size() == 1);
				    EXPECT_EQ(fields->entries[0].checked, true);
				    EXPECT_EQ(fields->entries[0].value,
				              name == "Yes" ? "Yes" : "Approved now");
				    EXPECT_FALSE(
				        doc.updateCheckBox(0, 0, value, false, old->revision));
				    EXPECT_FALSE(doc.updateCheckBox(0, 0, u"wrong", false,
				                                    doc.annotationRevision()));
				    const auto revision = doc.annotationRevision();
				    EXPECT_EQ(doc.updateCheckBox(0, 0, value, true, revision),
				              false);
				    EXPECT_EQ(doc.annotationRevision(), revision);
				    ASSERT_EQ(doc.updateCheckBox(0, 0, value, false, revision),
				              true);
				    const auto cleared = doc.formFields(0);
				    ASSERT_TRUE(cleared);
				    EXPECT_EQ(cleared->entries[0].checked, false);
				    EXPECT_EQ(cleared->entries[0].value, "Off");
				    const auto again = doc.render(0, 240, 320);
				    ASSERT_TRUE(again);
				    EXPECT_EQ(again->pixels, off->pixels);
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, RefusedCheckBoxesLeaveBytesAndRevisionIntact) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const std::string extra :
		         {"/Ff 1", "/Ff 32768", "/Ff 65536", "/Ff 33554432", "/F 1",
		          "/F 2", "/F 32", "/F 64", "/F 128", "/F 512", "/A <<>>",
		          "/AA << /U << /S /ResetForm >> >>", "/Opt [(other)]",
		          "/V /Yes", "/AS /Yes", "/V (Off)", "/AS (Off)", "/AP <<>>",
		          "/AP << /N << /Off 7 0 R >> >>"}) {
			    SCOPED_TRACE(extra);
			    const auto bytes =
			        formBytes({WIDGET +
			                   "/FT /Btn /T (box) /V /Off /AS /Off "
			                   "/AP << /N << /Yes 8 0 R /Off 7 0 R >> >> " +
			                   extra},
			                  {}, 1, {.appearances = false, .painted = true});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(
			        opened.document->updateCheckBox(0, 0, u"Off", true, 1));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
		    const auto bytes = formBytes(
		        {WIDGET + "/FT /Btn /T (box) /V /Off /AS /Off "
		                  "/AP << /N << /Yes 8 0 R /Off 7 0 R >> >>"});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    EXPECT_FALSE(
		        opened.document->updateCheckBox(-1, 0, u"Off", true, 1));
		    EXPECT_FALSE(
		        opened.document->updateCheckBox(0, 99, u"Off", true, 1));
		    EXPECT_FALSE(
		        opened.document->updateCheckBox(0, 0, u"Off", true, 0));
		    EXPECT_FALSE(opened.document->updateCheckBox(
		        0, 0, std::u16string(1, 0xD800), true, 1));
		    EXPECT_FALSE(
		        opened.document->updateTextField(0, 0, u"Off", u"Yes", 1));
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(FormFields, CheckBoxesRefuseMissingAndEmptyCheckedAppearances) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const std::string appearance :
		         {"123", "<<>>", "null", "7 0 R"}) {
			    for (bool checked : {false, true}) {
				    SCOPED_TRACE(appearance + (checked ? " on" : " off"));
				    const std::string state = checked ? "Yes" : "Off";
				    const auto bytes = formBytes(
				        {WIDGET + "/FT /Btn /T (box) /V /" + state + " /AS /" +
				         state + " /AP << /N << /Off 7 0 R /Yes " + appearance +
				         " >> >>"},
				        {}, 1, {.appearances = false});
				    const auto opened = pdf::openDocument(bytes);
				    ASSERT_TRUE(opened.document);
				    EXPECT_FALSE(opened.document->updateCheckBox(
				        0, 0, checked ? u"Yes" : u"Off", !checked, 1));
				    EXPECT_FALSE(opened.document->modified());
				    EXPECT_EQ(opened.document->annotationRevision(), 1u);
				    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, CancelingCheckBoxTransactionKeepsLiveBytes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = formBytes(
		        {WIDGET + "/FT /Btn /T (box) /V /Off /AS /Off "
		                  "/AP << /N << /Custom 8 0 R /Off 7 0 R >> >>"},
		        {}, 1, {.appearances = false, .painted = true});
		    const auto completed = pdf::openDocument(bytes);
		    ASSERT_TRUE(completed.document);
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateCheckBox(0, 0, u"Off", true, 1,
		                                                 [&] {
			                                                 ++checkpoints;
			                                                 return false;
		                                                 }),
		              true);
		    ASSERT_GT(checkpoints, 10);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    SCOPED_TRACE(stop);
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    int calls = 0;
			    EXPECT_FALSE(opened.document->updateCheckBox(
			        0, 0, u"Off", true, 1, [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, CheckBoxTransactionPreservesOtherFieldsNotesAndBookmarks) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(
		        formBytes({WIDGET + "/FT /Btn /T (box) /V /Yes /AS /Yes "
		                            "/AP << /N << /Yes 8 0 R /Off 7 0 R >> >>",
		                   WIDGET + "/FT /Tx /T (text) /V (keep)"},
		                  {}, 1, {.outline = true, .painted = true}));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.outline());
		    ASSERT_TRUE(doc.outlineLink(0));
		    ASSERT_EQ(doc.addTextNote(0, 50, 60, u"Keep this note"), 2);
		    const auto revision = doc.annotationRevision();
		    ASSERT_EQ(doc.updateCheckBox(0, 0, u"Yes", false, revision), true);
		    const auto fields = doc.formFields(0);
		    ASSERT_TRUE(fields && fields->entries.size() == 2);
		    EXPECT_EQ(fields->entries[0].checked, false);
		    EXPECT_FALSE(fields->entries[1].checked.has_value());
		    EXPECT_EQ(fields->entries[1].value, "keep");
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 3);
		    EXPECT_EQ(notes->entries[2].contents, "Keep this note");
		    const auto link = doc.outlineLink(0);
		    ASSERT_TRUE(link && link->destination);
		    EXPECT_EQ(link->destination->page, 0);
	    })
	    .get();
}

TEST(FormFields, CheckBoxesRefuseInheritedSharedAndSignedDocuments) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::string control =
		        WIDGET + "/V /Off /AS /Off "
		                 "/AP << /N << /Yes 8 0 R /Off 7 0 R >> >>";
		    for (const auto& bytes :
		         {formBytes({control}, "/FT /Btn /T (inherited)", 1,
		                    {.painted = true}),
		          formBytes({control, control}, "/FT /Btn /T (shared)", 1,
		                    {.painted = true}),
		          formBytes({control + " /FT /Btn /T (box)"}, {}, 1,
		                    {.nestedSignature = true, .painted = true}),
		          formBytes({control + " /FT /Btn /T (box)",
		                     WIDGET +
		                         "/FT /Sig /T (signature) /V << /Type /Sig "
		                         "/Contents <00> /ByteRange [0 1 2 3] >>"},
		                    {}, 1, {.painted = true})}) {
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(
			        opened.document->updateCheckBox(0, 0, u"Off", true, 1));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, WidgetRasterTracksIntrinsicAndViewRotationAtTwoScales) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::array<helpers::PagePoint, 4> centers{
		        {{45, 200}, {20, 45}, {115, 20}, {200, 115}}};
		    for (int intrinsic = 0; intrinsic < 4; ++intrinsic) {
			    const auto bytes = formBytes(
			        {WIDGET + "/FT /Tx /T (field) /AP << /N 8 0 R >>"}, {}, 1,
			        {.rotation = intrinsic * 90,
			         .appearances = false,
			         .painted = true});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    const auto size = opened.document->pageSize(0);
			    ASSERT_TRUE(size);
			    for (int rotation = 0; rotation < 4; ++rotation) {
				    const auto center = helpers::rotatePoint(
				        {centers[intrinsic].x / size->width,
				         centers[intrinsic].y / size->height},
				        rotation);
				    const auto rotated = helpers::rotatedSize(
				        {size->width, size->height}, rotation);
				    for (const int scale : {1, 3}) {
					    const auto image = opened.document->render(
					        0, static_cast<int>(rotated.width) * scale,
					        static_cast<int>(rotated.height) * scale, rotation);
					    ASSERT_TRUE(image);
					    EXPECT_EQ(
					        pixel(*image,
					              static_cast<int>(center.x * image->width),
					              static_cast<int>(center.y * image->height)),
					        RED);
					    EXPECT_EQ(pixel(*image, 0, 0), WHITE);
				    }
			    }
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, OptionalContentWidgetPrintWaitsForLayerSupport) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = formBytes(
		        {WIDGET +
		         "/FT /Tx /T (layered) /F 4 "
		         "/OC << /Type /OCG /Name (layer) >> /AP << /N 8 0 R >>"},
		        {}, 1, {.appearances = false, .painted = true});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    const auto print =
		        opened.document->printRaster(0, 220, 160, 0, 0, 220, 160);
		    ASSERT_TRUE(print);
		    EXPECT_EQ(pixel(*print, 20, 45), WHITE);
		    EXPECT_FALSE(opened.document->modified());
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(FormFields, WidgetVisibilityMatchesScreenAndPrintWithoutLeakingMasks) {
	pdf::Engine engine;
	engine
	    .run([] {
		    struct Case {
			    int flags;
			    bool screen, print;
		    };
		    const std::array<Case, 8> cases{{{0, true, false},
		                                     {4, true, true},
		                                     {32, false, false},
		                                     {36, false, true},
		                                     {2, false, false},
		                                     {6, false, false},
		                                     {1, false, false},
		                                     {5, false, false}}};
		    for (const std::string type : {"Tx", "Sig"}) {
			    for (const auto item : cases) {
				    SCOPED_TRACE(type + std::to_string(item.flags));
				    const auto bytes = formBytes(
				        {WIDGET + "/FT /" + type + " /T (field) /F " +
				         std::to_string(item.flags) + " /AP << /N 8 0 R >>"},
				        {}, 1, {.appearances = false, .painted = true});
				    const auto opened = pdf::openDocument(bytes);
				    ASSERT_TRUE(opened.document);
				    for (int repeat = 0; repeat < 2; ++repeat) {
					    const auto screen =
					        opened.document->render(0, 220, 160);
					    const auto print = opened.document->printRaster(
					        0, 220, 160, 0, 0, 220, 160);
					    ASSERT_TRUE(screen && print);
					    EXPECT_EQ(pixel(*screen, 20, 45),
					              item.screen ? RED : WHITE);
					    EXPECT_EQ(pixel(*print, 20, 45),
					              item.print ? RED : WHITE);
				    }
				    EXPECT_FALSE(opened.document->modified());
				    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, WidgetPrintTilesAndLegacyHdcKeepFullPageCoordinates) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(formBytes(
		        {WIDGET + "/FT /Tx /T (field) /F 4 /AP << /N 8 0 R >>"}, {}, 1,
		        {.appearances = false, .painted = true}));
		    ASSERT_TRUE(opened.document);
		    const auto full =
		        opened.document->printRaster(0, 220, 160, 0, 0, 220, 160);
		    const auto tile =
		        opened.document->printRaster(0, 220, 160, 10, 30, 35, 50);
		    ASSERT_TRUE(full && tile);
		    for (int y = 0; y < tile->height; ++y) {
			    for (int x = 0; x < tile->width; ++x) {
				    ASSERT_EQ(pixel(*tile, x, y), pixel(*full, x + 10, y + 30));
			    }
		    }
		    const auto wide = pdf::openDocument(formBytes(
		        {"/Subtype /Widget /Rect [150 50 180 70] /FT /Tx /T (wide) "
		         "/F 4 /AP << /N 8 0 R >>"},
		        {}, 1, {.rotation = 0, .appearances = false, .painted = true}));
		    ASSERT_TRUE(wide.document);
		    FormCanvas canvas(4506, 110);
		    ASSERT_TRUE(canvas.dc && canvas.bitmap);
		    ASSERT_TRUE(
		        wide.document->printPage(0, canvas.dc, 3, 5, 4500, 100));
		    EXPECT_EQ(GetPixel(canvas.dc, 4003, 95), RGB(255, 0, 0));
		    EXPECT_EQ(GetPixel(canvas.dc, 4203, 95), RGB(255, 0, 0));
		    EXPECT_EQ(GetPixel(canvas.dc, 1, 1), RGB(255, 255, 255));
		    EXPECT_FALSE(
		        wide.document->printPage(0, canvas.dc, INT_MAX, 0, 100, 100));
	    })
	    .get();
}

TEST(FormFields, WidgetDrawingKeepsLiveAppearancesAndRefreshesAfterEdits) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto widget = [](const pdf::Bitmap& image) {
			    std::vector<std::array<std::uint8_t, 4>> pixels;
			    for (int y = 20; y < 70; ++y) {
				    for (int x = 10; x < 30; ++x) {
					    pixels.push_back(pixel(image, x, y));
				    }
			    }
			    return pixels;
		    };
		    const auto bytes =
		        formBytes({WIDGET + "/FT /Tx /T (field) /V (value) /F 4"}, {},
		                  1, {.reference = true, .text = true});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    EXPECT_FALSE(
		        pdf::DocumentFormTest::hasAppearance(*opened.document));
		    const auto first = opened.document->render(0, 220, 160);
		    const auto printed =
		        opened.document->printRaster(0, 220, 160, 0, 0, 220, 160);
		    ASSERT_TRUE(first && printed);
		    const auto before = widget(*first), beforePrint = widget(*printed);
		    EXPECT_TRUE(std::any_of(before.begin(), before.end(),
		                            [](const auto& p) { return p != WHITE; }));
		    EXPECT_FALSE(
		        pdf::DocumentFormTest::hasAppearance(*opened.document));
		    EXPECT_FALSE(opened.document->modified());
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    for (int phase = 0; phase < 2; ++phase) {
			    if (phase == 0) {
				    ASSERT_EQ(
				        opened.document->addTextNote(0, 100, 100, u"note"), 1);
			    } else {
				    ASSERT_EQ(opened.document->highlightText({{0, 0}, {0, 5}}),
				              1);
			    }
			    const auto screen = opened.document->render(0, 220, 160);
			    const auto print =
			        opened.document->printRaster(0, 220, 160, 0, 0, 220, 160);
			    ASSERT_TRUE(screen && print);
			    EXPECT_EQ(widget(*screen), before);
			    EXPECT_EQ(widget(*print), beforePrint);
		    }
		    ASSERT_EQ(opened.document->updateTextField(0, 0, u"value",
		                                               u"new text", 1),
		              true);
		    const auto changed = opened.document->render(0, 220, 160);
		    ASSERT_TRUE(changed);
		    EXPECT_NE(widget(*changed), before);
		    const auto nextPrint =
		        opened.document->printRaster(0, 220, 160, 0, 0, 220, 160);
		    ASSERT_TRUE(nextPrint);
		    EXPECT_NE(widget(*nextPrint), beforePrint);
		    EXPECT_TRUE(opened.document->modified());
	    })
	    .get();
}
