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
} // namespace

bool drawableTextBoxCharacter(char16_t c) {
	const auto in = [c](char16_t low, char16_t high) {
		return c >= low && c <= high;
	};
	return in(0x20, 0x7e) || in(0xa0, 0x24f) || in(0x370, 0x52f) ||
	       in(0x1e00, 0x1fff) || in(0x2010, 0x2027) || in(0x2030, 0x205e) ||
	       in(0x2070, 0x22ff) || in(0x2460, 0x25ff) || in(0x3000, 0x312f) ||
	       in(0x3131, 0x318e) || in(0x3400, 0x4dbf) || in(0x4e00, 0x9fff) ||
	       in(0xac00, 0xd7a3) || in(0xf900, 0xfaff) || in(0xff01, 0xff60) ||
	       in(0xffe0, 0xffe6);
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
		winrt::com_ptr<IDWriteFactory2> factory;
		winrt::check_hresult(DWriteCreateFactory(
		    DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
		    reinterpret_cast<IUnknown**>(factory.put())));
		winrt::com_ptr<IDWriteFontFallback> fallback;
		winrt::check_hresult(factory->GetSystemFontFallback(fallback.put()));
		wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
		if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) {
			wcscpy_s(locale, L"en-US");
		}
		const auto source = winrt::make_self<Source>(
		    std::wstring(text.begin(), text.end()), locale);
		UINT32 position = 0;
		while (position < source->size()) {
			UINT32 length = 0;
			float scale = 1;
			winrt::com_ptr<IDWriteFont> font;
			winrt::check_hresult(fallback->MapCharacters(
			    source.get(), position, source->size() - position, nullptr,
			    L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL,
			    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &length,
			    font.put(), &scale));
			if (!length) {
				run.error = GlyphError::Font;
				return run;
			}
			winrt::com_ptr<IDWriteFontFace> face;
			if (font) {
				winrt::check_hresult(font->CreateFontFace(face.put()));
				if (!embeddable(*face)) {
					run.error = GlyphError::Embedding;
					return run;
				}
			}
			DWRITE_FONT_METRICS metrics{};
			if (face) {
				face->GetMetrics(&metrics);
			}
			const float size = pointSize * scale;
			for (UINT32 i = position; i < position + length; ++i) {
				const UINT32 point = text[i];
				if (point == u'\n') {
					continue;
				}
				UINT16 index = 0;
				DWRITE_GLYPH_METRICS glyph{};
				if (!face || !metrics.designUnitsPerEm ||
				    FAILED(face->GetGlyphIndices(&point, 1, &index)) ||
				    !index ||
				    FAILED(face->GetDesignGlyphMetrics(&index, 1, &glyph))) {
					run.error = GlyphError::Font;
					return run;
				}
				const auto sink = winrt::make_self<Sink>();
				winrt::check_hresult(
				    face->GetGlyphRunOutline(size, &index, nullptr, nullptr, 1,
				                             FALSE, FALSE, sink.get()));
				run.glyphs[i].advance = static_cast<double>(size) *
				                        glyph.advanceWidth /
				                        metrics.designUnitsPerEm;
				run.glyphs[i].path = std::move(sink->path);
			}
			position += length;
		}
	} catch (...) {
		run.error = GlyphError::Font;
		run.glyphs.clear();
	}
	return run;
}
} // namespace pdf
