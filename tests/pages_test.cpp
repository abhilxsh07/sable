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

TEST(PageEdits, InsertingCopiesPagesFromAnotherFile) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto source = readBytes(fixturePath("pages.pdf"));
		    const auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(
		        doc.insertPages(source, {1}, 0, doc.annotationRevision()));
		    auto pages = savedPages(savedCopy(doc));
		    ASSERT_EQ(pages.size(), 4u);
		    EXPECT_EQ(pages[0].text, u"Sable test page 2");
		    EXPECT_EQ(pages[1].text, u"Sable test page 1");
		    // every page, appended at the end, keeps its own rotation.
		    ASSERT_TRUE(
		        doc.insertPages(source, {}, 4, doc.annotationRevision()));
		    pages = savedPages(savedCopy(doc));
		    ASSERT_EQ(pages.size(), 7u);
		    EXPECT_EQ(pages[6].text, u"page 3");
		    EXPECT_EQ(pages[6].rotation, 1);
		    EXPECT_FALSE(
		        doc.insertPages(source, {3}, 0, doc.annotationRevision()));
		    EXPECT_FALSE(
		        doc.insertPages(source, {0}, 8, doc.annotationRevision()));
		    EXPECT_FALSE(doc.insertPages(readBytes(fixturePath("password.pdf")),
		                                 {}, 0, doc.annotationRevision()));
		    EXPECT_EQ(doc.pageCount(), 7);
		    EXPECT_EQ(readBytes(fixturePath("pages.pdf")), source);
	    })
	    .get();
}

TEST(PageEdits, SignedFilesRefuseInsertsButStillExtract) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto original = readBytes(fixturePath("note_signed.pdf"));
		    const auto opened = pdf::openFile(fixturePath("note_signed.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    EXPECT_FALSE(doc.insertPages(readBytes(fixturePath("pages.pdf")),
		                                 {0}, 0, doc.annotationRevision()));
		    EXPECT_EQ(doc.writeRefusal(), pdf::WriteRefusal::Signed);
		    const auto extracted = doc.extractPages({0});
		    ASSERT_TRUE(extracted);
		    EXPECT_EQ(savedPages(*extracted).size(), 1u);
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(readBytes(fixturePath("note_signed.pdf")), original);
	    })
	    .get();
}

TEST(PageEdits, ExtractingMakesANewPdfAndLeavesTheDocumentAlone) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto revision = doc.annotationRevision();
		    const auto bytes = doc.extractPages({2, 0});
		    ASSERT_TRUE(bytes);
		    const auto pages = savedPages(*bytes);
		    ASSERT_EQ(pages.size(), 2u);
		    EXPECT_EQ(pages[0].text, u"page 3");
		    EXPECT_EQ(pages[0].rotation, 1);
		    EXPECT_EQ(pages[1].text, u"Sable test page 1");
		    // the new PDF opens as a document of its own.
		    const auto reopened = pdf::openDocument(*bytes);
		    ASSERT_TRUE(reopened.document);
		    EXPECT_EQ(reopened.document->pageCount(), 2);
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.annotationRevision(), revision);
		    EXPECT_EQ(doc.pageCount(), 3);
		    EXPECT_FALSE(doc.extractPages({}));
		    EXPECT_FALSE(doc.extractPages({3}));
		    EXPECT_FALSE(doc.extractPages({0, 0}));
	    })
	    .get();
}

TEST(PageEdits, CombiningJoinsFilesInOrder) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto result = pdf::combineFiles(
		        {fixturePath("pages.pdf"), fixturePath("note_signed.pdf"),
		         fixturePath("pages.pdf")});
		    ASSERT_EQ(result.error, pdf::CombineError::None);
		    const auto pages = savedPages(result.bytes);
		    const auto opened = pdf::openFile(fixturePath("note_signed.pdf"));
		    ASSERT_TRUE(opened.document);
		    const auto middle =
		        static_cast<std::size_t>(opened.document->pageCount());
		    ASSERT_EQ(pages.size(), 6u + middle);
		    EXPECT_EQ(pages[0].text, u"Sable test page 1");
		    EXPECT_EQ(pages[2].rotation, 1);
		    EXPECT_EQ(pages[3 + middle].text, u"Sable test page 1");
		    const auto reopened = pdf::openDocument(result.bytes);
		    ASSERT_TRUE(reopened.document);
	    })
	    .get();
}

TEST(PageEdits, CombiningReportsProgressAndCancelsBetweenFiles) {
	pdf::Engine engine;
	engine
	    .run([] {
		    bool cancel = false;
		    std::vector<std::size_t> progress;
		    const auto result = pdf::combineFiles(
		        {fixturePath("long.pdf"), fixturePath("pages.pdf")},
		        128 * 1024 * 1024, [&] { return cancel; },
		        [&](std::size_t done, std::size_t total) {
			        EXPECT_EQ(total, 2u);
			        progress.push_back(done);
			        cancel = done == 1;
		        });
		    EXPECT_EQ(result.error, pdf::CombineError::Canceled);
		    EXPECT_TRUE(result.bytes.empty());
		    EXPECT_EQ(progress, (std::vector<std::size_t>{0, 1}));
	    })
	    .get();
}

TEST(PageEdits, CombiningReportsTheFileThatStoppedIt) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto missing = pdf::combineFiles(
		        {fixturePath("pages.pdf"), fixturePath("missing.pdf")});
		    EXPECT_EQ(missing.error, pdf::CombineError::Unreadable);
		    EXPECT_EQ(missing.file, 1u);
		    EXPECT_TRUE(missing.bytes.empty());
		    const auto locked = pdf::combineFiles(
		        {fixturePath("password.pdf"), fixturePath("pages.pdf")});
		    EXPECT_EQ(locked.error, pdf::CombineError::Password);
		    EXPECT_EQ(locked.file, 0u);
		    EXPECT_EQ(pdf::combineFiles({}).error, pdf::CombineError::Failed);
		    EXPECT_EQ(pdf::combineFiles({fixturePath("pages.pdf")}, 1024,
		                                [] { return true; })
		                  .error,
		              pdf::CombineError::TooLarge);
	    })
	    .get();
}

TEST(PageEdits, CombiningPastOneHundredTwentyEightMiBIsRefused) {
	// a real file just over the limit; PDF readers ignore the padding.
	const auto padded = std::filesystem::temp_directory_path() /
	                    "sable combine past the limit.pdf";
	{
		auto bytes = readBytes(fixturePath("pages.pdf"));
		bytes.resize(128 * 1024 * 1024 + 1, '\n');
		std::ofstream file(padded, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(bytes.data()),
		           static_cast<std::streamsize>(bytes.size()));
	}
	pdf::Engine engine;
	engine
	    .run([&padded] {
		    const auto result =
		        pdf::combineFiles({fixturePath("pages.pdf"), padded});
		    EXPECT_EQ(result.error, pdf::CombineError::TooLarge);
		    EXPECT_EQ(result.file, 1u);
		    EXPECT_TRUE(result.bytes.empty());
	    })
	    .get();
	std::error_code ignored;
	std::filesystem::remove(padded, ignored);
}

TEST(PageEdits, EditedPagesSaveToAFileThatReopens) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto source = fixturePath("pages.pdf");
		    const auto original = readBytes(source);
		    const auto opened = pdf::openFile(source);
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_TRUE(doc.rotatePages({1}, 1, doc.annotationRevision()));
		    ASSERT_TRUE(doc.movePages({2}, 0, doc.annotationRevision()));
		    ASSERT_TRUE(doc.deletePages({1}, doc.annotationRevision()));
		    ASSERT_TRUE(
		        doc.insertPages(original, {0}, 2, doc.annotationRevision()));
		    // Save a copy's own path: the file writer, then a fresh open.
		    const auto saved = std::filesystem::temp_directory_path() /
		                       "sable organized pages.pdf";
		    ASSERT_EQ(doc.saveCopy(saved, source), pdf::SaveResult::Saved);
		    const auto reopened = pdf::openFile(saved);
		    ASSERT_TRUE(reopened.document);
		    const auto pages = savedPages(readBytes(saved));
		    ASSERT_EQ(pages.size(), 3u);
		    EXPECT_EQ(pages[0].text, u"page 3");
		    EXPECT_EQ(pages[1].text, u"Sable test page 2");
		    EXPECT_EQ(pages[1].rotation, 1);
		    EXPECT_EQ(pages[2].text, u"Sable test page 1");
		    EXPECT_EQ(readBytes(source), original);
		    std::error_code ignored;
		    std::filesystem::remove(saved, ignored);
	    })
	    .get();
}
