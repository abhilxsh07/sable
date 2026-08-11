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
	// the later units of a shaped cluster: the cluster's first unit carries
	// its outline and advance, so these draw nothing.
	bool continuation = false;
};
// a glyph as written: its id in its font and where it sits, in points
// from the start of its line on the baseline, y up.
struct PlacedGlyph {
	std::uint16_t id = 0;
	double x = 0, y = 0;
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
	// every glyph in the order it's drawn, line by line.
	std::vector<PlacedGlyph> placed;
};

// Characters a text box can draw: Latin, Greek, Cyrillic, common
// punctuation and symbols, CJK and Hangul one at a time, plus Devanagari
// and Bengali (with ZWJ and ZWNJ), which are shaped. All inside the BMP.
// No other combining marks, right-to-left or other complex scripts, emoji
// or controls.
bool drawableTextBoxCharacter(char16_t c);
// Devanagari, Bengali, ZWJ and ZWNJ: the characters that go through
// DirectWrite's shaping before they become outlines.
bool shapedTextBoxCharacter(char16_t c);

// Outlines from the installed Windows fonts, Segoe UI first, then the
// system fallback for each run. Text with Devanagari or Bengali is shaped
// a line at a time, so conjuncts, matras and reph come out joined; text
// without them is drawn exactly as before. Nothing is bundled or embedded: the
// outlines go into the appearance as paths, and a font whose OS/2 fsType
// restricts embedding refuses. Not a PDFium call, so any thread can ask.
GlyphRun systemGlyphs(std::u16string_view text, float pointSize);
} // namespace pdf
