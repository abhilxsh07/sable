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
#include "recents.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

#include <array>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace helpers {
namespace {
namespace fs = std::filesystem;
namespace chr = std::chrono;
using RJson = nlohmann::ordered_json;

// the oldest and newest timestamps a file may have (1970 and the end of 9999)
constexpr std::int64_t kMinOpened = 0;
constexpr std::int64_t kMaxOpened = 253402300799;

std::string toUtf8(const fs::path& path) {
	try {
		const std::u8string text = path.u8string();
		return std::string(reinterpret_cast<const char*>(text.data()),
		                   text.size());
	} catch (const std::exception&) {
		return std::string();
	}
}

fs::path fromUtf8(const std::string& text) {
	return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),
	                              text.size()));
}

std::string displayName(const fs::path& path) {
	return toUtf8(path.filename());
}

template <typename Char> Char lowerChar(Char c) {
	if (c >= 'A' && c <= 'Z') {
		return static_cast<Char>(c + ('a' - 'A'));
	}
	if constexpr (sizeof(Char) > 1) {
		return static_cast<Char>(std::towlower(static_cast<std::wint_t>(c)));
	} else {
		return c;
	}
}

// what two paths are compared by
fs::path::string_type foldedKey(const fs::path& path) {
	fs::path::string_type key = path.lexically_normal().native();
	for (auto& c : key) {
		c = lowerChar(c);
	}
	return key;
}

std::vector<RecentFile>::iterator findEntry(std::vector<RecentFile>& items,
                                            const fs::path& path) {
	const fs::path::string_type key = foldedKey(path);
	return std::find_if(items.begin(), items.end(), [&key](const auto& item) {
		return foldedKey(item.path) == key;
	});
}

bool readFile(const fs::path& path, std::string& out) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	out.assign(std::istreambuf_iterator<char>(in),
	           std::istreambuf_iterator<char>());
	return !in.bad();
}

bool readInt64(const RJson& j, std::int64_t& out) {
	if (j.is_number_unsigned()) {
		const auto value = j.get<std::uint64_t>();
		const auto limit = static_cast<std::uint64_t>(
		    std::numeric_limits<std::int64_t>::max());
		if (value > limit) {
			return false;
		}
		out = static_cast<std::int64_t>(value);
		return true;
	}
	if (j.is_number_integer()) {
		out = j.get<std::int64_t>();
		return true;
	}
	return false;
}

// one element of "files". false if it can't be used. Only "path" has to be
// there, the rest falls back to defaults, but a field of the wrong type
// throws the whole entry out.
bool readEntry(const RJson& j, RecentFile& out) {
	if (!j.is_object()) {
		return false;
	}
	const auto pathIt = j.find("path");
	if (pathIt == j.end() || !pathIt->is_string()) {
		return false;
	}
	const std::string raw = pathIt->get<std::string>();
	if (raw.empty()) {
		return false;
	}

	RecentFile entry;
	entry.path = fromUtf8(raw);

	if (const auto it = j.find("name"); it != j.end()) {
		if (!it->is_string()) {
			return false;
		}
		entry.name = it->get<std::string>();
	}
	if (const auto it = j.find("opened"); it != j.end()) {
		std::int64_t seconds = 0;
		if (!readInt64(*it, seconds) || seconds < kMinOpened ||
		    seconds > kMaxOpened) {
			return false;
		}
		entry.openedAt = chr::sys_seconds{chr::seconds{seconds}};
	}
	if (const auto it = j.find("size"); it != j.end()) {
		if (!it->is_number_unsigned()) {
			return false;
		}
		entry.sizeBytes = it->get<std::uint64_t>();
	}
	if (const auto it = j.find("starred"); it != j.end()) {
		if (!it->is_boolean()) {
			return false;
		}
		entry.starred = it->get<bool>();
	}
	if (entry.name.empty()) {
		entry.name = displayName(entry.path);
	}
	out = std::move(entry);
	return true;
}

std::string twoDigits(int value) {
	std::string text = std::to_string(value);
	if (text.size() < 2) {
		text.insert(text.begin(), '0');
	}
	return text;
}

// "10:22 AM", from seconds since local midnight
std::string clockLabel(std::int64_t secondsOfDay) {
	const int hour = static_cast<int>(secondsOfDay / 3600);
	const int minute = static_cast<int>(secondsOfDay % 3600 / 60);
	const int hour12 = hour % 12 == 0 ? 12 : hour % 12;
	return std::to_string(hour12) + ":" + twoDigits(minute) +
	       (hour < 12 ? " AM" : " PM");
}
} // namespace

void RecentFiles::trim() {
	using Diff = std::vector<RecentFile>::difference_type;
	while (m_items.size() > kMaxRecentFiles) {
		// the oldest unstarred one, the top entry is never picked
		std::size_t victim = m_items.size() - 1;
		for (std::size_t i = m_items.size(); i-- > 1;) {
			if (!m_items[i].starred) {
				victim = i;
				break;
			}
		}
		m_items.erase(m_items.begin() + static_cast<Diff>(victim));
	}
}

void RecentFiles::add(RecentFile file) {
	if (file.name.empty()) {
		file.name = displayName(file.path);
	}
	const auto existing = findEntry(m_items, file.path);
	if (existing != m_items.end()) {
		file.starred = existing->starred;
		m_items.erase(existing);
	}
	m_items.insert(m_items.begin(), std::move(file));
	trim();
}

bool RecentFiles::remove(const fs::path& path) {
	const auto existing = findEntry(m_items, path);
	if (existing == m_items.end()) {
		return false;
	}
	m_items.erase(existing);
	return true;
}

bool RecentFiles::toggleStar(const fs::path& path) {
	const auto existing = findEntry(m_items, path);
	if (existing == m_items.end()) {
		return false;
	}
	existing->starred = !existing->starred;
	return existing->starred;
}

const std::vector<RecentFile>& RecentFiles::items() const {
	return m_items;
}

std::vector<RecentFile> RecentFiles::starred() const {
	std::vector<RecentFile> result;
	for (const RecentFile& item : m_items) {
		if (item.starred) {
			result.push_back(item);
		}
	}
	return result;
}

bool RecentFiles::empty() const {
	return m_items.empty();
}

void RecentFiles::clear() {
	m_items.clear();
}

RecentFiles loadRecents(const fs::path& file) {
	RecentFiles result;
	try {
		std::string text;
		if (!readFile(file, text)) {
			return result;
		}
		const RJson json = RJson::parse(text, nullptr, false);
		if (json.is_discarded() || !json.is_object()) {
			return result;
		}
		const auto files = json.find("files");
		if (files == json.end() || !files->is_array()) {
			return result;
		}

		std::unordered_map<fs::path::string_type, std::size_t> seen;
		for (const RJson& item : *files) {
			RecentFile entry;
			if (!readEntry(item, entry)) {
				continue;
			}
			const auto [slot, inserted] =
			    seen.try_emplace(foldedKey(entry.path), result.m_items.size());
			if (!inserted) {
				RecentFile& first = result.m_items[slot->second];
				first.starred = first.starred || entry.starred;
				continue;
			}
			result.m_items.push_back(std::move(entry));
		}
		result.trim();
	} catch (const std::exception&) {
		return RecentFiles();
	}
	return result;
}

bool saveRecents(const fs::path& file, const RecentFiles& recents) {
	std::string text;
	try {
		RJson files = RJson::array();
		for (const RecentFile& item : recents.items()) {
			const std::string path = toUtf8(item.path);
			if (path.empty()) {
				continue;
			}
			RJson entry = RJson::object();
			entry["path"] = path;
			entry["name"] = item.name;
			const auto opened = item.openedAt.time_since_epoch().count();
			entry["opened"] = static_cast<std::int64_t>(opened);
			entry["size"] = item.sizeBytes;
			entry["starred"] = item.starred;
			files.push_back(std::move(entry));
		}
		RJson json = RJson::object();
		json["version"] = 1;
		json["files"] = std::move(files);
		// tabs, like the settings file
		text = json.dump(1, '\t', false, RJson::error_handler_t::replace);
	} catch (const std::exception&) {
		return false;
	}

	std::error_code ec;
	if (file.has_parent_path()) {
		fs::create_directories(file.parent_path(), ec);
	}
	fs::path tmp = file;
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
	fs::rename(tmp, file, ec);
	if (ec) {
		std::error_code ignored;
		fs::remove(tmp, ignored);
		return false;
	}
	return true;
}

std::string humanSize(std::uint64_t bytes) {
	static constexpr std::array<const char*, 5> kUnits = {"B", "KB", "MB", "GB",
	                                                      "TB"};
	if (bytes < 1024) {
		return std::to_string(bytes) + " B";
	}

	double value = static_cast<double>(bytes);
	std::size_t unit = 0;
	while (value >= 1024.0 && unit + 1 < kUnits.size()) {
		value /= 1024.0;
		++unit;
	}

	if (unit == 1) {
		// KB has no decimals; 1023.5 and up rounds to 1024, which is 1.0 MB
		const auto whole = static_cast<std::uint64_t>(std::llround(value));
		if (whole < 1024) {
			return std::to_string(whole) + " KB";
		}
		value = 1.0;
		unit = 2;
	}

	// tenths as a whole number so the locale can't change the separator
	auto tenths = static_cast<std::uint64_t>(std::llround(value * 10.0));
	if (tenths >= 10240 && unit + 1 < kUnits.size()) {
		tenths = 10;
		++unit;
	}
	return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) +
	       " " + kUnits[unit];
}

std::string openedLabel(chr::sys_seconds opened, chr::sys_seconds now,
                        chr::minutes utcOffset) {
	if (now - opened < chr::seconds{60}) {
		return "Just now";
	}

	static constexpr std::array<const char*, 12> kMonths = {
	    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	const chr::sys_seconds localOpened = opened + utcOffset;
	const chr::sys_seconds localNow = now + utcOffset;
	const chr::sys_days openedDay = chr::floor<chr::days>(localOpened);
	const chr::sys_days nowDay = chr::floor<chr::days>(localNow);

	if (openedDay == nowDay) {
		const std::int64_t secondsOfDay = (localOpened - openedDay).count();
		return "Today, " + clockLabel(secondsOfDay);
	}
	if (openedDay + chr::days{1} == nowDay) {
		const std::int64_t secondsOfDay = (localOpened - openedDay).count();
		return "Yesterday, " + clockLabel(secondsOfDay);
	}

	const chr::year_month_day date{openedDay};
	const chr::year_month_day today{nowDay};
	std::string label = kMonths[static_cast<unsigned>(date.month()) - 1];
	label += ' ';
	label += std::to_string(static_cast<unsigned>(date.day()));
	if (date.year() != today.year()) {
		label += ", ";
		label += std::to_string(static_cast<int>(date.year()));
	}
	return label;
}

std::string folderLabel(const fs::path& path) {
	const fs::path parent = path.parent_path();
	const fs::path leaf = parent.filename();
	if (!leaf.empty()) {
		return toUtf8(leaf);
	}
	return toUtf8(parent);
}
} // namespace helpers
