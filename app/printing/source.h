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
#include <DocumentSource.h>
#include <PrintPreview.h>
#include <winrt/Windows.Graphics.Printing.h>
#include <atomic>
#include <mutex>
#include "../pdf/session.h"
#include "../pdf/print_layout.h"

#ifdef SABLE_SURFACE_TEST
namespace winrt::Sable::implementation {
struct TabsTest;
}
#endif

namespace printing {
struct Source
    : winrt::implements<
          Source, winrt::Windows::Graphics::Printing::IPrintDocumentSource,
          IPrintDocumentPageSource, IPrintPreviewPageCollection> {
	Source(pdf::PrintRenderer renderer, std::vector<pdf::PageSize> pages,
	       int current, std::function<void()> completed = {});
	HRESULT __stdcall GetPreviewPageCollection(
	    IPrintDocumentPackageTarget* target,
	    IPrintPreviewPageCollection** collection) noexcept final;
	HRESULT __stdcall Paginate(UINT32 current,
	                           ::IInspectable* options) noexcept final;
	HRESULT __stdcall MakePage(UINT32 page, float width,
	                           float height) noexcept final;
	HRESULT __stdcall MakeDocument(
	    ::IInspectable* options,
	    IPrintDocumentPackageTarget* target) noexcept final;
	void invalidate();
	void cancel(bool abort = true) noexcept;
	bool failed() const {
		return m_failed.load();
	}
	bool jobComplete() const {
		return m_jobComplete.load();
	}

  private:
#ifdef SABLE_SURFACE_TEST
	friend struct winrt::Sable::implementation::TabsTest;
	std::atomic<unsigned> m_previewFrames{0};
#endif
	struct Preview {
		UINT32 jobPage = 1;
		std::uint64_t generation = 0;
		int page = 0, width = 0, height = 0;
		pdf::PrintPaper paper;
		pdf::PrintLayout layout;
		winrt::com_ptr<IPrintPreviewDxgiPackageTarget> target;
	};
	static winrt::fire_and_forget preview(winrt::com_ptr<Source> self,
	                                      Preview request);
	static winrt::fire_and_forget
	job(winrt::com_ptr<Source> self,
	    winrt::Windows::Graphics::Printing::PrintTaskOptions options,
	    winrt::com_ptr<IPrintDocumentPackageTarget> target);
	std::optional<pdf::Bitmap> raster(pdf::PrintRasterRequest request,
	                                  const std::function<bool()>& canceled);
	void fail() noexcept;
	void jobFinished() noexcept;
	std::function<void()> m_completed;
	pdf::PrintRenderer m_renderer;
	std::vector<pdf::PageSize> m_pages;
	int m_current;
	std::mutex m_mutex;
	std::atomic<bool> m_closed{false}, m_failed{false}, m_printing{false};
	std::atomic<bool> m_jobComplete{false};
	std::atomic<std::uint64_t> m_generation{0};
	std::vector<int> m_selected;
	winrt::Windows::Graphics::Printing::PrintTaskOptions m_options{nullptr};
	winrt::com_ptr<IPrintPreviewDxgiPackageTarget> m_preview;
	winrt::com_ptr<IPrintDocumentPackageTarget> m_previewPackage, m_jobPackage;
};
} // namespace printing
