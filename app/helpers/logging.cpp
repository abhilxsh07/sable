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
#include "logging.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ShlObj.h>

// Only this translation unit includes spdlog. Header-only mode keeps wide
// filenames compatible with the installed library's narrow-filename build.
#define SPDLOG_USE_STD_FORMAT
#define SPDLOG_WCHAR_FILENAMES
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include <winrt/base.h>

namespace diagnostics {
namespace {
namespace fs = std::filesystem;
std::atomic<std::shared_ptr<Log>> current;
constexpr std::size_t keepLogSets = 10;

struct LogName {
	DWORD process = 0;
	bool backup = false;
};

LogName readLogName(std::wstring_view name) {
	constexpr std::wstring_view prefix = L"Sable-", extension = L".log";
	if (!name.starts_with(prefix) || !name.ends_with(extension)) {
		return {};
	}
	name.remove_prefix(prefix.size());
	name.remove_suffix(extension.size());
	const auto dot = name.find(L'.');
	if (dot != name.npos) {
		const auto backup = name.substr(dot + 1);
		if (backup.size() != 1 || backup.front() < L'1' ||
		    backup.front() > L'3') {
			return {};
		}
		name = name.substr(0, dot);
	}
	if (name.empty() || name.size() > 10 || name.front() == L'0') {
		return {};
	}
	std::uint64_t process = 0;
	for (const wchar_t digit : name) {
		if (digit < L'0' || digit > L'9') {
			return {};
		}
		process = process * 10 + static_cast<unsigned>(digit - L'0');
	}
	if (process > (std::numeric_limits<DWORD>::max)()) {
		return {};
	}
	return {static_cast<DWORD>(process), dot != name.npos};
}

bool processMayBeRunning(DWORD id) {
	// if access is denied, leave the logs alone.
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocess
	const std::unique_ptr<void, decltype(&CloseHandle)> process(
	    OpenProcess(SYNCHRONIZE, FALSE, id), CloseHandle);
	return process ? WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0
	               : GetLastError() != ERROR_INVALID_PARAMETER;
}

void pruneLogs(const fs::path& directory, DWORD currentProcess) noexcept {
	try {
		const auto root = GetFileAttributesW(directory.c_str());
		if (root == INVALID_FILE_ATTRIBUTES ||
		    (root & FILE_ATTRIBUTE_REPARSE_POINT)) {
			return;
		}
		struct LogSet {
			fs::file_time_type modified = fs::file_time_type::min();
			fs::path base;
			std::vector<fs::path> backups;
		};
		std::map<DWORD, LogSet> sets;
		for (const auto& entry : fs::directory_iterator(directory)) {
			const auto& path = entry.path();
			const auto [process, backup] =
			    readLogName(path.filename().native());
			if (!process || process == currentProcess) {
				continue;
			}
			const auto attrs = GetFileAttributesW(path.c_str());
			if (attrs == INVALID_FILE_ATTRIBUTES ||
			    (attrs &
			     (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
				continue;
			}
			std::error_code ec;
			const auto modified = entry.last_write_time(ec);
			if (ec) {
				continue;
			}
			auto& logs = sets[process];
			logs.modified = (std::max)(logs.modified, modified);
			if (backup) {
				logs.backups.push_back(path);
			} else {
				logs.base = path;
			}
		}
		std::vector<DWORD> newest;
		for (const auto& entry : sets) {
			newest.push_back(entry.first);
		}
		std::stable_sort(newest.begin(), newest.end(), [&](DWORD a, DWORD b) {
			return sets.at(a).modified > sets.at(b).modified;
		});
		// reserve one slot for this process, even if its pid was reused.
		for (std::size_t i = keepLogSets - 1; i < newest.size(); ++i) {
			if (processMayBeRunning(newest[i])) {
				continue;
			}
			const auto& logs = sets.at(newest[i]);
			if (!logs.base.empty() && !DeleteFileW(logs.base.c_str()) &&
			    GetLastError() != ERROR_FILE_NOT_FOUND) {
				continue;
			}
			for (const auto& backup : logs.backups) {
				DeleteFileW(backup.c_str());
			}
		}
	} catch (...) {
		OutputDebugStringW(L"Sable couldn't prune its old logs.\n");
	}
}

spdlog::level::level_enum severity(Level level) {
	switch (level) {
	case Level::Debug:
		return spdlog::level::debug;
	case Level::Info:
		return spdlog::level::info;
	case Level::Warning:
		return spdlog::level::warn;
	case Level::Error:
		return spdlog::level::err;
	}
	return spdlog::level::err;
}
} // namespace

Log::Log(const std::filesystem::path& file, bool debug, std::size_t bytes,
         std::size_t backups) noexcept {
	try {
		if (file.empty()) {
			return;
		}
		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
		    file.native(), bytes, backups));
#ifdef _DEBUG
		sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif
		m_logger = std::make_shared<spdlog::logger>("Sable", sinks.begin(),
		                                            sinks.end());
		m_logger->set_level(debug ? spdlog::level::debug : spdlog::level::info);
		m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
		m_logger->flush_on(spdlog::level::debug);
		m_logger->set_error_handler([](const std::string&) {
			OutputDebugStringW(L"Sable couldn't write its log.\n");
		});
	} catch (...) {
		m_logger.reset();
		OutputDebugStringW(
		    L"Sable couldn't open its log; continuing without file logging.\n");
	}
}

void Log::write(Level level, std::string_view message) const noexcept {
	try {
		if (m_logger) {
			m_logger->log(severity(level), "{}", message);
		}
#ifdef _DEBUG
		else {
			OutputDebugStringA((std::string(message) + "\n").c_str());
		}
#endif
	} catch (...) {
		OutputDebugStringW(L"Sable couldn't write its log.\n");
	}
}

bool environmentEnabled(const wchar_t* name) noexcept {
	return GetEnvironmentVariableW(name, nullptr, 0) > 1;
}

std::filesystem::path logDirectory() {
	PWSTR raw{};
	const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData,
	                                         KF_FLAG_DEFAULT, nullptr, &raw);
	const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(
	    raw, &CoTaskMemFree);
	return SUCCEEDED(result) ? std::filesystem::path(raw) / L"Sable" / L"logs"
	                         : std::filesystem::path{};
}

bool initialize(const std::filesystem::path& directory, bool debug) noexcept {
	try {
		const auto process = GetCurrentProcessId();
		const auto file =
		    directory.empty()
		        ? std::filesystem::path{}
		        : directory / (L"Sable-" + std::to_wstring(process) + L".log");
		auto log = std::make_shared<Log>(file, debug);
		const bool ready = log->ready();
		current.store(std::move(log));
		if (ready) {
			pruneLogs(directory, process);
		}
		return ready;
	} catch (...) {
		return false;
	}
}

void write(Level level, std::string_view message) noexcept {
	if (const auto log = current.load()) {
		log->write(level, message);
	}
}

void caught(Level level, std::string_view context) noexcept {
	try {
		write(level, std::string(context) + ": " +
		                 winrt::to_string(winrt::to_message()));
	} catch (...) {
		write(level, context);
	}
}
} // namespace diagnostics
