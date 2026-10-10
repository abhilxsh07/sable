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

#include <cassert>
#include <fstream>
#include <system_error>

#include <fpdf_doc.h>
#include <fpdfview.h>

#include "engine.h"

namespace pdf {
namespace {
// Every FPDF_* call has to happen on the PDFium thread. Off it, refuse
// instead of messing up PDFium's state, and stop right there in debug builds.
bool offThread() {
	if (Engine::onThread()) {
		return false;
	}
	assert(false && "pdf:: called off the PDFium thread");
	return true;
}

// FPDF_ERR_PASSWORD covers both "needs one" and "that wasn't it"
Error fromPdfium(unsigned long code, bool gavePassword) {
	switch (code) {
	case FPDF_ERR_FILE:
		return Error::File;
	case FPDF_ERR_FORMAT:
		return Error::Format;
	case FPDF_ERR_PASSWORD:
		return gavePassword ? Error::WrongPassword : Error::PasswordRequired;
	case FPDF_ERR_SECURITY:
		return Error::Security;
	default:
		return Error::Unknown;
	}
}

void appendUtf8(std::string& out, char32_t c) {
	if (c < 0x80) {
		out += static_cast<char>(c);
	} else if (c < 0x800) {
		out += static_cast<char>(0xC0 | (c >> 6));
		out += static_cast<char>(0x80 | (c & 0x3F));
	} else if (c < 0x10000) {
		out += static_cast<char>(0xE0 | (c >> 12));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (c & 0x3F));
	} else {
		out += static_cast<char>(0xF0 | (c >> 18));
		out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (c & 0x3F));
	}
}

// PDFium hands text out as UTF-16LE bytes
std::string utf16ToUtf8(const std::vector<unsigned char>& bytes) {
	std::string out;
	for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
		char32_t unit = static_cast<char32_t>(bytes[i] | (bytes[i + 1] << 8));
		if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size()) {
			const auto low =
			    static_cast<char32_t>(bytes[i + 2] | (bytes[i + 3] << 8));
			if (low >= 0xDC00 && low <= 0xDFFF) {
				unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
				i += 2;
			}
		}
		// a lone surrogate isn't a character
		if (unit >= 0xD800 && unit <= 0xDFFF) {
			unit = 0xFFFD;
		}
		appendUtf8(out, unit);
	}
	return out;
}
} // namespace

std::string_view errorMessage(Error error) {
	switch (error) {
	case Error::None:
		return "";
	case Error::File:
		return "Couldn't read the file.";
	case Error::Format:
		return "This isn't a PDF, or it's too damaged to open.";
	case Error::PasswordRequired:
		return "This PDF is protected with a password.";
	case Error::WrongPassword:
		return "That password didn't work.";
	case Error::Security:
		return "This PDF is protected in a way Sable can't open.";
	case Error::Unknown:
		break;
	}
	return "Something went wrong opening this PDF.";
}

OpenResult openDocument(std::vector<std::uint8_t> data,
                        const std::string& password) {
	if (offThread()) {
		return {nullptr, Error::Unknown};
	}
	// nothing to look at, PDFium would only say the same
	if (data.empty()) {
		return {nullptr, Error::Format};
	}
	FPDF_DOCUMENT handle =
	    FPDF_LoadMemDocument64(data.data(), data.size(),
	                           password.empty() ? nullptr : password.c_str());
	if (!handle) {
		return {nullptr, fromPdfium(FPDF_GetLastError(), !password.empty())};
	}
	// moving the vector keeps the same buffer, so PDFium's pointer stays good
	return {std::unique_ptr<Document>(new Document(std::move(data), handle)),
	        Error::None};
}

OpenResult openFile(const std::filesystem::path& path,
                    const std::string& password) {
	if (offThread()) {
		return {nullptr, Error::Unknown};
	}
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec) {
		return {nullptr, Error::File};
	}
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return {nullptr, Error::File};
	}
	std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
	in.read(reinterpret_cast<char*>(data.data()),
	        static_cast<std::streamsize>(data.size()));
	if (in.gcount() != static_cast<std::streamsize>(data.size())) {
		return {nullptr, Error::File};
	}
	return openDocument(std::move(data), password);
}

Document::Document(std::vector<std::uint8_t> data, fpdf_document_t__* handle)
    : m_data(std::move(data)), m_handle(handle) {}

Document::~Document() {
	// off the PDFium thread this leaks the document rather than closing it
	// somewhere it isn't allowed to
	if (m_handle && !offThread()) {
		FPDF_CloseDocument(m_handle);
	}
}

int Document::pageCount() const {
	if (offThread()) {
		return 0;
	}
	return FPDF_GetPageCount(m_handle);
}

std::optional<PageSize> Document::pageSize(int index) const {
	if (offThread() || index < 0 || index >= FPDF_GetPageCount(m_handle)) {
		return std::nullopt;
	}
	FS_SIZEF size{};
	if (!FPDF_GetPageSizeByIndexF(m_handle, index, &size)) {
		return std::nullopt;
	}
	return PageSize{size.width, size.height};
}

std::string Document::pageLabel(int index) const {
	if (offThread() || index < 0) {
		return {};
	}
	const unsigned long bytes = FPDF_GetPageLabel(m_handle, index, nullptr, 0);
	// 0 is no label at all, 2 is just the terminator
	if (bytes <= 2) {
		return {};
	}
	std::vector<unsigned char> buffer(bytes);
	if (FPDF_GetPageLabel(m_handle, index, buffer.data(), bytes) != bytes) {
		return {};
	}
	buffer.resize(bytes - 2);
	return utf16ToUtf8(buffer);
}
} // namespace pdf
