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

#include <filesystem>
#include <memory>
#include <string_view>

namespace spdlog {
class logger;
}
namespace diagnostics {
enum class Level { Debug, Info, Warning, Error };
// Immutable configuration; writes are safe from the UI and engine threads.
class Log {
  public:
	Log(const std::filesystem::path& file, bool debug,
	    std::size_t bytes = 2 * 1024 * 1024, std::size_t backups = 3) noexcept;
	bool ready() const noexcept {
		return static_cast<bool>(m_logger);
	}
	void write(Level level, std::string_view message) const noexcept;

  private:
	std::shared_ptr<spdlog::logger> m_logger;
};
// Same truthiness as Node's process.env: any nonempty value, including "0".
bool environmentEnabled(const wchar_t* name) noexcept;
std::filesystem::path logDirectory();
bool initialize(const std::filesystem::path& directory, bool debug) noexcept;
void write(Level level, std::string_view message) noexcept;
} // namespace diagnostics
