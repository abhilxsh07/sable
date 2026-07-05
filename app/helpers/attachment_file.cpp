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
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
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
namespace {
namespace fs = std::filesystem;
constexpr std::wstring_view tempPrefix = L".sable-save.sable-";

bool samePath(const fs::path& first, const fs::path& second) {
	return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) ==
	       CSTR_EQUAL;
}

DWORD temporaryOwner(std::wstring_view name) {
	if (!name.starts_with(tempPrefix) || !name.ends_with(L".tmp")) {
		return 0;
	}
	name.remove_prefix(tempPrefix.size());
	name.remove_suffix(4);
	const auto dash = name.find(L'-');
	if (dash == name.npos || dash == 0 || dash > 10) {
		return 0;
	}
	const auto pid = name.substr(0, dash);
	name.remove_prefix(dash + 1);
	const auto separator = name.find(L'-');
	const auto digits = [](std::wstring_view part) {
		return !part.empty() && std::ranges::all_of(part, [](wchar_t ch) {
			return ch >= L'0' && ch <= L'9';
		});
	};
	if (separator == name.npos || !digits(pid) ||
	    !digits(name.substr(0, separator)) ||
	    !digits(name.substr(separator + 1))) {
		return 0;
	}
	std::uint64_t owner = 0;
	for (const auto digit : pid) {
		owner = owner * 10 + static_cast<unsigned>(digit - L'0');
	}
	return owner <= (std::numeric_limits<DWORD>::max)()
	           ? static_cast<DWORD>(owner)
	           : 0;
}

void pruneSaveTemps(const fs::path& output, const fs::path& input) noexcept {
	try {
		// i can only check process ids on this machine. see:
		// https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdrivetypew
		std::error_code ec;
		const auto directory = fs::canonical(output.parent_path(), ec);
		if (ec) {
			return;
		}
		const auto drive = GetDriveTypeW(directory.root_path().c_str());
		if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE &&
		    drive != DRIVE_RAMDISK) {
			return;
		}
		fs::directory_iterator entry(output.parent_path(), ec);
		for (; !ec && entry != fs::directory_iterator{}; entry.increment(ec)) {
			const auto& path = entry->path();
			const DWORD owner = temporaryOwner(path.filename().native());
			if (!owner || owner == GetCurrentProcessId() ||
			    samePath(path, output) || samePath(path, input)) {
				continue;
			}
			const auto attrs = GetFileAttributesW(path.c_str());
			if (attrs == INVALID_FILE_ATTRIBUTES ||
			    (attrs &
			     (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
				continue;
			}
			const std::unique_ptr<void, decltype(&CloseHandle)> process(
			    OpenProcess(SYNCHRONIZE, FALSE, owner), CloseHandle);
			const bool exited =
			    process ? WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0
			            : GetLastError() == ERROR_INVALID_PARAMETER;
			if (exited) {
				DeleteFileW(path.c_str());
			}
		}
	} catch (...) {
		// a directory scan failure must not stop the save.
	}
}

bool replaceDestination(const fs::path& output, const fs::path& temporary) {
	std::error_code ec;
	const bool exists = fs::exists(output, ec);
	if (ec) {
		return false;
	}
	if (!exists) {
		return MoveFileExW(temporary.c_str(), output.c_str(),
		                   MOVEFILE_WRITE_THROUGH) != FALSE;
	}

	// keep the old file recoverable if the replacement only gets halfway.
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
	auto backup = temporary;
	backup += L".bak";
	if (fs::exists(backup, ec) || ec) {
		return false;
	}
	if (ReplaceFileW(output.c_str(), temporary.c_str(), backup.c_str(), 0,
	                 nullptr, nullptr)) {
		DeleteFileW(backup.c_str());
		return true;
	}
	if (GetLastError() == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
		MoveFileExW(backup.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH);
	}
	return false;
}

FileSaveResult writeFile(const std::filesystem::path& destination,
                         const std::filesystem::path* source,
                         std::span<const std::byte> bytes,
                         const std::function<bool()>& canceled) {
	const auto poll = [&]() -> std::optional<FileSaveResult> {
		try {
			if (canceled && canceled()) {
				return FileSaveResult::Canceled;
			}
		} catch (...) {
			return FileSaveResult::Failed;
		}
		return {};
	};
	if (destination.empty() || (source && source->empty())) {
		return FileSaveResult::Failed;
	}
	if (const auto stopped = poll()) {
		return *stopped;
	}
	std::error_code ec;
	const auto input = source ? fs::weakly_canonical(*source, ec) : fs::path{};
	if (ec) {
		return FileSaveResult::Failed;
	}
	const auto output = source ? fs::weakly_canonical(destination, ec)
	                           : fs::absolute(destination, ec);
	if (ec || output.filename().empty()) {
		return FileSaveResult::Failed;
	}
	const auto sameSource = [&] {
		if (!source) {
			return false;
		}
		if (samePath(input, output)) {
			return true;
		}
		const bool exists = fs::exists(output, ec);
		if (ec) {
			return false;
		}
		const bool sourceExists = fs::exists(input, ec);
		return !ec && exists && sourceExists &&
		       fs::equivalent(input, output, ec);
	};
	if (sameSource()) {
		return FileSaveResult::Source;
	}
	if (ec) {
		return FileSaveResult::Failed;
	}
	pruneSaveTemps(output, input);
	struct Writer {
		HANDLE file = INVALID_HANDLE_VALUE;
		fs::path temporary;
		~Writer() {
			if (file != INVALID_HANDLE_VALUE) {
				CloseHandle(file);
			}
			if (!temporary.empty()) {
				DeleteFileW(temporary.c_str());
			}
		}
	} writer;
	static std::atomic<unsigned> counter{0};
	writer.temporary = output.parent_path() / tempPrefix;
	writer.temporary +=
	    std::to_wstring(GetCurrentProcessId()) + L"-" +
	    std::to_wstring(
	        std::chrono::steady_clock::now().time_since_epoch().count()) +
	    L"-" + std::to_wstring(counter++) + L".tmp";
	writer.file =
	    CreateFileW(writer.temporary.c_str(), GENERIC_WRITE, 0, nullptr,
	                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (writer.file == INVALID_HANDLE_VALUE) {
		writer.temporary.clear();
		return FileSaveResult::Failed;
	}
	for (std::size_t offset = 0; offset < bytes.size();) {
		if (const auto stopped = poll()) {
			return *stopped;
		}
		const auto count = static_cast<DWORD>(
		    (std::min)(bytes.size() - offset, std::size_t{1024 * 1024}));
		DWORD written = 0;
		if (!WriteFile(writer.file, bytes.data() + offset, count, &written,
		               nullptr) ||
		    written != count) {
			return FileSaveResult::Failed;
		}
		offset += written;
	}
	if (!FlushFileBuffers(writer.file)) {
		return FileSaveResult::Failed;
	}
	const bool closed = CloseHandle(writer.file) != FALSE;
	writer.file = INVALID_HANDLE_VALUE;
	if (!closed) {
		return FileSaveResult::Failed;
	}
	if (const auto stopped = poll()) {
		return *stopped;
	}
	if (sameSource()) {
		return FileSaveResult::Source;
	}
	if (ec || !replaceDestination(output, writer.temporary)) {
		return FileSaveResult::Failed;
	}
	writer.temporary.clear();
	return FileSaveResult::Saved;
}
} // namespace

FileSaveResult saveFile(const std::filesystem::path& destination,
                        const std::filesystem::path& source,
                        const std::vector<std::uint8_t>& bytes,
                        const std::function<bool()>& canceled) {
	return writeFile(destination, &source, std::as_bytes(std::span(bytes)),
	                 canceled);
}

bool saveFile(const std::filesystem::path& destination, std::string_view text) {
	const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
	return writeFile(destination, nullptr, bytes, {}) == FileSaveResult::Saved;
}

bool saveAttachment(const std::filesystem::path& destination,
                    const std::filesystem::path& source,
                    const std::vector<std::uint8_t>& bytes) {
	return bytes.size() <= 128ULL * 1024 * 1024 &&
	       saveFile(destination, source, bytes) == FileSaveResult::Saved;
}
} // namespace helpers
