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
#include "ViewerPage.xaml.h"
#include "pdf/session.h"
#include "helpers/logging.h"

using namespace winrt;
namespace winrt::Sable::implementation {
bool ViewerPage::canHighlightSelection() const {
	return hasTextSelection() && m_session && m_noteAllowed && !m_noteBusy &&
	       !m_noteSaving && !m_noteDialog && !m_notePlacing &&
	       Visibility() == Microsoft::UI::Xaml::Visibility::Visible &&
	       (!m_notesAvailable || m_notesAvailable());
}

void ViewerPage::highlightSelection() {
	if (!canHighlightSelection()) {
		return;
	}
	cancelSelectionDrag();
	const auto selection = *m_selection;
	const auto version = m_selectionVersion;
	m_noteBusy = true;
	try {
		if (m_session->highlightText(selection, [weak = get_weak(),
		                                         queue = DispatcherQueue(),
		                                         version](pdf::HighlightResult
		                                                      result) {
			    queue.TryEnqueue([weak, version, result] {
				    const auto self = weak.get();
				    if (!self || self->m_closed) {
					    return;
				    }
				    self->m_noteBusy = false;
				    self->m_notesDirty = self->m_notesDirty || result.changed;
				    try {
					    if (result.count > 0 &&
					        self->m_selectionVersion == version) {
						    self->clearTextSelection();
					    }
					    if (result.changed) {
						    self->refreshNotes();
					    }
					    if (result.count == 0) {
						    self->showNotice(
						        L"Couldn't highlight this selection. The PDF "
						        L"may "
						        L"restrict comments or contain attachments, or "
						        L"the "
						        L"selection may be empty or too large. Your "
						        L"selection is still available.");
					    }
					    self->showComments();
				    } catch (...) {
					    diagnostics::write(
					        diagnostics::Level::Error,
					        "Couldn't refresh the highlighted PDF.");
				    }
			    });
		    })) {
			showComments();
			return;
		}
	} catch (...) {
	}
	m_noteBusy = false;
	showComments();
	showNotice(
	    L"Couldn't queue the highlight. Your selection is still available.");
}
} // namespace winrt::Sable::implementation
