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
#include <functional>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pdf {
struct FindOptions {
	bool matchCase = false;
	bool wholeWords = false;
	bool matchDiacritics = false;
};
struct FindMatch {
	int page = 0, first = 0, count = 0;
	bool operator==(const FindMatch&) const = default;
};
struct FindPage {
	std::vector<FindMatch> matches;
	bool limited = false;
};
// Accent-insensitive search keeps source character indices after Unicode
// decomposition, lowercasing and whitespace folding.
FindPage foldedFind(std::u32string_view text, std::u16string_view query,
                    FindOptions options, int page, std::size_t limit,
                    const std::function<bool()>& canceled = {});
} // namespace pdf
