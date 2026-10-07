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
// see:
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/helpers/settings.ts
#include "settings.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace helpers {
namespace {
namespace fs = std::filesystem;

// "^[a-zA-Z0-9]+$"
bool isAlnumName(const std::string& text) {
	if (text.empty()) {
		return false;
	}
	for (char c : text) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		                (c >= '0' && c <= '9');
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool validOsVariants(const Json& j) {
	if (!j.is_object()) {
		return false;
	}
	for (const auto& item : j.items()) {
		if (!isAlnumName(item.key()) || !item.value().is_string()) {
			return false;
		}
	}
	return true;
}

// ModifierKey in the schema
bool validModifierKey(const Json& j) {
	if (!j.is_object()) {
		return false;
	}
	if (!j.contains("savesAs") || !j.contains("osDependent")) {
		return false;
	}
	for (const auto& item : j.items()) {
		const std::string& name = item.key();
		const Json& value = item.value();
		if (name == "savesAs" || name == "displayAs") {
			if (!value.is_string()) {
				return false;
			}
		} else if (name == "osDependent") {
			if (!value.is_boolean()) {
				return false;
			}
		} else if (name == "osVariants") {
			if (!validOsVariants(value)) {
				return false;
			}
		} else {
			return false;
		}
	}
	return true;
}

// Keybind in the schema
bool validKeybind(const Json& j) {
	if (!j.is_object() || !j.contains("modifiers") || !j.contains("key")) {
		return false;
	}
	for (const auto& item : j.items()) {
		const std::string& name = item.key();
		const Json& value = item.value();
		if (name == "key") {
			if (!value.is_string() && !value.is_null()) {
				return false;
			}
		} else if (name == "modifiers") {
			if (!value.is_object()) {
				return false;
			}
			for (const auto& modifier : value.items()) {
				if (!isAlnumName(modifier.key()) ||
				    !validModifierKey(modifier.value())) {
					return false;
				}
			}
		} else {
			return false;
		}
	}
	return true;
}

// port of app/helpers/settings.ts keybindPropertyDef() (L200-263), with the
// default min 0 and max 2
bool validKeybindsEntry(const Json& j) {
	if (!j.is_object() || !j.contains("keybind") || !j.contains("action")) {
		return false;
	}
	for (const auto& item : j.items()) {
		const std::string& name = item.key();
		const Json& value = item.value();
		if (name == "keybind") {
			if (!value.is_array() || value.size() > 2) {
				return false;
			}
			for (const Json& keybind : value) {
				if (!validKeybind(keybind)) {
					return false;
				}
			}
		} else if (name == "action" || name == "data" ||
		           name == "displayName") {
			if (!value.is_string()) {
				return false;
			}
		} else {
			return false;
		}
	}
	return true;
}

bool isKnownKeybindId(const std::string& id) {
	static const char* const ids[] = {
	    "OpenWindow", "CloseWindow", "ReOpen",   "SwitchTab", "PreviousTab",
	    "LeftTab",    "RightTab",    "StartTab", "EndTab",
	};
	for (const char* known : ids) {
		if (id == known) {
			return true;
		}
	}
	return false;
}

// reads the file into a string, false if it can't be opened
bool readFile(const fs::path& path, std::string& out) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	std::ostringstream buffer;
	buffer << in.rdbuf();
	out = buffer.str();
	return true;
}

Json osVariantsToJson(const OsVariants& variants) {
	Json j = Json::object();
	for (const auto& entry : variants) {
		j[entry.first] = entry.second;
	}
	return j;
}

KeybindsMap keybindsFromJson(const Json& j) {
	KeybindsMap result;
	for (const auto& item : j.items()) {
		// the schema only describes the nine known ids, so for anything
		// else I keep it only if it looks like a keybind
		if (isKnownKeybindId(item.key()) || validKeybindsEntry(item.value())) {
			result.set(item.key(), item.value().get<Keybinds>());
		}
	}
	return result;
}
} // namespace

void to_json(Json& j, const ModifierKey& modifier) {
	j = Json::object();
	j["savesAs"] = modifier.savesAs;
	j["osDependent"] = modifier.osDependent;
	if (modifier.displayAs) {
		j["displayAs"] = *modifier.displayAs;
	}
	if (modifier.osVariants) {
		j["osVariants"] = osVariantsToJson(*modifier.osVariants);
	}
}

void from_json(const Json& j, ModifierKey& modifier) {
	modifier = ModifierKey{};
	modifier.savesAs = j.at("savesAs").get<std::string>();
	modifier.osDependent = j.at("osDependent").get<bool>();
	if (j.contains("displayAs")) {
		modifier.displayAs = j.at("displayAs").get<std::string>();
	}
	if (j.contains("osVariants")) {
		OsVariants variants;
		for (const auto& item : j.at("osVariants").items()) {
			variants.set(item.key(), item.value().get<std::string>());
		}
		modifier.osVariants = variants;
	}
}

void to_json(Json& j, const Keybind& keybind) {
	j = Json::object();
	if (keybind.key) {
		j["key"] = *keybind.key;
	} else {
		j["key"] = nullptr;
	}
	Json modifiers = Json::object();
	for (const auto& entry : keybind.modifiers) {
		modifiers[entry.first] = entry.second;
	}
	j["modifiers"] = modifiers;
}

void from_json(const Json& j, Keybind& keybind) {
	keybind = Keybind{};
	const Json& key = j.at("key");
	if (!key.is_null()) {
		keybind.key = key.get<std::string>();
	}
	for (const auto& item : j.at("modifiers").items()) {
		keybind.modifiers.set(item.key(), item.value().get<ModifierKey>());
	}
}

void to_json(Json& j, const Keybinds& keybinds) {
	j = Json::object();
	Json list = Json::array();
	for (const Keybind& keybind : keybinds.keybind) {
		list.push_back(keybind);
	}
	j["keybind"] = list;
	j["action"] = keybinds.action;
	if (keybinds.data) {
		j["data"] = *keybinds.data;
	}
	if (keybinds.displayName) {
		j["displayName"] = *keybinds.displayName;
	}
}

void from_json(const Json& j, Keybinds& keybinds) {
	keybinds = Keybinds{};
	for (const Json& keybind : j.at("keybind")) {
		keybinds.keybind.push_back(keybind.get<Keybind>());
	}
	keybinds.action = j.at("action").get<std::string>();
	if (j.contains("data")) {
		keybinds.data = j.at("data").get<std::string>();
	}
	if (j.contains("displayName")) {
		keybinds.displayName = j.at("displayName").get<std::string>();
	}
}

void to_json(Json& j, const GeneralSettings& general) {
	j = Json::object();
	j["MaximizeOnOpen"] = general.MaximizeOnOpen;
	j["DisplayThumbs"] = general.DisplayThumbs;
}

// a key that isn't there keeps whatever the struct had
void from_json(const Json& j, GeneralSettings& general) {
	if (j.contains("MaximizeOnOpen")) {
		general.MaximizeOnOpen = j.at("MaximizeOnOpen").get<bool>();
	}
	if (j.contains("DisplayThumbs")) {
		general.DisplayThumbs = j.at("DisplayThumbs").get<bool>();
	}
}

void to_json(Json& j, const NightPDFSettings& settings) {
	j = Json::object();
	j["version"] = settings.version;
	j["general"] = settings.general;
	Json keybinds = Json::object();
	for (const auto& entry : settings.keybinds) {
		keybinds[entry.first] = entry.second;
	}
	j["keybinds"] = keybinds;
}

void from_json(const Json& j, NightPDFSettings& settings) {
	settings = NightPDFSettings{};
	settings.version = j.at("version").get<std::string>();
	settings.general = j.at("general").get<GeneralSettings>();
	settings.keybinds = keybindsFromJson(j.at("keybinds"));
}

// port of app/helpers/settings.ts nightpdf_default_settings() (L296-376)
NightPDFSettings nightpdf_default_settings(const std::string& version) {
	NightPDFSettings settings;
	settings.version = version;
	settings.general.MaximizeOnOpen = true;
	settings.general.DisplayThumbs = true;

	auto add = [&settings](const std::string& id,
	                       const std::vector<std::string>& triggers,
	                       const std::string& action,
	                       const std::optional<std::string>& data,
	                       const std::string& displayName) {
		Keybinds entry;
		entry.keybind = KeybindHelper::keybindFromTriggerArray(triggers);
		entry.action = action;
		entry.data = data;
		entry.displayName = displayName;
		settings.keybinds.set(id, entry);
	};
	const std::optional<std::string> none;

	add("OpenWindow", {"Ctrl+T"}, "openNewPDF", none, "Open New PDF");
	add("CloseWindow", {"Ctrl+w", "Ctrl+F4"}, "close-tab", none, "Close Tab");
	add("ReOpen", {"Ctrl+Shift+T"}, "reopen-tab", none, "Reopen Tab");
	add("SwitchTab", {"Ctrl+Tab", "Ctrl+PageDown"}, "switch-tab", "next",
	    "Switch Tab");
	add("PreviousTab", {"Ctrl+Shift+Tab", "Ctrl+PageUp"}, "switch-tab", "prev",
	    "Previous Tab");
	add("LeftTab", {"Ctrl+Shift+PageUp"}, "move-tab", "prev", "Move Tab Left");
	add("RightTab", {"Ctrl+Shift+PageDown"}, "move-tab", "next",
	    "Move Tab Right");
	add("StartTab", {"Ctrl+Shift+Home"}, "move-tab", "start",
	    "Move Tab to Start");
	add("EndTab", {"Ctrl+Shift+End"}, "move-tab", "end", "Move Tab to End");
	return settings;
}

// port of app/helpers/settings.ts nightpdf_schema (L200-294)
bool isValidSection(const std::string& section, const Json& value) {
	if (section == "version") {
		return value.is_string();
	}
	if (section == "general") {
		if (!value.is_object()) {
			return false;
		}
		for (const char* name : {"MaximizeOnOpen", "DisplayThumbs"}) {
			if (value.contains(name) && !value.at(name).is_boolean()) {
				return false;
			}
		}
		return true;
	}
	if (section == "keybinds") {
		if (!value.is_object()) {
			return false;
		}
		for (const auto& item : value.items()) {
			if (isKnownKeybindId(item.key()) &&
			    !validKeybindsEntry(item.value())) {
				return false;
			}
		}
		return true;
	}
	return false;
}

// port of app/main/app.ts store migration (L88-104)
// in the future this can be use for migrations
bool migrateSettings(Json& json, const std::string& version) {
	if (!json.is_object()) {
		return false;
	}
	bool changed = false;
	const auto stored = json.find("version");
	const bool hasVersion = stored != json.end() && stored->is_string() &&
	                        !stored->get<std::string>().empty();
	if (hasVersion) {
		if (stored->get<std::string>() != version) {
			const auto general = json.find("general");
			if (general != json.end() && general->is_object()) {
				const auto thumbs = general->find("DisplayThumbs");
				if (thumbs == general->end() || !thumbs->is_boolean()) {
					(*general)["DisplayThumbs"] = true;
				}
			}
			json["version"] = version;
			changed = true;
		}
	} else {
		json["version"] = version;
		changed = true;
	}
	return changed;
}

// port of app/main/app.ts makeStore() (L51-68)
// Upstream clears the whole config when one part is bad. Here only the bad
// section goes back to its defaults, the rest of the file is kept.
NightPDFSettings settingsFromJson(const Json& json, const std::string& version,
                                  std::vector<std::string>* resetSections) {
	NightPDFSettings settings = nightpdf_default_settings(version);
	if (resetSections != nullptr) {
		resetSections->clear();
	}
	if (!json.is_object()) {
		return settings;
	}

	// returns the section if it's there and fine. A missing one just takes
	// the defaults, like electron-store does.
	auto pick = [&](const char* name) -> const Json* {
		const auto it = json.find(name);
		if (it == json.end()) {
			return nullptr;
		}
		if (!isValidSection(name, *it)) {
			if (resetSections != nullptr) {
				resetSections->push_back(name);
			}
			return nullptr;
		}
		return &*it;
	};

	if (const Json* section = pick("version")) {
		settings.version = section->get<std::string>();
	}
	if (const Json* section = pick("general")) {
		settings.general = section->get<GeneralSettings>();
	}
	if (const Json* section = pick("keybinds")) {
		settings.keybinds = keybindsFromJson(*section);
	}
	return settings;
}

NightPDFSettings loadSettings(const fs::path& path, const std::string& version,
                              std::vector<std::string>* resetSections) {
	if (resetSections != nullptr) {
		resetSections->clear();
	}
	std::string text;
	if (!readFile(path, text)) {
		return nightpdf_default_settings(version);
	}
	Json json = Json::parse(text, nullptr, false);
	if (json.is_discarded() || !json.is_object()) {
		return nightpdf_default_settings(version);
	}
	migrateSettings(json, version);
	return settingsFromJson(json, version, resetSections);
}

bool saveSettings(const fs::path& path, const NightPDFSettings& settings) {
	std::string text;
	try {
		const Json json = settings;
		// tabs, like electron-store writes it
		text = json.dump(1, '\t', false, Json::error_handler_t::replace);
	} catch (const std::exception&) {
		return false;
	}

	std::error_code ec;
	if (path.has_parent_path()) {
		fs::create_directories(path.parent_path(), ec);
	}
	fs::path tmp = path;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out) {
			return false;
		}
		out << text;
		out.flush();
		if (!out) {
			out.close();
			fs::remove(tmp, ec);
			return false;
		}
	}
	fs::rename(tmp, path, ec);
	if (ec) {
		std::error_code ignored;
		fs::remove(tmp, ignored);
		return false;
	}
	return true;
}

// port of app/main/app.ts setkeybind() (L115-125)
void setkeybind(NightPDFSettings& settings, const std::string& id,
                const Keybinds& command) {
	settings.keybinds.set(id, command);
}
} // namespace helpers
