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

#include "pdf/annotations.h"

namespace winrt::Sable::implementation {
// pen colors that read on white paper. red and yellow are the approved
// highlight values; the pale highlight green, blue and pink fade as lines.
// Drawing style and the drawing editor both offer these.
struct InkColor {
	const wchar_t* label;
	pdf::AnnotationColor color;
};
inline constexpr InkColor INK_COLORS[]{{L"Black", {0, 0, 0, 255}},
                                       {L"Blue", {37, 99, 235, 255}},
                                       {L"Red", {235, 73, 73, 255}},
                                       {L"Green", {31, 157, 85, 255}},
                                       {L"Yellow", {255, 240, 102, 255}}};
struct InkWidth {
	const wchar_t* label;
	float width;
};
inline constexpr InkWidth INK_WIDTHS[]{
    {L"Thin (1 pt)", 1}, {L"Medium (2 pt)", 2}, {L"Thick (4 pt)", 4}};
} // namespace winrt::Sable::implementation
