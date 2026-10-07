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
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "keybinds.h"

namespace helpers {
// ordered, so the saved file lists keys the way upstream wrote them
using Json = nlohmann::ordered_json;

// the "general" section: the two checkboxes in the settings window
struct GeneralSettings {
	bool MaximizeOnOpen = true;
	bool DisplayThumbs = true;

	bool operator==(const GeneralSettings&) const = default;
};

// port of app/helpers/settings.ts NightPDFSettings (L4-8)
struct NightPDFSettings {
	std::string version;
	GeneralSettings general;
	KeybindsMap keybinds;

	bool operator==(const NightPDFSettings&) const = default;
};

// json in the shape upstream's config.json has (names, nesting, values).
// the from_json ones expect valid input, run it through isValidSection or
// settingsFromJson first.
void to_json(Json& j, const ModifierKey& modifier);
void from_json(const Json& j, ModifierKey& modifier);
void to_json(Json& j, const Keybind& keybind);
void from_json(const Json& j, Keybind& keybind);
void to_json(Json& j, const Keybinds& keybinds);
void from_json(const Json& j, Keybinds& keybinds);
void to_json(Json& j, const GeneralSettings& general);
void from_json(const Json& j, GeneralSettings& general);
void to_json(Json& j, const NightPDFSettings& settings);
void from_json(const Json& j, NightPDFSettings& settings);

// port of app/helpers/settings.ts nightpdf_default_settings() (L296-376)
NightPDFSettings nightpdf_default_settings(const std::string& version);

// port of app/helpers/settings.ts nightpdf_schema (L200-294)
// section is "version", "general" or "keybinds". Anything else is false.
bool isValidSection(const std::string& section, const Json& value);

// port of app/main/app.ts store migration (L88-104)
// returns true when it changed something
bool migrateSettings(Json& json, const std::string& version);

// port of app/main/app.ts makeStore() (L51-68)
// Takes whatever was parsed from the file. A section that is missing gets its
// defaults, a section that fails isValidSection gets its defaults too and its
// name is added to resetSections (if given). The other sections are kept.
NightPDFSettings
settingsFromJson(const Json& json, const std::string& version,
                 std::vector<std::string>* resetSections = nullptr);

// reads, migrates and checks the file. No file, or a file that isn't json,
// gives the defaults. It never writes, call saveSettings for that.
NightPDFSettings
loadSettings(const std::filesystem::path& path, const std::string& version,
             std::vector<std::string>* resetSections = nullptr);

// writes "<path>.tmp" and renames it over the real file so a crash can't
// leave half a config behind. false if anything failed.
bool saveSettings(const std::filesystem::path& path,
                  const NightPDFSettings& settings);

// port of app/main/app.ts setkeybind() (L115-125)
void setkeybind(NightPDFSettings& settings, const std::string& id,
                const Keybinds& command);
} // namespace helpers
