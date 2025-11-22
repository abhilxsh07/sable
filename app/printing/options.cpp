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
#include "options.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <limits>

namespace printing {
std::vector<int> selectedPages(
    const winrt::Windows::Graphics::Printing::PrintTaskOptions& options,
    int count) {
	std::vector<pdf::PrintRange> ranges;
	// The print UI owns parsing; its custom set is already structured ranges.
	// see:
	// https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/print-from-your-app
	if (options
	        .try_as<winrt::Windows::Graphics::Printing::IPrintTaskOptions2>()) {
		for (const auto& range : options.CustomPageRanges()) {
			ranges.push_back({range.FirstPageNumber(), range.LastPageNumber()});
		}
	}
	auto pages = pdf::printPages(count, ranges);
	if (!pages || pages->empty()) {
		throw winrt::hresult_invalid_argument();
	}
	return std::move(*pages);
}
pdf::PrintPaper
paper(const winrt::Windows::Graphics::Printing::PrintPageDescription& page) {
	return {page.PageSize.Width,      page.PageSize.Height,
	        page.ImageableRect.X,     page.ImageableRect.Y,
	        page.ImageableRect.Width, page.ImageableRect.Height};
}
} // namespace printing
