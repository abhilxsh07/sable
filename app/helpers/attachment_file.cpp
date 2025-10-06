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
#include "attachment_file.h"
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <string_view>
namespace helpers {
std::string attachmentFilename(std::string name) {
	const auto slash = name.find_last_of("/\\");
	if (slash != std::string::npos) {
		name.erase(0, slash + 1);
	}
	for (auto& c : name) {
		if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F ||
		    std::string_view("<>:\"|?*").find(c) != std::string_view::npos) {
			c = '_';
		}
	}
	if (name.size() > 180) {
		std::size_t length = 180;
		while (length > 0 &&
		       (static_cast<unsigned char>(name[length]) & 0xC0) == 0x80) {
			--length;
		}
		name.resize(length);
	}
	while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
		name.pop_back();
	}
	if (name.empty()) {
		return "Attachment";
	}
	auto stem = name.substr(0, name.find('.'));
	while (!stem.empty() && stem.back() == ' ') {
		stem.pop_back();
	}
	std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) {
		return static_cast<char>(std::toupper(c));
	});
	// see: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
	const auto deviceDigit = stem.size() >= 3 ? stem.substr(3) : std::string{};
	const bool numberedDevice =
	    (stem.starts_with("COM") || stem.starts_with("LPT")) &&
	    ((deviceDigit.size() == 1 && deviceDigit[0] >= '1' &&
	      deviceDigit[0] <= '9') ||
	     deviceDigit == "\xC2\xB9" || deviceDigit == "\xC2\xB2" ||
	     deviceDigit == "\xC2\xB3");
	if (stem == "CONIN$" || stem == "CONOUT$" || stem == "CON" ||
	    stem == "PRN" || stem == "AUX" || stem == "NUL" || numberedDevice) {
		name.insert(name.begin(), '_');
	}
	return name;
}
bool saveAttachment(const std::filesystem::path& destination,
                    const std::filesystem::path& source,
                    const std::vector<std::uint8_t>& bytes) {
	namespace fs = std::filesystem;
	std::error_code ec;
	const auto input = fs::weakly_canonical(source, ec);
	if (ec) {
		return false;
	}
	const auto output = fs::weakly_canonical(destination, ec);
	if (ec || output.filename().empty() ||
	    CompareStringOrdinal(input.c_str(), -1, output.c_str(), -1, TRUE) ==
	        CSTR_EQUAL ||
	    bytes.size() > 128ULL * 1024 * 1024) {
		return false;
	}
	const bool exists = fs::exists(output, ec);
	if (ec) {
		return false;
	}
	const bool sourceExists = fs::exists(input, ec);
	if (ec ||
	    (exists && sourceExists && (fs::equivalent(input, output, ec) || ec))) {
		return false;
	}
	static std::atomic<unsigned> counter{0};
	auto temporary = output.parent_path() / L".sable-attachment";
	temporary +=
	    L".sable-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
	    std::to_wstring(
	        std::chrono::steady_clock::now().time_since_epoch().count()) +
	    L"-" + std::to_wstring(counter++) + L".tmp";
	const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
	                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD written = 0;
	bool saved = bytes.empty() || (WriteFile(file, bytes.data(),
	                                         static_cast<DWORD>(bytes.size()),
	                                         &written, nullptr) &&
	                               written == bytes.size());
	saved = saved && FlushFileBuffers(file);
	saved = CloseHandle(file) && saved;
	if (saved) {
		saved = MoveFileExW(temporary.c_str(), output.c_str(),
		                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	}
	if (!saved) {
		fs::remove(temporary, ec);
	}
	return saved;
}
} // namespace helpers
