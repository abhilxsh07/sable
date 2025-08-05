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
#include <filesystem>
#include <string>
#include <string_view>

// Where a sample file from tests/fixtures ends up after a build: in a
// fixtures folder next to the test exe. I work it out from the exe and not
// from the working folder, so it doesn't matter where the tests are started.
inline std::filesystem::path fixturePath(std::string_view name) {
	// a path can be longer than MAX_PATH, so the buffer doubles until the
	// whole of it fits
	std::wstring exe(MAX_PATH, L'\0');
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, exe.data(),
		                                        static_cast<DWORD>(exe.size()));
		if (length == 0) {
			// no idea where the exe is, so this is a guess that fails to open
			return std::filesystem::path("fixtures") / std::string(name);
		}
		if (length < exe.size()) {
			exe.resize(length);
			break;
		}
		exe.resize(exe.size() * 2);
	}
	return std::filesystem::path(exe).parent_path() / "fixtures" /
	       std::string(name);
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
