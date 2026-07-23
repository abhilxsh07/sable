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
#include "../pdf/annotations.h"
#include "layout.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace helpers {
inline constexpr std::size_t signatureNameLimit = 64;
// one line of 1-64 printable characters that isn't only spaces.
bool validSignatureName(std::u16string_view name);
// the first installed of Segoe Script, Ink Free and Segoe UI (italic).
struct SignatureFont {
	std::wstring family;
	bool italic = false;
};
SignatureFont signatureFont();
// the name in black on transparent pixels, trimmed to its ink.
std::optional<pdf::StampImage> typedSignature(std::u16string_view name);

using SignatureBox = std::pair<pdf::AnnotationPoint, pdf::AnnotationPoint>;
// a 36 pt tall box, at most 216 pt wide, kept on the displayed page around
// the click. aspect is the image's width over its height.
std::optional<SignatureBox> typedSignatureBox(double aspect, PageSize displayed,
                                              pdf::AnnotationPoint center);

// pad strokes in the pad's own units.
using PadStroke = std::vector<pdf::AnnotationPoint>;
// fits the drawing in 150 by 50 points around the click, keeping its shape
// and the 2 pt line on the displayed page.
std::vector<pdf::DisplayedStroke>
drawnSignatureStrokes(const std::vector<PadStroke>& pad, PageSize displayed,
                      pdf::AnnotationPoint center);
} // namespace helpers
