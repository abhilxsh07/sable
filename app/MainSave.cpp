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
#include "pch.h"
#include "MainWindow.xaml.h"
#include "pdf/session.h"
#include "helpers/attachment_file.h"
#include "helpers/logging.h"
#include <winrt/Microsoft.Windows.Storage.Pickers.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::Sable::implementation {
void MainWindow::onSaveCopy(const Windows::Foundation::IInspectable&,
                            const RoutedEventArgs&) {
	commitMenuEdit();
	if (m_presentationViewer) {
		endPresentation();
	}
	saveCopy();
}

fire_and_forget MainWindow::saveCopy() {
	weak_ref<MainWindow> weak;
	Microsoft::UI::Dispatching::DispatcherQueue queue{nullptr};
	std::shared_ptr<std::atomic_bool> cancel;
	weak_ref<ViewerPage> document;
	struct Finish {
		std::function<void()> notify;
		~Finish() {
			try {
				if (notify) {
					notify();
				}
			} catch (...) {
			}
		}
	} finish;
	try {
		weak = get_weak();
		queue = DispatcherQueue();
		if (m_closing || m_saveCopyCancel || !Viewer()) {
			co_return;
		}
		std::filesystem::path source;
		pdf::Session* session = nullptr;
		{
			const auto current = Viewer();
			const auto viewer = get_self<ViewerPage>(current);
			if (viewer->m_closed || !viewer->m_layout || !viewer->m_session) {
				viewer->showNotice(L"This PDF isn't ready to save yet.");
				co_return;
			}
			document = viewer->get_weak();
			source = viewer->m_path;
			session = viewer->m_session.get();
			m_saveCopyViewer = current;
		}
		cancel = std::make_shared<std::atomic_bool>(false);
		m_saveCopyCancel = cancel;
		SaveCopyItem().IsEnabled(false);
		finish.notify = [weak, queue, cancel] {
			queue.TryEnqueue([weak, cancel] {
				if (const auto self = weak.get();
				    self && !self->m_closing &&
				    self->m_saveCopyCancel == cancel) {
					self->m_saveCopyCancel.reset();
					self->m_saveCopyViewer = {};
					self->SaveCopyItem().IsEnabled(self->Viewer() != nullptr);
				}
			});
		};
		const apartment_context ui;
		const auto name = to_hstring(helpers::attachmentFilename(
		    to_string(source.stem().wstring()) + " copy.pdf"));
		hstring path;
#ifdef SABLE_SURFACE_TEST
		if (m_copyPicker) {
			path = co_await m_copyPicker(name);
		} else
#endif
		{
			Microsoft::Windows::Storage::Pickers::FileSavePicker picker(
			    AppWindow().Id());
			picker.SuggestedFileName(name);
			picker.FileTypeChoices().Insert(
			    L"PDF document", single_threaded_vector<hstring>({L".pdf"}));
			picker.DefaultFileExtension(L".pdf");
			const auto selected = co_await picker.PickSaveFileAsync();
			if (selected) {
				path = selected.Path();
			}
		}
		auto self = weak.get();
		auto viewer = document.get();
		if (!self || self->m_closing || !viewer || viewer->m_closed ||
		    cancel->load() || path.empty() ||
		    self->Viewer() != viewer->get_strong().as<Sable::ViewerPage>() ||
		    viewer->m_session.get() != session) {
			co_return;
		}
		struct Job {
			handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
			pdf::SaveResult result = pdf::SaveResult::Failed;
		};
		const auto job = std::make_shared<Job>();
		check_bool(static_cast<bool>(job->ready));
		check_bool(session->saveCopy(
		    path.c_str(), source, [cancel] { return cancel->load(); },
		    [job](pdf::SaveResult result) {
			    job->result = result;
			    SetEvent(job->ready.get());
		    }));
		self = nullptr;
		viewer = nullptr;
		co_await resume_on_signal(job->ready.get());
		co_await ui;
		self = weak.get();
		viewer = document.get();
		if (!self || self->m_closing || !viewer || viewer->m_closed ||
		    cancel->load() || self->m_saveCopyCancel != cancel) {
			co_return;
		}
		if (job->result == pdf::SaveResult::Source) {
			viewer->showNotice(L"Choose a different name or folder to keep the "
			                   L"original PDF.");
		} else if (job->result == pdf::SaveResult::Failed) {
			viewer->showNotice(
			    L"Couldn't save a copy. Choose a writable folder "
			    L"and try again.");
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't save a PDF copy.");
		try {
			if (queue) {
				queue.TryEnqueue([weak, document, cancel] {
					const auto self = weak.get();
					const auto viewer = document.get();
					if (self && !self->m_closing && viewer &&
					    !viewer->m_closed && (!cancel || !cancel->load())) {
						viewer->showNotice(
						    L"Couldn't save a copy. Try again or "
						    L"choose another folder.");
					}
				});
			}
		} catch (...) {
		}
	}
}
} // namespace winrt::Sable::implementation
