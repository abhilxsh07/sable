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
bool ViewerPage::canMarkSelection() const {
	return hasTextSelection() && !annotationRefusal() && !notePlacing() &&
	       Visibility() == Microsoft::UI::Xaml::Visibility::Visible &&
	       (!m_notesAvailable || m_notesAvailable());
}

void ViewerPage::markSelection(pdf::TextMarkup markup,
                               pdf::HighlightColor color) {
	if (!canMarkSelection()) {
		const auto reason = annotationRefusal();
		showNotice(reason ? reason : L"Select some text before marking it.");
		return;
	}
	cancelSelectionDrag();
	const auto selection = *m_selection;
	const auto version = m_selectionVersion;
	m_noteBusy = true;
	try {
		if (m_session->markText(
		        selection, markup,
		        [weak = get_weak(), queue = DispatcherQueue(),
		         version](pdf::MarkupResult result) {
			        queue.TryEnqueue([weak, version, result] {
				        const auto self = weak.get();
				        if (!self || self->m_closed) {
					        return;
				        }
				        self->m_noteBusy = false;
				        self->m_notesDirty =
				            self->m_notesDirty || result.changed;
				        try {
					        if (result.count > 0 &&
					            self->m_selectionVersion == version) {
						        self->clearTextSelection();
					        }
					        if (result.changed) {
						        self->refreshNotes();
					        }
					        if (result.count == 0) {
						        self->showNotice(writeNotice(
						            result.refusal,
						            L"Couldn't mark this selection. The PDF "
						            L"may "
						            L"restrict comments or contain "
						            L"attachments, or "
						            L"the "
						            L"selection may be empty or too large. "
						            L"Your "
						            L"selection is still available."));
					        }
					        self->showComments();
				        } catch (...) {
					        diagnostics::caught(
					            diagnostics::Level::Error,
					            "Couldn't refresh the marked PDF.");
				        }
			        });
		        },
		        color)) {
			showComments();
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue text markup");
	}
	m_noteBusy = false;
	showComments();
	showNotice(
	    L"Couldn't queue the text markup. Your selection is still available.");
}
} // namespace winrt::Sable::implementation
