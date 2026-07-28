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

#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_text.h>

#include <algorithm>
#include <limits>
#include <set>

namespace pdf {
using namespace detail;
namespace {
// the page indices are unique and on the page; move keeps its own order.
bool validPages(const std::vector<int>& pages, int count) {
	if (pages.empty() || pages.size() > static_cast<std::size_t>(count)) {
		return false;
	}
	std::set<int> seen;
	for (const int page : pages) {
		if (page < 0 || page >= count || !seen.insert(page).second) {
			return false;
		}
	}
	return true;
}

std::optional<std::vector<PageMark>>
markPages(FPDF_DOCUMENT document, const std::function<bool()>& stopped) {
	const int count = FPDF_GetPageCount(document);
	if (count <= 0) {
		return {};
	}
	std::vector<PageMark> marks;
	marks.reserve(static_cast<std::size_t>(count));
	for (int index = 0; index < count; ++index) {
		if (stopped()) {
			return {};
		}
		PagePtr page(FPDF_LoadPage(document, index));
		if (!page) {
			return {};
		}
		PageMark mark;
		const float width = FPDF_GetPageWidthF(page.get());
		const float height = FPDF_GetPageHeightF(page.get());
		// the sides without their order, so a turn doesn't change them.
		mark.shortSide = (std::min)(width, height);
		mark.longSide = (std::max)(width, height);
		mark.rotation = FPDFPage_GetRotation(page.get());
		mark.annotations = FPDFPage_GetAnnotCount(page.get());
		TextPtr text(FPDFText_LoadPage(page.get()));
		mark.characters = text ? FPDFText_CountChars(text.get()) : -1;
		if (mark.rotation < 0 || mark.annotations < 0 || mark.characters < 0) {
			return {};
		}
		marks.push_back(mark);
	}
	return marks;
}
} // namespace

bool Document::canAssemble() const {
	if (offThread()) {
		return false;
	}
	// an unprotected file reports every flag. bit 4 allows changes and
	// bit 11 assembling pages on its own (PDF 32000-1, table 22). see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/public/fpdfview.h
	const auto permissions = FPDF_GetDocPermissions(m_handle);
	const int form = FPDF_GetFormType(m_handle);
	return (permissions & (0x8 | 0x400)) != 0 &&
	       (form == FORMTYPE_NONE || form == FORMTYPE_ACRO_FORM);
}

bool Document::rewritePages(std::uint64_t revision,
                            const std::function<bool()>& canceled,
                            const std::function<bool(FPDF_DOCUMENT)>& apply,
                            const PageExpectation& expect) {
	if (offThread() || !revision || !currentRevision(revision) ||
	    revision == (std::numeric_limits<std::uint64_t>::max)() ||
	    !canAssemble() || (canceled && canceled())) {
		return false;
	}
	auto scratch = transaction(canceled);
	const std::function<bool()> stopped = [&scratch] {
		return scratch.stopped();
	};
	if (!scratch.document || !scratch.canonicalize()) {
		return false;
	}
	scratch.document->invalidateWriteCopy();
	const auto before = markPages(scratch.document->m_handle, stopped);
	if (!before) {
		return false;
	}
	const auto expected = expect(*before);
	if (!expected || !apply(scratch.document->m_handle) || stopped()) {
		return false;
	}
	scratch.pages = static_cast<int>(expected->size());
	auto next = scratch.save();
	if (!next.document || stopped()) {
		return false;
	}
	// each page has to land where the edit says, with its own size,
	// rotation, notes and text.
	const auto after = markPages(next.document->m_handle, stopped);
	if (!after || *after != *expected) {
		return false;
	}
	return scratch.commit(std::move(next.document));
}

bool Document::rotatePages(const std::vector<int>& pages, int turns,
                           std::uint64_t revision,
                           const std::function<bool()>& canceled) {
	const int quarter = (turns % 4 + 4) % 4;
	if (offThread() || !quarter || !validPages(pages, pageCount())) {
		return false;
	}
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/public/fpdf_edit.h
	return rewritePages(
	    revision, canceled,
	    [&](FPDF_DOCUMENT document) {
		    for (const int index : pages) {
			    PagePtr page(FPDF_LoadPage(document, index));
			    const int rotation =
			        page ? FPDFPage_GetRotation(page.get()) : -1;
			    if (rotation < 0) {
				    return false;
			    }
			    FPDFPage_SetRotation(page.get(), (rotation + quarter) % 4);
		    }
		    return true;
	    },
	    [&](std::vector<PageMark> marks)
	        -> std::optional<std::vector<PageMark>> {
		    for (const int index : pages) {
			    auto& mark = marks[static_cast<std::size_t>(index)];
			    mark.rotation = (mark.rotation + quarter) % 4;
		    }
		    return marks;
	    });
}

bool Document::deletePages(const std::vector<int>& pages,
                           std::uint64_t revision,
                           const std::function<bool()>& canceled) {
	const int count = offThread() ? 0 : pageCount();
	// a PDF needs at least one page.
	if (!validPages(pages, count) ||
	    pages.size() >= static_cast<std::size_t>(count)) {
		return false;
	}
	std::vector<int> descending(pages);
	std::sort(descending.rbegin(), descending.rend());
	return rewritePages(
	    revision, canceled,
	    [&](FPDF_DOCUMENT document) {
		    for (const int index : descending) {
			    FPDFPage_Delete(document, index);
		    }
		    return FPDF_GetPageCount(document) ==
		           count - static_cast<int>(pages.size());
	    },
	    [&](const std::vector<PageMark>& marks)
	        -> std::optional<std::vector<PageMark>> {
		    const std::set<int> gone(pages.begin(), pages.end());
		    std::vector<PageMark> kept;
		    for (int index = 0; index < static_cast<int>(marks.size());
		         ++index) {
			    if (!gone.contains(index)) {
				    kept.push_back(marks[static_cast<std::size_t>(index)]);
			    }
		    }
		    return kept;
	    });
}

bool Document::movePages(const std::vector<int>& pages, int destination,
                         std::uint64_t revision,
                         const std::function<bool()>& canceled) {
	const int count = offThread() ? 0 : pageCount();
	if (!validPages(pages, count) || destination < 0 ||
	    destination > count - static_cast<int>(pages.size())) {
		return false;
	}
	// the moved pages start at destination once they've been taken out.
	return rewritePages(
	    revision, canceled,
	    [&](FPDF_DOCUMENT document) {
		    return FPDF_MovePages(document, pages.data(),
		                          static_cast<unsigned long>(pages.size()),
		                          destination) != 0;
	    },
	    [&](const std::vector<PageMark>& marks)
	        -> std::optional<std::vector<PageMark>> {
		    const std::set<int> moving(pages.begin(), pages.end());
		    std::vector<PageMark> rest;
		    for (int index = 0; index < static_cast<int>(marks.size());
		         ++index) {
			    if (!moving.contains(index)) {
				    rest.push_back(marks[static_cast<std::size_t>(index)]);
			    }
		    }
		    std::vector<PageMark> block;
		    for (const int index : pages) {
			    block.push_back(marks[static_cast<std::size_t>(index)]);
		    }
		    rest.insert(rest.begin() + destination, block.begin(), block.end());
		    return rest;
	    });
}
} // namespace pdf
