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
// port of app/main/app.ts yargs config (L345-376) and startup args (L394-410)
// see:
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/main/app.ts#L345-L376
// the help layout follows help() and maxWidth() in yargs 17.7.2 (the version
// in package.json) and was checked against its real output
// see: https://raw.githubusercontent.com/yargs/yargs/v17.7.2/lib/usage.ts
#include "cli.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <system_error>

namespace helpers {
namespace {
// yargs wraps at the terminal width, but never wider than 80. Piped output
// has no width, so 80 it is. We always use 80.
const size_t WRAP = 80;

bool isInteger(const std::string& text) {
	size_t i = 0;
	if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
		i++;
	}
	if (i == text.size()) {
		return false;
	}
	for (; i < text.size(); i++) {
		if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
			return false;
		}
	}
	return true;
}

// yargs turns a -p value that isn't a number into NaN, doesn't complain, and
// app.ts then skips it because `if (p)` is false for NaN (and for 0). So a
// bad value just means "open on the first page". The same goes for a
// missing value. We take whole numbers only: no 2.5, 1e2 or 0x10, and
// nothing below 1 since there's no page 0 or -3 to open.
std::optional<int> parsePage(const std::string& text) {
	if (!isInteger(text)) {
		return std::nullopt;
	}
	const char* first = text.data();
	const char* last = text.data() + text.size();
	if (*first == '+') {
		first++;
	}
	int value = 0;
	const auto [end, ec] = std::from_chars(first, last, value);
	if (ec != std::errc() || end != last || value < 1) {
		return std::nullopt;
	}
	return value;
}

// the value after "-p" or "--pages". yargs takes the next argument as long
// as it isn't another option, so "-p a.pdf" eats the file name too.
bool looksLikeValue(const std::string& arg) {
	return arg.empty() || arg[0] != '-' || isInteger(arg);
}

int hexValue(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

bool isValidUtf8(const std::string& text) {
	size_t i = 0;
	while (i < text.size()) {
		const auto c = static_cast<unsigned char>(text[i]);
		size_t len = c < 0x80           ? 1
		             : (c >> 5) == 0x6  ? 2
		             : (c >> 4) == 0xE  ? 3
		             : (c >> 3) == 0x1E ? 4
		                                : 0;
		if (len == 0 || i + len > text.size()) {
			return false;
		}
		for (size_t k = 1; k < len; k++) {
			if ((static_cast<unsigned char>(text[i + k]) >> 6) != 0x2) {
				return false;
			}
		}
		i += len;
	}
	return true;
}

// a broken escape like "%zz" is left as it is. So is %00 (a NUL in the
// middle of a path) and anything that doesn't decode to valid UTF-8, since
// the app turns the path back into UTF-16 later.
std::string percentDecode(const std::string& text) {
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '%' && i + 2 < text.size() &&
		    hexValue(text[i + 1]) >= 0 && hexValue(text[i + 2]) >= 0 &&
		    hexValue(text[i + 1]) * 16 + hexValue(text[i + 2]) != 0) {
			out += static_cast<char>(hexValue(text[i + 1]) * 16 +
			                         hexValue(text[i + 2]));
			i += 2;
		} else {
			out += text[i];
		}
	}
	return isValidUtf8(out) ? out : text;
}

bool startsWithNoCase(const std::string& text, const std::string& prefix) {
	if (text.size() < prefix.size()) {
		return false;
	}
	for (size_t i = 0; i < prefix.size(); i++) {
		if (std::tolower(static_cast<unsigned char>(text[i])) !=
		    std::tolower(static_cast<unsigned char>(prefix[i]))) {
			return false;
		}
	}
	return true;
}

bool isDriveLetter(const std::string& text, size_t at) {
	return at + 1 < text.size() &&
	       std::isalpha(static_cast<unsigned char>(text[at])) &&
	       text[at + 1] == ':';
}

// width of a string in characters, so UTF-8 names don't skew the columns
size_t textWidth(const std::string& text) {
	size_t width = 0;
	for (const char c : text) {
		if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
			width++;
		}
	}
	return width;
}

struct HelpRow {
	std::string left;
	std::string describe;
	// "[boolean]" and friends, right aligned to the wrap width
	std::string type;
};

// one line of a yargs table: two spaces, the left column padded out to
// leftWidth, the description, then the type tag pushed against the right edge
void addRow(std::string& out, const HelpRow& row, size_t leftWidth) {
	std::string line = "  " + row.left;
	line.append(leftWidth - std::min(leftWidth, textWidth(line)), ' ');
	line += row.describe;
	if (!row.type.empty()) {
		const size_t used = textWidth(line) + row.type.size();
		line.append(used < WRAP ? WRAP - used : 1, ' ');
		line += row.type;
	}
	out += line + "\n";
}

// the widest left column text plus 4, which is what yargs' maxWidth() gives
// ui.span(). That already covers the 2 spaces of padding on the left.
size_t columnWidth(const std::vector<HelpRow>& rows) {
	size_t width = 0;
	for (const auto& row : rows) {
		width = std::max(width, textWidth(row.left));
	}
	return width + 4;
}
} // namespace

// port of app/main/app.ts yargs config (L345-376)
std::string helpText(const std::string& programName, int year) {
	// pdf is a positional and its own alias, hence "pdf, pdf". .array("pdf")
	// comes after the string type, so [array] wins.
	const std::vector<HelpRow> positionals = {
	    {"pdf, pdf", "The pdf to open", "[array]"},
	};
	// short switches come first and long-only ones are indented by the
	// width of "-x, " so the dashes line up. The order is the one yargs
	// really prints.
	const std::vector<HelpRow> options = {
	    {"    --help", "Show help.", "[boolean]"},
	    {"-p, --pages", "The page to open in the pdf", "[number]"},
	    {"    --version", "Show version number", "[boolean]"},
	};
	const std::vector<HelpRow> examples = {
	    {programName + " -p 5 pdf.pdf", "Loads pdf on the 5th page", ""},
	};

	std::string out = "Usage: " + programName + " [-p] <pdf>\n\n";
	out += "Positionals:\n";
	for (const auto& row : positionals) {
		addRow(out, row, columnWidth(positionals));
	}
	out += "\nOptions:\n";
	for (const auto& row : options) {
		addRow(out, row, columnWidth(options));
	}
	out += "\nExamples:\n";
	for (const auto& row : examples) {
		addRow(out, row, columnWidth(examples));
	}
	out += "\ncopyright " + std::to_string(year);
	return out;
}

// see:
// https://raw.githubusercontent.com/yargs/yargs/v17.7.2/lib/yargs-factory.ts
std::optional<std::string> cliText(const CliArgs& args,
                                   const std::string& programName,
                                   const std::string& version, int year) {
	if (args.showHelp) {
		return helpText(programName, year);
	}
	if (args.showVersion) {
		return version;
	}
	return std::nullopt;
}

// the bug in app.ts: `fileToOpen.replace("file://", "")` for a single path
// threw the result away, so file:// urls only worked in the multi-file case,
// and nothing ever decoded the %20s
std::string fileUrlToPath(const std::string& arg) {
	if (!startsWithNoCase(arg, "file://")) {
		return arg;
	}
	const std::string rest = arg.substr(7);
	std::string path;
	if (rest.empty() || rest[0] == '/') {
		// file:///C:/x is a drive path, file:///tmp/x keeps its slash
		path = isDriveLetter(rest, 1) ? rest.substr(1) : rest;
	} else {
		const size_t slash = rest.find('/');
		const std::string host = rest.substr(0, slash);
		const std::string tail =
		    slash == std::string::npos ? "" : rest.substr(slash);
		if (isDriveLetter(host, 0) && host.size() == 2) {
			// file://C:/x, not valid but the old replace() handled it
			path = rest;
		} else if (startsWithNoCase(host, "localhost") && host.size() == 9) {
			path = isDriveLetter(tail, 1) ? tail.substr(1) : tail;
		} else {
			// file://server/share/x.pdf is a network share
			path = "//" + rest;
		}
	}
	return percentDecode(path);
}

// port of app/main/app.ts startup args (L394-410)
CliArgs parseCli(const std::vector<std::string>& args) {
	CliArgs result;
	bool onlyFiles = false;
	for (size_t i = 0; i < args.size(); i++) {
		const std::string& arg = args[i];
		if (onlyFiles || arg.empty() || arg[0] != '-' || arg == "-" ||
		    isInteger(arg)) {
			result.files.push_back(fileUrlToPath(arg));
		} else if (arg == "--") {
			// everything after this is a file, even if it starts with a dash
			onlyFiles = true;
		} else if (arg == "--help") {
			result.showHelp = true;
		} else if (arg == "--version") {
			result.showVersion = true;
		} else if (arg == "-p" || arg == "--pages") {
			if (i + 1 < args.size() && looksLikeValue(args[i + 1])) {
				result.page = parsePage(args[++i]);
			} else {
				result.page = std::nullopt;
			}
		} else if (arg.rfind("--pages=", 0) == 0) {
			result.page = parsePage(arg.substr(8));
		} else if (arg.rfind("-p", 0) == 0 && arg.rfind("--", 0) != 0) {
			// -p=5 and -p5
			const size_t skip = arg[2] == '=' ? 3 : 2;
			result.page = parsePage(arg.substr(skip));
		} else {
			// unknown options are ignored, like yargs does. yargs would also
			// take the next argument as their value, so "--foo file.pdf"
			// lost the file in NightPDF. Here it doesn't.
			continue;
		}
	}
	// app.ts only looks at -p when there is a pdf to open
	if (result.files.empty()) {
		result.page = std::nullopt;
	}
	return result;
}
} // namespace helpers
