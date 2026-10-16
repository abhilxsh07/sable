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

#include <atomic>
#include <string>
#include <vector>

namespace diagnostics {
namespace {
std::atomic<std::shared_ptr<Log>> current;
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
		const auto file =
		    directory.empty()
		        ? std::filesystem::path{}
		        : directory /
		              (L"Sable-" + std::to_wstring(GetCurrentProcessId()) +
		               L".log");
		auto log = std::make_shared<Log>(file, debug);
		const bool ready = log->ready();
		current.store(std::move(log));
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
} // namespace diagnostics
