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
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
void ViewerPage::beginNoteEdit() {
	const auto selected = CommentsList().SelectedIndex();
	const auto& rows = m_commentScan.rows();
	if (m_closed || !m_session || !m_noteAllowed || m_noteBusy ||
	    m_noteSaving || m_noteDialog || m_notePlacing || !commentsOpen() ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
	    Rail().Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
	    (m_notesAvailable && !m_notesAvailable()) || selected < 0 ||
	    static_cast<std::size_t>(selected) >= rows.size() ||
	    rows[selected].subtype != "Text") {
		return;
	}
	const auto page = rows[selected].page, index = rows[selected].index;
	m_noteBusy = true;
	try {
		const auto session = m_session.get();
		if (session->textNote(
		        page, index,
		        [weak = get_weak(), queue = DispatcherQueue(),
		         session](pdf::TextNoteResult result) {
			        queue.TryEnqueue([weak, session,
			                          result = std::move(result)] {
				        const auto self = weak.get();
				        if (!self || self->m_closed ||
				            self->m_session.get() != session) {
					        return;
				        }
				        self->m_noteBusy = false;
				        self->showComments();
				        if (result.contents && !result.canceled) {
					        self->editExistingNote(result.page, result.index,
					                               std::move(*result.contents));
				        } else {
					        self->showNotice(L"Couldn't edit this note. It may "
					                         L"be protected, "
					                         L"use rich text, or belong to a "
					                         L"PDF whose edits "
					                         L"cannot be saved safely.");
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
	showNotice(L"Couldn't queue the note editor. Try again.");
}

fire_and_forget ViewerPage::editExistingNote(int page, int index,
                                             std::u16string expected) {
	const auto lifetime = get_strong();
	try {
		if (m_closed || !m_session || !m_noteAllowed || m_noteBusy ||
		    m_noteSaving || m_noteDialog || m_notePlacing || m_passwordDialog ||
		    !XamlRoot() || !commentsOpen() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    Rail().Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    (m_notesAvailable && !m_notesAvailable())) {
			co_return;
		}
		leavePageBox();
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(L"Edit note"));
		dialog.PrimaryButtonText(L"Apply");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(L"Changes the note contents on page " +
		              to_hstring(page + 1) + L".");
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		TextBox contents;
		contents.Header(box_value(L"Note"));
		contents.AcceptsReturn(true);
		contents.TextWrapping(TextWrapping::Wrap);
		contents.MinHeight(100);
		contents.MaxHeight(180);
		contents.MaxLength(32767);
		contents.Text(hstring(reinterpret_cast<const wchar_t*>(expected.data()),
		                      static_cast<std::uint32_t>(expected.size())));
		fields.Children().Append(contents);
		TextBlock error;
		error.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(error);
		dialog.Content(fields);
		// i keep the raw text when the control normalizes line endings.
		const auto initial = std::make_shared<hstring>(contents.Text());
		dialog.Opened([initial, weak = make_weak(contents)](auto&&, auto&&) {
			if (const auto text = weak.get()) {
				*initial = text.Text();
			}
		});
		dialog.IsPrimaryButtonEnabled(!contents.Text().empty());
		contents.TextChanged(
		    [weak = get_weak(), prompt = make_weak(dialog)](
		        const Windows::Foundation::IInspectable& sender, auto&&) {
			    if (const auto self = weak.get(); self && !self->m_noteBusy) {
				    if (const auto view = prompt.get()) {
					    view.IsPrimaryButtonEnabled(
					        !sender.as<TextBox>().Text().empty());
				    }
			    }
		    });
		dialog.Closing(
		    [weak = get_weak()](const ContentDialog& sender,
		                        const ContentDialogClosingEventArgs& args) {
			    if (const auto self = weak.get();
			        self && self->m_noteBusy && self->m_noteDialog == sender) {
				    args.Cancel(true);
			    }
		    });
		dialog.PrimaryButtonClick([weak = get_weak(),
		                           prompt = make_weak(dialog),
		                           text = make_weak(contents),
		                           failure = make_weak(error), initial, page,
		                           index, expected = std::move(expected)](
		                              auto&&,
		                              const ContentDialogButtonClickEventArgs&
		                                  args) {
			args.Cancel(true);
			const auto self = weak.get();
			const auto view = prompt.get();
			const auto field = text.get();
			const auto message = failure.get();
			if (!self || self->m_closed || self->m_noteBusy || !view ||
			    !field || !message || field.Text().empty()) {
				return;
			}
			const auto value = field.Text();
			const auto replacement =
			    value == *initial ? expected
			                      : std::u16string(value.begin(), value.end());
			self->m_noteBusy = true;
			view.IsPrimaryButtonEnabled(false);
			field.IsReadOnly(true);
			message.Text(L"Applying note changes\u2026");
			try {
				if (self->m_session->updateTextNote(
				        page, index, expected, replacement,
				        [weak, prompt, text, failure,
				         queue = self->DispatcherQueue()](auto result) {
					        queue.TryEnqueue([weak, prompt, text, failure,
					                          result] {
						        const auto owner = weak.get();
						        if (!owner || owner->m_closed) {
							        return;
						        }
						        owner->m_noteBusy = false;
						        owner->m_notesDirty =
						            owner->m_notesDirty || result.changed;
						        try {
							        if (const auto dialog = prompt.get()) {
								        if (result.index) {
									        dialog.Hide();
								        } else {
									        if (const auto field = text.get()) {
										        field.IsReadOnly(false);
										        dialog.IsPrimaryButtonEnabled(
										            !field.Text().empty());
									        }
									        if (const auto message =
									                failure.get()) {
										        message.Text(
										            L"Couldn't apply this "
										            L"edit. The note may have "
										            L"changed. Your text is "
										            L"still here.");
									        }
								        }
							        }
							        if (result.changed) {
								        owner->refreshNotes();
							        }
							        owner->showComments();
						        } catch (...) {
							        diagnostics::write(
							            diagnostics::Level::Error,
							            "Couldn't refresh the edited note.");
						        }
					        });
				        })) {
					self->showComments();
					return;
				}
			} catch (...) {
			}
			self->m_noteBusy = false;
			field.IsReadOnly(false);
			view.IsPrimaryButtonEnabled(true);
			message.Text(L"Couldn't queue this edit. Your text is still here.");
			self->showComments();
		});
		m_noteDialog = dialog;
		co_await dialog.ShowAsync();
		if (m_noteDialog == dialog) {
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showComments();
		}
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
} // namespace winrt::Sable::implementation
