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
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace helpers {
struct CliArgs {
	// the pdfs to open, in order, with any file:// prefix already gone
	std::vector<std::string> files;
	// -p/--pages. Only set when there is at least one pdf and the value
	// is a whole number of 1 or more, same as app.ts's `if (p)` checks
	std::optional<int> page;
	bool showHelp = false;
	bool showVersion = false;
	// set for input we can't make sense of, e.g. "Unknown argument: x"
	std::string error;
};

// args are everything after the program name, as UTF-8
CliArgs parseCli(const std::vector<std::string>& args);

// the text yargs prints for --help, with $0 replaced by programName and
// "copyright <year>" as the epilog. No trailing newline, like yargs.
std::string helpText(const std::string& programName, int year);

// "file:///C:/x%20y.pdf" -> "C:/x y.pdf". Anything that isn't a file://
// url comes back untouched.
std::string fileUrlToPath(const std::string& arg);
} // namespace helpers
