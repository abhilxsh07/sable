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
#include <fpdf_ppo.h>
#include <fpdf_text.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
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
namespace {
struct DocumentCloser {
	void operator()(fpdf_document_t__* document) const {
		FPDF_CloseDocument(document);
	}
};
using DocumentPtr = std::unique_ptr<fpdf_document_t__, DocumentCloser>;

// a bounded, cancelable writer for PDFs that don't come from an open file.
struct BoundedWriter : FPDF_FILEWRITE {
	BoundedWriter(std::size_t bound, const std::function<bool()>& cancel)
	    : FPDF_FILEWRITE{1, &write}, limit(bound), canceled(cancel) {}
	static int write(FPDF_FILEWRITE* base, const void* data,
	                 unsigned long size) noexcept {
		auto& self = *static_cast<BoundedWriter*>(base);
		try {
			if (self.failed || (self.canceled && self.canceled())) {
				self.failed = true;
				return 0;
			}
			if (size > self.limit - self.bytes.size()) {
				self.overflow = self.failed = true;
				return 0;
			}
			if (size && !data) {
				self.failed = true;
				return 0;
			}
			const auto* first = static_cast<const std::uint8_t*>(data);
			self.bytes.insert(self.bytes.end(), first, first + size);
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
};

DocumentPtr loadBytes(const std::vector<std::uint8_t>& bytes) {
	return DocumentPtr(
	    FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr));
}
} // namespace

bool Document::insertPages(const std::vector<std::uint8_t>& source,
                           const std::vector<int>& pages, int index,
                           std::uint64_t revision,
                           const std::function<bool()>& canceled) {
	const int count = offThread() ? 0 : pageCount();
	if (source.empty() || source.size() > EDIT_LIMIT || index < 0 ||
	    index > count) {
		return false;
	}
	const std::function<bool()> stopped = [&canceled] {
		return canceled && canceled();
	};
	// the source is only read; a password-protected one doesn't open.
	const auto from = loadBytes(source);
	const auto marks = from ? markPages(from.get(), stopped) : std::nullopt;
	if (!marks) {
		return false;
	}
	std::vector<int> chosen(pages);
	if (chosen.empty()) {
		chosen.resize(marks->size());
		for (std::size_t page = 0; page < chosen.size(); ++page) {
			chosen[page] = static_cast<int>(page);
		}
	} else if (!validPages(chosen, static_cast<int>(marks->size()))) {
		return false;
	}
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/public/fpdf_ppo.h
	return rewritePages(
	    revision, canceled,
	    [&](FPDF_DOCUMENT document) {
		    return FPDF_ImportPagesByIndex(
		               document, from.get(), chosen.data(),
		               static_cast<unsigned long>(chosen.size()), index) != 0;
	    },
	    [&](std::vector<PageMark> before)
	        -> std::optional<std::vector<PageMark>> {
		    std::vector<PageMark> block;
		    for (const int page : chosen) {
			    block.push_back((*marks)[static_cast<std::size_t>(page)]);
		    }
		    before.insert(before.begin() + index, block.begin(), block.end());
		    return before;
	    });
}

std::optional<std::vector<std::uint8_t>>
Document::extractPages(const std::vector<int>& pages,
                       const std::function<bool()>& canceled) const {
	const std::function<bool()> stopped = [&canceled] {
		return canceled && canceled();
	};
	// copying pages out needs the copy or assemble permission, but not the
	// signature guard: the open file and document stay as they are.
	if (offThread() || !validPages(pages, pageCount()) ||
	    (!canCopy() && (FPDF_GetDocPermissions(m_handle) & 0x400) == 0) ||
	    stopped()) {
		return {};
	}
	const auto marks = markPages(m_handle, stopped);
	DocumentPtr copy(FPDF_CreateNewDocument());
	if (!marks || !copy ||
	    !FPDF_ImportPagesByIndex(copy.get(), m_handle, pages.data(),
	                             static_cast<unsigned long>(pages.size()), 0) ||
	    stopped()) {
		return {};
	}
	BoundedWriter writer(EDIT_LIMIT, stopped);
	if (!FPDF_SaveAsCopy(copy.get(), &writer, 0) || writer.failed) {
		return {};
	}
	copy.reset();
	// the new PDF has to reopen with the chosen pages in the chosen order.
	std::vector<PageMark> expected;
	for (const int page : pages) {
		expected.push_back((*marks)[static_cast<std::size_t>(page)]);
	}
	const auto check = loadBytes(writer.bytes);
	const auto saved = check ? markPages(check.get(), stopped) : std::nullopt;
	if (!saved || *saved != expected) {
		return {};
	}
	return std::move(writer.bytes);
}

CombineResult combineFiles(const std::vector<std::filesystem::path>& files,
                           std::size_t limit,
                           const std::function<bool()>& canceled) {
	CombineResult result;
	const std::function<bool()> stopped = [&canceled] {
		return canceled && canceled();
	};
	const auto fail = [&result](CombineError error, std::size_t file = 0) {
		result.bytes.clear();
		result.error = error;
		result.file = file;
		return result;
	};
	if (offThread() || files.empty()) {
		return fail(CombineError::Failed);
	}
	// inputs already past the limit are refused before any of them loads.
	std::uintmax_t total = 0;
	for (std::size_t file = 0; file < files.size(); ++file) {
		std::error_code error;
		const auto size = std::filesystem::file_size(files[file], error);
		if (error) {
			return fail(CombineError::Unreadable, file);
		}
		total += size;
		if (total > limit) {
			return fail(CombineError::TooLarge, file);
		}
	}
	DocumentPtr combined(FPDF_CreateNewDocument());
	if (!combined) {
		return fail(CombineError::Failed);
	}
	// PDFium reads each source while it imports, so they all stay open
	// until the combined copy is written.
	std::vector<std::vector<std::uint8_t>> inputs;
	std::vector<DocumentPtr> sources;
	std::vector<PageMark> expected;
	inputs.reserve(files.size());
	for (std::size_t file = 0; file < files.size(); ++file) {
		if (stopped()) {
			return fail(CombineError::Canceled);
		}
		std::ifstream stream(files[file], std::ios::binary);
		std::vector<std::uint8_t> bytes(
		    (std::istreambuf_iterator<char>(stream)), {});
		if (!stream && !stream.eof()) {
			return fail(CombineError::Unreadable, file);
		}
		inputs.push_back(std::move(bytes));
		auto source = loadBytes(inputs.back());
		if (!source) {
			return fail(FPDF_GetLastError() == FPDF_ERR_PASSWORD
			                ? CombineError::Password
			                : CombineError::Unreadable,
			            file);
		}
		const auto marks = markPages(source.get(), stopped);
		if (!marks) {
			return fail(stopped() ? CombineError::Canceled
			                      : CombineError::Unreadable,
			            file);
		}
		if (!FPDF_ImportPagesByIndex(combined.get(), source.get(), nullptr, 0,
		                             FPDF_GetPageCount(combined.get()))) {
			return fail(CombineError::Failed, file);
		}
		expected.insert(expected.end(), marks->begin(), marks->end());
		sources.push_back(std::move(source));
	}
	BoundedWriter writer(limit, stopped);
	const bool saved = FPDF_SaveAsCopy(combined.get(), &writer, 0);
	if (writer.overflow) {
		return fail(CombineError::TooLarge);
	}
	if (!saved || writer.failed) {
		return fail(stopped() ? CombineError::Canceled : CombineError::Failed);
	}
	combined.reset();
	sources.clear();
	const auto check = loadBytes(writer.bytes);
	const auto pages = check ? markPages(check.get(), stopped) : std::nullopt;
	if (!pages || *pages != expected) {
		return fail(CombineError::Failed);
	}
	result.bytes = std::move(writer.bytes);
	return result;
}
} // namespace pdf
