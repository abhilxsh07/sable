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
using namespace winrt;
using namespace Windows::Graphics::Printing;
namespace printing {
HRESULT __stdcall Source::MakeDocument(
    ::IInspectable* value, IPrintDocumentPackageTarget* target) noexcept {
	try {
		if (!value || !target) {
			return E_POINTER;
		}
		if (m_closed) {
			return HRESULT_FROM_WIN32(ERROR_CANCELLED);
		}
		if (m_printing.exchange(true)) {
			return E_UNEXPECTED;
		}
		++m_generation;
		Windows::Foundation::IInspectable supplied;
		copy_from_abi(supplied, value);
		auto options = supplied.as<PrintTaskOptions>();
		com_ptr<IPrintDocumentPackageTarget> package;
		package.copy_from(target);
		{
			std::lock_guard lock(m_mutex);
			if (m_closed) {
				return HRESULT_FROM_WIN32(ERROR_CANCELLED);
			}
			m_jobPackage = package;
		}
		job(get_strong(), std::move(options), std::move(package));
		return S_OK;
	} catch (...) {
		return to_hresult();
	}
}
fire_and_forget Source::job(com_ptr<Source> self, PrintTaskOptions options,
                            com_ptr<IPrintDocumentPackageTarget> target) {
	struct Finish {
		std::function<void()> callback;
		~Finish() {
			callback();
		}
	} finished{[self] { self->jobFinished(); }};
	try {
		co_await resume_background();
		Apartment apartment;
		const auto canceled = [self] { return self->m_closed.load(); };
		if (canceled()) {
			co_return;
		}
		const auto pages =
		    selectedPages(options, static_cast<int>(self->m_pages.size()));
		Graphics graphics;
		const auto first = options.GetPageDescription(1);
		auto control = graphics.control(
		    target.get(),
		    static_cast<float>((std::max)(first.DpiX, first.DpiY)));
		// The system's job target already carries copies and paper options.
		// see:
		// https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/D2DPrintingFromDesktopApps/cpp/D2DPrintingFromDesktopApps.cpp
		UINT32 jobPage = 0;
		for (const int page : pages) {
			if (canceled()) {
				co_return;
			}
			const auto description = options.GetPageDescription(++jobPage);
			const auto paperSize = paper(description);
			const auto layout =
			    pdf::layoutPrint(self->m_pages[page], paperSize,
			                     description.DpiX, description.DpiY);
			if (!layout) {
				throw hresult_invalid_argument();
			}
			graphics.beginPage();
			for (int y = 0; y < layout->rasterHeight; y += 2048) {
				for (int x = 0; x < layout->rasterWidth; x += 2048) {
					pdf::PrintRect tile{
					    x, y, (std::min)(2048, layout->rasterWidth - x),
					    (std::min)(2048, layout->rasterHeight - y)};
					auto raster = self->raster(
					    {page, layout->rasterWidth, layout->rasterHeight, tile},
					    canceled);
					if (canceled()) {
						co_return;
					}
					if (!raster) {
						throw hresult_error(E_FAIL);
					}
					graphics.tile(*raster, *layout, tile);
				}
			}
			auto commands = graphics.endPage();
			check_hresult(control->AddPage(
			    commands.get(), D2D1::SizeF(paperSize.width, paperSize.height),
			    nullptr));
		}
		if (!canceled()) {
			check_hresult(control->Close());
		}
	} catch (...) {
		if (!self->m_closed) {
			self->fail();
		}
	}
}
} // namespace printing
