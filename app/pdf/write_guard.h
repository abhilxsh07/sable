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
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace pdf {
// why a write is refused. None means the edit may go ahead.
enum class WriteRefusal { None, Signed, Unavailable };

namespace detail {
// i read a rewritten copy token by token for signature names. stream
// bodies, strings and comments are skipped and #xx escapes are decoded, so
// /SigFlags, a font called /Signika or compressed bytes don't count.
// /Sig only counts as a field type, a dictionary type or a bare object.
inline bool signatureTokens(std::string_view bytes) {
	const auto space = [](char c) {
		return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
	};
	const auto delimiter = [](char c) {
		return std::string_view("()<>[]{}/%").find(c) != std::string_view::npos;
	};
	const auto hex = [](char c) {
		return c >= '0' && c <= '9'   ? c - '0'
		       : c >= 'a' && c <= 'f' ? c - 'a' + 10
		       : c >= 'A' && c <= 'F' ? c - 'A' + 10
		                              : -1;
	};
	const std::size_t size = bytes.size();
	std::string previous;
	std::optional<std::size_t> length;
	bool lengthNext = false, afterObj = false;
	std::size_t at = 0;
	while (at < size) {
		const char c = bytes[at];
		if (space(c)) {
			++at;
			continue;
		}
		const bool wantLength = lengthNext, bare = afterObj;
		lengthNext = false;
		afterObj = false;
		if (c == '%') {
			while (at < size && bytes[at] != '\n' && bytes[at] != '\r') {
				++at;
			}
			continue;
		}
		if (c == '(') {
			int depth = 0;
			do {
				if (bytes[at] == '\\') {
					++at;
				} else if (bytes[at] == '(') {
					++depth;
				} else if (bytes[at] == ')') {
					--depth;
				}
				++at;
			} while (at < size && depth > 0);
			previous.clear();
			continue;
		}
		if (c == '<') {
			if (at + 1 < size && bytes[at + 1] == '<') {
				at += 2;
			} else {
				const auto close = bytes.find('>', at);
				at = close == std::string_view::npos ? size : close + 1;
			}
			previous.clear();
			continue;
		}
		if (c == '/') {
			std::string name;
			for (++at; at < size && !space(bytes[at]) && !delimiter(bytes[at]);
			     ++at) {
				if (bytes[at] == '#' && at + 2 < size &&
				    hex(bytes[at + 1]) >= 0 && hex(bytes[at + 2]) >= 0) {
					name.push_back(static_cast<char>(hex(bytes[at + 1]) * 16 +
					                                 hex(bytes[at + 2])));
					at += 2;
				} else {
					name.push_back(bytes[at]);
				}
			}
			if (name == "DocMDP" || name == "UR3" ||
			    (name == "Sig" &&
			     (bare || previous == "FT" || previous == "Type"))) {
				return true;
			}
			lengthNext = name == "Length";
			previous = std::move(name);
			continue;
		}
		previous.clear();
		if (delimiter(c)) {
			++at;
			continue;
		}
		const std::size_t start = at;
		while (at < size && !space(bytes[at]) && !delimiter(bytes[at])) {
			++at;
		}
		const auto word = bytes.substr(start, at - start);
		if (word == "obj") {
			length.reset();
			afterObj = true;
		} else if (wantLength) {
			std::size_t value = 0;
			bool number = word.size() <= 9;
			for (const char digit : word) {
				number = number && digit >= '0' && digit <= '9';
				value = value * 10 + static_cast<std::size_t>(digit - '0');
			}
			length = number ? std::optional<std::size_t>(value) : std::nullopt;
		} else if (word == "stream") {
			if (at < size && bytes[at] == '\r') {
				++at;
			}
			if (at < size && bytes[at] == '\n') {
				++at;
			}
			// an indirect or wrong /Length falls back to the keyword.
			std::size_t end = std::string_view::npos;
			if (length && *length <= size - at) {
				end = at + *length;
				while (end < size && space(bytes[end])) {
					++end;
				}
				if (bytes.substr(end, 9) != "endstream") {
					end = std::string_view::npos;
				}
			}
			if (end == std::string_view::npos) {
				end = bytes.find("endstream", at);
			}
			at = end == std::string_view::npos ? size : end + 9;
			length.reset();
		}
	}
	return false;
}
} // namespace detail
} // namespace pdf
