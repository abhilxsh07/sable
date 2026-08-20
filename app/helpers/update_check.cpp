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
#include "update_check.h"

#include "store_file.h"

#include <charconv>
#include <tuple>
#include <nlohmann/json.hpp>

namespace helpers {
namespace {
std::optional<unsigned long> number(std::string_view text) {
	if (text.empty() || (text.size() > 1 && text.front() == '0')) {
		return std::nullopt;
	}
	unsigned long value = 0;
	const auto [end, error] =
	    std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return value;
}

bool identifier(std::string_view text) {
	if (text.empty()) {
		return false;
	}
	for (const char c : text) {
		const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
		                (c >= 'A' && c <= 'Z') || c == '-';
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool numeric(const std::string& part) {
	return part.find_first_not_of("0123456789") == std::string::npos;
}
} // namespace

std::optional<ReleaseVersion> parseReleaseVersion(std::string_view tag) {
	if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) {
		tag.remove_prefix(1);
	}
	if (const auto plus = tag.find('+'); plus != std::string_view::npos) {
		tag = tag.substr(0, plus);
	}
	std::string_view pre;
	if (const auto dash = tag.find('-'); dash != std::string_view::npos) {
		pre = tag.substr(dash + 1);
		tag = tag.substr(0, dash);
		if (pre.empty()) {
			return std::nullopt;
		}
	}
	ReleaseVersion version;
	unsigned long* parts[] = {&version.major, &version.minor, &version.patch};
	for (std::size_t i = 0; i < 3; ++i) {
		const auto dot = i < 2 ? tag.find('.') : tag.size();
		if (dot == std::string_view::npos) {
			return std::nullopt;
		}
		const auto value = number(tag.substr(0, dot));
		if (!value) {
			return std::nullopt;
		}
		*parts[i] = *value;
		tag.remove_prefix(i < 2 ? dot + 1 : dot);
	}
	while (!pre.empty()) {
		const auto dot = pre.find('.');
		const auto part = pre.substr(0, dot);
		if (!identifier(part)) {
			return std::nullopt;
		}
		version.pre.emplace_back(part);
		if (dot == std::string_view::npos) {
			break;
		}
		pre.remove_prefix(dot + 1);
		if (pre.empty()) {
			return std::nullopt;
		}
	}
	return version;
}

std::strong_ordering compareReleaseVersions(const ReleaseVersion& a,
                                            const ReleaseVersion& b) {
	if (const auto order = std::tie(a.major, a.minor, a.patch) <=>
	                       std::tie(b.major, b.minor, b.patch);
	    order != 0) {
		return order;
	}
	if (a.pre.empty() && b.pre.empty()) {
		return std::strong_ordering::equal;
	}
	if (a.pre.empty() || b.pre.empty()) {
		return a.pre.empty() ? std::strong_ordering::greater
		                     : std::strong_ordering::less;
	}
	for (std::size_t i = 0; i < a.pre.size() && i < b.pre.size(); ++i) {
		const auto& x = a.pre[i];
		const auto& y = b.pre[i];
		const bool xn = numeric(x), yn = numeric(y);
		if (xn && yn) {
			if (const auto order =
			        x.size() == y.size() ? x <=> y : x.size() <=> y.size();
			    order != 0) {
				return order;
			}
		} else if (xn != yn) {
			return xn ? std::strong_ordering::less
			          : std::strong_ordering::greater;
		} else if (const auto order = x <=> y; order != 0) {
			return order;
		}
	}
	return a.pre.size() <=> b.pre.size();
}

std::optional<AvailableRelease> newerRelease(std::string_view json,
                                             std::string_view current) {
	const auto installed = parseReleaseVersion(current);
	if (!installed) {
		return std::nullopt;
	}
	const auto list = nlohmann::json::parse(json, nullptr, false);
	if (!list.is_array()) {
		return std::nullopt;
	}
	std::optional<AvailableRelease> best;
	std::optional<ReleaseVersion> bestVersion;
	for (const auto& item : list) {
		if (!item.is_object()) {
			continue;
		}
		const auto flag = [&](const char* key) {
			const auto found = item.find(key);
			return found != item.end() && found->is_boolean() &&
			       found->get<bool>();
		};
		const auto text = [&](const char* key) -> std::string {
			const auto found = item.find(key);
			return found != item.end() && found->is_string()
			           ? found->get<std::string>()
			           : std::string{};
		};
		if (flag("draft") || (flag("prerelease") && installed->pre.empty())) {
			continue;
		}
		const auto tag = text("tag_name");
		const auto page = text("html_url");
		const auto version = parseReleaseVersion(tag);
		// only this repository's own page for that tag is ever opened
		if (!version || tag.size() > 64 ||
		    tag.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyz"
		                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-+") !=
		        std::string::npos ||
		    page != std::string(releasePagePrefix) + "tag/" + tag) {
			continue;
		}
		if (compareReleaseVersions(*version, *installed) <= 0) {
			continue;
		}
		if (!bestVersion ||
		    compareReleaseVersions(*version, *bestVersion) > 0) {
			bestVersion = version;
			best = AvailableRelease{tag, page};
		}
	}
	return best;
}

UpdateCheckChoice loadUpdateCheck(const std::filesystem::path& file) noexcept {
	UpdateCheckChoice choice;
	if (file.empty()) {
		return choice;
	}
	try {
		const auto text = readStoreFile(file);
		if (!text) {
			return choice;
		}
		const auto json = nlohmann::json::parse(*text, nullptr, false);
		if (!json.is_object()) {
			return choice;
		}
		if (const auto enabled = json.find("enabled");
		    enabled != json.end() && enabled->is_boolean()) {
			choice.enabled = enabled->get<bool>();
		}
	} catch (...) {
		return {};
	}
	return choice;
}

bool saveUpdateCheck(const std::filesystem::path& file,
                     const UpdateCheckChoice& choice) noexcept {
	if (file.empty()) {
		return false;
	}
	try {
		const nlohmann::json json{{"enabled", choice.enabled}};
		return writeStoreFile(file, json.dump(1, '\t') + "\n");
	} catch (...) {
		return false;
	}
}
} // namespace helpers
