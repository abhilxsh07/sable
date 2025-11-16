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
#include <cstdint>
#include <string>
#include <string_view>
namespace helpers {
// Missing or malformed dates return an empty string. Dates retain their
// explicit UTC offset; an absent timezone is displayed without inventing one.
std::string propertyDate(std::string_view raw);
std::string propertyFileSize(std::uint64_t bytes);
std::string propertyPageSize(double width, double height);
std::string propertyText(std::string text);
} // namespace helpers
