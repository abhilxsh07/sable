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
#include "document.h"
#include "engine.h"
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>

namespace pdf {
namespace {
bool sameSource(const std::filesystem::path& source,
                const std::filesystem::path& destination, std::error_code& ec) {
	namespace fs = std::filesystem;
	if (CompareStringOrdinal(source.c_str(), -1, destination.c_str(), -1,
	                         TRUE) == CSTR_EQUAL) {
		return true;
	}
	const bool inputExists = fs::exists(source, ec);
	if (ec) {
		return false;
	}
	const bool outputExists = fs::exists(destination, ec);
	if (ec) {
		return false;
	}
	return inputExists && outputExists &&
	       fs::equivalent(source, destination, ec);
}

struct Writer {
	HANDLE file = INVALID_HANDLE_VALUE;
	std::filesystem::path temporary;
	~Writer() {
		if (file != INVALID_HANDLE_VALUE) {
			CloseHandle(file);
		}
		if (!temporary.empty()) {
			DeleteFileW(temporary.c_str());
		}
	}
};
} // namespace

SaveResult Document::saveCopy(const std::filesystem::path& destination,
                              const std::filesystem::path& source,
                              const std::function<bool()>& canceled) const {
	assert(Engine::onThread());
	if (!Engine::onThread() || !m_handle || destination.empty() ||
	    source.empty()) {
		return SaveResult::Failed;
	}
	const auto stopped = [&] { return canceled && canceled(); };
	if (stopped()) {
		return SaveResult::Canceled;
	}
	namespace fs = std::filesystem;
	std::error_code ec;
	const auto input = fs::weakly_canonical(source, ec);
	if (ec) {
		return SaveResult::Failed;
	}
	const auto output = fs::weakly_canonical(destination, ec);
	if (ec || output.filename().empty()) {
		return SaveResult::Failed;
	}
	if (sameSource(input, output, ec)) {
		return SaveResult::Source;
	}
	if (ec) {
		return SaveResult::Failed;
	}
	static std::atomic<unsigned> counter{0};
	Writer writer;
	writer.temporary =
	    output.parent_path() /
	    (L".sable-copy-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
	     std::to_wstring(
	         std::chrono::steady_clock::now().time_since_epoch().count()) +
	     L"-" + std::to_wstring(counter++) + L".tmp");
	writer.file =
	    CreateFileW(writer.temporary.c_str(), GENERIC_WRITE, 0, nullptr,
	                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (writer.file == INVALID_HANDLE_VALUE) {
		// A failed CREATE_NEW must not remove someone else's file.
		writer.temporary.clear();
		return SaveResult::Failed;
	}
	// port of viewer.mjs downloadOrSave(): unchanged PDFs keep their bytes.
	// Rewriting through PDFium changes valid empty attachment streams.
	for (std::size_t offset = 0; offset < m_data.size();) {
		if (stopped()) {
			return SaveResult::Canceled;
		}
		const auto count = static_cast<DWORD>(
		    (std::min)(m_data.size() - offset, std::size_t{1024 * 1024}));
		DWORD written = 0;
		if (!WriteFile(writer.file, m_data.data() + offset, count, &written,
		               nullptr) ||
		    written != count) {
			return SaveResult::Failed;
		}
		offset += written;
	}
	if (!FlushFileBuffers(writer.file)) {
		return SaveResult::Failed;
	}
	const bool closed = CloseHandle(writer.file) != FALSE;
	writer.file = INVALID_HANDLE_VALUE;
	if (!closed) {
		return SaveResult::Failed;
	}
	if (stopped()) {
		return SaveResult::Canceled;
	}
	if (sameSource(input, output, ec)) {
		return SaveResult::Source;
	}
	if (ec) {
		return SaveResult::Failed;
	}
	if (!MoveFileExW(writer.temporary.c_str(), output.c_str(),
	                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		return SaveResult::Failed;
	}
	writer.temporary.clear();
	return SaveResult::Saved;
}
} // namespace pdf
