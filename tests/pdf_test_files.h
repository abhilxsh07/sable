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
