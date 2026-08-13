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
#include "document_internal.h"

#include "../helpers/lines.h"

#include <cstdio>

namespace pdf {
using namespace detail;
namespace {
std::optional<std::u16string> appearanceText(FPDF_ANNOTATION note, int mode,
                                             unsigned long most = 65536) {
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
}

bool validInkStyle(const InkStyle& style) {
	return std::isfinite(style.width) && style.width >= .5f &&
	       style.width <= 12 && style.color.red <= 255 &&
	       style.color.green <= 255 && style.color.blue <= 255 &&
	       style.color.alpha == 255;
}

// rect covering half the line width around every point, as addInk sets it
std::optional<AnnotationBounds> inkBounds(const std::vector<InkStroke>& strokes,
                                          float width) {
	double left = std::numeric_limits<double>::infinity(), bottom = left;
	double right = -left, top = -left;
	for (const auto& stroke : strokes) {
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
	const double half = double(width) / 2;
	return AnnotationBounds{
	    static_cast<float>(left - half), static_cast<float>(bottom - half),
	    static_cast<float>(right + half), static_cast<float>(top + half)};
}
} // namespace

std::optional<std::vector<std::string>>
Document::editNeighbors(const std::vector<int>& counts,
                        const AnnotationTarget& target,
                        const std::function<bool()>& canceled) const {
	constexpr std::size_t limit = 128 * 1024 * 1024;
	std::vector<std::string> result;
	std::size_t total = 0;
	for (int p = 0; p < pageCount(); ++p) {
		PagePtr page(FPDF_LoadPage(m_handle, p));
		if (!page || p >= static_cast<int>(counts.size()) ||
		    FPDFPage_GetAnnotCount(page.get()) != counts[p]) {
			return {};
		}
		const auto snapshot = annotations(p, canceled);
		if (!snapshot || snapshot->limited ||
		    snapshot->entries.size() != static_cast<std::size_t>(counts[p])) {
			return {};
		}
		for (int i = 0; i < counts[p]; ++i) {
			if (p == target.page && i == target.index) {
				continue;
			}
			const auto& entry = snapshot->entries[i];
			std::string print = entry.subtype + '\n' + entry.contents + '\n' +
			                    entry.author + '\n' +
			                    std::to_string(entry.flags);
			if (entry.bounds) {
				const auto& r = *entry.bounds;
				for (const float edge : {r.left, r.bottom, r.right, r.top}) {
					print += ' ' + std::to_string(edge);
				}
			}
			AnnotationPtr note(FPDFPage_GetAnnot(page.get(), i));
			for (int mode = 0; mode < 3; ++mode) {
				const auto text =
				    note ? appearanceText(
				               note.get(), mode,
				               static_cast<unsigned long>(limit - total + 2))
				         : std::nullopt;
				if (!text || (canceled && canceled())) {
					return {};
				}
				total += 2 * text->size() + 2;
				if (total > limit) {
					return {};
				}
				print += '\n' + utf16ToUtf8(*text);
			}
			result.push_back(std::move(print));
		}
	}
	return result;
}

std::optional<InkPoint> Document::displayedOffset(int index,
                                                  AnnotationPoint from,
                                                  AnnotationPoint to,
                                                  int rotation) const {
	if (offThread() || index < 0 || index >= pageCount() || rotation < 0 ||
	    rotation > 3) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	if (!page) {
		return {};
	}
	// the same fine device grid as the shape corners.
	constexpr int grid = 1 << 20;
	double fromX = 0, fromY = 0, toX = 0, toY = 0;
	const auto place = [&](AnnotationPoint point, double& x, double& y) {
		return point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1 &&
		       FPDF_DeviceToPage(page.get(), 0, 0, grid, grid, rotation,
		                         static_cast<int>(std::lround(point.x * grid)),
		                         static_cast<int>(std::lround(point.y * grid)),
		                         &x, &y) &&
		       std::isfinite(x) && std::isfinite(y);
	};
	if (!place(from, fromX, fromY) || !place(to, toX, toY)) {
		return {};
	}
	return InkPoint{static_cast<float>(toX - fromX),
	                static_cast<float>(toY - fromY)};
}

std::optional<bool> Document::editInkAt(const Annotation& expected,
                                        AnnotationPoint from,
                                        AnnotationPoint to, int rotation,
                                        std::optional<AnnotationColor> color,
                                        std::optional<float> width,
                                        const std::function<bool()>& canceled) {
	const auto offset =
	    displayedOffset(expected.target.page, from, to, rotation);
	if (!offset) {
		return {};
	}
	return editInk(expected, *offset, color, width, canceled);
}
std::optional<bool> Document::editInk(const Annotation& expected,
                                      InkPoint offset,
                                      std::optional<AnnotationColor> color,
                                      std::optional<float> width,
                                      const std::function<bool()>& canceled,
                                      std::optional<float> lineLength) {
	const auto stopped = [&] { return canceled && canceled(); };
	const auto& target = expected.target;
	if (offThread() || stopped() || !expected.bounds ||
	    (lineLength && (expected.line == LineKind::None || offset.x != 0 ||
	                    offset.y != 0 || !std::isfinite(*lineLength))) ||
	    target.subtype != "Ink" || expected.subtype != target.subtype ||
	    expected.index != target.index || !expected.contents.empty() ||
	    !expected.author.empty() || expected.flags != FPDF_ANNOT_FLAG_PRINT ||
	    !std::isfinite(offset.x) || !std::isfinite(offset.y) ||
	    std::abs(offset.x) > 100000.f || std::abs(offset.y) > 100000.f ||
	    m_annotationRevision == (std::numeric_limits<std::uint64_t>::max)() ||
	    !matchesTarget(target) || !canAnnotate() ||
	    !canSerializeEdits(canceled) ||
	    canWrite(canceled) != WriteRefusal::None ||
	    !provenUnique(target.page, target.index)) {
		return {};
	}
	const auto bounds = *expected.bounds;
	const auto notes = annotations(target.page, canceled);
	if (!notes || notes->limited) {
		return {};
	}
	const auto found =
	    std::find_if(notes->entries.begin(), notes->entries.end(),
	                 [&](const auto& note) { return note.target == target; });
	if (found == notes->entries.end() || !found->bounds ||
	    found->bounds->left != bounds.left ||
	    found->bounds->bottom != bounds.bottom ||
	    found->bounds->right != bounds.right ||
	    found->bounds->top != bounds.top || found->flags != expected.flags ||
	    !found->contents.empty() || !found->author.empty()) {
		return {};
	}
	// read the drawing back and rebuild it: only a drawing whose appearance
	// is exactly what addInk writes for its InkList and style gets edited.
	std::vector<InkStroke> strokes;
	InkStyle old;
	std::u16string oldAppearance;
	FS_RECTF box{};
	{
		PagePtr page(FPDF_LoadPage(m_handle, target.page));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
		                        : nullptr);
		if (!note || !m_linkedPages ||
		    !unlinkedAnnotation(m_handle, page.get(), target.index, note.get(),
		                        *m_linkedPages, m_editPageLoads) ||
		    !FPDF_GetPageBoundingBox(page.get(), &box)) {
			return {};
		}
		// Subj is allowed only as the Line or Arrow addInk writes, checked
		// against the strokes below.
		for (const auto* key :
		     {"Contents", "T",           "NM", "M",      "CreationDate",
		      "RC",       "DS",          "IC", "BS",     "BE",
		      "BM",       "RD",          "OC", "Rotate", "A",
		      "AA",       "IT",          "AS", "IRT",    "Popup",
		      "ExData",   "StructParent"}) {
			if (FPDFAnnot_HasKey(note.get(), key)) {
				return {};
			}
		}
		// SetColor writes the alpha as CA, which is always opaque here.
		float opacity = 1;
		if (FPDFAnnot_HasKey(note.get(), "CA") &&
		    (!FPDFAnnot_GetNumberValue(note.get(), "CA", &opacity) ||
		     opacity != 1)) {
			return {};
		}
		const auto count = FPDFAnnot_GetInkListCount(note.get());
		if (count < 1 || count > 256) {
			return {};
		}
		std::size_t total = 0;
		for (unsigned long i = 0; i < count; ++i) {
			const auto size =
			    FPDFAnnot_GetInkListPath(note.get(), i, nullptr, 0);
			total += size;
			if (size < 1 || size > 4096 || total > 16384) {
				return {};
			}
			std::vector<FS_POINTF> path(size);
			if (FPDFAnnot_GetInkListPath(note.get(), i, path.data(), size) !=
			    size) {
				return {};
			}
			auto& stroke = strokes.emplace_back();
			for (const auto& point : path) {
				stroke.push_back({point.x, point.y});
			}
		}
		if (FPDFAnnot_HasKey(note.get(), "Subj")) {
			const auto size =
			    FPDFAnnot_GetStringValue(note.get(), "Subj", nullptr, 0);
			std::vector<FPDF_WCHAR> subject(size / sizeof(FPDF_WCHAR) + 1);
			if (size < 2 || size > 64 ||
			    FPDFAnnot_GetStringValue(note.get(), "Subj", subject.data(),
			                             size) != size) {
				return {};
			}
			const std::u16string value(subject.begin(),
			                           subject.begin() + size / 2 - 1);
			const auto kind = value == u"Line"    ? LineKind::Line
			                  : value == u"Arrow" ? LineKind::Arrow
			                                      : LineKind::None;
			if (kind == LineKind::None || helpers::lineShape(strokes) != kind) {
				return {};
			}
		} else if (lineLength) {
			return {};
		}
		float horizontal = -1, vertical = -1, border = -1;
		const auto normal = appearanceText(note.get(), 0);
		if (!FPDFAnnot_GetBorder(note.get(), &horizontal, &vertical, &border) ||
		    horizontal != 0 || vertical != 0 || !normal ||
		    appearanceText(note.get(), 1) != std::optional{std::u16string{}} ||
		    appearanceText(note.get(), 2) != std::optional{std::u16string{}}) {
			return {};
		}
		// the color is only in the appearance: PDFium won't read /C while
		// one exists. rebuilding the stream proves the reading.
		double red = -1, green = -1, blue = -1;
		std::string head;
		for (std::size_t i = 0; i < normal->size() && i < 64; ++i) {
			head.push_back(static_cast<char>((*normal)[i] & 0x7f));
		}
		if (sscanf_s(head.c_str(), "%lf %lf %lf RG", &red, &green, &blue) !=
		        3 ||
		    !(red >= 0 && red <= 1 && green >= 0 && green <= 1 && blue >= 0 &&
		      blue <= 1)) {
			return {};
		}
		old.color = {static_cast<unsigned>(std::lround(red * 255)),
		             static_cast<unsigned>(std::lround(green * 255)),
		             static_cast<unsigned>(std::lround(blue * 255)), 255};
		old.width = border;
		const auto rebuilt = inkAppearance(strokes, old);
		const auto rect = inkBounds(strokes, old.width);
		if (!validInkStyle(old) ||
		    *normal != std::u16string(rebuilt.begin(), rebuilt.end()) ||
		    !rect || rect->left != bounds.left ||
		    rect->bottom != bounds.bottom || rect->right != bounds.right ||
		    rect->top != bounds.top) {
			return {};
		}
		oldAppearance = *normal;
	}
	InkStyle style = old;
	if (color) {
		style.color = *color;
	}
	if (width) {
		style.width = *width;
	}
	auto moved = strokes;
	if (lineLength) {
		auto resized = helpers::resizedLine(strokes, *lineLength, style.width);
		if (!resized) {
			return {};
		}
		moved = std::move(*resized);
	}
	for (auto& stroke : moved) {
		for (auto& point : stroke) {
			point.x += offset.x;
			point.y += offset.y;
		}
	}
	const auto destination = inkBounds(moved, style.width);
	if (!validInkStyle(style) || !destination ||
	    !(destination->left >= box.left && destination->bottom >= box.bottom &&
	      destination->right <= box.right && destination->top <= box.top)) {
		return {};
	}
	if (moved == strokes && style == old) {
		return false;
	}
	return rewriteInk(target, bounds, oldAppearance, moved, style, *destination,
	                  canceled);
}
// the proven drawing gets its new InkList, style, rect and appearance in
// place, so its index, its page order and every other annotation stay put.
std::optional<bool> Document::rewriteInk(
    const AnnotationTarget& target, const AnnotationBounds& bounds,
    const std::u16string& oldAppearance, const std::vector<InkStroke>& strokes,
    const InkStyle& style, const AnnotationBounds& destination,
    const std::function<bool()>& canceled) {
	const auto stopped = [&] { return canceled && canceled(); };
	constexpr std::size_t limit = 128 * 1024 * 1024;
	auto bytes = serialize(limit, canceled);
	if (!bytes || stopped()) {
		return {};
	}
	{
		// an odd BBox or Matrix could draw the same stream somewhere else.
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
	auto scratch = transaction(std::move(*bytes), canceled);
	const auto counts = editCounts();
	if (!scratch.document || !counts) {
		return {};
	}
	const auto before =
	    scratch.document->editNeighbors(*counts, target, canceled);
	const auto raw = inkAppearance(strokes, style);
	const std::u16string appearance(raw.begin(), raw.end());
	{
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, target.page));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
		                        : nullptr);
		const FS_RECTF rect{destination.left, destination.top,
		                    destination.right, destination.bottom};
		// the old appearance goes first: PDFium won't set a color while one
		// exists, and drops it when the border changes.
		if (!before || !note ||
		    !FPDFAnnot_SetAP(note.get(), FPDF_ANNOT_APPEARANCEMODE_NORMAL,
		                     nullptr) ||
		    !FPDFAnnot_SetColor(note.get(), FPDFANNOT_COLORTYPE_Color,
		                        style.color.red, style.color.green,
		                        style.color.blue, 255) ||
		    !FPDFAnnot_SetBorder(note.get(), 0, 0, style.width) ||
		    !FPDFAnnot_RemoveInkList(note.get())) {
			return {};
		}
		for (std::size_t i = 0; i < strokes.size(); ++i) {
			std::vector<FS_POINTF> path;
			for (const auto& [x, y] : strokes[i]) {
				path.push_back({x, y});
			}
			if (FPDFAnnot_AddInkStroke(note.get(), path.data(), path.size()) !=
			    static_cast<int>(i)) {
				return {};
			}
		}
		if (!FPDFAnnot_SetRect(note.get(), &rect) ||
		    !FPDFAnnot_SetAP(
		        note.get(), FPDF_ANNOT_APPEARANCEMODE_NORMAL,
		        reinterpret_cast<FPDF_WIDESTRING>(appearance.c_str()))) {
			return {};
		}
	}
	scratch.document->invalidateEdits();
	auto next = scratch.save();
	if (!next.document ||
	    !next.document->checkEditCounts(*counts, {target.page}) ||
	    next.document->editNeighbors(*counts, target, canceled) != before) {
		return {};
	}
	{
		PagePtr page(FPDF_LoadPage(next.document->m_handle, target.page));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
		                        : nullptr);
		FS_RECTF rect{};
		float horizontal = -1, vertical = -1, border = -1;
		if (!note || FPDFAnnot_GetSubtype(note.get()) != FPDF_ANNOT_INK ||
		    FPDFAnnot_GetFlags(note.get()) != FPDF_ANNOT_FLAG_PRINT ||
		    !FPDFAnnot_GetRect(note.get(), &rect) ||
		    rect.left != destination.left ||
		    rect.bottom != destination.bottom ||
		    rect.right != destination.right || rect.top != destination.top ||
		    appearanceText(note.get(), 0) != std::optional{appearance} ||
		    !FPDFAnnot_GetBorder(note.get(), &horizontal, &vertical, &border) ||
		    border != style.width ||
		    FPDFAnnot_GetInkListCount(note.get()) != strokes.size()) {
			return {};
		}
		for (std::size_t i = 0; i < strokes.size(); ++i) {
			const auto size = static_cast<unsigned long>(strokes[i].size());
			std::vector<FS_POINTF> path(size);
			if (FPDFAnnot_GetInkListPath(note.get(),
			                             static_cast<unsigned long>(i),
			                             path.data(), size) != size ||
			    !std::equal(path.begin(), path.end(), strokes[i].begin(),
			                [](const FS_POINTF& a, const InkPoint& b) {
				                return a.x == b.x && a.y == b.y;
			                })) {
				return {};
			}
		}
	}
	if (!scratch.commit(std::move(next.document))) {
		return {};
	}
	return true;
}
std::optional<bool>
Document::resizeLine(const Annotation& expected, float length,
                     const std::function<bool()>& canceled) {
	return editInk(expected, {0, 0}, {}, {}, canceled, length);
}
std::optional<bool>
Document::editStampAt(const Annotation& expected, AnnotationPoint from,
                      AnnotationPoint to, int rotation,
                      std::optional<float> width,
                      const std::function<bool()>& canceled) {
	const auto offset =
	    displayedOffset(expected.target.page, from, to, rotation);
	if (!offset) {
		return {};
	}
	return editStamp(expected, *offset, width, canceled);
}

std::optional<bool> Document::editStamp(const Annotation& expected,
                                        InkPoint offset,
                                        std::optional<float> width,
                                        const std::function<bool()>& canceled) {
	const auto stopped = [&] { return canceled && canceled(); };
	const auto& target = expected.target;
	if (offThread() || stopped() || !expected.bounds ||
	    target.subtype != "Stamp" || expected.subtype != target.subtype ||
	    expected.index != target.index || !expected.contents.empty() ||
	    !expected.author.empty() || expected.flags != FPDF_ANNOT_FLAG_PRINT ||
	    !std::isfinite(offset.x) || !std::isfinite(offset.y) ||
	    std::abs(offset.x) > 100000.f || std::abs(offset.y) > 100000.f ||
	    (width &&
	     !(std::isfinite(*width) && *width >= 8 && *width <= 100000)) ||
	    m_annotationRevision == (std::numeric_limits<std::uint64_t>::max)() ||
	    !matchesTarget(target) || !canAnnotate() ||
	    !canSerializeEdits(canceled) ||
	    canWrite(canceled) != WriteRefusal::None ||
	    !provenUnique(target.page, target.index)) {
		return {};
	}
	const auto bounds = *expected.bounds;
	const auto notes = annotations(target.page, canceled);
	if (!notes || notes->limited) {
		return {};
	}
	const auto found =
	    std::find_if(notes->entries.begin(), notes->entries.end(),
	                 [&](const auto& note) { return note.target == target; });
	if (found == notes->entries.end() || !found->bounds ||
	    found->bounds->left != bounds.left ||
	    found->bounds->bottom != bounds.bottom ||
	    found->bounds->right != bounds.right ||
	    found->bounds->top != bounds.top || found->flags != expected.flags ||
	    !found->contents.empty() || !found->author.empty()) {
		return {};
	}
	// the stamp has to be the one image object addStampAt writes, with its
	// rect the box around the image's four corners.
	FS_MATRIX old{};
	unsigned pixelsWide = 0, pixelsHigh = 0;
	std::vector<std::uint8_t> pixels;
	FS_RECTF box{};
	const auto corners = [](const FS_MATRIX& m) {
		const double xs[] = {m.e, m.e + m.a, m.e + m.c, m.e + m.a + m.c};
		const double ys[] = {m.f, m.f + m.b, m.f + m.d, m.f + m.b + m.d};
		return AnnotationBounds{
		    static_cast<float>(*std::min_element(std::begin(xs), std::end(xs))),
		    static_cast<float>(*std::min_element(std::begin(ys), std::end(ys))),
		    static_cast<float>(*std::max_element(std::begin(xs), std::end(xs))),
		    static_cast<float>(
		        *std::max_element(std::begin(ys), std::end(ys)))};
	};
	const auto decoded = [](FPDF_PAGEOBJECT object) {
		std::vector<std::uint8_t> data;
		const auto size = FPDFImageObj_GetImageDataDecoded(object, nullptr, 0);
		if (size && size <= 64ul * 1024 * 1024) {
			data.resize(size);
			if (FPDFImageObj_GetImageDataDecoded(object, data.data(), size) !=
			    size) {
				data.clear();
			}
		}
		return data;
	};
	{
		PagePtr page(FPDF_LoadPage(m_handle, target.page));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
		                        : nullptr);
		if (!note || !m_linkedPages ||
		    !unlinkedAnnotation(m_handle, page.get(), target.index, note.get(),
		                        *m_linkedPages, m_editPageLoads) ||
		    !FPDF_GetPageBoundingBox(page.get(), &box)) {
			return {};
		}
		for (const auto* key : {"Contents",
		                        "T",
		                        "Subj",
		                        "NM",
		                        "M",
		                        "CreationDate",
		                        "RC",
		                        "DS",
		                        "IC",
		                        "BS",
		                        "BE",
		                        "BM",
		                        "RD",
		                        "OC",
		                        "Rotate",
		                        "A",
		                        "AA",
		                        "IT",
		                        "AS",
		                        "IRT",
		                        "Popup",
		                        "ExData",
		                        "StructParent",
		                        "CA"}) {
			if (FPDFAnnot_HasKey(note.get(), key)) {
				return {};
			}
		}
		const auto object = FPDFAnnot_GetObjectCount(note.get()) == 1
		                        ? FPDFAnnot_GetObject(note.get(), 0)
		                        : nullptr;
		if (!object || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_IMAGE ||
		    !FPDFImageObj_GetImagePixelSize(object, &pixelsWide, &pixelsHigh) ||
		    !pixelsWide || !pixelsHigh || pixelsWide > StampImage::maxSide ||
		    pixelsHigh > StampImage::maxSide ||
		    !FPDFPageObj_GetMatrix(object, &old)) {
			return {};
		}
		pixels = decoded(object);
		if (pixels.empty()) {
			return {};
		}
	}
	// the stamp's appearance keeps the box it was made with, and a viewer
	// fits that box to the rect, so a new rect alone moves and scales the
	// image without touching it. the image's top-left corner on the turn
	// it was placed at stays put.
	const auto made = corners(old);
	const double madeWidth = double(made.right) - made.left;
	const double madeHeight = double(made.top) - made.bottom;
	const double shown = (double(bounds.right) - bounds.left) / madeWidth;
	if (!(madeWidth >= 1 && madeHeight >= 1) ||
	    std::abs(shown * madeHeight - (double(bounds.top) - bounds.bottom)) >
	        .01) {
		return {};
	}
	const double scale = width ? double(*width) / madeWidth : shown;
	const double cornerX = double(old.c) + old.e - made.left;
	const double cornerY = double(old.d) + old.f - made.bottom;
	const double topX = bounds.left + cornerX * shown + offset.x;
	const double topY = bounds.bottom + cornerY * shown + offset.y;
	AnnotationBounds destination{};
	destination.left = static_cast<float>(topX - cornerX * scale);
	destination.bottom = static_cast<float>(topY - cornerY * scale);
	destination.right =
	    static_cast<float>(destination.left + madeWidth * scale);
	destination.top =
	    static_cast<float>(destination.bottom + madeHeight * scale);
	if (!(destination.left >= box.left && destination.bottom >= box.bottom &&
	      destination.right <= box.right && destination.top <= box.top) ||
	    destination.right - destination.left < 1 ||
	    destination.top - destination.bottom < 1) {
		return {};
	}
	if (destination.left == bounds.left &&
	    destination.bottom == bounds.bottom &&
	    destination.right == bounds.right && destination.top == bounds.top) {
		return false;
	}
	constexpr std::size_t limit = 128 * 1024 * 1024;
	auto bytes = serialize(limit, canceled);
	if (!bytes || stopped()) {
		return {};
	}
	const auto regenerate = [](FPDF_ANNOTATION note) {
		const auto object = FPDFAnnot_GetObject(note, 0);
		return object && FPDFAnnot_UpdateObject(note, object);
	};
	{
		// the stored appearance has to draw what PDFium makes of the image
		// object, so an odd BBox or Matrix can't hide where the stamp is.
		auto proof = reopenFormCopy(*bytes);
		PagePtr page(proof.document
		                 ? FPDF_LoadPage(proof.document->m_handle, target.page)
		                 : nullptr);
		const FS_RECTF rect{bounds.left, bounds.top, bounds.right,
		                    bounds.bottom};
		if (!page || !matchesReplacedRendering(page.get(), target.index, rect,
		                                       regenerate, stopped)) {
			return {};
		}
	}
	auto scratch = transaction(std::move(*bytes), canceled);
	const auto counts = editCounts();
	if (!scratch.document || !counts) {
		return {};
	}
	const auto before =
	    scratch.document->editNeighbors(*counts, target, canceled);
	{
		PagePtr page(FPDF_LoadPage(scratch.document->m_handle, target.page));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
		                        : nullptr);
		const auto object = note ? FPDFAnnot_GetObject(note.get(), 0) : nullptr;
		const FS_RECTF rect{destination.left, destination.top,
		                    destination.right, destination.bottom};
		if (!before || !object || !FPDFAnnot_SetRect(note.get(), &rect)) {
			return {};
		}
	}
	scratch.document->invalidateEdits();
	auto saved = scratch.save();
	if (!saved.document ||
	    !saved.document->checkEditCounts(*counts, {target.page}) ||
	    saved.document->editNeighbors(*counts, target, canceled) != before) {
		return {};
	}
	{
		PagePtr page(FPDF_LoadPage(saved.document->m_handle, target.page));
		AnnotationPtr note(page ? FPDFPage_GetAnnot(page.get(), target.index)
		                        : nullptr);
		FS_RECTF rect{};
		FS_MATRIX kept{};
		unsigned keptWide = 0, keptHigh = 0;
		const auto object = note && FPDFAnnot_GetObjectCount(note.get()) == 1
		                        ? FPDFAnnot_GetObject(note.get(), 0)
		                        : nullptr;
		const auto same = [](float a, float b) {
			return std::abs(a - b) < .001f;
		};
		if (!object || FPDFAnnot_GetSubtype(note.get()) != FPDF_ANNOT_STAMP ||
		    FPDFAnnot_GetFlags(note.get()) != FPDF_ANNOT_FLAG_PRINT ||
		    !FPDFAnnot_GetRect(note.get(), &rect) ||
		    rect.left != destination.left ||
		    rect.bottom != destination.bottom ||
		    rect.right != destination.right || rect.top != destination.top ||
		    FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_IMAGE ||
		    !FPDFImageObj_GetImagePixelSize(object, &keptWide, &keptHigh) ||
		    keptWide != pixelsWide || keptHigh != pixelsHigh ||
		    !FPDFPageObj_GetMatrix(object, &kept) || !same(kept.a, old.a) ||
		    !same(kept.b, old.b) || !same(kept.c, old.c) ||
		    !same(kept.d, old.d) || !same(kept.e, old.e) ||
		    !same(kept.f, old.f) || decoded(object) != pixels) {
			return {};
		}
	}
	if (!scratch.commit(std::move(saved.document))) {
		return {};
	}
	return true;
}
} // namespace pdf
