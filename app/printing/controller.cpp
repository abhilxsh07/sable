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
#include "controller.h"
#include <PrintManagerInterop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include "../helpers/logging.h"

using namespace winrt;
using namespace Windows::Graphics::Printing;
namespace printing {
namespace {
bool unavailable(HRESULT result) {
	return result == REGDB_E_CLASSNOTREG || result == E_NOINTERFACE ||
	       result == E_NOTIMPL ||
	       result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}
} // namespace
std::shared_ptr<Controller> Controller::create(
    void* owner, pdf::PrintRenderer renderer, std::vector<pdf::PageSize> pages,
    int current, std::wstring title,
    Microsoft::UI::Dispatching::DispatcherQueue queue, Done done) {
	auto self = std::make_shared<Controller>();
	self->m_owner = owner;
	self->m_title = std::move(title);
	self->m_queue = std::move(queue);
	self->m_done = std::move(done);
	self->m_source = make_self<Source>(
	    std::move(renderer), std::move(pages), current,
	    [weak = self->weak_from_this(), dispatch = self->m_queue] {
		    dispatch.TryEnqueue([weak] {
			    if (const auto owner = weak.lock();
			        owner && owner->m_completion) {
				    owner->finish(*owner->m_completion);
			    }
		    });
	    });
	return self;
}
void Controller::open() {
	show(shared_from_this());
}
fire_and_forget Controller::show(std::shared_ptr<Controller> self) {
	try {
		if (!PrintManager::IsSupported()) {
			self->finish(std::nullopt);
			co_return;
		}
		// see:
		// https://learn.microsoft.com/en-us/windows/win32/api/printmanagerinterop/nn-printmanagerinterop-iprintmanagerinterop
		auto interop =
		    get_activation_factory<PrintManager, IPrintManagerInterop>();
		check_hresult(interop->GetForWindow(static_cast<HWND>(self->m_owner),
		                                    guid_of<PrintManager>(),
		                                    put_abi(self->m_manager)));
		self->m_requested = self->m_manager.PrintTaskRequested(
		    auto_revoke, [weak = self->weak_from_this()](
		                     auto&&, const PrintTaskRequestedEventArgs& args) {
			    try {
				    const auto request = args.Request();
				    const auto deferral = request.GetDeferral();
				    if (const auto owner = weak.lock()) {
					    if (owner->m_queue.TryEnqueue(
					            [weak, request, deferral] {
						            try {
							            if (const auto current = weak.lock();
							                current && !current->m_finished) {
								            current->requested(request);
							            }
						            } catch (...) {
							            if (const auto current = weak.lock()) {
								            current->finish(
								                pdf::PrintResult::Failed);
							            }
						            }
						            try {
							            deferral.Complete();
						            } catch (...) {
						            }
					            })) {
						    return;
					    }
				    }
				    deferral.Complete();
			    } catch (...) {
				    diagnostics::write(diagnostics::Level::Warning,
				                       "Couldn't create modern print task");
			    }
		    });
		Windows::Foundation::IAsyncOperation<bool> operation{nullptr};
		check_hresult(interop->ShowPrintUIForWindowAsync(
		    static_cast<HWND>(self->m_owner), guid_of<decltype(operation)>(),
		    put_abi(operation)));
		diagnostics::write(diagnostics::Level::Info,
		                   "Printing through modern Windows UI");
		if (!(co_await operation)) {
			self->finish(pdf::PrintResult::Canceled);
		}
	} catch (const hresult_error& error) {
		diagnostics::write(diagnostics::Level::Warning,
		                   "Couldn't show modern print UI: " +
		                       to_string(error.message()));
		self->finish(!self->m_created && unavailable(error.code())
		                 ? std::optional<pdf::PrintResult>{}
		                 : pdf::PrintResult::Failed);
	} catch (...) {
		self->finish(pdf::PrintResult::Failed);
	}
}
void Controller::requested(const PrintTaskRequest& request) {
	const auto source = m_source;
	auto task = request.CreatePrintTask(
	    m_title, [source](const PrintTaskSourceRequestedArgs& args) {
		    args.SetSource(source.as<IPrintDocumentSource>());
	    });
	m_created = true;
	const auto options = task.Options();
	const auto displayed = options.DisplayedOptions();
	displayed.Clear();
	for (const auto& option : {StandardPrintTaskOptions::Orientation(),
	                           StandardPrintTaskOptions::MediaSize(),
	                           StandardPrintTaskOptions::Copies(),
	                           StandardPrintTaskOptions::Collation(),
	                           StandardPrintTaskOptions::CustomPageRanges()}) {
		displayed.Append(option);
	}
	const auto range = options.PageRangeOptions();
	range.AllowAllPages(true);
	range.AllowCustomSetOfPages(true);
	range.AllowCurrentPage(false);
	const auto details =
	    OptionDetails::PrintTaskOptionDetails::GetFromPrintTaskOptions(options);
	// see:
	// https://learn.microsoft.com/en-us/uwp/api/windows.graphics.printing.optiondetails.printtaskoptiondetails.optionchanged
	m_optionsChanged =
	    details.OptionChanged(auto_revoke, [source](auto&&, auto&&) {
		    try {
			    source->invalidate();
		    } catch (...) {
			    diagnostics::write(diagnostics::Level::Warning,
			                       "Couldn't invalidate print preview");
		    }
	    });
	m_completed = task.Completed(
	    auto_revoke, [weak = weak_from_this(), queue = m_queue](
	                     auto&&, const PrintTaskCompletedEventArgs& args) {
		    const auto completion = args.Completion();
		    queue.TryEnqueue([weak, completion] {
			    if (const auto self = weak.lock()) {
				    self->finish(completion == PrintTaskCompletion::Submitted
				                     ? pdf::PrintResult::Completed
				                 : completion == PrintTaskCompletion::Failed
				                     ? pdf::PrintResult::Failed
				                     : pdf::PrintResult::Canceled);
			    }
		    });
	    });
}
void Controller::finish(std::optional<pdf::PrintResult> result) {
	if (m_finished) {
		return;
	}
	// Submitted can precede the asynchronous package writer finishing.
	if (result == pdf::PrintResult::Completed && !m_source->jobComplete()) {
		m_completion = result;
		return;
	}
	m_finished = true;
	if (m_source->failed()) {
		result = pdf::PrintResult::Failed;
	}
	m_source->cancel(result != pdf::PrintResult::Completed);
	m_requested.revoke();
	m_completed.revoke();
	m_optionsChanged.revoke();
	m_manager = nullptr;
	const auto done = std::move(m_done);
	if (done) {
		done(result);
	}
}
void Controller::cancel() {
	const auto self = shared_from_this();
	self->finish(pdf::PrintResult::Canceled);
}
} // namespace printing
