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
#include "properties.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace helpers {
namespace {
std::string number(double value, int precision = 1) {
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::fixed << std::setprecision(precision) << value;
	return out.str();
}
} // namespace
std::string propertyText(std::string text) {
	for (auto& c : text) {
		if (static_cast<unsigned char>(c) < 32 && c != '\n') {
			c = ' ';
		}
	}
	return text;
}
std::string propertyDate(std::string_view raw) {
	if (raw.starts_with("D:")) {
		raw.remove_prefix(2);
	}
	if (raw.size() < 4) {
		return {};
	}
	std::size_t at = 0;
	const auto digits = [&](std::size_t count) -> int {
		if (at + count > raw.size()) {
			return -1;
		}
		int value = 0;
		for (std::size_t i = 0; i < count; ++i) {
			const auto c = raw[at + i];
			if (c < '0' || c > '9') {
				return -1;
			}
			value = value * 10 + c - '0';
		}
		at += count;
		return value;
	};
	const int year = digits(4);
	int values[] = {1, 1, 0, 0, 0};
	for (auto& value : values) {
		if (at == raw.size() || raw[at] == 'Z' || raw[at] == '+' ||
		    raw[at] == '-') {
			break;
		}
		value = digits(2);
		if (value < 0) {
			return {};
		}
	}
	using namespace std::chrono;
	const year_month_day date{std::chrono::year{year},
	                          month{static_cast<unsigned>(values[0])},
	                          day{static_cast<unsigned>(values[1])}};
	if (year < 1 || !date.ok() || values[2] > 23 || values[3] > 59 ||
	    values[4] > 59) {
		return {};
	}
	std::string zone;
	if (at < raw.size()) {
		const auto sign = raw[at++];
		if (sign == 'Z') {
			zone = " UTC";
		} else if (sign == '+' || sign == '-') {
			const int hours = digits(2);
			if (at < raw.size() && raw[at] == '\'') {
				++at;
			}
			const int minutes = at == raw.size() ? 0 : digits(2);
			if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
				return {};
			}
			if (at < raw.size() && raw[at] == '\'') {
				++at;
			}
			zone = " UTC" + std::string(1, sign) + number(hours, 0);
			if (hours < 10) {
				zone.insert(zone.size() - 1, "0");
			}
			zone +=
			    ":" + std::string(minutes < 10 ? "0" : "") + number(minutes, 0);
		} else {
			return {};
		}
	}
	if (at != raw.size()) {
		return {};
	}
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2)
	    << values[0] << '-' << std::setw(2) << values[1] << "  " << std::setw(2)
	    << values[2] << ':' << std::setw(2) << values[3] << ':' << std::setw(2)
	    << values[4] << zone;
	return out.str();
}
std::string propertyFileSize(std::uint64_t bytes) {
	if (bytes < 1024) {
		return std::to_string(bytes) + (bytes == 1 ? " byte" : " bytes");
	}
	double size = static_cast<double>(bytes);
	const char* unit = "bytes";
	for (const auto suffix : {"KiB", "MiB", "GiB", "TiB", "PiB", "EiB"}) {
		size /= 1024;
		unit = suffix;
		if (size < 1024) {
			break;
		}
	}
	return number(size) + " " + unit + " (" + std::to_string(bytes) + " bytes)";
}
std::string propertyPageSize(double width, double height) {
	if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
	    height <= 0) {
		return {};
	}
	const auto shortSide = (std::min)(width, height);
	const auto longSide = (std::max)(width, height);
	std::string name;
	if (std::abs(shortSide - 595.28) < 2 && std::abs(longSide - 841.89) < 2) {
		name = "A4, ";
	} else if (std::abs(shortSide - 612) < 2 && std::abs(longSide - 792) < 2) {
		name = "Letter, ";
	} else if (std::abs(shortSide - 612) < 2 && std::abs(longSide - 1008) < 2) {
		name = "Legal, ";
	}
	return number(width * 25.4 / 72) + " x " + number(height * 25.4 / 72) +
	       " mm (" + name +
	       (width > height   ? "landscape"
	        : width < height ? "portrait"
	                         : "square") +
	       ")";
}
} // namespace helpers
