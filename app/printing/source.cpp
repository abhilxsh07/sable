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
#include "options.h"
#include "../helpers/logging.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <future>

using namespace winrt;
using namespace Windows::Graphics::Printing;
namespace printing {
Source::Source(pdf::PrintRenderer renderer, std::vector<pdf::PageSize> pages,
               int current, std::function<void()> completed)
    : m_completed(std::move(completed)), m_renderer(std::move(renderer)),
      m_pages(std::move(pages)), m_current(current) {}
HRESULT __stdcall Source::GetPreviewPageCollection(
    IPrintDocumentPackageTarget* target,
    IPrintPreviewPageCollection** collection) noexcept {
	if (!target || !collection) {
		return E_POINTER;
	}
	*collection = nullptr;
	try {
		com_ptr<IPrintPreviewDxgiPackageTarget> preview;
		check_hresult(target->GetPackageTarget(
		    ID_PREVIEWPACKAGETARGET_DXGI,
		    __uuidof(IPrintPreviewDxgiPackageTarget), preview.put_void()));
		{
			std::lock_guard lock(m_mutex);
			if (m_closed) {
				return HRESULT_FROM_WIN32(ERROR_CANCELLED);
			}
			m_preview = std::move(preview);
			m_previewPackage.copy_from(target);
			++m_generation;
		}
		return QueryInterface(__uuidof(IPrintPreviewPageCollection),
		                      reinterpret_cast<void**>(collection));
	} catch (...) {
		return to_hresult();
	}
}
HRESULT __stdcall Source::Paginate(UINT32, ::IInspectable* value) noexcept {
	try {
		if (!value) {
			return E_POINTER;
		}
		Windows::Foundation::IInspectable supplied;
		copy_from_abi(supplied, value);
		const auto options = supplied.as<PrintTaskOptions>();
		// Preview uses document page numbers; the shell validates ranges
		// against this count. The final job applies the selected ranges exactly
		// once.
		auto all = pdf::printPages(static_cast<int>(m_pages.size()), {});
		if (!all) {
			return E_INVALIDARG;
		}
		auto selected = std::move(*all);
		com_ptr<IPrintPreviewDxgiPackageTarget> target;
		const auto count = static_cast<UINT32>(selected.size());
		{
			std::lock_guard lock(m_mutex);
			if (m_closed) {
				return HRESULT_FROM_WIN32(ERROR_CANCELLED);
			}
			m_selected = std::move(selected);
			m_options = options;
			target = m_preview;
			++m_generation;
		}
		return target ? target->SetJobPageCount(FinalPageCount, count)
		              : E_UNEXPECTED;
	} catch (...) {
		return to_hresult();
	}
}
void Source::invalidate() {
	com_ptr<IPrintPreviewDxgiPackageTarget> target;
	{
		std::lock_guard lock(m_mutex);
		++m_generation;
		target = m_preview;
	}
	if (target && !m_closed) {
		check_hresult(target->InvalidatePreview());
	}
}
void Source::cancel(bool abort) noexcept {
	if (m_closed.exchange(true)) {
		return;
	}
	++m_generation;
	com_ptr<IPrintDocumentPackageTarget> preview, job;
	{
		std::lock_guard lock(m_mutex);
		preview = std::move(m_previewPackage);
		job = std::move(m_jobPackage);
		m_preview = nullptr;
		m_options = nullptr;
	}
	if (abort) {
		if (preview) {
			preview->Cancel();
		}
		if (job) {
			job->Cancel();
		}
	}
}
void Source::fail() noexcept {
	m_failed = true;
	diagnostics::write(diagnostics::Level::Warning,
	                   "Modern print rendering failed");
	cancel();
}
void Source::jobFinished() noexcept {
	{
		std::lock_guard lock(m_mutex);
		m_jobPackage = nullptr;
	}
	m_jobComplete = true;
	try {
		if (m_completed) {
			m_completed();
		}
	} catch (...) {
	}
}
std::optional<pdf::Bitmap>
Source::raster(pdf::PrintRasterRequest request,
               const std::function<bool()>& canceled) {
	if (canceled()) {
		return {};
	}
	auto promise = std::make_shared<std::promise<std::optional<pdf::Bitmap>>>();
	auto ready = promise->get_future();
	if (!m_renderer(request, canceled, [promise](auto result) {
		    promise->set_value(std::move(result));
	    })) {
		return {};
	}
	// Only workers wait here. A closed window doesn't wait for an engine tile.
	while (ready.wait_for(std::chrono::milliseconds(50)) !=
	       std::future_status::ready) {
		if (canceled()) {
			return {};
		}
	}
	return canceled() ? std::nullopt : ready.get();
}
} // namespace printing
