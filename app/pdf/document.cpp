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

bool onDisplayedPage(AnnotationPoint point) {
	return point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1;
}

bool displayedAnnotationPoint(FPDF_PAGE page, int rotation,
                              AnnotationPoint point, double& x, double& y) {
	constexpr int grid = 1 << 20;
	return FPDF_DeviceToPage(page, 0, 0, grid, grid, rotation,
	                         static_cast<int>(std::lround(point.x * grid)),
	                         static_cast<int>(std::lround(point.y * grid)), &x,
	                         &y) &&
	       std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 1000000. &&
	       std::abs(y) <= 1000000.;
}

struct BoxProofRegion {
	int pageWidth, pageHeight, left, top, width, height, objects;
};

std::optional<BoxProofRegion> boxProofRegion(FPDF_PAGE page,
                                             const FS_RECTF& rect) {
	const auto pageWidth = std::ceil(2. * FPDF_GetPageWidthF(page));
	const auto pageHeight = std::ceil(2. * FPDF_GetPageHeightF(page));
	if (!(pageWidth >= 1 && pageWidth <= 32768 && pageHeight >= 1 &&
	      pageHeight <= 32768)) {
		return {};
	}
	const int w = static_cast<int>(pageWidth), h = static_cast<int>(pageHeight);
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	if (!FPDF_PageToDevice(page, 0, 0, w, h, 0, rect.left, rect.top, &x0,
	                       &y0) ||
	    !FPDF_PageToDevice(page, 0, 0, w, h, 0, rect.right, rect.bottom, &x1,
	                       &y1) ||
	    (std::min)(x0, x1) < -1 || (std::min)(y0, y1) < -1 ||
	    (std::max)(x0, x1) > w + 1 || (std::max)(y0, y1) > h + 1) {
		return {};
	}
	const int width = std::abs(x1 - x0) + 4, height = std::abs(y1 - y0) + 4;
	const int left = (std::min)(x0, x1) - 2, top = (std::min)(y0, y1) - 2;
	const int objects = FPDFPage_CountObjects(page);
	if (static_cast<long long>(width) * height > 4 * 1024 * 1024 ||
	    objects < 0 || objects > 4096) {
		return {};
	}
	return BoxProofRegion{w, h, left, top, width, height, objects};
}

// i compare the old stream's actual drawing on a throwaway page. its
// appearance dictionary can affect placement even when the bytes match.
bool matchesBoxRendering(FPDF_PAGE page, int index, const FS_RECTF& rect,
                         const std::u16string& stream,
                         const std::function<bool()>& stopped) {
	const auto region = boxProofRegion(page, rect);
	const int annotations = FPDFPage_GetAnnotCount(page);
	if (!region || index < 0 || index >= annotations || annotations > 4096) {
		return false;
	}
	const auto [w, h, left, top, width, height, objects] = *region;
	for (int i = objects - 1; i >= 0; --i) {
		const auto object = FPDFPage_GetObject(page, i);
		if (stopped() || !object || !FPDFPage_RemoveObject(page, object)) {
			return false;
		}
		FPDFPageObj_Destroy(object);
	}
	for (int i = annotations - 1; i >= 0; --i) {
		if (stopped() || (i != index && !FPDFPage_RemoveAnnot(page, i))) {
			return false;
		}
	}
	const auto render = [&] {
		BitmapPtr bitmap(FPDFBitmap_Create(width, height, 1));
		if (bitmap && !stopped()) {
			FPDFBitmap_FillRect(bitmap.get(), 0, 0, width, height, 0xFFFFFFFF);
			FPDF_RenderPageBitmap(bitmap.get(), page, -left, -top, w, h, 0,
			                      FPDF_ANNOT);
		}
		return bitmap;
	};
	const auto before = render();
	AnnotationPtr note(FPDFPage_GetAnnot(page, 0));
	if (!before || stopped() ||
	    !FPDFAnnot_SetAP(note.get(), 0,
	                     reinterpret_cast<FPDF_WIDESTRING>(stream.c_str()))) {
		return false;
	}
	note.reset();
	const auto after = render();
	if (!after || stopped()) {
		return false;
	}
	const auto* a =
	    static_cast<const std::uint8_t*>(FPDFBitmap_GetBuffer(before.get()));
	const auto* b =
	    static_cast<const std::uint8_t*>(FPDFBitmap_GetBuffer(after.get()));
	const auto stride = FPDFBitmap_GetStride(before.get());
	if (!a || !b || stride != FPDFBitmap_GetStride(after.get())) {
		return false;
	}
	bool ink = false;
	for (int y = 0; y < height; ++y) {
		const auto* row = b + y * stride;
		if (stopped() || std::memcmp(a + y * stride, row, width * 4u)) {
			return false;
		}
		ink = ink ||
		      std::any_of(row, row + width * 4, [](auto c) { return c < 255; });
	}
	return ink;
}

// i only inspect this page; callbacks cannot run actions or start timers.
struct FormContext : FPDF_FORMFILLINFO {
	FormContext(FPDF_DOCUMENT initialOwner, int initialIndex)
	    : FPDF_FORMFILLINFO{}, document(initialOwner), pageIndex(initialIndex) {
		version = 1;
		FFI_Invalidate = [](FPDF_FORMFILLINFO*, FPDF_PAGE, double, double,
		                    double, double) {};
		FFI_SetCursor = [](FPDF_FORMFILLINFO*, int) {};
		FFI_SetTimer = [](FPDF_FORMFILLINFO*, int, TimerCallback) { return 0; };
		FFI_KillTimer = [](FPDF_FORMFILLINFO*, int) {};
		FFI_GetLocalTime = [](FPDF_FORMFILLINFO*) { return FPDF_SYSTEMTIME{}; };
		FFI_GetPage = [](FPDF_FORMFILLINFO* base, FPDF_DOCUMENT owner,
		                 int index) -> FPDF_PAGE {
			const auto& self = *static_cast<FormContext*>(base);
			return owner == self.document && index == self.pageIndex
			           ? self.page.get()
			           : nullptr;
		};
		FFI_GetRotation = [](FPDF_FORMFILLINFO*, FPDF_PAGE loadedPage) {
			return FPDFPage_GetRotation(loadedPage);
		};
		FFI_ExecuteNamedAction = [](FPDF_FORMFILLINFO*, FPDF_BYTESTRING) {};
	}
	void afterLoad() {
		FORM_OnAfterLoadPage(page.get(), handle);
		notified = true;
	}
	~FormContext() {
		if (page && handle && notified) {
			FORM_OnBeforeClosePage(page.get(), handle);
		}
		page.reset();
		FPDFDOC_ExitFormFillEnvironment(handle);
	}
	FPDF_DOCUMENT document;
	int pageIndex;
	PagePtr page;
	FPDF_FORMHANDLE handle = nullptr;
	bool notified = false;
};
struct TextCloser {
	void operator()(fpdf_textpage_t__* text) const {
		FPDFText_ClosePage(text);
	}
};
using TextPtr = std::unique_ptr<fpdf_textpage_t__, TextCloser>;

constexpr int TEXT_GRID = 1 << 20;

std::optional<TextPage> pageText(FPDF_PAGE page, FPDF_TEXTPAGE text) {
	const int count = FPDFText_CountChars(text);
	TextPage result;
	result.width = FPDF_GetPageWidthF(page);
	result.height = FPDF_GetPageHeightF(page);
	if (count < 0 || count > 200'000 || !std::isfinite(result.width) ||
	    !std::isfinite(result.height) || result.width <= 0 ||
	    result.height <= 0) {
		return {};
	}
	result.characters.reserve(count);
	const auto point = [&](double x, double y, int& dx, int& dy) {
		return FPDF_PageToDevice(page, 0, 0, TEXT_GRID, TEXT_GRID, 0, x, y, &dx,
		                         &dy);
	};
	for (int i = 0; i < count; ++i) {
		TextChar character;
		character.unicode = FPDFText_GetUnicode(text, i);
		FS_RECTF box{};
		if (FPDFText_GetLooseCharBox(text, i, &box) &&
		    std::isfinite(box.left) && std::isfinite(box.right) &&
		    std::isfinite(box.top) && std::isfinite(box.bottom)) {
			int ax = 0, ay = 0, bx = 0, by = 0;
			if (point(box.left, box.top, ax, ay) &&
			    point(box.right, box.bottom, bx, by)) {
				character.box = {
				    std::clamp((std::min)(ax, bx) / double(TEXT_GRID), 0., 1.),
				    std::clamp((std::min)(ay, by) / double(TEXT_GRID), 0., 1.),
				    std::clamp((std::max)(ax, bx) / double(TEXT_GRID), 0., 1.),
				    std::clamp((std::max)(ay, by) / double(TEXT_GRID), 0., 1.)};
			}
		}
		double x = 0, y = 0;
		FS_MATRIX matrix{};
		if (FPDFText_GetCharOrigin(text, i, &x, &y) && std::isfinite(x) &&
		    std::isfinite(y) && FPDFText_GetMatrix(text, i, &matrix) &&
		    std::isfinite(matrix.a) && std::isfinite(matrix.b)) {
			const double length = std::hypot(matrix.a, matrix.b);
			int ax = 0, ay = 0, bx = 0, by = 0;
			if (length > 0 && point(x, y, ax, ay) &&
			    point(x + matrix.a / length, y + matrix.b / length, bx, by)) {
				const double dx = (double(bx) - ax) * result.width;
				const double dy = (double(by) - ay) * result.height;
				const double displayed = std::hypot(dx, dy);
				if (displayed > 0) {
					const auto& b = character.box;
					const double height =
					    std::abs(dx) >= std::abs(dy)
					        ? (b.bottom - b.top) * result.height
					        : (b.right - b.left) * result.width;
					character.baseline = {
					    ax / double(TEXT_GRID), ay / double(TEXT_GRID),
					    dx / displayed, dy / displayed, height};
				}
			}
		}
		result.characters.push_back(character);
	}
	return result;
}

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

std::optional<std::vector<int>> annotationCounts(FPDF_DOCUMENT document,
                                                 std::size_t& pageLoads) {
	if (offThread()) {
		return {};
	}
	const int pages = FPDF_GetPageCount(document);
	if (pages < 0) {
		return {};
	}
	std::vector<int> counts(pages);
	for (int index = 0; index < pages; ++index) {
		++pageLoads;
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
                             const std::vector<int>& expected,
                             std::size_t& pageLoads) {
	if (offThread() ||
	    FPDF_GetPageCount(document) != static_cast<int>(expected.size())) {
		return false;
	}
	for (int index = 0; index < static_cast<int>(expected.size()); ++index) {
		++pageLoads;
		PagePtr page(FPDF_LoadPage(document, index));
		if (!page || FPDFPage_GetAnnotCount(page.get()) != expected[index]) {
			return false;
		}
	}
	return true;
}

bool uniqueMarkup(FPDF_DOCUMENT document, FPDF_PAGE own, int pageIndex,
                  int index, FPDF_ANNOTATION target, std::size_t& pageLoads) {
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
		++pageLoads;
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

std::optional<AnnotationColor> markupColor(HighlightColor color) {
	switch (color) {
	case HighlightColor::Yellow:
		return AnnotationColor{255, 240, 102, 255};
	case HighlightColor::Green:
		return AnnotationColor{125, 240, 102, 255};
	case HighlightColor::LightBlue:
		return AnnotationColor{143, 222, 249, 255};
	case HighlightColor::Pink:
		return AnnotationColor{247, 153, 209, 255};
	case HighlightColor::Red:
		return AnnotationColor{235, 73, 73, 255};
	default:
		return {};
	}
}

bool unlinkedAnnotation(FPDF_DOCUMENT document, FPDF_PAGE own, int targetIndex,
                        FPDF_ANNOTATION target,
                        const std::vector<int>& linkedPages,
                        std::size_t& pageLoads) {
	for (const auto key : {"Popup", "IRT", "Parent", "RT"}) {
		if (FPDFAnnot_HasKey(target, key)) {
			return false;
		}
	}
	for (const int index : linkedPages) {
		++pageLoads;
		PagePtr page(FPDF_LoadPage(document, index));
		const int count = page ? FPDFPage_GetAnnotCount(page.get()) : -1;
		if (count < 0 || count > 4096) {
			return false;
		}
		for (int i = 0; i < count; ++i) {
			AnnotationPtr note(FPDFPage_GetAnnot(page.get(), i));
			if (!note) {
				return false;
			}
			// i leave references to this comment intact until i handle threads.
			for (const auto key : {"Popup", "IRT", "Parent"}) {
				if (FPDFAnnot_HasKey(note.get(), key)) {
					AnnotationPtr linked(
					    FPDFAnnot_GetLinkedAnnot(note.get(), key));
					if (!linked || FPDFPage_GetAnnotIndex(own, linked.get()) ==
					                   targetIndex) {
						return false;
					}
				}
			}
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

std::string utf16ToUtf8(std::u16string_view text) {
	std::vector<unsigned char> encoded;
	for (const auto unit : text) {
		encoded.push_back(static_cast<unsigned char>(unit & 0xFF));
		encoded.push_back(static_cast<unsigned char>(unit >> 8));
	}
	return utf16ToUtf8(encoded);
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

Document* Document::formDrawingDocument() const {
	if (offThread() || FPDF_GetFormType(m_handle) != FORMTYPE_ACRO_FORM) {
		return nullptr;
	}
	if (m_formDrawingRevision != m_annotationRevision) {
		m_formDrawing.reset();
		m_formDrawingRevision = m_annotationRevision;
		try {
			if (auto bytes = serialize(128 * 1024 * 1024)) {
				m_formDrawing = reopenFormCopy(std::move(*bytes)).document;
			}
		} catch (...) {
			// i keep the base page when an isolated copy cannot be made.
		}
	}
	return m_formDrawing.get();
}

void Document::drawFormFields(int index, void* bitmap, int x, int y, int width,
                              int height, int rotation, bool printing) const {
	const auto scratch = formDrawingDocument();
	if (!scratch) {
		return;
	}
	FormContext context(scratch->m_handle, index);
	context.handle =
	    FPDFDOC_InitFormFillEnvironment(context.document, &context);
	if (!context.handle) {
		return;
	}
	context.page.reset(FPDF_LoadPage(context.document, index));
	if (!context.page) {
		return;
	}
	PagePtr source(FPDF_LoadPage(m_handle, index));
	const int count = FPDFPage_GetAnnotCount(context.page.get());
	if (!source || count < 0 || count > 4096 ||
	    count != FPDFPage_GetAnnotCount(source.get())) {
		return;
	}
	for (int i = 0; i < count; ++i) {
		AnnotationPtr original(FPDFPage_GetAnnot(source.get(), i));
		AnnotationPtr copy(FPDFPage_GetAnnot(context.page.get(), i));
		if (!original || !copy) {
			return;
		}
		if (FPDFAnnot_GetSubtype(original.get()) != FPDF_ANNOT_WIDGET) {
			continue;
		}
		const int flags = FPDFAnnot_GetFlags(original.get());
		const bool visible =
		    !(flags & (FPDF_ANNOT_FLAG_INVISIBLE | FPDF_ANNOT_FLAG_HIDDEN)) &&
		    (!printing || !FPDFAnnot_HasKey(original.get(), "OC")) &&
		    (printing ? (flags & FPDF_ANNOT_FLAG_PRINT) != 0
		              : (flags & FPDF_ANNOT_FLAG_NOVIEW) == 0);
		// i reapply live flags each draw; a print mask cannot leak onscreen.
		// the pinned form renderer ignores FPDF_PRINTING. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_formfill.cpp
		if (!FPDFAnnot_SetFlags(copy.get(),
		                        visible ? flags & ~FPDF_ANNOT_FLAG_NOVIEW
		                                : flags | FPDF_ANNOT_FLAG_HIDDEN)) {
			return;
		}
	}
	context.afterLoad();
	// i draw widgets after page content with the same transform and flags.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/public/fpdf_formfill.h
	FPDF_FFLDraw(context.handle, static_cast<FPDF_BITMAP>(bitmap),
	             context.page.get(), x, y, width, height, rotation,
	             FPDF_ANNOT | (printing ? FPDF_PRINTING : 0));
}

bool Document::canCopy() const {
	return !offThread() && (FPDF_GetDocPermissions(m_handle) & 0x10) != 0;
}

bool Document::canAnnotate() const {
	return !offThread() && (FPDF_GetDocPermissions(m_handle) & 0x20) != 0;
}

bool Document::canFillForms() const {
	if (offThread()) {
		return false;
	}
	const auto permissions = FPDF_GetDocPermissions(m_handle);
	// table 22 reserves bit 9 before revision 3. see:
	// https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf
	return (permissions & 0x20) ||
	       (FPDF_GetSecurityHandlerRevision(m_handle) >= 3 &&
	        (permissions & 0x100));
}

std::optional<bool>
Document::updateTextField(int pageIndex, int index,
                          std::u16string_view expected,
                          std::u16string_view contents, std::uint64_t revision,
                          const std::function<bool()>& canceled) {
	return updateFormField(pageIndex, index, expected, contents, revision,
	                       canceled, {});
}

std::optional<bool>
Document::updateCheckBox(int pageIndex, int index, std::u16string_view expected,
                         bool checked, std::uint64_t revision,
                         const std::function<bool()>& canceled) {
	return updateFormField(pageIndex, index, expected, {}, revision, canceled,
	                       checked);
}

std::optional<bool>
Document::updateRadioButton(int pageIndex, int index,
                            std::u16string_view expected,
                            const FormControl& control, std::uint64_t revision,
                            const std::function<bool()>& canceled) {
	return updateFormField(pageIndex, index, expected, {}, revision, canceled,
	                       true, &control);
}

std::optional<bool>
Document::updateChoiceField(int page, const FormField& field, int option,
                            std::uint64_t revision,
                            const std::function<bool()>& canceled) {
	if ((field.type != FormFieldType::ComboBox &&
	     field.type != FormFieldType::ListBox) ||
	    !field.options || field.options->empty() ||
	    field.options->size() > 256 || option < -1 ||
	    (option == -1 && field.type != FormFieldType::ListBox) ||
	    (option >= 0 &&
	     static_cast<std::size_t>(option) >= field.options->size()) ||
	    std::any_of(
	        field.options->begin(), field.options->end(),
	        [](const auto& item) { return item.label.empty(); })) {
		return {};
	}
	return updateFormField(page, field.index, {}, {}, revision, canceled, {},
	                       nullptr, &field, option);
}

std::optional<bool> Document::updateFormField(
    int pageIndex, int index, std::u16string_view expected,
    std::u16string_view contents, std::uint64_t revision,
    const std::function<bool()>& canceled, std::optional<bool> checked,
    const FormControl* control, const FormField* choice, int choiceIndex) {
	const auto stopped = [&] { return canceled && canceled(); };
	const bool clearingChoice = choice && choiceIndex == -1;
	const auto type = choice    ? choice->type
	                  : control ? FormFieldType::RadioButton
	                  : checked ? FormFieldType::CheckBox
	                            : FormFieldType::Text;
	if (offThread() || !revision || !currentRevision(revision) ||
	    (control &&
	     (control->count < 1 || control->count > 256 || control->index < 0 ||
	      control->index >= control->count || !checked || !*checked)) ||
	    pageIndex < 0 || pageIndex >= pageCount() || index < 0 ||
	    !canFillForms() || !validNoteText(expected) ||
	    !validNoteText(contents) || stopped() ||
	    FPDF_GetSignatureCount(m_handle) != 0 || !canSerializeEdits(canceled)) {
		return {};
	}
	constexpr std::size_t limit = 128 * 1024 * 1024;
	auto bytes = serialize(limit, canceled);
	if (!bytes || stopped()) {
		return {};
	}
	auto scratch = reopenFormCopy(std::move(*bytes));
	if (!scratch.document ||
	    FPDF_GetFormType(scratch.document->m_handle) != FORMTYPE_ACRO_FORM) {
		return {};
	}
	// the signature counter misses nested fields. a full rewrite expands
	// object streams and writes decoded dictionary/name tokens. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/edit/cpdf_creator.cpp
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/parser/cpdf_name.cpp
	scratch.document->m_modified = true;
	auto canonical = scratch.document->serialize(limit, canceled);
	if (!canonical || stopped()) {
		return {};
	}
	const std::string_view tokens(
	    reinterpret_cast<const char*>(canonical->data()), canonical->size());
	const auto actionKey = [&](std::string_view key) {
		for (auto at = tokens.find(key); at != std::string_view::npos;
		     at = tokens.find(key, at + key.size())) {
			const auto end = at + key.size();
			if (end == tokens.size() ||
			    static_cast<unsigned char>(tokens[end]) <= 32 ||
			    std::string_view("()<>[]{}/%").find(tokens[end]) !=
			        std::string_view::npos) {
				return true;
			}
		}
		return false;
	};
	// i can't see every merged field's actions through its widget.
	if (tokens.find("/Sig") != std::string_view::npos ||
	    tokens.find("/DocMDP") != std::string_view::npos || actionKey("/A") ||
	    actionKey("/AA") || tokens.find("/JS") != std::string_view::npos ||
	    tokens.find("/JavaScript") != std::string_view::npos) {
		return {};
	}
	scratch = reopenFormCopy(std::move(*canonical));
	if (!scratch.document || stopped()) {
		return {};
	}
	const auto matchesChoice = [&](const FormField& field, bool after) {
		return field.index == index && field.type == type &&
		       field.name == choice->name && field.flags == choice->flags &&
		       field.annotationFlags == choice->annotationFlags &&
		       field.options &&
		       field.options->size() == choice->options->size() &&
		       std::equal(
		           field.options->begin(), field.options->end(),
		           choice->options->begin(), [&](const auto& a, const auto& b) {
			           return a.index == b.index && a.label == b.label &&
			                  a.selected ==
			                      (after ? a.index == choiceIndex : b.selected);
		           });
	};
	if (choice) {
		const auto before = scratch.document->formFields(pageIndex, canceled);
		if (!before || before->limited ||
		    std::none_of(before->entries.begin(), before->entries.end(),
		                 [&](const auto& field) {
			                 return matchesChoice(field, false) &&
			                        field.value == choice->value;
		                 })) {
			return {};
		}
	}
	// group members by position, kept for the reopened check.
	std::vector<int> members(control ? control->count : 0, -1);
	std::vector<std::u16string> exports(members.size());
	std::u16string replacement(contents);
	std::optional<FormField> inheritedTarget;
	constexpr int plainFlags = FPDF_FORMFLAG_REQUIRED | FPDF_FORMFLAG_NOEXPORT;
	// i keep DoNotSpellCheck on text fields. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/constants/form_flags.h
	constexpr int textPlainFlags = plainFlags | (1 << 22);
	// Sort is for writers; i keep the stored option order. see:
	// https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf
	constexpr int choiceFlags = 1 << 19;
	const auto clearedChoice = [](FPDF_ANNOTATION widget) {
		return widget && !FPDFAnnot_HasKey(widget, "Parent") &&
		       !FPDFAnnot_HasKey(widget, "DV") &&
		       !FPDFAnnot_HasKey(widget, "V") && !FPDFAnnot_HasKey(widget, "I");
	};
	const auto inheritedWidget = [&](FPDF_FORMHANDLE form,
	                                 FPDF_ANNOTATION widget) {
		// i require the actual widget in the resolved field, not just its
		// name. count one also bounds appearance propagation. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp
		if (!widget || !FPDFAnnot_HasKey(widget, "Parent") ||
		    FPDFAnnot_GetFormFieldType(form, widget) !=
		        static_cast<int>(FormFieldType::Text) ||
		    FPDFAnnot_GetFormControlCount(form, widget) != 1 ||
		    FPDFAnnot_GetFormControlIndex(form, widget) != 0 ||
		    (FPDFAnnot_GetFormFieldFlags(form, widget) &
		     ~(textPlainFlags | FPDF_FORMFLAG_TEXT_MULTILINE))) {
			return false;
		}
		// i leave field data on the parent of this plain widget. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_interactiveform.cpp
		for (const auto key :
		     {"T", "FT", "Ff", "V", "DV", "Kids", "MaxLen", "RV", "DA", "Q",
		      "DS", "TU", "TM", "Opt", "I", "TI"}) {
			if (FPDFAnnot_HasKey(widget, key)) {
				return false;
			}
		}
		AnnotationPtr parent(FPDFAnnot_GetLinkedAnnot(widget, "Parent"));
		return !parent;
	};
	{
		FormContext context(scratch.document->m_handle, pageIndex);
		context.handle =
		    FPDFDOC_InitFormFillEnvironment(context.document, &context);
		if (!context.handle) {
			return {};
		}
		context.page.reset(FPDF_LoadPage(context.document, pageIndex));
		if (!context.page ||
		    index >= FPDFPage_GetAnnotCount(context.page.get())) {
			return {};
		}
		context.afterLoad();
		AnnotationPtr field(FPDFPage_GetAnnot(context.page.get(), index));
		// i keep one control unless this is an inspected radio group.
		// the pinned control-count implementation also handles text fields.
		// see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp
		constexpr int protectedFlags =
		    FPDF_ANNOT_FLAG_INVISIBLE | FPDF_ANNOT_FLAG_HIDDEN |
		    FPDF_ANNOT_FLAG_NOVIEW | FPDF_ANNOT_FLAG_READONLY |
		    FPDF_ANNOT_FLAG_LOCKED | 0x200;
		// radio flags have no SDK names: NoToggleToOff and Radio. unison
		// groups and the other button flags stay refused.
		constexpr int radioFlags = (1 << 14) | (1 << 15);
		const auto plainWidget = [&](FPDF_ANNOTATION annotation) {
			return FPDFAnnot_GetSubtype(annotation) == FPDF_ANNOT_WIDGET &&
			       !FPDFAnnot_HasKey(annotation, "A") &&
			       !FPDFAnnot_HasKey(annotation, "AA") &&
			       !(FPDFAnnot_GetFlags(annotation) & protectedFlags);
		};
		if (!field || !plainWidget(field.get()) ||
		    FPDFAnnot_GetFormFieldType(context.handle, field.get()) !=
		        static_cast<int>(type) ||
		    FPDFAnnot_GetFormControlCount(context.handle, field.get()) !=
		        (control ? control->count : 1) ||
		    (!control && type != FormFieldType::Text &&
		     FPDFAnnot_HasKey(field.get(), "Parent"))) {
			return {};
		}
		if (type == FormFieldType::Text &&
		    FPDFAnnot_HasKey(field.get(), "Parent")) {
			if (!inheritedWidget(context.handle, field.get())) {
				return {};
			}
			const auto fields =
			    scratch.document->formFields(pageIndex, canceled);
			if (!fields || fields->limited) {
				return {};
			}
			const auto target = std::find_if(
			    fields->entries.begin(), fields->entries.end(),
			    [index](const auto& item) { return item.index == index; });
			if (target == fields->entries.end() || target->name.empty() ||
			    std::count_if(fields->entries.begin(), fields->entries.end(),
			                  [&](const auto& item) {
				                  return item.name == target->name;
			                  }) != 1) {
				return {};
			}
			inheritedTarget = *target;
		}
		const int flags =
		    FPDFAnnot_GetFormFieldFlags(context.handle, field.get());
		const bool multiline = type == FormFieldType::Text &&
		                       (flags & FPDF_FORMFLAG_TEXT_MULTILINE);
		// the public header has no Comb constant. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/constants/form_flags.h
		constexpr int combFlag = 1 << 24;
		const bool comb = type == FormFieldType::Text && (flags & combFlag);
		if ((flags &
		     ~((type == FormFieldType::Text ? textPlainFlags : plainFlags) |
		       (control     ? radioFlags
		        : choice    ? choiceFlags | (type == FormFieldType::ComboBox
		                                         ? FPDF_FORMFLAG_CHOICE_COMBO
		                                         : 0)
		        : multiline ? FPDF_FORMFLAG_TEXT_MULTILINE
		        : comb      ? combFlag
		                    : 0))) ||
		    (!multiline &&
		     contents.find_first_of(u"\r\n") != std::u16string_view::npos)) {
			return {};
		}
		if (comb) {
			// i require a bounded cell count before opening the comb editor.
			// see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_textfield.cpp
			float cells = 0;
			if (!FPDFAnnot_GetNumberValue(field.get(), "MaxLen", &cells) ||
			    !std::isfinite(cells) || cells < 1 || cells > 32767 ||
			    std::floor(cells) != cells) {
				return {};
			}
		}
		const auto read = [&](FPDF_ANNOTATION annotation,
		                      auto getter) -> std::optional<std::u16string> {
			const auto size = getter(context.handle, annotation, nullptr, 0);
			if (size < 2 || size > 65536 || size % 2) {
				return {};
			}
			std::vector<FPDF_WCHAR> buffer(size / 2);
			if (getter(context.handle, annotation, buffer.data(), size) !=
			        size ||
			    buffer.back() != 0) {
				return {};
			}
			return std::u16string(buffer.begin(), buffer.end() - 1);
		};
		const auto value = [&](auto getter) {
			return read(field.get(), getter);
		};
		const auto previous = value(FPDFAnnot_GetFormFieldValue);
		const auto namedOf = [&](FPDF_ANNOTATION target, const char* key) {
			return read(target, [key](FPDF_FORMHANDLE,
			                          FPDF_ANNOTATION annotation,
			                          FPDF_WCHAR* buffer, unsigned long size) {
				return FPDFAnnot_GetStringValue(annotation, key, buffer, size);
			});
		};
		const auto named = [&](const char* key) {
			return namedOf(field.get(), key);
		};
		const auto checkedAppearance = [&](FPDF_ANNOTATION target) {
			// i keep checked appearances to text; this SDK can return
			// compressed bytes for empty streams. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/parser/cpdf_stream_acc.cpp
			const auto appearance =
			    read(target, [](FPDF_FORMHANDLE, FPDF_ANNOTATION annotation,
			                    FPDF_WCHAR* buffer, unsigned long size) {
				    return FPDFAnnot_GetAP(annotation,
				                           FPDF_ANNOT_APPEARANCEMODE_NORMAL,
				                           buffer, size);
			    });
			const std::u16string_view whitespace(u"\0\t\n\f\r ", 6);
			return appearance &&
			       appearance->find_first_not_of(whitespace) !=
			           std::u16string::npos &&
			       std::all_of(appearance->begin(), appearance->end(),
			                   [&](char16_t unit) {
				                   return (unit >= 32 && unit <= 126) ||
				                          whitespace.find(unit) !=
				                              std::u16string_view::npos;
			                   });
		};
		const auto hasCheckedAppearance = [&] {
			return checkedAppearance(field.get());
		};
		const bool terminal =
		    !control || !FPDFAnnot_HasKey(field.get(), "Parent");
		if (!previous ||
		    (choice ? utf16ToUtf8(*previous) != choice->value
		            : *previous != expected) ||
		    stopped()) {
			return {};
		}
		if (choice) {
			const auto selectedCount =
			    std::count_if(choice->options->begin(), choice->options->end(),
			                  [](const auto& item) { return item.selected; });
			if (!validNoteText(*previous) || selectedCount > 1 ||
			    (previous->empty() ? selectedCount != 0 : selectedCount != 1) ||
			    (clearingChoice && FPDFAnnot_HasKey(field.get(), "DV")) ||
			    FPDFAnnot_HasKey(field.get(), "Kids") || named("FT") != u"Ch" ||
			    FPDFAnnot_GetValueType(field.get(), "Opt") !=
			        FPDF_OBJECT_ARRAY ||
			    (FPDFAnnot_HasKey(field.get(), "V") &&
			     (FPDFAnnot_GetValueType(field.get(), "V") !=
			          FPDF_OBJECT_STRING ||
			      named("V") != previous))) {
				return {};
			}
		}
		if (checked && !control) {
			const auto on = value(FPDFAnnot_GetFormFieldExportValue);
			const bool current =
			    FPDFAnnot_IsChecked(context.handle, field.get());
			// i keep the value and appearance state on the same export name.
			if (!on || on->empty() || *on == u"Off" || !validNoteText(*on) ||
			    named("AS") != previous || named("V") != previous ||
			    *previous != (current ? *on : u"Off") ||
			    FPDFAnnot_GetValueType(field.get(), "V") != FPDF_OBJECT_NAME ||
			    FPDFAnnot_GetValueType(field.get(), "AS") != FPDF_OBJECT_NAME ||
			    FPDFAnnot_HasKey(field.get(), "Opt") ||
			    (current && !hasCheckedAppearance())) {
				return {};
			}
			replacement = *checked ? *on : u"Off";
		}
		if (control) {
			// i only write a group whose members are all on this page once;
			// otherwise clearing the old choice could reach a widget i never
			// checked.
			const auto name = value(FPDFAnnot_GetFormFieldName);
			const int count = FPDFPage_GetAnnotCount(context.page.get());
			if (!name || count < 0 || count > 4096 ||
			    (!FPDFAnnot_HasKey(field.get(), "Parent") &&
			     (control->count != 1 || FPDFAnnot_HasKey(field.get(), "Opt") ||
			      FPDFAnnot_GetValueType(field.get(), "V") !=
			          FPDF_OBJECT_NAME ||
			      named("V") != previous))) {
				return {};
			}
			// a parent field can hold scripts the linked-annotation API
			// can't open, but the form API reports them through inheritance.
			for (int event = FPDF_ANNOT_AACTION_KEY_STROKE;
			     event <= FPDF_ANNOT_AACTION_CALCULATE; ++event) {
				if (FPDFAnnot_GetFormAdditionalActionJavaScript(
				        context.handle, field.get(), event, nullptr, 0) != 2) {
					return {};
				}
			}
			std::optional<int> selected;
			for (int i = 0; i < count; ++i) {
				AnnotationPtr member(FPDFPage_GetAnnot(context.page.get(), i));
				if (!member || stopped()) {
					return {};
				}
				if (FPDFAnnot_GetSubtype(member.get()) != FPDF_ANNOT_WIDGET ||
				    FPDFAnnot_GetFormFieldType(context.handle, member.get()) !=
				        static_cast<int>(type) ||
				    read(member.get(), FPDFAnnot_GetFormFieldName) != name) {
					continue;
				}
				const int position =
				    FPDFAnnot_GetFormControlIndex(context.handle, member.get());
				const auto exported =
				    read(member.get(), FPDFAnnot_GetFormFieldExportValue);
				const auto state = namedOf(member.get(), "AS");
				const bool on =
				    FPDFAnnot_IsChecked(context.handle, member.get()) != 0;
				// duplicate exports can't say which control V selects.
				if (!plainWidget(member.get()) ||
				    FPDFAnnot_GetFormControlCount(
				        context.handle, member.get()) != control->count ||
				    position < 0 || position >= control->count ||
				    members[position] != -1 || !exported || exported->empty() ||
				    *exported == u"Off" || !validNoteText(*exported) ||
				    std::find(exports.begin(), exports.end(), *exported) !=
				        exports.end() ||
				    FPDFAnnot_GetValueType(member.get(), "AS") !=
				        FPDF_OBJECT_NAME ||
				    !state || (*state == u"Off") == on ||
				    (on && (selected || !checkedAppearance(member.get())))) {
					return {};
				}
				if (FPDFAnnot_HasKey(member.get(), "Parent")) {
					// kids stay plain widgets, not fields of their own.
					for (const char* key :
					     {"T", "FT", "Ff", "V", "DV", "Opt", "Kids"}) {
						if (FPDFAnnot_HasKey(member.get(), key)) {
							return {};
						}
					}
				} else if (control->count != 1) {
					return {};
				}
				members[position] = i;
				exports[position] = *exported;
				if (on) {
					selected = position;
				}
			}
			if (std::find(members.begin(), members.end(), -1) !=
			        members.end() ||
			    members[control->index] != index ||
			    utf16ToUtf8(exports[control->index]) != control->exportValue ||
			    *previous !=
			        (selected ? exports[*selected] : std::u16string(u"Off"))) {
				return {};
			}
			replacement = exports[control->index];
		}
		if (multiline && *previous != replacement) {
			// i keep raw no-ops, but match the editor's CRLF paragraphs.
			// see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/pwl/cpwl_edit_impl.cpp
			std::u16string normalized;
			normalized.reserve(replacement.size());
			for (std::size_t i = 0; i < replacement.size(); ++i) {
				const auto unit = replacement[i];
				if (unit == u'\r' || unit == u'\n') {
					normalized += u"\r\n";
					if (unit == u'\r' && i + 1 < replacement.size() &&
					    replacement[i + 1] == u'\n') {
						++i;
					}
				} else {
					normalized += unit;
				}
			}
			if (!validNoteText(normalized)) {
				return {};
			}
			replacement = std::move(normalized);
		}
		if (choice ? (clearingChoice ? previous->empty()
		                             : (*choice->options)[choiceIndex].selected)
		           : *previous == replacement) {
			return false;
		}
		// i commit through focus loss, then check the full value. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_textfield.cpp
		if (!FORM_SetFocusedAnnot(context.handle, field.get())) {
			return {};
		}
		if (choice) {
			// i save the focused choice through focus loss too. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_combobox.cpp
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_listbox.cpp
			int selected = choiceIndex;
			if (clearingChoice) {
				const auto found = std::find_if(
				    choice->options->begin(), choice->options->end(),
				    [](const auto& item) { return item.selected; });
				if (found == choice->options->end()) {
					return {};
				}
				selected = found->index;
			}
			if (!FORM_SetIndexSelected(context.handle, context.page.get(),
			                           selected, !clearingChoice)) {
				return {};
			}
		} else if (checked) {
			// space commits the focused checkbox. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_checkbox.cpp
			if (!FORM_OnChar(context.handle, context.page.get(), ' ', 0)) {
				return {};
			}
		} else {
			if (!previous->empty() &&
			    !FORM_SelectAllText(context.handle, context.page.get())) {
				return {};
			}
			FORM_ReplaceSelection(
			    context.handle, context.page.get(),
			    reinterpret_cast<FPDF_WIDESTRING>(replacement.c_str()));
		}
		if (!FORM_ForceToKillFocus(context.handle)) {
			return {};
		}
		if (choice) {
			const auto written = value(FPDFAnnot_GetFormFieldValue);
			// clearing removes V/I; DV would become the effective value.
			// see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_formfield.cpp
			if (!written || !validNoteText(*written) ||
			    (clearingChoice
			         ? !written->empty() || !clearedChoice(field.get())
			         : written->empty() || named("V") != written ||
			               FPDFAnnot_GetValueType(field.get(), "V") !=
			                   FPDF_OBJECT_STRING) ||
			    FPDFAnnot_GetOptionCount(context.handle, field.get()) !=
			        static_cast<int>(choice->options->size())) {
				return {};
			}
			replacement = *written;
			for (const auto& item : *choice->options) {
				if ((FPDFAnnot_IsOptionSelected(context.handle, field.get(),
				                                item.index) != 0) !=
				        (item.index == choiceIndex) ||
				    stopped()) {
					return {};
				}
			}
		}
		if (value(FPDFAnnot_GetFormFieldValue) != replacement ||
		    (checked && terminal &&
		     (named("V") != replacement || named("AS") != replacement)) ||
		    (checked && (FPDFAnnot_IsChecked(context.handle, field.get()) !=
		                 0) != *checked) ||
		    (checked && *checked && !hasCheckedAppearance()) || stopped()) {
			return {};
		}
		if (control) {
			for (int position = 0; position < control->count; ++position) {
				AnnotationPtr member(
				    FPDFPage_GetAnnot(context.page.get(), members[position]));
				const bool on = position == control->index;
				const auto state =
				    member ? namedOf(member.get(), "AS") : std::nullopt;
				if (!state || (*state == u"Off") == on ||
				    (FPDFAnnot_IsChecked(context.handle, member.get()) != 0) !=
				        on ||
				    stopped()) {
					return {};
				}
			}
		}
	}
	// i commit only saved, reopened bytes; failed edits never touch the live
	// PDF.
	scratch.document->m_modified = true;
	auto saved = scratch.document->serialize(limit, canceled);
	if (!saved || stopped()) {
		return {};
	}
	auto next = reopenFormCopy(std::move(*saved));
	if (!next.document || next.document->pageCount() != pageCount()) {
		return {};
	}
	if (inheritedTarget || clearingChoice) {
		// i check saved ownership and cleared keys on another copy,
		// keeping setup off live bytes.
		auto proof = reopenFormCopy(next.document->m_data);
		if (!proof.document || stopped()) {
			return {};
		}
		FormContext context(proof.document->m_handle, pageIndex);
		context.handle =
		    FPDFDOC_InitFormFillEnvironment(context.document, &context);
		if (!context.handle) {
			return {};
		}
		context.page.reset(FPDF_LoadPage(context.document, pageIndex));
		if (!context.page) {
			return {};
		}
		context.afterLoad();
		AnnotationPtr field(FPDFPage_GetAnnot(context.page.get(), index));
		if ((inheritedTarget &&
		     !inheritedWidget(context.handle, field.get())) ||
		    (clearingChoice && !clearedChoice(field.get())) || stopped()) {
			return {};
		}
	}
	const auto check = next.document->formFields(pageIndex, canceled);
	if (!check || check->limited ||
	    std::none_of(check->entries.begin(), check->entries.end(),
	                 [&](const FormField& field) {
		                 return field.index == index && field.type == type &&
		                        (!checked || field.checked == checked) &&
		                        (!choice || matchesChoice(field, true)) &&
		                        (!inheritedTarget ||
		                         (field.name == inheritedTarget->name &&
		                          field.flags == inheritedTarget->flags &&
		                          field.annotationFlags ==
		                              inheritedTarget->annotationFlags)) &&
		                        field.value == utf16ToUtf8(replacement);
	                 })) {
		return {};
	}
	if (control) {
		const auto shared = utf16ToUtf8(replacement);
		for (int position = 0; position < control->count; ++position) {
			if (stopped()) {
				return {};
			}
			const auto exported = utf16ToUtf8(exports[position]);
			if (std::none_of(
			        check->entries.begin(), check->entries.end(),
			        [&](const FormField& field) {
				        return field.index == members[position] &&
				               field.type == type &&
				               field.checked == (position == control->index) &&
				               field.value == shared && field.control &&
				               field.control->index == position &&
				               field.control->count == control->count &&
				               field.control->exportValue == exported;
			        })) {
				return {};
			}
		}
	}
	if (!m_outlineBookmarks.empty()) {
		if (!next.document->outline(canceled) ||
		    next.document->m_outlineBookmarks.size() !=
		        m_outlineBookmarks.size()) {
			return {};
		}
	}
	if (stopped()) {
		return {};
	}
	m_data.swap(next.document->m_data);
	std::swap(m_handle, next.document->m_handle);
	m_outlineBookmarks.swap(next.document->m_outlineBookmarks);
	invalidateEdits();
	++m_annotationRevision;
	m_modified = true;
	return true;
}

std::optional<int>
Document::addRectangle(int index, const AnnotationBounds& bounds,
                       std::uint64_t revision,
                       const std::function<bool()>& canceled) {
	return addOutlineShape(index, bounds, OutlineShape::Rectangle, revision,
	                       canceled);
}

std::optional<int> Document::addEllipse(int index,
                                        const AnnotationBounds& bounds,
                                        std::uint64_t revision,
                                        const std::function<bool()>& canceled) {
	return addOutlineShape(index, bounds, OutlineShape::Ellipse, revision,
	                       canceled);
}

std::optional<int>
Document::addOutlineShape(int index, const AnnotationBounds& bounds,
                          OutlineShape shape, std::uint64_t revision,
                          const std::function<bool()>& canceled) {
	const auto stopped = [&] { return canceled && canceled(); };
	if (offThread() || !revision || !currentRevision(revision) ||
	    revision == (std::numeric_limits<std::uint64_t>::max)() || index < 0 ||
	    index >= pageCount() || stopped() || !canAnnotate() ||
	    (FPDF_GetFormType(m_handle) != FORMTYPE_NONE &&
	     FPDF_GetFormType(m_handle) != FORMTYPE_ACRO_FORM) ||
	    FPDF_GetSignatureCount(m_handle) != 0) {
		return {};
	}
	for (float edge : {bounds.left, bounds.bottom, bounds.right, bounds.top}) {
		if (!std::isfinite(edge) || std::abs(edge) > 1000000.f) {
			return {};
		}
	}
	// @todo: expose the outline border width and color with shape tools.
	constexpr double border = 2;
	const double width = double(bounds.right) - bounds.left;
	const double height = double(bounds.top) - bounds.bottom;
	if (width <= 2 * border || height <= 2 * border ||
	    !canSerializeEdits(canceled)) {
		return {};
	}
	const auto& [left, bottom, right, top] = bounds;
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::fixed << std::setprecision(5) << "0 g ";
	if (shape == OutlineShape::Rectangle) {
		stream << left << ' ' << bottom << ' ' << width << ' ' << height
		       << " re " << left + border << ' ' << bottom + border << ' '
		       << width - 2 * border << ' ' << height - 2 * border << " re f*";
	} else {
		const double cx = left + width / 2, cy = bottom + height / 2;
		const auto contour = [&](double rx, double ry) {
			// four cubics meet each axis with the ellipse's tangent.
			constexpr double k = 0.5522847498307936;
			const double dx = rx * k, dy = ry * k;
			stream << cx + rx << ' ' << cy << " m " << cx + rx << ' ' << cy + dy
			       << ' ' << cx + dx << ' ' << cy + ry << ' ' << cx << ' '
			       << cy + ry << " c " << cx - dx << ' ' << cy + ry << ' '
			       << cx - rx << ' ' << cy + dy << ' ' << cx - rx << ' ' << cy
			       << " c " << cx - rx << ' ' << cy - dy << ' ' << cx - dx
			       << ' ' << cy - ry << ' ' << cx << ' ' << cy - ry << " c "
			       << cx + dx << ' ' << cy - ry << ' ' << cx + rx << ' '
			       << cy - dy << ' ' << cx + rx << ' ' << cy << " c h ";
		};
		contour(width / 2, height / 2);
		contour(width / 2 - border, height / 2 - border);
		stream << "f*";
	}
	const auto subtype = shape == OutlineShape::Rectangle ? FPDF_ANNOT_SQUARE
	                                                      : FPDF_ANNOT_CIRCLE;
	const auto raw = stream.str();
	return appendAnnotation(index, subtype, bounds,
	                        std::u16string(raw.begin(), raw.end()), {}, {},
	                        canceled);
}

std::optional<int> Document::addStamp(int index, const AnnotationBounds& bounds,
                                      const StampImage& image,
                                      std::uint64_t revision,
                                      const std::function<bool()>& canceled) {
	return writeStamp(index, bounds, image,
	                  {bounds.right - bounds.left, 0, 0,
	                   bounds.top - bounds.bottom, bounds.left, bounds.bottom},
	                  revision, canceled);
}

std::optional<int> Document::writeStamp(int index,
                                        const AnnotationBounds& bounds,
                                        const StampImage& image,
                                        const std::array<float, 6>& transform,
                                        std::uint64_t revision,
                                        const std::function<bool()>& canceled) {
	const auto& [left, bottom, right, top] = bounds;
	if (offThread() || !revision || !currentRevision(revision) ||
	    revision == (std::numeric_limits<std::uint64_t>::max)() || index < 0 ||
	    index >= pageCount() || !image.valid() ||
	    !(left >= -1000000.f && bottom >= -1000000.f && right <= 1000000.f &&
	      top <= 1000000.f && right - left >= 1 && top - bottom >= 1) ||
	    (canceled && canceled()) || !canAnnotate() ||
	    (FPDF_GetFormType(m_handle) != FORMTYPE_NONE &&
	     FPDF_GetFormType(m_handle) != FORMTYPE_ACRO_FORM) ||
	    FPDF_GetSignatureCount(m_handle) != 0 || !canSerializeEdits(canceled)) {
		return {};
	}
	const auto& [a, b, c, d, e, f] = transform;
	const FS_MATRIX matrix{a, b, c, d, e, f};
	const auto write = [&](FPDF_ANNOTATION note, FPDF_DOCUMENT document) {
		BitmapPtr bitmap(FPDFBitmap_Create(static_cast<int>(image.width),
		                                   static_cast<int>(image.height), 1));
		if (!bitmap) {
			return false;
		}
		const auto stride = 4 * image.width;
		if (FPDFBitmap_GetFormat(bitmap.get()) != FPDFBitmap_BGRA ||
		    FPDFBitmap_GetStride(bitmap.get()) != static_cast<int>(stride) ||
		    !FPDFBitmap_GetBuffer(bitmap.get())) {
			return false;
		}
		std::memcpy(FPDFBitmap_GetBuffer(bitmap.get()), image.bgra.data(),
		            image.bgra.size());
		std::unique_ptr<fpdf_pageobject_t__, decltype(&FPDFPageObj_Destroy)>
		    object(FPDFPageObj_NewImageObj(document), &FPDFPageObj_Destroy);
		if (!object ||
		    !FPDFImageObj_SetBitmap(nullptr, 0, object.get(), bitmap.get()) ||
		    !FPDFPageObj_SetMatrix(object.get(), &matrix) ||
		    !FPDFAnnot_AppendObject(note, object.get())) {
			return false;
		}
		object.release();
		return true;
	};
	const auto verify = [&](FPDF_ANNOTATION note) {
		if (FPDFAnnot_GetObjectCount(note) != 1) {
			return false;
		}
		const auto object = FPDFAnnot_GetObject(note, 0);
		unsigned width = 0, height = 0;
		FS_MATRIX kept{};
		const auto same = [](float a, float b) {
			return std::abs(a - b) < .001f;
		};
		return object && FPDFPageObj_GetType(object) == FPDF_PAGEOBJ_IMAGE &&
		       FPDFImageObj_GetImagePixelSize(object, &width, &height) &&
		       width == image.width && height == image.height &&
		       FPDFPageObj_GetMatrix(object, &kept) && same(kept.a, matrix.a) &&
		       same(kept.b, matrix.b) && same(kept.c, matrix.c) &&
		       same(kept.d, matrix.d) && same(kept.e, matrix.e) &&
		       same(kept.f, matrix.f);
	};
	return appendAnnotation(index, FPDF_ANNOT_STAMP, bounds, {}, write, verify,
	                        canceled);
}

// i write into a canonical scratch copy, then reopen the saved bytes and
// check the new annotation before replacing the live document.
std::optional<int> Document::appendAnnotation(
    int index, int subtype, const AnnotationBounds& bounds,
    const std::optional<std::u16string>& appearance,
    const std::function<bool(FPDF_ANNOTATION, FPDF_DOCUMENT)>& decorate,
    const std::function<bool(FPDF_ANNOTATION)>& verify,
    const std::function<bool()>& canceled) {
	const auto stopped = [&] { return canceled && canceled(); };
	const auto& [left, bottom, right, top] = bounds;
	const FS_RECTF rect{left, top, right, bottom};
	constexpr std::size_t limit = 128 * 1024 * 1024;
	auto bytes = serialize(limit, canceled);
	if (!bytes || stopped()) {
		return {};
	}
	auto scratch = reopenFormCopy(std::move(*bytes));
	if (!scratch.document || stopped()) {
		return {};
	}
	// i check expanded object streams for signature names before writing.
	scratch.document->m_modified = true;
	auto canonical = scratch.document->serialize(limit, canceled);
	if (!canonical || stopped()) {
		return {};
	}
	const std::string_view tokens(
	    reinterpret_cast<const char*>(canonical->data()), canonical->size());
	if (tokens.find("/Sig") != std::string_view::npos ||
	    tokens.find("/DocMDP") != std::string_view::npos) {
		return {};
	}
	scratch = reopenFormCopy(std::move(*canonical));
	if (!scratch.document || stopped()) {
		return {};
	}

	auto expected = editCounts();
	if (!expected) {
		return {};
	}
	const int added = (*expected)[index];
	++(*expected)[index];
	{
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, index));
		FS_RECTF box{};
		if (!page || !FPDF_GetPageBoundingBox(page.get(), &box) ||
		    !(left >= box.left && bottom >= box.bottom && right <= box.right &&
		      top <= box.top) ||
		    added >= 4096 || FPDFPage_GetAnnotCount(page.get()) != added) {
			return {};
		}
		AnnotationPtr note(FPDFPage_CreateAnnot(page.get(), subtype));
		if (!note || !FPDFAnnot_SetRect(note.get(), &rect) ||
		    !FPDFAnnot_SetFlags(note.get(), FPDF_ANNOT_FLAG_PRINT) ||
		    (decorate && !decorate(note.get(), scratch.document->m_handle)) ||
		    (appearance &&
		     !FPDFAnnot_SetAP(
		         note.get(), FPDF_ANNOT_APPEARANCEMODE_NORMAL,
		         reinterpret_cast<FPDF_WIDESTRING>(appearance->c_str())))) {
			return {};
		}
	}
	if (stopped() || !scratch.document->checkEditCounts(*expected, {index})) {
		return {};
	}
	scratch.document->m_modified = true;
	auto saved = scratch.document->serialize(limit, canceled);
	if (!saved || stopped()) {
		return {};
	}
	auto next = reopenFormCopy(std::move(*saved));
	if (!next.document || next.document->pageCount() != pageCount() ||
	    !next.document->checkEditCounts(*expected, {index})) {
		return {};
	}
	{
		PagePtr page(FPDF_LoadPage(next.document->m_handle, index));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), added)
		                        : nullptr);
		FS_RECTF kept{};
		if (!note || FPDFAnnot_GetSubtype(note.get()) != subtype ||
		    FPDFAnnot_GetFlags(note.get()) != FPDF_ANNOT_FLAG_PRINT ||
		    (verify && !verify(note.get())) ||
		    !FPDFAnnot_GetRect(note.get(), &kept) || kept.left != left ||
		    kept.bottom != bottom || kept.right != right || kept.top != top) {
			return {};
		}
		if (appearance) {
			std::vector<FPDF_WCHAR> ap(appearance->size() + 1);
			const auto size =
			    static_cast<unsigned long>(ap.size() * sizeof(FPDF_WCHAR));
			if (FPDFAnnot_GetAP(note.get(), FPDF_ANNOT_APPEARANCEMODE_NORMAL,
			                    ap.data(), size) != size ||
			    ap.back() ||
			    !std::equal(appearance->begin(), appearance->end(),
			                ap.begin())) {
				return {};
			}
		}
	}
	if (!m_outlineBookmarks.empty() &&
	    (!next.document->outline(canceled) ||
	     next.document->m_outlineBookmarks.size() !=
	         m_outlineBookmarks.size())) {
		return {};
	}
	if (stopped()) {
		return {};
	}
	m_data.swap(next.document->m_data);
	std::swap(m_handle, next.document->m_handle);
	m_outlineBookmarks.swap(next.document->m_outlineBookmarks);
	invalidateEdits();
	++m_annotationRevision;
	m_modified = true;
	return added;
}

std::optional<int> Document::addInk(int index,
                                    const std::vector<InkStroke>& strokes,
                                    std::uint64_t revision,
                                    const std::function<bool()>& canceled,
                                    InkStyle style) {
	const auto stopped = [&] { return canceled && canceled(); };
	if (offThread() || !revision || !currentRevision(revision) ||
	    revision == (std::numeric_limits<std::uint64_t>::max)() || index < 0 ||
	    index >= pageCount() || stopped() || !canAnnotate() ||
	    (FPDF_GetFormType(m_handle) != FORMTYPE_NONE &&
	     FPDF_GetFormType(m_handle) != FORMTYPE_ACRO_FORM) ||
	    FPDF_GetSignatureCount(m_handle) != 0 || strokes.empty() ||
	    strokes.size() > 256 || !std::isfinite(style.width) ||
	    style.width < .5f || style.width > 12 || style.color.red > 255 ||
	    style.color.green > 255 || style.color.blue > 255 ||
	    style.color.alpha != 255) {
		return {};
	}
	const double width = style.width;
	std::size_t points = 0;
	double left = std::numeric_limits<double>::infinity(), bottom = left;
	double right = -left, top = -left;
	for (const auto& stroke : strokes) {
		points += stroke.size();
		if (stroke.empty() || stroke.size() > 4096 || points > 16384) {
			return {};
		}
		for (const auto& [x, y] : stroke) {
			if (!std::isfinite(x) || !std::isfinite(y) ||
			    std::abs(x) > 1000000.f || std::abs(y) > 1000000.f) {
				return {};
			}
			left = (std::min)(left, double(x));
			bottom = (std::min)(bottom, double(y));
			right = (std::max)(right, double(x));
			top = (std::max)(top, double(y));
		}
	}
	if (!canSerializeEdits(canceled)) {
		return {};
	}
	// the rectangle covers half the line width around every point.
	const AnnotationBounds bounds{static_cast<float>(left - width / 2),
	                              static_cast<float>(bottom - width / 2),
	                              static_cast<float>(right + width / 2),
	                              static_cast<float>(top + width / 2)};
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	// round caps and joins also draw a single tap as a dot.
	stream << std::fixed << std::setprecision(5) << style.color.red / 255.
	       << ' ' << style.color.green / 255. << ' ' << style.color.blue / 255.
	       << " RG " << width << " w 1 J 1 j";
	for (const auto& stroke : strokes) {
		stream << ' ' << stroke.front().x << ' ' << stroke.front().y << " m";
		for (std::size_t i = stroke.size() == 1 ? 0 : 1; i < stroke.size();
		     ++i) {
			stream << ' ' << stroke[i].x << ' ' << stroke[i].y << " l";
		}
	}
	stream << " S";
	const auto raw = stream.str();
	// C and Border let other editors rebuild the same line. both have to go
	// in before the appearance: PDFium refuses colors once one exists and
	// drops it when the border changes.
	const auto decorate = [&](FPDF_ANNOTATION note, FPDF_DOCUMENT) {
		if (!FPDFAnnot_SetColor(note, FPDFANNOT_COLORTYPE_Color,
		                        style.color.red, style.color.green,
		                        style.color.blue, 255) ||
		    !FPDFAnnot_SetBorder(note, 0, 0, style.width)) {
			return false;
		}
		for (std::size_t i = 0; i < strokes.size(); ++i) {
			std::vector<FS_POINTF> path;
			path.reserve(strokes[i].size());
			for (const auto& [x, y] : strokes[i]) {
				path.push_back({x, y});
			}
			if (FPDFAnnot_AddInkStroke(note, path.data(), path.size()) !=
			    static_cast<int>(i)) {
				return false;
			}
		}
		return true;
	};
	// InkList keeps the strokes editable after the appearance is replaced.
	const auto verify = [&](FPDF_ANNOTATION note) {
		float horizontal = -1, vertical = -1, border = -1;
		if (FPDFAnnot_GetInkListCount(note) != strokes.size() ||
		    !FPDFAnnot_GetBorder(note, &horizontal, &vertical, &border) ||
		    horizontal != 0 || vertical != 0 || border != style.width) {
			return false;
		}
		for (std::size_t i = 0; i < strokes.size(); ++i) {
			const auto size = static_cast<unsigned long>(strokes[i].size());
			std::vector<FS_POINTF> path(size);
			if (FPDFAnnot_GetInkListPath(note, static_cast<unsigned long>(i),
			                             path.data(), size) != size ||
			    !std::equal(path.begin(), path.end(), strokes[i].begin(),
			                [](const FS_POINTF& a, const InkPoint& b) {
				                return a.x == b.x && a.y == b.y;
			                })) {
				return false;
			}
		}
		return true;
	};
	return appendAnnotation(index, FPDF_ANNOT_INK, bounds,
	                        std::u16string(raw.begin(), raw.end()), decorate,
	                        verify, canceled);
}

std::optional<bool>
Document::resizeShape(const Annotation& expected, ShapeSize size,
                      const std::function<bool()>& canceled) {
	const auto stopped = [&] { return canceled && canceled(); };
	const auto& target = expected.target;
	if (offThread() || stopped() || !expected.bounds ||
	    (target.subtype != "Square" && target.subtype != "Circle") ||
	    expected.subtype != target.subtype || expected.index != target.index ||
	    !expected.contents.empty() || !expected.author.empty() ||
	    expected.flags != FPDF_ANNOT_FLAG_PRINT || !std::isfinite(size.width) ||
	    !std::isfinite(size.height) || size.width <= 4 || size.height <= 4 ||
	    m_annotationRevision == (std::numeric_limits<std::uint64_t>::max)() ||
	    !matchesTarget(target) || !canAnnotate() ||
	    !canSerializeEdits(canceled) ||
	    !provenUnique(target.page, target.index)) {
		return {};
	}
	const auto bounds = *expected.bounds;
	auto destination = bounds;
	if (size.width != bounds.right - bounds.left) {
		destination.right = bounds.left + size.width;
	}
	if (size.height != bounds.top - bounds.bottom) {
		destination.bottom = bounds.top - size.height;
	}
	const auto sameBounds = [](const auto& a, const auto& b) {
		return a.left == b.left && a.bottom == b.bottom && a.right == b.right &&
		       a.top == b.top;
	};
	const auto notes = annotations(target.page, canceled);
	if (!notes || notes->limited) {
		return {};
	}
	const auto found =
	    std::find_if(notes->entries.begin(), notes->entries.end(),
	                 [&](const auto& note) { return note.target == target; });
	if (found == notes->entries.end() || !found->bounds ||
	    !sameBounds(*found->bounds, bounds) || found->flags != expected.flags ||
	    found->contents != expected.contents ||
	    found->author != expected.author) {
		return {};
	}
	// the target's plain outline is small; neighbors only share the copy
	// budget, since explicit-line boxes draw glyph outlines past 64 KiB.
	const auto read = [](FPDF_ANNOTATION note, int mode,
	                     unsigned long most =
	                         65536) -> std::optional<std::u16string> {
		const auto bytes = FPDFAnnot_GetAP(note, mode, nullptr, 0);
		if (bytes < 2 || bytes > most || bytes % 2) {
			return {};
		}
		std::vector<FPDF_WCHAR> text(bytes / 2);
		if (FPDFAnnot_GetAP(note, mode, text.data(), bytes) != bytes ||
		    text.back()) {
			return {};
		}
		return std::u16string(text.begin(), text.end() - 1);
	};
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
		     {"Contents",     "T",       "Subj",   "NM", "M",
		      "CreationDate", "RC",      "DS",     "C",  "IC",
		      "BS",           "Border",  "BE",     "CA", "BM",
		      "RD",           "Rotate",  "OC",     "A",  "AA",
		      "IT",           "Measure", "ExData", "AS", "StructParent"}) {
			if (FPDFAnnot_HasKey(note.get(), key)) {
				return {};
			}
		}
		if (!FPDFAnnot_HasKey(note.get(), "AP") ||
		    read(note.get(), 1) != std::optional<std::u16string>{u""} ||
		    read(note.get(), 2) != std::optional<std::u16string>{u""}) {
			return {};
		}
	}
	constexpr std::size_t limit = 128 * 1024 * 1024;
	auto bytes = serialize(limit, canceled);
	if (!bytes || stopped()) {
		return {};
	}
	auto scratch = reopenFormCopy(*bytes);
	const auto shape = target.subtype == "Square" ? OutlineShape::Rectangle
	                                              : OutlineShape::Ellipse;
	if (!scratch.document) {
		return {};
	}
	const auto reference = scratch.document->addOutlineShape(
	    target.page, bounds, shape, 1, canceled);
	if (!reference || stopped()) {
		return {};
	}
	std::u16string oldAppearance;
	{
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, target.page));
		AnnotationPtr note(FPDFPage_GetAnnot(page.get(), target.index)),
		    model(FPDFPage_GetAnnot(page.get(), *reference));
		const auto old = read(note.get(), 0), standard = read(model.get(), 0);
		if (!old || old->empty() || old != standard) {
			return {};
		}
		oldAppearance = *old;
	}
	{
		// i also prove BBox/Matrix placement on an isolated empty page.
		auto proof = reopenFormCopy(*bytes);
		PagePtr page(proof.document
		                 ? FPDF_LoadPage(proof.document->m_handle, target.page)
		                 : nullptr);
		const FS_RECTF rect{bounds.left, bounds.top, bounds.right,
		                    bounds.bottom};
		if (!page || !matchesBoxRendering(page.get(), target.index, rect,
		                                  oldAppearance, stopped)) {
			return {};
		}
	}
	if (sameBounds(bounds, destination)) {
		return false;
	}
	const auto created = scratch.document->addOutlineShape(
	    target.page, destination, shape, scratch.document->annotationRevision(),
	    canceled);
	if (!created || stopped()) {
		return {};
	}
	std::u16string appearance;
	{
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, target.page));
		AnnotationPtr model(FPDFPage_GetAnnot(page.get(), *created));
		const auto text = read(model.get(), 0);
		if (!text) {
			return {};
		}
		appearance = *text;
		model.reset();
		if (!FPDFPage_RemoveAnnot(page.get(), *created) ||
		    !FPDFPage_RemoveAnnot(page.get(), *reference)) {
			return {};
		}
	}
	const auto counts = editCounts();
	if (!counts) {
		return {};
	}
	struct Neighbor {
		std::string subtype, contents, author;
		int flags;
		std::vector<float> bounds;
		std::vector<std::u16string> appearances;
		bool operator==(const Neighbor&) const = default;
	};
	// a shared appearance or rectangle must not change another annotation.
	const auto neighbors =
	    [&](const Document& doc) -> std::optional<std::vector<Neighbor>> {
		std::vector<Neighbor> result;
		std::size_t total = 0;
		for (int p = 0; p < pageCount(); ++p) {
			PagePtr page(FPDF_LoadPage(doc.m_handle, p));
			if (!page || FPDFPage_GetAnnotCount(page.get()) != (*counts)[p]) {
				return {};
			}
			const auto snapshot = doc.annotations(p, canceled);
			if (!snapshot || snapshot->limited ||
			    snapshot->entries.size() !=
			        static_cast<std::size_t>((*counts)[p])) {
				return {};
			}
			for (int i = 0; i < (*counts)[p]; ++i) {
				if (p == target.page && i == target.index) {
					continue;
				}
				const auto& entry = snapshot->entries[i];
				Neighbor neighbor{entry.subtype,
				                  entry.contents,
				                  entry.author,
				                  entry.flags,
				                  {},
				                  {}};
				if (entry.bounds) {
					const auto& r = *entry.bounds;
					neighbor.bounds = {r.left, r.bottom, r.right, r.top};
				}
				AnnotationPtr note(FPDFPage_GetAnnot(page.get(), i));
				for (int mode = 0; mode < 3; ++mode) {
					const auto room = limit / 2 - total;
					const auto text =
					    note ? read(note.get(), mode,
					                static_cast<unsigned long>(2 * room + 2))
					         : std::nullopt;
					if (!text || stopped() ||
					    text->size() > limit / 2 - total) {
						return {};
					}
					total += text->size();
					neighbor.appearances.push_back(*text);
				}
				result.push_back(std::move(neighbor));
			}
		}
		return result;
	};
	const auto before = neighbors(*scratch.document);
	{
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, target.page));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
		                        : nullptr);
		const FS_RECTF rect{destination.left, destination.top,
		                    destination.right, destination.bottom};
		if (!before || !note || !FPDFAnnot_SetRect(note.get(), &rect) ||
		    !FPDFAnnot_SetAP(
		        note.get(), 0,
		        reinterpret_cast<FPDF_WIDESTRING>(appearance.c_str()))) {
			return {};
		}
	}
	scratch.document->invalidateEdits();
	scratch.document->m_modified = true;
	auto saved = scratch.document->serialize(limit, canceled);
	if (!saved || stopped()) {
		return {};
	}
	auto next = reopenFormCopy(std::move(*saved));
	if (!next.document || next.document->pageCount() != pageCount() ||
	    !next.document->checkEditCounts(*counts, {target.page}) ||
	    neighbors(*next.document) != before) {
		return {};
	}
	{
		PagePtr page(FPDF_LoadPage(next.document->m_handle, target.page));
		AnnotationPtr note(FPDFPage_GetAnnot(page.get(), target.index));
		FS_RECTF rect{};
		if (!note ||
		    FPDFAnnot_GetSubtype(note.get()) !=
		        (shape == OutlineShape::Rectangle ? FPDF_ANNOT_SQUARE
		                                          : FPDF_ANNOT_CIRCLE) ||
		    FPDFAnnot_GetFlags(note.get()) != FPDF_ANNOT_FLAG_PRINT ||
		    !FPDFAnnot_GetRect(note.get(), &rect) ||
		    rect.left != destination.left ||
		    rect.bottom != destination.bottom ||
		    rect.right != destination.right || rect.top != destination.top ||
		    read(note.get(), 0) != std::optional{appearance}) {
			return {};
		}
	}
	if (!m_outlineBookmarks.empty() &&
	    (!next.document->outline(canceled) ||
	     next.document->m_outlineBookmarks.size() !=
	         m_outlineBookmarks.size())) {
		return {};
	}
	if (stopped()) {
		return {};
	}
	m_data.swap(next.document->m_data);
	std::swap(m_handle, next.document->m_handle);
	m_outlineBookmarks.swap(next.document->m_outlineBookmarks);
	invalidateEdits();
	++m_annotationRevision;
	m_modified = true;
	return true;
}

std::optional<int> Document::addRectangleAt(
    int index, AnnotationPoint first, AnnotationPoint second, int rotation,
    std::uint64_t revision, const std::function<bool()>& canceled) {
	return addOutlineShapeAt(index, first, second, rotation,
	                         OutlineShape::Rectangle, revision, canceled);
}

std::optional<int>
Document::addEllipseAt(int index, AnnotationPoint first, AnnotationPoint second,
                       int rotation, std::uint64_t revision,
                       const std::function<bool()>& canceled) {
	return addOutlineShapeAt(index, first, second, rotation,
	                         OutlineShape::Ellipse, revision, canceled);
}

std::optional<int>
Document::addOutlineShapeAt(int index, AnnotationPoint first,
                            AnnotationPoint second, int rotation,
                            OutlineShape shape, std::uint64_t revision,
                            const std::function<bool()>& canceled) {
	if (offThread() || index < 0 || index >= pageCount() || rotation < 0 ||
	    rotation > 3 || !onDisplayedPage(first) || !onDisplayedPage(second) ||
	    !revision || !currentRevision(revision) || (canceled && canceled())) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return {};
	}
	double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	if (!displayedAnnotationPoint(page.get(), rotation, first, x0, y0) ||
	    !displayedAnnotationPoint(page.get(), rotation, second, x1, y1)) {
		return {};
	}
	const AnnotationBounds bounds{static_cast<float>((std::min)(x0, x1)),
	                              static_cast<float>((std::min)(y0, y1)),
	                              static_cast<float>((std::max)(x0, x1)),
	                              static_cast<float>((std::max)(y0, y1))};
	page.reset();
	return addOutlineShape(index, bounds, shape, revision, canceled);
}

std::optional<int> Document::addStampAt(int index, AnnotationPoint first,
                                        AnnotationPoint second, int rotation,
                                        const StampImage& image,
                                        std::uint64_t revision,
                                        const std::function<bool()>& canceled) {
	if (offThread() || index < 0 || index >= pageCount() || rotation < 0 ||
	    rotation > 3 || !onDisplayedPage(first) || !onDisplayedPage(second) ||
	    !image.valid() || !revision || !currentRevision(revision) ||
	    (canceled && canceled())) {
		return {};
	}
	const double left = (std::min)(first.x, second.x);
	const double right = (std::max)(first.x, second.x);
	const double top = (std::min)(first.y, second.y);
	const double bottom = (std::max)(first.y, second.y);
	PagePtr page(FPDF_LoadPage(m_handle, index));
	AnnotationPoint tl, tr, bl;
	if (!page ||
	    !displayedAnnotationPoint(page.get(), rotation, {left, top}, tl.x,
	                              tl.y) ||
	    !displayedAnnotationPoint(page.get(), rotation, {right, top}, tr.x,
	                              tr.y) ||
	    !displayedAnnotationPoint(page.get(), rotation, {left, bottom}, bl.x,
	                              bl.y)) {
		return {};
	}
	page.reset();
	const double width = std::hypot(tr.x - tl.x, tr.y - tl.y);
	const double height = std::hypot(bl.x - tl.x, bl.y - tl.y);
	if (width < 1 || height < 1) {
		return {};
	}
	// i fit the image at the top left and keep its axes on the displayed page.
	const double scale = (std::min)(width / image.width, height / image.height);
	const double sx = scale * image.width / width;
	const double sy = scale * image.height / height;
	const double a = (tr.x - tl.x) * sx, b = (tr.y - tl.y) * sx;
	const double c = (tl.x - bl.x) * sy, d = (tl.y - bl.y) * sy;
	const double e = tl.x - c, f = tl.y - d;
	const AnnotationBounds bounds{
	    static_cast<float>((std::min)({e, e + a, tl.x, tl.x + a})),
	    static_cast<float>((std::min)({f, f + b, tl.y, tl.y + b})),
	    static_cast<float>((std::max)({e, e + a, tl.x, tl.x + a})),
	    static_cast<float>((std::max)({f, f + b, tl.y, tl.y + b}))};
	return writeStamp(index, bounds, image,
	                  {static_cast<float>(a), static_cast<float>(b),
	                   static_cast<float>(c), static_cast<float>(d),
	                   static_cast<float>(e), static_cast<float>(f)},
	                  revision, canceled);
}

std::optional<int>
Document::addInkAt(int index, const std::vector<DisplayedStroke>& strokes,
                   int rotation, std::uint64_t revision,
                   const std::function<bool()>& canceled, InkStyle style) {
	if (offThread() || index < 0 || index >= pageCount() || rotation < 0 ||
	    rotation > 3 || strokes.empty() || strokes.size() > 256 || !revision ||
	    !currentRevision(revision) || (canceled && canceled())) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return {};
	}
	std::vector<InkStroke> converted;
	converted.reserve(strokes.size());
	std::size_t points = 0;
	for (const auto& stroke : strokes) {
		points += stroke.size();
		if (stroke.empty() || stroke.size() > 4096 || points > 16384) {
			return {};
		}
		auto& ink = converted.emplace_back();
		ink.reserve(stroke.size());
		for (const auto point : stroke) {
			// the same fine device grid as the shape corners.
			constexpr int grid = 1 << 20;
			double x = 0, y = 0;
			if (!(point.x >= 0 && point.x <= 1 && point.y >= 0 &&
			      point.y <= 1) ||
			    !FPDF_DeviceToPage(
			        page.get(), 0, 0, grid, grid, rotation,
			        static_cast<int>(std::lround(point.x * grid)),
			        static_cast<int>(std::lround(point.y * grid)), &x, &y) ||
			    !std::isfinite(x) || !std::isfinite(y) ||
			    std::abs(x) > 1000000. || std::abs(y) > 1000000.) {
				return {};
			}
			ink.push_back({static_cast<float>(x), static_cast<float>(y)});
		}
	}
	page.reset();
	return addInk(index, converted, revision, canceled, style);
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
	    FPDF_GetSignatureCount(m_handle) != 0 || !canSerializeEdits(canceled)) {
		return {};
	}
	constexpr std::size_t limit = 128 * 1024 * 1024;
	auto bytes = serialize(limit, canceled);
	if (!bytes || stopped()) {
		return {};
	}
	auto scratch = reopenFormCopy(std::move(*bytes));
	if (!scratch.document) {
		return {};
	}
	scratch.document->m_modified = true;
	// i expand object streams before checking hidden signature material.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/edit/cpdf_creator.cpp
	auto canonical = scratch.document->serialize(limit, canceled);
	if (!canonical || stopped()) {
		return {};
	}
	const std::string_view tokens(
	    reinterpret_cast<const char*>(canonical->data()), canonical->size());
	if (tokens.find("/Sig") != std::string_view::npos ||
	    tokens.find("/DocMDP") != std::string_view::npos) {
		return {};
	}
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
	scratch = reopenFormCopy(std::move(*canonical));
	if (!scratch.document || stopped()) {
		return {};
	}
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
	scratch.document->m_modified = true;
	auto saved = scratch.document->serialize(limit, canceled);
	if (!saved || stopped()) {
		return {};
	}
	auto next = reopenFormCopy(std::move(*saved));
	if (!next.document || next.document->pageCount() != pageCount() ||
	    !proof(*next.document) ||
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
	if (!m_outlineBookmarks.empty() &&
	    (!next.document->outline(canceled) ||
	     next.document->m_outlineBookmarks.size() !=
	         m_outlineBookmarks.size())) {
		return {};
	}
	if (stopped()) {
		return {};
	}
	m_data.swap(next.document->m_data);
	std::swap(m_handle, next.document->m_handle);
	m_outlineBookmarks.swap(next.document->m_outlineBookmarks);
	invalidateEdits();
	++m_annotationRevision;
	m_modified = true;
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
	auto scratch = reopenFormCopy(std::move(*bytes));
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
	scratch.document->m_modified = true;
	auto saved = scratch.document->serialize(limit, canceled);
	if (!saved || stopped()) {
		return {};
	}
	auto next = reopenFormCopy(std::move(*saved));
	if (!next.document || next.document->pageCount() != pageCount()) {
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
	if (!m_outlineBookmarks.empty() &&
	    (!next.document->outline(canceled) ||
	     next.document->m_outlineBookmarks.size() !=
	         m_outlineBookmarks.size())) {
		return {};
	}
	if (stopped()) {
		return {};
	}
	m_data.swap(next.document->m_data);
	std::swap(m_handle, next.document->m_handle);
	m_outlineBookmarks.swap(next.document->m_outlineBookmarks);
	invalidateEdits();
	++m_annotationRevision;
	m_modified = true;
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
	auto expected = editCounts();
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
	    FPDFAnnot_SetFlags(note.get(), FPDF_ANNOT_FLAG_NOVIEW) &&
	    checkEditCounts(*expected, {index});
	note.reset();
	if (!complete) {
		// i remove only the appended note if a setter failed.
		if (!FPDFPage_RemoveAnnot(page.get(), count)) {
			invalidateEdits();
			throw std::runtime_error("Couldn't roll back the new note.");
		}
		m_modified = wasModified;
		return {};
	}
	m_uniqueMarkup.insert({index, count});
	m_formDrawing.reset();
	m_formDrawingRevision = 0;
	return count;
}

std::optional<std::u16string> Document::textNote(int index, int noteIndex,
                                                 std::uint64_t revision) const {
	if (offThread() || !currentRevision(revision) || index < 0 ||
	    index >= pageCount() || noteIndex < 0 || !canAnnotate() ||
	    !canSerializeEdits({})) {
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
                                             std::u16string_view contents,
                                             std::uint64_t revision) {
	if (offThread() || !currentRevision(revision) || contents.empty() ||
	    !validNoteText(contents) || !validNoteText(expected)) {
		return {};
	}
	const auto previous = textNote(pageIndex, noteIndex, revision);
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
	const int flags = FPDFAnnot_GetFlags(note.get());
	const int hidden =
	    (flags | FPDF_ANNOT_FLAG_NOVIEW) & ~FPDF_ANNOT_FLAG_PRINT;
	if (*previous == contents && flags == hidden) {
		return false;
	}
	const bool wasModified = m_modified;
	m_modified = true;
	if (!FPDFAnnot_SetStringValue(
	        note.get(), "Contents",
	        reinterpret_cast<FPDF_WIDESTRING>(replacement.c_str())) ||
	    !FPDFAnnot_SetFlags(note.get(), hidden)) {
		const bool restoredText =
		    FPDFAnnot_SetStringValue(
		        note.get(), "Contents",
		        reinterpret_cast<FPDF_WIDESTRING>(previous->c_str())) != 0;
		const bool restoredFlags = FPDFAnnot_SetFlags(note.get(), flags) != 0;
		if (!restoredText || !restoredFlags) {
			invalidateEdits();
			throw std::runtime_error("Couldn't restore the note contents.");
		}
		m_modified = wasModified;
		return {};
	}
	m_formDrawing.reset();
	m_formDrawingRevision = 0;
	return true;
}

std::optional<int> Document::highlightText(TextSelection selection) {
	return markText(selection, TextMarkup::Highlight);
}

std::optional<CommentContents>
Document::markupComment(int pageIndex, int index, TextMarkup markup,
                        std::uint64_t revision) const {
	const auto subtype = markupSubtype(markup);
	if (offThread() || !currentRevision(revision) || !subtype ||
	    pageIndex < 0 || pageIndex >= pageCount() || index < 0 ||
	    !canAnnotate() || !canSerializeEdits({})) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, pageIndex));
	AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), index) : nullptr);
	constexpr int LOCKED_CONTENTS = 0x200;
	if (!note || FPDFAnnot_GetSubtype(note.get()) != *subtype ||
	    (FPDFAnnot_GetFlags(note.get()) &
	     (FPDF_ANNOT_FLAG_READONLY | LOCKED_CONTENTS)) ||
	    FPDFAnnot_HasKey(note.get(), "RC") ||
	    (!m_uniqueMarkup.contains({pageIndex, index}) &&
	     !uniqueMarkup(m_handle, page.get(), pageIndex, index, note.get(),
	                   m_editPageLoads))) {
		return {};
	}
	m_uniqueMarkup.insert({pageIndex, index});
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

std::optional<bool> Document::updateMarkupComment(
    int page, int index, TextMarkup markup, const CommentContents& expected,
    std::u16string_view contents, std::uint64_t revision) {
	if (contents.empty()) {
		return {};
	}
	return setMarkupComment(page, index, markup, expected, contents, revision);
}

std::optional<bool> Document::setMarkupComment(int page, int index,
                                               TextMarkup markup,
                                               const CommentContents& expected,
                                               std::u16string_view contents,
                                               std::uint64_t revision) {
	if (offThread() || !currentRevision(revision) || !validNoteText(contents) ||
	    !validNoteText(expected.text)) {
		return {};
	}
	const auto previous = markupComment(page, index, markup, revision);
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
			invalidateEdits();
			throw std::runtime_error("Couldn't restore the markup comment.");
		}
		m_modified = wasModified;
		return {};
	}
	m_formDrawing.reset();
	m_formDrawingRevision = 0;
	return true;
}

std::optional<int> Document::markText(TextSelection selection,
                                      TextMarkup markup, HighlightColor color) {
	if (offThread() || !canAnnotate()) {
		return {};
	}
	const auto subtype = markupSubtype(markup);
	const auto rgb = markupColor(color);
	if (!subtype || !rgb) {
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
		const auto snapshot = pageText(target.page.get(), text.get());
		if (!snapshot) {
			return {};
		}
		const int chars = static_cast<int>(snapshot->characters.size());
		if (index == first.page && first.index > chars) {
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
		const auto bands = textRects(*snapshot, begin, end);
		if (bands.size() > 4096 - totalQuads) {
			return {};
		}
		target.quads.reserve(bands.size());
		int cursor = begin;
		const int turn = FPDFPage_GetRotation(target.page.get());
		for (const auto& band : bands) {
			// i keep the text's top edge when the page or text is rotated.
			double dx = turn % 2 ? 0 : (turn == 2 ? -1 : 1);
			double dy = turn % 2 ? (turn == 3 ? -1 : 1) : 0;
			for (; cursor < end; ++cursor) {
				const auto& character = snapshot->characters[cursor];
				const auto& b = character.box;
				const double x = (b.left + b.right) / 2;
				const double y = (b.top + b.bottom) / 2;
				if (character.baseline.height > 0 && b.right > b.left &&
				    b.bottom > b.top && x >= band.left && x <= band.right &&
				    y >= band.top && y <= band.bottom) {
					dx = character.baseline.dx;
					dy = character.baseline.dy;
					++cursor;
					break;
				}
			}
			const std::pair<double, double> corners[] = {
			    {band.left, band.top},
			    {band.right, band.top},
			    {band.left, band.bottom},
			    {band.right, band.bottom}};
			const int order = std::abs(dx) >= std::abs(dy) ? (dx < 0 ? 2 : 0)
			                                               : (dy < 0 ? 3 : 1);
			constexpr int indices[4][4] = {
			    {0, 1, 2, 3}, {1, 3, 0, 2}, {3, 2, 1, 0}, {2, 0, 3, 1}};
			FS_QUADPOINTSF quad{};
			float* const coordinates[] = {&quad.x1, &quad.y1, &quad.x2,
			                              &quad.y2, &quad.x3, &quad.y3,
			                              &quad.x4, &quad.y4};
			FS_RECTF clipped{box.right, box.bottom, box.left, box.top};
			for (int p = 0; p < 4; ++p) {
				const auto& [x, y] = corners[indices[order][p]];
				double px = 0, py = 0;
				if (!FPDF_DeviceToPage(
				        target.page.get(), 0, 0, TEXT_GRID, TEXT_GRID, 0,
				        static_cast<int>(std::lround(x * TEXT_GRID)),
				        static_cast<int>(std::lround(y * TEXT_GRID)), &px,
				        &py) ||
				    !std::isfinite(px) || !std::isfinite(py)) {
					return {};
				}
				const float qx = static_cast<float>(
				    std::clamp(px, double(box.left), double(box.right)));
				const float qy = static_cast<float>(
				    std::clamp(py, double(box.bottom), double(box.top)));
				*coordinates[p * 2] = qx;
				*coordinates[p * 2 + 1] = qy;
				clipped.left = (std::min)(clipped.left, qx);
				clipped.right = (std::max)(clipped.right, qx);
				clipped.bottom = (std::min)(clipped.bottom, qy);
				clipped.top = (std::max)(clipped.top, qy);
			}
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
			target.quads.push_back(quad);
		}
		totalQuads += target.quads.size();
		if (!target.quads.empty()) {
			targets.push_back(std::move(target));
		}
	}
	if (targets.empty()) {
		return {};
	}
	BitmapPtr appearance(FPDFBitmap_Create(1, 1, 0));
	if (!appearance) {
		return {};
	}
	FPDFBitmap_FillRect(appearance.get(), 0, 0, 1, 1, 0xFFFFFFFF);
	auto expected = editCounts();
	if (!expected) {
		return {};
	}
	std::vector<int> pages;
	pages.reserve(targets.size());
	for (const auto& target : targets) {
		++(*expected)[target.pageIndex];
		pages.push_back(target.pageIndex);
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
			invalidateEdits();
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
			           FPDFAnnot_SetFlags(note.get(), FPDF_ANNOT_FLAG_PRINT) &&
			           (markup != TextMarkup::Highlight ||
			            FPDFAnnot_SetColor(note.get(),
			                               FPDFANNOT_COLORTYPE_Color, rgb->red,
			                               rgb->green, rgb->blue, rgb->alpha));
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
	// i let PDFium make its default appearances before these pages are saved.
	// a one-pixel render does this even when the page was never on screen.
	for (const auto& target : targets) {
		FPDF_RenderPageBitmap(appearance.get(), target.page.get(), 0, 0, 1, 1,
		                      0, FPDF_ANNOT);
		AnnotationPtr note(FPDFPage_GetAnnot(target.page.get(), target.index));
		if (!note ||
		    FPDFAnnot_GetValueType(note.get(), "AP") !=
		        FPDF_OBJECT_DICTIONARY ||
		    FPDFAnnot_GetAP(note.get(), FPDF_ANNOT_APPEARANCEMODE_NORMAL,
		                    nullptr, 0) <= 2) {
			return rollback();
		}
	}
	if (!checkEditCounts(*expected, pages)) {
		return rollback();
	}
	for (const auto& target : targets) {
		m_uniqueMarkup.insert({target.pageIndex, target.index});
	}
	// i use PDFium's default markup appearances.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/63155a9b3873593a0f0d06bede4046ba7914b3ac/core/fpdfdoc/cpdf_generateap.cpp
	m_formDrawing.reset();
	m_formDrawingRevision = 0;
	return static_cast<int>(added);
}

bool Document::currentRevision(std::uint64_t revision) const {
	// i accept old callers only until the first index-changing edit.
	return revision == m_annotationRevision ||
	       (revision == 0 && m_annotationRevision == 1);
}

std::uint64_t Document::annotationRevision() const {
	return offThread() ? 0 : m_annotationRevision;
}

bool Document::matchesTarget(const AnnotationTarget& target) const {
	if (offThread() || target.revision == 0 ||
	    target.revision != m_annotationRevision || target.page < 0 ||
	    target.page >= pageCount() || target.index < 0) {
		return false;
	}
	const auto snapshot = annotations(target.page);
	if (!snapshot || snapshot->limited) {
		return false;
	}
	const auto found = std::find_if(
	    snapshot->entries.begin(), snapshot->entries.end(),
	    [&](const Annotation& note) { return note.target == target; });
	return found != snapshot->entries.end();
}

bool Document::provenUnique(int pageIndex, int index) const {
	if (m_uniqueMarkup.contains({pageIndex, index})) {
		return true;
	}
	if (m_identityPages.contains(pageIndex) || !m_editCounts || index < 0 ||
	    pageIndex < 0 || pageIndex >= static_cast<int>(m_editCounts->size())) {
		return false;
	}
	++m_editPageLoads;
	PagePtr own(FPDF_LoadPage(m_handle, pageIndex));
	const int count = own ? FPDFPage_GetAnnotCount(own.get()) : -1;
	if (count < 0 || count > 4096 || index >= count ||
	    count != (*m_editCounts)[pageIndex]) {
		return false;
	}
	std::vector<AnnotationPtr> notes;
	std::vector<bool> unique(count, true);
	notes.reserve(count);
	for (int candidate = 0; candidate < count; ++candidate) {
		AnnotationPtr note(FPDFPage_GetAnnot(own.get(), candidate));
		const int first =
		    note ? FPDFPage_GetAnnotIndex(own.get(), note.get()) : -1;
		if (first < 0 || first > candidate) {
			return false;
		}
		if (first != candidate) {
			unique[first] = unique[candidate] = false;
		}
		notes.push_back(std::move(note));
	}
	// i prove this page's dictionaries together, then discard every handle.
	for (int pageIndexOther = 0; pageIndexOther < pageCount();
	     ++pageIndexOther) {
		if (pageIndexOther == pageIndex) {
			continue;
		}
		++m_editPageLoads;
		PagePtr other(FPDF_LoadPage(m_handle, pageIndexOther));
		const int otherCount = other ? FPDFPage_GetAnnotCount(other.get()) : -1;
		if (otherCount < 0 || otherCount != (*m_editCounts)[pageIndexOther]) {
			return false;
		}
		if (!otherCount) {
			continue;
		}
		for (int candidate = 0; candidate < count; ++candidate) {
			if (unique[candidate] &&
			    FPDFPage_GetAnnotIndex(other.get(), notes[candidate].get()) >=
			        0) {
				unique[candidate] = false;
			}
		}
	}
	std::set<std::pair<int, int>> proven;
	for (int candidate = 0; candidate < count; ++candidate) {
		if (unique[candidate]) {
			proven.insert({pageIndex, candidate});
		}
	}
	m_identityPages.insert(pageIndex);
	m_uniqueMarkup.merge(proven);
	return m_uniqueMarkup.contains({pageIndex, index});
}

std::optional<bool> Document::removeAnnotation(const AnnotationTarget& target) {
	if (offThread() || !matchesTarget(target) || !canAnnotate() ||
	    !canSerializeEdits({}) ||
	    m_annotationRevision == (std::numeric_limits<std::uint64_t>::max)() ||
	    (target.subtype != "Text" && target.subtype != "Highlight" &&
	     target.subtype != "Underline" && target.subtype != "StrikeOut" &&
	     target.subtype != "FreeText" && target.subtype != "Square" &&
	     target.subtype != "Circle" && target.subtype != "Ink")) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, target.page));
	AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
	                        : nullptr);
	constexpr int LOCKED_CONTENTS = 0x200;
	if (!note || !m_editCounts || !m_linkedPages ||
	    target.page >= static_cast<int>(m_editCounts->size()) ||
	    FPDFPage_GetAnnotCount(page.get()) != (*m_editCounts)[target.page] ||
	    (FPDFAnnot_GetFlags(note.get()) &
	     (FPDF_ANNOT_FLAG_READONLY | FPDF_ANNOT_FLAG_LOCKED |
	      LOCKED_CONTENTS)) ||
	    (!m_uniqueMarkup.contains({target.page, target.index}) &&
	     !provenUnique(target.page, target.index)) ||
	    !unlinkedAnnotation(m_handle, page.get(), target.index, note.get(),
	                        *m_linkedPages, m_editPageLoads)) {
		return {};
	}
	m_independentPages.insert(target.page);
	note.reset();
	// i finish every refusal check before removing the dictionary reference.
	if (!FPDFPage_RemoveAnnot(page.get(), target.index)) {
		invalidateEdits();
		return {};
	}
	// i keep the other proofs: removal cannot add sharing or reply links.
	--(*m_editCounts)[target.page];
	auto next = m_uniqueMarkup.lower_bound({target.page, target.index});
	while (next != m_uniqueMarkup.end() && next->first == target.page) {
		auto entry = m_uniqueMarkup.extract(next++);
		if (entry.value().second != target.index) {
			--entry.value().second;
			m_uniqueMarkup.insert(std::move(entry));
		}
	}
	++m_annotationRevision;
	m_modified = true;
	return true;
}

std::optional<bool>
Document::clearMarkupComment(const AnnotationTarget& target,
                             const CommentContents& expected) {
	if (!matchesTarget(target)) {
		return {};
	}
	const auto markup = target.subtype == "Highlight"   ? TextMarkup::Highlight
	                    : target.subtype == "Underline" ? TextMarkup::Underline
	                                                    : TextMarkup::StrikeOut;
	if (target.subtype != "Highlight" && target.subtype != "Underline" &&
	    target.subtype != "StrikeOut") {
		return {};
	}
	return setMarkupComment(target.page, target.index, markup, expected, {},
	                        target.revision);
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
	return serializeOutput(limit, canceled, false);
}

std::optional<std::vector<std::uint8_t>>
Document::serializeForCopy(std::size_t limit,
                           const std::function<bool()>& canceled) const {
	return serializeOutput(limit, canceled, true);
}

std::optional<std::vector<std::uint8_t>>
Document::serializeOutput(std::size_t limit,
                          const std::function<bool()>& canceled,
                          bool hideNotes) const {
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
	detail::NoteVisibility notes;
	if (hideNotes &&
	    !notes.collect(m_handle, [&writer] { return writer.stopped(); })) {
		return {};
	}
	if (!m_modified && notes.empty()) {
		if (m_data.size() > limit) {
			return {};
		}
		writer.bytes = m_data;
	} else {
		if (!canSerializeEdits([&writer] { return writer.stopped(); })) {
			return {};
		}
		if (!notes.run(
		        [&] { return FPDF_SaveAsCopy(m_handle, &writer, 0) != 0; })) {
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
	result.revision = m_annotationRevision;
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
	constexpr int grid = 1 << 20;
	const auto displayPoint = [&](float px, float py, float& dx, float& dy) {
		const double padX = 16 * (double(pageBox.right) - pageBox.left);
		const double padY = 16 * (double(pageBox.top) - pageBox.bottom);
		if (!usablePage || px < pageBox.left - padX ||
		    px > pageBox.right + padX || py < pageBox.bottom - padY ||
		    py > pageBox.top + padY) {
			return false;
		}
		int x = 0, y = 0;
		if (!FPDF_PageToDevice(page.get(), 0, 0, grid, grid, 0, px, py, &x,
		                       &y)) {
			return false;
		}
		dx = x / float(grid);
		dy = y / float(grid);
		return true;
	};
	std::size_t total = 0, totalQuads = 0;
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
		annotation.target = {index, i, annotation.subtype,
		                     m_annotationRevision};
		AnnotationColor color;
		if (FPDFAnnot_GetColor(handle.get(), FPDFANNOT_COLORTYPE_Color,
		                       &color.red, &color.green, &color.blue,
		                       &color.alpha)) {
			annotation.color = color;
		}
		const auto quads = FPDFAnnot_CountAttachmentPoints(handle.get());
		if (quads > 4096 - totalQuads) {
			result.limited = true;
		} else {
			totalQuads += quads;
			annotation.quads.reserve(quads);
			for (std::size_t q = 0; q < quads; ++q) {
				FS_QUADPOINTSF quad{};
				if (!FPDFAnnot_GetAttachmentPoints(handle.get(), q, &quad) ||
				    !std::isfinite(quad.x1) || !std::isfinite(quad.y1) ||
				    !std::isfinite(quad.x2) || !std::isfinite(quad.y2) ||
				    !std::isfinite(quad.x3) || !std::isfinite(quad.y3) ||
				    !std::isfinite(quad.x4) || !std::isfinite(quad.y4)) {
					result.limited = true;
					annotation.quads.clear();
					break;
				}
				annotation.quads.push_back({quad.x1, quad.y1, quad.x2, quad.y2,
				                            quad.x3, quad.y3, quad.x4,
				                            quad.y4});
			}
		}
		annotation.displayQuads.reserve(annotation.quads.size());
		for (const auto& quad : annotation.quads) {
			AnnotationQuad shown;
			if (!displayPoint(quad.x1, quad.y1, shown.x1, shown.y1) ||
			    !displayPoint(quad.x2, quad.y2, shown.x2, shown.y2) ||
			    !displayPoint(quad.x3, quad.y3, shown.x3, shown.y3) ||
			    !displayPoint(quad.x4, quad.y4, shown.x4, shown.y4)) {
				result.limited = true;
				annotation.displayQuads.clear();
				break;
			}
			annotation.displayQuads.push_back(shown);
		}
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
			float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
			if (displayPoint(rect.left, rect.bottom, x1, y1) &&
			    displayPoint(rect.right, rect.top, x2, y2)) {
				annotation.displayBounds =
				    AnnotationRect{(std::min)(x1, x2), (std::min)(y1, y2),
				                   (std::max)(x1, x2), (std::max)(y1, y2)};
			}
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

std::optional<FormFields>
Document::formFields(int index, const std::function<bool()>& canceled) const {
	const auto stopped = [&] { return canceled && canceled(); };
	if (offThread() || index < 0 || index >= pageCount() || stopped()) {
		return {};
	}
	FormFields result;
	result.page = index;
	result.revision = m_annotationRevision;
	const int formType = FPDF_GetFormType(m_handle);
	if (formType == FORMTYPE_NONE) {
		return stopped() ? std::nullopt : std::optional{std::move(result)};
	}
	if (formType != FORMTYPE_ACRO_FORM) {
		return {};
	}
	// form setup can rewrite dictionaries and appearances, even without input.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/cpdfsdk_pageview.cpp
	auto bytes = serialize(128 * 1024 * 1024, canceled);
	if (!bytes || stopped()) {
		return {};
	}
	const auto scratch = reopenFormCopy(std::move(*bytes));
	if (!scratch.document || stopped()) {
		return {};
	}
	FormContext context(scratch.document->m_handle, index);
	context.handle =
	    FPDFDOC_InitFormFillEnvironment(context.document, &context);
	if (!context.handle) {
		return {};
	}
	context.page.reset(FPDF_LoadPage(context.document, index));
	if (!context.page) {
		return {};
	}
	context.afterLoad();
	const int count = FPDFPage_GetAnnotCount(context.page.get());
	if (count < 0) {
		return {};
	}
	result.limited = count > 4096;
	std::size_t total = 0;
	int optionTotal = 0;
	FS_RECTF pageBox{};
	const float width = FPDF_GetPageWidthF(context.page.get());
	const float height = FPDF_GetPageHeightF(context.page.get());
	const bool usablePage =
	    std::isfinite(width) && width > 0 && std::isfinite(height) &&
	    height > 0 && FPDF_GetPageBoundingBox(context.page.get(), &pageBox) &&
	    std::isfinite(pageBox.left) && std::isfinite(pageBox.right) &&
	    std::isfinite(pageBox.bottom) && std::isfinite(pageBox.top) &&
	    pageBox.left < pageBox.right && pageBox.bottom < pageBox.top;
	result.limited = result.limited || !usablePage;
	const auto displayed =
	    [&](const FS_RECTF& rect) -> std::optional<AnnotationRect> {
		if (!usablePage) {
			result.limited = true;
			return {};
		}
		// i clip in PDF coordinates before asking for integer device points.
		const double left = (std::max)(rect.left, pageBox.left);
		const double right = (std::min)(rect.right, pageBox.right);
		const double bottom = (std::max)(rect.bottom, pageBox.bottom);
		const double top = (std::min)(rect.top, pageBox.top);
		if (left >= right || bottom >= top) {
			return {};
		}
		constexpr int grid = 1 << 20;
		int ax = 0, ay = 0, bx = 0, by = 0;
		if (!FPDF_PageToDevice(context.page.get(), 0, 0, grid, grid, 0, left,
		                       bottom, &ax, &ay) ||
		    !FPDF_PageToDevice(context.page.get(), 0, 0, grid, grid, 0, right,
		                       top, &bx, &by) ||
		    ax < -1 || ay < -1 || bx < -1 || by < -1 || ax > grid + 1 ||
		    ay > grid + 1 || bx > grid + 1 || by > grid + 1) {
			result.limited = true;
			return {};
		}
		AnnotationRect shown{
		    std::clamp((std::min)(ax, bx) / double(grid), 0., 1.),
		    std::clamp((std::min)(ay, by) / double(grid), 0., 1.),
		    std::clamp((std::max)(ax, bx) / double(grid), 0., 1.),
		    std::clamp((std::max)(ay, by) / double(grid), 0., 1.)};
		if (shown.left >= shown.right || shown.top >= shown.bottom) {
			result.limited = true;
			return {};
		}
		return shown;
	};
	const auto text =
	    [&](auto getter,
	        FPDF_ANNOTATION annotation) -> std::optional<std::string> {
		const auto size = getter(context.handle, annotation, nullptr, 0);
		if (size < 2 || size % 2 || size > 64 * 1024 ||
		    total + size > 1024 * 1024) {
			result.limited = true;
			return {};
		}
		total += size;
		std::vector<unsigned char> buffer(size);
		if (getter(context.handle, annotation,
		           reinterpret_cast<FPDF_WCHAR*>(buffer.data()),
		           size) != size ||
		    buffer[size - 1] || buffer[size - 2]) {
			result.limited = true;
			return {};
		}
		buffer.resize(size - 2);
		return utf16ToUtf8(buffer);
	};
	static_assert(static_cast<int>(FormFieldType::Signature) ==
	              FPDF_FORMFIELD_SIGNATURE);
	for (int i = 0; i < (std::min)(count, 4096); ++i) {
		if (stopped()) {
			return {};
		}
		AnnotationPtr annotation(FPDFPage_GetAnnot(context.page.get(), i));
		if (!annotation) {
			result.limited = true;
			continue;
		}
		if (FPDFAnnot_GetSubtype(annotation.get()) != FPDF_ANNOT_WIDGET) {
			continue;
		}
		const int type =
		    FPDFAnnot_GetFormFieldType(context.handle, annotation.get());
		if (type < 0 || type > FPDF_FORMFIELD_SIGNATURE) {
			result.limited = true;
			continue;
		}
		FormField field;
		field.index = i;
		field.type = static_cast<FormFieldType>(type);
		field.flags =
		    FPDFAnnot_GetFormFieldFlags(context.handle, annotation.get());
		field.annotationFlags = FPDFAnnot_GetFlags(annotation.get());
		if (field.flags & FPDF_FORMFLAG_CHOICE_MULTI_SELECT) {
			result.limited = true;
		}
		field.name =
		    text(FPDFAnnot_GetFormFieldName, annotation.get()).value_or("");
		field.value =
		    text(FPDFAnnot_GetFormFieldValue, annotation.get()).value_or("");
		if (field.type == FormFieldType::CheckBox ||
		    field.type == FormFieldType::RadioButton) {
			field.checked =
			    FPDFAnnot_IsChecked(context.handle, annotation.get()) != 0;
			// i keep exports separate from the shared value, especially for
			// radio groups with Opt arrays. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp
			const int controls =
			    FPDFAnnot_GetFormControlCount(context.handle, annotation.get());
			if (controls <= 0 || controls > 4096) {
				result.limited = true;
			} else {
				const int position = FPDFAnnot_GetFormControlIndex(
				    context.handle, annotation.get());
				if (position < 0 || position >= controls) {
					result.limited = true;
				} else if (auto exported =
				               text(FPDFAnnot_GetFormFieldExportValue,
				                    annotation.get())) {
					field.control =
					    FormControl{position, controls, std::move(*exported)};
				}
			}
		}
		if (field.type == FormFieldType::ComboBox ||
		    field.type == FormFieldType::ListBox) {
			const int options =
			    FPDFAnnot_GetOptionCount(context.handle, annotation.get());
			if (options < 0 || options > 4096 - optionTotal) {
				result.limited = true;
			} else {
				optionTotal += options;
				std::vector<FormOption> choices;
				choices.reserve(options);
				for (int option = 0; option < options; ++option) {
					if (stopped()) {
						return {};
					}
					auto label = text(
					    [option](auto handle, auto field, auto buffer,
					             auto size) {
						    return FPDFAnnot_GetOptionLabel(
						        handle, field, option, buffer, size);
					    },
					    annotation.get());
					if (!label) {
						break;
					}
					choices.push_back(
					    {option, std::move(*label),
					     FPDFAnnot_IsOptionSelected(
					         context.handle, annotation.get(), option) != 0});
				}
				if (choices.size() == static_cast<std::size_t>(options)) {
					field.options = std::move(choices);
				}
			}
		}
		FS_RECTF rect{};
		if (FPDFAnnot_GetValueType(annotation.get(), "Rect") ==
		        FPDF_OBJECT_ARRAY &&
		    FPDFAnnot_GetRect(annotation.get(), &rect) &&
		    std::isfinite(rect.left) && std::isfinite(rect.right) &&
		    std::isfinite(rect.bottom) && std::isfinite(rect.top) &&
		    rect.left < rect.right && rect.bottom < rect.top) {
			field.bounds =
			    AnnotationBounds{rect.left, rect.bottom, rect.right, rect.top};
			// i follow the SDK's visibility rule before exposing control
			// bounds. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/cpdfsdk_baannot.cpp
			if (!(field.annotationFlags &
			      (FPDF_ANNOT_FLAG_INVISIBLE | FPDF_ANNOT_FLAG_HIDDEN |
			       FPDF_ANNOT_FLAG_NOVIEW))) {
				field.displayBounds = displayed(rect);
			}
		} else {
			result.limited = true;
		}
		result.entries.push_back(std::move(field));
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
