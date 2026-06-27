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
#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

#include "helpers/logging.h"
#include "pdf_test_files.h"

namespace {
struct Folder {
	std::filesystem::path path = makeTempDir("logging");
	~Folder() {
		std::error_code ignored;
		std::filesystem::remove_all(path, ignored);
	}
};
std::string read(const std::filesystem::path& file) {
	std::ifstream input(file, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), {}};
}
} // namespace

TEST(Logging, FiltersDebugAndWritesLiteralMessagesToUnicodePaths) {
	Folder folder;
	const auto file = folder.path / L"বাংলা" / L"reader.log";
	diagnostics::Log log(file, false);
	ASSERT_TRUE(log.ready());
	log.write(diagnostics::Level::Debug, "hidden debug marker");
	log.write(diagnostics::Level::Info, "literal {braces} 100%");
	log.write(diagnostics::Level::Warning, "warning marker");
	log.write(diagnostics::Level::Error, "error marker");
	const auto text = read(file);
	EXPECT_EQ(text.find("hidden debug marker"), std::string::npos);
	EXPECT_NE(text.find("[info]"), std::string::npos);
	EXPECT_NE(text.find("literal {braces} 100%"), std::string::npos);
	EXPECT_NE(text.find("warning marker"), std::string::npos);
	EXPECT_NE(text.find("error marker"), std::string::npos);
	const auto verbose = folder.path / "verbose.log";
	diagnostics::Log debug(verbose, true);
	ASSERT_TRUE(debug.ready());
	debug.write(diagnostics::Level::Debug, "visible debug marker");
	EXPECT_NE(read(verbose).find("visible debug marker"), std::string::npos);
}

TEST(Logging, RotatesWithABoundedBackupCount) {
	Folder folder;
	diagnostics::Log log(folder.path / "rotate.log", true, 256, 2);
	ASSERT_TRUE(log.ready());
	for (int i = 0; i < 50; ++i) {
		log.write(diagnostics::Level::Info, std::string(100, 'x'));
	}
	EXPECT_TRUE(std::filesystem::exists(folder.path / "rotate.log"));
	EXPECT_TRUE(std::filesystem::exists(folder.path / "rotate.1.log"));
	EXPECT_TRUE(std::filesystem::exists(folder.path / "rotate.2.log"));
	EXPECT_FALSE(std::filesystem::exists(folder.path / "rotate.3.log"));
}

TEST(Logging, StartupPrunesOldLogSetsAndKeepsLockedAndUnrelatedFiles) {
	namespace fs = std::filesystem;
	using namespace std::chrono;
	Folder folder;
	const auto old = fs::file_time_type::clock::now() - hours(24);
	const auto log = [&](DWORD process, std::wstring_view suffix) {
		return folder.path /
		       (L"Sable-" + std::to_wstring(process) + std::wstring(suffix));
	};
	const auto seed = [](const fs::path& file, fs::file_time_type modified) {
		std::ofstream(file) << "old log";
		fs::last_write_time(file, modified);
	};
	constexpr DWORD first = 4'294'967'200;
	for (DWORD i = 0; i < 12; ++i) {
		const auto modified = old + minutes(i);
		if (i != 0) {
			seed(log(first + i, L".log"), modified);
		}
		seed(log(first + i, L".1.log"), modified);
	}
	const auto other = log(first, L".extra.log");
	seed(other, old);
	const auto directory = log(first + 21, L".log");
	fs::create_directory(directory);

	const auto lockedPath = log(first + 20, L".log");
	const auto lockedBackup = log(first + 20, L".1.log");
	seed(lockedPath, old - hours(1));
	seed(lockedBackup, old - hours(1));
	diagnostics::Log locked(lockedPath, false);
	ASSERT_TRUE(locked.ready());
	const auto active = log(GetCurrentProcessId(), L".log");
	const auto activeBackup = log(GetCurrentProcessId(), L".1.log");
	seed(active, old - hours(1));
	seed(activeBackup, old - hours(1));
	struct ResetLog {
		~ResetLog() {
			diagnostics::initialize({}, false);
		}
	} reset;
	ASSERT_TRUE(diagnostics::initialize(folder.path, false));
	diagnostics::write(diagnostics::Level::Info, "started after pruning");

	for (DWORD i = 0; i < 12; ++i) {
		EXPECT_EQ(fs::exists(log(first + i, L".log")), i >= 3) << i;
		EXPECT_EQ(fs::exists(log(first + i, L".1.log")), i >= 3) << i;
	}
	EXPECT_TRUE(fs::exists(lockedPath));
	EXPECT_TRUE(fs::exists(lockedBackup));
	EXPECT_TRUE(fs::exists(activeBackup));
	EXPECT_NE(read(active).find("started after pruning"), std::string::npos);
	EXPECT_TRUE(fs::exists(other));
	EXPECT_TRUE(fs::is_directory(directory));
}

TEST(Logging, ConcurrentWritesRemainWholeAndBadPathsAreNonfatal) {
	Folder folder;
	const auto file = folder.path / "parallel.log";
	diagnostics::Log log(file, true);
	ASSERT_TRUE(log.ready());
	std::vector<std::thread> threads;
	for (int i = 0; i < 4; ++i) {
		threads.emplace_back([&log, i] {
			for (int j = 0; j < 30; ++j) {
				log.write(diagnostics::Level::Info,
				          "worker-" + std::to_string(i));
			}
		});
	}
	for (auto& thread : threads) {
		thread.join();
	}
	std::ifstream input(file);
	std::string line;
	int count = 0;
	while (std::getline(input, line)) {
		EXPECT_NE(line.find("worker-"), std::string::npos);
		++count;
	}
	EXPECT_EQ(count, 120);
	const auto blocker = folder.path / "file";
	std::ofstream(blocker) << "not a directory";
	diagnostics::Log failed(blocker / "reader.log", true);
	EXPECT_FALSE(failed.ready());
	EXPECT_NO_THROW(failed.write(diagnostics::Level::Error, "still running"));
}

TEST(Logging, EnvironmentUsesNonemptyValueSemantics) {
	constexpr auto name = L"SABLE_UNIT_ENV_ENABLED";
	// A test-only variable, never a real application setting.
	SetEnvironmentVariableW(name, nullptr);
	EXPECT_FALSE(diagnostics::environmentEnabled(name));
	SetEnvironmentVariableW(name, L"");
	EXPECT_FALSE(diagnostics::environmentEnabled(name));
	SetEnvironmentVariableW(name, L"0");
	EXPECT_TRUE(diagnostics::environmentEnabled(name));
	SetEnvironmentVariableW(name, L"true");
	EXPECT_TRUE(diagnostics::environmentEnabled(name));
	SetEnvironmentVariableW(name, nullptr);
}
