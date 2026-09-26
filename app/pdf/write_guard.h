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
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdf {
// why a write is refused. None means the edit may go ahead.
enum class WriteRefusal { None, Signed, Unavailable, TooLarge };

namespace detail {
enum class TokenGuard { Signatures, FormActions };
// i follow signature fields to their values in the rewritten copy. an
// empty field has no signature contents. strings, streams and comments
// cannot supply dictionary keys, and #xx escapes are decoded first.
inline bool guardedTokens(std::string_view bytes, TokenGuard guard) {
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
	using Object = std::pair<std::uint32_t, std::uint32_t>;
	const auto reference =
	    [&](std::string_view first, std::size_t& at,
	        std::string_view ending) -> std::optional<Object> {
		std::size_t next = at;
		const auto word = [&]() {
			while (next < bytes.size()) {
				if (space(bytes[next])) {
					++next;
				} else if (bytes[next] == '%') {
					while (next < bytes.size() && bytes[next] != '\n' &&
					       bytes[next] != '\r') {
						++next;
					}
				} else {
					break;
				}
			}
			const auto start = next;
			while (next < bytes.size() && !space(bytes[next]) &&
			       !delimiter(bytes[next])) {
				++next;
			}
			return bytes.substr(start, next - start);
		};
		Object id{};
		const auto number = [](std::string_view text, std::uint32_t& value) {
			const auto result =
			    std::from_chars(text.data(), text.data() + text.size(), value);
			return !text.empty() && result.ec == std::errc{} &&
			       result.ptr == text.data() + text.size();
		};
		if (!number(first, id.first) || !id.first ||
		    !number(word(), id.second) || word() != ending) {
			return {};
		}
		at = next;
		return id;
	};
	enum class Key { None, Other, FieldType, Contents, Value, Parent, Length };
	struct Field {
		bool typed = false, signature = false;
		std::optional<Object> parent;
	};
	struct Dictionary {
		Field field;
		Key key = Key::None;
		std::size_t arrays = 0;
		std::optional<Object> object, value;
		bool contents = false, valueContents = false, valueOfParent = false;
	};
	std::vector<Dictionary> dictionaries, candidates;
	std::map<Object, Field> fields;
	std::set<Object> contents;
	std::optional<Object> object;
	std::size_t arrays = 0;
	const auto takeKey = [&]() {
		return dictionaries.empty() || dictionaries.back().arrays != arrays
		           ? Key::None
		           : std::exchange(dictionaries.back().key, Key::None);
	};
	const auto stringValue = [&](bool nonempty) {
		if (takeKey() == Key::Contents) {
			dictionaries.back().contents = nonempty;
		}
	};
	const std::size_t size = bytes.size();
	std::optional<std::size_t> length;
	std::size_t at = 0;
	while (at < size) {
		const char c = bytes[at];
		if (space(c)) {
			++at;
			continue;
		}
		if (c == '%') {
			while (at < size && bytes[at] != '\n' && bytes[at] != '\r') {
				++at;
			}
			continue;
		}
		if (c == '(') {
			int depth = 1;
			bool nonempty = false;
			++at;
			while (at < size && depth > 0) {
				const char value = bytes[at++];
				if (value == '\\' && at < size) {
					const char escaped = bytes[at++];
					if (escaped == '\r' && at < size && bytes[at] == '\n') {
						++at;
					}
					nonempty = nonempty || (escaped != '\r' && escaped != '\n');
				} else if (value == '(') {
					++depth;
					nonempty = true;
				} else if (value == ')') {
					--depth;
					nonempty = nonempty || depth > 0;
				} else {
					nonempty = true;
				}
			}
			stringValue(nonempty);
			continue;
		}
		if (c == '<') {
			if (at + 1 < size && bytes[at + 1] == '<') {
				const bool value = takeKey() == Key::Value;
				Dictionary dictionary;
				dictionary.arrays = arrays;
				dictionary.valueOfParent = value;
				if (dictionaries.empty() && arrays == 0) {
					dictionary.object = object;
				}
				dictionaries.push_back(dictionary);
				at += 2;
			} else {
				bool nonempty = false;
				for (++at; at < size && bytes[at] != '>'; ++at) {
					nonempty = nonempty || hex(bytes[at]) >= 0;
				}
				at += at < size;
				stringValue(nonempty);
			}
			continue;
		}
		if (c == '>' && bytes.substr(at, 2) == ">>") {
			if (!dictionaries.empty()) {
				const auto dictionary = dictionaries.back();
				dictionaries.pop_back();
				if (dictionary.object) {
					if (dictionary.field.typed || dictionary.field.parent) {
						fields[*dictionary.object] = dictionary.field;
					}
					if (dictionary.contents) {
						contents.insert(*dictionary.object);
					}
				}
				if (dictionary.value || dictionary.valueContents) {
					candidates.push_back(dictionary);
				}
				if (dictionary.valueOfParent && !dictionaries.empty()) {
					dictionaries.back().valueContents = dictionary.contents;
				}
			}
			at += 2;
			continue;
		}
		if (c == '[') {
			takeKey();
			++arrays;
			++at;
			continue;
		}
		if (c == ']') {
			arrays -= arrays > 0;
			++at;
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
			if (guard == TokenGuard::FormActions &&
			    (name == "A" || name == "AA" || name == "JS" ||
			     name == "JavaScript")) {
				return true;
			}
			if (guard == TokenGuard::Signatures &&
			    (name == "DocMDP" || name == "Perms")) {
				return true;
			}
			if (!dictionaries.empty() && dictionaries.back().arrays == arrays) {
				auto& dictionary = dictionaries.back();
				if (dictionary.key == Key::None) {
					dictionary.key = name == "FT"         ? Key::FieldType
					                 : name == "Contents" ? Key::Contents
					                 : name == "V"        ? Key::Value
					                 : name == "Parent"   ? Key::Parent
					                 : name == "Length"   ? Key::Length
					                                      : Key::Other;
				} else if (takeKey() == Key::FieldType) {
					dictionary.field.typed = true;
					dictionary.field.signature = name == "Sig";
				}
			}
			continue;
		}
		if (delimiter(c)) {
			++at;
			continue;
		}
		const std::size_t start = at;
		while (at < size && !space(bytes[at]) && !delimiter(bytes[at])) {
			++at;
		}
		const auto word = bytes.substr(start, at - start);
		const auto key = takeKey();
		if (dictionaries.empty() && arrays == 0 && word != "stream") {
			object = reference(word, at, "obj");
			length.reset();
		} else if (key == Key::Value) {
			dictionaries.back().value = reference(word, at, "R");
		} else if (key == Key::Parent) {
			dictionaries.back().field.parent = reference(word, at, "R");
		} else if (key == Key::Length) {
			std::size_t value{};
			const auto result =
			    std::from_chars(word.data(), word.data() + word.size(), value);
			length = result.ec == std::errc{} &&
			                 result.ptr == word.data() + word.size()
			             ? std::optional<std::size_t>(value)
			             : std::nullopt;
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
	if (guard == TokenGuard::FormActions) {
		return false;
	}
	for (const auto& candidate : candidates) {
		if (!candidate.valueContents &&
		    (!candidate.value || !contents.contains(*candidate.value))) {
			continue;
		}
		auto field = candidate.field;
		std::set<Object> visited;
		while (!field.typed && field.parent &&
		       visited.insert(*field.parent).second) {
			const auto found = fields.find(*field.parent);
			if (found == fields.end()) {
				break;
			}
			field = found->second;
		}
		if (field.signature) {
			return true;
		}
	}
	return false;
}
inline bool signatureTokens(std::string_view bytes) {
	return guardedTokens(bytes, TokenGuard::Signatures);
}
inline bool formActionTokens(std::string_view bytes) {
	return guardedTokens(bytes, TokenGuard::FormActions);
}
} // namespace detail
} // namespace pdf
