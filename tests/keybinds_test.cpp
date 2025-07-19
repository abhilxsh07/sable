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

#include "keybinds.h"
#include "settings.h"

using helpers::KeybindHelper;
using helpers::KeybindsHelper;

namespace {
struct Expected {
	const char* id;
	std::vector<std::string> triggers;
	const char* action;
	const char* data; // nullptr when there is none
	const char* displayName;
};

// what the settings file has had since the first release
const std::vector<Expected>& expectedDefaults() {
	static const std::vector<Expected> expected = {
	    {"OpenWindow", {"Ctrl+T"}, "openNewPDF", nullptr, "Open New PDF"},
	    {"CloseWindow",
	     {"Ctrl+w", "Ctrl+F4"},
	     "close-tab",
	     nullptr,
	     "Close Tab"},
	    {"ReOpen", {"Ctrl+Shift+T"}, "reopen-tab", nullptr, "Reopen Tab"},
	    {"SwitchTab",
	     {"Ctrl+Tab", "Ctrl+PageDown"},
	     "switch-tab",
	     "next",
	     "Switch Tab"},
	    {"PreviousTab",
	     {"Ctrl+Shift+Tab", "Ctrl+PageUp"},
	     "switch-tab",
	     "prev",
	     "Previous Tab"},
	    {"LeftTab", {"Ctrl+Shift+PageUp"}, "move-tab", "prev", "Move Tab Left"},
	    {"RightTab",
	     {"Ctrl+Shift+PageDown"},
	     "move-tab",
	     "next",
	     "Move Tab Right"},
	    {"StartTab",
	     {"Ctrl+Shift+Home"},
	     "move-tab",
	     "start",
	     "Move Tab to Start"},
	    {"EndTab", {"Ctrl+Shift+End"}, "move-tab", "end", "Move Tab to End"},
	};
	return expected;
}

KeybindsHelper defaultsHelper(const std::string& platform) {
	return KeybindsHelper(helpers::nightpdf_default_settings("1.0.0").keybinds,
	                      platform);
}
} // namespace

TEST(KeybindHelper, DefaultsRoundTripThroughTrigger) {
	for (const Expected& row : expectedDefaults()) {
		for (const std::string& trigger : row.triggers) {
			const auto helper = KeybindHelper::fromTrigger(trigger, "win32");
			EXPECT_EQ(helper.toTrigger(), trigger) << row.id;
		}
	}
}

TEST(KeybindHelper, DefaultSettingsKeepTheirCtrl) {
	// the old bug: "Ctrl" wasn't found in ModifierKeys, so it was dropped
	const KeybindsHelper keybinds = defaultsHelper("win32");
	for (const Expected& row : expectedDefaults()) {
		EXPECT_EQ(keybinds.getActionKeybindsTrigger(row.id), row.triggers)
		    << row.id;
		for (const KeybindHelper& helper : keybinds.getActionKeybinds(row.id)) {
			EXPECT_TRUE(
			    helper.getKeybind().modifiers.contains("CommandOrControl"))
			    << row.id;
			EXPECT_EQ(helper.toTrigger().rfind("Ctrl+", 0), 0u) << row.id;
			EXPECT_EQ(helper.toStringArray().front(), "Ctrl") << row.id;
		}
	}
}

TEST(KeybindHelper, TokenCanBeNameOrSavesAs) {
	const auto byName =
	    KeybindHelper::fromTrigger("CommandOrControl+T", "win32");
	const auto bySavesAs = KeybindHelper::fromTrigger("Ctrl+T", "win32");
	EXPECT_EQ(byName.getKeybind(), bySavesAs.getKeybind());
	EXPECT_EQ(byName.toTrigger(), "Ctrl+T");

	// Control is both a name and a savesAs, and stays Control
	const auto control = KeybindHelper::fromTrigger("Control+T", "win32");
	EXPECT_TRUE(control.getKeybind().modifiers.contains("Control"));
	EXPECT_FALSE(control.getKeybind().modifiers.contains("CommandOrControl"));
	EXPECT_EQ(control.toTrigger(), "Control+T");
}

TEST(KeybindHelper, UnknownModifierIsDropped) {
	const auto helper = KeybindHelper::fromTrigger("Hyper+Shift+T", "win32");
	EXPECT_EQ(helper.getKeybind().modifiers.size(), 1u);
	EXPECT_EQ(helper.toTrigger(), "Shift+T");
}

TEST(KeybindHelper, SplitsLikeUpstream) {
	const auto plain = KeybindHelper::fromTrigger("F5", "win32");
	EXPECT_TRUE(plain.getKeybind().modifiers.empty());
	EXPECT_EQ(plain.getKey(), std::optional<std::string>("F5"));

	const auto empty = KeybindHelper::fromTrigger("", "win32");
	EXPECT_EQ(empty.getKey(), std::optional<std::string>(""));
}

TEST(KeybindHelper, KeepsModifierOrder) {
	const auto helper = KeybindHelper::fromTrigger("Shift+Alt+Ctrl+X", "win32");
	EXPECT_EQ(helper.toTrigger(), "Shift+Alt+Ctrl+X");
	const auto values = helper.getModifierKeys();
	ASSERT_EQ(values.size(), 3u);
	EXPECT_EQ(values[0].savesAs, "Shift");
	EXPECT_EQ(values[2].savesAs, "Ctrl");
}

TEST(KeybindHelper, Win32DisplayStrings) {
	const auto ctrl = KeybindHelper::fromTrigger("Ctrl+Shift+PageUp", "win32");
	EXPECT_EQ(ctrl.toString(), "Ctrl + Shift + PageUp");

	const auto meta = KeybindHelper::fromTrigger("Meta+Alt+X", "win32");
	EXPECT_EQ(meta.toString(), "Win + Alt + X");

	const auto control = KeybindHelper::fromTrigger("Control+X", "win32");
	EXPECT_EQ(control.toString(), "Ctrl + X");

	const auto altGr = KeybindHelper::fromTrigger("AltGraph+X", "win32");
	EXPECT_EQ(altGr.toString(), "AltGr + X");
	EXPECT_EQ(altGr.toStringArray(), (std::vector<std::string>{"AltGr", "X"}));
}

TEST(KeybindHelper, OtherPlatformsUseTheirVariant) {
	const auto mac = KeybindHelper::fromTrigger("Ctrl+Meta+X", "darwin");
	EXPECT_EQ(mac.toString(), "\xE2\x8C\x98 + \xE2\x8C\x98 + X");
	const auto linux = KeybindHelper::fromTrigger("Ctrl+Meta+X", "linux");
	EXPECT_EQ(linux.toString(), "Ctrl + Meta + X");
	const auto none = KeybindHelper::fromTrigger("Meta+X", "none");
	EXPECT_EQ(none.toString(), "Meta + X");
}

TEST(KeybindHelper, ModifierToString) {
	EXPECT_EQ(helpers::modifierToString("Meta", "win32"), "Win");
	EXPECT_EQ(helpers::modifierToString("Alt", "win32"), "Alt");
	EXPECT_EQ(helpers::modifierToString("Shift", "darwin"), "Shift");
	EXPECT_EQ(helpers::modifierToString("CommandOrControl", "win32"), "Ctrl");
	// not in the table, so shown as it is
	EXPECT_EQ(helpers::modifierToString("Hyper", "win32"), "Hyper");
}

TEST(KeybindHelper, EmptyBindingShowsNothing) {
	helpers::Keybind keybind;
	keybind.modifiers.set("Shift", helpers::ModifierKeys.at("Shift"));
	const auto helper = KeybindHelper::fromKeybind(keybind, "win32");
	EXPECT_TRUE(helper.toStringArray().empty());
	EXPECT_EQ(helper.toString(), "");
	EXPECT_EQ(helper.toTrigger(), "Shift+");
	EXPECT_EQ(helper.getKey(), std::nullopt);
}

TEST(KeybindHelper, KeybindFromTriggerArray) {
	const auto keybinds =
	    KeybindHelper::keybindFromTriggerArray({"Ctrl+A", "Alt+B"});
	ASSERT_EQ(keybinds.size(), 2u);
	EXPECT_EQ(keybinds[1].key, std::optional<std::string>("B"));
	const auto helpers = KeybindHelper::fromKeybindArray(keybinds, "win32");
	ASSERT_EQ(helpers.size(), 2u);
	EXPECT_EQ(helpers[0].toString(), "Ctrl + A");
	EXPECT_EQ(helpers[0].getPlatform(), "win32");
}

TEST(KeybindsHelper, ReadsTheDefaultActions) {
	const KeybindsHelper keybinds = defaultsHelper("win32");
	ASSERT_EQ(keybinds.actions.size(), expectedDefaults().size());
	for (std::size_t i = 0; i < expectedDefaults().size(); ++i) {
		const Expected& row = expectedDefaults()[i];
		EXPECT_EQ(keybinds.actions[i], row.id);
		EXPECT_EQ(keybinds.getAction(row.id), row.action);
		EXPECT_EQ(keybinds.getActionDisplayName(row.id), row.displayName);
		if (row.data == nullptr) {
			EXPECT_EQ(keybinds.getActionData(row.id), std::nullopt) << row.id;
		} else {
			EXPECT_EQ(keybinds.getActionData(row.id),
			          std::optional<std::string>(row.data))
			    << row.id;
		}
	}
	EXPECT_EQ(keybinds.getActionKeybindsString("SwitchTab"),
	          (std::vector<std::string>{"Ctrl + Tab", "Ctrl + PageDown"}));
	EXPECT_EQ(keybinds.getActionKeybind("CloseWindow", 1).toTrigger(),
	          "Ctrl+F4");
}

TEST(KeybindsHelper, DisplayNameFallsBackToTheAction) {
	helpers::KeybindsMap map;
	helpers::Keybinds entry;
	entry.action = "something";
	map.set("NoName", entry);
	const KeybindsHelper keybinds(map, "win32");
	EXPECT_EQ(keybinds.getActionDisplayName("NoName"), "NoName");
}

TEST(KeybindsHelper, UpdateAndRemove) {
	KeybindsHelper keybinds = defaultsHelper("win32");
	const auto replacement = KeybindHelper::fromTrigger("Alt+Q", "win32");

	keybinds.updateActionKeybind("OpenWindow", 0, replacement.getKeybind());
	EXPECT_EQ(keybinds.getActionKeybindsTrigger("OpenWindow"),
	          (std::vector<std::string>{"Alt+Q"}));

	// one past the end adds a second binding
	keybinds.updateActionKeybind(
	    "OpenWindow", 1,
	    KeybindHelper::fromTrigger("Ctrl+Q", "win32").getKeybind());
	EXPECT_EQ(keybinds.getActionKeybindsTrigger("OpenWindow"),
	          (std::vector<std::string>{"Alt+Q", "Ctrl+Q"}));
	EXPECT_THROW(
	    keybinds.updateActionKeybind("OpenWindow", 5, replacement.getKeybind()),
	    std::out_of_range);

	const helpers::Keybinds& after =
	    keybinds.removeActionKeybind("OpenWindow", 0);
	EXPECT_EQ(after.keybind.size(), 1u);
	EXPECT_EQ(keybinds.getActionKeybindsTrigger("OpenWindow"),
	          (std::vector<std::string>{"Ctrl+Q"}));
	// out of range removes nothing
	keybinds.removeActionKeybind("OpenWindow", 7);
	EXPECT_EQ(keybinds.getActionKeybinds("OpenWindow").size(), 1u);
}

TEST(KeybindsHelper, UnknownActionThrows) {
	const KeybindsHelper keybinds = defaultsHelper("win32");
	EXPECT_THROW(keybinds.getAction("Nope"), std::out_of_range);
	EXPECT_THROW(keybinds.getActionKeybinds("Nope"), std::out_of_range);
	EXPECT_THROW(keybinds.getActionKeybind("OpenWindow", 3), std::out_of_range);
}

TEST(OrderedMap, KeepsInsertionOrder) {
	helpers::OrderedMap<int> map;
	map.set("b", 1);
	map.set("a", 2);
	map.set("b", 3); // stays in front
	EXPECT_EQ(map.keys(), (std::vector<std::string>{"b", "a"}));
	EXPECT_EQ(map.at("b"), 3);
	EXPECT_TRUE(map.erase("b"));
	EXPECT_FALSE(map.erase("b"));
	EXPECT_EQ(map.size(), 1u);
}
