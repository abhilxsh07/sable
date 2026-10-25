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
#include "print_layout.h"
#include <algorithm>
#include <cmath>

namespace pdf {
std::optional<PrintLayout> layoutPrint(PageSize page, PrintPaper paper,
                                       double dpiX, double dpiY) {
	for (const double value :
	     {page.width, page.height, paper.width, paper.height, paper.imageWidth,
	      paper.imageHeight}) {
		if (!std::isfinite(value) || value <= 0) {
			return {};
		}
	}
	if (!std::isfinite(paper.x) || !std::isfinite(paper.y) ||
	    !std::isfinite(dpiX) || !std::isfinite(dpiY) || dpiX <= 0 ||
	    dpiY <= 0) {
		return {};
	}
	const float left = (std::max)(0.f, paper.x);
	const float top = (std::max)(0.f, paper.y);
	const float width =
	    (std::min)(paper.width, paper.x + paper.imageWidth) - left;
	const float height =
	    (std::min)(paper.height, paper.y + paper.imageHeight) - top;
	if (width <= 0 || height <= 0) {
		return {};
	}
	const float scale = (std::min)(width / page.width, height / page.height);
	PrintLayout result;
	result.width = page.width * scale;
	result.height = page.height * scale;
	result.x = left + (width - result.width) / 2;
	result.y = top + (height - result.height) / 2;
	const double across = std::ceil(result.width * dpiX / 96.);
	const double down = std::ceil(result.height * dpiY / 96.);
	if (!std::isfinite(across) || !std::isfinite(down) || across > 100'000 ||
	    down > 100'000) {
		return {};
	}
	result.rasterWidth = (std::max)(1, static_cast<int>(across));
	result.rasterHeight = (std::max)(1, static_cast<int>(down));
	return result;
}
} // namespace pdf
