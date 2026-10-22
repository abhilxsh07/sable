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
		copy.document->m_rewriteCount = m_rewriteCount;
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
	const auto bytes = canonicalBytes(stopped);
	if (bytes && !stopped()) {
		auto opened = reopenFormCopy(*bytes);
		result.document = std::move(opened.document);
		if (result.document) {
			result.document->m_dataCanonical = true;
		}
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
	if (document->m_dataCanonical) {
		return true;
	}
	const auto stop = [this] { return stopped(); };
	const auto bytes = document->canonicalBytes(stop);
	if (!bytes || stopped()) {
		return false;
	}
	auto opened = source.reopenFormCopy(*bytes);
	document = std::move(opened.document);
	if (document) {
		document->m_dataCanonical = true;
	}
	return document && !stopped();
}

OpenResult Document::Transaction::save() {
	if (!document || stopped()) {
		return {};
	}
	document->m_modified = true;
	const auto stop = [this] { return stopped(); };
	auto bytes = document->serializeOutput(EDIT_LIMIT, stop, true);
	if (!bytes || stopped()) {
		return {};
	}
	auto next = source.reopenFormCopy(std::move(*bytes));
	const int expected = pages >= 0 ? pages : source.pageCount();
	if (!next.document || next.document->pageCount() != expected || stopped()) {
		return {};
	}
	next.document->m_dataCanonical = true;
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
	source.m_dataCanonical = true;
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

bool Document::hasOutline() const {
	return !offThread() && FPDFBookmark_GetFirstChild(m_handle, nullptr);
}

bool Document::hasAttachments() const {
	return !offThread() && FPDFDoc_GetAttachmentCount(m_handle) > 0;
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
	// i refuse an oversized file up front instead of rewriting it first.
	if (m_data.size() > EDIT_LIMIT) {
		m_writeRefusal = WriteRefusal::TooLarge;
		return *m_writeRefusal;
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
	const auto canonical = canonicalBytes(stopped);
	if (!canonical && m_overLimit && !stopped()) {
		// expanded object streams can push a smaller file past the limit.
		m_writeRefusal = WriteRefusal::TooLarge;
		return *m_writeRefusal;
	}
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

WriteRefusal
Document::editEligibility(const std::function<bool()>& canceled) const {
	const auto refusal = canWrite(canceled);
	return refusal != WriteRefusal::None ? refusal
	       : canSerializeEdits(canceled) ? WriteRefusal::None
	                                     : WriteRefusal::Unavailable;
}

const std::vector<std::uint8_t>*
Document::canonicalBytes(const std::function<bool()>& canceled) const {
	if (offThread() || (canceled && canceled())) {
		return nullptr;
	}
	if (m_dataCanonical) {
		return &m_data;
	}
	if (!m_canonical) {
		auto bytes = serializeOutput(EDIT_LIMIT, canceled, true);
		if (!bytes || (canceled && canceled())) {
			return nullptr;
		}
		m_canonical = std::move(*bytes);
	}
	return &*m_canonical;
}

void Document::invalidateWriteCopy() const {
	// the verdict outlives edits: nothing i write adds a signature, DocMDP
	// or /Perms, and the token reader skips strings and streams.
	m_canonical.reset();
	m_dataCanonical = false;
}

WriteRefusal Document::writeRefusal() const {
	return offThread() ? WriteRefusal::None
	                   : m_writeRefusal.value_or(WriteRefusal::None);
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
	invalidateWriteCopy();
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
                          const std::function<bool()>& canceled,
                          bool rewrite) const {
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
				if (size > self.limit - self.bytes.size()) {
					self.overflow = true;
				}
				if (self.overflow || (size && !data)) {
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
		bool failed = false, overflow = false;
	} writer(limit, canceled);
	if (writer.stopped()) {
		return {};
	}
	if (!rewrite && (!m_modified || m_dataCanonical || m_canonical)) {
		const auto& bytes =
		    !m_modified || m_dataCanonical ? m_data : *m_canonical;
		if (bytes.size() > limit) {
			return {};
		}
		writer.bytes = bytes;
	} else {
		if (!canSerializeEdits([&writer] { return writer.stopped(); })) {
			return {};
		}
		++*m_rewriteCount;
		const bool saved = FPDF_SaveAsCopy(m_handle, &writer, 0);
		m_overLimit = writer.overflow;
		if (!saved) {
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
