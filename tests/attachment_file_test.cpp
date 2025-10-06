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
#include <fstream>
#include <iterator>
namespace {
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
	for (const auto& entry : fs::directory_iterator(dir)) {
		EXPECT_NE(entry.path().extension(), ".tmp");
	}
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
