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

TEST(NoteVisibility, ImportedIconsStayHiddenWithoutChangingLiveNotes) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("comments.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto before = doc.annotations(0);
		    const auto original = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(before && original);
		    ASSERT_FALSE(before->entries.empty());
		    const auto screen = doc.render(0, 300, 400);
		    const auto print = doc.printRaster(0, 300, 400, 0, 0, 300, 400);
		    ASSERT_TRUE(screen && print);
		    const auto bytes = doc.serializeForCopy(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    EXPECT_NE(bytes, original);
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.serialize(1024 * 1024), original);
		    const auto live = doc.annotations(0);
		    ASSERT_TRUE(live);
		    ASSERT_EQ(live->entries.size(), before->entries.size());
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto notes = saved.document->annotations(0);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), before->entries.size());
		    for (std::size_t i = 0; i < notes->entries.size(); ++i) {
			    const auto& note = notes->entries[i];
			    EXPECT_EQ(live->entries[i].flags, before->entries[i].flags);
			    EXPECT_EQ(live->entries[i].contents,
			              before->entries[i].contents);
			    EXPECT_EQ(note.contents, before->entries[i].contents);
			    if (note.subtype == "Text") {
				    EXPECT_NE(note.flags & FPDF_ANNOT_FLAG_NOVIEW, 0);
				    EXPECT_EQ(note.flags & FPDF_ANNOT_FLAG_PRINT, 0);
			    } else {
				    EXPECT_EQ(note.flags, before->entries[i].flags);
			    }
		    }
		    const auto savedScreen = saved.document->render(0, 300, 400);
		    const auto savedPrint =
		        saved.document->printRaster(0, 300, 400, 0, 0, 300, 400);
		    ASSERT_TRUE(savedScreen && savedPrint);
		    EXPECT_EQ(screen->pixels, savedScreen->pixels);
		    EXPECT_EQ(print->pixels, savedPrint->pixels);
		    const auto rawRaster = [](const std::vector<std::uint8_t>& data) {
			    std::vector<std::uint8_t> pixels(300 * 400 * 4);
			    const auto document =
			        FPDF_LoadMemDocument64(data.data(), data.size(), nullptr);
			    const auto page =
			        document ? FPDF_LoadPage(document, 0) : nullptr;
			    const auto bitmap = FPDFBitmap_CreateEx(
			        300, 400, FPDFBitmap_BGRA, pixels.data(), 300 * 4);
			    if (page && bitmap) {
				    FPDFBitmap_FillRect(bitmap, 0, 0, 300, 400, 0xffffffff);
				    FPDF_RenderPageBitmap(bitmap, page, 0, 0, 300, 400, 0,
				                          FPDF_ANNOT);
			    } else {
				    pixels.clear();
			    }
			    if (bitmap) {
				    FPDFBitmap_Destroy(bitmap);
			    }
			    if (page) {
				    FPDF_ClosePage(page);
			    }
			    if (document) {
				    FPDF_CloseDocument(document);
			    }
			    return pixels;
		    };
		    EXPECT_NE(rawRaster(*original), screen->pixels);
		    EXPECT_EQ(rawRaster(*bytes), screen->pixels);
		    EXPECT_FALSE(doc.serializeForCopy(8));
		    EXPECT_FALSE(
		        doc.serializeForCopy(1024 * 1024, [] { return true; }));
		    EXPECT_EQ(doc.serialize(1024 * 1024), original);
	    })
	    .get();
}

TEST(NoteVisibility, LargeLinkInventoriesStillRenderWithoutAnEdit) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("annotations.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& document = *opened.document;
		    const auto bytes = document.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    EXPECT_TRUE(document.render(3, 100, 100));
		    EXPECT_TRUE(document.printRaster(3, 100, 100, 0, 0, 100, 100));
		    EXPECT_FALSE(document.modified());
		    EXPECT_EQ(document.serialize(1024 * 1024), bytes);
	    })
	    .get();
}

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
