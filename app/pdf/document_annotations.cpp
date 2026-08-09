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

namespace pdf {
using namespace detail;

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
	    canWrite(canceled) != WriteRefusal::None) {
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
	    canWrite(canceled) != WriteRefusal::None ||
	    !canSerializeEdits(canceled)) {
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
	auto scratch = transaction(canceled);
	if (!scratch.document || !scratch.canonicalize()) {
		return {};
	}
	scratch.document->invalidateWriteCopy();

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
	auto next = scratch.save();
	if (!next.document || !next.document->checkEditCounts(*expected, {index})) {
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
	if (!scratch.commit(std::move(next.document))) {
		return {};
	}
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
	    canWrite(canceled) != WriteRefusal::None || strokes.empty() ||
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
	const auto raw = inkAppearance(strokes, style);
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
	    canWrite(canceled) != WriteRefusal::None ||
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
	auto scratch = transaction(*bytes, canceled);
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
	auto next = scratch.save();
	if (!next.document ||
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
	if (!scratch.commit(std::move(next.document))) {
		return {};
	}
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

std::optional<int>
Document::addFillMarkAt(int index, AnnotationPoint point, int rotation,
                        FillMark mark, std::uint64_t revision,
                        const std::function<bool()>& canceled) {
	if (offThread() || index < 0 || index >= pageCount() || rotation < 0 ||
	    rotation > 3 || !onDisplayedPage(point) || !revision ||
	    !currentRevision(revision) || (canceled && canceled())) {
		return {};
	}
	PagePtr page(FPDF_LoadPage(m_handle, index));
	AnnotationPoint tl, tr, bl;
	if (!page ||
	    !displayedAnnotationPoint(page.get(), rotation, {0, 0}, tl.x, tl.y) ||
	    !displayedAnnotationPoint(page.get(), rotation, {1, 0}, tr.x, tr.y) ||
	    !displayedAnnotationPoint(page.get(), rotation, {0, 1}, bl.x, bl.y)) {
		return {};
	}
	page.reset();
	const double width = std::hypot(tr.x - tl.x, tr.y - tl.y);
	const double height = std::hypot(bl.x - tl.x, bl.y - tl.y);
	// @todo: offer a mark size. the inset includes the round stroke caps.
	constexpr double inset = 6.25;
	if (width < 2 * inset || height < 2 * inset) {
		return {};
	}
	const double x = std::clamp(point.x * width, inset, width - inset);
	const double y = std::clamp(point.y * height, inset, height - inset);
	const auto at = [&](double dx, double dy) {
		const double along = (x + dx) / width, down = (y + dy) / height;
		return InkPoint{static_cast<float>(tl.x + along * (tr.x - tl.x) +
		                                   down * (bl.x - tl.x)),
		                static_cast<float>(tl.y + along * (tr.y - tl.y) +
		                                   down * (bl.y - tl.y))};
	};
	switch (mark) {
	case FillMark::Check:
		return addInk(index, {{at(-5, 0), at(-1.5, 3.5), at(5, -5)}}, revision,
		              canceled);
	case FillMark::Cross:
		return addInk(index, {{at(-5, -5), at(5, 5)}, {at(-5, 5), at(5, -5)}},
		              revision, canceled);
	}
	return {};
}

std::optional<int> Document::addTextNote(int index, float x, float y,
                                         std::u16string_view contents,
                                         std::u16string_view author) {
	if (offThread() || index < 0 || index >= pageCount() || !canAnnotate() ||
	    contents.empty() || !validNoteText(contents) ||
	    !validNoteText(author) || !std::isfinite(x) || !std::isfinite(y) ||
	    canWrite({}) != WriteRefusal::None) {
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
	invalidateWriteCopy();
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
	    !validNoteText(contents) || !validNoteText(expected) ||
	    canWrite({}) != WriteRefusal::None) {
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
	invalidateWriteCopy();
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
	    !validNoteText(expected.text) || canWrite({}) != WriteRefusal::None) {
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
	invalidateWriteCopy();
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
	    !canSerializeEdits({}) || canWrite({}) != WriteRefusal::None) {
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
			invalidateWriteCopy();
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
	    !canSerializeEdits({}) || canWrite({}) != WriteRefusal::None ||
	    m_annotationRevision == (std::numeric_limits<std::uint64_t>::max)() ||
	    (target.subtype != "Text" && target.subtype != "Highlight" &&
	     target.subtype != "Underline" && target.subtype != "StrikeOut" &&
	     target.subtype != "FreeText" && target.subtype != "Square" &&
	     target.subtype != "Circle" && target.subtype != "Ink" &&
	     target.subtype != "Stamp")) {
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
	invalidateWriteCopy();
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
} // namespace pdf
