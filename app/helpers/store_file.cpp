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
#include "store_file.h"

#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <system_error>

#include "attachment_file.h"

namespace helpers {
namespace {
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;

std::wstring storeMutexName(const std::filesystem::path& path) {
	namespace fs = std::filesystem;
	std::error_code ec;
	const auto absolute = fs::absolute(path, ec);
	if (ec) {
		return {};
	}
	// resolve the directory without opening the file another writer replaces.
	auto key =
	    fs::weakly_canonical(absolute.parent_path(), ec) / absolute.filename();
	if (ec) {
		return {};
	}
	key.make_preferred();

	// use file system casing, independent of the user's locale.
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringex
	const int length =
	    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, key.c_str(), -1,
	                  nullptr, 0, nullptr, nullptr, 0);
	if (!length) {
		return {};
	}
	std::wstring folded(static_cast<std::size_t>(length), L'\0');
	if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, key.c_str(), -1,
	                   folded.data(), length, nullptr, nullptr, 0)) {
		return {};
	}
	// hash the path to keep the mutex name short and stable across processes.
	std::uint64_t hash = 14695981039346656037ULL;
	for (const wchar_t ch : folded) {
		hash ^= static_cast<std::uint16_t>(ch);
		hash *= 1099511628211ULL;
	}
	return L"Global\\Sable.Store." + std::to_wstring(hash);
}
} // namespace

std::optional<std::string> readStoreFile(const std::filesystem::path& path) {
	// distinguish a missing file from a read or sharing error.
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
	const auto file =
	    CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		const auto error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
			return std::string{};
		}
		return std::nullopt;
	}

	const Handle handle(file, CloseHandle);
	std::string text;
	char buffer[4096];
	for (;;) {
		DWORD count = 0;
		if (!ReadFile(handle.get(), buffer, sizeof(buffer), &count, nullptr)) {
			return std::nullopt;
		}
		if (count == 0) {
			return text;
		}
		text.append(buffer, count);
	}
}

bool writeStoreFile(const std::filesystem::path& path, std::string_view text) {
	std::error_code ec;
	if (path.has_parent_path()) {
		std::filesystem::create_directories(path.parent_path(), ec);
	}
	return !ec && saveFile(path, text);
}

bool withStoreLock(const std::filesystem::path& path,
                   const std::function<bool()>& update) {
	if (path.empty()) {
		return false;
	}
	const auto name = storeMutexName(path);
	if (name.empty()) {
		return false;
	}
	// same bounded wait as Night Run. a crashed writer leaves the old file.
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw
	const Handle mutex(CreateMutexW(nullptr, FALSE, name.c_str()), CloseHandle);
	if (!mutex) {
		return false;
	}
	const DWORD wait = WaitForSingleObject(mutex.get(), 250);
	if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
		return false;
	}
	struct Unlock {
		HANDLE mutex;
		~Unlock() {
			ReleaseMutex(mutex);
		}
	} unlock{mutex.get()};
	return update();
}
} // namespace helpers
