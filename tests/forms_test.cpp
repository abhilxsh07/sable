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
#include "pdf/document.h"
#include "pdf/engine.h"
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
std::vector<std::uint8_t> formBytes(std::vector<std::string> fields,
                                    std::string parent = {}, int repeat = 1) {
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
	    "<< /Fields [" + roots +
	        "] /NeedAppearances true "
	        "/DA (/Helv 10 Tf 0 g) /DR << /Font << /Helv "
	        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >> >>",
	    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
	    "/CropBox [20 40 180 260] /Rotate 90 /Annots [" +
	        refs + "] >>",
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
