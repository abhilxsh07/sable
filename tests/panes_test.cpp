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
#include "helpers/panes.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
TEST(Panes, WidthsStayInRange) {
	EXPECT_EQ(helpers::clampPaneWidth(320), 320);
	EXPECT_EQ(helpers::clampPaneWidth(100), 280);
	EXPECT_EQ(helpers::clampPaneWidth(900), 480);
	EXPECT_EQ(helpers::clampPaneWidth(333.6), 334);
	EXPECT_EQ(helpers::clampPaneWidth(-1), 280);
	EXPECT_EQ(helpers::clampPaneWidth(std::numeric_limits<double>::quiet_NaN()),
	          320);
	EXPECT_EQ(helpers::clampPaneWidth(std::numeric_limits<double>::infinity()),
	          320);
}

TEST(Panes, ResizingLeavesThePagesTheirRoom) {
	// plenty of room: the width asked for
	EXPECT_EQ(helpers::fitPaneWidth(480, 900), 480);
	EXPECT_EQ(helpers::fitPaneWidth(600, 900), 480);
	// the pages can spare 400.5 px: the pane stops there
	EXPECT_EQ(helpers::fitPaneWidth(480, 400.5), 400);
	// never under the minimum, however little room there is
	EXPECT_EQ(helpers::fitPaneWidth(480, 100), 280);
	EXPECT_EQ(helpers::fitPaneWidth(300, -50), 280);
	// not laid out yet: nothing to fit against
	EXPECT_EQ(
	    helpers::fitPaneWidth(460, std::numeric_limits<double>::quiet_NaN()),
	    460);
}

TEST(Panes, EachPaneKeepsItsLastWidth) {
	EXPECT_EQ(helpers::loadPaneWidths({}).width("launcher"), 320);
	EXPECT_FALSE(helpers::savePaneWidth({}, "launcher", 400));
	const auto dir =
	    std::filesystem::temp_directory_path() /
	    ("sable-panes-" +
	     std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
	std::filesystem::create_directories(dir);
	const auto file = dir / "panes.json";
	EXPECT_EQ(helpers::loadPaneWidths(file).width("launcher"), 320);
	ASSERT_TRUE(helpers::savePaneWidth(file, "launcher", 400));
	ASSERT_TRUE(helpers::savePaneWidth(file, "properties", 999));
	auto panes = helpers::loadPaneWidths(file);
	EXPECT_EQ(panes.width("launcher"), 400);
	EXPECT_EQ(panes.width("properties"), 480);
	EXPECT_EQ(panes.width("export"), 320);
	// saving one keeps the other
	ASSERT_TRUE(helpers::savePaneWidth(file, "launcher", 290));
	panes = helpers::loadPaneWidths(file);
	EXPECT_EQ(panes.width("launcher"), 290);
	EXPECT_EQ(panes.width("properties"), 480);
	EXPECT_FALSE(helpers::savePaneWidth(file, "", 300));
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << R"({"launcher": "wide", "properties": 100, "export": 360})";
	}
	panes = helpers::loadPaneWidths(file);
	EXPECT_EQ(panes.width("launcher"), 320);
	EXPECT_EQ(panes.width("properties"), 280);
	EXPECT_EQ(panes.width("export"), 360);
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << "not json";
	}
	EXPECT_EQ(helpers::loadPaneWidths(file), helpers::PaneWidths{});
	// a broken file is replaced, not kept
	ASSERT_TRUE(helpers::savePaneWidth(file, "launcher", 300));
	EXPECT_EQ(helpers::loadPaneWidths(file).width("launcher"), 300);
	std::error_code ignored;
	std::filesystem::remove_all(dir, ignored);
}
} // namespace
