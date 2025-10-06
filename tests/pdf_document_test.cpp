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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf_test_files.h"

namespace fs = std::filesystem;

// gtest prints a bare enum class as raw bytes. This makes it print the name
// instead, and it has to sit next to the enum for gtest to find it.
namespace pdf {
inline void PrintTo(Error error, std::ostream* os) {
	switch (error) {
	case Error::None:
		*os << "Error::None";
		return;
	case Error::File:
		*os << "Error::File";
		return;
	case Error::Format:
		*os << "Error::Format";
		return;
	case Error::PasswordRequired:
		*os << "Error::PasswordRequired";
		return;
	case Error::WrongPassword:
		*os << "Error::WrongPassword";
		return;
	case Error::Security:
		*os << "Error::Security";
		return;
	case Error::Unknown:
		*os << "Error::Unknown";
		return;
	}
	*os << "Error(" << static_cast<int>(error) << ")";
}
} // namespace pdf

// The sample files come from tests/fixtures/make_fixtures.py, which says what
// is on each page.
namespace {
// What came of opening a file. These are all plain values, so they can leave
// the PDFium thread. A Document can't.
struct Opened {
	pdf::Error error = pdf::Error::None;
	bool hasDocument = false;
	int pageCount = 0;
	std::optional<pdf::PageSize> firstPage;
};

// what a file says about one of its pages
struct PageInfo {
	std::optional<pdf::PageSize> size;
	std::string label;
};

bool writeFile(const fs::path& path, const std::string& bytes) {
	std::ofstream out(path, std::ios::binary);
	out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	out.close();
	return !out.fail();
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
	std::vector<std::uint8_t> bytes(fs::file_size(path));
	std::ifstream in(path, std::ios::binary);
	in.read(reinterpret_cast<char*>(bytes.data()),
	        static_cast<std::streamsize>(bytes.size()));
	bytes.resize(static_cast<std::size_t>(in.gcount()));
	return bytes;
}

void expectSize(const std::optional<pdf::PageSize>& size, int index,
                float width, float height) {
	ASSERT_TRUE(size.has_value()) << "page " << index;
	EXPECT_FLOAT_EQ(size->width, width) << "page " << index;
	EXPECT_FLOAT_EQ(size->height, height) << "page " << index;
}

// Every test has an engine of its own. All the pdf:: calls are made inside
// the lambdas given to engine.run(), and only plain values come back out.
class PdfDocumentTest : public ::testing::Test {
  protected:
	void TearDown() override {
		std::error_code ec;
		for (const fs::path& dir : m_dirs) {
			fs::remove_all(dir, ec);
		}
	}

	// a new folder that's removed when the test is over
	fs::path tempDir(std::string_view tag) {
		m_dirs.push_back(makeTempDir(tag));
		return m_dirs.back();
	}

	// opens a file and reports how it went. The document is closed again when
	// the lambda ends, which is on the PDFium thread as well.
	Opened tryOpen(const fs::path& path, const std::string& password = {}) {
		const auto work = [path, password] {
			const auto result = pdf::openFile(path, password);
			Opened opened;
			opened.error = result.error;
			opened.hasDocument = result.document != nullptr;
			if (result.document) {
				opened.pageCount = result.document->pageCount();
				opened.firstPage = result.document->pageSize(0);
			}
			return opened;
		};
		return engine.run(work).get();
	}

	// the same for a file that's already in memory
	Opened tryOpenBytes(std::vector<std::uint8_t> bytes,
	                    const std::string& password = {}) {
		auto work = [data = std::move(bytes), password]() mutable {
			const auto result = pdf::openDocument(std::move(data), password);
			Opened opened;
			opened.error = result.error;
			opened.hasDocument = result.document != nullptr;
			if (result.document) {
				opened.pageCount = result.document->pageCount();
				opened.firstPage = result.document->pageSize(0);
			}
			return opened;
		};
		return engine.run(std::move(work)).get();
	}

	// asks one of the sample files about a page. The file is opened again for
	// every question, which is cheap for a file this small.
	PageInfo pageInfo(std::string_view file, int index) {
		const auto work = [path = fixturePath(file), index] {
			PageInfo info;
			const auto opened = pdf::openFile(path);
			if (opened.document) {
				info.size = opened.document->pageSize(index);
				info.label = opened.document->pageLabel(index);
			}
			return info;
		};
		return engine.run(work).get();
	}

	pdf::Engine engine;

  private:
	std::vector<fs::path> m_dirs;
};
} // namespace

TEST_F(PdfDocumentTest, OpensTheSampleFile) {
	const auto opened = tryOpen(fixturePath("pages.pdf"));
	EXPECT_EQ(opened.error, pdf::Error::None);
	EXPECT_TRUE(opened.hasDocument);
	EXPECT_EQ(opened.pageCount, 3);
}

TEST_F(PdfDocumentTest, PageSizesAreInPoints) {
	expectSize(pageInfo("pages.pdf", 0).size, 0, 595.0f, 842.0f);
	expectSize(pageInfo("pages.pdf", 1).size, 1, 792.0f, 612.0f);
}

// the third page is stored 300 x 200 with /Rotate 90, and it's shown turned
TEST_F(PdfDocumentTest, PageSizeCountsThePagesRotation) {
	expectSize(pageInfo("pages.pdf", 2).size, 2, 200.0f, 300.0f);
}

TEST_F(PdfDocumentTest, PageSizeIsNothingForAnIndexThatIsNotAPage) {
	EXPECT_FALSE(pageInfo("pages.pdf", -1).size.has_value());
	EXPECT_FALSE(pageInfo("pages.pdf", 3).size.has_value());
}

TEST_F(PdfDocumentTest, PageLabelsComeFromTheFile) {
	EXPECT_EQ(pageInfo("pages.pdf", 0).label, "i");
	EXPECT_EQ(pageInfo("pages.pdf", 1).label, "ii");
	EXPECT_EQ(pageInfo("pages.pdf", 2).label, "1");
}

TEST_F(PdfDocumentTest, PageLabelIsEmptyForAnIndexThatIsNotAPage) {
	EXPECT_EQ(pageInfo("pages.pdf", -1).label, "");
	EXPECT_EQ(pageInfo("pages.pdf", 3).label, "");
}

// restricted.pdf has no /PageLabels
TEST_F(PdfDocumentTest, PageLabelIsEmptyWhenTheFileHasNone) {
	EXPECT_EQ(pageInfo("restricted.pdf", 0).label, "");
}

TEST_F(PdfDocumentTest, MissingFileIsAFileError) {
	const auto opened = tryOpen(tempDir("missing") / "nothing.pdf");
	EXPECT_EQ(opened.error, pdf::Error::File);
	EXPECT_FALSE(opened.hasDocument);
}

TEST_F(PdfDocumentTest, EmptyFileIsAFormatError) {
	const fs::path path = tempDir("empty") / "empty.pdf";
	ASSERT_TRUE(writeFile(path, ""));

	const auto opened = tryOpen(path);
	EXPECT_EQ(opened.error, pdf::Error::Format);
	EXPECT_FALSE(opened.hasDocument);
}

TEST_F(PdfDocumentTest, EmptyBufferIsAFormatError) {
	const auto opened = tryOpenBytes({});
	EXPECT_EQ(opened.error, pdf::Error::Format);
	EXPECT_FALSE(opened.hasDocument);
}

// broken.pdf starts like a PDF and has nothing in it
TEST_F(PdfDocumentTest, BrokenFileIsAFormatError) {
	const auto opened = tryOpen(fixturePath("broken.pdf"));
	EXPECT_EQ(opened.error, pdf::Error::Format);
	EXPECT_FALSE(opened.hasDocument);
}

TEST_F(PdfDocumentTest, EncryptedFileAsksForAPassword) {
	const auto opened = tryOpen(fixturePath("password.pdf"));
	EXPECT_EQ(opened.error, pdf::Error::PasswordRequired);
	EXPECT_FALSE(opened.hasDocument);
}

TEST_F(PdfDocumentTest, WrongPasswordIsRejected) {
	const auto opened = tryOpen(fixturePath("password.pdf"), "wrong");
	EXPECT_EQ(opened.error, pdf::Error::WrongPassword);
	EXPECT_FALSE(opened.hasDocument);
}

TEST_F(PdfDocumentTest, UserPasswordOpensTheFile) {
	const auto opened = tryOpen(fixturePath("password.pdf"), "sable");
	EXPECT_EQ(opened.error, pdf::Error::None);
	EXPECT_TRUE(opened.hasDocument);
	EXPECT_EQ(opened.pageCount, 1);
	expectSize(opened.firstPage, 0, 595.0f, 842.0f);
}

TEST_F(PdfDocumentTest, OwnerPasswordOpensTheFileToo) {
	const auto opened = tryOpen(fixturePath("password.pdf"), "nightpdf");
	EXPECT_EQ(opened.error, pdf::Error::None);
	EXPECT_TRUE(opened.hasDocument);
	EXPECT_EQ(opened.pageCount, 1);
}

// all the permissions are off in restricted.pdf, but it has no user password
TEST_F(PdfDocumentTest, RestrictedFileOpensWithoutAPassword) {
	const auto opened = tryOpen(fixturePath("restricted.pdf"));
	EXPECT_EQ(opened.error, pdf::Error::None);
	EXPECT_TRUE(opened.hasDocument);
	EXPECT_EQ(opened.pageCount, 1);
}

TEST_F(PdfDocumentTest, PasswordForAFileThatNeedsNoneIsIgnored) {
	const auto opened = tryOpen(fixturePath("pages.pdf"), "sable");
	EXPECT_EQ(opened.error, pdf::Error::None);
	EXPECT_EQ(opened.pageCount, 3);
}

// spaces and letters outside ASCII, in the name of the folder and of the file
TEST_F(PdfDocumentTest, OpensAPathWithSpacesAndNonAsciiLetters) {
	const fs::path folder = tempDir("paths") / L"sable tests ü 日本";
	ASSERT_TRUE(fs::create_directories(folder));
	const fs::path target = folder / L"Ünïcödé file name.pdf";
	ASSERT_TRUE(fs::copy_file(fixturePath("pages.pdf"), target));

	const auto opened = tryOpen(target);
	EXPECT_EQ(opened.error, pdf::Error::None);
	EXPECT_EQ(opened.pageCount, 3);
}

TEST_F(PdfDocumentTest, OpensABufferInMemory) {
	auto bytes = readBytes(fixturePath("pages.pdf"));
	ASSERT_FALSE(bytes.empty());

	const auto opened = tryOpenBytes(std::move(bytes));
	EXPECT_EQ(opened.error, pdf::Error::None);
	EXPECT_EQ(opened.pageCount, 3);
}

TEST_F(PdfDocumentTest, ABufferTakesAPasswordToo) {
	const auto bytes = readBytes(fixturePath("password.pdf"));
	ASSERT_FALSE(bytes.empty());

	EXPECT_EQ(tryOpenBytes(bytes).error, pdf::Error::PasswordRequired);
	EXPECT_EQ(tryOpenBytes(bytes, "wrong").error, pdf::Error::WrongPassword);
	const auto opened = tryOpenBytes(bytes, "sable");
	EXPECT_EQ(opened.error, pdf::Error::None);
	EXPECT_EQ(opened.pageCount, 1);
}

TEST_F(PdfDocumentTest, LinksUseDisplayedCoordinatesAndRejectOtherActions) {
	const auto links =
	    engine
	        .run([] {
		        auto opened = pdf::openFile("tests/fixtures/links.pdf");
		        std::vector<std::optional<pdf::Link>> result;
		        if (!opened.document) {
			        return result;
		        }
		        for (int row = 0; row < 7; ++row) {
			        result.push_back(opened.document->linkAt(
			            0, 80.0 / 360, (570.0 - (520 - row * 80)) / 540));
		        }
		        result.push_back(
		            opened.document->linkAt(1, 140.0 / 500, 60.0 / 300));
		        result.push_back(
		            opened.document->linkAt(1, 140.0 / 500, 160.0 / 300));
		        result.push_back(
		            opened.document->linkAt(1, 250.0 / 500, 60.0 / 300));
		        result.push_back(opened.document->linkAt(0, 0.9, 0.1));
		        result.push_back(opened.document->linkAt(-1, 0, 0));
		        result.push_back(opened.document->linkAt(2, 0, 0));
		        result.push_back(opened.document->linkAt(0, -0.1, 0));
		        return result;
	        })
	        .get();
	ASSERT_EQ(links.size(), 14u);
	ASSERT_TRUE(links[0]);
	EXPECT_EQ(links[0]->uri, "https://example.com/sable?x=1#part");
	ASSERT_TRUE(links[1] && links[1]->destination);
	const auto xyz = *links[1]->destination;
	EXPECT_EQ(xyz.page, 1);
	ASSERT_TRUE(xyz.x && xyz.y && xyz.zoom);
	EXPECT_NEAR(*xyz.x, 0.8, 0.00001);
	EXPECT_NEAR(*xyz.y, 0.2, 0.00001);
	EXPECT_EQ(*xyz.zoom, 2);
	ASSERT_TRUE(links[2] && links[2]->destination);
	EXPECT_EQ(links[2]->destination->fit, pdf::LinkFit::Page);
	ASSERT_TRUE(links[3] && links[3]->destination);
	EXPECT_EQ(links[3]->destination->fit, pdf::LinkFit::Width);
	EXPECT_FALSE(links[4]);
	EXPECT_FALSE(links[5]);
	EXPECT_FALSE(links[6]);
	ASSERT_TRUE(links[7]);
	EXPECT_EQ(links[7]->uri, "HTTP://example.com/rotated");
	ASSERT_TRUE(links[8] && links[8]->destination);
	EXPECT_FALSE(links[8]->destination->x);
	EXPECT_FALSE(links[8]->destination->zoom);
	ASSERT_TRUE(links[8]->destination->y);
	EXPECT_NEAR(*links[8]->destination->y, 370.0 / 540, 0.00001);
	ASSERT_TRUE(links[9] && links[9]->destination);
	EXPECT_EQ(links[9]->destination->fit, pdf::LinkFit::Rectangle);
	EXPECT_NEAR(links[9]->destination->width, 160.0 / 360, 0.00001);
	EXPECT_NEAR(links[9]->destination->height, 300.0 / 540, 0.00001);
	for (std::size_t i = 10; i < links.size(); ++i) {
		EXPECT_FALSE(links[i]);
	}
}

// None is the first error in the enum and Unknown the last. errorMessage never
// touches PDFium, but it goes through the engine like the rest so every pdf::
// call in this file follows the same rule.
TEST_F(PdfDocumentTest, EveryErrorHasItsOwnMessage) {
	const auto work = [] {
		std::vector<std::string> all;
		for (int i = static_cast<int>(pdf::Error::None);
		     i <= static_cast<int>(pdf::Error::Unknown); ++i) {
			all.emplace_back(pdf::errorMessage(static_cast<pdf::Error>(i)));
		}
		return all;
	};
	const auto messages = engine.run(work).get();

	ASSERT_FALSE(messages.empty());
	EXPECT_TRUE(messages.front().empty());
	std::set<std::string> seen;
	for (std::size_t i = 1; i < messages.size(); ++i) {
		const auto name = ::testing::PrintToString(static_cast<pdf::Error>(i));
		EXPECT_FALSE(messages[i].empty()) << name;
		EXPECT_TRUE(seen.insert(messages[i]).second)
		    << name << " says the same as another error: " << messages[i];
	}
}

TEST_F(PdfDocumentTest, OutlinesPreserveHierarchyUnicodeAndSafeDestinations) {
	const auto snapshot =
	    engine
	        .run([] {
		        std::pair<std::optional<pdf::Outline>,
		                  std::vector<std::optional<pdf::Link>>>
		            result;
		        const auto opened = pdf::openFile(fixturePath("outline.pdf"));
		        if (opened.document) {
			        result.first = opened.document->outline();
			        if (result.first) {
				        for (std::size_t i = 0;
				             i < result.first->entries.size(); ++i) {
					        result.second.push_back(
					            opened.document->outlineLink(i));
				        }
			        }
		        }
		        return result;
	        })
	        .get();
	const auto& data = snapshot.first;
	const auto& links = snapshot.second;
	ASSERT_EQ(links.size(), 8u);
	ASSERT_TRUE(data);
	ASSERT_EQ(data->entries.size(), 8u);
	EXPECT_FALSE(data->limited);
	const auto& nodes = data->entries;
	EXPECT_EQ(nodes[0].title, "Chapters \xCE\xA9\xF0\x9F\x98\x80");
	EXPECT_EQ(nodes[0].parent, -1);
	EXPECT_TRUE(nodes[0].expanded);
	EXPECT_EQ(nodes[1].parent, 0);
	EXPECT_EQ(nodes[2].parent, 0);
	EXPECT_EQ(nodes[3].parent, 2);
	EXPECT_FALSE(nodes[2].expanded);
	ASSERT_TRUE(links[0] && links[0]->destination);
	EXPECT_EQ(links[0]->destination->fit, pdf::LinkFit::Page);
	ASSERT_TRUE(links[1] && links[1]->destination);
	const auto& xyz = *links[1]->destination;
	EXPECT_EQ(xyz.page, 1);
	ASSERT_TRUE(xyz.x && xyz.y && xyz.zoom);
	EXPECT_NEAR(*xyz.x, 190.0 / 240, 0.00001);
	EXPECT_NEAR(*xyz.y, 40.0 / 360, 0.00001);
	EXPECT_DOUBLE_EQ(*xyz.zoom, 1.5);
	ASSERT_TRUE(links[2] && links[2]->destination);
	EXPECT_EQ(links[2]->destination->page, 2);
	EXPECT_EQ(links[2]->destination->fit, pdf::LinkFit::Width);
	ASSERT_TRUE(links[2]->destination->y);
	EXPECT_NEAR(*links[2]->destination->y, 0.25, 0.00001);
	ASSERT_TRUE(links[3]);
	EXPECT_EQ(links[3]->uri, "https://example.com/outline");
	EXPECT_EQ(nodes[5].title, "Untitled");
	for (std::size_t i = 4; i < nodes.size(); ++i) {
		EXPECT_FALSE(links[i]);
	}
}
TEST_F(PdfDocumentTest, OutlineCyclesAndDeepTreesStayBounded) {
	const auto data =
	    engine
	        .run([] {
		        std::vector<std::optional<pdf::Outline>> result;
		        for (const auto name :
		             {"outline_cycle.pdf", "outline_deep.pdf"}) {
			        const auto opened = pdf::openFile(fixturePath(name));
			        result.push_back(opened.document
			                             ? opened.document->outline()
			                             : std::nullopt);
		        }
		        return result;
	        })
	        .get();
	ASSERT_EQ(data.size(), 2u);
	ASSERT_TRUE(data[0] && data[1]);
	EXPECT_GE(data[0]->entries.size(), 8u);
	EXPECT_LE(data[0]->entries.size(), 9u);
	ASSERT_EQ(data[1]->entries.size(), 64u);
	EXPECT_TRUE(data[1]->limited);
	for (std::size_t i = 0; i < data[1]->entries.size(); ++i) {
		EXPECT_EQ(data[1]->entries[i].parent, static_cast<int>(i) - 1);
		EXPECT_EQ(data[1]->entries[i].title, "Level " + std::to_string(i));
	}
}
TEST_F(PdfDocumentTest,
       AttachmentsKeepUnicodeBinaryEmptyAndUnreadableDistinct) {
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("attachments.pdf"));
		    ASSERT_TRUE(opened.document);
		    const auto& doc = *opened.document;
		    const auto list = doc.attachments();
		    ASSERT_TRUE(list);
		    ASSERT_EQ(list->entries.size(), 4u);
		    EXPECT_EQ(list->entries[0].name,
		              "notes \xCE\xA9\xF0\x9F\x98\x80.txt");
		    EXPECT_EQ(list->entries[1].name, "..\\..\\empty.bin");
		    EXPECT_EQ(list->entries[3].name.find('\0'), std::string::npos);
		    const auto bytes = doc.attachment(list->entries[0].index);
		    ASSERT_TRUE(bytes);
		    EXPECT_EQ(*bytes, (std::vector<std::uint8_t>{'h', 'e', 'l', 'l',
		                                                 'o', 0, 255, '\n'}));
		    const auto empty = doc.attachment(1);
		    ASSERT_TRUE(empty);
		    EXPECT_TRUE(empty->empty());
		    EXPECT_FALSE(doc.attachment(2));
		    EXPECT_FALSE(doc.attachment(-1));
		    EXPECT_FALSE(doc.attachment(4));
		    EXPECT_FALSE(doc.attachment(0, [] { return true; }));
		    EXPECT_FALSE(doc.attachments([] { return true; }));
		    const auto plain = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(plain.document);
		    const auto none = plain.document->attachments();
		    ASSERT_TRUE(none);
		    EXPECT_TRUE(none->entries.empty());
	    })
	    .get();
}

TEST_F(PdfDocumentTest, EmptyOutlinesAndCanceledExtractionAreDistinct) {
	const auto data =
	    engine
	        .run([] {
		        const auto empty = pdf::openFile(fixturePath("pages.pdf"));
		        const auto outlined = pdf::openFile(fixturePath("outline.pdf"));
		        return std::pair{
		            empty.document ? empty.document->outline() : std::nullopt,
		            outlined.document
		                ? outlined.document->outline([] { return true; })
		                : std::nullopt};
	        })
	        .get();
	ASSERT_TRUE(data.first);
	EXPECT_TRUE(data.first->entries.empty());
	EXPECT_FALSE(data.first->limited);
	EXPECT_FALSE(data.second);
}
