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
#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdfview.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf_test_files.h"

namespace {
std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
	std::ifstream file(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(file), {}};
}

// rotation and first text of every page in a saved copy, read straight
// from PDFium on the engine thread.
struct SavedPage {
	int rotation = -1;
	std::u16string text;
};
std::vector<SavedPage> savedPages(const std::vector<std::uint8_t>& bytes) {
	std::vector<SavedPage> pages;
	const auto document =
	    FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr);
	if (!document) {
		return pages;
	}
	for (int index = 0; index < FPDF_GetPageCount(document); ++index) {
		const auto page = FPDF_LoadPage(document, index);
		SavedPage saved;
		saved.rotation = page ? FPDFPage_GetRotation(page) : -1;
		if (const auto text = page ? FPDFText_LoadPage(page) : nullptr) {
			const int count = (std::min)(FPDFText_CountChars(text), 17);
			std::u16string chars(static_cast<std::size_t>(count) + 1, u'\0');
			FPDFText_GetText(text, 0, count,
			                 reinterpret_cast<unsigned short*>(chars.data()));
			chars.resize(static_cast<std::size_t>(count));
			saved.text = chars;
			FPDFText_ClosePage(text);
		}
		if (page) {
			FPDF_ClosePage(page);
		}
		pages.push_back(saved);
	}
	FPDF_CloseDocument(document);
	return pages;
}

std::vector<std::uint8_t> savedCopy(const pdf::Document& document) {
	auto bytes = document.serializeForCopy(16 * 1024 * 1024);
	return bytes ? *bytes : std::vector<std::uint8_t>{};
}
} // namespace

// pages.pdf: "Sable test page 1" (595 x 842), "Sable test page 2"
// (792 x 612) and "page 3" (300 x 200 with /Rotate 90).
TEST(PageEdits, RotationIsWrittenAndAddsToAnExistingRotation) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto revision = doc.annotationRevision();
		    ASSERT_TRUE(doc.rotatePages({0, 2}, 1, revision));
		    EXPECT_TRUE(doc.modified());
		    EXPECT_GT(doc.annotationRevision(), revision);
		    const auto first = doc.pageSize(0);
		    ASSERT_TRUE(first);
		    EXPECT_FLOAT_EQ(first->width, 842);
		    EXPECT_FLOAT_EQ(first->height, 595);
		    auto pages = savedPages(savedCopy(doc));
		    ASSERT_EQ(pages.size(), 3u);
		    EXPECT_EQ(pages[0].rotation, 1);
		    EXPECT_EQ(pages[1].rotation, 0);
		    // the rotated page goes from 90 to 180 degrees.
		    EXPECT_EQ(pages[2].rotation, 2);
		    // three more quarter turns bring the first page back upright.
		    ASSERT_TRUE(doc.rotatePages({0}, -1, doc.annotationRevision()));
		    ASSERT_TRUE(doc.rotatePages({2}, 2, doc.annotationRevision()));
		    pages = savedPages(savedCopy(doc));
		    EXPECT_EQ(pages[0].rotation, 0);
		    EXPECT_EQ(pages[2].rotation, 0);
		    EXPECT_FALSE(doc.rotatePages({1}, 4, doc.annotationRevision()));
	    })
	    .get();
}

TEST(PageEdits, DeletingKeepsTheOtherPagesInOrder) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = readBytes(fixturePath("pages.pdf"));
		    const auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.deletePages({1}, doc.annotationRevision()));
		    ASSERT_EQ(doc.pageCount(), 2);
		    const auto pages = savedPages(savedCopy(doc));
		    ASSERT_EQ(pages.size(), 2u);
		    EXPECT_EQ(pages[0].text, u"Sable test page 1");
		    EXPECT_EQ(pages[1].text, u"page 3");
		    EXPECT_EQ(pages[1].rotation, 1);
		    // a PDF keeps at least one page.
		    EXPECT_FALSE(doc.deletePages({0, 1}, doc.annotationRevision()));
		    EXPECT_EQ(doc.pageCount(), 2);
		    EXPECT_EQ(readBytes(fixturePath("pages.pdf")), original);
	    })
	    .get();
}

TEST(PageEdits, MovingPlacesTheBlockAtItsDestination) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.movePages({2, 0}, 0, doc.annotationRevision()));
		    auto pages = savedPages(savedCopy(doc));
		    ASSERT_EQ(pages.size(), 3u);
		    EXPECT_EQ(pages[0].text, u"page 3");
		    EXPECT_EQ(pages[0].rotation, 1);
		    EXPECT_EQ(pages[1].text, u"Sable test page 1");
		    EXPECT_EQ(pages[2].text, u"Sable test page 2");
		    ASSERT_TRUE(doc.movePages({0}, 2, doc.annotationRevision()));
		    pages = savedPages(savedCopy(doc));
		    EXPECT_EQ(pages[2].text, u"page 3");
		    const auto size = doc.pageSize(2);
		    ASSERT_TRUE(size);
		    EXPECT_FLOAT_EQ(size->width, 200);
		    EXPECT_FLOAT_EQ(size->height, 300);
	    })
	    .get();
}

TEST(PageEdits, BadSelectionsAndStaleRevisionsChangeNothing) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto revision = doc.annotationRevision();
		    EXPECT_FALSE(doc.rotatePages({}, 1, revision));
		    EXPECT_FALSE(doc.rotatePages({3}, 1, revision));
		    EXPECT_FALSE(doc.rotatePages({-1}, 1, revision));
		    EXPECT_FALSE(doc.deletePages({1, 1}, revision));
		    EXPECT_FALSE(doc.movePages({0}, 3, revision));
		    EXPECT_FALSE(doc.movePages({0, 1}, 2, revision));
		    EXPECT_FALSE(doc.movePages({0}, -1, revision));
		    EXPECT_FALSE(doc.rotatePages({0}, 1, revision + 1));
		    EXPECT_FALSE(
		        doc.rotatePages({0}, 1, revision, [] { return true; }));
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.annotationRevision(), revision);
		    EXPECT_EQ(doc.pageCount(), 3);
		    // after one edit the old revision no longer applies.
		    ASSERT_TRUE(doc.rotatePages({0}, 1, revision));
		    EXPECT_FALSE(doc.deletePages({0}, revision));
	    })
	    .get();
}

TEST(PageEdits, SignedFilesRefuseEveryPageEdit) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("note_signed.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto revision = doc.annotationRevision();
		    EXPECT_FALSE(doc.rotatePages({0}, 1, revision));
		    EXPECT_FALSE(doc.movePages({0}, 0, revision));
		    EXPECT_EQ(doc.writeRefusal(), pdf::WriteRefusal::Signed);
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.annotationRevision(), revision);
	    })
	    .get();
}
