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

#include <fpdf_annot.h>
#include <fpdf_edit.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace pdf {
struct MultilineBoxAppearance {
	std::u16string stream;
	int paintedLines = 0;
};

// i use standard font outlines so SetAP needs no stream font resources.
// see:
// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_edittext.cpp
// see:
// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp
inline std::optional<MultilineBoxAppearance>
multilineBoxAppearance(FPDF_DOCUMENT document, const FS_RECTF& rect,
                       std::u16string_view contents) {
	if (contents.empty() || contents.size() > 128 ||
	    contents.find(u'\n') == std::u16string_view::npos ||
	    std::any_of(contents.begin(), contents.end(), [](char16_t c) {
		    return c != u'\n' && (c < 32 || c > 126);
	    })) {
		return {};
	}
	for (float edge : {rect.left, rect.bottom, rect.right, rect.top}) {
		if (!std::isfinite(edge) || std::abs(edge) > 1000000.f) {
			return {};
		}
	}
	const int lines = 1 + static_cast<int>(std::count(contents.begin(),
	                                                  contents.end(), u'\n'));
	if (lines > 8 || rect.right - rect.left <= 4 ||
	    rect.top - rect.bottom < 17 + 15 * (lines - 1)) {
		return {};
	}
	using Font = std::remove_pointer_t<FPDF_FONT>;
	const std::unique_ptr<Font, decltype(&FPDFFont_Close)> font(
	    FPDFText_LoadStandardFont(document, "Helvetica"), FPDFFont_Close);
	if (!font) {
		return {};
	}
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::fixed << std::setprecision(5);
	const double width = static_cast<double>(rect.right) - rect.left;
	const double height = static_cast<double>(rect.top) - rect.bottom;
	out << "0 g " << rect.left + .5 << ' ' << rect.bottom + .5 << ' '
	    << width - 1 << ' ' << height - 1 << " re " << rect.left + 1.5 << ' '
	    << rect.bottom + 1.5 << ' ' << width - 3 << ' ' << height - 3
	    << " re f*\n";
	constexpr float pointSize = 12;
	const double start = rect.left + 2.;
	double x = start, y = rect.top - 13.;
	int paintedLines = 0;
	bool painted = false;
	for (char16_t c : contents) {
		if (c == u'\n') {
			if (painted) {
				out << "f\n";
				++paintedLines;
			}
			painted = false;
			x = start;
			y -= 15;
			continue;
		}
		float advance = 0;
		if (!FPDFFont_GetGlyphWidth(font.get(), c, pointSize, &advance) ||
		    !std::isfinite(advance) || advance <= 0 ||
		    x + advance > rect.right - 1.) {
			return {};
		}
		if (c != u' ') {
			const auto path = FPDFFont_GetGlyphPath(font.get(), c, pointSize);
			const int count = FPDFGlyphPath_CountGlyphSegments(path);
			if (count < 2 || count > 256) {
				return {};
			}
			int curve = 0;
			for (int i = 0; i < count; ++i) {
				const auto segment = FPDFGlyphPath_GetGlyphPathSegment(path, i);
				float px = 0, py = 0;
				if (!FPDFPathSegment_GetPoint(segment, &px, &py)) {
					return {};
				}
				const double gx = x + pointSize * px, gy = y + pointSize * py;
				if (!std::isfinite(gx) || !std::isfinite(gy) ||
				    gx < rect.left + 1. || gx > rect.right - 1. ||
				    gy < rect.bottom + 1. || gy > rect.top - 1.) {
					return {};
				}
				out << gx << ' ' << gy << ' ';
				const int type = FPDFPathSegment_GetType(segment);
				if (type == FPDF_SEGMENT_BEZIERTO) {
					if (++curve == 3) {
						out << "c\n";
						curve = 0;
					}
				} else if (curve == 0 && type == FPDF_SEGMENT_MOVETO) {
					out << "m\n";
				} else if (curve == 0 && type == FPDF_SEGMENT_LINETO) {
					out << "l\n";
				} else {
					return {};
				}
				if (FPDFPathSegment_GetClose(segment)) {
					if (curve != 0) {
						return {};
					}
					out << "h\n";
				}
			}
			if (curve != 0) {
				return {};
			}
			painted = true;
		}
		x += advance;
	}
	if (painted) {
		out << "f\n";
		++paintedLines;
	}
	const auto bytes = out.str();
	if (!paintedLines || bytes.size() >= 256 * 1024) {
		return {};
	}
	return MultilineBoxAppearance{std::u16string(bytes.begin(), bytes.end()),
	                              paintedLines};
}

inline bool
matchesMultilineBoxAppearance(FPDF_ANNOTATION note, const FS_RECTF& rect,
                              const MultilineBoxAppearance& expected) {
	const auto size = FPDFAnnot_GetAP(note, 0, nullptr, 0);
	if (size != (expected.stream.size() + 1) * sizeof(FPDF_WCHAR)) {
		return false;
	}
	std::vector<FPDF_WCHAR> stream(size / sizeof(FPDF_WCHAR));
	if (FPDFAnnot_GetAP(note, 0, stream.data(), size) != size ||
	    stream.back() ||
	    !std::equal(expected.stream.begin(), expected.stream.end(),
	                stream.begin()) ||
	    FPDFAnnot_GetObjectCount(note) != expected.paintedLines + 1) {
		return false;
	}
	for (int i = 0; i <= expected.paintedLines; ++i) {
		const auto object = FPDFAnnot_GetObject(note, i);
		float left = 0, bottom = 0, right = 0, top = 0;
		if (FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_PATH ||
		    !FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top) ||
		    !std::isfinite(left) || !std::isfinite(bottom) ||
		    !std::isfinite(right) || !std::isfinite(top) || left < rect.left ||
		    bottom < rect.bottom || right > rect.right || top > rect.top) {
			return false;
		}
	}
	return true;
}
} // namespace pdf
