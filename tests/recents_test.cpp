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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "recents.h"

using helpers::RecentFile;
using helpers::RecentFiles;
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;
namespace chr = std::chrono;

namespace {
// each test gets its own folder under the system temp dir
class RecentsFiles : public ::testing::Test {
  protected:
	void SetUp() override {
		static std::atomic<int> counter{0};
		const auto stamp =
		    std::chrono::steady_clock::now().time_since_epoch().count();
		m_dir = fs::temp_directory_path() /
		        ("sable_recents_test_" + std::to_string(stamp) + "_" +
		         std::to_string(counter++));
		fs::create_directories(m_dir);
	}

	void TearDown() override {
		std::error_code ec;
		fs::remove_all(m_dir, ec);
	}

	fs::path dir() const {
		return m_dir;
	}

	fs::path file(const std::string& name = "recent.json") const {
		return m_dir / name;
	}

	void write(const fs::path& path, const std::string& text) const {
		std::ofstream out(path, std::ios::binary);
		out << text;
	}

	std::string read(const fs::path& path) const {
		std::ifstream in(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << in.rdbuf();
		return buffer.str();
	}

  private:
	fs::path m_dir;
};

chr::sys_seconds secs(std::int64_t value) {
	return chr::sys_seconds{chr::seconds{value}};
}

RecentFile make(const std::string& path, std::int64_t opened = 0,
                bool starred = false) {
	RecentFile file;
	file.path = fs::path(path);
	file.openedAt = secs(opened);
	file.starred = starred;
	return file;
}

// "dir/f<index>.pdf"
std::string numbered(int index) {
	return "dir/f" + std::to_string(index) + ".pdf";
}

// a local-looking time written as UTC
chr::sys_seconds at(int year, unsigned month, unsigned day, int hour,
                    int minute, int second = 0) {
	return chr::sys_days{chr::year{year} / chr::month{month} / chr::day{day}} +
	       chr::hours{hour} + chr::minutes{minute} + chr::seconds{second};
}

const chr::minutes kUtc{0};
const chr::minutes kIst{5 * 60 + 30};

std::string label(chr::sys_seconds opened, chr::sys_seconds now,
                  chr::minutes offset = kUtc) {
	return helpers::openedLabel(opened, now, offset);
}

std::string entryJson(const std::string& path, std::int64_t opened,
                      bool starred = false) {
	return R"({"path": ")" + path + R"(", "opened": )" +
	       std::to_string(opened) + R"(, "starred": )" +
	       (starred ? "true" : "false") + "}";
}

std::string listJson(const std::string& entries) {
	return R"({"version": 1, "files": [)" + entries + "]}";
}
} // namespace

// adding

TEST(Recents, StartsEmpty) {
	const RecentFiles recents;
	EXPECT_TRUE(recents.empty());
	EXPECT_TRUE(recents.items().empty());
	EXPECT_TRUE(recents.starred().empty());
}

TEST(Recents, LimitIsFifty) {
	EXPECT_EQ(helpers::kMaxRecentFiles, 50u);
}

TEST(RecentsAdd, NewestComesFirst) {
	RecentFiles recents;
	recents.add(make("a/one.pdf", 1));
	recents.add(make("a/two.pdf", 2));
	recents.add(make("a/three.pdf", 3));
	ASSERT_EQ(recents.items().size(), 3u);
	EXPECT_EQ(recents.items()[0].path, fs::path("a/three.pdf"));
	EXPECT_EQ(recents.items()[1].path, fs::path("a/two.pdf"));
	EXPECT_EQ(recents.items()[2].path, fs::path("a/one.pdf"));
	EXPECT_FALSE(recents.empty());
}

TEST(RecentsAdd, EmptyNameBecomesFileName) {
	RecentFiles recents;
	recents.add(make("some/folder/Report.pdf"));
	EXPECT_EQ(recents.items()[0].name, "Report.pdf");
}

TEST(RecentsAdd, GivenNameIsKept) {
	RecentFiles recents;
	RecentFile file = make("some/folder/Report.pdf");
	file.name = "Q3 report";
	recents.add(file);
	EXPECT_EQ(recents.items()[0].name, "Q3 report");
}

TEST(RecentsAdd, NonAsciiNameIsUtf8) {
	RecentFiles recents;
	RecentFile file;
	file.path = fs::path(std::u8string(u8"docs/r\u00e9sum\u00e9.pdf"));
	recents.add(file);
	EXPECT_EQ(recents.items()[0].name, "r\xC3\xA9sum\xC3\xA9.pdf");
}

TEST(RecentsAdd, KeepsAllFields) {
	RecentFiles recents;
	RecentFile file = make("a/b.pdf", 1700000000, true);
	file.sizeBytes = 4096;
	recents.add(file);
	const RecentFile& stored = recents.items()[0];
	EXPECT_EQ(stored.path, fs::path("a/b.pdf"));
	EXPECT_EQ(stored.openedAt, secs(1700000000));
	EXPECT_EQ(stored.sizeBytes, 4096u);
	EXPECT_TRUE(stored.starred);
}

TEST(RecentsAdd, SamePathMovesToTop) {
	RecentFiles recents;
	recents.add(make("a/one.pdf", 1));
	recents.add(make("a/two.pdf", 2));
	recents.add(make("a/one.pdf", 3));
	ASSERT_EQ(recents.items().size(), 2u);
	EXPECT_EQ(recents.items()[0].path, fs::path("a/one.pdf"));
	EXPECT_EQ(recents.items()[1].path, fs::path("a/two.pdf"));
}

TEST(RecentsAdd, SamePathUpdatesTheEntry) {
	RecentFiles recents;
	RecentFile first = make("a/one.pdf", 1);
	first.sizeBytes = 10;
	recents.add(first);
	RecentFile second = make("a/one.pdf", 99);
	second.sizeBytes = 20;
	second.name = "renamed";
	recents.add(second);
	ASSERT_EQ(recents.items().size(), 1u);
	EXPECT_EQ(recents.items()[0].openedAt, secs(99));
	EXPECT_EQ(recents.items()[0].sizeBytes, 20u);
	EXPECT_EQ(recents.items()[0].name, "renamed");
}

TEST(RecentsAdd, DedupeIgnoresCase) {
	RecentFiles recents;
	recents.add(make("Docs/Alpha.pdf", 1));
	recents.add(make("other.pdf", 2));
	recents.add(make("docs/ALPHA.PDF", 3));
	ASSERT_EQ(recents.items().size(), 2u);
	EXPECT_EQ(recents.items()[0].path, fs::path("docs/ALPHA.PDF"));
	EXPECT_EQ(recents.items()[0].name, "ALPHA.PDF");
}

TEST(RecentsAdd, DedupeKeepsTheStar) {
	RecentFiles recents;
	recents.add(make("Docs/Alpha.pdf", 1, true));
	recents.add(make("docs/alpha.pdf", 2, false));
	ASSERT_EQ(recents.items().size(), 1u);
	EXPECT_TRUE(recents.items()[0].starred);
}

TEST(RecentsAdd, DedupeNormalizesThePath) {
	RecentFiles recents;
	recents.add(make("docs/./Alpha.pdf"));
	recents.add(make("docs/sub/../alpha.pdf"));
	EXPECT_EQ(recents.items().size(), 1u);
}

TEST(RecentsAdd, SameNameInOtherFolderIsAnotherFile) {
	RecentFiles recents;
	recents.add(make("one/a.pdf"));
	recents.add(make("two/a.pdf"));
	EXPECT_EQ(recents.items().size(), 2u);
}

// the cap

TEST(RecentsCap, StopsAtFifty) {
	RecentFiles recents;
	for (int i = 0; i < 80; ++i) {
		recents.add(make(numbered(i), i));
	}
	ASSERT_EQ(recents.items().size(), helpers::kMaxRecentFiles);
	EXPECT_EQ(recents.items().front().path, fs::path(numbered(79)));
	EXPECT_EQ(recents.items().back().path, fs::path(numbered(30)));
}

TEST(RecentsCap, DropsOldestUnstarredFirst) {
	RecentFiles recents;
	for (int i = 0; i < 50; ++i) {
		recents.add(make(numbered(i), i));
	}
	// the very oldest and the one after it
	EXPECT_TRUE(recents.toggleStar(fs::path(numbered(0))));
	recents.add(make("dir/new.pdf", 100));
	ASSERT_EQ(recents.items().size(), 50u);
	EXPECT_EQ(recents.items().front().path, fs::path("dir/new.pdf"));
	EXPECT_EQ(recents.items().back().path, fs::path(numbered(0)));
	// f1 is the one that went
	for (const RecentFile& item : recents.items()) {
		EXPECT_NE(item.path, fs::path(numbered(1)));
	}
}

TEST(RecentsCap, StarredSurvive) {
	RecentFiles recents;
	for (int i = 0; i < 10; ++i) {
		recents.add(make(numbered(i), i, true));
	}
	for (int i = 10; i < 100; ++i) {
		recents.add(make(numbered(i), i));
	}
	ASSERT_EQ(recents.items().size(), 50u);
	EXPECT_EQ(recents.starred().size(), 10u);
	// 10 starred and the 40 newest of the rest
	EXPECT_EQ(recents.items().front().path, fs::path(numbered(99)));
	EXPECT_EQ(recents.items()[39].path, fs::path(numbered(60)));
}

TEST(RecentsCap, AllStarredDropsTheOldestStarred) {
	RecentFiles recents;
	for (int i = 0; i < 50; ++i) {
		recents.add(make(numbered(i), i, true));
	}
	recents.add(make("dir/new.pdf", 100));
	ASSERT_EQ(recents.items().size(), 50u);
	EXPECT_EQ(recents.items().front().path, fs::path("dir/new.pdf"));
	EXPECT_EQ(recents.items().back().path, fs::path(numbered(1)));
}

TEST(RecentsCap, ReAddingAtTheCapDropsNothing) {
	RecentFiles recents;
	for (int i = 0; i < 50; ++i) {
		recents.add(make(numbered(i), i));
	}
	recents.add(make(numbered(0), 200));
	ASSERT_EQ(recents.items().size(), 50u);
	EXPECT_EQ(recents.items().front().path, fs::path(numbered(0)));
	EXPECT_EQ(recents.items().back().path, fs::path(numbered(1)));
}

// remove and star

TEST(RecentsEdit, RemoveTakesTheEntryOut) {
	RecentFiles recents;
	recents.add(make("a/one.pdf"));
	recents.add(make("a/two.pdf"));
	EXPECT_TRUE(recents.remove(fs::path("a/one.pdf")));
	ASSERT_EQ(recents.items().size(), 1u);
	EXPECT_EQ(recents.items()[0].path, fs::path("a/two.pdf"));
}

TEST(RecentsEdit, RemoveMissingIsFalse) {
	RecentFiles recents;
	recents.add(make("a/one.pdf"));
	EXPECT_FALSE(recents.remove(fs::path("a/nope.pdf")));
	EXPECT_EQ(recents.items().size(), 1u);
}

TEST(RecentsEdit, RemoveIgnoresCase) {
	RecentFiles recents;
	recents.add(make("A/One.pdf"));
	EXPECT_TRUE(recents.remove(fs::path("a/ONE.pdf")));
	EXPECT_TRUE(recents.empty());
}

TEST(RecentsEdit, ToggleStarFlipsAndReturnsTheState) {
	RecentFiles recents;
	recents.add(make("a/one.pdf"));
	EXPECT_TRUE(recents.toggleStar(fs::path("A/ONE.pdf")));
	EXPECT_TRUE(recents.items()[0].starred);
	EXPECT_FALSE(recents.toggleStar(fs::path("a/one.pdf")));
	EXPECT_FALSE(recents.items()[0].starred);
}

TEST(RecentsEdit, ToggleStarMissingIsFalse) {
	RecentFiles recents;
	recents.add(make("a/one.pdf"));
	EXPECT_FALSE(recents.toggleStar(fs::path("a/nope.pdf")));
	EXPECT_FALSE(recents.items()[0].starred);
}

TEST(RecentsEdit, ToggleStarKeepsTheOrder) {
	RecentFiles recents;
	recents.add(make("a/one.pdf"));
	recents.add(make("a/two.pdf"));
	recents.toggleStar(fs::path("a/one.pdf"));
	EXPECT_EQ(recents.items()[0].path, fs::path("a/two.pdf"));
}

TEST(RecentsEdit, StarredListsOnlyStarredNewestFirst) {
	RecentFiles recents;
	recents.add(make("a/one.pdf", 1, true));
	recents.add(make("a/two.pdf", 2, false));
	recents.add(make("a/three.pdf", 3, true));
	const std::vector<RecentFile> starred = recents.starred();
	ASSERT_EQ(starred.size(), 2u);
	EXPECT_EQ(starred[0].path, fs::path("a/three.pdf"));
	EXPECT_EQ(starred[1].path, fs::path("a/one.pdf"));
}

TEST(RecentsEdit, ClearEmptiesTheList) {
	RecentFiles recents;
	recents.add(make("a/one.pdf"));
	recents.clear();
	EXPECT_TRUE(recents.empty());
}

// saving and loading

TEST_F(RecentsFiles, RoundTrip) {
	RecentFiles recents;
	RecentFile one = make("docs/One.pdf", 1700000000, true);
	one.sizeBytes = 123456;
	one.name = "First";
	recents.add(one);
	RecentFile two;
	two.path = fs::path(std::u8string(u8"docs/r\u00e9sum\u00e9.pdf"));
	two.openedAt = secs(1700000500);
	recents.add(two);

	ASSERT_TRUE(helpers::saveRecents(file(), recents));
	const RecentFiles back = helpers::loadRecents(file());
	ASSERT_EQ(back.items().size(), 2u);
	EXPECT_EQ(back.items(), recents.items());
	EXPECT_EQ(back.items()[0].name, "r\xC3\xA9sum\xC3\xA9.pdf");
}

TEST_F(RecentsFiles, SavedFileHasTheDocumentedShape) {
	RecentFiles recents;
	RecentFile one = make("docs/One.pdf", 1700000000, true);
	one.sizeBytes = 99;
	recents.add(one);
	ASSERT_TRUE(helpers::saveRecents(file(), recents));

	const std::string text = read(file());
	// tabs, like the settings file
	EXPECT_NE(text.find("\n\t\"version\": 1"), std::string::npos);

	const Json json = Json::parse(text);
	EXPECT_EQ(json.at("version"), 1);
	ASSERT_EQ(json.at("files").size(), 1u);
	const Json& entry = json.at("files").at(0);
	EXPECT_EQ(entry.at("path"), "docs/One.pdf");
	EXPECT_EQ(entry.at("name"), "One.pdf");
	EXPECT_EQ(entry.at("opened"), 1700000000);
	EXPECT_EQ(entry.at("size"), 99);
	EXPECT_EQ(entry.at("starred"), true);
}

TEST_F(RecentsFiles, SaveMakesTheFolderAndLeavesNoTempFile) {
	const fs::path nested = dir() / "deeper" / "still" / "recent.json";
	ASSERT_TRUE(helpers::saveRecents(nested, RecentFiles()));
	EXPECT_TRUE(fs::exists(nested));
	fs::path tmp = nested;
	tmp += ".tmp";
	EXPECT_FALSE(fs::exists(tmp));
}

TEST_F(RecentsFiles, SaveReplacesAnOlderFile) {
	write(file(), "old stuff");
	RecentFiles recents;
	recents.add(make("a/one.pdf", 5));
	ASSERT_TRUE(helpers::saveRecents(file(), recents));
	EXPECT_EQ(helpers::loadRecents(file()).items().size(), 1u);
}

TEST_F(RecentsFiles, SaveFailsWhenThePathIsAFolder) {
	EXPECT_FALSE(helpers::saveRecents(dir(), RecentFiles()));
	fs::path tmp = dir();
	tmp += ".tmp";
	EXPECT_FALSE(fs::exists(tmp));
}

TEST_F(RecentsFiles, MissingFileIsEmpty) {
	EXPECT_TRUE(helpers::loadRecents(file("nope.json")).empty());
}

TEST_F(RecentsFiles, EmptyFileIsEmpty) {
	write(file(), "");
	EXPECT_TRUE(helpers::loadRecents(file()).empty());
}

TEST_F(RecentsFiles, CorruptJsonIsEmpty) {
	write(file(), R"({"version": 1, "files": [{"path": "a.pd)");
	EXPECT_TRUE(helpers::loadRecents(file()).empty());
	write(file(), "not json at all");
	EXPECT_TRUE(helpers::loadRecents(file()).empty());
}

TEST_F(RecentsFiles, WrongTopLevelTypesAreEmpty) {
	write(file(), "[]");
	EXPECT_TRUE(helpers::loadRecents(file()).empty());
	write(file(), R"({"version": 1})");
	EXPECT_TRUE(helpers::loadRecents(file()).empty());
	write(file(), R"({"version": 1, "files": {"path": "a.pdf"}})");
	EXPECT_TRUE(helpers::loadRecents(file()).empty());
	write(file(), R"({"version": 1, "files": "a.pdf"})");
	EXPECT_TRUE(helpers::loadRecents(file()).empty());
}

TEST_F(RecentsFiles, FolderInPlaceOfFileIsEmpty) {
	EXPECT_TRUE(helpers::loadRecents(dir()).empty());
}

TEST_F(RecentsFiles, BadEntriesAreSkipped) {
	const std::string text =
	    listJson(R"(42, "a.pdf", null, [], )"
	             R"({"name": "no path"}, )"
	             R"({"path": 7}, )"
	             R"({"path": ""}, )"
	             R"({"path": "b.pdf", "opened": "yesterday"}, )"
	             R"({"path": "c.pdf", "opened": 1.5}, )"
	             R"({"path": "d.pdf", "opened": -5}, )"
	             R"({"path": "e.pdf", "size": -1}, )"
	             R"({"path": "f.pdf", "size": "big"}, )"
	             R"({"path": "g.pdf", "starred": "yes"}, )"
	             R"({"path": "h.pdf", "name": 12}, )"
	             R"({"path": "good.pdf", "opened": 10})");
	write(file(), text);
	const RecentFiles recents = helpers::loadRecents(file());
	ASSERT_EQ(recents.items().size(), 1u);
	EXPECT_EQ(recents.items()[0].path, fs::path("good.pdf"));
	EXPECT_EQ(recents.items()[0].openedAt, secs(10));
}

TEST_F(RecentsFiles, OpenedOutOfRangeIsSkipped) {
	const std::string huge =
	    R"({"path": "c.pdf", "opened": 18446744073709551615})";
	write(file(), listJson(entryJson("a.pdf", 253402300800) + ", " +
	                       entryJson("b.pdf", 253402300799) + ", " + huge));
	const RecentFiles recents = helpers::loadRecents(file());
	ASSERT_EQ(recents.items().size(), 1u);
	EXPECT_EQ(recents.items()[0].path, fs::path("b.pdf"));
}

TEST_F(RecentsFiles, MissingOptionalFieldsGetDefaults) {
	write(file(), listJson(R"({"path": "docs/Only.pdf"})"));
	const RecentFiles recents = helpers::loadRecents(file());
	ASSERT_EQ(recents.items().size(), 1u);
	const RecentFile& item = recents.items()[0];
	EXPECT_EQ(item.name, "Only.pdf");
	EXPECT_EQ(item.openedAt, secs(0));
	EXPECT_EQ(item.sizeBytes, 0u);
	EXPECT_FALSE(item.starred);
}

TEST_F(RecentsFiles, EmptyNameGetsTheFileName) {
	write(file(), listJson(R"({"path": "docs/Only.pdf", "name": ""})"));
	EXPECT_EQ(helpers::loadRecents(file()).items()[0].name, "Only.pdf");
}

TEST_F(RecentsFiles, UnknownKeysAndVersionAreIgnored) {
	write(file(), R"({"version": 7, "extra": true, "files": [)"
	              R"({"path": "a.pdf", "colour": "red"}]})");
	EXPECT_EQ(helpers::loadRecents(file()).items().size(), 1u);
}

TEST_F(RecentsFiles, FileOrderIsKept) {
	write(file(),
	      listJson(entryJson("a.pdf", 3) + ", " + entryJson("b.pdf", 2) + ", " +
	               entryJson("c.pdf", 1)));
	const RecentFiles recents = helpers::loadRecents(file());
	ASSERT_EQ(recents.items().size(), 3u);
	EXPECT_EQ(recents.items()[0].path, fs::path("a.pdf"));
	EXPECT_EQ(recents.items()[2].path, fs::path("c.pdf"));
}

TEST_F(RecentsFiles, LoadKeepsTheFirstFifty) {
	std::string entries;
	for (int i = 0; i < 70; ++i) {
		entries += (i == 0 ? "" : ", ") + entryJson(numbered(i), 1000 - i);
	}
	write(file(), listJson(entries));
	const RecentFiles recents = helpers::loadRecents(file());
	ASSERT_EQ(recents.items().size(), 50u);
	EXPECT_EQ(recents.items().front().path, fs::path(numbered(0)));
	EXPECT_EQ(recents.items().back().path, fs::path(numbered(49)));
}

TEST_F(RecentsFiles, LoadCapKeepsStarredEntries) {
	std::string entries;
	for (int i = 0; i < 70; ++i) {
		entries +=
		    (i == 0 ? "" : ", ") + entryJson(numbered(i), 1000 - i, i == 65);
	}
	write(file(), listJson(entries));
	const RecentFiles recents = helpers::loadRecents(file());
	ASSERT_EQ(recents.items().size(), 50u);
	EXPECT_EQ(recents.items().back().path, fs::path(numbered(65)));
	EXPECT_TRUE(recents.items().back().starred);
	EXPECT_EQ(recents.items().front().path, fs::path(numbered(0)));
}

TEST_F(RecentsFiles, LoadMergesDuplicates) {
	write(file(), listJson(entryJson("Docs/A.pdf", 30) + ", " +
	                       entryJson("other.pdf", 20) + ", " +
	                       entryJson("docs/a.PDF", 10, true) + ", " +
	                       entryJson("docs/./a.pdf", 5)));
	const RecentFiles recents = helpers::loadRecents(file());
	ASSERT_EQ(recents.items().size(), 2u);
	EXPECT_EQ(recents.items()[0].path, fs::path("Docs/A.pdf"));
	EXPECT_EQ(recents.items()[0].openedAt, secs(30));
	EXPECT_TRUE(recents.items()[0].starred);
	EXPECT_EQ(recents.items()[1].path, fs::path("other.pdf"));
}

// humanSize

TEST(RecentsHumanSize, Bytes) {
	EXPECT_EQ(helpers::humanSize(0), "0 B");
	EXPECT_EQ(helpers::humanSize(1), "1 B");
	EXPECT_EQ(helpers::humanSize(512), "512 B");
	EXPECT_EQ(helpers::humanSize(1023), "1023 B");
}

TEST(RecentsHumanSize, KilobytesAreWhole) {
	EXPECT_EQ(helpers::humanSize(1024), "1 KB");
	EXPECT_EQ(helpers::humanSize(1535), "1 KB");
	EXPECT_EQ(helpers::humanSize(1536), "2 KB");
	EXPECT_EQ(helpers::humanSize(454 * 1024), "454 KB");
	EXPECT_EQ(helpers::humanSize(1023 * 1024), "1023 KB");
}

TEST(RecentsHumanSize, KilobytesRollOverToMegabytes) {
	EXPECT_EQ(helpers::humanSize(1048063), "1023 KB");
	EXPECT_EQ(helpers::humanSize(1048064), "1.0 MB");
	EXPECT_EQ(helpers::humanSize(1048575), "1.0 MB");
	EXPECT_EQ(helpers::humanSize(1048576), "1.0 MB");
}

TEST(RecentsHumanSize, Megabytes) {
	EXPECT_EQ(helpers::humanSize(1572864), "1.5 MB");
	EXPECT_EQ(helpers::humanSize(4u * 1048576u), "4.0 MB");
	EXPECT_EQ(helpers::humanSize(1023u * 1048576u), "1023.0 MB");
}

TEST(RecentsHumanSize, MegabytesRollOverToGigabytes) {
	EXPECT_EQ(helpers::humanSize(1073741823), "1.0 GB");
	EXPECT_EQ(helpers::humanSize(1073741824), "1.0 GB");
}

TEST(RecentsHumanSize, Gigabytes) {
	EXPECT_EQ(helpers::humanSize(1288490189), "1.2 GB");
	EXPECT_EQ(helpers::humanSize(10ull * 1073741824ull), "10.0 GB");
}

TEST(RecentsHumanSize, Terabytes) {
	constexpr std::uint64_t tb = 1ull << 40;
	EXPECT_EQ(helpers::humanSize(tb - 1), "1.0 TB");
	EXPECT_EQ(helpers::humanSize(tb), "1.0 TB");
	EXPECT_EQ(helpers::humanSize(tb + tb / 2), "1.5 TB");
}

TEST(RecentsHumanSize, HugeValuesStayInTerabytes) {
	EXPECT_EQ(helpers::humanSize(std::numeric_limits<std::uint64_t>::max()),
	          "16777216.0 TB");
}

// openedLabel

TEST(RecentsOpenedLabel, JustNowUnderAMinute) {
	const auto now = at(2026, 10, 7, 14, 30);
	EXPECT_EQ(label(now, now), "Just now");
	EXPECT_EQ(label(now - chr::seconds{59}, now), "Just now");
}

TEST(RecentsOpenedLabel, ExactlyAMinuteIsToday) {
	const auto now = at(2026, 10, 7, 14, 30);
	EXPECT_EQ(label(now - chr::seconds{60}, now), "Today, 2:29 PM");
}

TEST(RecentsOpenedLabel, FutureIsJustNow) {
	const auto now = at(2026, 10, 7, 14, 30);
	EXPECT_EQ(label(now + chr::seconds{1}, now), "Just now");
	EXPECT_EQ(label(now + chr::hours{30}, now), "Just now");
}

TEST(RecentsOpenedLabel, TodayUsesTwelveHourClock) {
	const auto now = at(2026, 10, 7, 23, 59);
	EXPECT_EQ(label(at(2026, 10, 7, 10, 22), now), "Today, 10:22 AM");
	EXPECT_EQ(label(at(2026, 10, 7, 9, 5), now), "Today, 9:05 AM");
	EXPECT_EQ(label(at(2026, 10, 7, 13, 7), now), "Today, 1:07 PM");
	EXPECT_EQ(label(at(2026, 10, 7, 23, 58), now), "Today, 11:58 PM");
}

TEST(RecentsOpenedLabel, NoonAndMidnight) {
	const auto now = at(2026, 10, 7, 23, 59);
	EXPECT_EQ(label(at(2026, 10, 7, 0, 0), now), "Today, 12:00 AM");
	EXPECT_EQ(label(at(2026, 10, 7, 0, 5), now), "Today, 12:05 AM");
	EXPECT_EQ(label(at(2026, 10, 7, 11, 59), now), "Today, 11:59 AM");
	EXPECT_EQ(label(at(2026, 10, 7, 12, 0), now), "Today, 12:00 PM");
	EXPECT_EQ(label(at(2026, 10, 7, 12, 30), now), "Today, 12:30 PM");
}

TEST(RecentsOpenedLabel, Yesterday) {
	const auto now = at(2026, 10, 7, 0, 10);
	EXPECT_EQ(label(at(2026, 10, 6, 21, 5), now), "Yesterday, 9:05 PM");
	EXPECT_EQ(label(at(2026, 10, 6, 0, 0), now), "Yesterday, 12:00 AM");
	EXPECT_EQ(label(at(2026, 10, 6, 23, 59, 59), now), "Yesterday, 11:59 PM");
}

TEST(RecentsOpenedLabel, YesterdayAcrossMonthYearAndLeapDay) {
	EXPECT_EQ(label(at(2026, 9, 30, 8, 0), at(2026, 10, 1, 9, 0)),
	          "Yesterday, 8:00 AM");
	EXPECT_EQ(label(at(2025, 12, 31, 22, 0), at(2026, 1, 1, 10, 0)),
	          "Yesterday, 10:00 PM");
	EXPECT_EQ(label(at(2024, 2, 29, 12, 0), at(2024, 3, 1, 12, 0)),
	          "Yesterday, 12:00 PM");
}

TEST(RecentsOpenedLabel, EarlierThisYearHasNoYear) {
	const auto now = at(2026, 10, 7, 14, 30);
	EXPECT_EQ(label(at(2026, 10, 5, 12, 0), now), "Oct 5");
	EXPECT_EQ(label(at(2026, 3, 3, 12, 0), now), "Mar 3");
	EXPECT_EQ(label(at(2026, 1, 1, 0, 0), now), "Jan 1");
}

TEST(RecentsOpenedLabel, EveryMonthName) {
	const auto now = at(2026, 12, 31, 12, 0);
	const char* const names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	for (unsigned month = 1; month <= 12; ++month) {
		EXPECT_EQ(label(at(2026, month, 1, 12, 0), now),
		          std::string(names[month - 1]) + " 1");
	}
}

TEST(RecentsOpenedLabel, OtherYearsShowTheYear) {
	const auto now = at(2026, 10, 7, 14, 30);
	EXPECT_EQ(label(at(2025, 10, 5, 12, 0), now), "Oct 5, 2025");
	EXPECT_EQ(label(at(2025, 12, 31, 23, 59), now), "Dec 31, 2025");
	EXPECT_EQ(label(secs(0), now), "Jan 1, 1970");
}

TEST(RecentsOpenedLabel, BeforeTheEpochStillWorks) {
	EXPECT_EQ(label(secs(-1), at(2026, 10, 7, 14, 30)), "Dec 31, 1969");
}

TEST(RecentsOpenedLabel, PositiveOffsetMovesTheDay) {
	// 01:30 on Oct 8 in India
	const auto now = at(2026, 10, 7, 20, 0);
	EXPECT_EQ(label(at(2026, 10, 7, 19, 0), now, kIst), "Today, 12:30 AM");
	EXPECT_EQ(label(at(2026, 10, 7, 17, 0), now, kIst), "Yesterday, 10:30 PM");
	EXPECT_EQ(label(at(2026, 10, 7, 17, 0), now, kUtc), "Today, 5:00 PM");
}

TEST(RecentsOpenedLabel, NegativeOffsetMovesTheDay) {
	// 7:00 PM on Oct 6 at UTC-8
	const chr::minutes offset{-8 * 60};
	const auto now = at(2026, 10, 7, 3, 0);
	EXPECT_EQ(label(at(2026, 10, 7, 1, 0), now, offset), "Today, 5:00 PM");
	EXPECT_EQ(label(at(2026, 10, 6, 5, 0), now, offset), "Yesterday, 9:00 PM");
}

TEST(RecentsOpenedLabel, YearIsLocalToo) {
	// already 2027 in India, still 2026 in UTC
	const auto now = at(2026, 12, 31, 20, 0);
	EXPECT_EQ(label(at(2026, 6, 1, 12, 0), now, kIst), "Jun 1, 2026");
	EXPECT_EQ(label(at(2026, 6, 1, 12, 0), now, kUtc), "Jun 1");
}

TEST(RecentsOpenedLabel, JustNowIgnoresTheOffset) {
	const auto now = at(2026, 10, 7, 20, 0);
	EXPECT_EQ(label(now - chr::seconds{30}, now, kIst), "Just now");
}

// folderLabel

TEST(RecentsFolderLabel, ParentFolderName) {
	EXPECT_EQ(helpers::folderLabel(fs::path("home") / "Downloads" / "a.pdf"),
	          "Downloads");
	EXPECT_EQ(helpers::folderLabel(fs::path("Downloads/a.pdf")), "Downloads");
}

TEST(RecentsFolderLabel, BareFileNameHasNoFolder) {
	EXPECT_EQ(helpers::folderLabel(fs::path("a.pdf")), "");
	EXPECT_EQ(helpers::folderLabel(fs::path()), "");
}

TEST(RecentsFolderLabel, FileInTheRootGivesTheRoot) {
	const fs::path root = fs::current_path().root_path();
	ASSERT_FALSE(root.empty());
	EXPECT_EQ(helpers::folderLabel(root / "a.pdf"), root.string());
}

TEST(RecentsFolderLabel, NonAsciiFolderIsUtf8) {
	const fs::path path = fs::path(std::u8string(u8"docs/caf\u00e9")) / "a.pdf";
	EXPECT_EQ(helpers::folderLabel(path), "caf\xC3\xA9");
}
