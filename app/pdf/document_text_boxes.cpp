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
} // namespace pdf
