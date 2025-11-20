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

#include <functional>
#include <span>

#include "document.h"

namespace pdf {
struct PrintRange {
	int first = 1, last = 1; // inclusive, 1-based dialog page numbers
};
// Empty ranges mean all pages. Invalid ranges reject the whole selection.
std::optional<std::vector<int>> printPages(int count,
                                           std::span<const PrintRange> ranges);
struct PrintRect {
	int x = 0, y = 0, width = 0, height = 0;
};
struct PrintRasterRequest {
	int page = 0, width = 0, height = 0;
	PrintRect tile;
};
// Fit within the printable area, preserving physical aspect at either DPI.
std::optional<PrintRect> fitPrintPage(PageSize page, int width, int height,
                                      int dpiX, int dpiY);
struct PrintJob {
	std::shared_ptr<void> device; // owns the Windows printer HDC
	std::vector<int>
	    pages; // 0-based, each page once; the driver handles copies
	std::wstring title;
};
enum class PrintResult { Completed, Canceled, Failed };
struct PrintDialogResult {
	std::optional<PrintJob> job;
	bool failed = false;
};
// UI thread: the native dialog owns modality; cancel/Apply create no job.
PrintDialogResult choosePrinter(void* owner, int count, int current,
                                const std::wstring& title);
// Engine thread: all GDI/PDFium work; a failure or close aborts the job.
PrintResult printDocument(const Document& document, const PrintJob& job,
                          const std::function<bool()>& canceled);
} // namespace pdf
