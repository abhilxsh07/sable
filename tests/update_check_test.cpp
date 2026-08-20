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
#include "helpers/update_check.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace {
std::strong_ordering order(const char* a, const char* b) {
	return helpers::compareReleaseVersions(*helpers::parseReleaseVersion(a),
	                                       *helpers::parseReleaseVersion(b));
}

TEST(UpdateCheck, TagsParseLikeSemver) {
	const auto v = helpers::parseReleaseVersion("v1.0.0-alpha.1+build.5");
	ASSERT_TRUE(v);
	EXPECT_EQ(v->major, 1u);
	EXPECT_EQ(v->minor, 0u);
	EXPECT_EQ(v->patch, 0u);
	EXPECT_EQ(v->pre, (std::vector<std::string>{"alpha", "1"}));
	EXPECT_TRUE(helpers::parseReleaseVersion("2.10.3"));
	for (const char* bad :
	     {"", "v", "1.2", "1.2.3.4", "01.2.3", "1.2.3-", "1.2.3-alpha..1",
	      "1.2.3-alpha.", "1.x.3", "1.2.3-al pha", "dev", "-1.2.3"}) {
		EXPECT_FALSE(helpers::parseReleaseVersion(bad)) << bad;
	}
}

TEST(UpdateCheck, PrereleasesSortBeforeTheirRelease) {
	EXPECT_EQ(order("1.0.0-alpha.1", "1.0.0"), std::strong_ordering::less);
	EXPECT_EQ(order("1.0.0-alpha.1", "1.0.0-alpha.2"),
	          std::strong_ordering::less);
	EXPECT_EQ(order("1.0.0-alpha.2", "1.0.0-alpha.10"),
	          std::strong_ordering::less);
	EXPECT_EQ(order("1.0.0-alpha.1", "1.0.0-beta.1"),
	          std::strong_ordering::less);
	EXPECT_EQ(order("1.0.0-alpha", "1.0.0-alpha.1"),
	          std::strong_ordering::less);
	EXPECT_EQ(order("1.0.0-1", "1.0.0-alpha"), std::strong_ordering::less);
	EXPECT_EQ(order("v1.2.0", "1.10.0"), std::strong_ordering::less);
	EXPECT_EQ(order("v1.0.0-alpha.1", "1.0.0-alpha.1+x"),
	          std::strong_ordering::equal);
}

constexpr const char* releases = R"([
	{"tag_name": "v1.0.0-alpha.3", "draft": true, "prerelease": true,
	 "html_url": "https://github.com/abhilxsh07/sable/releases/tag/v1.0.0-alpha.3"},
	{"tag_name": "v1.0.0-alpha.2", "draft": false, "prerelease": true,
	 "html_url": "https://github.com/abhilxsh07/sable/releases/tag/v1.0.0-alpha.2"},
	{"tag_name": "v9.0.0", "draft": false, "prerelease": false,
	 "html_url": "https://example.com/abhilxsh07/sable/releases/tag/v9.0.0"},
	{"tag_name": "v1.0.0-alpha.1", "draft": false, "prerelease": true,
	 "html_url": "https://github.com/abhilxsh07/sable/releases/tag/v1.0.0-alpha.1"},
	{"tag_name": "not-a-version", "draft": false, "prerelease": false,
	 "html_url": "https://github.com/abhilxsh07/sable/releases/tag/x"},
	"junk"
])";

TEST(UpdateCheck, NewestPublishedReleaseFromThisRepositoryWins) {
	const auto found = helpers::newerRelease(releases, "v1.0.0-alpha.1");
	ASSERT_TRUE(found);
	EXPECT_EQ(found->tag, "v1.0.0-alpha.2");
	EXPECT_EQ(
	    found->page,
	    "https://github.com/abhilxsh07/sable/releases/tag/v1.0.0-alpha.2");
	EXPECT_FALSE(helpers::newerRelease(releases, "v1.0.0-alpha.2"));
	// a stable install isn't offered prereleases
	EXPECT_FALSE(helpers::newerRelease(releases, "v0.9.0"));
	EXPECT_FALSE(helpers::newerRelease(releases, "dev"));
}

TEST(UpdateCheck, StableReleasesAreOfferedToPrereleases) {
	constexpr const char* list = R"([
		{"tag_name": "v1.0.0", "draft": false, "prerelease": false,
		 "html_url": "https://github.com/abhilxsh07/sable/releases/tag/v1.0.0"}
	])";
	const auto found = helpers::newerRelease(list, "v1.0.0-alpha.1");
	ASSERT_TRUE(found);
	EXPECT_EQ(found->tag, "v1.0.0");
	EXPECT_FALSE(helpers::newerRelease(list, "v1.0.0"));
}

TEST(UpdateCheck, BadResponsesFindNothing) {
	for (
	    const char* body :
	    {"", "{}", "null", "[", R"({"message": "Not Found"})", "[1, 2]",
	     R"([{"tag_name": "v2.0.0", "html_url": "https://github.com/abhilxsh07/sable/releases/tag/v2.0.0\"x"}])",
	     R"([{"tag_name": "v2.0.0", "html_url": "https://github.com/abhilxsh07/sable/releases/../../evil"}])",
	     R"([{"tag_name": "v2.0.0", "html_url": "https://github.com/abhilxsh07/sable/releases/tag/v2.0.1"}])"}) {
		EXPECT_FALSE(helpers::newerRelease(body, "v1.0.0-alpha.1")) << body;
	}
}

TEST(UpdateCheck, ChoiceDefaultsOnAndRoundTrips) {
	EXPECT_TRUE(helpers::loadUpdateCheck({}).enabled);
	EXPECT_FALSE(helpers::saveUpdateCheck({}, {false}));
	const auto dir =
	    std::filesystem::temp_directory_path() /
	    ("sable-update-check-" +
	     std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
	std::filesystem::create_directories(dir);
	const auto file = dir / "update-check.json";
	EXPECT_TRUE(helpers::loadUpdateCheck(file).enabled);
	ASSERT_TRUE(helpers::saveUpdateCheck(file, {false}));
	EXPECT_FALSE(helpers::loadUpdateCheck(file).enabled);
	ASSERT_TRUE(helpers::saveUpdateCheck(file, {true}));
	EXPECT_TRUE(helpers::loadUpdateCheck(file).enabled);
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << R"({"enabled": "no"})";
	}
	EXPECT_TRUE(helpers::loadUpdateCheck(file).enabled);
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << "not json";
	}
	EXPECT_TRUE(helpers::loadUpdateCheck(file).enabled);
	std::error_code ignored;
	std::filesystem::remove_all(dir, ignored);
}
} // namespace
