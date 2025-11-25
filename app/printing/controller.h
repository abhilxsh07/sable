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
#include "source.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Graphics.Printing.OptionDetails.h>
#include <functional>
#include <memory>

namespace printing {
class Controller : public std::enable_shared_from_this<Controller> {
  public:
	// No result means the modern UI is unavailable; the reader may fall back.
	using Done = std::function<void(std::optional<pdf::PrintResult>)>;
	static std::shared_ptr<Controller>
	create(void* owner, pdf::PrintRenderer renderer,
	       std::vector<pdf::PageSize> pages, int current, std::wstring title,
	       winrt::Microsoft::UI::Dispatching::DispatcherQueue queue, Done done);
	void open();
	void cancel();

  private:
#ifdef SABLE_SURFACE_TEST
	friend struct winrt::Sable::implementation::TabsTest;
#endif
	static winrt::fire_and_forget show(std::shared_ptr<Controller> self);
	void requested(
	    const winrt::Windows::Graphics::Printing::PrintTaskRequest& request);
	void finish(std::optional<pdf::PrintResult> result);
	void* m_owner = nullptr;
	std::wstring m_title;
	Done m_done;
	bool m_finished = false, m_created = false;
	std::optional<pdf::PrintResult> m_completion;
	winrt::Microsoft::UI::Dispatching::DispatcherQueue m_queue{nullptr};
	winrt::com_ptr<Source> m_source;
	winrt::Windows::Graphics::Printing::PrintManager m_manager{nullptr};
	winrt::Windows::Graphics::Printing::PrintManager::PrintTaskRequested_revoker
	    m_requested;
	winrt::Windows::Graphics::Printing::PrintTask::Completed_revoker
	    m_completed;
	winrt::Windows::Graphics::Printing::OptionDetails::PrintTaskOptionDetails::
	    OptionChanged_revoker m_optionsChanged;
};
} // namespace printing
