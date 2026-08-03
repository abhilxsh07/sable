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
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace helpers {
// "1-3, 5" as sorted, unique zero-based pages of a count-page document.
// Spaces are ignored; a range may run backwards. nullopt when any part is
// empty, out of range or not a number.
std::optional<std::vector<int>> parsePageList(std::wstring_view text,
                                              int count);
// "<stem> - page 007.png", padded to the widest page number in the set.
std::wstring exportName(std::wstring_view stem, int page, int last,
                        std::wstring_view extension);
// pixel sides for a page in points at dpi, shrunk to stay within 12000 px
// a side and 80 megapixels.
std::pair<int, int> exportPixels(double width, double height, int dpi);
} // namespace helpers
