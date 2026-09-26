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
#include <fpdf_edit.h>
#include <cstdio>
#include <fstream>
#include <climits>
#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include "pdf/document.h"
#include "pdf/engine.h"
#include "rotation.h"
#include "pdf_test_files.h"

namespace pdf {
struct DocumentFormTest {
	static int signatures(const Document& document) {
		return FPDF_GetSignatureCount(document.m_handle);
	}
	static const std::vector<std::uint8_t>& rawBytes(const Document& document) {
		return document.m_data;
	}
	static bool hasFieldKey(const Document& document, int index,
	                        const char* key) {
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		const auto field = FPDFPage_GetAnnot(page, index);
		EXPECT_TRUE(page && field);
		const bool result = FPDFAnnot_HasKey(field, key) != 0;
		FPDFPage_CloseAnnot(field);
		FPDF_ClosePage(page);
		return result;
	}
	static bool hasAppearance(const Document& document) {
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		const auto annotation = FPDFPage_GetAnnot(page, 0);
		const bool result = FPDFAnnot_HasKey(annotation, "AP") != 0;
		FPDFPage_CloseAnnot(annotation);
		FPDF_ClosePage(page);
		return result;
	}
	static bool hasParentAnnotation(const Document& document) {
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		const auto widget = FPDFPage_GetAnnot(page, 0);
		EXPECT_TRUE(FPDFAnnot_HasKey(widget, "Parent"));
		const auto parent = FPDFAnnot_GetLinkedAnnot(widget, "Parent");
		const bool result = parent != nullptr;
		FPDFPage_CloseAnnot(parent);
		FPDFPage_CloseAnnot(widget);
		FPDF_ClosePage(page);
		return result;
	}
	static std::vector<std::string> appearanceFonts(const Document& document,
	                                                int index) {
		std::vector<std::string> fonts;
		const auto page = FPDF_LoadPage(document.m_handle, 0);
		const auto annotation = FPDFPage_GetAnnot(page, index);
		for (int i = 0; i < FPDFAnnot_GetObjectCount(annotation); ++i) {
			const auto object = FPDFAnnot_GetObject(annotation, i);
			if (FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_TEXT) {
				continue;
			}
			const auto font = FPDFTextObj_GetFont(object);
			char name[256]{};
			const auto size =
			    FPDFFont_GetBaseFontName(font, name, sizeof(name));
			if (size > 1 && size <= sizeof(name) && name[size - 1] == 0) {
				fonts.emplace_back(name, size - 1);
			}
		}
		FPDFPage_CloseAnnot(annotation);
		FPDF_ClosePage(page);
		return fonts;
	}
};
} // namespace pdf

namespace {
struct FormPageOptions {
	int rotation = 90;
	bool appearances = true, reference = false, outline = false,
	     nestedSignature = false, painted = false, text = false;
	std::string boxes = "/MediaBox [0 0 200 300] /CropBox [20 40 180 260]";
	std::string stream;
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
		objects.push_back(view.stream.empty()
		                      ? "<< /Length " + std::to_string(paint.size()) +
		                            " >>\nstream\n" + paint + "\nendstream"
		                      : view.stream);
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
const std::string CHOICE = WIDGET + "/FT /Ch /T (choice) /V (old) /I [0] "
                                    "/Opt [[(old) (First)] [(new) (Second)]] ";
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

TEST(FormFields, OrdinaryParentsStayOutsideTheAnnotationApi) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto& parent :
		         {"/FT /Tx /Ff 16777216 /T (comb) /V (old) /MaxLen 4",
		          "/FT /Btn /T (check) /V /Wrong /Opt [(hidden)]"}) {
			    const auto bytes = formBytes({WIDGET}, parent);
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(pdf::DocumentFormTest::hasParentAnnotation(
			        *opened.document));
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, PasswordFormsRetainCredentialsPermissionsAndSavedValues) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto* name :
		         {"password_form.pdf", "password_form_denied.pdf",
		          "password_form_action.pdf"}) {
			    SCOPED_TRACE(name);
			    const auto path = fixturePath(name);
			    EXPECT_EQ(pdf::openFile(path).error,
			              pdf::Error::PasswordRequired);
			    EXPECT_EQ(pdf::openFile(path, "wrong").error,
			              pdf::Error::WrongPassword);
			    for (const auto* password : {"sable", "nightpdf"}) {
				    const auto opened = pdf::openFile(path, password);
				    ASSERT_TRUE(opened.document);
				    auto& doc = *opened.document;
				    const auto original = doc.serialize(1024 * 1024);
				    ASSERT_TRUE(original);
				    const auto fields = doc.formFields(0);
				    ASSERT_TRUE(fields && fields->entries.size() == 2);
				    EXPECT_EQ(fields->entries[0].value, "old");
				    EXPECT_EQ(fields->entries[1].value, "keep");
				    EXPECT_TRUE(doc.render(0, 400, 300));
				    EXPECT_FALSE(doc.modified());
				    EXPECT_EQ(doc.annotationRevision(), 1u);
				    EXPECT_EQ(doc.serialize(1024 * 1024), original);
				    const bool allowed =
				        std::string_view(name) != "password_form_action.pdf" &&
				        (std::string_view(name) != "password_form_denied.pdf" ||
				         std::string_view(password) == "nightpdf");
				    if (!allowed) {
					    for (const auto* value : {u"old", u"new"}) {
						    EXPECT_FALSE(
						        doc.updateTextField(0, 0, u"old", value, 1));
					    }
					    EXPECT_EQ(doc.serialize(1024 * 1024), original);
					    EXPECT_EQ(doc.annotationRevision(), 1u);
					    continue;
				    }
				    EXPECT_EQ(doc.updateTextField(0, 0, u"old", u"old", 1),
				              false);
				    EXPECT_FALSE(doc.updateTextField(0, 0, u"old",
				                                     u"too long for field", 1));
				    EXPECT_EQ(doc.serialize(1024 * 1024), original);
				    ASSERT_EQ(
				        doc.updateTextField(0, 0, u"old", u"Caf\u00e9", 1),
				        true);
				    EXPECT_FALSE(
				        doc.updateTextField(0, 1, u"keep", u"stale", 1));
				    ASSERT_EQ(
				        doc.updateTextField(0, 1, u"keep", u"neighbor", 2),
				        true);
				    const auto saved = doc.serializeForCopy(1024 * 1024);
				    ASSERT_TRUE(saved);
				    EXPECT_EQ(pdf::openDocument(*saved).error,
				              pdf::Error::PasswordRequired);
				    EXPECT_EQ(pdf::openDocument(*saved, "wrong").error,
				              pdf::Error::WrongPassword);
				    for (const auto* role : {"sable", "nightpdf"}) {
					    const auto reopened = pdf::openDocument(*saved, role);
					    ASSERT_TRUE(reopened.document);
					    const auto before = pdf::openDocument(*original, role);
					    ASSERT_TRUE(before.document);
					    EXPECT_EQ(reopened.document->canFillForms(),
					              before.document->canFillForms());
					    EXPECT_EQ(reopened.document->canCopy(),
					              before.document->canCopy());
					    EXPECT_EQ(reopened.document->canAnnotate(),
					              before.document->canAnnotate());
					    const auto kept = reopened.document->formFields(0);
					    ASSERT_TRUE(kept && kept->entries.size() == 2);
					    EXPECT_EQ(kept->entries[0].value, "Caf\u00e9");
					    EXPECT_EQ(kept->entries[1].value, "neighbor");
					    EXPECT_EQ(reopened.document->textNote(0, 2),
					              before.document->textNote(0, 2));
				    }
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, PasswordFormCancellationLeavesEveryLiveCheckpointIntact) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto path = fixturePath("password_form.pdf");
		    const auto completed = pdf::openFile(path, "sable");
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
			    const auto opened = pdf::openFile(path, "sable");
			    ASSERT_TRUE(opened.document);
			    const auto original = opened.document->serialize(1024 * 1024);
			    int calls = 0;
			    EXPECT_FALSE(opened.document->updateTextField(
			        0, 0, u"old", u"new", 1, [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), original);
		    }
	    })
	    .get();
}

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
			    if (i == 1 || i == 2) {
				    ASSERT_TRUE(result->entries[i].control);
				    EXPECT_EQ(result->entries[i].control->index, 0);
				    EXPECT_EQ(result->entries[i].control->count, 1);
			    } else {
				    EXPECT_FALSE(result->entries[i].control);
			    }
			    if (i == 3 || i == 4) {
				    ASSERT_TRUE(result->entries[i].options);
				    ASSERT_EQ(result->entries[i].options->size(), 1u);
			    } else {
				    EXPECT_FALSE(result->entries[i].options);
			    }
		    }
		    const auto annotations = opened.document->annotations(0);
		    ASSERT_TRUE(annotations);
		    ASSERT_TRUE(opened.document->removeAnnotation(
		        annotations->entries[0].target));
		    EXPECT_EQ(opened.document->writeRefusal(), pdf::WriteRefusal::None);
		    const auto shifted = opened.document->formFields(0);
		    ASSERT_TRUE(shifted);
		    ASSERT_FALSE(shifted->entries.empty());
		    EXPECT_EQ(shifted->entries.front().index, 0);
		    EXPECT_NE(shifted->revision, result->revision);
	    })
	    .get();
}

TEST(FormFields, EmptySignatureFieldsAcceptNotesDeleteAndText) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const std::string value :
		         {"", "/V null", "/V << >>",
		          "/V << /Type /Sig /Contents <> /ByteRange [0 0 0 0] >>"}) {
			    SCOPED_TRACE(value);
			    const auto opened = pdf::openDocument(
			        formBytes({WIDGET + "/FT /Tx /T (field) /V (old)",
			                   WIDGET + "/FT /Sig /T (signature) " + value}));
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    EXPECT_EQ(pdf::DocumentFormTest::signatures(doc), 1);
			    ASSERT_EQ(doc.addTextNote(0, 50, 60, u"New"), 2);
			    const auto notes = doc.annotations(0);
			    ASSERT_TRUE(notes && !notes->entries.empty());
			    ASSERT_TRUE(doc.removeAnnotation(notes->entries.back().target));
			    ASSERT_TRUE(doc.updateTextField(0, 0, u"old", u"new",
			                                    doc.annotationRevision()));
			    EXPECT_EQ(doc.writeRefusal(), pdf::WriteRefusal::None);
			    const auto saved = doc.serialize(1024 * 1024);
			    ASSERT_TRUE(saved);
			    const auto reopened = pdf::openDocument(*saved);
			    ASSERT_TRUE(reopened.document);
			    const auto fields = reopened.document->formFields(0);
			    ASSERT_TRUE(fields && fields->entries.size() == 2);
			    EXPECT_EQ(fields->entries[0].value, "new");
			    EXPECT_EQ(fields->entries[1].type,
			              pdf::FormFieldType::Signature);
		    }
	    })
	    .get();
}

TEST(FormFields, ChoiceSnapshotsOwnLabelsPositionsAndSelection) {
	std::array<std::optional<pdf::FormFields>, 2> snapshots;
	{
		pdf::Engine engine;
		for (int combo = 0; combo < 2; ++combo) {
			engine
			    .run([&, combo] {
				    const auto bytes = formBytes(
				        {WIDGET, WIDGET},
				        "/FT /Ch /T (choice) /Ff " +
				            std::to_string(combo ? 131072 : 0) +
				            " /V (export-b) /I [1] /Opt [(plain) "
				            "[(export-b) <FEFF004E03A9D83DDE00>] "
				            "[(export-c) (same)] [(export-d) (same)] ()]",
				        1, {.appearances = false});
				    const auto opened = pdf::openDocument(bytes);
				    ASSERT_TRUE(opened.document);
				    snapshots[combo] = opened.document->formFields(0);
				    EXPECT_FALSE(opened.document->modified());
				    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
			    })
			    .get();
		}
	}
	for (int combo = 0; combo < 2; ++combo) {
		ASSERT_TRUE(snapshots[combo] && !snapshots[combo]->limited);
		ASSERT_EQ(snapshots[combo]->entries.size(), 2u);
		for (const auto& field : snapshots[combo]->entries) {
			EXPECT_EQ(field.type, combo ? pdf::FormFieldType::ComboBox
			                            : pdf::FormFieldType::ListBox);
			EXPECT_FALSE(field.checked || field.control);
			EXPECT_EQ(field.value, "export-b");
			ASSERT_TRUE(field.options);
			ASSERT_EQ(field.options->size(), 5u);
			const char* labels[] = {"plain", "N\u03a9\U0001f600", "same",
			                        "same", ""};
			for (int i = 0; i < 5; ++i) {
				EXPECT_EQ((*field.options)[i].index, i);
				EXPECT_EQ((*field.options)[i].label, labels[i]);
				EXPECT_EQ((*field.options)[i].selected, i == 1);
			}
		}
	}
}

TEST(FormFields, ChoiceSelectionsKeepDuplicatesIndicesAndEditableValues) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::string duplicate =
		        "/V (dup) /Opt [[(dup) (First)] [(dup) (Second)]]";
		    const auto opened = pdf::openDocument(
		        formBytes({WIDGET + "/FT /Ch /T (duplicates) " + duplicate,
		                   WIDGET + "/FT /Ch /T (indexed) /I [1] " + duplicate,
		                   WIDGET + "/FT /Ch /T (editable) /Ff 393216 "
		                            "/V (typed) /Opt [(preset)]",
		                   WIDGET + "/FT /Ch /T (multiple) /Ff 2097152 "
		                            "/V [(a) (c)] /I [0 2] /Opt [(a) (b) (c)]"},
		                  {}, 1, {.appearances = false}));
		    ASSERT_TRUE(opened.document);
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields && fields->limited);
		    ASSERT_EQ(fields->entries.size(), 4u);
		    for (const auto& field : fields->entries) {
			    ASSERT_TRUE(field.options);
		    }
		    const auto& duplicateOptions = *fields->entries[0].options;
		    ASSERT_EQ(duplicateOptions.size(), 2u);
		    EXPECT_TRUE(duplicateOptions[0].selected);
		    EXPECT_TRUE(duplicateOptions[1].selected);
		    const auto& indexed = *fields->entries[1].options;
		    ASSERT_EQ(indexed.size(), 2u);
		    EXPECT_FALSE(indexed[0].selected);
		    EXPECT_TRUE(indexed[1].selected);
		    const auto& editable = fields->entries[2];
		    ASSERT_EQ(editable.options->size(), 1u);
		    EXPECT_EQ(editable.value, "typed");
		    EXPECT_EQ(editable.options->front().label, "preset");
		    EXPECT_FALSE(editable.options->front().selected);
		    const auto& multiple = *fields->entries[3].options;
		    ASSERT_EQ(multiple.size(), 3u);
		    EXPECT_TRUE(multiple[0].selected && multiple[2].selected);
		    EXPECT_FALSE(multiple[1].selected);
	    })
	    .get();
}

TEST(FormFields, ChoiceEmptyResultsDoNotProveRawOptionValidity) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(formBytes(
		        {WIDGET + "/FT /Ch /T (missing)",
		         WIDGET + "/FT /Ch /T (empty) /Opt []",
		         WIDGET + "/FT /Ch /T (wrong) /Opt 42",
		         WIDGET + "/FT /Ch /T (malformed) "
		                  "/Opt [() [] [(export)] [(export) 42] null 42]",
		         WIDGET + "/FT /Tx /T (text) /Opt [(ignored)]"},
		        {}, 1, {.appearances = false}));
		    ASSERT_TRUE(opened.document);
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields && !fields->limited);
		    ASSERT_EQ(fields->entries.size(), 5u);
		    for (int i = 0; i < 3; ++i) {
			    ASSERT_TRUE(fields->entries[i].options);
			    EXPECT_TRUE(fields->entries[i].options->empty());
		    }
		    ASSERT_TRUE(fields->entries[3].options);
		    ASSERT_EQ(fields->entries[3].options->size(), 6u);
		    for (const auto& option : *fields->entries[3].options) {
			    EXPECT_TRUE(option.label.empty());
		    }
		    EXPECT_FALSE(fields->entries[4].options);
	    })
	    .get();
}

TEST(FormFields, ChoiceListsShareBudgetsAndOmitIncompleteOptions) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto inspect = [](const std::vector<std::string>& fields) {
			    const auto opened = pdf::openDocument(
			        formBytes(fields, {}, 1, {.appearances = false}));
			    return opened.document ? opened.document->formFields(0)
			                           : std::nullopt;
		    };
		    std::string crowded;
		    for (int i = 0; i < 4096; ++i) {
			    crowded += "() ";
		    }
		    const auto full =
		        inspect({WIDGET + "/FT /Ch /T (full) /Opt [" + crowded + "]",
		                 WIDGET + "/FT /Ch /T (overflow) /Opt [(extra)]"});
		    ASSERT_TRUE(full && full->limited);
		    ASSERT_EQ(full->entries.size(), 2u);
		    ASSERT_TRUE(full->entries[0].options);
		    EXPECT_EQ(full->entries[0].options->size(), 4096u);
		    EXPECT_FALSE(full->entries[1].options);
		    const auto excessive = inspect(
		        {WIDGET + "/FT /Ch /T (too many) /Opt [" + crowded + "()]"});
		    ASSERT_TRUE(excessive && excessive->limited);
		    ASSERT_EQ(excessive->entries.size(), 1u);
		    EXPECT_FALSE(excessive->entries[0].options);
		    std::string aggregate;
		    for (int i = 0; i < 17; ++i) {
			    aggregate += "(" + std::string(31000, 'a') + ") ";
		    }
		    for (const auto& options :
		         {"(first) (" + std::string(33000, 'a') + ")", aggregate}) {
			    const auto limited = inspect(
			        {WIDGET + "/FT /Ch /T (partial) /Opt [" + options + "]",
			         WIDGET + "/FT /Ch /T (later) /Opt [(last)]"});
			    ASSERT_TRUE(limited && limited->limited);
			    ASSERT_EQ(limited->entries.size(), 2u);
			    EXPECT_FALSE(limited->entries[0].options);
			    ASSERT_TRUE(limited->entries[1].options);
			    ASSERT_EQ(limited->entries[1].options->size(), 1u);
			    EXPECT_EQ(limited->entries[1].options->front().label, "last");
		    }
	    })
	    .get();
}

TEST(FormFields, ChoiceCancellationKeepsLiveBytesAndSnapshotRevision) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = formBytes(
		        {WIDGET + "/FT /Ch /T (choice) /V (b) /Opt [(a) (b) (c)]"}, {},
		        1, {.appearances = false});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    int checkpoints = 0;
		    const auto fields = opened.document->formFields(0, [&] {
			    ++checkpoints;
			    return false;
		    });
		    ASSERT_TRUE(fields && !fields->limited);
		    ASSERT_EQ(fields->entries.size(), 1u);
		    ASSERT_TRUE(fields->entries[0].options);
		    ASSERT_EQ(fields->entries[0].options->size(), 3u);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    int calls = 0;
			    EXPECT_FALSE(opened.document->formFields(
			        0, [&] { return ++calls >= stop; }));
		    }
		    const auto again = opened.document->formFields(0);
		    ASSERT_TRUE(again && !again->limited);
		    ASSERT_EQ(again->entries.size(), 1u);
		    ASSERT_TRUE(again->entries[0].options);
		    ASSERT_EQ(again->entries[0].options->size(), 3u);
		    EXPECT_TRUE((*again->entries[0].options)[1].selected);
		    EXPECT_EQ(again->revision, fields->revision);
		    EXPECT_FALSE(opened.document->modified());
		    EXPECT_EQ(opened.document->annotationRevision(), fields->revision);
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(FormFields, ButtonSnapshotsOwnGroupPositionsAndExports) {
	std::array<std::optional<pdf::FormFields>, 2> snapshots;
	{
		pdf::Engine engine;
		for (int radio = 0; radio < 2; ++radio) {
			snapshots[radio] =
			    engine
			        .run([radio] {
				        const auto bytes = formBytes(
				            {WIDGET +
				                 "/AS /Off /AP << /N << /Chosen#20one 8 0 R "
				                 "/Off 7 0 R >> >>",
				             WIDGET + "/AS /Caf#E9 /AP << /N << /Caf#E9 8 0 R "
				                      "/Off 7 0 R >> >>"},
				            "/FT /Btn /Ff " +
				                std::to_string(radio ? 32768 : 0) +
				                " /T (group) /V /Caf#E9",
				            1, {.appearances = false});
				        const auto opened = pdf::openDocument(bytes);
				        if (!opened.document) {
					        ADD_FAILURE() << "button group did not open";
					        return std::optional<pdf::FormFields>{};
				        }
				        const auto revision =
				            opened.document->annotationRevision();
				        const auto fields = opened.document->formFields(0);
				        EXPECT_FALSE(opened.document->modified());
				        EXPECT_EQ(opened.document->annotationRevision(),
				                  revision);
				        EXPECT_EQ(opened.document->serialize(1024 * 1024),
				                  bytes);
				        return fields;
			        })
			        .get();
		}
	}
	for (int radio = 0; radio < 2; ++radio) {
		ASSERT_TRUE(snapshots[radio] && !snapshots[radio]->limited);
		ASSERT_EQ(snapshots[radio]->entries.size(), 2u);
		for (int i = 0; i < 2; ++i) {
			const auto& field = snapshots[radio]->entries[i];
			EXPECT_EQ(field.type, radio ? pdf::FormFieldType::RadioButton
			                            : pdf::FormFieldType::CheckBox);
			EXPECT_EQ(field.name, "group");
			EXPECT_EQ(field.value, "Caf\u00e9");
			EXPECT_EQ(field.checked, i == 1);
			ASSERT_TRUE(field.control);
			EXPECT_EQ(field.control->index, i);
			EXPECT_EQ(field.control->count, 2);
			EXPECT_EQ(field.control->exportValue,
			          i ? "Caf\u00e9" : "Chosen one");
		}
	}
}

TEST(FormFields, RadioOptionLabelsStaySeparateFromStoredIndices) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(formBytes(
		        {WIDGET + "/AS /Off /AP << /N << /A 8 0 R /Off 7 0 R >> >>",
		         WIDGET + "/AS /B /AP << /N << /B 8 0 R /Off 7 0 R >> >>"},
		        "/FT /Btn /Ff 32768 /T (choice) /V /1 "
		        "/Opt [(First label) <FEFF005300650063006F006E0064002003A9>]",
		        1, {.appearances = false}));
		    ASSERT_TRUE(opened.document);
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields && !fields->limited);
		    ASSERT_EQ(fields->entries.size(), 2u);
		    for (int i = 0; i < 2; ++i) {
			    ASSERT_TRUE(fields->entries[i].control);
			    EXPECT_EQ(fields->entries[i].value, "Second \u03a9");
			    EXPECT_EQ(fields->entries[i].checked, i == 1);
			    EXPECT_EQ(fields->entries[i].control->exportValue,
			              i ? "Second \u03a9" : "First label");
		    }
	    })
	    .get();
}

TEST(FormFields, UnisonRadiosKeepDuplicateExportsAndIndividualPositions) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(formBytes(
		        {WIDGET + "/AS /Same /AP << /N << /Same 8 0 R /Off 7 0 R >> >>",
		         WIDGET +
		             "/AS /Same /AP << /N << /Same 8 0 R /Off 7 0 R >> >>"},
		        "/FT /Btn /Ff 33587200 /T (unison) /V /Same", 1,
		        {.appearances = false}));
		    ASSERT_TRUE(opened.document);
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields && !fields->limited);
		    ASSERT_EQ(fields->entries.size(), 2u);
		    for (int i = 0; i < 2; ++i) {
			    ASSERT_TRUE(fields->entries[i].control);
			    EXPECT_EQ(fields->entries[i].control->index, i);
			    EXPECT_EQ(fields->entries[i].control->count, 2);
			    EXPECT_EQ(fields->entries[i].control->exportValue, "Same");
			    EXPECT_EQ(fields->entries[i].checked, true);
		    }
	    })
	    .get();
}

TEST(FormFields, CheckboxExportsUseSdkFallbackAndCancelWithoutChanges) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = formBytes(
		        {WIDGET + "/FT /Btn /T (box) /AS /Off /V /Off /Opt [()] "
		                  "/AP << /N << /Custom 8 0 R /Off 7 0 R >> >>"},
		        {}, 1, {.appearances = false});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    int checkpoints = 0;
		    const auto fields = opened.document->formFields(0, [&] {
			    ++checkpoints;
			    return false;
		    });
		    ASSERT_TRUE(fields && !fields->limited);
		    ASSERT_EQ(fields->entries.size(), 1u);
		    ASSERT_TRUE(fields->entries[0].control);
		    EXPECT_EQ(fields->entries[0].control->exportValue, "Yes");
		    EXPECT_EQ(fields->entries[0].value, "Off");
		    EXPECT_EQ(fields->entries[0].checked, false);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    int calls = 0;
			    EXPECT_FALSE(opened.document->formFields(
			        0, [&] { return ++calls >= stop; }));
		    }
		    EXPECT_FALSE(opened.document->modified());
		    EXPECT_EQ(opened.document->annotationRevision(), fields->revision);
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(FormFields, ButtonExportsShareStringLimitsAndOmitIncompleteControls) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = formBytes(
		        {WIDGET + "/FT /Btn /T (long) /AS /Off /V /Off /Opt [(" +
		         std::string(33000, 'a') +
		         ")] /AP << /N << /Yes 8 0 R /Off 7 0 R >> >>"},
		        {}, 1, {.appearances = false});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields && fields->limited);
		    ASSERT_EQ(fields->entries.size(), 1u);
		    EXPECT_FALSE(fields->entries[0].control);
		    EXPECT_EQ(fields->entries[0].checked, false);
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    std::vector<std::string> large;
		    for (int i = 0; i < 18; ++i) {
			    large.push_back(WIDGET + "/FT /Btn /T (box" +
			                    std::to_string(i) +
			                    ") /AS /Off /V /Off /Opt [(" +
			                    std::string(31000, char('a' + i)) +
			                    ")] /AP << /N << /Yes 8 0 R /Off 7 0 R >> >>");
		    }
		    const auto aggregate = pdf::openDocument(
		        formBytes(large, {}, 1, {.appearances = false}));
		    ASSERT_TRUE(aggregate.document);
		    const auto limited = aggregate.document->formFields(0);
		    ASSERT_TRUE(limited && limited->limited);
		    ASSERT_EQ(limited->entries.size(), 18u);
		    ASSERT_TRUE(limited->entries.front().control);
		    EXPECT_EQ(limited->entries.front().control->exportValue.size(),
		              31000u);
		    EXPECT_FALSE(limited->entries.back().control);
		    const auto crowded = pdf::openDocument(
		        formBytes(std::vector<std::string>(
		                      4097, WIDGET + "/AS /Off /AP << /N << "
		                                     "/Yes 8 0 R /Off 7 0 R >> >>"),
		                  "/FT /Btn /Ff 32768 /T (crowded) /V /Off", 1,
		                  {.appearances = false}));
		    ASSERT_TRUE(crowded.document);
		    const auto group = crowded.document->formFields(0);
		    ASSERT_TRUE(group && group->limited);
		    ASSERT_EQ(group->entries.size(), 4096u);
		    EXPECT_FALSE(group->entries.front().control);
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

TEST(FormFields, SingleChoicesRoundTripValuesPositionsNotesAndBookmarks) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const int flags : {6, 131078, 524294, 655366}) {
			    const auto bytes = formBytes(
			        {CHOICE + "/Ff " + std::to_string(flags) +
			             " /DV (old) /Opt [[(old) (Zulu)] "
			             "[<FEFF00630061006600E9> (Zulu)] "
			             "[<FEFF00630061006600E9> (Alpha)]]",
			         "/Subtype /Text /Rect [1 2 3 4] /Contents (keep)"},
			        {}, 1, {.outline = true});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    ASSERT_TRUE(doc.outline());
			    const auto old = doc.formFields(0);
			    ASSERT_TRUE(old && old->entries.size() == 1);
			    EXPECT_EQ(
			        doc.updateChoiceField(0, old->entries[0], 0, old->revision),
			        false);
			    EXPECT_FALSE(doc.modified());
			    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
			    for (const int option : {1, 2, 0}) {
				    const auto before = doc.formFields(0);
				    ASSERT_TRUE(before && before->entries.size() == 1);
				    ASSERT_EQ(doc.updateChoiceField(0, before->entries[0],
				                                    option, before->revision),
				              true);
				    EXPECT_GT(doc.annotationRevision(), before->revision);
				    EXPECT_TRUE(pdf::DocumentFormTest::hasAppearance(doc));
				    EXPECT_FALSE(doc.updateChoiceField(0, old->entries[0], 1,
				                                       old->revision));
				    const auto saved = doc.serializeForCopy(1024 * 1024);
				    ASSERT_TRUE(saved);
				    const auto reopened = pdf::openDocument(*saved);
				    ASSERT_TRUE(reopened.document);
				    const auto fields = reopened.document->formFields(0);
				    ASSERT_TRUE(fields && fields->entries.size() == 1);
				    const auto& field = fields->entries[0];
				    EXPECT_EQ(field.flags, flags);
				    EXPECT_EQ(field.value, option == 0 ? "old" : "caf\u00e9");
				    ASSERT_TRUE(field.options && field.options->size() == 3);
				    for (int i = 0; i < 3; ++i) {
					    EXPECT_EQ((*field.options)[i].index, i);
					    EXPECT_EQ((*field.options)[i].label,
					              i == 2 ? "Alpha" : "Zulu");
					    EXPECT_EQ((*field.options)[i].selected, i == option);
				    }
				    EXPECT_EQ(reopened.document->textNote(0, 1), u"keep");
				    EXPECT_TRUE(doc.outlineLink(0));
			    }
			    EXPECT_EQ(old->entries[0].value, "old");
			    ASSERT_TRUE(old->entries[0].options);
			    EXPECT_TRUE(old->entries[0].options->front().selected);
		    }
	    })
	    .get();
}

TEST(FormFields, MixedFormsKeepPersistedTextAndChoiceFonts) {
	pdf::Engine engine;
	engine
	    .run([] {
		    wchar_t directory[1024]{};
		    const auto length = GetEnvironmentVariableW(
		        L"SableFormAppearanceEvidence", directory, 1024);
		    ASSERT_LT(length, 1024u);
		    for (const bool explicitFont : {false, true}) {
			    for (const bool accent : {false, true}) {
				    SCOPED_TRACE(explicitFont);
				    SCOPED_TRACE(accent);
				    const auto source =
				        mixedFormPdf({.explicitFont = explicitFont});
				    const auto opened =
				        pdf::openDocument(std::vector<std::uint8_t>(
				            source.begin(), source.end()));
				    ASSERT_TRUE(opened.document);
				    auto& document = *opened.document;
				    const auto value =
				        accent ? u"Desktop caf\u00e9" : u"Desktop text";
				    ASSERT_EQ(
				        document.updateTextField(0, 1, u"initial", value,
				                                 document.annotationRevision()),
				        true);
				    std::vector<std::string> firstFonts;
				    for (const bool mixed : {false, true}) {
					    SCOPED_TRACE(mixed);
					    if (mixed) {
						    for (const int position : {4, 5}) {
							    const auto fields = document.formFields(0);
							    ASSERT_TRUE(fields &&
							                fields->entries.size() == 6);
							    ASSERT_EQ(document.updateChoiceField(
							                  0, fields->entries[position], 2,
							                  fields->revision),
							              true);
						    }
					    }
					    const auto bytes =
					        document.serializeForCopy(1024 * 1024);
					    ASSERT_TRUE(bytes);
					    const auto reopened = pdf::openDocument(*bytes);
					    ASSERT_TRUE(reopened.document);
					    // i read the saved AP objects before form setup or
					    // drawing.
					    const auto fonts =
					        pdf::DocumentFormTest::appearanceFonts(
					            *reopened.document, 1);
					    EXPECT_FALSE(fonts.empty());
					    if (explicitFont) {
						    for (const auto& font : fonts) {
							    EXPECT_EQ(font, "Helvetica");
						    }
					    }
					    if (!mixed) {
						    firstFonts = fonts;
					    } else {
						    EXPECT_EQ(fonts, firstFonts);
						    for (const int index : {5, 6}) {
							    const auto choiceFonts =
							        pdf::DocumentFormTest::appearanceFonts(
							            *reopened.document, index);
							    EXPECT_FALSE(choiceFonts.empty());
							    for (const auto& font : choiceFonts) {
								    EXPECT_EQ(font, "Helvetica");
							    }
						    }
					    }
					    if (length) {
						    const auto name =
						        std::string(explicitFont ? "explicit-"
						                                 : "legacy-") +
						        (accent ? "accent-" : "ascii-") +
						        (mixed ? "mixed.pdf" : "text.pdf");
						    std::ofstream out(std::filesystem::path(directory) /
						                          name,
						                      std::ios::binary);
						    out.write(
						        reinterpret_cast<const char*>(bytes->data()),
						        static_cast<std::streamsize>(bytes->size()));
						    out.close();
						    ASSERT_TRUE(out);
					    }
				    }
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, ChoicesCanSelectFromAnUnsetValueAndRejectStaleMetadata) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = formBytes({CHOICE + "/V () /I []"});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields && fields->entries.size() == 1);
		    const auto& target = fields->entries[0];
		    ASSERT_TRUE(target.options && target.options->size() == 2);
		    for (int mismatch = 0; mismatch < 6; ++mismatch) {
			    auto changed = target;
			    if (mismatch == 0) {
				    changed.options->back().label = "wrong";
			    } else if (mismatch == 1) {
				    changed.options->back().selected = true;
			    } else if (mismatch == 2) {
				    changed.options->back().index = 0;
			    } else if (mismatch == 3) {
				    changed.value = "wrong";
			    } else if (mismatch == 4) {
				    changed.name = "wrong";
			    } else {
				    changed.options->push_back({2, "extra", false});
			    }
			    EXPECT_FALSE(
			        opened.document->updateChoiceField(0, changed, 1, 1));
		    }
		    EXPECT_EQ(opened.document->updateChoiceField(0, target, -1, 1),
		              false);
		    EXPECT_FALSE(opened.document->updateChoiceField(0, target, -2, 1));
		    EXPECT_FALSE(opened.document->updateChoiceField(0, target, 2, 1));
		    EXPECT_FALSE(opened.document->updateChoiceField(0, target, 1, 0));
		    EXPECT_FALSE(opened.document->updateChoiceField(-1, target, 1, 1));
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    EXPECT_EQ(opened.document->annotationRevision(), 1u);
		    ASSERT_EQ(opened.document->updateChoiceField(0, target, 1, 1),
		              true);
		    const auto after = opened.document->formFields(0);
		    ASSERT_TRUE(after && after->entries.size() == 1);
		    EXPECT_EQ(after->entries[0].value, "new");
		    EXPECT_EQ(opened.document->updateChoiceField(0, after->entries[0],
		                                                 1, after->revision),
		              false);
	    })
	    .get();
}

TEST(FormFields, SingleListsClearSelectionsAndPreserveSavedIdentity) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const int flags : {0, 6, 524288, 524294}) {
			    for (const int selected : {0, 1, 2}) {
				    SCOPED_TRACE(flags);
				    SCOPED_TRACE(selected);
				    const auto bytes = formBytes(
				        {CHOICE + "/Ff " + std::to_string(flags) +
				             " /Opt [[(old) (Zulu)] [(new) (Alpha)] "
				             "[(new) (Alpha)]] /V (" +
				             (selected == 0 ? "old" : "new") + ") /I [" +
				             std::to_string(selected) + "]",
				         "/Subtype /Text /Rect [1 2 3 4] /Contents (keep)"},
				        {}, 1, {.outline = true});
				    const auto opened = pdf::openDocument(bytes);
				    ASSERT_TRUE(opened.document);
				    auto& doc = *opened.document;
				    ASSERT_EQ(doc.addTextNote(0, 50, 60, u"unsaved"), 2);
				    const auto before = doc.formFields(0);
				    ASSERT_TRUE(before && before->entries.size() == 1);
				    ASSERT_TRUE(before->entries[0].options &&
				                before->entries[0].options->size() == 3);
				    EXPECT_TRUE(
				        (*before->entries[0].options)[selected].selected);
				    ASSERT_EQ(doc.updateChoiceField(0, before->entries[0], -1,
				                                    before->revision),
				              true);
				    EXPECT_GT(doc.annotationRevision(), before->revision);
				    const auto verify = [&](const pdf::Document& document) {
					    for (const auto key : {"V", "I", "DV"}) {
						    EXPECT_FALSE(pdf::DocumentFormTest::hasFieldKey(
						        document, 0, key));
					    }
					    const auto fields = document.formFields(0);
					    ASSERT_TRUE(fields && fields->entries.size() == 1);
					    const auto& field = fields->entries[0];
					    EXPECT_EQ(field.index, 0);
					    EXPECT_EQ(field.type, pdf::FormFieldType::ListBox);
					    EXPECT_EQ(field.name, before->entries[0].name);
					    EXPECT_EQ(field.flags, flags);
					    EXPECT_EQ(field.annotationFlags,
					              before->entries[0].annotationFlags);
					    EXPECT_TRUE(field.value.empty());
					    ASSERT_TRUE(field.options &&
					                field.options->size() == 3);
					    for (int i = 0; i < 3; ++i) {
						    EXPECT_EQ((*field.options)[i].index, i);
						    EXPECT_EQ((*field.options)[i].label,
						              i == 0 ? "Zulu" : "Alpha");
						    EXPECT_FALSE((*field.options)[i].selected);
					    }
					    EXPECT_EQ(document.textNote(
					                  0, 1, document.annotationRevision()),
					              u"keep");
					    EXPECT_EQ(document.textNote(
					                  0, 2, document.annotationRevision()),
					              u"unsaved");
					    const auto outline = document.outline();
					    ASSERT_TRUE(outline && outline->entries.size() == 1);
					    EXPECT_EQ(outline->entries[0].title, "Go");
					    const auto link = document.outlineLink(0);
					    ASSERT_TRUE(link && link->destination);
					    EXPECT_EQ(link->destination->page, 0);
				    };
				    verify(doc);
				    const auto cleared = doc.formFields(0);
				    const auto unchanged = pdf::DocumentFormTest::rawBytes(doc);
				    ASSERT_TRUE(cleared && cleared->entries.size() == 1);
				    EXPECT_EQ(doc.updateChoiceField(0, cleared->entries[0], -1,
				                                    cleared->revision),
				              false);
				    EXPECT_EQ(doc.annotationRevision(), cleared->revision);
				    EXPECT_EQ(pdf::DocumentFormTest::rawBytes(doc), unchanged);
				    EXPECT_TRUE(doc.modified());
				    const auto saved = doc.serializeForCopy(1024 * 1024);
				    ASSERT_TRUE(saved);
				    const auto reopened = pdf::openDocument(*saved);
				    ASSERT_TRUE(reopened.document);
				    verify(*reopened.document);
				    const auto empty = reopened.document->formFields(0);
				    ASSERT_TRUE(empty && empty->entries.size() == 1);
				    EXPECT_EQ(reopened.document->updateChoiceField(
				                  0, empty->entries[0], -1, empty->revision),
				              false);
				    EXPECT_FALSE(reopened.document->modified());
				    EXPECT_EQ(reopened.document->annotationRevision(),
				              empty->revision);
				    EXPECT_EQ(reopened.document->serialize(1024 * 1024), saved);
				    ASSERT_EQ(doc.updateChoiceField(0, cleared->entries[0], 2,
				                                    cleared->revision),
				              true);
				    const auto again = doc.formFields(0);
				    ASSERT_TRUE(again && again->entries.size() == 1 &&
				                again->entries[0].options);
				    EXPECT_TRUE((*again->entries[0].options)[2].selected);
				    ASSERT_EQ(doc.updateChoiceField(0, again->entries[0], -1,
				                                    again->revision),
				              true);
				    verify(doc);
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, ListClearingRefusesDefaultsUnsafeFieldsAndStaleMetadata) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto baseline = pdf::openDocument(formBytes({CHOICE}));
		    ASSERT_TRUE(baseline.document);
		    const auto original = baseline.document->formFields(0);
		    ASSERT_TRUE(original && original->entries.size() == 1);
		    std::vector<std::vector<std::uint8_t>> cases;
		    for (const std::string extra :
		         {"/DV (old)", "/DV ()", "/DV /old", "/DV 17"}) {
			    cases.push_back(formBytes({CHOICE + extra}));
			    cases.push_back(formBytes({CHOICE + "/V () /I [] " + extra}));
		    }
		    for (const std::string extra :
		         {"/Ff 131072", "/Ff 655360", "/Ff 1", "/Ff 2097152",
		          "/Ff 262144", "/Ff 67108864", "/F 2", "/F 64", "/F 128",
		          "/Kids []", "/V /old", "/V [(old)]",
		          "/Opt [[(old) (First)] [(new) ()]]", "/A << /S /ResetForm >>",
		          "/AA << /V << /S /JavaScript /JS (x) >> >>"}) {
			    cases.push_back(formBytes({CHOICE + extra}));
		    }
		    cases.push_back(formBytes({WIDGET}, CHOICE.substr(WIDGET.size())));
		    cases.push_back(
		        formBytes({WIDGET, WIDGET}, CHOICE.substr(WIDGET.size())));
		    cases.push_back(formBytes({CHOICE, CHOICE}));
		    cases.push_back(
		        formBytes({CHOICE}, {}, 1, {.nestedSignature = true}));
		    cases.push_back(formBytes({WIDGET + "/FT /Tx /T (text) /V (old)"}));
		    for (std::size_t caseIndex = 0; caseIndex < cases.size();
		         ++caseIndex) {
			    SCOPED_TRACE(caseIndex);
			    const auto& bytes = cases[caseIndex];
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    const auto fields = opened.document->formFields(0);
			    ASSERT_TRUE(fields);
			    const auto& target = fields->entries.empty()
			                             ? original->entries[0]
			                             : fields->entries[0];
			    EXPECT_FALSE(opened.document->updateChoiceField(
			        0, target, -1, fields->revision));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(),
			              fields->revision);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
		    const auto bytes = formBytes({CHOICE});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    const auto fields = opened.document->formFields(0);
		    ASSERT_TRUE(fields && fields->entries.size() == 1);
		    for (int mismatch = 0; mismatch < 7; ++mismatch) {
			    auto stale = fields->entries[0];
			    if (mismatch == 0) {
				    stale.name = "wrong";
			    }
			    if (mismatch == 1) {
				    stale.flags = 6;
			    }
			    if (mismatch == 2) {
				    stale.annotationFlags = 4;
			    }
			    if (mismatch == 3) {
				    stale.value = "wrong";
			    }
			    if (mismatch == 4) {
				    stale.options->back().label = "wrong";
			    }
			    if (mismatch == 5) {
				    stale.options->back().index = 0;
			    }
			    if (mismatch == 6) {
				    stale.options->back().selected = true;
			    }
			    EXPECT_FALSE(opened.document->updateChoiceField(
			        0, stale, -1, fields->revision));
		    }
		    EXPECT_FALSE(opened.document->updateChoiceField(
		        0, fields->entries[0], -2, fields->revision));
		    EXPECT_FALSE(opened.document->updateChoiceField(
		        0, fields->entries[0], -1, 0));
		    EXPECT_EQ(opened.document->annotationRevision(), fields->revision);
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(FormFields, ListClearCancellationKeepsBytesAndAllowsRetry) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const int flags : {0, 524288}) {
			    const auto bytes =
			        formBytes({CHOICE + "/Ff " + std::to_string(flags)});
			    const auto completed = pdf::openDocument(bytes);
			    ASSERT_TRUE(completed.document);
			    const auto fields = completed.document->formFields(0);
			    ASSERT_TRUE(fields && fields->entries.size() == 1);
			    int checkpoints = 0;
			    ASSERT_EQ(completed.document->updateChoiceField(
			                  0, fields->entries[0], -1, fields->revision,
			                  [&] {
				                  ++checkpoints;
				                  return false;
			                  }),
			              true);
			    ASSERT_GT(checkpoints, 10);
			    for (int stop = 1; stop <= checkpoints; ++stop) {
				    const auto opened = pdf::openDocument(bytes);
				    ASSERT_TRUE(opened.document);
				    int calls = 0;
				    EXPECT_FALSE(opened.document->updateChoiceField(
				        0, fields->entries[0], -1, fields->revision,
				        [&] { return ++calls >= stop; }));
				    EXPECT_FALSE(opened.document->modified());
				    EXPECT_EQ(opened.document->annotationRevision(),
				              fields->revision);
				    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
				    EXPECT_EQ(opened.document->updateChoiceField(
				                  0, fields->entries[0], -1, fields->revision),
				              true);
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, ChoiceRefusalsKeepLiveBytesAndRevision) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto baseline = pdf::openDocument(formBytes({CHOICE}));
		    ASSERT_TRUE(baseline.document);
		    const auto original = baseline.document->formFields(0);
		    ASSERT_TRUE(original && original->entries.size() == 1);
		    for (const std::string extra :
		         {"/Ff 1",
		          "/Ff 393216",
		          "/Ff 2097152",
		          "/Ff 524289",
		          "/Ff 917504",
		          "/Ff 2621440",
		          "/Ff 67633152",
		          "/Ff 67108864",
		          "/F 2",
		          "/F 32",
		          "/F 64",
		          "/F 128",
		          "/F 512",
		          "/A <<>>",
		          "/AA << /Fo << /S /ResetForm >> >>",
		          "/AA << /V << /S /JavaScript /JS (event) >> >>",
		          "/Kids []",
		          "/V /old",
		          "/V [(old)]",
		          "/Opt []",
		          "/Opt [[(old) (First)] [() (Second)]]",
		          "/Opt [[(old) (First)] [null (Second)]]",
		          "/Opt [[(old) (First)] [(new) ()]]",
		          "/Opt [[(old) (First)] [(old) (Second)]] /I []"}) {
			    SCOPED_TRACE(extra);
			    const auto bytes = formBytes({CHOICE + extra});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    const auto fields = opened.document->formFields(0);
			    ASSERT_TRUE(fields);
			    const auto& target = fields->entries.empty()
			                             ? original->entries[0]
			                             : fields->entries[0];
			    EXPECT_FALSE(opened.document->updateChoiceField(
			        0, target, 1, fields->revision));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(),
			              fields->revision);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
		    const auto parent = CHOICE.substr(WIDGET.size());
		    for (const auto& bytes :
		         {formBytes({WIDGET}, parent),
		          formBytes({WIDGET, WIDGET}, parent),
		          formBytes({CHOICE, CHOICE}),
		          formBytes(
		              {CHOICE.substr(WIDGET.size()) +
		                   "/Opt [[(old) (First)] [(old) (Second)]] "
		                   "/AA << /V << /S /ResetForm /Fields [(other)] >> >>",
		               CHOICE + "/Opt [[(old) (First)] [(old) (Second)]]",
		               WIDGET + "/FT /Tx /T (other) /V (keep) /DV (reset)"}),
		          formBytes({CHOICE, WIDGET + "/FT /Tx /T (other) "
		                                      "/AA << /V << /S /JavaScript "
		                                      "/JS (elsewhere) >> >>"}),
		          formBytes({CHOICE}, {}, 1, {.nestedSignature = true})}) {
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    const auto fields = opened.document->formFields(0);
			    ASSERT_TRUE(fields && !fields->entries.empty());
			    EXPECT_FALSE(opened.document->updateChoiceField(
			        0, fields->entries[0], 1, fields->revision));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(),
			              fields->revision);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, SortKeepsUnsafeChoicesAndNonChoiceFieldsRefused) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const int combo : {0, 131072}) {
			    const auto field =
			        CHOICE + "/Ff " + std::to_string(524288 | combo) + " ";
			    for (const std::string extra :
			         {"/Ff " + std::to_string(524289 | combo),
			          "/Ff " + std::to_string(786432 | combo),
			          "/Ff " + std::to_string(2621440 | combo),
			          "/Ff " + std::to_string(67633152 | combo),
			          std::string("/A << /S /ResetForm >>"),
			          std::string(
			              "/AA << /V << /S /JavaScript /JS (x) >> >>")}) {
				    const auto bytes = formBytes({field + extra});
				    const auto opened = pdf::openDocument(bytes);
				    ASSERT_TRUE(opened.document);
				    const auto fields = opened.document->formFields(0);
				    ASSERT_TRUE(fields && !fields->entries.empty());
				    for (const int option : {0, 1}) {
					    EXPECT_FALSE(opened.document->updateChoiceField(
					        0, fields->entries[0], option, fields->revision));
				    }
				    EXPECT_EQ(opened.document->annotationRevision(),
				              fields->revision);
				    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
			    }
			    const auto bytes =
			        formBytes({WIDGET}, field.substr(WIDGET.size()));
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    const auto fields = opened.document->formFields(0);
			    ASSERT_TRUE(fields && !fields->entries.empty());
			    for (const int option : {0, 1}) {
				    EXPECT_FALSE(opened.document->updateChoiceField(
				        0, fields->entries[0], option, fields->revision));
			    }
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
		    for (const int kind : {0, 1, 2}) {
			    const auto bytes = formBytes(
			        {WIDGET +
			         (kind == 0
			              ? "/FT /Tx /T (text) /V (old) /Ff 524288"
			              : "/FT /Btn /T (button) /V /Yes /AS /Yes /Ff " +
			                    std::to_string(524288 |
			                                   (kind == 2 ? 49152 : 0)) +
			                    " /AP << /N << /Yes 8 0 R /Off 7 0 R >> >>")},
			        {}, 1, {.appearances = false, .painted = true});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(kind == 0 ? opened.document->updateTextField(
			                                 0, 0, u"old", u"new", 1)
			                 : kind == 1 ? opened.document->updateCheckBox(
			                                   0, 0, u"Yes", false, 1)
			                             : opened.document->updateRadioButton(
			                                   0, 0, u"Yes", {0, 1, "Yes"}, 1));
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, ChoiceCancellationNeverReplacesTheLiveDocument) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const int flags : {0, 131072, 524288, 655360}) {
			    const auto bytes =
			        formBytes({CHOICE + "/Ff " + std::to_string(flags)});
			    const auto completed = pdf::openDocument(bytes);
			    ASSERT_TRUE(completed.document);
			    const auto fields = completed.document->formFields(0);
			    ASSERT_TRUE(fields && fields->entries.size() == 1);
			    int checkpoints = 0;
			    ASSERT_EQ(completed.document->updateChoiceField(
			                  0, fields->entries[0], 1, fields->revision,
			                  [&] {
				                  ++checkpoints;
				                  return false;
			                  }),
			              true);
			    ASSERT_GT(checkpoints, 10);
			    for (int stop = 1; stop <= checkpoints; ++stop) {
				    const auto opened = pdf::openDocument(bytes);
				    ASSERT_TRUE(opened.document);
				    int calls = 0;
				    EXPECT_FALSE(opened.document->updateChoiceField(
				        0, fields->entries[0], 1, fields->revision,
				        [&] { return ++calls >= stop; }));
				    EXPECT_FALSE(opened.document->modified());
				    EXPECT_EQ(opened.document->annotationRevision(),
				              fields->revision);
				    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
			    }
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

TEST(FormFields, InheritedTextRoundTripsWithItsFieldIdentity) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::array<std::pair<std::u16string, std::string>, 4> cases{
		        {{u"1234", "1234"},
		         {u"caf\u00e9", "caf\u00e9"},
		         {u"A\U0001F600", "A\U0001F600"},
		         {u"", ""}}};
		    for (const auto& [text, utf8] : cases) {
			    const auto bytes = formBytes(
			        {WIDGET + "/F 4"},
			        "/FT /Tx /T 6 0 R /Ff 6 /V (old) /DV (default) /MaxLen 4",
			        1, {.outline = true});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    const auto before = doc.formFields(0);
			    ASSERT_TRUE(before && before->entries.size() == 1);
			    EXPECT_EQ(before->entries[0].name, "N\u03a9\U0001f600");
			    EXPECT_EQ(
			        doc.updateTextField(0, 0, u"old", u"old", before->revision),
			        false);
			    EXPECT_FALSE(doc.modified());
			    EXPECT_EQ(doc.annotationRevision(), before->revision);
			    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
			    ASSERT_TRUE(doc.outline());
			    ASSERT_EQ(doc.addTextNote(0, 50, 60, u"Keep this note"), 1);
			    ASSERT_EQ(doc.updateTextField(0, 0, u"old", text,
			                                  doc.annotationRevision()),
			              true);
			    const auto after = doc.formFields(0);
			    ASSERT_TRUE(after && after->entries.size() == 1);
			    EXPECT_EQ(after->entries[0].name, before->entries[0].name);
			    EXPECT_EQ(after->entries[0].flags, 6);
			    EXPECT_EQ(after->entries[0].annotationFlags, 4);
			    EXPECT_EQ(after->entries[0].value, utf8);
			    EXPECT_EQ(before->entries[0].value, "old");
			    EXPECT_TRUE(pdf::DocumentFormTest::hasAppearance(doc));
			    EXPECT_EQ(doc.textNote(0, 1, after->revision),
			              u"Keep this note");
			    EXPECT_EQ(
			        doc.updateTextField(0, 0, text, text, after->revision),
			        false);
			    EXPECT_FALSE(doc.updateTextField(0, 0, text, u"stale",
			                                     before->revision));
			    EXPECT_FALSE(doc.updateTextField(0, 0, u"wrong", u"no",
			                                     after->revision));
			    EXPECT_EQ(doc.annotationRevision(), after->revision);
			    const auto saved = doc.serializeForCopy(1024 * 1024);
			    ASSERT_TRUE(saved);
			    const auto reopened = pdf::openDocument(*saved);
			    ASSERT_TRUE(reopened.document);
			    const auto fields = reopened.document->formFields(0);
			    ASSERT_TRUE(fields && fields->entries.size() == 1);
			    EXPECT_EQ(fields->entries[0].name, before->entries[0].name);
			    EXPECT_EQ(fields->entries[0].value, utf8);
			    EXPECT_EQ(fields->entries[0].flags, 6);
			    EXPECT_EQ(reopened.document->textNote(0, 1), u"Keep this note");
			    ASSERT_TRUE(reopened.document->outline());
			    const auto link = reopened.document->outlineLink(0);
			    ASSERT_TRUE(link && link->destination);
			    EXPECT_EQ(link->destination->page, 0);
		    }
	    })
	    .get();
}

TEST(FormFields, InheritedTextRefusesShadowsAliasesAndUnsafeParents) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::string parent = "/FT /Tx /T (field) /V (old) ";
		    std::vector<std::vector<std::uint8_t>> cases;
		    for (const std::string extra : {"/T (child)",
		                                    "/FT /Tx",
		                                    "/Ff 0",
		                                    "/V (old)",
		                                    "/DV (default)",
		                                    "/MaxLen 4",
		                                    "/Kids []",
		                                    "/RV (styled)",
		                                    "/DA (/Helv 8 Tf)",
		                                    "/Q 2",
		                                    "/DS (style)",
		                                    "/TU (tip)",
		                                    "/TM (alias)",
		                                    "/Opt [(option)]",
		                                    "/I [0]",
		                                    "/TI 0",
		                                    "/F 2",
		                                    "/F 32",
		                                    "/F 64",
		                                    "/F 128",
		                                    "/F 512"}) {
			    cases.push_back(formBytes({WIDGET + extra}, parent));
		    }
		    for (const std::string extra :
		         {"/Ff 1", "/Ff 4097", "/Ff 12288", "/Ff 8392704",
		          "/Ff 16781312 /MaxLen 4", "/Ff 33558528", "/Ff 8192",
		          "/Ff 1048576", "/Ff 16777216 /MaxLen 4", "/Ff 33554432",
		          "/AA << /Fo << /S /ResetForm >> >>", "/A <<>>",
		          "/AA << /K << /S /JavaScript /JS (doSomething) >> >>",
		          "/Type /Annot /Subtype /Widget /Rect [40 50 90 70]"}) {
			    cases.push_back(formBytes({WIDGET}, parent + extra));
		    }
		    cases.push_back(formBytes({WIDGET, WIDGET}, parent));
		    cases.push_back(formBytes({WIDGET}, parent, 2));
		    for (std::size_t sample = 0; sample < cases.size(); ++sample) {
			    SCOPED_TRACE(sample);
			    const auto& bytes = cases[sample];
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    for (const auto& text : {u"old", u"new"}) {
				    EXPECT_FALSE(opened.document->updateTextField(0, 0, u"old",
				                                                  text, 1));
			    }
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
		    const auto bytes = formBytes({WIDGET}, parent + "/MaxLen 3");
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    EXPECT_FALSE(
		        opened.document->updateTextField(0, 0, u"old", u"four", 1));
		    EXPECT_FALSE(opened.document->modified());
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    ASSERT_EQ(opened.document->updateTextField(0, 0, u"old", u"ok", 1),
		              true);
	    })
	    .get();
}

TEST(FormFields, InheritedTextCancellationKeepsEveryLiveCheckpoint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes({WIDGET}, "/FT /Tx /T (field) /V (old)");
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
		    }
	    })
	    .get();
}

TEST(FormFields, InheritedMultilineKeepsRawNoOpsAndSavedIdentity) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes({WIDGET + "/F 4"},
		                  "/FT /Tx /T 6 0 R /Ff 4102 /V (first\\nsecond)", 1,
		                  {.appearances = false, .outline = true});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto before = doc.formFields(0);
		    ASSERT_TRUE(before && before->entries.size() == 1);
		    EXPECT_EQ(before->entries[0].value, "first\nsecond");
		    EXPECT_EQ(doc.updateTextField(0, 0, u"first\nsecond",
		                                  u"first\nsecond", before->revision),
		              false);
		    EXPECT_FALSE(doc.updateTextField(
		        0, 0, u"first\r\nsecond", u"changed\nline", before->revision));
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.annotationRevision(), before->revision);
		    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
		    EXPECT_FALSE(pdf::DocumentFormTest::hasAppearance(doc));
		    ASSERT_EQ(doc.addTextNote(0, 50, 60, u"Keep this note"), 1);
		    ASSERT_EQ(doc.updateTextField(0, 0, u"first\nsecond",
		                                  u"Caf\u00e9\r\n\r\U0001F600\nlast",
		                                  doc.annotationRevision()),
		              true);
		    const auto after = doc.formFields(0);
		    ASSERT_TRUE(after && after->entries.size() == 1);
		    const auto& field = after->entries[0];
		    EXPECT_EQ(field.name, before->entries[0].name);
		    EXPECT_EQ(field.flags, 4102);
		    EXPECT_EQ(field.annotationFlags, 4);
		    EXPECT_EQ(field.value, "Caf\u00e9\r\n\r\n\U0001F600\r\nlast");
		    EXPECT_TRUE(pdf::DocumentFormTest::hasAppearance(doc));
		    EXPECT_EQ(doc.updateTextField(
		                  0, 0, u"Caf\u00e9\r\n\r\n\U0001F600\r\nlast",
		                  u"Caf\u00e9\n\n\U0001F600\nlast", after->revision),
		              false);
		    EXPECT_FALSE(doc.updateTextField(0, 0, u"first\nsecond", u"stale",
		                                     before->revision));
		    const auto saved = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(saved);
		    const auto reopened = pdf::openDocument(*saved);
		    ASSERT_TRUE(reopened.document);
		    const auto kept = reopened.document->formFields(0);
		    ASSERT_TRUE(kept && kept->entries.size() == 1);
		    EXPECT_EQ(kept->entries[0].name, field.name);
		    EXPECT_EQ(kept->entries[0].value, field.value);
		    EXPECT_EQ(kept->entries[0].flags, field.flags);
		    EXPECT_EQ(kept->entries[0].annotationFlags, field.annotationFlags);
		    EXPECT_EQ(reopened.document->textNote(0, 1), u"Keep this note");
		    ASSERT_TRUE(reopened.document->outline());
		    const auto link = reopened.document->outlineLink(0);
		    ASSERT_TRUE(link && link->destination);
		    EXPECT_EQ(link->destination->page, 0);
		    ASSERT_EQ(doc.updateTextField(
		                  0, 0, u"Caf\u00e9\r\n\r\n\U0001F600\r\nlast", u"",
		                  after->revision),
		              true);
		    const auto cleared = doc.formFields(0);
		    ASSERT_TRUE(cleared && cleared->entries.size() == 1);
		    EXPECT_TRUE(cleared->entries[0].value.empty());
		    EXPECT_EQ(before->entries[0].value, "first\nsecond");
	    })
	    .get();
}

TEST(FormFields, InheritedMultilineRefusalsKeepTheRetryIntact) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = formBytes(
		        {WIDGET}, "/FT /Tx /T (field) /Ff 4096 /V (x) /MaxLen 2");
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    for (const auto& input :
		         {std::u16string(u"a\nb"), std::u16string(16384, u'\n'),
		          std::u16string(u"tab\tvalue")}) {
			    EXPECT_FALSE(doc.updateTextField(0, 0, u"x", input, 1));
			    EXPECT_FALSE(doc.modified());
			    EXPECT_EQ(doc.annotationRevision(), 1u);
			    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
		    }
		    ASSERT_EQ(doc.updateTextField(0, 0, u"x", u"ok", 1), true);
		    const auto fields = doc.formFields(0);
		    ASSERT_TRUE(fields && fields->entries.size() == 1);
		    EXPECT_EQ(fields->entries[0].value, "ok");
		    const auto exact = pdf::openDocument(formBytes(
		        {WIDGET}, "/FT /Tx /T (field) /Ff 4096 /V (x) /MaxLen 3"));
		    ASSERT_TRUE(exact.document);
		    ASSERT_EQ(exact.document->updateTextField(0, 0, u"x", u"a\nb", 1),
		              true);
		    const auto paragraphs = exact.document->formFields(0);
		    ASSERT_TRUE(paragraphs && paragraphs->entries.size() == 1);
		    EXPECT_EQ(paragraphs->entries[0].value, "a\r\nb");
	    })
	    .get();
}

TEST(FormFields, InheritedMultilineCancellationKeepsEveryLiveCheckpoint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes({WIDGET}, "/FT /Tx /T (field) /Ff 4096 /V (old)");
		    const auto completed = pdf::openDocument(bytes);
		    ASSERT_TRUE(completed.document);
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateTextField(0, 0, u"old",
		                                                  u"first\nsecond", 1,
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
			        0, 0, u"old", u"first\nsecond", 1,
			        [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, NoSpellCheckTextKeepsFlagsAndSavedContents) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const bool inherited : {false, true}) {
			    for (const int style : {0, 4096, 16777216}) {
				    if (inherited && style == 16777216) {
					    continue;
				    }
				    SCOPED_TRACE(inherited);
				    SCOPED_TRACE(style);
				    const int flags = 4194304 | 6 | style;
				    const auto attributes =
				        "/FT /Tx /T (field) /Ff " + std::to_string(flags) +
				        " /V (old) /MaxLen " + (style == 16777216 ? "4" : "32");
				    const auto bytes = formBytes(
				        {WIDGET + "/F 4 " + (inherited ? "" : attributes)},
				        inherited ? attributes : "", 1,
				        {.appearances = false, .outline = true});
				    const auto opened = pdf::openDocument(bytes);
				    ASSERT_TRUE(opened.document);
				    auto& doc = *opened.document;
				    const auto before = doc.formFields(0);
				    ASSERT_TRUE(before && before->entries.size() == 1);
				    EXPECT_EQ(doc.updateTextField(0, 0, u"old", u"old", 1),
				              false);
				    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
				    EXPECT_FALSE(doc.modified());
				    ASSERT_TRUE(doc.outline());
				    ASSERT_EQ(doc.addTextNote(0, 50, 60, u"Keep this note"), 1);
				    const std::u16string input =
				        style == 4096       ? u"Caf\u00e9\n\n\U0001F600"
				        : style == 16777216 ? u"caf\u00e9"
				                            : u"Caf\u00e9\U0001F600";
				    const std::u16string stored =
				        style == 4096 ? u"Caf\u00e9\r\n\r\n\U0001F600" : input;
				    const std::string expected =
				        style == 4096       ? "Caf\u00e9\r\n\r\n\U0001F600"
				        : style == 16777216 ? "caf\u00e9"
				                            : "Caf\u00e9\U0001F600";
				    ASSERT_EQ(doc.updateTextField(0, 0, u"old", input,
				                                  doc.annotationRevision()),
				              true);
				    const auto after = doc.formFields(0);
				    ASSERT_TRUE(after && after->entries.size() == 1);
				    EXPECT_EQ(after->entries[0].flags, flags);
				    EXPECT_EQ(after->entries[0].annotationFlags, 4);
				    EXPECT_EQ(after->entries[0].name, "field");
				    EXPECT_EQ(after->entries[0].value, expected);
				    EXPECT_EQ(doc.updateTextField(0, 0, stored, input,
				                                  after->revision),
				              false);
				    EXPECT_FALSE(doc.updateTextField(0, 0, u"wrong", u"stale",
				                                     after->revision));
				    const auto saved = doc.serializeForCopy(1024 * 1024);
				    ASSERT_TRUE(saved);
				    const auto reopened = pdf::openDocument(*saved);
				    ASSERT_TRUE(reopened.document);
				    const auto kept = reopened.document->formFields(0);
				    ASSERT_TRUE(kept && kept->entries.size() == 1);
				    EXPECT_EQ(kept->entries[0].flags, flags);
				    EXPECT_EQ(kept->entries[0].value, expected);
				    EXPECT_EQ(reopened.document->textNote(0, 1),
				              u"Keep this note");
				    ASSERT_TRUE(reopened.document->outline());
				    const auto link = reopened.document->outlineLink(0);
				    ASSERT_TRUE(link && link->destination);
				    EXPECT_EQ(link->destination->page, 0);
				    ASSERT_EQ(
				        doc.updateTextField(0, 0, stored, u"", after->revision),
				        true);
				    const auto cleared = doc.formFields(0);
				    ASSERT_TRUE(cleared && cleared->entries.size() == 1);
				    EXPECT_TRUE(cleared->entries[0].value.empty());
				    EXPECT_EQ(before->entries[0].value, "old");
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, NoSpellCheckKeepsUnsafeAndNonTextWritesRefused) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const int extra :
		         {1, 8192, 1048576, 8388608, 33554432, 4096 | 16777216}) {
			    SCOPED_TRACE(extra);
			    const auto bytes = formBytes(
			        {WIDGET + "/FT /Tx /T (field) /V (old) /MaxLen 4 /Ff " +
			         std::to_string(4194304 | extra)});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    for (const auto& value : {u"old", u"new"}) {
				    EXPECT_FALSE(opened.document->updateTextField(0, 0, u"old",
				                                                  value, 1));
			    }
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
		    for (const bool inherited : {false, true}) {
			    const std::string attributes =
			        "/FT /Tx /T (field) /V (old) /Ff 4194304 /MaxLen 3";
			    const auto bytes =
			        formBytes({WIDGET + (inherited ? "" : attributes)},
			                  inherited ? attributes : "");
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    EXPECT_FALSE(doc.updateTextField(0, 0, u"old", u"four", 1));
			    EXPECT_FALSE(doc.modified());
			    EXPECT_EQ(doc.annotationRevision(), 1u);
			    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
			    ASSERT_EQ(doc.updateTextField(0, 0, u"old", u"ok", 1), true);
		    }
		    const auto inheritedComb = formBytes(
		        {WIDGET}, "/FT /Tx /T (field) /V (old) /Ff 20971520 /MaxLen 4");
		    const auto inherited = pdf::openDocument(inheritedComb);
		    ASSERT_TRUE(inherited.document);
		    EXPECT_FALSE(
		        inherited.document->updateTextField(0, 0, u"old", u"new", 1));
		    EXPECT_EQ(inherited.document->serialize(1024 * 1024),
		              inheritedComb);
		    for (const bool radio : {false, true}) {
			    const auto bytes = formBytes(
			        {WIDGET + "/FT /Btn /T (button) /V /Yes /AS /Yes /Ff " +
			         std::to_string(4194304 | (radio ? 49152 : 0)) +
			         " /AP << /N << /Yes 8 0 R /Off 7 0 R >> >>"},
			        {}, 1, {.appearances = false, .painted = true});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    EXPECT_FALSE(radio ? opened.document->updateRadioButton(
			                             0, 0, u"Yes", {0, 1, "Yes"}, 1)
			                       : opened.document->updateCheckBox(
			                             0, 0, u"Yes", true, 1));
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
		    for (const bool combo : {false, true}) {
			    const auto bytes =
			        formBytes({CHOICE + "/Ff " +
			                   std::to_string(4194304 | (combo ? 131072 : 0))});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    const auto fields = opened.document->formFields(0);
			    ASSERT_TRUE(fields && fields->entries.size() == 1);
			    EXPECT_FALSE(opened.document->updateChoiceField(
			        0, fields->entries[0], 0, fields->revision));
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, NoSpellCheckCancellationKeepsEveryLiveCheckpoint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes({WIDGET}, "/FT /Tx /T (field) /Ff 4198400 /V (old)");
		    const auto completed = pdf::openDocument(bytes);
		    ASSERT_TRUE(completed.document);
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateTextField(0, 0, u"old",
		                                                  u"first\nsecond", 1,
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
			        0, 0, u"old", u"first\nsecond", 1,
			        [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, CombTextWritesRoundTripAndKeepOtherContents) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::array<std::pair<std::u16string, std::string>, 4> cases{
		        {{u"1234", "1234"},
		         {u"caf\u00e9", "caf\u00e9"},
		         {u"A\U0001F600", "A\U0001F600"},
		         {u"", ""}}};
		    for (const auto& [text, utf8] : cases) {
			    const auto bytes = formBytes(
			        {WIDGET +
			             "/FT /Tx /Ff 16777222 /MaxLen 4 /T (code) /V (old)",
			         WIDGET + "/FT /Tx /T (keep) /V (untouched)"},
			        {}, 1, {.outline = true});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    ASSERT_TRUE(doc.outline());
			    ASSERT_EQ(doc.addTextNote(0, 50, 60, u"Keep this note"), 2);
			    const auto before = doc.formFields(0);
			    ASSERT_TRUE(before && before->entries.size() == 2);
			    EXPECT_EQ(
			        doc.updateTextField(0, 0, u"old", u"old", before->revision),
			        false);
			    EXPECT_EQ(doc.annotationRevision(), before->revision);
			    ASSERT_EQ(
			        doc.updateTextField(0, 0, u"old", text, before->revision),
			        true);
			    EXPECT_EQ(before->entries[0].value, "old");
			    const auto after = doc.formFields(0);
			    ASSERT_TRUE(after && after->entries.size() == 2);
			    EXPECT_GT(after->revision, before->revision);
			    EXPECT_TRUE(pdf::DocumentFormTest::hasAppearance(doc));
			    EXPECT_EQ(after->entries[0].flags, 16777222);
			    EXPECT_EQ(after->entries[1].value, "untouched");
			    EXPECT_EQ(doc.textNote(0, 2, after->revision),
			              u"Keep this note");
			    EXPECT_EQ(
			        doc.updateTextField(0, 0, text, text, after->revision),
			        false);
			    EXPECT_FALSE(doc.updateTextField(0, 0, text, u"stale",
			                                     before->revision));
			    EXPECT_FALSE(doc.updateTextField(0, 0, u"wrong", u"0000",
			                                     after->revision));
			    EXPECT_EQ(doc.annotationRevision(), after->revision);
			    const auto saved = doc.serializeForCopy(1024 * 1024);
			    ASSERT_TRUE(saved);
			    const auto reopened = pdf::openDocument(*saved);
			    ASSERT_TRUE(reopened.document);
			    const auto fields = reopened.document->formFields(0);
			    ASSERT_TRUE(fields && fields->entries.size() == 2);
			    EXPECT_EQ(fields->entries[0].value, after->entries[0].value);
			    EXPECT_EQ(fields->entries[1].value, "untouched");
			    EXPECT_EQ(reopened.document->textNote(0, 2), u"Keep this note");
			    ASSERT_TRUE(reopened.document->outline());
			    const auto link = reopened.document->outlineLink(0);
			    ASSERT_TRUE(link && link->destination);
			    EXPECT_EQ(link->destination->page, 0);
			    EXPECT_EQ(after->entries[0].value, utf8);
		    }
	    })
	    .get();
}

TEST(FormFields, CombLimitsAndConflictingFlagsRefuseWithoutLiveChanges) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const std::string extra :
		         {"", "/MaxLen 0", "/MaxLen -1", "/MaxLen 3.5", "/MaxLen 32768",
		          "/MaxLen (4)", "/MaxLen /Four", "/MaxLen null", "/MaxLen [4]",
		          "/MaxLen <<>>"}) {
			    SCOPED_TRACE(extra);
			    const auto bytes = formBytes(
			        {WIDGET + "/FT /Tx /Ff 16777216 /T (code) /V (old) " +
			         extra});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    for (const auto& text : {u"old", u"new"}) {
				    EXPECT_FALSE(opened.document->updateTextField(0, 0, u"old",
				                                                  text, 1));
			    }
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
		    for (const int flags : {16777216, 16777217, 16781312, 16785408,
		                            17825792, 50331648, 25165824}) {
			    SCOPED_TRACE(flags);
			    const auto bytes = formBytes(
			        {WIDGET + "/FT /Tx /T (code) /V (old) /MaxLen 4 /Ff " +
			         std::to_string(flags)});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    for (const auto& text : {u"12345", u"a\nb", u"a\rb", u"a\tb"}) {
				    EXPECT_FALSE(opened.document->updateTextField(0, 0, u"old",
				                                                  text, 1));
			    }
			    if (flags != 16777216) {
				    EXPECT_FALSE(opened.document->updateTextField(0, 0, u"old",
				                                                  u"new", 1));
				    EXPECT_FALSE(opened.document->updateTextField(0, 0, u"old",
				                                                  u"old", 1));
			    }
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, CombCancellationPreservesEveryLiveCheckpoint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = formBytes(
		        {WIDGET +
		         "/FT /Tx /Ff 16777216 /MaxLen 4.0 /T (code) /V (old)"});
		    const auto completed = pdf::openDocument(bytes);
		    ASSERT_TRUE(completed.document);
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateTextField(0, 0, u"old", u"1234",
		                                                  1,
		                                                  [&] {
			                                                  ++checkpoints;
			                                                  return false;
		                                                  }),
		              true);
		    ASSERT_GT(checkpoints, 10);
		    for (int stop = 1; stop <= checkpoints; ++stop) {
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    int calls = 0;
			    EXPECT_FALSE(opened.document->updateTextField(
			        0, 0, u"old", u"1234", 1, [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, MultilineTextNormalizesParagraphsAndRoundTrips) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::array<std::pair<std::u16string, std::u16string>, 10>
		        cases{{{u"first\nsecond", u"first\r\nsecond"},
		               {u"first\rsecond", u"first\r\nsecond"},
		               {u"first\r\nsecond", u"first\r\nsecond"},
		               {u"\nfirst\n", u"\r\nfirst\r\n"},
		               {u"first\n\nthird", u"first\r\n\r\nthird"},
		               {u"\r\n", u"\r\n"},
		               {u"\r\r", u"\r\n\r\n"},
		               {u"", u""},
		               {u"single", u"single"},
		               {u"a\r\nb\rc\nd", u"a\r\nb\r\nc\r\nd"}}};
		    for (std::size_t sample = 0; sample < cases.size(); ++sample) {
			    SCOPED_TRACE(sample);
			    const auto& [input, stored] = cases[sample];
			    const auto bytes = formBytes(
			        {WIDGET + "/FT /Tx /Ff 4096 /T (field) /V (old)"});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    const auto old = doc.formFields(0);
			    ASSERT_TRUE(old && old->entries.size() == 1);
			    ASSERT_EQ(
			        doc.updateTextField(0, 0, u"old", input, old->revision),
			        true);
			    const auto fields = doc.formFields(0);
			    ASSERT_TRUE(fields && fields->entries.size() == 1);
			    EXPECT_GT(fields->revision, old->revision);
			    EXPECT_EQ(fields->entries[0].flags, 4096);
			    std::string expected;
			    for (const auto unit : stored) {
				    expected += static_cast<char>(unit);
			    }
			    EXPECT_EQ(fields->entries[0].value, expected);
			    EXPECT_EQ(old->entries[0].value, "old");
			    EXPECT_EQ(
			        doc.updateTextField(0, 0, stored, input, fields->revision),
			        false);
			    EXPECT_FALSE(
			        doc.updateTextField(0, 0, stored, u"stale", old->revision));
			    const auto saved = doc.serializeForCopy(1024 * 1024);
			    ASSERT_TRUE(saved);
			    const auto reopened = pdf::openDocument(*saved);
			    ASSERT_TRUE(reopened.document);
			    const auto kept = reopened.document->formFields(0);
			    ASSERT_TRUE(kept && kept->entries.size() == 1);
			    EXPECT_EQ(kept->entries[0].value, fields->entries[0].value);
			    EXPECT_EQ(doc.annotationRevision(), fields->revision);
		    }
	    })
	    .get();
}

TEST(FormFields, MultilineNoOpsKeepRawLineEndingsAndExactTargets) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes = formBytes(
		        {WIDGET + "/FT /Tx /Ff 4096 /T (field) /V (first\\nsecond)"},
		        {}, 1, {.appearances = false});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto before = doc.formFields(0);
		    ASSERT_TRUE(before && before->entries.size() == 1);
		    EXPECT_EQ(before->entries[0].value, "first\nsecond");
		    EXPECT_EQ(doc.updateTextField(0, 0, u"first\nsecond",
		                                  u"first\nsecond", before->revision),
		              false);
		    EXPECT_FALSE(doc.updateTextField(
		        0, 0, u"first\r\nsecond", u"changed\nline", before->revision));
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.annotationRevision(), before->revision);
		    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
		    EXPECT_FALSE(pdf::DocumentFormTest::hasAppearance(doc));
		    ASSERT_EQ(doc.updateTextField(0, 0, u"first\nsecond",
		                                  u"first\nthird", before->revision),
		              true);
		    const auto after = doc.formFields(0);
		    ASSERT_TRUE(after && after->entries.size() == 1);
		    EXPECT_EQ(after->entries[0].value, "first\r\nthird");
		    EXPECT_EQ(before->entries[0].value, "first\nsecond");
	    })
	    .get();
}

TEST(FormFields, MultilineUnicodeKeepsOtherFieldsNotesAndBookmarks) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes({WIDGET + "/FT /Tx /Ff 4102 /T (field) /V (old)",
		                   WIDGET + "/FT /Tx /T (untouched) /V (keep)"},
		                  {}, 1, {.outline = true});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.outline());
		    ASSERT_EQ(doc.addTextNote(0, 50, 60, u"Keep this note"), 2);
		    ASSERT_EQ(doc.updateTextField(0, 0, u"old",
		                                  u"Caf\u00e9\n\U0001F600",
		                                  doc.annotationRevision()),
		              true);
		    const auto saved = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(saved);
		    const auto reopened = pdf::openDocument(*saved);
		    ASSERT_TRUE(reopened.document);
		    const auto fields = reopened.document->formFields(0);
		    ASSERT_TRUE(fields && fields->entries.size() == 2);
		    EXPECT_EQ(fields->entries[0].value, "Caf\u00e9\r\n\U0001F600");
		    EXPECT_EQ(fields->entries[1].value, "keep");
		    EXPECT_EQ(reopened.document->textNote(0, 2), u"Keep this note");
		    ASSERT_TRUE(reopened.document->outline());
		    const auto link = reopened.document->outlineLink(0);
		    ASSERT_TRUE(link && link->destination);
		    EXPECT_EQ(link->destination->page, 0);
	    })
	    .get();
}

TEST(FormFields, MultilineLimitsRefuseTruncationAndExpandedSnapshots) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const int limit : {2, 3}) {
			    SCOPED_TRACE(limit);
			    const auto bytes = formBytes(
			        {WIDGET + "/FT /Tx /Ff 4096 /T (field) /V (x) /MaxLen " +
			         std::to_string(limit)});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    const auto result = doc.updateTextField(0, 0, u"x", u"a\nb", 1);
			    if (limit == 2) {
				    EXPECT_FALSE(result);
				    EXPECT_FALSE(doc.modified());
				    EXPECT_EQ(doc.annotationRevision(), 1u);
				    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
			    } else {
				    ASSERT_EQ(result, true);
				    const auto fields = doc.formFields(0);
				    ASSERT_TRUE(fields && fields->entries.size() == 1);
				    EXPECT_EQ(fields->entries[0].value, "a\r\nb");
			    }
		    }
		    const auto bytes =
		        formBytes({WIDGET + "/FT /Tx /Ff 4096 /T (field) /V (x)"});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    for (const auto& input : {std::u16string(16384, u'\n'),
		                              std::u16string(u"tab\tvalue")}) {
			    EXPECT_FALSE(
			        opened.document->updateTextField(0, 0, u"x", input, 1));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, MultilineCancellationKeepsEveryLiveCheckpoint) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes({WIDGET + "/FT /Tx /Ff 4096 /T (field) /V (old)"});
		    const auto completed = pdf::openDocument(bytes);
		    ASSERT_TRUE(completed.document);
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateTextField(0, 0, u"old",
		                                                  u"first\nsecond", 1,
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
			        0, 0, u"old", u"first\nsecond", 1,
			        [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, RefusedOrTruncatedTextWritesKeepOriginalBytesAndRevision) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const std::string extra :
		         {"/MaxLen 3", "/Ff 1", "/Ff 4097", "/Ff 12288", "/Ff 8392704",
		          "/Ff 33558528", "/Ff 8192", "/Ff 1048576", "/Ff 16777216",
		          "/Ff 33554432", "/F 2", "/F 32", "/F 64", "/F 128", "/F 512",
		          "/AA << /Fo << /S /ResetForm >> >>", "/A <<>>"}) {
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
		    // the counter can't see this field, so the token check names it.
		    EXPECT_EQ(nested.document->writeRefusal(),
		              pdf::WriteRefusal::Signed);
		    EXPECT_FALSE(nested.document->addTextNote(0, 80, 100, u"New"));
		    EXPECT_FALSE(nested.document->highlightText({{0, 0}, {0, 1}}));
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

namespace {
void expectInheritedCheckBoxRefusal(const std::vector<std::uint8_t>& bytes,
                                    bool checked) {
	auto saved = bytes;
	for (int pass = 0; pass < 2; ++pass) {
		SCOPED_TRACE(pass);
		const auto opened = pdf::openDocument(saved);
		ASSERT_TRUE(opened.document);
		auto& doc = *opened.document;
		const auto before = doc.formFields(0);
		ASSERT_TRUE(before && !before->limited);
		ASSERT_EQ(before->entries.size(), 1u);
		const auto& field = before->entries[0];
		EXPECT_EQ(field.type, pdf::FormFieldType::CheckBox);
		EXPECT_EQ(field.name, "box");
		EXPECT_EQ(field.flags, 6);
		EXPECT_EQ(field.annotationFlags, 4);
		EXPECT_EQ(field.checked, checked);
		EXPECT_EQ(field.value, checked ? "Yes" : "Off");
		ASSERT_TRUE(field.control);
		EXPECT_EQ(field.control->count, 1);
		EXPECT_EQ(field.control->index, 0);
		EXPECT_EQ(field.control->exportValue, "Yes");
		for (const bool desired : {false, true}) {
			SCOPED_TRACE(desired);
			EXPECT_FALSE(doc.updateCheckBox(0, 0, checked ? u"Yes" : u"Off",
			                                desired, before->revision));
			EXPECT_FALSE(doc.modified());
			EXPECT_EQ(doc.annotationRevision(), before->revision);
			EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
		}
		const auto copy = doc.serializeForCopy(1024 * 1024);
		ASSERT_TRUE(copy);
		EXPECT_EQ(*copy, bytes);
		saved = *copy;
	}
}
} // namespace

TEST(FormFields, InheritedCheckBoxesRefuseHiddenParentValueDisagreements) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const bool checked : {false, true}) {
			    for (const std::string value :
			         {checked ? "/Yes" : "/Off", "/Other", "(Yes)", "17"}) {
				    SCOPED_TRACE(checked);
				    SCOPED_TRACE(value);
				    const auto bytes = formBytes(
				        {WIDGET + "/F 4 /AS /" + (checked ? "Yes" : "Off") +
				         " /AP << /N << /Yes 8 0 R /Off 7 0 R >> >>"},
				        "/FT /Btn /T (box) /Ff 6 /V " + value, 1,
				        {.appearances = false, .painted = true});
				    expectInheritedCheckBoxRefusal(bytes, checked);
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, InheritedCheckBoxesRefuseMatchingExportsWithParentOptions) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const bool checked : {false, true}) {
			    for (const std::string value :
			         {checked ? "/Yes" : "/Off", "/0"}) {
				    SCOPED_TRACE(checked);
				    SCOPED_TRACE(value);
				    const auto bytes = formBytes(
				        {WIDGET + "/F 4 /AS /" + (checked ? "Yes" : "Off") +
				         " /AP << /N << /Yes 8 0 R /Off 7 0 R >> >>"},
				        "/FT /Btn /T (box) /Ff 6 /Opt [(Yes)] /V " + value, 1,
				        {.appearances = false, .painted = true});
				    expectInheritedCheckBoxRefusal(bytes, checked);
			    }
		    }
	    })
	    .get();
}

namespace {
// three plain kids under one radio parent; their on names are the exports
std::vector<std::string> radioKids(const std::array<std::string, 3>& names,
                                   int on) {
	std::vector<std::string> kids;
	for (int i = 0; i < 3; ++i) {
		kids.push_back(WIDGET + "/AS /" + (i == on ? names[i] : "Off") +
		               " /AP << /N << /" + names[i] +
		               " 8 0 R /Off 7 0 R >> >>");
	}
	return kids;
}
// BMP-only, enough for the snapshot values in these fixtures
std::u16string utf16(const std::string& text) {
	std::u16string out;
	for (std::size_t i = 0; i < text.size();) {
		const auto lead = static_cast<unsigned char>(text[i]);
		const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : 3;
		auto unit = static_cast<char16_t>(
		    length == 1 ? lead : lead & (length == 2 ? 0x1F : 0x0F));
		for (std::size_t k = 1; k < length && i + k < text.size(); ++k) {
			unit = static_cast<char16_t>(
			    (unit << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F));
		}
		out.push_back(unit);
		i += length;
	}
	return out;
}
bool storesName(const std::vector<std::uint8_t>& bytes, const std::string& key,
                const std::string& name) {
	const std::string text(bytes.begin(), bytes.end());
	for (auto at = text.find(key); at != std::string::npos;
	     at = text.find(key, at + 1)) {
		auto next = at + key.size();
		while (next < text.size() &&
		       std::isspace(static_cast<unsigned char>(text[next]))) {
			++next;
		}
		const auto end = next + name.size();
		if (text.compare(next, name.size(), name) == 0 &&
		    (end == text.size() ||
		     std::string_view("/ \r\n\t>]").find(text[end]) !=
		         std::string_view::npos)) {
			return true;
		}
	}
	return false;
}
std::vector<std::uint8_t> rawPdf(const std::vector<std::string>& objects) {
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
struct RadioTarget {
	std::u16string value;
	pdf::FormControl control;
};
std::optional<RadioTarget> radioTarget(const pdf::Document& document,
                                       int index) {
	const auto fields = document.formFields(0);
	if (!fields || fields->limited) {
		return {};
	}
	for (const auto& field : fields->entries) {
		if (field.index == index && field.control) {
			return RadioTarget{utf16(field.value), *field.control};
		}
	}
	return {};
}
void expectChoice(const std::optional<pdf::FormFields>& fields, int chosen,
                  const std::string& value, std::size_t count = 3) {
	ASSERT_TRUE(fields && !fields->limited);
	ASSERT_EQ(fields->entries.size(), count);
	for (std::size_t i = 0; i < count; ++i) {
		SCOPED_TRACE(i);
		const auto& field = fields->entries[i];
		EXPECT_EQ(field.type, pdf::FormFieldType::RadioButton);
		EXPECT_EQ(field.checked, static_cast<int>(i) == chosen);
		EXPECT_EQ(field.value, value);
		ASSERT_TRUE(field.control);
		EXPECT_EQ(field.control->index, static_cast<int>(i));
		EXPECT_EQ(field.control->count, static_cast<int>(count));
	}
}
} // namespace

TEST(FormFields, RadioWritesSelectOneMemberAndSaveIt) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::array<std::string, 3> names{"First", "Caf#E9", "Third"};
		    for (int on : {-1, 1}) {
			    SCOPED_TRACE(on);
			    const auto bytes =
			        formBytes(radioKids(names, on),
			                  "/FT /Btn /Ff 49152 /T (radio) /V /" +
			                      (on < 0 ? std::string("Off") : names[on]),
			                  1, {.appearances = false, .painted = true});
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    const auto old = doc.formFields(0);
			    ASSERT_TRUE(old);
			    expectChoice(old, on, on < 0 ? "Off" : "Café");
			    const auto third = radioTarget(doc, 2);
			    ASSERT_TRUE(third);
			    EXPECT_EQ(third->control.exportValue, "Third");
			    ASSERT_EQ(doc.updateRadioButton(0, 2, third->value,
			                                    third->control, old->revision),
			              true);
			    EXPECT_TRUE(doc.modified());
			    EXPECT_GT(doc.annotationRevision(), old->revision);
			    EXPECT_EQ(old->entries[2].checked, false);
			    expectChoice(doc.formFields(0), 2, "Third");
			    const auto saved = doc.serializeForCopy(1024 * 1024);
			    ASSERT_TRUE(saved);
			    const auto reopened = pdf::openDocument(*saved);
			    ASSERT_TRUE(reopened.document);
			    expectChoice(reopened.document->formFields(0), 2, "Third");
			    // the shared value follows the choice, not just the states
			    EXPECT_TRUE(storesName(*saved, "/V", "/Third"));

			    EXPECT_FALSE(doc.updateRadioButton(
			        0, 2, third->value, third->control, old->revision));
			    const auto revision = doc.annotationRevision();
			    const auto chosen = radioTarget(doc, 2);
			    ASSERT_TRUE(chosen);
			    EXPECT_EQ(doc.updateRadioButton(0, 2, chosen->value,
			                                    chosen->control, revision),
			              false);
			    EXPECT_EQ(doc.annotationRevision(), revision);
			    const auto first = radioTarget(doc, 0);
			    ASSERT_TRUE(first);
			    for (const auto& wrong : {pdf::FormControl{0, 3, "Third"},
			                              pdf::FormControl{1, 3, "First"},
			                              pdf::FormControl{0, 2, "First"},
			                              pdf::FormControl{0, 4, "First"}}) {
				    EXPECT_FALSE(doc.updateRadioButton(0, 0, first->value,
				                                       wrong, revision));
			    }
			    EXPECT_FALSE(doc.updateRadioButton(0, 0, u"First",
			                                       first->control, revision));
			    EXPECT_FALSE(doc.updateRadioButton(0, 1, first->value,
			                                       first->control, revision));
			    EXPECT_FALSE(
			        doc.updateCheckBox(0, 0, first->value, true, revision));
			    EXPECT_FALSE(doc.updateRadioButton(1, 0, first->value,
			                                       first->control, revision));
			    EXPECT_EQ(doc.annotationRevision(), revision);
			    ASSERT_EQ(doc.updateRadioButton(0, 0, first->value,
			                                    first->control, revision),
			              true);
			    expectChoice(doc.formFields(0), 0, "First");
		    }
	    })
	    .get();
}

TEST(FormFields, RadioOptionLabelsWriteTheirOwnControl) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(formBytes(
		        {WIDGET + "/AS /0 /AP << /N << /0 8 0 R /Off 7 0 R >> >>",
		         WIDGET + "/AS /Off /AP << /N << /1 8 0 R /Off 7 0 R >> >>"},
		        "/FT /Btn /Ff 49152 /T (choice) /V /0 "
		        "/Opt [(First label) (Second label)]",
		        1, {.appearances = false, .painted = true}));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    expectChoice(doc.formFields(0), 0, "First label", 2);
		    const auto second = radioTarget(doc, 1);
		    ASSERT_TRUE(second);
		    EXPECT_EQ(second->control.exportValue, "Second label");
		    ASSERT_EQ(doc.updateRadioButton(0, 1, second->value,
		                                    second->control,
		                                    doc.annotationRevision()),
		              true);
		    expectChoice(doc.formFields(0), 1, "Second label", 2);
		    const auto saved = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(saved);
		    const auto reopened = pdf::openDocument(*saved);
		    ASSERT_TRUE(reopened.document);
		    expectChoice(reopened.document->formFields(0), 1, "Second label",
		                 2);
	    })
	    .get();
}

TEST(FormFields, SingleRadioFieldsSelectWithoutAParent) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::string solo =
		        WIDGET + "/FT /Btn /Ff 32768 /T (solo) /V /Off /AS /Off "
		                 "/AP << /N << /Only 8 0 R /Off 7 0 R >> >>";
		    const auto opened = pdf::openDocument(formBytes(
		        {solo}, {}, 1, {.appearances = false, .painted = true}));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto target = radioTarget(doc, 0);
		    ASSERT_TRUE(target);
		    EXPECT_EQ(target->control.count, 1);
		    ASSERT_EQ(doc.updateRadioButton(0, 0, target->value,
		                                    target->control,
		                                    doc.annotationRevision()),
		              true);
		    expectChoice(doc.formFields(0), 0, "Only", 1);
		    const auto bytes =
		        formBytes({solo + " /Opt [(Only)]"}, {}, 1,
		                  {.appearances = false, .painted = true});
		    const auto refused = pdf::openDocument(bytes);
		    ASSERT_TRUE(refused.document);
		    EXPECT_FALSE(refused.document->updateRadioButton(
		        0, 0, u"Off", pdf::FormControl{0, 1, "Only"}, 1));
		    EXPECT_EQ(refused.document->serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(FormFields, RefusedRadioGroupsLeaveBytesAndRevisionIntact) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const std::array<std::string, 3> names{"First", "Second", "Third"};
		    const std::string parent = "/FT /Btn /T (radio) /V /Off ";
		    const FormPageOptions view{.appearances = false, .painted = true};
		    const auto base = radioKids(names, -1);
		    {
			    // the same group without the defects below accepts the write
			    const auto opened = pdf::openDocument(
			        formBytes(base, parent + "/Ff 49154", 1, view));
			    ASSERT_TRUE(opened.document);
			    EXPECT_EQ(opened.document->updateRadioButton(
			                  0, 0, u"Off", pdf::FormControl{0, 3, "First"}, 1),
			              true);
		    }
		    std::vector<std::vector<std::uint8_t>> cases;
		    for (const std::string extra :
		         {"/Ff 33603584", "/Ff 49153", "/Ff 16384",
		          "/Ff 49152 /AA << /K << /S /JavaScript /JS (1) >> >>",
		          "/Ff 49152 /AA << /F << /S /JavaScript /JS (1) >> >>",
		          "/Ff 49152 /AA << /V << /S /JavaScript /JS (1) >> >>",
		          "/Ff 49152 /AA << /C << /S /JavaScript /JS (1) >> >>"}) {
			    cases.push_back(formBytes(base, parent + extra, 1, view));
		    }
		    for (const std::string extra :
		         {"/A <<>>", "/AA << /U << /S /ResetForm >> >>", "/F 1", "/F 2",
		          "/F 32", "/F 64", "/F 128", "/F 512", "/V /Off", "/DV /Off",
		          "/Opt [(x)]", "/FT /Btn", "/Ff 49152"}) {
			    for (int kid : {0, 1}) {
				    auto kids = base;
				    kids[kid] += " " + extra;
				    cases.push_back(
				        formBytes(kids, parent + "/Ff 49152", 1, view));
			    }
		    }
		    for (const auto& kid :
		         {WIDGET +
		              "/AS (Off) /AP << /N << /Second 8 0 R /Off 7 0 R >> >>",
		          WIDGET +
		              "/AS /Other /AP << /N << /Second 8 0 R /Off 7 0 R >> >>",
		          WIDGET +
		              "/AS /Off /AP << /N << /First 8 0 R /Off 7 0 R >> >>"}) {
			    auto kids = base;
			    kids[1] = kid;
			    cases.push_back(formBytes(kids, parent + "/Ff 49152", 1, view));
		    }
		    // two members checked at once, and a checked member without
		    // printable checked content
		    auto both = radioKids(names, 0);
		    both[1] = radioKids(names, 1)[1];
		    cases.push_back(formBytes(
		        both, "/FT /Btn /T (radio) /V /First /Ff 49152", 1, view));
		    cases.push_back(formBytes(
		        radioKids(names, 1), "/FT /Btn /T (radio) /V /Second /Ff 49152",
		        1, {.appearances = false}));
		    // a member missing from the page, and members listed twice
		    {
			    auto bytes = formBytes(base, parent + "/Ff 49152", 1, view);
			    std::string text(bytes.begin(), bytes.end());
			    const std::string annots = "/Annots [9 0 R 10 0 R 11 0 R ";
			    const auto at = text.find(annots);
			    ASSERT_NE(at, std::string::npos);
			    text.replace(at + annots.size() - 7, 6, "      ");
			    cases.emplace_back(text.begin(), text.end());
		    }
		    cases.push_back(formBytes(base, parent + "/Ff 49152", 2, view));
		    cases.push_back(formBytes(base, parent + "/Ff 49152", 1,
		                              {.appearances = false,
		                               .nestedSignature = true,
		                               .painted = true}));
		    for (std::size_t i = 0; i < cases.size(); ++i) {
			    SCOPED_TRACE(i);
			    const auto& bytes = cases[i];
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    // the document's own snapshot, so only the defect stands
			    // between the request and the write
			    const auto target = radioTarget(doc, 0);
			    ASSERT_TRUE(target);
			    EXPECT_FALSE(doc.updateRadioButton(0, 0, target->value,
			                                       target->control, 1));
			    EXPECT_FALSE(doc.modified());
			    EXPECT_EQ(doc.annotationRevision(), 1u);
			    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
		    }
		    const auto box =
		        formBytes({WIDGET + "/FT /Btn /T (box) /V /Off /AS /Off "
		                            "/AP << /N << /Yes 8 0 R /Off 7 0 R >> >>"},
		                  {}, 1, view);
		    const auto opened = pdf::openDocument(box);
		    ASSERT_TRUE(opened.document);
		    EXPECT_FALSE(opened.document->updateRadioButton(
		        0, 0, u"Off", pdf::FormControl{0, 1, "Yes"}, 1));
		    EXPECT_EQ(opened.document->serialize(1024 * 1024), box);
		    // pushbuttons have no group snapshot, so this request is explicit
		    const auto push = formBytes(base, parent + "/Ff 65536", 1, view);
		    const auto button = pdf::openDocument(push);
		    ASSERT_TRUE(button.document);
		    EXPECT_FALSE(button.document->updateRadioButton(
		        0, 0, u"Off", pdf::FormControl{0, 3, "First"}, 1));
		    EXPECT_EQ(button.document->serialize(1024 * 1024), push);
	    })
	    .get();
}

TEST(FormFields, MergedActionsAreRefusedWithoutBlockingSupportedWrites) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto fixture = [](int kind, const std::string& action) {
			    const bool script = action == "script";
			    const std::string field =
			        kind == 0   ? "/FT /Tx /T (field) /V (old)"
			        : kind == 1 ? "/FT /Btn /T (field) /V /Off"
			                    : "/FT /Btn /Ff 49152 /T (field) /V /First";
			    const auto button = [](const std::string& on, bool checked) {
				    return WIDGET + "/AS /" + (checked ? on : "Off") +
				           " /AP << /N << /" + on + " 10 0 R /Off 11 0 R >> >>";
			    };
			    std::vector<std::string> objects{
			        "<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R " +
			            std::string(script ? "/Names << /JavaScript << /Names "
			                                 "[(stored) 15 0 R] >> >> "
			                               : "") +
			            ">>",
			        "<< /Type /Pages /Kids [4 0 R] /Count 1 >>",
			        "<< /Fields [5 0 R 6 0 R 12 0 R] >>",
			        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
			        "/Annots [" +
			            std::string(kind == 2 ? "7 0 R 8 0 R 9 0 R" : "6 0 R") +
			            " 12 0 R] >>",
			        "<< " + field + " " + (script ? "" : action) + " >>",
			        kind == 2
			            ? "<< " + field + " /Kids [7 0 R 8 0 R 9 0 R] >>"
			            : "<< /Type /Annot /P 4 0 R " + field + " " +
			                  (kind == 0 ? WIDGET : button("Yes", false)) +
			                  " >>",
			        "<< /Type /Annot /Parent 6 0 R /P 4 0 R " +
			            button("First", true) + " >>",
			        "<< /Type /Annot /Parent 6 0 R /P 4 0 R " +
			            button("Second", false) + " >>",
			        "<< /Type /Annot /Parent 6 0 R /P 4 0 R " +
			            button("Third", false) + " >>",
			        "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] "
			        "/Length 23 >>\nstream\n1 0 0 rg 0 0 50 20 re "
			        "f\nendstream",
			        "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] "
			        "/Length 0 >>\nstream\nendstream",
			        "<< /Type /Annot /P 4 0 R " + WIDGET +
			            "/FT /Tx /T (other) /V (keep) /DV (reset) >>",
			        "<< /V << /S /ResetForm /Fields [(other)] >> >>",
			        "<< /S /ResetForm /Fields [(other)] >>"};
			    if (script) {
				    objects.push_back("<< /S /JavaScript /JS (stored) >>");
			    }
			    return rawPdf(objects);
		    };
		    for (int kind = 0; kind < 3; ++kind) {
			    for (const auto& action :
			         {"", "/AA << /V << /S /ResetForm /Fields [(other)] >> >>",
			          "/#41#41 13 0 R", "/A 14 0 R", "script"}) {
				    for (const bool change : {true, false}) {
					    SCOPED_TRACE(kind);
					    SCOPED_TRACE(action);
					    SCOPED_TRACE(change);
					    const auto bytes = fixture(kind, action);
					    const auto opened = pdf::openDocument(bytes);
					    ASSERT_TRUE(opened.document);
					    auto& doc = *opened.document;
					    const auto before = doc.formFields(0);
					    ASSERT_TRUE(before && !before->limited);
					    ASSERT_EQ(before->entries.size(), kind == 2 ? 4u : 2u);
					    EXPECT_EQ(before->entries.back().value, "keep");
					    if (kind != 0) {
						    ASSERT_TRUE(before->entries[0].control);
						    EXPECT_EQ(before->entries[0].control->count,
						              kind == 2 ? 3 : 1);
					    }
					    const auto result =
					        kind == 0
					            ? doc.updateTextField(
					                  0, 0, u"old", change ? u"new" : u"old", 1)
					        : kind == 1
					            ? doc.updateCheckBox(0, 0, u"Off", change, 1)
					            : doc.updateRadioButton(
					                  0, change ? 1 : 0, u"First",
					                  {change ? 1 : 0, 3,
					                   change ? "Second" : "First"},
					                  1);
					    const bool plain = std::string_view(action).empty();
					    if (plain && (kind != 1 || !change)) {
						    ASSERT_TRUE(result);
						    EXPECT_EQ(*result, change);
					    } else {
						    ASSERT_FALSE(result);
					    }
					    const bool changed = plain && change && kind != 1;
					    EXPECT_EQ(doc.modified(), changed);
					    EXPECT_EQ(doc.annotationRevision(),
					              before->revision + (changed ? 1 : 0));
					    if (!changed) {
						    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
					    }
					    const auto after = doc.formFields(0);
					    ASSERT_TRUE(after && after->entries.size() ==
					                             before->entries.size());
					    EXPECT_EQ(after->entries.back().value, "keep");
				    }
			    }
		    }
	    })
	    .get();
}

TEST(FormFields, RadioWritesRefuseScriptsOnSameNamedParents) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto kid = [](int parent, const std::string& on) {
			    return WIDGET + "/P 4 0 R /Parent " + std::to_string(parent) +
			           " 0 R /AS /Off /AP << /N << /" + on +
			           " 10 0 R /Off 11 0 R >> >>";
		    };
		    const std::string parent =
		        "/FT /Btn /Ff 49152 /T (radio) /V /Off /Kids ";
		    const auto bytes = rawPdf(
		        {"<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R >>",
		         "<< /Type /Pages /Kids [4 0 R] /Count 1 >>",
		         "<< /Fields [5 0 R 6 0 R] >>",
		         "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
		         "/Annots [7 0 R 8 0 R 9 0 R] >>",
		         "<< " + parent + "[7 0 R 8 0 R] >>",
		         "<< " + parent +
		             "[9 0 R] /AA << /K << /S /JavaScript /JS (1) >> >> >>",
		         "<< /Type /Annot " + kid(5, "First") + " >>",
		         "<< /Type /Annot " + kid(5, "Second") + " >>",
		         "<< /Type /Annot " + kid(6, "Third") + " >>",
		         "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] "
		         "/Length 23 >>\nstream\n1 0 0 rg 0 0 50 20 re f\nendstream",
		         "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] "
		         "/Length 0 >>\nstream\nendstream"});
		    const auto opened = pdf::openDocument(bytes);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto target = radioTarget(doc, 0);
		    ASSERT_TRUE(target);
		    EXPECT_FALSE(
		        doc.updateRadioButton(0, 0, target->value, target->control, 1));
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.annotationRevision(), 1u);
		    EXPECT_EQ(doc.serialize(1024 * 1024), bytes);
	    })
	    .get();
}

TEST(FormFields, CancelingRadioTransactionKeepsLiveBytes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto bytes =
		        formBytes(radioKids({"First", "Second", "Third"}, 0),
		                  "/FT /Btn /Ff 49152 /T (radio) /V /First", 1,
		                  {.appearances = false, .painted = true});
		    const pdf::FormControl second{1, 3, "Second"};
		    const auto completed = pdf::openDocument(bytes);
		    ASSERT_TRUE(completed.document);
		    int checkpoints = 0;
		    ASSERT_EQ(completed.document->updateRadioButton(0, 1, u"First",
		                                                    second, 1,
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
			    EXPECT_FALSE(opened.document->updateRadioButton(
			        0, 1, u"First", second, 1,
			        [&] { return ++calls >= stop; }));
			    EXPECT_FALSE(opened.document->modified());
			    EXPECT_EQ(opened.document->annotationRevision(), 1u);
			    EXPECT_EQ(opened.document->serialize(1024 * 1024), bytes);
		    }
	    })
	    .get();
}

TEST(FormFields, RadioTransactionPreservesNotesAndBookmarks) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(formBytes(
		        radioKids({"First", "Second", "Third"}, 2),
		        "/FT /Btn /Ff 49152 /T (radio) /V /Third", 1,
		        {.appearances = false, .outline = true, .painted = true}));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.outline());
		    ASSERT_TRUE(doc.outlineLink(0));
		    ASSERT_EQ(doc.addTextNote(0, 50, 60, u"Keep this note"), 3);
		    const auto target = radioTarget(doc, 0);
		    ASSERT_TRUE(target);
		    ASSERT_EQ(doc.updateRadioButton(0, 0, target->value,
		                                    target->control,
		                                    doc.annotationRevision()),
		              true);
		    expectChoice(doc.formFields(0), 0, "First");
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 4);
		    EXPECT_EQ(notes->entries[3].contents, "Keep this note");
		    const auto link = doc.outlineLink(0);
		    ASSERT_TRUE(link && link->destination);
		    EXPECT_EQ(link->destination->page, 0);
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

TEST(FormFields, ActionLookingDataDoesNotRefuseTextEdits) {
	pdf::Engine engine;
	engine
	    .run([] {
		    // a stored deflate block keeps the action-looking bytes in the
		    // compressed stream, where they must not count as PDF names.
		    const std::string paint = "% /JS /JavaScript /A /AA\n";
		    std::string compressed("\x78\x01\x01", 3);
		    const auto size = static_cast<unsigned>(paint.size());
		    for (const auto value :
		         {size & 255, size >> 8, (~size) & 255, ((~size) >> 8) & 255}) {
			    compressed.push_back(static_cast<char>(value));
		    }
		    compressed += paint;
		    unsigned a = 1, b = 0;
		    for (const unsigned char byte : paint) {
			    a = (a + byte) % 65521;
			    b = (b + a) % 65521;
		    }
		    const auto adler = (b << 16) | a;
		    for (const auto shift : {24, 16, 8, 0}) {
			    compressed.push_back(static_cast<char>(adler >> shift));
		    }
		    const auto stream = "<< /Filter /FlateDecode /Length " +
		                        std::to_string(compressed.size()) +
		                        " >>\nstream\n" + compressed + "\nendstream";
		    for (const auto& bytes :
		         {formBytes({WIDGET + "/FT /Tx /T (field) /V (old) "
		                              "/TU (/JS /JavaScript /A /AA)"}),
		          formBytes({WIDGET + "/FT /Tx /T (field) /V (old) "
		                              "/JSuffix 1 /JavaScriptish 2"}),
		          formBytes({WIDGET + "/FT /Tx /T (field) /V (old)"}, {}, 1,
		                    {.reference = true, .stream = stream})}) {
			    const auto opened = pdf::openDocument(bytes);
			    ASSERT_TRUE(opened.document);
			    auto& document = *opened.document;
			    const auto updated = document.updateTextField(
			        0, 0, u"old", u"new", document.annotationRevision());
			    EXPECT_TRUE(updated && *updated);
			    if (!updated || !*updated) {
				    continue;
			    }
			    const auto saved = document.serializeForCopy(1024 * 1024);
			    ASSERT_TRUE(saved);
			    const auto reopened = pdf::openDocument(*saved);
			    ASSERT_TRUE(reopened.document);
			    const auto fields = reopened.document->formFields(0);
			    ASSERT_TRUE(fields && fields->entries.size() == 1);
			    EXPECT_EQ(fields->entries[0].value, "new");
		    }
	    })
	    .get();
}
