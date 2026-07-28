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

namespace pdf::detail {
// what a page edit compares to prove each page landed where it should.
struct PageMark {
	float shortSide = 0, longSide = 0;
	int rotation = 0, annotations = 0, characters = 0;
	bool operator==(const PageMark&) const = default;
};
// the biggest bitmap render() makes: 16384 on a side, 256 MB in all
inline constexpr int MAX_SIDE = 16384;
inline constexpr long long MAX_PIXELS = 64LL * 1024 * 1024;

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

inline bool onDisplayedPage(AnnotationPoint point) {
	return point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1;
}

inline bool displayedAnnotationPoint(FPDF_PAGE page, int rotation,
                                     AnnotationPoint point, double& x,
                                     double& y) {
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

inline std::optional<BoxProofRegion> boxProofRegion(FPDF_PAGE page,
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
inline bool matchesBoxRendering(FPDF_PAGE page, int index, const FS_RECTF& rect,
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

inline constexpr int TEXT_GRID = 1 << 20;

inline std::optional<TextPage> pageText(FPDF_PAGE page, FPDF_TEXTPAGE text) {
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

inline std::optional<FPDF_ANNOTATION_SUBTYPE> markupSubtype(TextMarkup markup) {
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
inline bool offThread() {
	if (Engine::onThread()) {
		return false;
	}
	assert(false && "pdf:: called off the PDFium thread");
	return true;
}

inline std::optional<std::vector<int>>
annotationCounts(FPDF_DOCUMENT document, std::size_t& pageLoads) {
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

inline bool matchesAnnotationCounts(FPDF_DOCUMENT document,
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

inline bool uniqueMarkup(FPDF_DOCUMENT document, FPDF_PAGE own, int pageIndex,
                         int index, FPDF_ANNOTATION target,
                         std::size_t& pageLoads) {
	if (offThread() || FPDFPage_GetAnnotIndex(own, target) != index) {
		return false;
	}
	// i compare dictionary identity, including independent annotation arrays.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp
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

inline std::optional<AnnotationColor> markupColor(HighlightColor color) {
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

inline bool unlinkedAnnotation(FPDF_DOCUMENT document, FPDF_PAGE own,
                               int targetIndex, FPDF_ANNOTATION target,
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
inline Error fromPdfium(unsigned long code, bool gavePassword) {
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

inline void appendUtf8(std::string& out, char32_t c) {
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

inline bool validNoteText(std::u16string_view text) {
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
inline std::string utf16ToUtf8(const std::vector<unsigned char>& bytes) {
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

inline std::string utf16ToUtf8(std::u16string_view text) {
	std::vector<unsigned char> encoded;
	for (const auto unit : text) {
		encoded.push_back(static_cast<unsigned char>(unit & 0xFF));
		encoded.push_back(static_cast<unsigned char>(unit >> 8));
	}
	return utf16ToUtf8(encoded);
}
} // namespace pdf::detail
