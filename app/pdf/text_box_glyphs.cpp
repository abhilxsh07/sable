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
#include "text_box_glyphs.h"

#include <windows.h>
#include <unknwn.h>
#include <d2d1.h>
#include <dwrite_2.h>
#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace pdf {
namespace {
// the reader walks the text through this when it asks the fallback which
// font covers each run.
// see:
// https://learn.microsoft.com/en-us/windows/win32/api/dwrite_2/nf-dwrite_2-idwritefontfallback-mapcharacters
struct Source : winrt::implements<Source, IDWriteTextAnalysisSource> {
	std::wstring text, locale;
	Source(std::wstring value, std::wstring name)
	    : text(std::move(value)), locale(std::move(name)) {}
	UINT32 size() const {
		return static_cast<UINT32>(text.size());
	}
	HRESULT __stdcall GetTextAtPosition(UINT32 position, WCHAR const** start,
	                                    UINT32* length) noexcept override {
		*start = position < size() ? text.data() + position : nullptr;
		*length = position < size() ? size() - position : 0;
		return S_OK;
	}
	HRESULT __stdcall GetTextBeforePosition(UINT32 position,
	                                        WCHAR const** start,
	                                        UINT32* length) noexcept override {
		*start = position && position <= size() ? text.data() : nullptr;
		*length = position <= size() ? position : 0;
		return S_OK;
	}
	DWRITE_READING_DIRECTION __stdcall GetParagraphReadingDirection() noexcept
	    override {
		return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
	}
	HRESULT __stdcall GetLocaleName(UINT32 position, UINT32* length,
	                                WCHAR const** name) noexcept override {
		*length = position < size() ? size() - position : 0;
		*name = locale.c_str();
		return S_OK;
	}
	HRESULT __stdcall GetNumberSubstitution(
	    UINT32 position, UINT32* length,
	    IDWriteNumberSubstitution** substitution) noexcept override {
		*length = position < size() ? size() - position : 0;
		*substitution = nullptr;
		return S_OK;
	}
};

// collects one glyph outline. DirectWrite's y grows down, the PDF's up.
// see:
// https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritefontface-getglyphrunoutline
struct Sink : winrt::implements<Sink, IDWriteGeometrySink> {
	std::vector<GlyphSegment> path;
	void add(GlyphSegment::Kind kind, D2D1_POINT_2F point) {
		path.push_back({kind, point.x, -static_cast<double>(point.y)});
	}
	void __stdcall SetFillMode(D2D1_FILL_MODE) noexcept override {}
	void __stdcall SetSegmentFlags(D2D1_PATH_SEGMENT) noexcept override {}
	void __stdcall BeginFigure(D2D1_POINT_2F start,
	                           D2D1_FIGURE_BEGIN) noexcept override {
		add(GlyphSegment::Kind::Move, start);
	}
	void __stdcall AddLines(const D2D1_POINT_2F* points,
	                        UINT32 count) noexcept override {
		for (UINT32 i = 0; i < count; ++i) {
			add(GlyphSegment::Kind::Line, points[i]);
		}
	}
	void __stdcall AddBeziers(const D2D1_BEZIER_SEGMENT* curves,
	                          UINT32 count) noexcept override {
		for (UINT32 i = 0; i < count; ++i) {
			add(GlyphSegment::Kind::Curve, curves[i].point1);
			add(GlyphSegment::Kind::Curve, curves[i].point2);
			add(GlyphSegment::Kind::Curve, curves[i].point3);
		}
	}
	void __stdcall EndFigure(D2D1_FIGURE_END end) noexcept override {
		if (end == D2D1_FIGURE_END_CLOSED) {
			path.push_back({GlyphSegment::Kind::Close, 0, 0});
		}
	}
	HRESULT __stdcall Close() noexcept override {
		return S_OK;
	}
};

// OS/2 fsType: 0x0002 restricted licence and 0x0200 bitmap-only both rule
// out embedding outlines. A font without the table refuses too.
// see: https://learn.microsoft.com/en-us/typography/opentype/spec/os2
bool embeddable(IDWriteFontFace& face) {
	const void* data = nullptr;
	UINT32 size = 0;
	void* context = nullptr;
	BOOL exists = FALSE;
	if (FAILED(
	        face.TryGetFontTable(DWRITE_MAKE_OPENTYPE_TAG('O', 'S', '/', '2'),
	                             &data, &size, &context, &exists)) ||
	    !exists) {
		return false;
	}
	bool allowed = false;
	if (size >= 10) {
		const auto* bytes = static_cast<const std::uint8_t*>(data);
		const unsigned fsType = (bytes[8] << 8) | bytes[9];
		allowed = (fsType & 0x000f) != 0x0002 && !(fsType & 0x0200);
	}
	face.ReleaseFontTable(context);
	return allowed;
}
// collects the script runs of one stretch of text.
// see:
// https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextanalyzer-analyzescript
struct ScriptSink : winrt::implements<ScriptSink, IDWriteTextAnalysisSink> {
	struct Run {
		UINT32 position = 0, length = 0;
		DWRITE_SCRIPT_ANALYSIS analysis{};
	};
	std::vector<Run> runs;
	HRESULT __stdcall SetScriptAnalysis(
	    UINT32 position, UINT32 length,
	    DWRITE_SCRIPT_ANALYSIS const* analysis) noexcept override {
		try {
			runs.push_back({position, length, *analysis});
		} catch (...) {
			return E_OUTOFMEMORY;
		}
		return S_OK;
	}
	HRESULT __stdcall SetLineBreakpoints(
	    UINT32, UINT32, DWRITE_LINE_BREAKPOINT const*) noexcept override {
		return S_OK;
	}
	HRESULT __stdcall SetBidiLevel(UINT32, UINT32, UINT8,
	                               UINT8) noexcept override {
		return S_OK;
	}
	HRESULT __stdcall SetNumberSubstitution(
	    UINT32, UINT32, IDWriteNumberSubstitution*) noexcept override {
		return S_OK;
	}
};

struct Fonts {
	winrt::com_ptr<IDWriteFactory2> factory;
	winrt::com_ptr<IDWriteFontFallback> fallback;
	winrt::com_ptr<IDWriteTextAnalyzer> analyzer;
	std::wstring locale;
	float pointSize = 0;
};

// one fallback run: the face that covers it and how far it reaches.
struct Mapped {
	UINT32 length = 0;
	float scale = 1;
	winrt::com_ptr<IDWriteFontFace> face;
};

GlyphError map(const Fonts& fonts, Source& source, UINT32 position,
               UINT32 length, Mapped& mapped) {
	winrt::com_ptr<IDWriteFont> font;
	winrt::check_hresult(fonts.fallback->MapCharacters(
	    &source, position, length, nullptr, L"Segoe UI",
	    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
	    DWRITE_FONT_STRETCH_NORMAL, &mapped.length, font.put(), &mapped.scale));
	if (!mapped.length) {
		return GlyphError::Font;
	}
	if (font) {
		winrt::check_hresult(font->CreateFontFace(mapped.face.put()));
		if (!embeddable(*mapped.face)) {
			return GlyphError::Embedding;
		}
	}
	return GlyphError::None;
}

// one glyph per character, as the first Unicode boxes were drawn. first is
// where the source's text starts in the whole run.
GlyphError unshaped(const Fonts& fonts, Source& source, UINT32 begin,
                    UINT32 end, std::size_t first, double& pen, GlyphRun& run) {
	UINT32 position = begin;
	while (position < end) {
		Mapped mapped;
		if (const auto error =
		        map(fonts, source, position, end - position, mapped);
		    error != GlyphError::None) {
			return error;
		}
		DWRITE_FONT_METRICS metrics{};
		if (mapped.face) {
			mapped.face->GetMetrics(&metrics);
		}
		const float size = fonts.pointSize * mapped.scale;
		for (UINT32 i = position; i < position + mapped.length; ++i) {
			const UINT32 point = source.text[i];
			if (point == u'\n') {
				pen = 0;
				continue;
			}
			UINT16 index = 0;
			DWRITE_GLYPH_METRICS glyph{};
			if (!mapped.face || !metrics.designUnitsPerEm ||
			    FAILED(mapped.face->GetGlyphIndices(&point, 1, &index)) ||
			    !index ||
			    FAILED(mapped.face->GetDesignGlyphMetrics(&index, 1, &glyph))) {
				return GlyphError::Font;
			}
			const auto sink = winrt::make_self<Sink>();
			winrt::check_hresult(mapped.face->GetGlyphRunOutline(
			    size, &index, nullptr, nullptr, 1, FALSE, FALSE, sink.get()));
			auto& out = run.glyphs[first + i];
			out.advance = static_cast<double>(size) * glyph.advanceWidth /
			              metrics.designUnitsPerEm;
			out.path = std::move(sink->path);
			run.placed.push_back({index, pen, 0});
			pen += out.advance;
		}
		position += mapped.length;
	}
	return GlyphError::None;
}

// shapes one script run in one face. Each cluster's glyphs become one
// outline on its first character; the rest of the cluster draws nothing.
// see:
// https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextanalyzer-getglyphs
// see:
// https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextanalyzer-getglyphplacements
GlyphError shape(const Fonts& fonts, IDWriteFontFace& face, float size,
                 const DWRITE_SCRIPT_ANALYSIS& script, const wchar_t* text,
                 UINT32 length, std::size_t first, double& pen, GlyphRun& run) {
	std::vector<UINT16> clusters(length);
	std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> textProperties(length);
	std::vector<UINT16> ids;
	std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyphProperties;
	UINT32 capacity = length * 3 / 2 + 16, count = 0;
	for (;;) {
		ids.resize(capacity);
		glyphProperties.resize(capacity);
		const HRESULT result = fonts.analyzer->GetGlyphs(
		    text, length, &face, FALSE, FALSE, &script, fonts.locale.c_str(),
		    nullptr, nullptr, nullptr, 0, capacity, clusters.data(),
		    textProperties.data(), ids.data(), glyphProperties.data(), &count);
		if (result == E_NOT_SUFFICIENT_BUFFER && capacity < 65536) {
			capacity *= 2;
			continue;
		}
		winrt::check_hresult(result);
		break;
	}
	std::vector<FLOAT> advances(count);
	std::vector<DWRITE_GLYPH_OFFSET> offsets(count);
	winrt::check_hresult(fonts.analyzer->GetGlyphPlacements(
	    text, clusters.data(), textProperties.data(), length, ids.data(),
	    glyphProperties.data(), count, &face, size, FALSE, FALSE, &script,
	    fonts.locale.c_str(), nullptr, nullptr, 0, advances.data(),
	    offsets.data()));
	for (UINT32 g = 0; g < count; ++g) {
		if (!ids[g]) {
			return GlyphError::Font;
		}
	}
	UINT32 at = 0;
	while (at < length) {
		UINT32 next = at + 1;
		while (next < length && clusters[next] == clusters[at]) {
			++next;
		}
		const UINT32 from = clusters[at];
		const UINT32 to = next < length ? clusters[next] : count;
		// left to right only, so a cluster's glyphs follow the last one's
		if (to < from || to > count) {
			return GlyphError::Script;
		}
		double width = 0;
		for (UINT32 g = from; g < to; ++g) {
			run.placed.push_back({ids[g],
			                      pen + width + offsets[g].advanceOffset,
			                      offsets[g].ascenderOffset});
			width += advances[g];
		}
		auto& glyph = run.glyphs[first + at];
		if (to > from) {
			const auto sink = winrt::make_self<Sink>();
			winrt::check_hresult(face.GetGlyphRunOutline(
			    size, ids.data() + from, advances.data() + from,
			    offsets.data() + from, to - from, FALSE, FALSE, sink.get()));
			glyph.path = std::move(sink->path);
		}
		glyph.advance = width;
		for (UINT32 k = at + 1; k < next; ++k) {
			run.glyphs[first + k].continuation = true;
		}
		if (!(width > 0)) {
			// a joiner on its own takes no room and draws nothing
			if (!glyph.path.empty()) {
				return GlyphError::Script;
			}
			glyph.continuation = true;
		}
		pen += width;
		at = next;
	}
	return GlyphError::None;
}

GlyphError shaped(const Fonts& fonts, Source& source, UINT32 begin, UINT32 end,
                  std::size_t first, double& pen, GlyphRun& run) {
	const auto scripts = winrt::make_self<ScriptSink>();
	winrt::check_hresult(fonts.analyzer->AnalyzeScript(
	    &source, begin, end - begin, scripts.get()));
	std::sort(
	    scripts->runs.begin(), scripts->runs.end(),
	    [](const auto& a, const auto& b) { return a.position < b.position; });
	UINT32 covered = begin;
	for (const auto& script : scripts->runs) {
		if (script.position != covered ||
		    script.position + script.length > end) {
			return GlyphError::Script;
		}
		UINT32 position = script.position;
		const UINT32 stop = script.position + script.length;
		while (position < stop) {
			Mapped mapped;
			if (const auto error =
			        map(fonts, source, position, stop - position, mapped);
			    error != GlyphError::None) {
				return error;
			}
			if (!mapped.face || position + mapped.length > stop) {
				return GlyphError::Font;
			}
			if (const auto error =
			        shape(fonts, *mapped.face, fonts.pointSize * mapped.scale,
			              script.analysis, source.text.data() + position,
			              mapped.length, first + position, pen, run);
			    error != GlyphError::None) {
				return error;
			}
			position += mapped.length;
		}
		covered = stop;
	}
	return covered == end ? GlyphError::None : GlyphError::Script;
}
} // namespace

bool shapedTextBoxCharacter(char16_t c) {
	return (c >= 0x900 && c <= 0x9ff) || c == 0x200c || c == 0x200d;
}

bool drawableTextBoxCharacter(char16_t c) {
	const auto in = [c](char16_t low, char16_t high) {
		return c >= low && c <= high;
	};
	return in(0x20, 0x7e) || in(0xa0, 0x24f) || in(0x370, 0x52f) ||
	       in(0x1e00, 0x1fff) || in(0x2010, 0x2027) || in(0x2030, 0x205e) ||
	       in(0x2070, 0x22ff) || in(0x2460, 0x25ff) || in(0x3000, 0x312f) ||
	       in(0x3131, 0x318e) || in(0x3400, 0x4dbf) || in(0x4e00, 0x9fff) ||
	       in(0xac00, 0xd7a3) || in(0xf900, 0xfaff) || in(0xff01, 0xff60) ||
	       in(0xffe0, 0xffe6) || shapedTextBoxCharacter(c);
}

GlyphRun systemGlyphs(std::u16string_view text, float pointSize) {
	GlyphRun run;
	for (const auto c : text) {
		if (c != u'\n' && !drawableTextBoxCharacter(c)) {
			run.error = GlyphError::Script;
			return run;
		}
	}
	run.glyphs.resize(text.size());
	if (text.empty() || !std::isfinite(pointSize) || pointSize <= 0) {
		return run;
	}
	try {
		Fonts fonts;
		fonts.pointSize = pointSize;
		winrt::check_hresult(DWriteCreateFactory(
		    DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
		    reinterpret_cast<IUnknown**>(fonts.factory.put())));
		winrt::check_hresult(
		    fonts.factory->GetSystemFontFallback(fonts.fallback.put()));
		winrt::check_hresult(
		    fonts.factory->CreateTextAnalyzer(fonts.analyzer.put()));
		wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
		if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) {
			wcscpy_s(locale, L"en-US");
		}
		fonts.locale = locale;
		double pen = 0;
		if (std::none_of(text.begin(), text.end(), shapedTextBoxCharacter)) {
			// i keep the whole text in one source here, so the fallback
			// picks the same fonts the first Unicode boxes were drawn with.
			const auto source = winrt::make_self<Source>(
			    std::wstring(text.begin(), text.end()), fonts.locale);
			run.error =
			    unshaped(fonts, *source, 0, source->size(), 0, pen, run);
		} else {
			// shaped text goes a line at a time, split where it changes
			// between shaped and unshaped characters.
			std::size_t start = 0;
			for (;;) {
				auto stop = text.find(u'\n', start);
				if (stop == std::u16string_view::npos) {
					stop = text.size();
				}
				pen = 0;
				const auto source = winrt::make_self<Source>(
				    std::wstring(text.begin() + start, text.begin() + stop),
				    fonts.locale);
				UINT32 at = 0;
				while (at < source->size() && run.error == GlyphError::None) {
					const bool shapedRun =
					    shapedTextBoxCharacter(source->text[at]);
					UINT32 next = at + 1;
					while (next < source->size() &&
					       shapedTextBoxCharacter(source->text[next]) ==
					           shapedRun) {
						++next;
					}
					run.error = shapedRun ? shaped(fonts, *source, at, next,
					                               start, pen, run)
					                      : unshaped(fonts, *source, at, next,
					                                 start, pen, run);
					at = next;
				}
				if (run.error != GlyphError::None || stop == text.size()) {
					break;
				}
				start = stop + 1;
			}
		}
	} catch (...) {
		run.error = GlyphError::Font;
	}
	if (run.error != GlyphError::None) {
		run.glyphs.clear();
		run.placed.clear();
	}
	return run;
}
} // namespace pdf
