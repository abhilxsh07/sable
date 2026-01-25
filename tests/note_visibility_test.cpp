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
#include <fstream>
#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf/note_visibility.h"
#include "pdf_test_files.h"

TEST(NoteVisibility, OutputExceptionsRestoreTheOriginalFlags) {
	pdf::Engine engine;
	engine
	    .run([] {
		    std::ifstream file(fixturePath("comments.pdf"), std::ios::binary);
		    const std::string bytes(std::istreambuf_iterator<char>(file), {});
		    const auto document =
		        FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr);
		    ASSERT_TRUE(document);
		    const auto page = FPDF_LoadPage(document, 0);
		    ASSERT_TRUE(page);
		    const auto note = FPDFPage_GetAnnot(page, 0);
		    ASSERT_TRUE(note);
		    const int flags = FPDFAnnot_GetFlags(note);
		    {
			    pdf::detail::NoteVisibility visibility;
			    ASSERT_TRUE(visibility.collect(page));
			    ASSERT_FALSE(visibility.empty());
			    EXPECT_THROW(visibility.run([&]() -> bool {
				    EXPECT_NE(FPDFAnnot_GetFlags(note) & FPDF_ANNOT_FLAG_NOVIEW,
				              0);
				    EXPECT_EQ(FPDFAnnot_GetFlags(note) & FPDF_ANNOT_FLAG_PRINT,
				              0);
				    throw std::runtime_error("output interrupted");
			    }),
			                 std::runtime_error);
			    EXPECT_EQ(FPDFAnnot_GetFlags(note), flags);
		    }
		    FPDFPage_CloseAnnot(note);
		    FPDF_ClosePage(page);
		    FPDF_CloseDocument(document);
	    })
	    .get();
}
