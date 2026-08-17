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
#include "helpers/attachment_file.h"
#include "pdf_test_files.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <thread>
namespace {
// .tmp files left in a save folder. ReplaceFileW works through a file
// named after the backup plus ".tmp" (".SABLE-SAVE...TMP.BAK.tmp") and
// that entry can still be listed for a moment after the call returns,
// gone by the time anything opens it. That one isn't mine, so it gets a
// couple of seconds to disappear; my own temporary file fails at once.
std::vector<std::filesystem::path>
leftoverTemporaryFiles(const std::filesystem::path& dir) {
	namespace fs = std::filesystem;
	const auto replaceWork = [](const fs::path& path) {
		auto name = path.filename().wstring();
		std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) {
			return static_cast<wchar_t>(std::towlower(c));
		});
		return name.ends_with(L".bak.tmp");
	};
	const auto deadline =
	    std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (true) {
		std::vector<fs::path> found;
		bool waiting = false;
		for (const auto& entry : fs::directory_iterator(dir)) {
			if (entry.path().extension() == ".tmp" ||
			    entry.path().extension() == ".TMP") {
				found.push_back(entry.path());
				waiting = waiting || replaceWork(entry.path());
			}
		}
		if (!waiting || std::chrono::steady_clock::now() > deadline) {
			return found;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

class FileSaveTest : public ::testing::Test {
  protected:
	const std::filesystem::path dir = makeTempDir("file_save");
	void TearDown() override {
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
	void write(const std::filesystem::path& path, const std::string& text) {
		std::ofstream out(path, std::ios::binary);
		out << text;
	}
	std::vector<std::uint8_t> read(const std::filesystem::path& path) {
		std::ifstream in(path, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), {}};
	}
	void noTemporaryFiles() {
		EXPECT_TRUE(leftoverTemporaryFiles(dir).empty());
	}
};

TEST_F(FileSaveTest, SavesChunksAndEmptyFilesWhileProtectingTheSource) {
	const auto source = dir / "source.pdf", target = dir / L"copy \u03a9.pdf";
	write(source, "source");
	write(target, "previous");
	const std::vector<std::uint8_t> bytes(2 * 1024 * 1024 + 3, 0xA5);
	EXPECT_EQ(helpers::saveFile(target, source, bytes),
	          helpers::FileSaveResult::Saved);
	EXPECT_EQ(read(target), bytes);
	EXPECT_EQ(helpers::saveFile(source, source, bytes),
	          helpers::FileSaveResult::Source);
	EXPECT_EQ(read(source),
	          (std::vector<std::uint8_t>{'s', 'o', 'u', 'r', 'c', 'e'}));
	EXPECT_EQ(helpers::saveFile(target, source, {}),
	          helpers::FileSaveResult::Saved);
	EXPECT_TRUE(read(target).empty());
	noTemporaryFiles();
}

TEST_F(FileSaveTest, CancellationAndFailuresLeaveTheDestinationIntact) {
	const auto source = dir / "source.pdf", target = dir / "copy.pdf";
	write(source, "source");
	write(target, "previous");
	const auto original = read(target);
	const std::vector<std::uint8_t> bytes(2 * 1024 * 1024 + 3, 0xA5);
	for (const int stop : {1, 2, 3, 4, 5}) {
		int checks = 0;
		EXPECT_EQ(helpers::saveFile(target, source, bytes,
		                            [&] { return ++checks == stop; }),
		          helpers::FileSaveResult::Canceled);
		EXPECT_EQ(read(target), original);
		noTemporaryFiles();
	}
	int checks = 0;
	EXPECT_EQ(helpers::saveFile(target, source, bytes,
	                            [&]() -> bool {
		                            if (++checks == 3) {
			                            throw 1;
		                            }
		                            return false;
	                            }),
	          helpers::FileSaveResult::Failed);
	const auto locked =
	    CreateFileW(target.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
	                FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(locked, INVALID_HANDLE_VALUE);
	EXPECT_EQ(helpers::saveFile(target, source, bytes),
	          helpers::FileSaveResult::Failed);
	CloseHandle(locked);
	EXPECT_EQ(read(target), original);
	EXPECT_EQ(helpers::saveFile(dir / "missing" / "copy.pdf", source, bytes),
	          helpers::FileSaveResult::Failed);
	noTemporaryFiles();
}

TEST_F(FileSaveTest, DoesNotQueryCancellationAfterReplacingTheDestination) {
	const auto source = dir / "source.pdf", target = dir / "copy.pdf";
	write(source, "source");
	write(target, "previous");
	const std::vector<std::uint8_t> bytes{1, 2, 3};
	EXPECT_EQ(helpers::saveFile(target, source, bytes,
	                            [&] { return read(target) == bytes; }),
	          helpers::FileSaveResult::Saved);
	EXPECT_EQ(read(target), bytes);
	noTemporaryFiles();
}

TEST_F(FileSaveTest, ReplacingAnExistingFileKeepsItsNamedStream) {
	const auto source = dir / "source.pdf", target = dir / "copy.pdf";
	auto stream = target;
	stream += L":sable-test";
	write(source, "source");
	write(target, "previous");
	write(stream, "metadata");
	const auto metadata = read(stream);
	ASSERT_EQ(metadata.size(), 8u);

	const std::vector<std::uint8_t> bytes{1, 2, 3};
	ASSERT_EQ(helpers::saveFile(target, source, bytes),
	          helpers::FileSaveResult::Saved);
	EXPECT_EQ(read(target), bytes);
	EXPECT_EQ(read(stream), metadata);
	EXPECT_EQ(std::distance(std::filesystem::directory_iterator(dir),
	                        std::filesystem::directory_iterator{}),
	          2);
}

TEST_F(FileSaveTest, OverwritingAnExistingFileKeepsItsAttributesAndBirth) {
	const auto source = dir / "source.pdf", target = dir / L"saved \u03a9.pdf";
	write(source, "source");
	write(target, "an older and longer copy of the file");
	ASSERT_TRUE(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_HIDDEN));
	WIN32_FILE_ATTRIBUTE_DATA before{};
	ASSERT_TRUE(
	    GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &before));
	const std::vector<std::uint8_t> bytes{'%', 'P', 'D', 'F'};
	ASSERT_EQ(helpers::saveFile(target, source, bytes),
	          helpers::FileSaveResult::Saved);
	EXPECT_EQ(read(target), bytes);
	WIN32_FILE_ATTRIBUTE_DATA after{};
	ASSERT_TRUE(
	    GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &after));
	// ReplaceFileW keeps the replaced file's attributes and creation time
	EXPECT_TRUE(after.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
	EXPECT_EQ(CompareFileTime(&before.ftCreationTime, &after.ftCreationTime),
	          0);
	EXPECT_EQ(read(source).size(), 6u);
	noTemporaryFiles();
}

TEST_F(FileSaveTest, ReadOnlyDestinationRefusesAndStaysAsItWas) {
	const auto source = dir / "source.pdf", target = dir / "locked.pdf";
	write(source, "source");
	write(target, "previous");
	const auto original = read(target);
	ASSERT_TRUE(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY));
	EXPECT_EQ(helpers::saveFile(target, source, {1, 2, 3}),
	          helpers::FileSaveResult::Failed);
	EXPECT_EQ(read(target), original);
	EXPECT_TRUE(GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_READONLY);
	noTemporaryFiles();
	EXPECT_EQ(std::distance(std::filesystem::directory_iterator(dir),
	                        std::filesystem::directory_iterator{}),
	          2);
	SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
}

TEST_F(FileSaveTest, HardLinkedDestinationGetsTheNewFileAndItsOtherNamesDont) {
	const auto source = dir / "source.pdf", target = dir / "copy.pdf";
	const auto other = dir / "other name.pdf";
	write(source, "source");
	write(target, "previous");
	std::error_code ec;
	std::filesystem::create_hard_link(target, other, ec);
	if (ec) {
		GTEST_SKIP() << "Hard-link creation is unavailable: " << ec.message();
	}
	const auto original = read(target);
	const std::vector<std::uint8_t> bytes{4, 5, 6};
	ASSERT_EQ(helpers::saveFile(target, source, bytes),
	          helpers::FileSaveResult::Saved);
	// the chosen name gets the new PDF, the way Explorer replaces a file;
	// the other link keeps the old bytes and becomes a file of its own
	EXPECT_EQ(read(target), bytes);
	EXPECT_EQ(read(other), original);
	EXPECT_EQ(std::filesystem::hard_link_count(target, ec), 1u);
	EXPECT_EQ(std::filesystem::hard_link_count(other, ec), 1u);
	EXPECT_EQ(read(source).size(), 6u);
	noTemporaryFiles();
}
TEST_F(FileSaveTest, PrunesAbandonedTempsBesideTheTarget) {
	const auto stale = dir / L".sable-save.sable-4294967292-1-0.tmp";
	const auto source = dir / L".sable-save.sable-4294967292-1-1.tmp";
	const auto target = dir / L".sable-save.sable-4294967292-1-2.tmp";
	const auto active =
	    dir / (L".sable-save.sable-" + std::to_wstring(GetCurrentProcessId()) +
	           L"-1-0.tmp");
	const auto unrelated = dir / L".sable-save.sable-other.tmp";
	const auto folder = dir / L".sable-save.sable-4294967292-1-3.tmp";
	std::filesystem::create_directory(folder);
	const auto nested = folder / stale.filename();
	for (const auto& file :
	     {stale, source, target, active, unrelated, nested}) {
		write(file, "original");
	}
	const auto original = read(source);
	const std::vector<std::uint8_t> bytes{1, 2, 3};
	ASSERT_EQ(helpers::saveFile(target, source, bytes),
	          helpers::FileSaveResult::Saved);
	EXPECT_FALSE(std::filesystem::exists(stale));
	EXPECT_EQ(read(target), bytes);
	for (const auto& file : {source, active, unrelated, nested}) {
		EXPECT_EQ(read(file), original);
	}
	EXPECT_TRUE(std::filesystem::is_directory(folder));
}

TEST_F(FileSaveTest, RechecksSourceAliasesJustBeforeReplacingTheDestination) {
	const auto source = dir / "source.pdf", target = dir / "alias.pdf";
	write(source, "source");
	int checks = 0;
	std::error_code ec;
	const auto result = helpers::saveFile(target, source, {1, 2, 3}, [&] {
		if (++checks == 3) {
			std::filesystem::create_hard_link(source, target, ec);
		}
		return false;
	});
	if (ec) {
		GTEST_SKIP() << "Hard-link creation is unavailable: " << ec.message();
	}
	EXPECT_EQ(result, helpers::FileSaveResult::Source);
	EXPECT_EQ(read(target), read(source));
	EXPECT_EQ(read(source).size(), 6u);
	noTemporaryFiles();
}

TEST(AttachmentFileTest, SuggestedNamesDiscardPathsAndWindowsSpecialNames) {
	EXPECT_EQ(helpers::attachmentFilename("../../hello.txt"), "hello.txt");
	EXPECT_EQ(helpers::attachmentFilename("C:\\dir\\hello.txt"), "hello.txt");
	EXPECT_EQ(helpers::attachmentFilename("CON.txt"), "_CON.txt");
	EXPECT_EQ(helpers::attachmentFilename("lpt1"), "_lpt1");
	EXPECT_EQ(helpers::attachmentFilename("COM\xC2\xB9.txt"),
	          "_COM\xC2\xB9.txt");
	EXPECT_EQ(helpers::attachmentFilename("CONOUT$"), "_CONOUT$");
	EXPECT_EQ(helpers::attachmentFilename(".. "), "Attachment");
	EXPECT_EQ(helpers::attachmentFilename("a:b?*.txt"), "a_b__.txt");
	EXPECT_EQ(helpers::attachmentFilename("notes \xCE\xA9.txt"),
	          "notes \xCE\xA9.txt");
	EXPECT_EQ(helpers::attachmentFilename(std::string(179, 'a') + "\xCE\xA9"),
	          std::string(179, 'a'));
}
TEST(AttachmentFileTest,
     SavesBinaryAndEmptyFilesAndPreservesSourceAndFailedTargets) {
	namespace fs = std::filesystem;
	const auto dir = makeTempDir("attachment");
	struct Cleanup {
		fs::path path;
		~Cleanup() {
			std::error_code ec;
			fs::remove_all(path, ec);
		}
	} cleanup{dir};
	const auto source = dir / "source.pdf";
	const auto target = dir / L"notes \u03a9.bin";
	{
		std::ofstream out(source);
		out << "source";
	}
	const std::vector<std::uint8_t> bytes{0, 255, 10, 13};
	ASSERT_TRUE(helpers::saveAttachment(target, source, bytes));
	std::ifstream in(target, std::ios::binary);
	const std::vector<std::uint8_t> saved((std::istreambuf_iterator<char>(in)),
	                                      {});
	EXPECT_EQ(saved, bytes);
	in.close();
	EXPECT_TRUE(helpers::saveAttachment(target, source, {}));
	EXPECT_EQ(fs::file_size(target), 0u);
	EXPECT_FALSE(helpers::saveAttachment(source, source, bytes));
	EXPECT_FALSE(helpers::saveAttachment(dir / "SOURCE.pdf", source, bytes));
	EXPECT_EQ(fs::file_size(source), 6u);
	EXPECT_FALSE(
	    helpers::saveAttachment(dir / "missing" / "file", source, bytes));
	const auto locked =
	    CreateFileW(target.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
	                FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(locked, INVALID_HANDLE_VALUE);
	EXPECT_FALSE(helpers::saveAttachment(target, source, bytes));
	CloseHandle(locked);
	EXPECT_EQ(fs::file_size(target), 0u);
	fs::remove(source);
	EXPECT_TRUE(helpers::saveAttachment(target, source, bytes));
	EXPECT_EQ(fs::file_size(target), bytes.size());
	EXPECT_FALSE(helpers::saveAttachment(source, source, bytes));
	EXPECT_TRUE(leftoverTemporaryFiles(dir).empty());
}
TEST(AttachmentFileTest, RejectsHardLinkAliasesOfTheSource) {
	namespace fs = std::filesystem;
	const auto dir = makeTempDir("attachment_alias");
	struct Cleanup {
		fs::path path;
		~Cleanup() {
			std::error_code ec;
			fs::remove_all(path, ec);
		}
	} cleanup{dir};
	const auto source = dir / "source.pdf";
	const auto alias = dir / "alias.pdf";
	{
		std::ofstream out(source);
		out << "source";
	}
	std::error_code ec;
	fs::create_hard_link(source, alias, ec);
	if (ec) {
		GTEST_SKIP() << "Hard-link creation is unavailable: " << ec.message();
	}
	EXPECT_FALSE(helpers::saveAttachment(alias, source, {1, 2, 3}));
	EXPECT_EQ(fs::file_size(source), 6u);
}
} // namespace
