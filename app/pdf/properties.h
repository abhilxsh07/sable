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
#include <optional>
#include <string>

namespace pdf {
// Values from the loaded document snapshot; safe to pass between threads.
struct Properties {
	std::uint64_t bytes = 0;
	int pages = 0;
	std::optional<int> version;
	std::optional<bool> linearized;
	bool encrypted = false, limited = false;
	std::string title, author, subject, keywords, creator, producer;
	std::string created, modified;
};
} // namespace pdf
