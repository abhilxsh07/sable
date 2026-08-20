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
#pragma once

#include <compare>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helpers {
// NightPDF called electron-updater's checkForUpdatesAndNotify once a window
// opened. Sable has no updater, so it only asks GitHub for its release list
// once per start and says when a newer release is out. Nothing is
// downloaded.
// see:
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/main/app.ts#L150-L159
inline constexpr wchar_t updateCheckUrl[] =
    L"https://api.github.com/repos/abhilxsh07/sable/releases?per_page=10";
inline constexpr std::string_view releasePagePrefix =
    "https://github.com/abhilxsh07/sable/releases/";

// "v1.2.3-alpha.1" or "1.2.3". Build metadata after "+" is ignored.
struct ReleaseVersion {
	unsigned long major = 0, minor = 0, patch = 0;
	std::vector<std::string> pre;
};
std::optional<ReleaseVersion> parseReleaseVersion(std::string_view tag);
// semver precedence: a prerelease sorts before its release, numeric parts
// compare as numbers and before text parts
std::strong_ordering compareReleaseVersions(const ReleaseVersion& a,
                                            const ReleaseVersion& b);

struct AvailableRelease {
	std::string tag;
	std::string page;
	bool operator==(const AvailableRelease&) const = default;
};
// The newest published release in a GitHub /releases response that is newer
// than current. Drafts never count; prereleases count only while current
// is a prerelease itself. A release whose page isn't this repository's
// releases/tag/<tag> is ignored.
std::optional<AvailableRelease> newerRelease(std::string_view json,
                                             std::string_view current);

// Whether to check at all. It lives in update-check.json next to
// config.json, like read-aloud.json, so the settings file keeps NightPDF's
// keys. A missing or unreadable file means on, as NightPDF always checked.
struct UpdateCheckChoice {
	bool enabled = true;
	bool operator==(const UpdateCheckChoice&) const = default;
};
UpdateCheckChoice loadUpdateCheck(const std::filesystem::path& file) noexcept;
bool saveUpdateCheck(const std::filesystem::path& file,
                     const UpdateCheckChoice& choice) noexcept;
} // namespace helpers
