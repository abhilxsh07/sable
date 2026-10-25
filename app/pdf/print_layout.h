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
#include "printing.h"

namespace pdf {
// Paper and margins are DIPs (1/96 inch), raster dimensions are pixels.
struct PrintPaper {
	float width = 0, height = 0;
	float x = 0, y = 0, imageWidth = 0, imageHeight = 0;
};
struct PrintLayout {
	float x = 0, y = 0, width = 0, height = 0;
	int rasterWidth = 0, rasterHeight = 0;
};
std::optional<PrintLayout> layoutPrint(PageSize page, PrintPaper paper,
                                       double dpiX, double dpiY);
} // namespace pdf
