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
#include "document_internal.h"
#include "note_visibility.h"
#include "text_box_appearance.h"

#include <cassert>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <memory>
#include <iomanip>
#include <locale>
#include <sstream>
#include <set>
#include <system_error>
#include <stdexcept>

#include <fpdf_doc.h>
#include <fpdf_formfill.h>
#include <fpdf_dataavail.h>
#include <cstring>
#include <limits>
#include <fpdf_attachment.h>
#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdf_signature.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include "engine.h"

namespace pdf {
using namespace detail;

TextBoxTextError validateTextBoxText(std::u16string_view text,
                                     TextBoxLayout layout) {
	if (text.empty()) {
		return TextBoxTextError::Blank;
	}
	if (text.size() > textBoxMaxCharacters) {
		return TextBoxTextError::Length;
	}
	int lines = 1;
	bool painted = false;
	for (const auto c : text) {
		if (c == u'\n' && layout == TextBoxLayout::ExplicitLines) {
			++lines;
		} else if (c < 32 || c > 126) {
			return TextBoxTextError::Characters;
		} else if (c != u' ') {
			painted = true;
		}
	}
	if (lines > textBoxMaxLines) {
		return TextBoxTextError::Lines;
	}
	// i keep the old single-line writer's space handling.
	return lines > 1 && !painted ? TextBoxTextError::Blank
	                             : TextBoxTextError::None;
}

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
		return {std::unique_ptr<Document>(
		            new Document(std::move(data), handle, password)),
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

Document::Document(std::vector<std::uint8_t> data, fpdf_document_t__* handle,
                   const std::string& password)
    : m_data(std::move(data)), m_handle(handle),
      m_password(FPDF_GetSecurityHandlerRevision(handle) >= 0 ? password : "") {
}

Document::~Document() {
	if (!m_password.empty()) {
		SecureZeroMemory(m_password.data(), m_password.size());
	}
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
	const auto drawing = formDrawingDocument();
	// the base renderer also generates widget appearances. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_annotlist.cpp
	PagePtr page(FPDF_LoadPage(drawing ? drawing->m_handle : m_handle, index));
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
	detail::NoteVisibility notes;
	if (!notes.collect(page.get()) || !notes.run([&] {
		    FPDF_RenderPageBitmap(bitmap.get(), page.get(), 0, 0, width, height,
		                          rotation, FPDF_ANNOT);
		    return true;
	    })) {
		return {};
	}
	drawFormFields(index, bitmap.get(), 0, 0, width, height, rotation, false);
	return out;
}

OpenResult Document::reopenFormCopy(std::vector<std::uint8_t> data) const {
	if (offThread()) {
		return {nullptr, Error::Unknown};
	}
	auto copy = openDocument(std::move(data), m_password);
	if (copy.document) {
		const auto handle = copy.document->m_handle;
		if (FPDF_GetSecurityHandlerRevision(handle) !=
		        FPDF_GetSecurityHandlerRevision(m_handle) ||
		    FPDF_GetDocPermissions(handle) !=
		        FPDF_GetDocPermissions(m_handle) ||
		    FPDF_GetDocUserPermissions(handle) !=
		        FPDF_GetDocUserPermissions(m_handle)) {
			return {nullptr, Error::Security};
		}
	}
	return copy;
}

bool Document::Transaction::stopped() {
	halted = halted || (canceled && canceled());
	return halted;
}

Document::Transaction
Document::transaction(const std::function<bool()>& canceled) {
	Transaction result{*this, canceled, {}};
	const std::function<bool()> stopped = [&result] {
		return result.stopped();
	};
	if (offThread() || stopped() || canWrite(stopped) != WriteRefusal::None ||
	    !canSerializeEdits(stopped)) {
		return result;
	}
	auto bytes = serialize(EDIT_LIMIT, stopped);
	if (bytes && !stopped()) {
		auto opened = reopenFormCopy(std::move(*bytes));
		result.document = std::move(opened.document);
	}
	return result;
}

Document::Transaction
Document::transaction(std::vector<std::uint8_t> data,
                      const std::function<bool()>& canceled) {
	Transaction result{*this, canceled, {}};
	if (offThread() || result.stopped()) {
		return result;
	}
	// i keep the raw snapshot used by the caller's appearance proofs.
	auto opened = reopenFormCopy(std::move(data));
	if (!result.stopped()) {
		result.document = std::move(opened.document);
	}
	return result;
}

bool Document::Transaction::canonicalize() {
	if (!document || stopped()) {
		return false;
	}
	document->m_modified = true;
	const auto stop = [this] { return stopped(); };
	auto bytes = document->serialize(EDIT_LIMIT, stop);
	if (!bytes || stopped()) {
		return false;
	}
	auto opened = source.reopenFormCopy(std::move(*bytes));
	document = std::move(opened.document);
	return document && !stopped();
}

OpenResult Document::Transaction::save() {
	if (!document || stopped()) {
		return {};
	}
	document->m_modified = true;
	const auto stop = [this] { return stopped(); };
	auto bytes = document->serialize(EDIT_LIMIT, stop);
	if (!bytes || stopped()) {
		return {};
	}
	auto next = source.reopenFormCopy(std::move(*bytes));
	if (!next.document || next.document->pageCount() != source.pageCount() ||
	    stopped()) {
		return {};
	}
	return next;
}

bool Document::Transaction::commit(std::unique_ptr<Document> next) {
	if (!next ||
	    (!source.m_outlineBookmarks.empty() &&
	     (!next->outline([this] { return stopped(); }) ||
	      next->m_outlineBookmarks.size() !=
	          source.m_outlineBookmarks.size())) ||
	    stopped()) {
		return false;
	}
	source.m_data.swap(next->m_data);
	std::swap(source.m_handle, next->m_handle);
	source.m_outlineBookmarks.swap(next->m_outlineBookmarks);
	source.invalidateEdits();
	++source.m_annotationRevision;
	source.m_modified = true;
	return true;
}

bool Document::canCopy() const {
	return !offThread() && (FPDF_GetDocPermissions(m_handle) & 0x10) != 0;
}

bool Document::canAnnotate() const {
	return !offThread() && (FPDF_GetDocPermissions(m_handle) & 0x20) != 0;
}

WriteRefusal Document::canWrite(const std::function<bool()>& canceled) const {
	// a caller's cancel can fire once, so i keep it from then on.
	bool halted = false;
	const std::function<bool()> stopped = [&] {
		halted = halted || (canceled && canceled());
		return halted;
	};
	if (offThread() || stopped()) {
		return WriteRefusal::Unavailable;
	}
	if (m_writeRefusal) {
		return *m_writeRefusal;
	}
	const int signatures = FPDF_GetSignatureCount(m_handle);
	if (signatures < 0) {
		m_writeRefusal = WriteRefusal::Unavailable;
		return *m_writeRefusal;
	}
	// the count includes empty fields. the contents come from the field's
	// signature value. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/public/fpdf_signature.h
	for (int index = 0; index < signatures; ++index) {
		if (stopped()) {
			return WriteRefusal::Unavailable;
		}
		const auto signature = FPDF_GetSignatureObject(m_handle, index);
		if (signature && FPDFSignatureObj_GetContents(signature, nullptr, 0)) {
			m_writeRefusal = WriteRefusal::Signed;
			return *m_writeRefusal;
		}
	}
	if (!canSerializeEdits(stopped)) {
		// a file i never rewrite keeps whatever signature it has.
		return stopped() ? WriteRefusal::Unavailable : WriteRefusal::None;
	}
	// the counter reads only the top-level /Fields array, and the public
	// headers can't reach /Perms. i read a full rewrite, which expands
	// object streams, for nested signature values, DocMDP and /Perms. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_signature.cpp
	// https://pdfium.googlesource.com/pdfium/+/2fd6cff57d9412cc42ef1a7e4e0a59b13a1e7cec/public/fpdf_catalog.h
	constexpr std::size_t limit = 128 * 1024 * 1024;
	auto bytes = serialize(limit, stopped);
	if (!bytes || stopped()) {
		return WriteRefusal::Unavailable;
	}
	const auto scratch = reopenFormCopy(std::move(*bytes));
	if (!scratch.document || stopped()) {
		return WriteRefusal::Unavailable;
	}
	scratch.document->m_modified = true;
	const auto canonical = scratch.document->serialize(limit, stopped);
	if (!canonical || stopped()) {
		return WriteRefusal::Unavailable;
	}
	m_writeRefusal = detail::signatureTokens(
	                     {reinterpret_cast<const char*>(canonical->data()),
	                      canonical->size()})
	                     ? WriteRefusal::Signed
	                     : WriteRefusal::None;
	return *m_writeRefusal;
}

WriteRefusal Document::writeRefusal() const {
	return offThread() ? WriteRefusal::None
	                   : m_writeRefusal.value_or(WriteRefusal::None);
}

std::optional<int> Document::addTextBox(
    int index, const AnnotationBounds& bounds, std::u16string_view contents,
    std::u16string_view author, std::uint64_t revision,
    const std::function<bool()>& canceled, TextBoxLayout layout) {
	const auto stopped = [&] { return canceled && canceled(); };
	if (offThread() || !revision || !currentRevision(revision) || index < 0 ||
	    index >= pageCount() || !canAnnotate() || stopped() ||
	    validateTextBoxText(contents, layout) != TextBoxTextError::None ||
	    !validNoteText(author) || !std::isfinite(bounds.left) ||
	    !std::isfinite(bounds.bottom) || !std::isfinite(bounds.right) ||
	    !std::isfinite(bounds.top) || bounds.right <= bounds.left ||
	    bounds.top <= bounds.bottom ||
	    (FPDF_GetFormType(m_handle) != FORMTYPE_NONE &&
	     FPDF_GetFormType(m_handle) != FORMTYPE_ACRO_FORM) ||
	    canWrite(canceled) != WriteRefusal::None ||
	    !canSerializeEdits(canceled)) {
		return {};
	}
	auto scratch = transaction(canceled);
	// i expand object streams before choosing the font name. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/edit/cpdf_creator.cpp
	if (!scratch.document || !scratch.canonicalize()) {
		return {};
	}
	const std::string_view tokens(
	    reinterpret_cast<const char*>(scratch.document->m_data.data()),
	    scratch.document->m_data.size());
	// the rewrite expands object streams and canonicalizes names. i choose
	// an absent name so existing DR fonts cannot replace this box's font.
	// strings/streams can cause a conservative collision too. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_generateap.cpp
	std::string fontName;
	for (int candidate = 0; candidate < 4096; ++candidate) {
		if (stopped()) {
			return {};
		}
		auto name = "SableTextBoxFont" + std::to_string(candidate);
		if (tokens.find("/" + name) == std::string_view::npos) {
			fontName = std::move(name);
			break;
		}
	}
	if (fontName.empty()) {
		return {};
	}
	const std::string appearance = "/" + fontName + " 12 Tf 0 g";
	const std::u16string defaultAppearance(appearance.begin(),
	                                       appearance.end());

	auto expected = editCounts();
	if (!expected) {
		return {};
	}
	int added = -1;
	const FS_RECTF rect{bounds.left, bounds.top, bounds.right, bounds.bottom};
	std::optional<MultilineBoxAppearance> lines;
	const std::u16string text(contents), name(author);
	{
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, index));
		FS_RECTF box{};
		if (!page || !FPDF_GetPageBoundingBox(page.get(), &box) ||
		    !std::isfinite(box.left) || !std::isfinite(box.bottom) ||
		    !std::isfinite(box.right) || !std::isfinite(box.top) ||
		    bounds.left < box.left || bounds.bottom < box.bottom ||
		    bounds.right > box.right || bounds.top > box.top) {
			return {};
		}
		if (contents.find(u'\n') != std::u16string_view::npos) {
			if (!boxProofRegion(page.get(), rect)) {
				return {};
			}
			lines = multilineBoxAppearance(scratch.document->m_handle, rect,
			                               contents);
			if (!lines || stopped()) {
				return {};
			}
		}
		added = FPDFPage_GetAnnotCount(page.get());
		if (added < 0 || added >= 4096 || (*expected)[index] != added) {
			return {};
		}
		++(*expected)[index];
		AnnotationPtr note(
		    FPDFPage_CreateAnnot(page.get(), FPDF_ANNOT_FREETEXT));
		// @todo: expose the text-box font size with the native editor.
		if (!note || !FPDFAnnot_SetRect(note.get(), &rect) ||
		    !FPDFAnnot_SetStringValue(
		        note.get(), "Contents",
		        reinterpret_cast<FPDF_WIDESTRING>(text.c_str())) ||
		    !FPDFAnnot_SetStringValue(
		        note.get(), "T",
		        reinterpret_cast<FPDF_WIDESTRING>(name.c_str())) ||
		    !FPDFAnnot_SetStringValue(
		        note.get(), "DA",
		        reinterpret_cast<FPDF_WIDESTRING>(defaultAppearance.c_str())) ||
		    !FPDFAnnot_SetFlags(note.get(), FPDF_ANNOT_FLAG_PRINT)) {
			return {};
		}
	}
	if (stopped() || !scratch.document->checkEditCounts(*expected, {index})) {
		return {};
	}
	{
		// render this isolated document directly; render() isolates forms
		// again.
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, index));
		BitmapPtr bitmap(FPDFBitmap_Create(64, 64, 1));
		if (!page || !bitmap || stopped()) {
			return {};
		}
		FPDFBitmap_FillRect(bitmap.get(), 0, 0, 64, 64, 0xFFFFFFFF);
		FPDF_RenderPageBitmap(bitmap.get(), page.get(), 0, 0, 64, 64, 0,
		                      FPDF_ANNOT);
	}
	if (stopped()) {
		return {};
	}
	if (lines) {
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, index));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), added)
		                        : nullptr);
		if (!note || !FPDFAnnot_HasKey(note.get(), "AP") ||
		    !FPDFAnnot_SetAP(
		        note.get(), 0,
		        reinterpret_cast<FPDF_WIDESTRING>(lines->stream.c_str()))) {
			return {};
		}
	}
	std::u16string literal = u"(";
	for (auto c : text) {
		if (c == u'(' || c == u')' || c == u'\\') {
			literal += u'\\';
		}
		literal += c;
	}
	literal += u") Tj";
	const auto proof = [&](const Document& document) {
		PagePtr page(FPDF_LoadPage(document.m_handle, index));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), added)
		                        : nullptr);
		FS_RECTF rect{};
		if (!note || FPDFAnnot_GetSubtype(note.get()) != FPDF_ANNOT_FREETEXT ||
		    FPDFAnnot_GetFlags(note.get()) != FPDF_ANNOT_FLAG_PRINT ||
		    !FPDFAnnot_GetRect(note.get(), &rect) || rect.left != bounds.left ||
		    rect.bottom != bounds.bottom || rect.right != bounds.right ||
		    rect.top != bounds.top || !FPDFAnnot_HasKey(note.get(), "AP")) {
			return false;
		}
		if (lines) {
			return matchesMultilineBoxAppearance(note.get(), rect, *lines) &&
			       !stopped();
		}
		const auto size = FPDFAnnot_GetAP(note.get(), 0, nullptr, 0);
		if (size < 2 || size > 65536 || size % 2) {
			return false;
		}
		std::vector<FPDF_WCHAR> ap(size / 2);
		if (FPDFAnnot_GetAP(note.get(), 0, ap.data(), size) != size ||
		    ap.back() ||
		    std::u16string(ap.begin(), ap.end() - 1).find(literal) ==
		        std::u16string::npos) {
			return false;
		}
		int textObjects = 0;
		const int count = FPDFAnnot_GetObjectCount(note.get());
		if (count < 1 || count > 16) {
			return false;
		}
		for (int i = 0; i < count; ++i) {
			const auto object = FPDFAnnot_GetObject(note.get(), i);
			if (FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_TEXT) {
				continue;
			}
			++textObjects;
			float left, bottom, right, top, sizeInPoints;
			char font[64]{};
			const auto face = FPDFTextObj_GetFont(object);
			if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top) ||
			    !std::isfinite(left) || !std::isfinite(bottom) ||
			    !std::isfinite(right) || !std::isfinite(top) ||
			    left < bounds.left || bottom < bounds.bottom ||
			    right > bounds.right || top > bounds.top ||
			    !FPDFTextObj_GetFontSize(object, &sizeInPoints) ||
			    sizeInPoints != 12 ||
			    FPDFFont_GetBaseFontName(face, font, sizeof(font)) != 10 ||
			    std::string_view(font) != "Helvetica") {
				return false;
			}
		}
		return textObjects == 1 && !stopped();
	};
	if (!proof(*scratch.document)) {
		return {};
	}
	auto next = scratch.save();
	if (!next.document || !proof(*next.document) ||
	    !next.document->checkEditCounts(*expected, {index})) {
		return {};
	}
	const auto notes = next.document->annotations(index, canceled);
	if (!notes || notes->limited || notes->entries.empty() ||
	    notes->entries.back().index != added ||
	    notes->entries.back().contents != utf16ToUtf8(text) ||
	    notes->entries.back().author != utf16ToUtf8(name)) {
		return {};
	}
	if (!scratch.commit(std::move(next.document))) {
		return {};
	}
	return added;
}

std::optional<Annotation>
Document::textBox(const AnnotationTarget& target,
                  const std::function<bool()>& canceled, TextBoxLayout layout) {
	if (offThread() || target.subtype != "FreeText" || !matchesTarget(target)) {
		return {};
	}
	const auto notes = annotations(target.page, canceled);
	if (!notes || notes->limited) {
		return {};
	}
	for (const auto& note : notes->entries) {
		if (note.target == target) {
			const std::u16string text(note.contents.begin(),
			                          note.contents.end());
			const auto result = updateTextBox(note, text, canceled, layout);
			return result && !*result ? std::optional{note} : std::nullopt;
		}
	}
	return {};
}

std::optional<bool> Document::updateTextBox(
    const Annotation& expected, std::u16string_view contents,
    const std::function<bool()>& canceled, TextBoxLayout layout) {
	return writeTextBox(expected, contents, {}, canceled, layout);
}

std::optional<bool> Document::updateTextBox(
    const Annotation& expected, std::u16string_view contents, TextBoxSize size,
    const std::function<bool()>& canceled, TextBoxLayout layout) {
	return writeTextBox(expected, contents, size, canceled, layout);
}

std::optional<bool>
Document::writeTextBox(const Annotation& expected, std::u16string_view contents,
                       std::optional<TextBoxSize> size,
                       const std::function<bool()>& canceled,
                       TextBoxLayout layout) {
	const auto stopped = [&] { return canceled && canceled(); };
	const auto& target = expected.target;
	if (expected.contents.size() > textBoxMaxCharacters) {
		return {};
	}
	const std::u16string previous(expected.contents.begin(),
	                              expected.contents.end());
	const bool oldLines = previous.find(u'\n') != std::u16string::npos;
	const bool newLines = contents.find(u'\n') != std::u16string_view::npos;
	if (offThread() || stopped() ||
	    m_annotationRevision == (std::numeric_limits<std::uint64_t>::max)() ||
	    !canAnnotate() || !expected.bounds || target.subtype != "FreeText" ||
	    expected.subtype != target.subtype || expected.index != target.index ||
	    validateTextBoxText(contents, layout) != TextBoxTextError::None ||
	    validateTextBoxText(previous, layout) != TextBoxTextError::None ||
	    !matchesTarget(target) || !canSerializeEdits(canceled) ||
	    canWrite(canceled) != WriteRefusal::None ||
	    !provenUnique(target.page, target.index)) {
		return {};
	}
	const auto notes = annotations(target.page, canceled);
	if (!notes || notes->limited) {
		return {};
	}
	const auto found =
	    std::find_if(notes->entries.begin(), notes->entries.end(),
	                 [&](const auto& note) { return note.target == target; });
	const auto& bounds = *expected.bounds;
	auto destination = bounds;
	if (size) {
		if (!std::isfinite(size->width) || !std::isfinite(size->height) ||
		    size->width <= 0 || size->height <= 0) {
			return {};
		}
		// i keep exact coordinates when the requested dimensions match.
		if (size->width != bounds.right - bounds.left) {
			destination.right = bounds.left + size->width;
		}
		if (size->height != bounds.top - bounds.bottom) {
			destination.bottom = bounds.top - size->height;
		}
	}
	const bool sameBounds = destination.right == bounds.right &&
	                        destination.bottom == bounds.bottom;
	if (found == notes->entries.end() || !found->bounds ||
	    found->contents != expected.contents ||
	    found->author != expected.author || found->flags != expected.flags ||
	    found->flags != FPDF_ANNOT_FLAG_PRINT ||
	    found->bounds->left != bounds.left ||
	    found->bounds->bottom != bounds.bottom ||
	    found->bounds->right != bounds.right ||
	    found->bounds->top != bounds.top) {
		return {};
	}
	const auto read = [](FPDF_ANNOTATION note, const char* key,
	                     int mode = -1) -> std::optional<std::u16string> {
		const auto size = mode < 0
		                      ? FPDFAnnot_GetStringValue(note, key, nullptr, 0)
		                      : FPDFAnnot_GetAP(note, mode, nullptr, 0);
		if (size < 2 || size > 65536 || size % 2) {
			return {};
		}
		std::vector<FPDF_WCHAR> text(size / 2);
		const auto copied =
		    mode < 0 ? FPDFAnnot_GetStringValue(note, key, text.data(), size)
		             : FPDFAnnot_GetAP(note, mode, text.data(), size);
		if (copied != size || text.back()) {
			return {};
		}
		return std::u16string(text.begin(), text.end() - 1);
	};
	std::u16string author;
	{
		PagePtr page(FPDF_LoadPage(m_handle, target.page));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
		                        : nullptr);
		if (!note || !m_linkedPages ||
		    !unlinkedAnnotation(m_handle, page.get(), target.index, note.get(),
		                        *m_linkedPages, m_editPageLoads)) {
			return {};
		}
		for (const auto* key :
		     {"RC", "DS", "RD", "BS", "BE", "C", "CA", "BM", "Q", "IT", "CL",
		      "Rotate", "LE", "OC", "A", "AA"}) {
			if (FPDFAnnot_HasKey(note.get(), key)) {
				return {};
			}
		}
		if (FPDFAnnot_GetValueType(note.get(), "Contents") !=
		        FPDF_OBJECT_STRING ||
		    FPDFAnnot_GetValueType(note.get(), "DA") != FPDF_OBJECT_STRING ||
		    !FPDFAnnot_HasKey(note.get(), "AP")) {
			return {};
		}
		for (int mode : {1, 2}) {
			if (FPDFAnnot_GetAP(note.get(), mode, nullptr, 0) != 2) {
				return {};
			}
		}
		if (FPDFAnnot_HasKey(note.get(), "T")) {
			if (FPDFAnnot_GetValueType(note.get(), "T") != FPDF_OBJECT_STRING) {
				return {};
			}
			const auto by = read(note.get(), "T");
			if (!by || !validNoteText(*by) ||
			    utf16ToUtf8(*by) != expected.author) {
				return {};
			}
			author = *by;
		}
	}
	constexpr std::size_t limit = 128 * 1024 * 1024;
	auto bytes = serialize(limit, canceled);
	if (!bytes || stopped()) {
		return {};
	}
	if (oldLines) {
		auto isolated = reopenFormCopy(*bytes);
		if (!isolated.document || stopped()) {
			return {};
		}
		const auto handle = isolated.document->m_handle;
		const FS_RECTF rect{bounds.left, bounds.top, bounds.right,
		                    bounds.bottom};
		const auto model = multilineBoxAppearance(handle, rect, previous);
		PagePtr page(FPDF_LoadPage(handle, target.page));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
		                        : nullptr);
		if (!note || !model ||
		    !matchesMultilineBoxAppearance(note.get(), rect, *model)) {
			return {};
		}
		note.reset();
		if (!matchesBoxRendering(page.get(), target.index, rect, model->stream,
		                         stopped)) {
			return {};
		}
	}
	auto scratch = transaction(std::move(*bytes), canceled);
	if (!scratch.document) {
		return {};
	}
	const auto reference = scratch.document->addTextBox(
	    target.page, bounds, previous, author, 1, canceled, layout);
	if (!reference || stopped()) {
		return {};
	}
	// i compare the complete old appearance against a fresh standard box.
	// the resource prefix alone does not prove its font or encoding. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_edittext.cpp
	const auto equivalent = [&](FPDF_DOCUMENT document, FPDF_PAGE page,
	                            FPDF_ANNOTATION note, FPDF_ANNOTATION model,
	                            bool normalize) {
		auto ap = read(note, nullptr, 0), correct = read(model, nullptr, 0);
		const auto da = read(note, "DA"), standard = read(model, "DA");
		if (!ap || !correct || !da || !standard) {
			return false;
		}
		if (normalize) {
			const auto end = da->find(u' '), other = standard->find(u' ');
			if (end == std::u16string::npos || other == std::u16string::npos ||
			    da->substr(end) != u" 12 Tf 0 g" || da->front() != u'/') {
				return false;
			}
			const auto token = da->substr(0, end) + u" 12 Tf";
			const auto replacement = standard->substr(0, other) + u" 12 Tf";
			for (auto at = ap->find(token); at != std::u16string::npos;
			     at = ap->find(token, at + replacement.size())) {
				ap->replace(at, token.size(), replacement);
			}
		}
		if (*ap != *correct ||
		    FPDFAnnot_GetObjectCount(note) != FPDFAnnot_GetObjectCount(model)) {
			return false;
		}
		int texts = 0;
		for (int i = 0; i < FPDFAnnot_GetObjectCount(note); ++i) {
			const auto a = FPDFAnnot_GetObject(note, i),
			           b = FPDFAnnot_GetObject(model, i);
			if (FPDFPageObj_GetType(a) != FPDFPageObj_GetType(b)) {
				return false;
			}
			if (FPDFPageObj_GetType(a) != FPDF_PAGEOBJ_TEXT) {
				continue;
			}
			++texts;
			const auto fa = FPDFTextObj_GetFont(a), fb = FPDFTextObj_GetFont(b);
			size_t na = 0, nb = 0;
			if (FPDFFont_GetIsEmbedded(fa) != 0 ||
			    !FPDFFont_GetFontData(fa, nullptr, 0, &na) ||
			    !FPDFFont_GetFontData(fb, nullptr, 0, &nb) || na != nb || !na ||
			    na > 1024 * 1024) {
				return false;
			}
			std::vector<std::uint8_t> data(na), expectedData(nb);
			if (!FPDFFont_GetFontData(fa, data.data(), data.size(), &na) ||
			    !FPDFFont_GetFontData(fb, expectedData.data(),
			                          expectedData.size(), &nb) ||
			    data != expectedData) {
				return false;
			}
			BitmapPtr image(
			    FPDFTextObj_GetRenderedBitmap(document, page, a, 2));
			BitmapPtr sample(
			    FPDFTextObj_GetRenderedBitmap(document, page, b, 2));
			if (!image || !sample) {
				return false;
			}
			const int width = FPDFBitmap_GetWidth(image.get()),
			          height = FPDFBitmap_GetHeight(image.get());
			if (width < 1 || width > 8192 || height < 1 || height > 512 ||
			    FPDFBitmap_GetWidth(sample.get()) != width ||
			    FPDFBitmap_GetHeight(sample.get()) != height ||
			    FPDFBitmap_GetFormat(image.get()) != FPDFBitmap_BGRA ||
			    FPDFBitmap_GetFormat(sample.get()) != FPDFBitmap_BGRA) {
				return false;
			}
			const auto* pixels = static_cast<const std::uint8_t*>(
			    FPDFBitmap_GetBuffer(image.get()));
			const auto* modelPixels = static_cast<const std::uint8_t*>(
			    FPDFBitmap_GetBuffer(sample.get()));
			for (int y = 0; y < height; ++y) {
				if (std::memcmp(pixels + y * FPDFBitmap_GetStride(image.get()),
				                modelPixels +
				                    y * FPDFBitmap_GetStride(sample.get()),
				                static_cast<size_t>(width) * 4)) {
					return false;
				}
			}
		}
		return texts == 1 && !stopped();
	};
	{
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, target.page));
		AnnotationPtr note(FPDFPage_GetAnnot(page.get(), target.index)),
		    model(FPDFPage_GetAnnot(page.get(), *reference));
		if (stopped() ||
		    (!oldLines && !equivalent(scratch.document->m_handle, page.get(),
		                              note.get(), model.get(), true))) {
			return {};
		}
		if (contents == previous && sameBounds) {
			return false;
		}
		model.reset();
		note.reset();
		if (!FPDFPage_RemoveAnnot(page.get(), *reference)) {
			return {};
		}
	}
	scratch.document->invalidateEdits();
	scratch.document->m_modified = true;
	const auto created = scratch.document->addTextBox(
	    target.page, destination, contents, author,
	    scratch.document->annotationRevision(), canceled, layout);
	if (!created || stopped()) {
		return {};
	}
	const FS_RECTF rect{destination.left, destination.top, destination.right,
	                    destination.bottom};
	const auto lines =
	    newLines
	        ? multilineBoxAppearance(scratch.document->m_handle, rect, contents)
	        : std::optional<MultilineBoxAppearance>{};
	if (newLines && !lines) {
		return {};
	}
	std::u16string appearance;
	{
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, target.page));
		AnnotationPtr note(FPDFPage_GetAnnot(page.get(), target.index)),
		    model(FPDFPage_GetAnnot(page.get(), *created));
		const auto da = read(model.get(), "DA");
		const std::u16string replacement(contents);
		if (!da || !FPDFAnnot_SetRect(note.get(), &rect) ||
		    !FPDFAnnot_SetStringValue(
		        note.get(), "Contents",
		        reinterpret_cast<FPDF_WIDESTRING>(replacement.c_str())) ||
		    !FPDFAnnot_SetStringValue(
		        note.get(), "DA",
		        reinterpret_cast<FPDF_WIDESTRING>(da->c_str())) ||
		    !FPDFAnnot_SetAP(
		        note.get(), 0,
		        lines ? reinterpret_cast<FPDF_WIDESTRING>(lines->stream.c_str())
		              : nullptr)) {
			return {};
		}
		note.reset();
		model.reset();
		page.reset();
		PagePtr rendered(
		    FPDF_LoadPage(scratch.document->m_handle, target.page));
		BitmapPtr bitmap(FPDFBitmap_Create(64, 64, 1));
		if (!rendered || !bitmap || stopped()) {
			return {};
		}
		FPDFBitmap_FillRect(bitmap.get(), 0, 0, 64, 64, 0xFFFFFFFF);
		FPDF_RenderPageBitmap(bitmap.get(), rendered.get(), 0, 0, 64, 64, 0,
		                      FPDF_ANNOT);
		note.reset(FPDFPage_GetAnnot(rendered.get(), target.index));
		model.reset(FPDFPage_GetAnnot(rendered.get(), *created));
		if (lines
		        ? !matchesMultilineBoxAppearance(note.get(), rect, *lines) ||
		              !matchesMultilineBoxAppearance(model.get(), rect, *lines)
		        : !equivalent(scratch.document->m_handle, rendered.get(),
		                      note.get(), model.get(), false)) {
			return {};
		}
		if (!lines) {
			appearance = *read(note.get(), nullptr, 0);
		}
		model.reset();
		note.reset();
		if (!FPDFPage_RemoveAnnot(rendered.get(), *created)) {
			return {};
		}
	}
	scratch.document->invalidateEdits();
	auto next = scratch.save();
	if (!next.document) {
		return {};
	}
	const auto counts = editCounts();
	if (!counts || !next.document->checkEditCounts(*counts, {target.page})) {
		return {};
	}
	const auto kept = next.document->annotations(target.page, canceled);
	if (!kept || kept->limited) {
		return {};
	}
	const auto after = std::find_if(
	    kept->entries.begin(), kept->entries.end(),
	    [&](const auto& note) { return note.index == target.index; });
	if (after == kept->entries.end() || !after->bounds ||
	    after->contents != utf16ToUtf8(std::u16string(contents)) ||
	    after->author != expected.author || after->flags != expected.flags ||
	    after->bounds->left != destination.left ||
	    after->bounds->bottom != destination.bottom ||
	    after->bounds->right != destination.right ||
	    after->bounds->top != destination.top) {
		return {};
	}
	{
		PagePtr page(FPDF_LoadPage(next.document->m_handle, target.page));
		AnnotationPtr note(FPDFPage_GetAnnot(page.get(), target.index));
		if (lines ? !matchesMultilineBoxAppearance(note.get(), rect, *lines)
		          : read(note.get(), nullptr, 0) != appearance) {
			return {};
		}
	}
	if (!scratch.commit(std::move(next.document))) {
		return {};
	}
	return true;
}

std::optional<int> Document::addTextBoxAt(
    int index, double x, double y, int rotation, std::u16string_view contents,
    std::u16string_view author, std::uint64_t revision,
    const std::function<bool()>& canceled, TextBoxLayout layout) {
	if (offThread() || index < 0 || index >= pageCount() || !std::isfinite(x) ||
	    !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1 || rotation < 0 ||
	    rotation > 3 || (canceled && canceled())) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	FS_RECTF box{};
	double px = 0, py = 0;
	constexpr int GRID = 1 << 20;
	if (!page || !FPDF_GetPageBoundingBox(page.get(), &box) ||
	    !std::isfinite(box.left) || !std::isfinite(box.bottom) ||
	    !std::isfinite(box.right) || !std::isfinite(box.top) ||
	    double(box.right) - box.left < 4 || double(box.top) - box.bottom < 4 ||
	    !FPDF_DeviceToPage(page.get(), 0, 0, GRID, GRID, rotation,
	                       static_cast<int>(std::lround(x * GRID)),
	                       static_cast<int>(std::lround(y * GRID)), &px, &py) ||
	    !std::isfinite(px) || !std::isfinite(py)) {
		return {};
	}
	// @todo: expose the default text-box dimensions with resize controls.
	const double width = (std::min)(200., double(box.right) - box.left);
	const double height = (std::min)(60., double(box.top) - box.bottom);
	const double left =
	    std::clamp(px - width / 2, double(box.left), double(box.right) - width);
	const double bottom = std::clamp(py - height / 2, double(box.bottom),
	                                 double(box.top) - height);
	const AnnotationBounds bounds{
	    static_cast<float>(left), static_cast<float>(bottom),
	    static_cast<float>(left + width), static_cast<float>(bottom + height)};
	page.reset();
	return addTextBox(index, bounds, contents, author, revision, canceled,
	                  layout);
}

bool Document::currentRevision(std::uint64_t revision) const {
	// i accept old callers only until the first index-changing edit.
	return revision == m_annotationRevision ||
	       (revision == 0 && m_annotationRevision == 1);
}

std::uint64_t Document::annotationRevision() const {
	return offThread() ? 0 : m_annotationRevision;
}

std::optional<std::vector<int>> Document::editCounts() const {
	if (!m_editCounts) {
		m_editCounts = annotationCounts(m_handle, m_editPageLoads);
	}
	return m_editCounts;
}

bool Document::checkEditCounts(const std::vector<int>& expected,
                               const std::vector<int>& pages) const {
	const bool independent =
	    std::all_of(pages.begin(), pages.end(), [this](int page) {
		    return m_independentPages.contains(page);
	    });
	if (!independent) {
		// an empty shared array has no dictionary identity to inspect yet.
		if (!matchesAnnotationCounts(m_handle, expected, m_editPageLoads)) {
			return false;
		}
	} else {
		for (const int index : pages) {
			++m_editPageLoads;
			PagePtr page(FPDF_LoadPage(m_handle, index));
			if (!page ||
			    FPDFPage_GetAnnotCount(page.get()) != expected[index]) {
				return false;
			}
		}
	}
	m_editCounts = expected;
	m_independentPages.insert(pages.begin(), pages.end());
	return true;
}

void Document::invalidateEdits() {
	m_formDrawing.reset();
	m_formDrawingRevision = 0;
	m_serializable.reset();
	m_editCounts.reset();
	m_independentPages.clear();
	m_uniqueMarkup.clear();
	m_identityPages.clear();
	m_linkedPages.reset();
}

bool Document::canSerializeEdits(const std::function<bool()>& canceled) const {
	if (offThread() || (canceled && canceled())) {
		return false;
	}
	if (m_serializable) {
		return *m_serializable;
	}
	const auto refuse = [this] {
		m_serializable = false;
		return false;
	};
	// @todo: preserve empty attachment streams before allowing rewrites.
	if (FPDFDoc_GetAttachmentCount(m_handle) != 0) {
		return refuse();
	}
	const int pages = pageCount();
	if (pages < 0) {
		return refuse();
	}
	std::vector<int> counts(pages), linkedPages;
	for (int index = 0; index < pages; ++index) {
		if (canceled && canceled()) {
			return false;
		}
		++m_editPageLoads;
		PagePtr page(FPDF_LoadPage(m_handle, index));
		const int count = page ? FPDFPage_GetAnnotCount(page.get()) : -1;
		if (count < 0 || count > 4096) {
			return refuse();
		}
		counts[index] = count;
		bool linked = false;
		for (int i = 0; i < count; ++i) {
			if (canceled && canceled()) {
				return false;
			}
			AnnotationPtr note(FPDFPage_GetAnnot(page.get(), i));
			if (!note ||
			    FPDFAnnot_GetSubtype(note.get()) == FPDF_ANNOT_FILEATTACHMENT) {
				return refuse();
			}
			linked = linked || FPDFAnnot_HasKey(note.get(), "Popup") ||
			         FPDFAnnot_HasKey(note.get(), "IRT") ||
			         FPDFAnnot_HasKey(note.get(), "Parent");
		}
		if (linked) {
			linkedPages.push_back(index);
		}
	}
	m_linkedPages = std::move(linkedPages);
	m_editCounts = std::move(counts);
	m_serializable = true;
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
	const auto bytes = serializeForCopy(limit, stopped);
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
	return serializeOutput(limit, canceled);
}

std::optional<std::vector<std::uint8_t>>
Document::serializeForCopy(std::size_t limit,
                           const std::function<bool()>& canceled) const {
	// an untouched file is copied byte for byte, whoever wrote its notes.
	// a rewrite needs no masking: the notes i add or edit already store
	// NoView without Print, and other apps' notes keep their flags.
	return serializeOutput(limit, canceled);
}

std::optional<std::vector<std::uint8_t>>
Document::serializeOutput(std::size_t limit,
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
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/edit/cpdf_creator.cpp
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
	return pageText(page.get(), text.get());
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
	if (formDrawingDocument()) {
		if (width > 100'000 || height > 100'000 ||
		    static_cast<long long>(x) + width >
		        (std::numeric_limits<int>::max)() ||
		    static_cast<long long>(y) + height >
		        (std::numeric_limits<int>::max)()) {
			return false;
		}
		for (int top = 0; top < height; top += 4096) {
			for (int left = 0; left < width; left += 4096) {
				const auto tile = printRaster(index, width, height, left, top,
				                              (std::min)(4096, width - left),
				                              (std::min)(4096, height - top));
				if (!tile) {
					return false;
				}
				BITMAPINFO info{};
				info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				info.bmiHeader.biWidth = tile->width;
				info.bmiHeader.biHeight = -tile->height;
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = 32;
				// i transfer top-down opaque tiles, without a page-sized
				// bitmap. see:
				// https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-stretchdibits
				const int lines = StretchDIBits(
				    static_cast<HDC>(device), x + left, y + top, tile->width,
				    tile->height, 0, 0, tile->width, tile->height,
				    tile->pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
				if (!lines || lines == static_cast<int>(GDI_ERROR)) {
					return false;
				}
			}
		}
		return true;
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return false;
	}
	detail::NoteVisibility notes;
	return notes.collect(page.get()) && notes.run([&] {
		return FPDF_RenderPage(static_cast<HDC>(device), page.get(), x, y,
		                       width, height, 0,
		                       FPDF_ANNOT | FPDF_PRINTING) != 0;
	});
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

// see:
// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/public/fpdf_attachment.h
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

std::optional<pdf::Properties>
pdf::Document::properties(const std::function<bool()>& canceled) const {
	if (offThread() || (canceled && canceled())) {
		return std::nullopt;
	}
	Properties result;
	result.bytes = m_data.size();
	result.pages = FPDF_GetPageCount(m_handle);
	result.encrypted = FPDF_GetSecurityHandlerRevision(m_handle) >= 0;
	int version = 0;
	if (FPDF_GetFileVersion(m_handle, &version)) {
		result.version = version;
	}
	const std::pair<const char*, std::string*> fields[] = {
	    {"Title", &result.title},          {"Author", &result.author},
	    {"Subject", &result.subject},      {"Keywords", &result.keywords},
	    {"Creator", &result.creator},      {"Producer", &result.producer},
	    {"CreationDate", &result.created}, {"ModDate", &result.modified}};
	for (const auto& [tag, value] : fields) {
		if (canceled && canceled()) {
			return std::nullopt;
		}
		const auto bytes = FPDF_GetMetaText(m_handle, tag, nullptr, 0);
		if (bytes <= 2) {
			continue;
		}
		if (bytes > 128 * 1024 || bytes % 2) {
			result.limited = true;
			continue;
		}
		std::vector<unsigned char> buffer(bytes);
		if (FPDF_GetMetaText(m_handle, tag, buffer.data(), bytes) != bytes) {
			result.limited = true;
			continue;
		}
		buffer.resize(bytes - 2);
		*value = utf16ToUtf8(buffer);
	}
	// The availability API's file length is 32-bit on Windows. Larger files
	// still expose their other properties, with unknown linearization.
	if (m_data.size() <= (std::numeric_limits<unsigned long>::max)()) {
		struct Available : FX_FILEAVAIL {
			std::size_t length;
			explicit Available(std::size_t size) : length(size) {
				version = 1;
				IsDataAvail = [](FX_FILEAVAIL* base, std::size_t offset,
				                 std::size_t count) -> FPDF_BOOL {
					const auto total = static_cast<Available*>(base)->length;
					return offset <= total && count <= total - offset;
				};
			}
		} available(m_data.size());
		FPDF_FILEACCESS file{};
		file.m_FileLen = static_cast<unsigned long>(m_data.size());
		file.m_Param = const_cast<std::vector<std::uint8_t>*>(&m_data);
		file.m_GetBlock = [](void* data, unsigned long offset,
		                     unsigned char* destination, unsigned long count) {
			const auto& bytes =
			    *static_cast<const std::vector<std::uint8_t>*>(data);
			if (offset > bytes.size() || count > bytes.size() - offset) {
				return 0;
			}
			std::memcpy(destination, bytes.data() + offset, count);
			return 1;
		};
		if (const auto availability = FPDFAvail_Create(&available, &file)) {
			const auto status = FPDFAvail_IsLinearized(availability);
			FPDFAvail_Destroy(availability);
			if (status != PDF_LINEARIZATION_UNKNOWN) {
				result.linearized = status == PDF_LINEARIZED;
			}
		}
	}
	if (canceled && canceled()) {
		return std::nullopt;
	}
	return result;
}
