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
using namespace Microsoft::UI::Xaml::Controls;
using Windows::Foundation::IAsyncOperation;
namespace winrt::Sable::implementation {
IAsyncOperation<bool> MainWindow::saveNotes(Sable::ViewerPage document) {
	const auto lifetime = get_strong();
	const auto viewer = get_self<ViewerPage>(document);
	if (m_closing || m_notesSaving || viewer->m_closed || !viewer->m_session ||
	    !viewer->m_notesDirty || viewer->m_noteBusy || viewer->m_noteDialog ||
	    viewer->m_notePlacing) {
		co_return false;
	}
	m_notesSaving = true;
	viewer->m_noteSaving = true;
	const auto finish =
	    std::shared_ptr<void>(nullptr, [lifetime, document](void*) noexcept {
		    lifetime->m_notesSaving = false;
		    const auto pdf = get_self<ViewerPage>(document);
		    pdf->m_noteSaving = false;
		    try {
			    if (!pdf->m_closed) {
				    pdf->showComments();
			    }
		    } catch (...) {
		    }
	    });
	try {
		viewer->showComments();
		const auto source = viewer->m_path;
		const auto session = viewer->m_session.get();
		const auto name = to_hstring(helpers::attachmentFilename(
		    to_string(source.stem().wstring()) + " notes.pdf"));
		hstring path;
#ifdef SABLE_SURFACE_TEST
		if (m_notesPicker) {
			path = co_await m_notesPicker(name);
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
		if (m_closing || viewer->m_closed || path.empty() ||
		    viewer->m_session.get() != session) {
			co_return false;
		}
		struct Job {
			handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
			pdf::SaveResult result = pdf::SaveResult::Failed;
		};
		const auto job = std::make_shared<Job>();
		check_bool(static_cast<bool>(job->ready));
		const apartment_context ui;
		// @todo: expose an edited-file memory limit in settings.
		check_bool(session->saveEdits(path.c_str(), source,
		                              1024ull * 1024 * 1024, {},
		                              [job](auto result) {
			                              job->result = result;
			                              SetEvent(job->ready.get());
		                              }));
		co_await resume_on_signal(job->ready.get());
		co_await ui;
		if (m_closing || viewer->m_closed) {
			co_return false;
		}
		if (job->result == pdf::SaveResult::Saved) {
			// edits are locked until this save's UI continuation completes.
			viewer->m_notesDirty = false;
			viewer->showNotice(L"Saved a copy with your notes.");
			co_return true;
		}
		viewer->showNotice(
		    job->result == pdf::SaveResult::Source
		        ? L"Choose a different name or folder to keep the original PDF."
		        : L"Couldn't save your notes. Choose a writable folder and try "
		          L"again. Your notes are still open.");
	} catch (...) {
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't save the PDF notes.");
		if (!m_closing && !viewer->m_closed) {
			viewer->showNotice(L"Couldn't save your notes. Your notes are "
			                   L"still open; try another folder.");
		}
	}
	co_return false;
}

} // namespace winrt::Sable::implementation
