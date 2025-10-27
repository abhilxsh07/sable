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
#include "pdf/page_filter.h"
#include "helpers/logging.h"
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
void ViewerPage::beginNotePlacement() {
	if (m_notePlacing) {
		cancelNotePlacement();
		return;
	}
	if (m_closed || !m_layout || !m_noteAllowed || m_noteBusy || m_noteSaving ||
	    m_noteDialog || m_linkPending || !commentsOpen() ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
	    (m_notesAvailable && !m_notesAvailable())) {
		return;
	}
	leavePageBox();
	cancelSelectionDrag();
	m_ignoreTextTap = false;
	m_notePlacing = true;
	showComments();
	Scroller().Focus(FocusState::Programmatic);
}

bool ViewerPage::cancelNotePlacement() {
	const bool canceled = std::exchange(m_notePlacing, false);
	if (canceled && !m_closed) {
		showComments();
	}
	return canceled;
}

bool ViewerPage::placeNote(Windows::Foundation::Point point) {
	if (!m_notePlacing) {
		return false;
	}
	if (m_closed || !m_layout || !commentsOpen() ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
	    (m_notesAvailable && !m_notesAvailable())) {
		cancelNotePlacement();
		return true;
	}
	if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
		return true;
	}
	const auto& pages = m_layout->pages();
	for (std::size_t page = 0; page < pages.size(); ++page) {
		const auto& rect = pages[page];
		const double x = (point.X - rect.x) / rect.width;
		const double y = (point.Y - rect.y) / rect.height;
		if (std::isfinite(x) && std::isfinite(y) && x >= 0 && x <= 1 &&
		    y >= 0 && y <= 1) {
			m_notePlacing = false;
			showComments();
			editNote(
			    NoteTarget{static_cast<int>(page), x, y, key(page).rotation});
			break;
		}
	}
	return true;
}

fire_and_forget ViewerPage::editNote(std::optional<NoteTarget> target) {
	const auto lifetime = get_strong();
	try {
		if (m_closed || !m_layout || !m_session || !m_noteAllowed ||
		    m_noteBusy || m_noteSaving || m_noteDialog || m_notePlacing ||
		    m_passwordDialog || !XamlRoot() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    (m_notesAvailable && !m_notesAvailable())) {
			co_return;
		}
		leavePageBox();
		const int page = target ? target->page : static_cast<int>(m_current);
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(L"Add note"));
		dialog.PrimaryButtonText(L"Add");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		dialog.IsPrimaryButtonEnabled(!m_noteDraft.empty());
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text((target ? L"Adds a note at the selected position on page "
		                      : L"Adds a note at the center of page ") +
		              to_hstring(page + 1) + L".");
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		TextBox author;
		author.Header(box_value(L"Author (optional)"));
		author.MaxLength(32767);
		author.Text(m_noteAuthor);
		fields.Children().Append(author);
		TextBox contents;
		contents.Header(box_value(L"Note"));
		contents.AcceptsReturn(true);
		contents.TextWrapping(TextWrapping::Wrap);
		contents.MinHeight(100);
		contents.MaxHeight(180);
		contents.MaxLength(32767);
		contents.Text(m_noteDraft);
		contents.TextChanged(
		    [weak = make_weak(dialog)](
		        const Windows::Foundation::IInspectable& sender, auto&&) {
			    if (const auto prompt = weak.get()) {
				    prompt.IsPrimaryButtonEnabled(
				        !sender.as<TextBox>().Text().empty());
			    }
		    });
		fields.Children().Append(contents);
		dialog.Content(fields);
		m_noteDialog = dialog;
		const auto choice = co_await dialog.ShowAsync();
		if (m_noteDialog == dialog) {
			m_noteDialog = nullptr;
		}
		if (m_closed) {
			co_return;
		}
		m_noteDraft = contents.Text();
		m_noteAuthor = author.Text();
		if (choice != ContentDialogResult::Primary) {
			co_return;
		}
		submitNote(page, std::u16string(m_noteDraft.begin(), m_noteDraft.end()),
		           std::u16string(m_noteAuthor.begin(), m_noteAuthor.end()),
		           target);
	} catch (...) {
		if (m_noteDialog) {
			m_noteDialog.Hide();
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showNotice(L"Couldn't open the note editor. Try again.");
		}
	}
}

void ViewerPage::submitNote(int page, std::u16string text,
                            std::u16string author,
                            std::optional<NoteTarget> target) {
	if (m_closed || !m_session || !m_noteAllowed || m_noteBusy ||
	    m_noteSaving || m_notePlacing ||
	    (m_notesAvailable && !m_notesAvailable())) {
		return;
	}
	m_noteBusy = true;
	try {
		const auto reply = [weak = get_weak(),
		                    queue = DispatcherQueue()](auto result) {
			queue.TryEnqueue([weak, result] {
				if (const auto self = weak.get(); self && !self->m_closed) {
					self->m_noteBusy = false;
					self->m_notesDirty = self->m_notesDirty || result.changed;
					try {
						if (result.changed) {
							self->refreshNotes();
						}
						if (result.index) {
							self->m_noteDraft = {};
						} else {
							self->showNotice(
							    L"Couldn't add this note. This PDF may "
							    L"restrict "
							    L"comments, contain attachments, or be "
							    L"too "
							    L"small "
							    L"for a note. Your text is still in "
							    L"Add "
							    L"note.");
						}
						self->showComments();
					} catch (...) {
						diagnostics::write(
						    diagnostics::Level::Error,
						    "Couldn't refresh the edited PDF view.");
					}
				}
			});
		};
		const bool queued =
		    target ? m_session->addTextNoteAt(page, target->x, target->y,
		                                      target->rotation, std::move(text),
		                                      std::move(author), reply)
		           : m_session->addCenteredTextNote(page, std::move(text),
		                                            std::move(author), reply);
		if (!queued) {
			m_noteBusy = false;
			showNotice(L"Couldn't queue this note. Try again.");
		}
	} catch (...) {
		m_noteBusy = false;
		showNotice(L"Couldn't queue this note. Try again.");
	}
	showComments();
}

void ViewerPage::refreshNotes() {
	// i discard old replies even when their page dimensions still match.
	++m_renderVersion;
	m_session->cancelRender();
	m_cache.clear();
	m_failed.clear();
	m_device.filter->setInput(nullptr);
	m_device.bitmaps.clear();
	m_thumbLoaded.clear();
	for (const auto& [page, button] : m_thumbs) {
		prepareThumbnail(button, page);
	}
	pauseComments();
	m_commentScan.reset(static_cast<int>(m_sizes.size()));
	CommentsList().Items().Clear();
	for (std::size_t page = 0; page < m_sizes.size(); ++page) {
		invalidate(page);
	}
	updateVisible();
}
} // namespace winrt::Sable::implementation
