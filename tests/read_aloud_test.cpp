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
#include "helpers/read_aloud.h"
#include "pdf_test_files.h"
#include <gtest/gtest.h>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
TEST(ReadAloud, TextReadsAsOneRunWithHyphenatedWordsJoined) {
	EXPECT_EQ(helpers::speakableText("One line\nand the next.\n\nA new "
	                                 "para-\ngraph  with   gaps\f"),
	          "One line and the next. A new paragraph with gaps");
	EXPECT_EQ(helpers::speakableText(" \n\r\n\t "), "");
	// a dash on its own before the break isn't a hyphenated word
	EXPECT_EQ(helpers::speakableText("left -\nright"), "left - right");
	EXPECT_EQ(helpers::speakableText("\u0939\u093f\u0928\u094d\u0926\u0940"),
	          "\u0939\u093f\u0928\u094d\u0926\u0940");
}

TEST(ReadAloud, SpeedsSnapToTheOfferedOnes) {
	EXPECT_EQ(helpers::nearestReadAloudRate(1), 1);
	EXPECT_EQ(helpers::nearestReadAloudRate(1.3), 1.25);
	EXPECT_EQ(helpers::nearestReadAloudRate(9), 2);
	EXPECT_EQ(helpers::nearestReadAloudRate(0), .5);
	EXPECT_EQ(helpers::nearestReadAloudRate(std::nan("")), 1);
	EXPECT_EQ(helpers::readAloudRateLabel(1.25), L"1.25x");
	EXPECT_EQ(helpers::readAloudRateLabel(1), L"1x");
}

TEST(ReadAloud, ChoiceRoundTripsAndBadFilesGiveDefaults) {
	namespace fs = std::filesystem;
	const auto folder =
	    fs::temp_directory_path() /
	    ("sable-read-aloud-" + std::to_string(GetCurrentProcessId()));
	fs::create_directories(folder);
	const auto file = folder / "read-aloud.json";
	EXPECT_EQ(helpers::loadReadAloud({}), helpers::ReadAloudChoice{});
	EXPECT_EQ(helpers::loadReadAloud(file), helpers::ReadAloudChoice{});
	EXPECT_FALSE(helpers::saveReadAloud({}, {}));
	const helpers::ReadAloudChoice choice{"HKEY_LOCAL_MACHINE\\Voice", 1.5};
	ASSERT_TRUE(helpers::saveReadAloud(file, choice));
	EXPECT_EQ(helpers::loadReadAloud(file), choice);
	for (const char* text :
	     {"not json", "[1, 2]", "{\"voice\": 3, \"rate\": \"fast\"}"}) {
		std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
		EXPECT_EQ(helpers::loadReadAloud(file), helpers::ReadAloudChoice{})
		    << text;
	}
	std::ofstream(file, std::ios::binary | std::ios::trunc)
	    << "{\"voice\": \"v\", \"rate\": 1.3}";
	EXPECT_EQ(helpers::loadReadAloud(file),
	          (helpers::ReadAloudChoice{"v", 1.25}));
	std::error_code ec;
	fs::remove_all(folder, ec);
}
} // namespace
