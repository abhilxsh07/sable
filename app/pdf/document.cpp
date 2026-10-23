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
	const auto validText = [](std::u16string_view text) {
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
				if (++i == text.size() || text[i] < 0xDC00 ||
				    text[i] > 0xDFFF) {
					return false;
				}
			}
		}
		return true;
	};
	if (offThread() || index < 0 || index >= pageCount() || !canAnnotate() ||
	    contents.empty() || !validText(contents) || !validText(author) ||
	    !std::isfinite(x) || !std::isfinite(y)) {
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
	// i allocate terminated strings before changing the document.
	const std::u16string text(contents), name(author);
	AnnotationPtr note(FPDFPage_CreateAnnot(page.get(), FPDF_ANNOT_TEXT));
	if (!note) {
		return {};
	}
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
	    FPDFAnnot_SetFlags(note.get(), FPDF_ANNOT_FLAG_PRINT);
	note.reset();
	if (!complete) {
		// i remove only the appended note if a setter failed.
		if (!FPDFPage_RemoveAnnot(page.get(), count)) {
			throw std::runtime_error("Couldn't roll back the new note.");
		}
		return {};
	}
	return count;
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
