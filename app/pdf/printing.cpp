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
#include "printing.h"

#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

#include "engine.h"

namespace pdf {
std::optional<std::vector<int>> printPages(int count,
                                           std::span<const PrintRange> ranges) {
	if (count <= 0) {
		return std::nullopt;
	}
	std::vector<int> pages;
	if (ranges.empty()) {
		pages.resize(count);
		std::iota(pages.begin(), pages.end(), 0);
		return pages;
	}
	std::vector<bool> selected(count);
	for (const auto& range : ranges) {
		if (range.first < 1 || range.last < range.first || range.last > count) {
			return std::nullopt;
		}
		for (int page = range.first - 1; page < range.last; ++page) {
			selected[page] = true;
		}
	}
	for (int page = 0; page < count; ++page) {
		if (selected[page]) {
			pages.push_back(page);
		}
	}
	return pages;
}

std::optional<PrintRect> fitPrintPage(PageSize page, int width, int height,
                                      int dpiX, int dpiY) {
	if (!std::isfinite(page.width) || !std::isfinite(page.height) ||
	    page.width <= 0 || page.height <= 0 || width <= 0 || height <= 0 ||
	    dpiX <= 0 || dpiY <= 0) {
		return std::nullopt;
	}
	const double across = static_cast<double>(page.width) * dpiX;
	const double down = static_cast<double>(page.height) * dpiY;
	const double scale = (std::min)(width / across, height / down);
	const int w =
	    std::clamp(static_cast<int>(std::lround(across * scale)), 1, width);
	const int h =
	    std::clamp(static_cast<int>(std::lround(down * scale)), 1, height);
	return PrintRect{(width - w) / 2, (height - h) / 2, w, h};
}

PrintDialogResult choosePrinter(void* owner, int count, int current,
                                const std::wstring& title) {
	if (!owner || count <= 0 || current < 0 || current >= count) {
		return {{}, true};
	}
	struct Dialog {
		PRINTDLGEXW value{sizeof(PRINTDLGEXW)};
		~Dialog() {
			if (value.hDC) {
				DeleteDC(value.hDC);
			}
			if (value.hDevMode) {
				GlobalFree(value.hDevMode);
			}
			if (value.hDevNames) {
				GlobalFree(value.hDevNames);
			}
		}
	} dialog;
	std::array<PRINTPAGERANGE, 32> ranges{};
	auto& value = dialog.value;
	value.hwndOwner = static_cast<HWND>(owner);
	// The returned DC carries copies/collation. Never multiply the page loop.
	// Microsoft: PRINTDLGEXW, PD_USEDEVMODECOPIESANDCOLLATE / PD_RETURNDC.
	value.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOSELECTION |
	              PD_HIDEPRINTTOFILE;
	value.nMinPage = 1;
	value.nMaxPage = static_cast<DWORD>(count);
	value.nCopies = 1;
	value.lpPageRanges = ranges.data();
	value.nMaxPageRanges = static_cast<DWORD>(ranges.size());
	value.nStartPage = START_PAGE_GENERAL;
	if (FAILED(PrintDlgExW(&value))) {
		return {{}, true};
	}
	if (value.dwResultAction != PD_RESULT_PRINT) {
		return {};
	}
	std::vector<PrintRange> selection;
	if (value.Flags & PD_CURRENTPAGE) {
		selection.push_back({current + 1, current + 1});
	} else if (value.Flags & PD_PAGENUMS) {
		if (value.nPageRanges == 0 || value.nPageRanges > ranges.size()) {
			return {{}, true};
		}
		for (DWORD i = 0; i < value.nPageRanges; ++i) {
			if (ranges[i].nFromPage > static_cast<DWORD>(count) ||
			    ranges[i].nToPage > static_cast<DWORD>(count)) {
				return {{}, true};
			}
			selection.push_back({static_cast<int>(ranges[i].nFromPage),
			                     static_cast<int>(ranges[i].nToPage)});
		}
	}
	auto pages = printPages(count, selection);
	if (!value.hDC || !pages || pages->empty()) {
		return {{}, true};
	}
	PrintJob job;
	job.pages = std::move(*pages);
	job.title = title;
	job.device =
	    std::shared_ptr<void>(std::exchange(value.hDC, nullptr),
	                          [](void* dc) { DeleteDC(static_cast<HDC>(dc)); });
	return {std::move(job), false};
}

PrintResult printDocument(const Document& document, const PrintJob& job,
                          const std::function<bool()>& canceled) {
	if (!Engine::onThread() || !job.device || job.pages.empty() || !canceled) {
		return PrintResult::Failed;
	}
	if (canceled()) {
		return PrintResult::Canceled;
	}
	const int count = document.pageCount();
	if (std::any_of(job.pages.begin(), job.pages.end(),
	                [count](int page) { return page < 0 || page >= count; })) {
		return PrintResult::Failed;
	}
	const auto dc = static_cast<HDC>(job.device.get());
	const int width = GetDeviceCaps(dc, HORZRES),
	          height = GetDeviceCaps(dc, VERTRES);
	const int dpiX = GetDeviceCaps(dc, LOGPIXELSX),
	          dpiY = GetDeviceCaps(dc, LOGPIXELSY);
	if (width <= 0 || height <= 0 || dpiX <= 0 || dpiY <= 0) {
		return PrintResult::Failed;
	}
	DOCINFOW info{sizeof(DOCINFOW)};
	info.lpszDocName = job.title.c_str();
	if (StartDocW(dc, &info) <= 0) {
		return canceled() || GetLastError() == ERROR_CANCELLED
		           ? PrintResult::Canceled
		           : PrintResult::Failed;
	}
	struct Abort {
		HDC dc;
		bool active = true;
		~Abort() {
			if (active) {
				AbortDoc(dc);
			}
		}
	} abort{dc};
	for (const int page : job.pages) {
		if (canceled()) {
			return PrintResult::Canceled;
		}
		const auto size = document.pageSize(page);
		const auto rect = size ? fitPrintPage(*size, width, height, dpiX, dpiY)
		                       : std::nullopt;
		if (!rect || StartPage(dc) <= 0 ||
		    !document.printPage(page, dc, rect->x, rect->y, rect->width,
		                        rect->height)) {
			return PrintResult::Failed;
		}
		if (canceled()) {
			return PrintResult::Canceled;
		}
		if (EndPage(dc) <= 0) {
			return PrintResult::Failed;
		}
	}
	if (canceled()) {
		return PrintResult::Canceled;
	}
	if (EndDoc(dc) <= 0) {
		return PrintResult::Failed;
	}
	abort.active = false;
	return PrintResult::Completed;
}
} // namespace pdf
