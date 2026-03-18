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

// windows.h without the extras, and without the min and max macros
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The folder the test exe is in, or empty when Windows can't say. I work
// files out from here and not from the working folder, so it doesn't matter
// where the tests are started.
inline std::filesystem::path exeFolder() {
	// a path can be longer than MAX_PATH, so the buffer doubles until the
	// whole of it fits
	std::wstring exe(MAX_PATH, L'\0');
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, exe.data(),
		                                        static_cast<DWORD>(exe.size()));
		if (length == 0) {
			return {};
		}
		if (length < exe.size()) {
			exe.resize(length);
			break;
		}
		exe.resize(exe.size() * 2);
	}
	return std::filesystem::path(exe).parent_path();
}

// Where a sample file from tests/fixtures ends up after a build: in a
// fixtures folder next to the test exe. Without the exe's folder this is a
// guess that fails to open.
inline std::filesystem::path fixturePath(std::string_view name) {
	return exeFolder() / "fixtures" / std::string(name);
}

// tests/golden/filter_expected.json isn't copied next to the exe. Both the
// working folder and the exe are in the repo or in <repo>/<Platform>/<Config>,
// so this walks up from each of them. Empty when neither gets there.
inline std::filesystem::path goldenPath() {
	namespace fs = std::filesystem;
	std::error_code ec;
	for (fs::path dir : {fs::current_path(ec), exeFolder()}) {
		for (int i = 0; i < 8 && !dir.empty(); i++) {
			for (const char* sub : {"tests/golden", "golden", "."}) {
				const fs::path candidate = dir / sub / "filter_expected.json";
				if (fs::is_regular_file(candidate, ec)) {
					return candidate;
				}
			}
			if (dir == dir.parent_path()) {
				break;
			}
			dir = dir.parent_path();
		}
	}
	return {};
}

// A new, empty folder under the system temp dir that nothing else is using.
// The caller removes it again.
inline std::filesystem::path makeTempDir(std::string_view tag) {
	// the process id and the clock tell runs apart, the counter tells calls
	// apart
	static std::atomic<int> counter{0};
	const auto stamp =
	    std::chrono::steady_clock::now().time_since_epoch().count();
	const auto dir = std::filesystem::temp_directory_path() /
	                 ("sable_" + std::string(tag) + "_" +
	                  std::to_string(GetCurrentProcessId()) + "_" +
	                  std::to_string(stamp) + "_" + std::to_string(counter++));
	std::filesystem::create_directories(dir);
	return dir;
}

inline std::string checkBoxPdf() {
	const std::string paint = "0 0 0 rg 0 0 50 20 re f";
	const std::vector<std::string> objects{
	    "<< /Type /Catalog /Pages 2 0 R /AcroForm 3 0 R >>",
	    "<< /Type /Pages /Kids [4 0 R] /Count 1 >>",
	    "<< /Fields [5 0 R] /NeedAppearances false >>",
	    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
	    "/Annots [5 0 R] >>",
	    "<< /Type /Annot /Subtype /Widget /FT /Btn /T (box) /F 4 "
	    "/V /Off /AS /Off /Rect [40 50 90 70] /P 4 0 R "
	    "/AP << /N << /Approved#20now 6 0 R /Off 7 0 R >> >> >>",
	    "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] /Length " +
	        std::to_string(paint.size()) + " >>\nstream\n" + paint +
	        "\nendstream",
	    "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] /Length 0 >>\n"
	    "stream\nendstream"};
	std::string bytes = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	for (const auto& object : objects) {
		offsets.push_back(bytes.size());
		bytes +=
		    std::to_string(offsets.size()) + " 0 obj\n" + object + "\nendobj\n";
	}
	const auto xref = bytes.size();
	bytes += "xref\n0 8\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		bytes += line;
	}
	return bytes + "trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n" +
	       std::to_string(xref) + "\n%%EOF\n";
}

// i keep both controls on one page and give the second a custom export.
inline std::string radioPdf() {
	const std::string paint = "0 0 0 rg 0 0 50 20 re f";
	const std::vector<std::string> objects{
	    "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R] "
	    "/NeedAppearances false >> >>",
	    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
	    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
	    "/Annots [5 0 R 6 0 R 7 0 R] >>",
	    "<< /FT /Btn /Ff 49152 /T (radio) /V /First "
	    "/Kids [6 0 R 7 0 R] >>",
	    "<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] "
	    "/Contents (Original) /P 3 0 R >>",
	    "<< /Type /Annot /Subtype /Widget /Parent 4 0 R /P 3 0 R "
	    "/Rect [40 50 90 70] /F 4 /AS /First "
	    "/AP << /N << /First 8 0 R /Off 9 0 R >> >> >>",
	    "<< /Type /Annot /Subtype /Widget /Parent 4 0 R /P 3 0 R "
	    "/Rect [40 80 90 100] /F 4 /AS /Off "
	    "/AP << /N << /Next#20choice 8 0 R /Off 9 0 R >> >> >>",
	    "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] /Length " +
	        std::to_string(paint.size()) + " >>\nstream\n" + paint +
	        "\nendstream",
	    "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] "
	    "/Length 0 >>\nstream\nendstream"};
	std::string data = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	for (const auto& object : objects) {
		offsets.push_back(data.size());
		data +=
		    std::to_string(offsets.size()) + " 0 obj\n" + object + "\nendobj\n";
	}
	const auto xref = data.size();
	data += "xref\n0 " + std::to_string(objects.size() + 1) +
	        "\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		data += line;
	}
	return data + "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
	       " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
}

inline std::string choicePdf(bool combo) {
	const std::vector<std::string> objects{
	    "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R] "
	    "/NeedAppearances false /DA (/Helv 10 Tf 0 g) "
	    "/DR << /Font << /Helv << /Type /Font /Subtype /Type1 "
	    "/BaseFont /Helvetica >> >> >> >> >>",
	    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
	    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
	    "/Annots [4 0 R] >>",
	    "<< /Type /Annot /Subtype /Widget /FT /Ch /T (choice) /F 4 /Ff " +
	        std::string(combo ? "131072" : "0") +
	        " /V (old) /I [0] /Opt [[(old) (First)] "
	        "[<FEFF00630061006600E9> (Same)] "
	        "[<FEFF00630061006600E9> (Same)]] "
	        "/Rect [40 50 190 130] /P 3 0 R >>"};
	std::string bytes = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	for (const auto& object : objects) {
		offsets.push_back(bytes.size());
		bytes +=
		    std::to_string(offsets.size()) + " 0 obj\n" + object + "\nendobj\n";
	}
	const auto xref = bytes.size();
	bytes += "xref\n0 5\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		bytes += line;
	}
	return bytes + "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" +
	       std::to_string(xref) + "\n%%EOF\n";
}

// i keep every supported field beside a note and a direct bookmark.
struct MixedFormOptions {
	bool explicitFont = true, multiline = false, comb = false,
	     inherited = false, inheritedCheckBox = false, noSpellCheck = false,
	     sortedChoices = false, listDefault = false;
	int textLimit = 32;
};
inline std::string mixedFormPdf(MixedFormOptions settings = {}) {
	const auto [explicitFont, multiline, comb, inherited, inheritedCheckBox,
	            noSpellCheck, sortedChoices, listDefault, textLimit] = settings;
	const int spellFlag = noSpellCheck ? 4194304 : 0;
	const int sortFlag = sortedChoices ? 524288 : 0;
	const auto parent = std::to_string(explicitFont ? 17 : 16) + " 0 R";
	const auto boxParent =
	    std::to_string((explicitFont ? 17 : 16) + (inherited ? 1 : 0)) + " 0 R";
	const std::string paint = "0 0 0 rg 0 0 50 20 re f";
	const std::string options = sortedChoices
	                                ? "/V (old) /I [0] /Opt [[(old) (Zulu)] "
	                                  "[<FEFF00630061006600E9> (Alpha)] "
	                                  "[<FEFF00630061006600E9> (Alpha)]] "
	                                : "/V (old) /I [0] /Opt [[(old) (First)] "
	                                  "[<FEFF00630061006600E9> (Same)] "
	                                  "[<FEFF00630061006600E9> (Same)]] ";
	std::vector<std::string> objects{
	    "<< /Type /Catalog /Pages 2 0 R /Outlines 14 0 R "
	    "/AcroForm << /Fields [" +
	        std::string(inherited ? parent : "4 0 R") + " " +
	        (inheritedCheckBox ? boxParent : "5 0 R") +
	        " 6 0 R 9 0 R 10 0 R] "
	        "/NeedAppearances false /DA (/Helv 10 Tf 0 g) "
	        "/DR << /Font << /Helv " +
	        std::string(explicitFont ? "16 0 R"
	                                 : "<< /Type /Font /Subtype /Type1 "
	                                   "/BaseFont /Helvetica >>") +
	        " >> >> >> >>",
	    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
	    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
	    "/Annots [11 0 R 4 0 R 5 0 R 7 0 R 8 0 R 9 0 R 10 0 R] >>",
	    inherited ? "<< /Type /Annot /Subtype /Widget /Parent " + parent +
	                    " /F 4 /Rect [40 700 " +
	                    (multiline ? "300 800" : "190 720") + "] /P 3 0 R >>"
	    : comb ? "<< /Type /Annot /Subtype /Widget /FT /Tx /T (text) "
	             "/V (AB) /Ff " +
	                 std::to_string(16777216 | spellFlag) + " /MaxLen " +
	                 std::to_string(textLimit) +
	                 " /BS << /W 1 /S /S >> /MK << /BC [0 0 0] >> "
	                 "/F 4 /Rect [40 700 190 720] /P 3 0 R >>"
	    : multiline
	        ? "<< /Type /Annot /Subtype /Widget /FT /Tx /T (text) "
	          "/V (first\\nsecond) /Ff " +
	              std::to_string(4096 | spellFlag) + " " +
	              (textLimit > 0 ? "/MaxLen " + std::to_string(textLimit) + " "
	                             : "") +
	              "/F 4 /Rect [40 700 300 800] /P 3 0 R >>"
	        : "<< /Type /Annot /Subtype /Widget /FT /Tx /T (text) "
	          "/V (initial) " +
	              std::string(noSpellCheck ? "/Ff 4194304 " : "") +
	              (noSpellCheck && textLimit > 0
	                   ? "/MaxLen " + std::to_string(textLimit) + " "
	                   : "") +
	              "/F 4 /Rect [40 700 190 720] /P 3 0 R >>",
	    inheritedCheckBox
	        ? "<< /Type /Annot /Subtype /Widget /Parent " + boxParent +
	              " /F 4 /AS /Off /Rect [40 650 90 670] /P 3 0 R "
	              "/AP << /N << /Approved#20now 12 0 R /Off 13 0 R >> >> >>"
	        : "<< /Type /Annot /Subtype /Widget /FT /Btn /T (box) /F 4 "
	          "/V /Off /AS /Off /Rect [40 650 90 670] /P 3 0 R "
	          "/AP << /N << /Approved#20now 12 0 R /Off 13 0 R >> >> >>",
	    "<< /FT /Btn /Ff 49152 /T (radio) /V /First "
	    "/Kids [7 0 R 8 0 R] >>",
	    "<< /Type /Annot /Subtype /Widget /Parent 6 0 R /P 3 0 R "
	    "/Rect [40 600 90 620] /F 4 /AS /First "
	    "/AP << /N << /First 12 0 R /Off 13 0 R >> >> >>",
	    "<< /Type /Annot /Subtype /Widget /Parent 6 0 R /P 3 0 R "
	    "/Rect [40 550 90 570] /F 4 /AS /Off "
	    "/AP << /N << /Next#20choice 12 0 R /Off 13 0 R >> >> >>",
	    "<< /Type /Annot /Subtype /Widget /FT /Ch /T (combo) /F 4 "
	    "/Ff " +
	        std::to_string(131072 | sortFlag) + " " + options +
	        "/Rect [40 480 190 510] /P 3 0 R >>",
	    "<< /Type /Annot /Subtype /Widget /FT /Ch /T (list) /F 4 " +
	        std::string(sortedChoices ? "/Ff 524288 " : "") +
	        (listDefault ? "/DV (old) " : "") + options +
	        "/Rect [40 350 190 450] /P 3 0 R >>",
	    "<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] "
	    "/Contents (Original) /P 3 0 R >>",
	    "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] /Length " +
	        std::to_string(paint.size()) + " >>\nstream\n" + paint +
	        "\nendstream",
	    "<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] "
	    "/Length 0 >>\nstream\nendstream",
	    "<< /Type /Outlines /First 15 0 R /Last 15 0 R /Count 1 >>",
	    "<< /Title (Fields) /Parent 14 0 R /Dest [3 0 R /Fit] >>"};
	if (explicitFont) {
		// i make the Latin encoding explicit for the saved appearances.
		objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
		                  "/Encoding /WinAnsiEncoding >>");
	}
	if (inherited) {
		objects.push_back(
		    "<< /FT /Tx /T (text) /Ff " +
		    std::to_string((multiline ? 4102 : 6) | spellFlag) +
		    (multiline ? " /V (first\\nsecond) " : " /V (initial) ") +
		    "/DV (default) /MaxLen " + std::to_string(textLimit) +
		    " /Kids [4 0 R] >>");
	}
	if (inheritedCheckBox) {
		objects.push_back("<< /FT /Btn /T (box) /Ff 6 /V /Other "
		                  "/Opt [(Approved now)] /Kids [5 0 R] >>");
	}
	std::string bytes = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	for (const auto& object : objects) {
		offsets.push_back(bytes.size());
		bytes +=
		    std::to_string(offsets.size()) + " 0 obj\n" + object + "\nendobj\n";
	}
	const auto xref = bytes.size();
	bytes += "xref\n0 " + std::to_string(objects.size() + 1) +
	         "\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		bytes += line;
	}
	return bytes + "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
	       " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
}
