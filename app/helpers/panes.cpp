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
#include "panes.h"

#include "store_file.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace helpers {
double clampPaneWidth(double width) noexcept {
	if (!std::isfinite(width)) {
		return paneDefaultWidth;
	}
	return std::round(std::fmin(std::fmax(width, paneMinWidth), paneMaxWidth));
}

double fitPaneWidth(double width, double room) noexcept {
	const double clamped = clampPaneWidth(width);
	if (std::isnan(room) || room >= clamped) {
		return clamped;
	}
	return std::fmax(std::floor(room), paneMinWidth);
}

double PaneWidths::width(std::string_view pane) const {
	const auto found = widths.find(pane);
	return found == widths.end() ? paneDefaultWidth : found->second;
}

namespace {
// the file's object, or an empty one when it's missing or unreadable
nlohmann::json readPanes(const std::filesystem::path& file) {
	const auto text = readStoreFile(file);
	if (!text) {
		return nlohmann::json::object();
	}
	auto json = nlohmann::json::parse(*text, nullptr, false);
	return json.is_object() ? json : nlohmann::json::object();
}
} // namespace

PaneWidths loadPaneWidths(const std::filesystem::path& file) noexcept {
	PaneWidths panes;
	if (file.empty()) {
		return panes;
	}
	try {
		const auto json = readPanes(file);
		for (const auto& [name, value] : json.items()) {
			if (value.is_number() && name.size() <= 64) {
				panes.widths[name] = clampPaneWidth(value.get<double>());
			}
		}
	} catch (...) {
		return {};
	}
	return panes;
}

bool savePaneWidth(const std::filesystem::path& file, std::string_view pane,
                   double width) noexcept {
	if (file.empty() || pane.empty()) {
		return false;
	}
	try {
		return withStoreLock(file, [&] {
			auto json = readPanes(file);
			json[std::string(pane)] = clampPaneWidth(width);
			return writeStoreFile(file, json.dump(1, '\t') + "\n");
		});
	} catch (...) {
		return false;
	}
}
} // namespace helpers
