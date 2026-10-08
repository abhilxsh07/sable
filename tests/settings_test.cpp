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
#include <filesystem>
#include <fstream>
#include <sstream>

#include "keybinds.h"
#include "settings.h"

using helpers::Json;
using helpers::NightPDFSettings;
namespace fs = std::filesystem;

namespace {
const char* const kVersion = "1.2.0";

// each test gets its own folder under the system temp dir
class SettingsFiles : public ::testing::Test {
  protected:
	void SetUp() override {
		static std::atomic<int> counter{0};
		const auto stamp =
		    std::chrono::steady_clock::now().time_since_epoch().count();
		m_dir = fs::temp_directory_path() /
		        ("sable_settings_test_" + std::to_string(stamp) + "_" +
		         std::to_string(counter++));
		fs::create_directories(m_dir);
	}

	void TearDown() override {
		std::error_code ec;
		fs::remove_all(m_dir, ec);
	}

	fs::path file(const std::string& name = "config.json") const {
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

Json defaultsJson() {
	return Json(helpers::nightpdf_default_settings(kVersion));
}
} // namespace

TEST(Settings, DefaultsMatchUpstream) {
	const NightPDFSettings settings =
	    helpers::nightpdf_default_settings(kVersion);
	EXPECT_EQ(settings.version, kVersion);
	EXPECT_TRUE(settings.general.MaximizeOnOpen);
	EXPECT_TRUE(settings.general.DisplayThumbs);
	EXPECT_EQ(settings.keybinds.keys(),
	          (std::vector<std::string>{"OpenWindow", "CloseWindow", "ReOpen",
	                                    "SwitchTab", "PreviousTab", "LeftTab",
	                                    "RightTab", "StartTab", "EndTab"}));
}

TEST(Settings, JsonHasUpstreamShape) {
	const Json j = defaultsJson();
	ASSERT_TRUE(j.is_object());
	EXPECT_EQ(j.at("version"), kVersion);
	EXPECT_EQ(j.at("general"),
	          (Json{{"MaximizeOnOpen", true}, {"DisplayThumbs", true}}));

	const Json& open = j.at("keybinds").at("OpenWindow");
	EXPECT_EQ(open.at("action"), "openNewPDF");
	EXPECT_EQ(open.at("displayName"), "Open New PDF");
	EXPECT_FALSE(open.contains("data"));
	ASSERT_EQ(open.at("keybind").size(), 1u);

	// the Ctrl has to be in the file this time
	const Json& keybind = open.at("keybind").at(0);
	EXPECT_EQ(keybind.at("key"), "T");
	const Json& ctrl = keybind.at("modifiers").at("CommandOrControl");
	EXPECT_EQ(ctrl.at("savesAs"), "Ctrl");
	EXPECT_EQ(ctrl.at("osDependent"), true);
	EXPECT_EQ(ctrl.at("osVariants"),
	          (Json{{"darwin", "\xE2\x8C\x98"}, {"default", "Ctrl"}}));
	EXPECT_FALSE(ctrl.contains("displayAs"));

	const Json& switchTab = j.at("keybinds").at("SwitchTab");
	EXPECT_EQ(switchTab.at("data"), "next");
	EXPECT_EQ(switchTab.at("keybind").size(), 2u);
}

TEST(Settings, DefaultsPassTheSchema) {
	const Json j = defaultsJson();
	EXPECT_TRUE(helpers::isValidSection("version", j.at("version")));
	EXPECT_TRUE(helpers::isValidSection("general", j.at("general")));
	EXPECT_TRUE(helpers::isValidSection("keybinds", j.at("keybinds")));
	EXPECT_FALSE(helpers::isValidSection("nope", j.at("general")));
}

TEST(Settings, JsonRoundTrip) {
	const NightPDFSettings settings =
	    helpers::nightpdf_default_settings(kVersion);
	const Json j = settings;
	const NightPDFSettings back = j.get<NightPDFSettings>();
	EXPECT_EQ(back, settings);

	// a null key survives too
	NightPDFSettings custom = settings;
	custom.keybinds.at("EndTab").keybind[0].key = std::nullopt;
	const Json cj = custom;
	EXPECT_TRUE(
	    cj.at("keybinds").at("EndTab").at("keybind").at(0).at("key").is_null());
	EXPECT_EQ(cj.get<NightPDFSettings>(), custom);
}

TEST(Settings, SchemaRejectsBadKeybinds) {
	const auto valid = [](const Json& entry) {
		return helpers::isValidSection("keybinds", Json{{"OpenWindow", entry}});
	};
	const Json good = defaultsJson().at("keybinds").at("OpenWindow");
	EXPECT_TRUE(valid(good));

	Json noAction = good;
	noAction.erase("action");
	EXPECT_FALSE(valid(noAction));

	Json extraField = good;
	extraField["surprise"] = 1;
	EXPECT_FALSE(valid(extraField));

	Json tooMany = good;
	tooMany["keybind"].push_back(good.at("keybind").at(0));
	tooMany["keybind"].push_back(good.at("keybind").at(0));
	EXPECT_FALSE(valid(tooMany));

	Json badKey = good;
	badKey["keybind"][0]["key"] = 5;
	EXPECT_FALSE(valid(badKey));

	Json badModifier = good;
	badModifier["keybind"][0]["modifiers"]["CommandOrControl"].erase("savesAs");
	EXPECT_FALSE(valid(badModifier));

	Json badName = good;
	badName["keybind"][0]["modifiers"]["Not Allowed"] =
	    good.at("keybind").at(0).at("modifiers").at("CommandOrControl");
	EXPECT_FALSE(valid(badName));

	Json badVariant = good;
	badVariant["keybind"][0]["modifiers"]["CommandOrControl"]["osVariants"]
	          ["default"] = 3;
	EXPECT_FALSE(valid(badVariant));

	Json nullKey = good;
	nullKey["keybind"][0]["key"] = nullptr;
	EXPECT_TRUE(valid(nullKey));

	EXPECT_FALSE(
	    helpers::isValidSection("general", Json{{"DisplayThumbs", "x"}}));
	EXPECT_FALSE(helpers::isValidSection("version", 3));
}

TEST(Settings, InvalidGeneralResetsOnlyGeneral) {
	Json j = defaultsJson();
	j["general"] = Json{{"MaximizeOnOpen", "yes"}, {"DisplayThumbs", false}};
	// a binding the user changed, it has to survive
	const auto alt = helpers::KeybindHelper::fromTrigger("Alt+Q", "win32");
	j["keybinds"]["OpenWindow"]["keybind"] = Json::array({alt.getKeybind()});

	std::vector<std::string> reset;
	const NightPDFSettings settings =
	    helpers::settingsFromJson(j, kVersion, &reset);

	EXPECT_EQ(reset, (std::vector<std::string>{"general"}));
	EXPECT_EQ(settings.general,
	          helpers::nightpdf_default_settings(kVersion).general);
	const helpers::KeybindsHelper keybinds(settings.keybinds, "win32");
	EXPECT_EQ(keybinds.getActionKeybindsTrigger("OpenWindow"),
	          (std::vector<std::string>{"Alt+Q"}));
	EXPECT_EQ(keybinds.getActionKeybindsTrigger("CloseWindow"),
	          (std::vector<std::string>{"Ctrl+w", "Ctrl+F4"}));
}

TEST(Settings, InvalidKeybindsResetOnlyKeybinds) {
	Json j = defaultsJson();
	j["general"]["MaximizeOnOpen"] = false;
	j["keybinds"]["EndTab"]["action"] = 7;

	std::vector<std::string> reset;
	const NightPDFSettings settings =
	    helpers::settingsFromJson(j, kVersion, &reset);
	EXPECT_EQ(reset, (std::vector<std::string>{"keybinds"}));
	EXPECT_FALSE(settings.general.MaximizeOnOpen);
	EXPECT_EQ(settings.keybinds,
	          helpers::nightpdf_default_settings(kVersion).keybinds);
}

TEST(Settings, MissingSectionsTakeDefaults) {
	std::vector<std::string> reset;
	const NightPDFSettings settings = helpers::settingsFromJson(
	    Json{{"version", kVersion},
	         {"general", Json{{"DisplayThumbs", false}}}},
	    kVersion, &reset);
	EXPECT_TRUE(reset.empty());
	EXPECT_FALSE(settings.general.DisplayThumbs);
	// not in the file, so it stays at its default
	EXPECT_TRUE(settings.general.MaximizeOnOpen);
	EXPECT_EQ(settings.keybinds,
	          helpers::nightpdf_default_settings(kVersion).keybinds);
}

TEST(Settings, ExtraKeybindIdsAreKeptWhenTheyLookRight) {
	Json j = defaultsJson();
	j["keybinds"]["Custom"] = j.at("keybinds").at("EndTab");
	j["keybinds"]["Junk"] = 5;
	const NightPDFSettings settings = helpers::settingsFromJson(j, kVersion);
	EXPECT_TRUE(settings.keybinds.contains("Custom"));
	EXPECT_FALSE(settings.keybinds.contains("Junk"));
}

TEST(Settings, SetKeybindReplacesInPlace) {
	NightPDFSettings settings = helpers::nightpdf_default_settings(kVersion);
	helpers::Keybinds command = settings.keybinds.at("ReOpen");
	command.keybind =
	    helpers::KeybindHelper::keybindFromTriggerArray({"Alt+R"});
	helpers::setkeybind(settings, "ReOpen", command);
	EXPECT_EQ(settings.keybinds.at("ReOpen").keybind[0].key,
	          std::optional<std::string>("R"));
	EXPECT_EQ(settings.keybinds.keys()[2], "ReOpen");
	EXPECT_EQ(settings.keybinds.size(), 9u);
}

TEST(Migration, OlderVersionGetsUpdatedAndFixed) {
	Json j = defaultsJson();
	j["version"] = "0.9.0";
	j["general"]["DisplayThumbs"] = "maybe";
	j["general"]["MaximizeOnOpen"] = false;

	EXPECT_TRUE(helpers::migrateSettings(j, kVersion));
	EXPECT_EQ(j.at("version"), kVersion);
	EXPECT_EQ(j.at("general").at("DisplayThumbs"), true);
	EXPECT_EQ(j.at("general").at("MaximizeOnOpen"), false);

	// running it again does nothing
	EXPECT_FALSE(helpers::migrateSettings(j, kVersion));
}

TEST(Migration, OldFileWithoutDisplayThumbs) {
	Json j = Json{{"version", "0.5.0"},
	              {"general", Json{{"MaximizeOnOpen", false}}}};
	EXPECT_TRUE(helpers::migrateSettings(j, kVersion));
	EXPECT_EQ(j.at("general"),
	          (Json{{"MaximizeOnOpen", false}, {"DisplayThumbs", true}}));
}

TEST(Migration, MissingVersionIsFilledIn) {
	Json j = Json::object();
	EXPECT_TRUE(helpers::migrateSettings(j, kVersion));
	EXPECT_EQ(j.at("version"), kVersion);
	EXPECT_FALSE(j.contains("general"));

	Json same = defaultsJson();
	EXPECT_FALSE(helpers::migrateSettings(same, kVersion));

	Json notAnObject = Json::array();
	EXPECT_FALSE(helpers::migrateSettings(notAnObject, kVersion));
}

TEST_F(SettingsFiles, SaveThenLoadGivesTheSameSettings) {
	NightPDFSettings settings = helpers::nightpdf_default_settings(kVersion);
	settings.general.MaximizeOnOpen = false;
	settings.keybinds.at("OpenWindow").keybind =
	    helpers::KeybindHelper::keybindFromTriggerArray(
	        {"Alt+Shift+O", "Meta+O"});

	ASSERT_TRUE(helpers::saveSettings(file(), settings));
	EXPECT_FALSE(fs::exists(file().string() + ".tmp"));

	std::vector<std::string> reset;
	const NightPDFSettings loaded =
	    helpers::loadSettings(file(), kVersion, &reset);
	EXPECT_TRUE(reset.empty());
	EXPECT_EQ(loaded, settings);
}

TEST_F(SettingsFiles, SaveOverwritesAnExistingFile) {
	write(file(), "{\"old\": true}");
	NightPDFSettings settings = helpers::nightpdf_default_settings(kVersion);
	ASSERT_TRUE(helpers::saveSettings(file(), settings));
	EXPECT_EQ(read(file()).find("old"), std::string::npos);

	settings.general.DisplayThumbs = false;
	ASSERT_TRUE(helpers::saveSettings(file(), settings));
	EXPECT_EQ(helpers::loadSettings(file(), kVersion), settings);
}

TEST_F(SettingsFiles, SaveMakesMissingFolders) {
	const fs::path nested = file("a") / "b" / "config.json";
	const NightPDFSettings settings =
	    helpers::nightpdf_default_settings(kVersion);
	ASSERT_TRUE(helpers::saveSettings(nested, settings));
	EXPECT_EQ(helpers::loadSettings(nested, kVersion), settings);
}

TEST_F(SettingsFiles, SavedFileIsPlainTabIndentedJson) {
	ASSERT_TRUE(helpers::saveSettings(
	    file(), helpers::nightpdf_default_settings(kVersion)));
	const std::string text = read(file());
	EXPECT_EQ(text.find('\r'), std::string::npos);
	EXPECT_NE(text.find("\n\t\"general\""), std::string::npos);
	EXPECT_NE(text.find("\"CommandOrControl\""), std::string::npos);
	EXPECT_FALSE(Json::parse(text, nullptr, false).is_discarded());
}

TEST_F(SettingsFiles, SaveFailsWhenTheTargetIsAFolder) {
	fs::create_directories(file("config.json"));
	EXPECT_FALSE(helpers::saveSettings(
	    file(), helpers::nightpdf_default_settings(kVersion)));
	EXPECT_FALSE(fs::exists(file().string() + ".tmp"));
}

TEST_F(SettingsFiles, MissingFileGivesDefaults) {
	const NightPDFSettings settings =
	    helpers::loadSettings(file("nope.json"), kVersion);
	EXPECT_EQ(settings, helpers::nightpdf_default_settings(kVersion));
}

TEST_F(SettingsFiles, CorruptJsonGivesDefaults) {
	write(file(), "{ \"version\": \"1.2.0\", \"general\": {");
	EXPECT_EQ(helpers::loadSettings(file(), kVersion),
	          helpers::nightpdf_default_settings(kVersion));

	write(file(), "[1, 2, 3]");
	EXPECT_EQ(helpers::loadSettings(file(), kVersion),
	          helpers::nightpdf_default_settings(kVersion));

	write(file(), "");
	EXPECT_EQ(helpers::loadSettings(file(), kVersion),
	          helpers::nightpdf_default_settings(kVersion));
}

TEST_F(SettingsFiles, LoadMigratesAnOlderFile) {
	Json j = defaultsJson();
	j["version"] = "0.9.0";
	j["general"] = Json{{"MaximizeOnOpen", false}};
	write(file(), j.dump());

	const NightPDFSettings settings = helpers::loadSettings(file(), kVersion);
	EXPECT_EQ(settings.version, kVersion);
	EXPECT_FALSE(settings.general.MaximizeOnOpen);
	EXPECT_TRUE(settings.general.DisplayThumbs);
}

TEST_F(SettingsFiles, LoadResetsABadSectionOnly) {
	Json j = defaultsJson();
	j["general"] = "oops";
	j["keybinds"]["LeftTab"]["keybind"] = Json::array();
	write(file(), j.dump());

	std::vector<std::string> reset;
	const NightPDFSettings settings =
	    helpers::loadSettings(file(), kVersion, &reset);
	EXPECT_EQ(reset, (std::vector<std::string>{"general"}));
	EXPECT_TRUE(settings.keybinds.at("LeftTab").keybind.empty());
	EXPECT_EQ(settings.general, helpers::GeneralSettings{});
}
