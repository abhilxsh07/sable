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
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <memory>
#include <set>
#include <system_error>
#include <stdexcept>

#include <fpdf_doc.h>
#include <fpdf_attachment.h>
#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include "engine.h"

namespace pdf {
namespace {
// the biggest bitmap render() makes: 16384 on a side, 256 MB in all
constexpr int MAX_SIDE = 16384;
constexpr long long MAX_PIXELS = 64LL * 1024 * 1024;

struct PageCloser {
	void operator()(fpdf_page_t__* page) const {
		FPDF_ClosePage(page);
	}
};
struct BitmapDestroyer {
	void operator()(fpdf_bitmap_t__* bitmap) const {
		FPDFBitmap_Destroy(bitmap);
	}
};
using PagePtr = std::unique_ptr<fpdf_page_t__, PageCloser>;
struct AnnotationCloser {
	void operator()(fpdf_annotation_t__* annotation) const {
		FPDFPage_CloseAnnot(annotation);
	}
};
using AnnotationPtr = std::unique_ptr<fpdf_annotation_t__, AnnotationCloser>;
using BitmapPtr = std::unique_ptr<fpdf_bitmap_t__, BitmapDestroyer>;
struct TextCloser {
	void operator()(fpdf_textpage_t__* text) const {
		FPDFText_ClosePage(text);
	}
};
using TextPtr = std::unique_ptr<fpdf_textpage_t__, TextCloser>;

std::optional<FPDF_ANNOTATION_SUBTYPE> markupSubtype(TextMarkup markup) {
	switch (markup) {
	case TextMarkup::Highlight:
		return FPDF_ANNOT_HIGHLIGHT;
	case TextMarkup::Underline:
		return FPDF_ANNOT_UNDERLINE;
	case TextMarkup::StrikeOut:
		return FPDF_ANNOT_STRIKEOUT;
	default:
		return {};
	}
}

// Every FPDF_* call has to happen on the PDFium thread. Off it, refuse
// instead of messing up PDFium's state, and stop right there in debug builds.
bool offThread() {
	if (Engine::onThread()) {
		return false;
	}
	assert(false && "pdf:: called off the PDFium thread");
	return true;
}

std::optional<std::vector<int>> annotationCounts(FPDF_DOCUMENT document) {
	if (offThread()) {
		return {};
	}
	const int pages = FPDF_GetPageCount(document);
	if (pages < 0) {
		return {};
	}
	std::vector<int> counts(pages);
	for (int index = 0; index < pages; ++index) {
		PagePtr page(FPDF_LoadPage(document, index));
		if (!page) {
			return {};
		}
		counts[index] = FPDFPage_GetAnnotCount(page.get());
		if (counts[index] < 0) {
			return {};
		}
	}
	return counts;
}

bool matchesAnnotationCounts(FPDF_DOCUMENT document,
                             const std::vector<int>& expected) {
	if (offThread() ||
	    FPDF_GetPageCount(document) != static_cast<int>(expected.size())) {
		return false;
	}
	for (int index = 0; index < static_cast<int>(expected.size()); ++index) {
		PagePtr page(FPDF_LoadPage(document, index));
		if (!page || FPDFPage_GetAnnotCount(page.get()) != expected[index]) {
			return false;
		}
	}
	return true;
}

bool uniqueMarkup(FPDF_DOCUMENT document, FPDF_PAGE own, int pageIndex,
                  int index, FPDF_ANNOTATION target) {
	if (offThread() || FPDFPage_GetAnnotIndex(own, target) != index) {
		return false;
	}
	// i compare dictionary identity, including independent annotation arrays.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/main/fpdfsdk/fpdf_annot.cpp
	for (int page = 0; page < FPDF_GetPageCount(document); ++page) {
		if (page == pageIndex) {
			continue;
		}
		PagePtr other(FPDF_LoadPage(document, page));
		if (!other || FPDFPage_GetAnnotIndex(other.get(), target) >= 0) {
			return false;
		}
	}
	const int count = FPDFPage_GetAnnotCount(own);
	for (int candidate = index + 1; candidate < count; ++candidate) {
		AnnotationPtr other(FPDFPage_GetAnnot(own, candidate));
		const int first = other ? FPDFPage_GetAnnotIndex(own, other.get()) : -1;
		if (first < 0 || first == index) {
			return false;
		}
	}
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

bool validNoteText(std::u16string_view text) {
	// i keep new fields within the snapshot reader's 64 KiB limit.
	if (text.size() > 32767) {
		return false;
	}
	for (std::size_t i = 0; i < text.size(); ++i) {
		const auto unit = text[i];
		if (unit == 0 || (unit >= 0xDC00 && unit <= 0xDFFF)) {
			return false;
		}
		if (unit >= 0xD800 && unit <= 0xDBFF) {
			if (++i == text.size() || text[i] < 0xDC00 || text[i] > 0xDFFF) {
				return false;
			}
		}
	}
	return true;
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
	try {
		return {
		    std::unique_ptr<Document>(new Document(std::move(data), handle)),
		    Error::None};
	} catch (...) {
		FPDF_CloseDocument(handle);
		throw;
	}
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
	if (!m_handle) {
		return;
	}
	// Off the PDFium thread this leaks the document rather than closing it
	// somewhere it isn't allowed to. Once the engine is gone (the app is
	// shutting down) that's expected, so only complain while it's running.
	if (Engine::onThread()) {
		FPDF_CloseDocument(m_handle);
	} else if (Engine::running()) {
		offThread();
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

std::optional<Bitmap> Document::render(int index, int width, int height,
                                       int rotation) const {
	if (offThread() || index < 0 || index >= FPDF_GetPageCount(m_handle)) {
		return std::nullopt;
	}
	if (width <= 0 || height <= 0 || width > MAX_SIDE || height > MAX_SIDE ||
	    static_cast<long long>(width) * height > MAX_PIXELS) {
		return std::nullopt;
	}
	if (rotation < 0 || rotation > 3) {
		return std::nullopt;
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return std::nullopt;
	}

	Bitmap out;
	out.width = width;
	out.height = height;
	out.stride = width * 4;
	out.pixels.resize(static_cast<std::size_t>(out.stride) *
	                  static_cast<std::size_t>(height));
	// PDFium draws straight into our buffer, no copy afterwards
	BitmapPtr bitmap(FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGRA,
	                                     out.pixels.data(), out.stride));
	if (!bitmap) {
		return std::nullopt;
	}
	// white paper underneath, like PDF.js
	FPDFBitmap_FillRect(bitmap.get(), 0, 0, width, height, 0xFFFFFFFF);
	// No FPDF_LCD_TEXT: sub-pixel text has coloured fringes, and they get
	// very visible once the night filter inverts or hue-rotates the page.
	FPDF_RenderPageBitmap(bitmap.get(), page.get(), 0, 0, width, height,
	                      rotation, FPDF_ANNOT);
	return out;
}

bool Document::canCopy() const {
	return !offThread() && (FPDF_GetDocPermissions(m_handle) & 0x10) != 0;
}

bool Document::canAnnotate() const {
	return !offThread() && (FPDF_GetDocPermissions(m_handle) & 0x20) != 0;
}

std::optional<int> Document::addTextNote(int index, float x, float y,
                                         std::u16string_view contents,
                                         std::u16string_view author) {
	if (offThread() || index < 0 || index >= pageCount() || !canAnnotate() ||
	    contents.empty() || !validNoteText(contents) ||
	    !validNoteText(author) || !std::isfinite(x) || !std::isfinite(y)) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	FS_RECTF box{};
	FS_RECTF rect{x, y + 20, x + 20, y};
	if (!page || !FPDF_GetPageBoundingBox(page.get(), &box) ||
	    !std::isfinite(box.left) || !std::isfinite(box.right) ||
	    !std::isfinite(box.bottom) || !std::isfinite(box.top) ||
	    !std::isfinite(rect.right) || !std::isfinite(rect.top) ||
	    rect.left >= rect.right || rect.bottom >= rect.top ||
	    rect.left < box.left || rect.bottom < box.bottom ||
	    rect.right > box.right || rect.top > box.top) {
		return {};
	}
	const int count = FPDFPage_GetAnnotCount(page.get());
	if (count < 0 || count >= 4096) {
		return {};
	}
	auto expected = annotationCounts(m_handle);
	if (!expected || (*expected)[index] != count) {
		return {};
	}
	++(*expected)[index];
	// i allocate terminated strings before changing the document.
	const std::u16string text(contents), name(author);
	const bool wasModified = m_modified;
	AnnotationPtr note(FPDFPage_CreateAnnot(page.get(), FPDF_ANNOT_TEXT));
	if (!note) {
		return {};
	}
	m_modified = true;
	// PDFium generates a 20-point Text icon from the rectangle's lower left.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/63155a9b3873593a0f0d06bede4046ba7914b3ac/core/fpdfdoc/cpdf_generateap.cpp
	const bool complete =
	    FPDFAnnot_SetRect(note.get(), &rect) &&
	    FPDFAnnot_SetStringValue(
	        note.get(), "Contents",
	        reinterpret_cast<FPDF_WIDESTRING>(text.c_str())) &&
	    FPDFAnnot_SetStringValue(
	        note.get(), "T", reinterpret_cast<FPDF_WIDESTRING>(name.c_str())) &&
	    FPDFAnnot_SetFlags(note.get(), FPDF_ANNOT_FLAG_PRINT) &&
	    matchesAnnotationCounts(m_handle, *expected);
	note.reset();
	if (!complete) {
		// i remove only the appended note if a setter failed.
		if (!FPDFPage_RemoveAnnot(page.get(), count)) {
			throw std::runtime_error("Couldn't roll back the new note.");
		}
		m_modified = wasModified;
		return {};
	}
	return count;
}

std::optional<std::u16string> Document::textNote(int index,
                                                 int noteIndex) const {
	if (offThread() || index < 0 || index >= pageCount() || noteIndex < 0 ||
	    !canAnnotate() || !canSerializeEdits({})) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page || noteIndex >= FPDFPage_GetAnnotCount(page.get())) {
		return {};
	}
	AnnotationPtr note(FPDFPage_GetAnnot(page.get(), noteIndex));
	// Locked protects properties; LockedContents also protects the text.
	// see:
	// https://opensource.adobe.com/dc-acrobat-sdk-docs/pdflsdk/apireference/PD_Layer/PDAnnot.html
	constexpr int LOCKED_CONTENTS = 0x200;
	if (!note || FPDFAnnot_GetSubtype(note.get()) != FPDF_ANNOT_TEXT ||
	    (FPDFAnnot_GetFlags(note.get()) &
	     (FPDF_ANNOT_FLAG_READONLY | LOCKED_CONTENTS)) != 0 ||
	    FPDFAnnot_HasKey(note.get(), "RC") ||
	    FPDFAnnot_GetValueType(note.get(), "Contents") != FPDF_OBJECT_STRING) {
		return {};
	}
	const auto bytes =
	    FPDFAnnot_GetStringValue(note.get(), "Contents", nullptr, 0);
	if (bytes < 2 || bytes > 65536 || bytes % 2 != 0) {
		return {};
	}
	std::vector<FPDF_WCHAR> buffer(bytes / 2);
	if (FPDFAnnot_GetStringValue(note.get(), "Contents", buffer.data(),
	                             bytes) != bytes ||
	    buffer.back() != 0) {
		return {};
	}
	std::u16string contents(buffer.begin(), buffer.end() - 1);
	return validNoteText(contents) ? std::optional(std::move(contents))
	                               : std::nullopt;
}

std::optional<bool> Document::updateTextNote(int pageIndex, int noteIndex,
                                             std::u16string_view expected,
                                             std::u16string_view contents) {
	if (offThread() || contents.empty() || !validNoteText(contents) ||
	    !validNoteText(expected)) {
		return {};
	}
	const auto previous = textNote(pageIndex, noteIndex);
	if (!previous || *previous != expected) {
		return {};
	}
	if (*previous == contents) {
		return false;
	}
	// i allocate the replacement before touching the existing dictionary.
	const std::u16string replacement(contents);
	PagePtr page(FPDF_LoadPage(m_handle, pageIndex));
	AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), noteIndex)
	                        : nullptr);
	if (!note) {
		return {};
	}
	const bool wasModified = m_modified;
	m_modified = true;
	if (!FPDFAnnot_SetStringValue(
	        note.get(), "Contents",
	        reinterpret_cast<FPDF_WIDESTRING>(replacement.c_str()))) {
		if (!FPDFAnnot_SetStringValue(
		        note.get(), "Contents",
		        reinterpret_cast<FPDF_WIDESTRING>(previous->c_str()))) {
			throw std::runtime_error("Couldn't restore the note contents.");
		}
		m_modified = wasModified;
		return {};
	}
	return true;
}

std::optional<int> Document::highlightText(TextSelection selection) {
	return markText(selection, TextMarkup::Highlight);
}

std::optional<CommentContents>
Document::markupComment(int pageIndex, int index, TextMarkup markup) const {
	const auto subtype = markupSubtype(markup);
	if (offThread() || !subtype || pageIndex < 0 || pageIndex >= pageCount() ||
	    index < 0 || !canAnnotate() || !canSerializeEdits({})) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, pageIndex));
	AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), index) : nullptr);
	constexpr int LOCKED_CONTENTS = 0x200;
	if (!note || FPDFAnnot_GetSubtype(note.get()) != *subtype ||
	    (FPDFAnnot_GetFlags(note.get()) &
	     (FPDF_ANNOT_FLAG_READONLY | LOCKED_CONTENTS)) ||
	    FPDFAnnot_HasKey(note.get(), "RC") ||
	    !uniqueMarkup(m_handle, page.get(), pageIndex, index, note.get())) {
		return {};
	}
	CommentContents result;
	result.present = FPDFAnnot_HasKey(note.get(), "Contents") != 0;
	if (!result.present) {
		return result;
	}
	if (FPDFAnnot_GetValueType(note.get(), "Contents") != FPDF_OBJECT_STRING) {
		return {};
	}
	const auto bytes =
	    FPDFAnnot_GetStringValue(note.get(), "Contents", nullptr, 0);
	if (bytes < 2 || bytes > 65536 || bytes % 2 != 0) {
		return {};
	}
	std::vector<FPDF_WCHAR> buffer(bytes / 2);
	if (FPDFAnnot_GetStringValue(note.get(), "Contents", buffer.data(),
	                             bytes) != bytes ||
	    buffer.back() != 0) {
		return {};
	}
	result.text.assign(buffer.begin(), buffer.end() - 1);
	return validNoteText(result.text) ? std::optional(std::move(result))
	                                  : std::nullopt;
}

std::optional<bool>
Document::updateMarkupComment(int page, int index, TextMarkup markup,
                              const CommentContents& expected,
                              std::u16string_view contents) {
	if (offThread() || contents.empty() || !validNoteText(contents) ||
	    !validNoteText(expected.text)) {
		return {};
	}
	const auto previous = markupComment(page, index, markup);
	if (!previous || *previous != expected) {
		return {};
	}
	if (previous->text == contents) {
		return false;
	}
	const std::u16string replacement(contents);
	PagePtr target(FPDF_LoadPage(m_handle, page));
	AnnotationPtr note(target ? FPDFPage_GetAnnot(target.get(), index)
	                          : nullptr);
	if (!note) {
		return {};
	}
	const bool wasModified = m_modified;
	m_modified = true;
	// i change Contents alone, leaving the quads and appearance untouched.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/bada86efe77a43a6aabd428082eebc29d5dc77ba/fpdfsdk/fpdf_annot.cpp
	if (!FPDFAnnot_SetStringValue(
	        note.get(), "Contents",
	        reinterpret_cast<FPDF_WIDESTRING>(replacement.c_str()))) {
		if (!previous->present ||
		    !FPDFAnnot_SetStringValue(
		        note.get(), "Contents",
		        reinterpret_cast<FPDF_WIDESTRING>(previous->text.c_str()))) {
			throw std::runtime_error("Couldn't restore the markup comment.");
		}
		m_modified = wasModified;
		return {};
	}
	return true;
}

std::optional<int> Document::markText(TextSelection selection,
                                      TextMarkup markup) {
	if (offThread() || !canAnnotate()) {
		return {};
	}
	const auto subtype = markupSubtype(markup);
	if (!subtype) {
		return {};
	}
	const auto [first, last] = ordered(selection);
	if (first.page < 0 || first.index < 0 || last.index < 0 ||
	    last.page >= pageCount() || last.page - first.page >= 128 ||
	    !canSerializeEdits({})) {
		return {};
	}
	struct Target {
		PagePtr page;
		int pageIndex = 0, index = 0;
		std::vector<FS_QUADPOINTSF> quads;
		FS_RECTF bounds{};
	};
	std::vector<Target> targets;
	targets.reserve(last.page - first.page + 1);
	std::size_t totalQuads = 0;
	int totalChars = 0;
	for (int index = first.page; index <= last.page; ++index) {
		Target target;
		target.pageIndex = index;
		target.page.reset(FPDF_LoadPage(m_handle, index));
		if (!target.page) {
			return {};
		}
		TextPtr text(FPDFText_LoadPage(target.page.get()));
		if (!text) {
			return {};
		}
		const int chars = FPDFText_CountChars(text.get());
		if (chars < 0 || chars > 200'000 ||
		    (index == first.page && first.index > chars)) {
			return {};
		}
		const auto [begin, end] = textSpan(selection, index, chars);
		totalChars += end - begin;
		if (totalChars > 1'000'000) {
			return {};
		}
		if (begin == end) {
			continue;
		}
		FS_RECTF box{};
		target.index = FPDFPage_GetAnnotCount(target.page.get());
		if (target.index < 0 || target.index >= 4096 ||
		    !FPDF_GetPageBoundingBox(target.page.get(), &box) ||
		    !std::isfinite(box.left) || !std::isfinite(box.right) ||
		    !std::isfinite(box.top) || !std::isfinite(box.bottom) ||
		    box.left >= box.right || box.bottom >= box.top) {
			return {};
		}
		const int count = FPDFText_CountRects(text.get(), begin, end - begin);
		if (count < 0 || static_cast<std::size_t>(count) > 4096 - totalQuads) {
			return {};
		}
		target.quads.reserve(count);
		for (int rect = 0; rect < count; ++rect) {
			double left = 0, top = 0, right = 0, bottom = 0;
			if (!FPDFText_GetRect(text.get(), rect, &left, &top, &right,
			                      &bottom) ||
			    !std::isfinite(left) || !std::isfinite(top) ||
			    !std::isfinite(right) || !std::isfinite(bottom)) {
				return {};
			}
			FS_RECTF clipped{
			    static_cast<float>(
			        std::clamp(left, double(box.left), double(box.right))),
			    static_cast<float>(
			        std::clamp(top, double(box.bottom), double(box.top))),
			    static_cast<float>(
			        std::clamp(right, double(box.left), double(box.right))),
			    static_cast<float>(
			        std::clamp(bottom, double(box.bottom), double(box.top)))};
			if (clipped.left >= clipped.right ||
			    clipped.bottom >= clipped.top) {
				continue;
			}
			if (target.quads.empty()) {
				target.bounds = clipped;
			} else {
				target.bounds.left =
				    (std::min)(target.bounds.left, clipped.left);
				target.bounds.right =
				    (std::max)(target.bounds.right, clipped.right);
				target.bounds.bottom =
				    (std::min)(target.bounds.bottom, clipped.bottom);
				target.bounds.top = (std::max)(target.bounds.top, clipped.top);
			}
			target.quads.push_back({clipped.left, clipped.top, clipped.right,
			                        clipped.top, clipped.left, clipped.bottom,
			                        clipped.right, clipped.bottom});
		}
		totalQuads += target.quads.size();
		if (!target.quads.empty()) {
			targets.push_back(std::move(target));
		}
	}
	if (targets.empty()) {
		return {};
	}
	auto expected = annotationCounts(m_handle);
	if (!expected) {
		return {};
	}
	for (const auto& target : targets) {
		++(*expected)[target.pageIndex];
	}
	const bool wasModified = m_modified;
	std::size_t added = 0;
	const auto rollback = [&]() -> std::optional<int> {
		bool restored = true;
		while (added > 0) {
			const auto& previous = targets[--added];
			if (!FPDFPage_RemoveAnnot(previous.page.get(), previous.index)) {
				restored = false;
			}
		}
		if (!restored) {
			throw std::runtime_error("Couldn't roll back the text markup.");
		}
		m_modified = wasModified;
		return {};
	};
	for (auto& target : targets) {
		target.index = FPDFPage_GetAnnotCount(target.page.get());
		AnnotationPtr note(
		    target.index >= 0 && target.index < 4096
		        ? FPDFPage_CreateAnnot(target.page.get(), *subtype)
		        : nullptr);
		bool complete = note != nullptr;
		if (note) {
			++added;
			m_modified = true;
			complete = FPDFAnnot_SetRect(note.get(), &target.bounds) &&
			           FPDFAnnot_SetFlags(note.get(), FPDF_ANNOT_FLAG_PRINT);
			for (const auto& quad : target.quads) {
				if (!complete ||
				    !FPDFAnnot_AppendAttachmentPoints(note.get(), &quad)) {
					complete = false;
					break;
				}
			}
		}
		if (!complete) {
			note.reset();
			return rollback();
		}
	}
	if (!matchesAnnotationCounts(m_handle, *expected)) {
		return rollback();
	}
	// i use PDFium's default markup appearances.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/63155a9b3873593a0f0d06bede4046ba7914b3ac/core/fpdfdoc/cpdf_generateap.cpp
	return static_cast<int>(added);
}

std::optional<int> Document::addCenteredTextNote(int index,
                                                 std::u16string_view contents,
                                                 std::u16string_view author) {
	if (offThread() || index < 0 || index >= pageCount() || !canAnnotate() ||
	    !canSerializeEdits({})) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	FS_RECTF box{};
	if (!page || !FPDF_GetPageBoundingBox(page.get(), &box)) {
		return {};
	}
	const float x = static_cast<float>((double(box.left) + box.right) / 2 - 10);
	const float y = static_cast<float>((double(box.bottom) + box.top) / 2 - 10);
	page.reset();
	return addTextNote(index, x, y, contents, author);
}

std::optional<int> Document::addTextNoteAt(int index, double x, double y,
                                           int rotation,
                                           std::u16string_view contents,
                                           std::u16string_view author) {
	if (offThread() || index < 0 || index >= pageCount() || !std::isfinite(x) ||
	    !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1 || rotation < 0 ||
	    rotation > 3 || !canAnnotate() || !canSerializeEdits({})) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	FS_RECTF box{};
	double px = 0, py = 0;
	constexpr int GRID = 1 << 20;
	if (!page || !FPDF_GetPageBoundingBox(page.get(), &box) ||
	    !std::isfinite(box.left) || !std::isfinite(box.right) ||
	    !std::isfinite(box.bottom) || !std::isfinite(box.top) ||
	    double(box.right) - box.left < 20 ||
	    double(box.top) - box.bottom < 20 ||
	    !FPDF_DeviceToPage(page.get(), 0, 0, GRID, GRID, rotation,
	                       static_cast<int>(std::lround(x * GRID)),
	                       static_cast<int>(std::lround(y * GRID)), &px, &py) ||
	    !std::isfinite(px) || !std::isfinite(py)) {
		return {};
	}
	// i keep the full icon inside the visible page, including edge clicks.
	const float left = static_cast<float>(
	    std::clamp(px - 10, double(box.left), double(box.right) - 20));
	const float bottom = static_cast<float>(
	    std::clamp(py - 10, double(box.bottom), double(box.top) - 20));
	page.reset();
	return addTextNote(index, left, bottom, contents, author);
}

bool Document::canSerializeEdits(const std::function<bool()>& canceled) const {
	// @todo: preserve empty attachment streams before allowing rewrites.
	if (offThread() || FPDFDoc_GetAttachmentCount(m_handle) != 0) {
		return false;
	}
	for (int index = 0; index < pageCount(); ++index) {
		if ((canceled && canceled())) {
			return false;
		}
		PagePtr page(FPDF_LoadPage(m_handle, index));
		const int count = page ? FPDFPage_GetAnnotCount(page.get()) : -1;
		if (count < 0 || count > 4096) {
			return false;
		}
		for (int i = 0; i < count; ++i) {
			if ((canceled && canceled())) {
				return false;
			}
			AnnotationPtr note(FPDFPage_GetAnnot(page.get(), i));
			if (!note ||
			    FPDFAnnot_GetSubtype(note.get()) == FPDF_ANNOT_FILEATTACHMENT) {
				return false;
			}
		}
	}
	return true;
}

SaveResult Document::saveEdits(const std::filesystem::path& destination,
                               const std::filesystem::path& source,
                               std::size_t limit,
                               const std::function<bool()>& canceled) const {
	bool wasCanceled = false;
	const auto stopped = [&] {
		wasCanceled = wasCanceled || (canceled && canceled());
		return wasCanceled;
	};
	const auto bytes = serialize(limit, stopped);
	if (!bytes) {
		return wasCanceled ? SaveResult::Canceled : SaveResult::Failed;
	}
	return helpers::saveFile(destination, source, *bytes, stopped);
}

bool Document::modified() const {
	return !offThread() && m_modified;
}

std::optional<std::vector<std::uint8_t>>
Document::serialize(std::size_t limit,
                    const std::function<bool()>& canceled) const {
	if (offThread()) {
		return {};
	}
	struct Writer : FPDF_FILEWRITE {
		Writer(std::size_t bound, const std::function<bool()>& cancel)
		    : FPDF_FILEWRITE{1, &write}, limit(bound), canceled(cancel) {}
		bool stopped() noexcept {
			try {
				failed = failed || (canceled && canceled());
			} catch (...) {
				failed = true;
			}
			return failed;
		}
		static int write(FPDF_FILEWRITE* base, const void* data,
		                 unsigned long size) noexcept {
			auto& self = *static_cast<Writer*>(base);
			if (self.stopped()) {
				return 0;
			}
			try {
				if (size > self.limit - self.bytes.size() || (size && !data)) {
					self.failed = true;
					return 0;
				}
				if (size) {
					const auto first = static_cast<const std::uint8_t*>(data);
					self.bytes.insert(self.bytes.end(), first, first + size);
				}
				return 1;
			} catch (...) {
				self.failed = true;
				return 0;
			}
		}
		std::size_t limit;
		const std::function<bool()>& canceled;
		std::vector<std::uint8_t> bytes;
		bool failed = false;
	} writer(limit, canceled);
	if (writer.stopped()) {
		return {};
	}
	if (!m_modified) {
		if (m_data.size() > limit) {
			return {};
		}
		writer.bytes = m_data;
	} else {
		if (!canSerializeEdits([&writer] { return writer.stopped(); })) {
			return {};
		}
		if (!FPDF_SaveAsCopy(m_handle, &writer, 0)) {
			return {};
		}
	}
	// i keep callback failure sticky, including PDFium's final flush.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/main/core/fpdfapi/edit/cpdf_creator.cpp
	if (writer.stopped()) {
		return {};
	}
	return std::move(writer.bytes);
}

std::optional<FindPage>
Document::findPage(int index, const std::u16string& query, FindOptions options,
                   std::size_t limit,
                   const std::function<bool()>& canceled) const {
	if (offThread() || index < 0 || index >= pageCount() ||
	    query.size() > 4096 || query.find(u'\0') != std::u16string::npos) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return {};
	}
	TextPtr text(FPDFText_LoadPage(page.get()));
	if (!text) {
		return {};
	}
	const int count = FPDFText_CountChars(text.get());
	if (count < 0 || count > 1'000'000) {
		return {};
	}
	std::u32string characters;
	characters.reserve(count);
	for (int i = 0; i < count; ++i) {
		if (i % 1024 == 0 && canceled && canceled()) {
			return FindPage{};
		}
		characters += static_cast<char32_t>(FPDFText_GetUnicode(text.get(), i));
	}
	return foldedFind(characters, query, options, index, limit, canceled);
}

std::optional<TextPage> Document::textPage(int index) const {
	if (offThread() || index < 0 || index >= pageCount()) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return {};
	}
	TextPtr text(FPDFText_LoadPage(page.get()));
	if (!text) {
		return {};
	}
	const int count = FPDFText_CountChars(text.get());
	if (count < 0 || count > 200'000) {
		return {};
	}
	TextPage result;
	result.characters.reserve(count);
	constexpr int grid = 1 << 20;
	for (int i = 0; i < count; ++i) {
		TextChar character;
		character.unicode = FPDFText_GetUnicode(text.get(), i);
		FS_RECTF box{};
		if (FPDFText_GetLooseCharBox(text.get(), i, &box) &&
		    std::isfinite(box.left) && std::isfinite(box.right) &&
		    std::isfinite(box.top) && std::isfinite(box.bottom)) {
			int ax = 0, ay = 0, bx = 0, by = 0;
			if (FPDF_PageToDevice(page.get(), 0, 0, grid, grid, 0, box.left,
			                      box.top, &ax, &ay) &&
			    FPDF_PageToDevice(page.get(), 0, 0, grid, grid, 0, box.right,
			                      box.bottom, &bx, &by)) {
				character.box = {
				    std::clamp((std::min)(ax, bx) / double(grid), 0., 1.),
				    std::clamp((std::min)(ay, by) / double(grid), 0., 1.),
				    std::clamp((std::max)(ax, bx) / double(grid), 0., 1.),
				    std::clamp((std::max)(ay, by) / double(grid), 0., 1.)};
			}
		}
		result.characters.push_back(character);
	}
	return result;
}

std::optional<std::string> Document::textRange(int index, int first,
                                               int end) const {
	if (offThread() || !canCopy() || index < 0 || index >= pageCount() ||
	    first < 0 || end < first) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return {};
	}
	TextPtr text(FPDFText_LoadPage(page.get()));
	if (!text) {
		return {};
	}
	const int count = FPDFText_CountChars(text.get());
	if (count < 0 || count > 1'000'000) {
		return {};
	}
	std::u32string result;
	first = (std::min)(first, count);
	end = (std::min)(end, count);
	result.reserve(end - first);
	for (int i = first; i < end; ++i) {
		result += FPDFText_GetUnicode(text.get(), i);
	}
	return textUtf8(result);
}
bool Document::printPage(int index, void* device, int x, int y, int width,
                         int height) const {
	if (offThread() || !device || index < 0 ||
	    index >= FPDF_GetPageCount(m_handle) || width <= 0 || height <= 0) {
		return false;
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return false;
	}
	return FPDF_RenderPage(static_cast<HDC>(device), page.get(), x, y, width,
	                       height, 0, FPDF_ANNOT | FPDF_PRINTING) != 0;
}

namespace {
std::optional<Link> resolveLink(FPDF_DOCUMENT document, FPDF_DEST dest,
                                FPDF_ACTION action) {
	constexpr int GRID = 1 << 20;
	double px = 0, py = 0;
	const auto type =
	    action ? FPDFAction_GetType(action) : PDFACTION_UNSUPPORTED;
	if (action && type != PDFACTION_GOTO && type != PDFACTION_URI) {
		return std::nullopt;
	}
	if (!dest && type == PDFACTION_GOTO) {
		dest = FPDFAction_GetDest(document, action);
	}
	if (!dest) {
		if (type != PDFACTION_URI) {
			return std::nullopt;
		}
		const auto size = FPDFAction_GetURIPath(document, action, nullptr, 0);
		if (size < 2 || size > 1024 * 1024) {
			return std::nullopt;
		}
		std::string uri(size, '\0');
		if (FPDFAction_GetURIPath(document, action, uri.data(), size) != size ||
		    uri.back() != '\0') {
			return std::nullopt;
		}
		uri.pop_back();
		if (std::any_of(uri.begin(), uri.end(), [](unsigned char c) {
			    return c < 0x20 || c == 0x7f;
		    })) {
			return std::nullopt;
		}
		auto prefix = uri.substr(0, 8);
		std::transform(
		    prefix.begin(), prefix.end(), prefix.begin(),
		    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (!prefix.starts_with("https://") && !prefix.starts_with("http://")) {
			return std::nullopt;
		}
		return Link{std::nullopt, std::move(uri)};
	}
	LinkDestination target;
	target.page = FPDFDest_GetDestPageIndex(document, dest);
	if (target.page < 0 || target.page >= FPDF_GetPageCount(document)) {
		return std::nullopt;
	}
	PagePtr destination(FPDF_LoadPage(document, target.page));
	if (!destination) {
		return std::nullopt;
	}
	unsigned long count = 0;
	float params[4]{};
	const auto view = FPDFDest_GetView(dest, &count, params);
	if (count > 4 || !std::all_of(params, params + count, [](float value) {
		    return std::isfinite(value);
	    })) {
		return std::nullopt;
	}
	if (!FPDF_DeviceToPage(destination.get(), 0, 0, GRID, GRID, 0, 0, 0, &px,
	                       &py)) {
		return std::nullopt;
	}
	FPDF_BOOL hasX = false, hasY = false, hasZoom = false;
	float dx = 0, dy = 0, zoom = 0;
	if (view == PDFDEST_VIEW_XYZ) {
		FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZoom, &dx, &dy,
		                           &zoom);
		if (hasZoom && std::isfinite(zoom) && zoom > 0) {
			target.zoom = zoom;
		}
	} else if (view == PDFDEST_VIEW_FIT || view == PDFDEST_VIEW_FITB) {
		target.fit = LinkFit::Page;
	} else if (view == PDFDEST_VIEW_FITH || view == PDFDEST_VIEW_FITBH) {
		target.fit = LinkFit::Width;
		hasY = count > 0;
		dy = params[0];
	} else if (view == PDFDEST_VIEW_FITV || view == PDFDEST_VIEW_FITBV) {
		target.fit = LinkFit::Height;
		hasX = count > 0;
		dx = params[0];
	} else if (view == PDFDEST_VIEW_FITR && count == 4) {
		target.fit = LinkFit::Rectangle;
	}
	if ((hasX && !std::isfinite(dx)) || (hasY && !std::isfinite(dy))) {
		return std::nullopt;
	}
	int ax = 0, ay = 0, bx = 0, by = 0;
	if (target.fit == LinkFit::Rectangle) {
		if (!FPDF_PageToDevice(destination.get(), 0, 0, GRID, GRID, 0,
		                       params[0], params[1], &ax, &ay) ||
		    !FPDF_PageToDevice(destination.get(), 0, 0, GRID, GRID, 0,
		                       params[2], params[3], &bx, &by)) {
			return std::nullopt;
		}
		target.x = static_cast<double>((std::min)(ax, bx)) / GRID;
		target.y = static_cast<double>((std::min)(ay, by)) / GRID;
		target.width = std::abs(static_cast<double>(bx) - ax) / GRID;
		target.height = std::abs(static_cast<double>(by) - ay) / GRID;
		if (target.width <= 0 || target.height <= 0) {
			return std::nullopt;
		}
	} else if (hasX || hasY) {
		if (!FPDF_PageToDevice(destination.get(), 0, 0, GRID, GRID, 0,
		                       hasX ? dx : px, hasY ? dy : py, &ax, &ay)) {
			return std::nullopt;
		}
		const bool swapped = FPDFPage_GetRotation(destination.get()) % 2 != 0;
		if (swapped ? hasY : hasX) {
			target.x = static_cast<double>(ax) / GRID;
		}
		if (swapped ? hasX : hasY) {
			target.y = static_cast<double>(ay) / GRID;
		}
	}
	return Link{target, {}};
}
} // namespace

std::optional<Annotations>
Document::annotations(int index, const std::function<bool()>& canceled) const {
	const auto stopped = [&] { return canceled && canceled(); };
	if (offThread() || index < 0 || index >= pageCount() || stopped()) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return {};
	}
	const int count = FPDFPage_GetAnnotCount(page.get());
	if (count < 0) {
		return {};
	}
	Annotations result;
	result.page = index;
	result.limited = count > 4096;
	FS_RECTF pageBox{};
	const float width = FPDF_GetPageWidthF(page.get());
	const float height = FPDF_GetPageHeightF(page.get());
	const bool usablePage =
	    std::isfinite(width) && width > 0 && std::isfinite(height) &&
	    height > 0 && FPDF_GetPageBoundingBox(page.get(), &pageBox) &&
	    std::isfinite(pageBox.left) && std::isfinite(pageBox.right) &&
	    std::isfinite(pageBox.bottom) && std::isfinite(pageBox.top) &&
	    pageBox.left < pageBox.right && pageBox.bottom < pageBox.top;
	std::size_t total = 0;
	const auto text = [&](FPDF_ANNOTATION annotation, const char* key) {
		std::string value;
		const auto size = FPDFAnnot_GetStringValue(annotation, key, nullptr, 0);
		// I bound both individual fields and the whole page's text allocation.
		if (size < 2 || size % 2 != 0 || size > 64 * 1024 ||
		    total + size > 1024 * 1024) {
			result.limited = true;
			return value;
		}
		total += size;
		std::vector<unsigned char> bytes(size);
		if (FPDFAnnot_GetStringValue(
		        annotation, key, reinterpret_cast<FPDF_WCHAR*>(bytes.data()),
		        size) != size ||
		    bytes[size - 1] != 0 || bytes[size - 2] != 0) {
			result.limited = true;
			return value;
		}
		bytes.resize(size - 2);
		return utf16ToUtf8(bytes);
	};
	for (int i = 0; i < (std::min)(count, 4096); ++i) {
		if (stopped()) {
			return {};
		}
		AnnotationPtr handle(FPDFPage_GetAnnot(page.get(), i));
		if (!handle) {
			result.limited = true;
			continue;
		}
		Annotation annotation;
		annotation.index = i;
		annotation.flags = FPDFAnnot_GetFlags(handle.get());
		annotation.subtype = text(handle.get(), "Subtype");
		annotation.contents = text(handle.get(), "Contents");
		annotation.author = text(handle.get(), "T");
		FS_RECTF rect{};
		if (FPDFAnnot_GetValueType(handle.get(), "Rect") == FPDF_OBJECT_ARRAY &&
		    FPDFAnnot_GetRect(handle.get(), &rect) &&
		    std::isfinite(rect.left) && std::isfinite(rect.bottom) &&
		    std::isfinite(rect.right) && std::isfinite(rect.top) &&
		    rect.left <= rect.right && rect.bottom <= rect.top) {
			annotation.bounds =
			    AnnotationBounds{rect.left, rect.bottom, rect.right, rect.top};
			if (usablePage) {
				auto visible = rect;
				if (annotation.subtype == "Text" &&
				    FPDFAnnot_GetValueType(handle.get(), "AP") !=
				        FPDF_OBJECT_DICTIONARY) {
					// i target the 20-point icon PDFium generates for a note.
					visible.right = visible.left + 20;
					visible.top = visible.bottom + 20;
				}
				const double left = (std::max)(visible.left, pageBox.left);
				const double right = (std::min)(visible.right, pageBox.right);
				const double bottom =
				    (std::max)(visible.bottom, pageBox.bottom);
				const double top = (std::min)(visible.top, pageBox.top);
				constexpr int grid = 1 << 20;
				int x = 0, y = 0;
				// i clip before conversion so remote bounds stay in range.
				if (rect.left < rect.right && rect.bottom < rect.top &&
				    left < right && bottom < top &&
				    FPDF_PageToDevice(page.get(), 0, 0, grid, grid, 0,
				                      left + (right - left) / 2,
				                      bottom + (top - bottom) / 2, &x, &y)) {
					annotation.position =
					    AnnotationPoint{std::clamp(x / double(grid), 0., 1.),
					                    std::clamp(y / double(grid), 0., 1.)};
				}
			}
		}
		result.entries.push_back(std::move(annotation));
	}
	return stopped() ? std::nullopt : std::optional{std::move(result)};
}

// see:
// https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_attachment.h
std::optional<Attachments>
Document::attachments(const std::function<bool()>& canceled) const {
	if (offThread()) {
		return {};
	}
	Attachments result;
	const int count = FPDFDoc_GetAttachmentCount(m_handle);
	std::size_t total = 0;
	for (int i = 0; i < (std::min)(count, 10000); ++i) {
		if (canceled && canceled()) {
			return {};
		}
		const auto handle = FPDFDoc_GetAttachment(m_handle, i);
		const auto size =
		    handle ? FPDFAttachment_GetName(handle, nullptr, 0) : 0;
		std::string name;
		if (size >= 2 && size <= 8192 && size % 2 == 0 &&
		    total + size <= 2 * 1024 * 1024) {
			total += size;
			std::vector<unsigned char> bytes(size);
			if (FPDFAttachment_GetName(
			        handle, reinterpret_cast<FPDF_WCHAR*>(bytes.data()),
			        size) == size &&
			    bytes[size - 1] == 0 && bytes[size - 2] == 0) {
				bytes.resize(size - 2);
				name = utf16ToUtf8(bytes);
			}
		}
		if (name.empty()) {
			name = "Attachment " + std::to_string(i + 1);
			result.limited = true;
		}
		for (auto& c : name) {
			if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
				c = ' ';
			}
		}
		result.entries.push_back({i, std::move(name)});
	}
	result.limited = result.limited || count > 10000;
	return result;
}

std::optional<std::vector<std::uint8_t>>
Document::attachment(int index, const std::function<bool()>& canceled) const {
	if (offThread() || index < 0 ||
	    index >= FPDFDoc_GetAttachmentCount(m_handle) ||
	    (canceled && canceled())) {
		return {};
	}
	const auto handle = FPDFDoc_GetAttachment(m_handle, index);
	unsigned long size = 0;
	if (!handle || !FPDFAttachment_GetFile(handle, nullptr, 0, &size) ||
	    size > 128UL * 1024 * 1024 || (canceled && canceled())) {
		return {};
	}
	std::vector<std::uint8_t> bytes(size);
	unsigned char empty = 0;
	unsigned long written = 0;
	if (!FPDFAttachment_GetFile(handle, size ? bytes.data() : &empty, size,
	                            &written) ||
	    written != size || (canceled && canceled())) {
		return {};
	}
	return bytes;
}

std::optional<Outline>
Document::outline(const std::function<bool()>& canceled) const {
	if (offThread()) {
		return {};
	}
	struct Pending {
		FPDF_BOOKMARK bookmark;
		int parent, depth;
	};
	std::vector<Pending> pending;
	if (const auto root = FPDFBookmark_GetFirstChild(m_handle, nullptr)) {
		pending.push_back({root, -1, 0});
	}
	std::set<FPDF_BOOKMARK> seen;
	std::vector<void*> bookmarks;
	Outline result;
	std::size_t titleBytes = 0;
	while (!pending.empty() && result.entries.size() < 10'000) {
		if (canceled && canceled()) {
			return {};
		}
		const auto item = pending.back();
		pending.pop_back();
		if (!seen.insert(item.bookmark).second) {
			result.limited = true;
			continue;
		}
		OutlineNode node;
		node.parent = item.parent;
		node.expanded = FPDFBookmark_GetCount(item.bookmark) > 0;
		const auto size = FPDFBookmark_GetTitle(item.bookmark, nullptr, 0);
		if (size >= 2 && size <= 8192 && size % 2 == 0 &&
		    titleBytes + size <= 2 * 1024 * 1024) {
			titleBytes += size;
			std::vector<unsigned char> bytes(size);
			if (FPDFBookmark_GetTitle(item.bookmark, bytes.data(), size) ==
			        size &&
			    bytes[size - 1] == 0 && bytes[size - 2] == 0) {
				bytes.resize(size - 2);
				node.title = utf16ToUtf8(bytes);
				for (auto& value : node.title) {
					if (static_cast<unsigned char>(value) < 0x20 ||
					    value == 0x7F) {
						value = ' ';
					}
				}
			} else {
				result.limited = true;
			}
		} else if (size) {
			result.limited = true;
		}
		if (node.title.empty() ||
		    std::all_of(node.title.begin(), node.title.end(),
		                [](char c) { return c == ' '; })) {
			node.title = "Untitled";
		}
		const auto action = FPDFBookmark_GetAction(item.bookmark);
		const auto type =
		    action ? FPDFAction_GetType(action) : PDFACTION_UNSUPPORTED;
		node.canFollow =
		    action ? type == PDFACTION_GOTO || type == PDFACTION_URI
		           : FPDFBookmark_GetDest(m_handle, item.bookmark) != nullptr;
		bookmarks.push_back(item.bookmark);
		const int index = static_cast<int>(result.entries.size());
		result.entries.push_back(std::move(node));
		if (const auto next =
		        FPDFBookmark_GetNextSibling(m_handle, item.bookmark)) {
			pending.push_back({next, item.parent, item.depth});
		}
		if (const auto child =
		        FPDFBookmark_GetFirstChild(m_handle, item.bookmark)) {
			if (item.depth < 63) {
				pending.push_back({child, index, item.depth + 1});
			} else {
				result.limited = true;
			}
		}
	}
	if (!pending.empty()) {
		result.limited = true;
	}
	m_outlineBookmarks = std::move(bookmarks);
	return result;
}

std::optional<Link> Document::outlineLink(std::size_t index) const {
	if (offThread() || index >= m_outlineBookmarks.size()) {
		return {};
	}
	const auto bookmark = static_cast<FPDF_BOOKMARK>(m_outlineBookmarks[index]);
	return resolveLink(m_handle, FPDFBookmark_GetDest(m_handle, bookmark),
	                   FPDFBookmark_GetAction(bookmark));
}

std::optional<Link> Document::linkAt(int index, double x, double y) const {
	if (offThread() || index < 0 || index >= FPDF_GetPageCount(m_handle) ||
	    !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 ||
	    y > 1) {
		return std::nullopt;
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	// a fixed device grid preserves sub-pixel accuracy without UI/DPI state
	constexpr int GRID = 1 << 20;
	double px = 0, py = 0;
	if (!page ||
	    !FPDF_DeviceToPage(page.get(), 0, 0, GRID, GRID, 0,
	                       static_cast<int>(std::lround(x * GRID)),
	                       static_cast<int>(std::lround(y * GRID)), &px, &py)) {
		return std::nullopt;
	}
	const auto link = FPDFLink_GetLinkAtPoint(page.get(), px, py);
	if (!link) {
		return std::nullopt;
	}
	return resolveLink(m_handle, FPDFLink_GetDest(m_handle, link),
	                   FPDFLink_GetAction(link));
}
} // namespace pdf
