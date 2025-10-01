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
#include "search.h"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace pdf {
namespace {
struct Folded {
	std::wstring text;
	std::vector<int> first, last;
};
std::wstring utf16(char32_t c) {
	if (!c || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
		c = 0xFFFD;
	}
	if (c <= 0xFFFF) {
		return std::wstring(1, static_cast<wchar_t>(c));
	}
	c -= 0x10000;
	return {static_cast<wchar_t>(0xD800 + (c >> 10)),
	        static_cast<wchar_t>(0xDC00 + (c & 1023))};
}
std::u32string scalars(std::u16string_view text) {
	std::u32string result;
	for (std::size_t i = 0; i < text.size(); ++i) {
		char32_t c = text[i];
		if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() &&
		    text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
			c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
		}
		result += c;
	}
	return result;
}
bool whitespace(wchar_t c) {
	WORD type = 0;
	GetStringTypeW(CT_CTYPE1, &c, 1, &type);
	return (type & C1_SPACE) != 0;
}
bool diacritic(wchar_t c) {
	WORD type = 0;
	GetStringTypeW(CT_CTYPE3, &c, 1, &type);
	return (type & (C3_NONSPACING | C3_DIACRITIC)) != 0;
}
bool word(wchar_t c) {
	WORD one = 0, three = 0;
	GetStringTypeW(CT_CTYPE1, &c, 1, &one);
	GetStringTypeW(CT_CTYPE3, &c, 1, &three);
	return c == L'_' || (one & (C1_ALPHA | C1_DIGIT)) ||
	       (three & (C3_NONSPACING | C3_DIACRITIC));
}
Folded fold(std::u32string_view text, FindOptions options,
            const std::function<bool()>& canceled) {
	Folded result;
	for (std::size_t index = 0; index < text.size(); ++index) {
		if (index % 1024 == 0 && canceled && canceled()) {
			return {};
		}
		if (!text[index]) {
			continue;
		}
		auto unit = utf16(text[index]);
		// see:
		// https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-normalizestring
		wchar_t decomposed[32]{};
		const int count =
		    NormalizeString(NormalizationD, unit.data(),
		                    static_cast<int>(unit.size()), decomposed, 32);
		if (count <= 0) {
			throw std::runtime_error("Unicode normalization failed");
		}
		unit.assign(decomposed, count);
		if (!options.matchCase) {
			wchar_t lower[64]{};
			const int length = LCMapStringEx(
			    LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, unit.data(),
			    static_cast<int>(unit.size()), lower, 64, nullptr, nullptr, 0);
			if (length <= 0) {
				throw std::runtime_error("Unicode case mapping failed");
			}
			unit.assign(lower, length);
		}
		for (const auto c : unit) {
			WORD type = 0;
			GetStringTypeW(CT_CTYPE3, &c, 1, &type);
			if (!options.matchDiacritics &&
			    (type & (C3_NONSPACING | C3_DIACRITIC))) {
				if (!result.last.empty()) {
					result.last.back() = static_cast<int>(index);
				}
				continue;
			}
			const wchar_t value = whitespace(c) ? L' ' : c;
			if (value == L' ' && !result.text.empty() &&
			    result.text.back() == L' ') {
				result.last.back() = static_cast<int>(index);
			} else {
				result.text += value;
				result.first.push_back(static_cast<int>(index));
				result.last.push_back(static_cast<int>(index));
			}
		}
	}
	return result;
}
} // namespace
FindPage foldedFind(std::u32string_view text, std::u16string_view query,
                    FindOptions options, int page, std::size_t limit,
                    const std::function<bool()>& canceled) {
	FindPage result;
	if (query.empty() || !limit) {
		return result;
	}
	const auto needle = fold(scalars(query), options, canceled).text;
	if (needle.empty()) {
		return result;
	}
	const auto haystack = fold(text, options, canceled);
	if (canceled && canceled()) {
		return result;
	}
	std::vector<std::size_t> prefix(needle.size());
	for (std::size_t i = 1, j = 0; i < needle.size(); ++i) {
		while (j && needle[i] != needle[j]) {
			j = prefix[j - 1];
		}
		if (needle[i] == needle[j]) {
			++j;
		}
		prefix[i] = j;
	}
	for (std::size_t i = 0, j = 0; i < haystack.text.size(); ++i) {
		if (i % 1024 == 0 && canceled && canceled()) {
			return {};
		}
		while (j && haystack.text[i] != needle[j]) {
			j = prefix[j - 1];
		}
		if (haystack.text[i] == needle[j]) {
			++j;
		}
		if (j != needle.size()) {
			continue;
		}
		const auto start = i + 1 - j;
		const bool completeAccent = !options.matchDiacritics ||
		                            i + 1 == haystack.text.size() ||
		                            !diacritic(haystack.text[i + 1]);
		const bool boundary =
		    completeAccent &&
		    (!options.wholeWords ||
		     ((!start || !word(haystack.text[start - 1])) &&
		      (i + 1 == haystack.text.size() || !word(haystack.text[i + 1]))));
		if (boundary) {
			if (result.matches.size() == limit) {
				result.limited = true;
				break;
			}
			result.matches.push_back(
			    {page, haystack.first[start],
			     haystack.last[i] - haystack.first[start] + 1});
			j = 0; // Non-overlapping matches, like PDFium without
			       // FPDF_CONSECUTIVE.
		} else {
			j = prefix[j - 1];
		}
	}
	return result;
}
} // namespace pdf
