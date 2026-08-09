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

#include <cstdint>
#include <string_view>
#include <vector>

namespace pdf {
// One glyph outline in points, from the pen position on the baseline, y up.
// A cubic curve is three Curve points in a row.
struct GlyphSegment {
	enum class Kind : std::uint8_t { Move, Line, Curve, Close };
	Kind kind = Kind::Move;
	double x = 0, y = 0;
};
struct Glyph {
	double advance = 0;
	std::vector<GlyphSegment> path;
};
enum class GlyphError {
	None,
	Script,    // a character Sable can't draw one at a time
	Font,      // no installed font has a glyph for it
	Embedding, // the font's licence doesn't allow embedding
};
struct GlyphRun {
	GlyphError error = GlyphError::None;
	// one per UTF-16 unit of the text; line breaks get an empty glyph.
	std::vector<Glyph> glyphs;
};

// Characters a text box can draw without shaping: Latin, Greek, Cyrillic,
// common punctuation and symbols, CJK and Hangul, all inside the BMP. No
// combining marks, right-to-left or complex scripts, emoji or controls.
bool drawableTextBoxCharacter(char16_t c);

// Outlines from the installed Windows fonts, Segoe UI first, then the
// system fallback for each run. Nothing is bundled or embedded: the
// outlines go into the appearance as paths, and a font whose OS/2 fsType
// restricts embedding refuses. Not a PDFium call, so any thread can ask.
GlyphRun systemGlyphs(std::u16string_view text, float pointSize);
} // namespace pdf
