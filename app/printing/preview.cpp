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
#include "source.h"
#include "apartment.h"
#include "graphics.h"
#include "options.h"
#include <algorithm>
#include <cmath>

using namespace winrt;
namespace printing {
HRESULT __stdcall Source::MakePage(UINT32 page, float width,
                                   float height) noexcept {
	try {
		if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
		    height <= 0 || width > 8192 || height > 8192) {
			return E_INVALIDARG;
		}
		Preview request;
		{
			std::lock_guard lock(m_mutex);
			if (m_closed || m_printing) {
				return HRESULT_FROM_WIN32(ERROR_CANCELLED);
			}
			if (!m_options || !m_preview || m_selected.empty()) {
				return E_UNEXPECTED;
			}
			if (page == JOB_PAGE_APPLICATION_DEFINED) {
				const auto found =
				    std::find(m_selected.begin(), m_selected.end(), m_current);
				page =
				    found == m_selected.end()
				        ? 1u
				        : static_cast<UINT32>(found - m_selected.begin() + 1);
			}
			if (page == 0 || page > m_selected.size()) {
				return E_INVALIDARG;
			}
			request.jobPage = page;
			request.page = m_selected[page - 1];
			request.paper = paper(m_options.GetPageDescription(page));
			const auto scale = (std::min)(width / request.paper.width,
			                              height / request.paper.height);
			if (!std::isfinite(scale) || scale <= 0) {
				return E_INVALIDARG;
			}
			request.width = (std::max)(
			    1, static_cast<int>(std::ceil(request.paper.width * scale)));
			request.height = (std::max)(
			    1, static_cast<int>(std::ceil(request.paper.height * scale)));
			// Keep preview rasters bounded; final output uses printer-DPI
			// tiles.
			const auto dpi =
			    (std::min)(96.f * scale, 96.f * 4096.f /
			                                 (std::max)(request.paper.width,
			                                            request.paper.height));
			const auto layout = pdf::layoutPrint(m_pages[request.page],
			                                     request.paper, dpi, dpi);
			if (!layout) {
				return E_INVALIDARG;
			}
			request.layout = *layout;
			request.target = m_preview;
			request.generation = ++m_generation;
		}
		preview(get_strong(), std::move(request));
		return S_OK;
	} catch (...) {
		return to_hresult();
	}
}
fire_and_forget Source::preview(com_ptr<Source> self, Preview request) {
	try {
		co_await resume_background();
		Apartment apartment;
		const auto canceled = [self, generation = request.generation] {
			return self->m_closed || generation != self->m_generation.load();
		};
		if (canceled()) {
			co_return;
		}
		const auto& layout = request.layout;
		auto raster =
		    self->raster({request.page,
		                  layout.rasterWidth,
		                  layout.rasterHeight,
		                  {0, 0, layout.rasterWidth, layout.rasterHeight}},
		                 canceled);
		if (canceled()) {
			co_return;
		}
		if (!raster) {
			self->fail();
			co_return;
		}
		Graphics graphics;
		auto surface = graphics.preview(*raster, request.paper, layout,
		                                request.width, request.height);
		if (!canceled()) {
			check_hresult(request.target->DrawPage(
			    request.jobPage, surface.get(),
			    96.f * request.width / request.paper.width,
			    96.f * request.height / request.paper.height));
#ifdef SABLE_SURFACE_TEST
			++self->m_previewFrames;
#endif
		}
	} catch (...) {
		if (!self->m_closed &&
		    request.generation == self->m_generation.load()) {
			self->fail();
		}
	}
}
} // namespace printing
