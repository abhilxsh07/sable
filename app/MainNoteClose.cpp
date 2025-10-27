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
IAsyncOperation<bool> MainWindow::confirmNotes(Sable::ViewerPage document) {
	const auto lifetime = get_strong();
	const auto viewer = get_self<ViewerPage>(document);
	if (viewer->m_noteBusy || viewer->m_noteSaving || viewer->m_noteDialog ||
	    viewer->m_notePlacing ||
	    (viewer->m_notesDirty && viewer->m_passwordDialog)) {
		viewer->showNotice(L"Finish the open dialog or note operation first.");
		co_return false;
	}
	if (!viewer->m_notesDirty) {
		co_return true;
	}
	ContentDialog dialog;
	dialog.XamlRoot(RootGrid().XamlRoot());
	dialog.Title(box_value(L"Save your notes?"));
	dialog.Content(box_value(
	    L"Your notes in " + hstring(viewer->m_path.filename().wstring()) +
	    L" haven't been saved. Save them to a separate PDF before closing."));
	dialog.PrimaryButtonText(L"Save notes");
	dialog.SecondaryButtonText(L"Discard");
	dialog.CloseButtonText(L"Cancel");
	dialog.DefaultButton(ContentDialogButton::Close);
	viewer->m_noteDialog = dialog;
	ContentDialogResult choice = ContentDialogResult::None;
	try {
#ifdef SABLE_SURFACE_TEST
		if (m_notesChoice) {
			choice = co_await m_notesChoice();
		} else
#endif
		{
			choice = co_await dialog.ShowAsync();
		}
	} catch (...) {
		viewer->m_noteDialog = nullptr;
		throw;
	}
	viewer->m_noteDialog = nullptr;
	if (m_closing || viewer->m_closed) {
		co_return false;
	}
	if (choice == ContentDialogResult::Primary) {
		co_return co_await saveNotes(document);
	}
	co_return choice == ContentDialogResult::Secondary;
}

fire_and_forget MainWindow::closeTab(TabViewItem tab) {
	const auto lifetime = get_strong();
	if (m_closing || m_closePending || m_notesSaving) {
		co_return;
	}
	m_closePending = true;
	try {
		const auto found = std::find_if(
		    m_tabs.begin(), m_tabs.end(),
		    [&tab](const auto& entry) { return entry.tab == tab; });
		if (found != m_tabs.end()) {
			const auto document = found->viewer;
			Tabs().SelectedItem(tab);
			if (co_await confirmNotes(document)) {
				removeTab(tab);
			}
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't finish closing the PDF tab.");
	}
	m_closePending = false;
}

fire_and_forget MainWindow::closeWithNotes() {
	const auto lifetime = get_strong();
	if (m_closing || m_closePending || m_notesSaving) {
		co_return;
	}
	m_closePending = true;
	try {
		// opening and tab removal are held while this snapshot is reviewed.
		const auto tabs = m_tabs;
		for (const auto& entry : tabs) {
			const auto pdf = get_self<ViewerPage>(entry.viewer);
			if (pdf->m_notesDirty || pdf->m_noteBusy || pdf->m_noteDialog ||
			    pdf->m_notePlacing) {
				Tabs().SelectedItem(entry.tab);
				if (!co_await confirmNotes(entry.viewer)) {
					m_closePending = false;
					co_return;
				}
			}
		}
		m_allowNoteClose = true;
		Close();
	} catch (...) {
		m_allowNoteClose = false;
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't finish closing the reader window.");
	}
	m_closePending = false;
}
} // namespace winrt::Sable::implementation
