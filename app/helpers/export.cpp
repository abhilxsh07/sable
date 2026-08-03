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
#include "export.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace helpers {
namespace {
std::optional<int> number(std::wstring_view text) {
	if (text.empty() || text.size() > 9) {
		return {};
	}
	int value = 0;
	for (const auto c : text) {
		if (c < L'0' || c > L'9') {
			return {};
		}
		value = value * 10 + (c - L'0');
	}
	return value;
}
} // namespace

std::optional<std::vector<int>> parsePageList(std::wstring_view text,
                                              int count) {
	std::wstring compact;
	for (const auto c : text) {
		if (c != L' ' && c != L'\t') {
			compact.push_back(c == L'–' ? L'-' : c);
		}
	}
	if (compact.empty() || count <= 0) {
		return {};
	}
	std::set<int> pages;
	std::size_t start = 0;
	while (start <= compact.size()) {
		const auto end = (std::min)(compact.find(L',', start), compact.size());
		const std::wstring_view part(compact.data() + start, end - start);
		const auto dash = part.find(L'-');
		const auto first = number(part.substr(0, dash));
		const auto last = dash == std::wstring_view::npos
		                      ? first
		                      : number(part.substr(dash + 1));
		if (!first || !last || *first < 1 || *last < 1 || *first > count ||
		    *last > count) {
			return {};
		}
		for (int page = (std::min)(*first, *last);
		     page <= (std::max)(*first, *last); ++page) {
			pages.insert(page - 1);
		}
		start = end + 1;
	}
	return std::vector<int>(pages.begin(), pages.end());
}

std::wstring exportName(std::wstring_view stem, int page, int last,
                        std::wstring_view extension) {
	const auto digits =
	    (std::max)(std::to_wstring(last).size(), std::size_t{3});
	auto number = std::to_wstring(page);
	number.insert(0, digits - (std::min)(digits, number.size()), L'0');
	return std::wstring(stem) + L" - page " + number + std::wstring(extension);
}

std::pair<int, int> exportPixels(double width, double height, int dpi) {
	if (!(width > 0) || !(height > 0) || dpi <= 0) {
		return {0, 0};
	}
	double wide = width * dpi / 72, tall = height * dpi / 72;
	const double side = (std::max)(wide, tall);
	if (side > 12000) {
		wide *= 12000 / side;
		tall *= 12000 / side;
	}
	if (wide * tall > 80e6) {
		const double shrink = std::sqrt(80e6 / (wide * tall));
		wide *= shrink;
		tall *= shrink;
	}
	return {(std::max)(1, static_cast<int>(std::lround(wide))),
	        (std::max)(1, static_cast<int>(std::lround(tall)))};
}
} // namespace helpers
