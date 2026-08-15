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
#include "read_aloud.h"

#include "store_file.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>

namespace helpers {
double nearestReadAloudRate(double rate) {
	if (!std::isfinite(rate)) {
		return 1;
	}
	double best = 1;
	for (const double offered : readAloudRates) {
		if (std::abs(offered - rate) < std::abs(best - rate)) {
			best = offered;
		}
	}
	return best;
}

ReadAloudChoice loadReadAloud(const std::filesystem::path& file) noexcept {
	ReadAloudChoice choice;
	if (file.empty()) {
		return choice;
	}
	try {
		const auto text = readStoreFile(file);
		if (!text) {
			return choice;
		}
		const auto json = nlohmann::json::parse(*text, nullptr, false);
		if (!json.is_object()) {
			return choice;
		}
		if (const auto voice = json.find("voice");
		    voice != json.end() && voice->is_string() &&
		    voice->get_ref<const std::string&>().size() <= 512) {
			choice.voice = voice->get<std::string>();
		}
		if (const auto rate = json.find("rate");
		    rate != json.end() && rate->is_number()) {
			choice.rate = nearestReadAloudRate(rate->get<double>());
		}
	} catch (...) {
		return {};
	}
	return choice;
}

bool saveReadAloud(const std::filesystem::path& file,
                   const ReadAloudChoice& choice) noexcept {
	if (file.empty()) {
		return false;
	}
	try {
		const nlohmann::json json{{"voice", choice.voice},
		                          {"rate", nearestReadAloudRate(choice.rate)}};
		return writeStoreFile(file, json.dump(1, '\t') + "\n");
	} catch (...) {
		return false;
	}
}

std::string speakableText(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	bool space = false;
	for (std::size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		const bool breakHere = c == '\n' || c == '\r';
		// "exam-\nple" reads as "example"
		if (c == '-' && i + 1 < text.size() &&
		    (text[i + 1] == '\n' || text[i + 1] == '\r') && i > 0 &&
		    text[i - 1] != ' ') {
			while (i + 1 < text.size() &&
			       (text[i + 1] == '\n' || text[i + 1] == '\r')) {
				++i;
			}
			continue;
		}
		if (breakHere || c == ' ' || c == '\t' || c == '\f') {
			space = !out.empty();
			continue;
		}
		if (space) {
			out.push_back(' ');
			space = false;
		}
		out.push_back(c);
	}
	return out;
}

std::wstring readAloudRateLabel(double rate) {
	wchar_t text[16]{};
	std::swprintf(text, 16, L"%gx", nearestReadAloudRate(rate));
	return text;
}
} // namespace helpers
