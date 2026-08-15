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
#include <string>
#include <string_view>

namespace helpers {
// The voice and speed Read Aloud last used. They live in read-aloud.json
// next to config.json, like night-run.json, so the settings file keeps the
// shape NightPDF wrote. An empty voice means Windows' default voice.
struct ReadAloudChoice {
	std::string voice;
	double rate = 1;
	bool operator==(const ReadAloudChoice&) const = default;
};
// the speeds the bar offers, slowest first
inline constexpr double readAloudRates[] = {.5, .75, 1, 1.25, 1.5, 2};
// the offered speed closest to rate; 1 for anything that isn't a number
double nearestReadAloudRate(double rate);
// a missing or unreadable file gives the defaults; an empty path never
// touches the disk.
ReadAloudChoice loadReadAloud(const std::filesystem::path& file) noexcept;
bool saveReadAloud(const std::filesystem::path& file,
                   const ReadAloudChoice& choice) noexcept;
// a page's text as one run for the voice: words hyphenated across a line
// break are joined, other line breaks and runs of spaces become one space.
std::string speakableText(std::string_view text);
// "x1.25" style labels for the speed menu
std::wstring readAloudRateLabel(double rate);
} // namespace helpers
