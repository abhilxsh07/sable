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

#include "document.h"
#include "text_box_glyphs.h"

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

// i draw a box as glyph outlines when it has line breaks or anything past
// ASCII. PDFium's own generator only covers one line of ASCII Helvetica.
inline bool needsOutlineAppearance(std::u16string_view contents) {
	return std::any_of(contents.begin(), contents.end(),
	                   [](char16_t c) { return c == u'\n' || c > 126; });
}

// i use font outlines so SetAP needs no stream font resources: Helvetica's
// for ASCII and the installed Windows fonts' for anything else, so nothing
// is embedded either way.
// see:
// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_edittext.cpp
// see:
// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp
inline std::optional<MultilineBoxAppearance>
multilineBoxAppearance(FPDF_DOCUMENT document, const FS_RECTF& rect,
                       std::u16string_view contents) {
	if (!needsOutlineAppearance(contents) ||
	    validateTextBoxText(contents, TextBoxLayout::ExplicitLines) !=
	        TextBoxTextError::None) {
		return {};
	}
	for (float edge : {rect.left, rect.bottom, rect.right, rect.top}) {
		if (!std::isfinite(edge) || std::abs(edge) > 1000000.f) {
			return {};
		}
	}
	const int lines = 1 + static_cast<int>(std::count(contents.begin(),
	                                                  contents.end(), u'\n'));
	// Nirmala UI's vowel signs and reph reach past 12 pt above the baseline
	// and its lower matras below it, so shaped text gets more room per line.
	const bool shaped =
	    std::any_of(contents.begin(), contents.end(), shapedTextBoxCharacter);
	const double firstBaseline = shaped ? 15 : 13, pitch = shaped ? 18 : 15;
	if (rect.right - rect.left <= 4 ||
	    rect.top - rect.bottom < firstBaseline + 4 + pitch * (lines - 1)) {
		return {};
	}
	constexpr float pointSize = 12;
	const bool ascii = std::all_of(contents.begin(), contents.end(),
	                               [](char16_t c) { return c < 128; });
	using Font = std::remove_pointer_t<FPDF_FONT>;
	const std::unique_ptr<Font, decltype(&FPDFFont_Close)> font(
	    ascii ? FPDFText_LoadStandardFont(document, "Helvetica") : nullptr,
	    FPDFFont_Close);
	const auto system = ascii ? GlyphRun{} : systemGlyphs(contents, pointSize);
	if (ascii ? !font
	          : system.error != GlyphError::None ||
	                system.glyphs.size() != contents.size()) {
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
	const double start = rect.left + 2.;
	double x = start, y = rect.top - firstBaseline;
	int paintedLines = 0;
	bool painted = false;
	for (std::size_t at = 0; at < contents.size(); ++at) {
		const char16_t c = contents[at];
		if (c == u'\n') {
			if (painted) {
				out << "f\n";
				++paintedLines;
			}
			painted = false;
			x = start;
			y -= pitch;
			continue;
		}
		double advance = 0;
		std::vector<GlyphSegment> outline;
		if (ascii) {
			float glyphWidth = 0;
			if (!FPDFFont_GetGlyphWidth(font.get(), c, pointSize,
			                            &glyphWidth)) {
				return {};
			}
			advance = glyphWidth;
			if (c != u' ') {
				const auto path =
				    FPDFFont_GetGlyphPath(font.get(), c, pointSize);
				const int count = FPDFGlyphPath_CountGlyphSegments(path);
				if (count < 2 || count > 256) {
					return {};
				}
				for (int i = 0; i < count; ++i) {
					const auto segment =
					    FPDFGlyphPath_GetGlyphPathSegment(path, i);
					float px = 0, py = 0;
					if (!FPDFPathSegment_GetPoint(segment, &px, &py)) {
						return {};
					}
					const int type = FPDFPathSegment_GetType(segment);
					outline.push_back(
					    {type == FPDF_SEGMENT_BEZIERTO
					         ? GlyphSegment::Kind::Curve
					     : type == FPDF_SEGMENT_MOVETO
					         ? GlyphSegment::Kind::Move
					     : type == FPDF_SEGMENT_LINETO
					         ? GlyphSegment::Kind::Line
					         : GlyphSegment::Kind::Close,
					     // float products, as the first boxes were written
					     static_cast<double>(pointSize * px),
					     static_cast<double>(pointSize * py)});
					if (outline.back().kind == GlyphSegment::Kind::Close) {
						return {};
					}
					if (FPDFPathSegment_GetClose(segment)) {
						outline.push_back({GlyphSegment::Kind::Close, 0, 0});
					}
				}
			}
		} else {
			// the rest of a shaped cluster: its first character drew it
			if (system.glyphs[at].continuation) {
				continue;
			}
			advance = system.glyphs[at].advance;
			outline = system.glyphs[at].path;
			if (outline.size() > 4096) {
				return {};
			}
		}
		if (!std::isfinite(advance) || advance <= 0 ||
		    x + advance > rect.right - 1.) {
			return {};
		}
		int curve = 0;
		for (const auto& segment : outline) {
			if (segment.kind == GlyphSegment::Kind::Close) {
				if (curve != 0) {
					return {};
				}
				out << "h\n";
				continue;
			}
			const double gx = x + segment.x, gy = y + segment.y;
			if (!std::isfinite(gx) || !std::isfinite(gy) ||
			    gx < rect.left + 1. || gx > rect.right - 1. ||
			    gy < rect.bottom + 1. || gy > rect.top - 1.) {
				return {};
			}
			out << gx << ' ' << gy << ' ';
			if (segment.kind == GlyphSegment::Kind::Curve) {
				if (++curve == 3) {
					out << "c\n";
					curve = 0;
				}
			} else if (curve == 0 && segment.kind == GlyphSegment::Kind::Move) {
				out << "m\n";
			} else if (curve == 0 && segment.kind == GlyphSegment::Kind::Line) {
				out << "l\n";
			} else {
				return {};
			}
		}
		if (curve != 0) {
			return {};
		}
		painted = painted || !outline.empty();
		x += advance;
	}
	if (painted) {
		out << "f\n";
		++paintedLines;
	}
	const auto bytes = out.str();
	if (!paintedLines || bytes.size() >= (ascii ? 256 : 1024) * 1024) {
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
