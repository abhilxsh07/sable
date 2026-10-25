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
#include "engine.h"
#include <fpdfview.h>
#include <memory>

namespace pdf {
std::optional<Bitmap> Document::printRaster(int index, int pageWidth,
                                            int pageHeight, int x, int y,
                                            int width, int height) const {
	if (!Engine::onThread() || index < 0 || index >= pageCount() ||
	    pageWidth <= 0 || pageHeight <= 0 || pageWidth > 100'000 ||
	    pageHeight > 100'000 || x < 0 || y < 0 || width <= 0 || height <= 0 ||
	    width > 4096 || height > 4096 || x > pageWidth - width ||
	    y > pageHeight - height) {
		return {};
	}
	const auto closePage = [](FPDF_PAGE page) { FPDF_ClosePage(page); };
	std::unique_ptr<fpdf_page_t__, decltype(closePage)> page(
	    FPDF_LoadPage(m_handle, index), closePage);
	if (!page) {
		return {};
	}
	Bitmap result{width, height, width * 4, {}};
	result.pixels.resize(static_cast<std::size_t>(result.stride) * height);
	const auto destroy = [](FPDF_BITMAP bitmap) { FPDFBitmap_Destroy(bitmap); };
	std::unique_ptr<fpdf_bitmap_t__, decltype(destroy)> bitmap(
	    FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGRA,
	                        result.pixels.data(), result.stride),
	    destroy);
	if (!bitmap) {
		return {};
	}
	FPDFBitmap_FillRect(bitmap.get(), 0, 0, width, height, 0xffffffff);
	// same print flags as the printer HDC, including print-only annotations
	FPDF_RenderPageBitmap(bitmap.get(), page.get(), -x, -y, pageWidth,
	                      pageHeight, 0, FPDF_ANNOT | FPDF_PRINTING);
	return result;
}
} // namespace pdf
