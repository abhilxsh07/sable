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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace helpers {
// one entry of the recent files list (the home screen cards)
struct RecentFile {
	std::filesystem::path path;
	std::string name; // UTF-8 display name, the file name by default
	std::chrono::sys_seconds openedAt{};
	std::uint64_t sizeBytes = 0;
	bool starred = false;

	bool operator==(const RecentFile&) const = default;
};

// the list never grows past this
inline constexpr std::size_t kMaxRecentFiles = 50;

// Newest first. Two paths are the same file when they match ignoring case
// (after lexically_normal), the way Windows sees them.
class RecentFiles {
  public:
	// An entry with the same path is moved to the top and replaced by file,
	// but it keeps the star it had. Anything else is inserted at the top.
	// Over kMaxRecentFiles the oldest entries that aren't starred go first,
	// starred ones only when nothing else is left (the new entry stays).
	// An empty name becomes the file name of the path.
	void add(RecentFile file);

	// true if the path was in the list
	bool remove(const std::filesystem::path& path);

	// returns the new state, false when the path isn't in the list
	bool toggleStar(const std::filesystem::path& path);

	const std::vector<RecentFile>& items() const;

	// only the starred ones, still newest first
	std::vector<RecentFile> starred() const;

	bool empty() const;
	void clear();

  private:
	friend RecentFiles loadRecents(const std::filesystem::path& file);

	void trim();

	std::vector<RecentFile> m_items;
};

// recent.json: {"version": 1, "files": [{"path", "name", "opened" (unix
// seconds), "size", "starred"}]}. Never throws. A missing or broken file
// gives an empty list, entries that don't make sense are skipped, duplicates
// are merged (the first one wins, a star on any of them stays) and only the
// first kMaxRecentFiles are kept, same rule as add().
RecentFiles loadRecents(const std::filesystem::path& file);

// writes "<file>.tmp" and renames it over the real file so a crash can't
// leave half a list behind. false if anything failed.
bool saveRecents(const std::filesystem::path& file, const RecentFiles& recents);

// "0 B", "512 B", "1 KB", "454 KB", "4.0 MB", "1.2 GB", "1.0 TB".
// Steps of 1024, whole numbers for KB, one decimal above that. A value that
// rounds up to 1024 of a unit is shown as 1.0 of the next one.
std::string humanSize(std::uint64_t bytes);

// "Just now", "Today, 10:22 AM", "Yesterday, 9:05 PM", "Oct 5" or
// "Oct 5, 2025". Days are counted in local time, which is UTC plus
// utcOffset. Meant for timestamps from loadRecents (years 1970 to 9999).
std::string openedLabel(std::chrono::sys_seconds opened,
                        std::chrono::sys_seconds now,
                        std::chrono::minutes utcOffset);

// name of the folder the file is in, as UTF-8 ("Downloads"). A file in the
// root gives the root ("C:\"), a bare file name gives "".
std::string folderLabel(const std::filesystem::path& path);
} // namespace helpers
