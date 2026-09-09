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

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace helpers {
// The tool pane left of the pages. NightPDF had no such pane, so its width
// lives in panes.json next to config.json, like read-aloud.json, and
// config.json keeps NightPDF's keys. Each pane (the tool list, properties)
// keeps the last width it was given.
inline constexpr double paneMinWidth = 280.;
inline constexpr double paneMaxWidth = 480.;
inline constexpr double paneDefaultWidth = 320.;
// how far one arrow key on the pane's grip moves its edge
inline constexpr double paneWidthStep = 16.;

// a width the pane can take: whole pixels from 280 to 480. Anything that
// isn't a number is the default.
double clampPaneWidth(double width) noexcept;
// as clampPaneWidth, but no wider than room (what the pages can spare), and
// never under the minimum
double fitPaneWidth(double width, double room) noexcept;

struct PaneWidths {
	std::map<std::string, double, std::less<>> widths;
	// the width last given to pane, or the default
	double width(std::string_view pane) const;
	bool operator==(const PaneWidths&) const = default;
};
// a missing or unreadable file, or a value that isn't a number, gives the
// default; values out of range are clamped
PaneWidths loadPaneWidths(const std::filesystem::path& file) noexcept;
// one pane's width, keeping what the file says for the others: another
// window may have saved theirs
bool savePaneWidth(const std::filesystem::path& file, std::string_view pane,
                   double width) noexcept;
} // namespace helpers
