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
#include <winrt/Microsoft.UI.Xaml.Automation.h>

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
	    (rows[selected].subtype != "FreeText" &&
	     rows[selected].subtype != "Text" &&
	     rows[selected].subtype != "Highlight" &&
	     rows[selected].subtype != "Underline" &&
	     rows[selected].subtype != "StrikeOut" &&
	     rows[selected].subtype != "Square" &&
	     rows[selected].subtype != "Circle" &&
	     rows[selected].subtype != "Ink" &&
	     rows[selected].subtype != "Stamp")) {
		return;
	}
	const auto page = rows[selected].page, index = rows[selected].index;
	std::optional<pdf::TextMarkup> markup;
	if (rows[selected].subtype == "Highlight") {
		markup = pdf::TextMarkup::Highlight;
	} else if (rows[selected].subtype == "Underline") {
		markup = pdf::TextMarkup::Underline;
	} else if (rows[selected].subtype == "StrikeOut") {
		markup = pdf::TextMarkup::StrikeOut;
	}
	m_noteBusy = true;
	try {
		const auto session = m_session.get();
		if (rows[selected].subtype == "Square" ||
		    rows[selected].subtype == "Circle" ||
		    rows[selected].subtype == "Ink" ||
		    rows[selected].subtype == "Stamp") {
			const auto target = rows[selected].target;
			pauseComments();
			const bool queued = session->annotations(
			    page, [weak = get_weak(), queue = DispatcherQueue(), session,
			           target](pdf::AnnotationsResult result) {
				    queue.TryEnqueue([weak, session, target,
				                      result = std::move(result)] {
					    const auto self = weak.get();
					    if (!self || self->m_closed ||
					        self->m_session.get() != session) {
						    return;
					    }
					    self->m_noteBusy = false;
					    self->showComments();
					    if (result.annotations && !result.canceled &&
					        !result.annotations->limited) {
						    for (const auto& note :
						         result.annotations->entries) {
							    if (note.target == target && note.bounds) {
								    if (note.subtype == "Ink") {
									    self->editDrawing(note);
								    } else if (note.subtype == "Stamp") {
									    self->editStampSize(note);
								    } else {
									    self->editShape(note);
								    }
								    return;
							    }
						    }
					    }
					    if (!result.canceled && self->commentsOpen()) {
						    self->showNotice(L"Couldn't read this shape. Try "
						                     L"selecting it again.");
					    }
				    });
			    });
			if (!queued) {
				m_noteBusy = false;
				showNotice(L"Couldn't queue the shape editor. Try again.");
			}
			showComments();
			return;
		}
		if (rows[selected].subtype == "FreeText") {
			const bool queued = session->textBox(
			    rows[selected].target,
			    [weak = get_weak(), queue = DispatcherQueue(),
			     session](pdf::TextBoxResult result) {
				    queue.TryEnqueue(
				        [weak, session, result = std::move(result)] {
					        const auto self = weak.get();
					        if (!self || self->m_closed ||
					            self->m_session.get() != session) {
						        return;
					        }
					        self->m_noteBusy = false;
					        self->showComments();
					        if (result.box && !result.canceled) {
						        const auto page = result.box->target.page;
						        self->editTextBox({page, 0, 0, 0},
						                          std::move(result.box));
					        } else if (!result.canceled) {
						        self->showNotice(
						            L"Couldn't edit this text box. "
						            L"It may use an unsupported style or "
						            L"belong to a protected PDF.");
					        }
				        });
			    },
			    pdf::TextBoxLayout::ExplicitLines);
			if (queued) {
				showComments();
				return;
			}
			m_noteBusy = false;
			showComments();
			showNotice(L"Couldn't queue the text box editor. Try again.");
			return;
		}

		const auto reply = [weak = get_weak(), queue = DispatcherQueue(),
		                    session, markup](pdf::TextNoteResult result) {
			queue.TryEnqueue([weak, session, markup,
			                  result = std::move(result)] {
				const auto self = weak.get();
				if (!self || self->m_closed ||
				    self->m_session.get() != session) {
					return;
				}
				self->m_noteBusy = false;
				self->showComments();
				if (result.contents && !result.canceled) {
					self->editExistingNote(
					    result.page, result.index, std::move(*result.contents),
					    markup, result.contentsPresent, result.target);
				} else {
					self->showNotice(L"Couldn't edit this note. It may "
					                 L"be protected, "
					                 L"use rich text, or belong to a "
					                 L"PDF whose edits "
					                 L"cannot be saved safely.");
				}
			});
		};
		const auto revision = rows[selected].target.revision;
		if (markup
		        ? session->markupComment(page, index, *markup, reply, revision)
		        : session->textNote(page, index, reply, revision)) {
			showComments();
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue the note editor");
	}
	m_noteBusy = false;
	showComments();
	showNotice(L"Couldn't queue the note editor. Try again.");
}

fire_and_forget ViewerPage::editShape(pdf::Annotation existing) {
	const auto lifetime = get_strong();
	try {
		if (!existing.bounds || m_closed || !m_session || !m_noteAllowed ||
		    m_noteBusy || m_noteSaving || m_noteDialog || m_notePlacing ||
		    m_passwordDialog || !XamlRoot() || !commentsOpen() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    Rail().Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    (m_notesAvailable && !m_notesAvailable())) {
			co_return;
		}
		leavePageBox();
		const auto session = m_session.get();
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(existing.subtype == "Circle"
		                           ? L"Resize ellipse"
		                           : L"Resize rectangle"));
		dialog.PrimaryButtonText(L"Apply");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(L"Page " + to_hstring(existing.target.page + 1) +
		              L" \u00b7 Size uses the PDF's original orientation. "
		              L"72 pt = 1 inch. The top-left corner stays fixed.");
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		TextBox width, height;
		width.Header(box_value(L"Width (pt)"));
		height.Header(box_value(L"Height (pt)"));
		width.MaxLength(64);
		height.MaxLength(64);
		const auto& bounds = *existing.bounds;
		width.Text(
		    to_hstring(helpers::textBoxDimension(bounds.right - bounds.left)));
		height.Text(
		    to_hstring(helpers::textBoxDimension(bounds.top - bounds.bottom)));
		Automation::AutomationProperties::SetName(width,
		                                          L"Shape width in PDF points");
		Automation::AutomationProperties::SetName(
		    height, L"Shape height in PDF points");
		fields.Children().Append(width);
		fields.Children().Append(height);
		TextBlock error;
		error.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(error);
		ScrollViewer scroll;
		scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
		scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
		scroll.Padding({0, 0, 12, 0});
		scroll.Content(fields);
		const auto fit = [body = make_weak(scroll),
		                  root = make_weak(XamlRoot())] {
			if (const auto content = body.get()) {
				if (const auto host = root.get()) {
					content.MaxHeight(
					    (std::max)(80., host.Size().Height - 210.));
				}
			}
		};
		fit();
		const auto sizeChanged =
		    XamlRoot().Changed(auto_revoke, [fit](auto&&, auto&&) { fit(); });
		dialog.Content(scroll);
		dialog.Opened([width](auto&&, auto&&) {
			width.Focus(FocusState::Programmatic);
			width.SelectAll();
		});
		dialog.Closing(
		    [weak = get_weak()](const ContentDialog& sender,
		                        const ContentDialogClosingEventArgs& args) {
			    if (const auto self = weak.get();
			        self && self->m_noteBusy && self->m_noteDialog == sender) {
				    args.Cancel(true);
			    }
		    });
		const auto recover = [weak = get_weak(), session,
		                      prompt = make_weak(dialog),
		                      wide = make_weak(width), tall = make_weak(height),
		                      failure = make_weak(error)](hstring message) {
			const auto self = weak.get();
			const auto view = prompt.get();
			if (!self || self->m_closed || self->m_session.get() != session ||
			    !view || self->m_noteDialog != view) {
				return;
			}
			self->m_noteBusy = false;
			if (const auto w = wide.get()) {
				w.IsReadOnly(false);
			}
			if (const auto h = tall.get()) {
				h.IsReadOnly(false);
			}
			view.IsPrimaryButtonEnabled(true);
			if (const auto label = failure.get()) {
				label.Text(message);
			}
			self->showComments();
		};
		dialog.PrimaryButtonClick(
		    [weak = get_weak(), session, existing, prompt = make_weak(dialog),
		     wide = make_weak(width), tall = make_weak(height),
		     failure = make_weak(error),
		     recover](auto&&, const ContentDialogButtonClickEventArgs& args) {
			    args.Cancel(true);
			    const auto self = weak.get();
			    const auto view = prompt.get();
			    const auto w = wide.get(), h = tall.get();
			    if (!self || self->m_closed || self->m_noteBusy || !view ||
			        !w || !h || self->m_session.get() != session ||
			        self->m_noteDialog != view) {
				    return;
			    }
			    const auto& bounds = *existing.bounds;
			    const auto width = helpers::parseTextBoxDimension(
			        to_string(w.Text()), bounds.right - bounds.left);
			    const auto height = helpers::parseTextBoxDimension(
			        to_string(h.Text()), bounds.top - bounds.bottom);
			    if (!width || !height) {
				    recover(L"Enter positive width and height in PDF points. "
				            L"Use a decimal point for fractional sizes. Your "
				            L"draft is still here.");
				    return;
			    }
			    self->m_noteBusy = true;
			    view.IsPrimaryButtonEnabled(false);
			    w.IsReadOnly(true);
			    h.IsReadOnly(true);
			    if (const auto label = failure.get()) {
				    label.Text(L"Resizing shape\u2026");
			    }
			    try {
				    const bool queued = session->resizeShape(
				        existing, {*width, *height},
				        [weak, session, prompt, recover,
				         queue = self->DispatcherQueue()](auto result) {
					        queue.TryEnqueue(
					            [weak, session, prompt, recover, result] {
						            const auto owner = weak.get();
						            if (!owner || owner->m_closed ||
						                owner->m_session.get() != session) {
							            return;
						            }
						            if (!result.index) {
							            recover(writeNotice(
							                result.refusal,
							                L"Couldn't resize this shape. "
							                L"It may use an unsupported "
							                L"style, be protected, or "
							                L"extend outside the page. "
							                L"Check its size. Your draft "
							                L"is still here."));
							            return;
						            }
						            owner->m_noteBusy = false;
						            owner->m_notesDirty =
						                owner->m_notesDirty || result.changed;
						            if (const auto dialog = prompt.get()) {
							            dialog.Hide();
						            }
						            if (result.changed) {
							            owner->refreshNotes(result.page);
						            }
						            owner->showComments();
					            });
				        });
				    if (queued) {
					    self->showComments();
					    return;
				    }
			    } catch (...) {
				    diagnostics::caught(diagnostics::Level::Error,
				                        "Could not queue the shape resize");
			    }
			    recover(
			        L"Couldn't queue this resize. Your draft is still here.");
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
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open the shape editor");
		m_noteBusy = false;
		if (m_noteDialog) {
			m_noteDialog.Hide();
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showNotice(L"Couldn't open the shape editor. Try again.");
			showComments();
		}
	}
}

fire_and_forget
ViewerPage::editExistingNote(int page, int index, std::u16string expected,
                             std::optional<pdf::TextMarkup> markup,
                             bool contentsPresent,
                             std::optional<pdf::AnnotationTarget> target) {
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
		const auto session = m_session.get();
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(markup ? L"Edit comment" : L"Edit note"));
		dialog.PrimaryButtonText(L"Apply");
		if (markup) {
			dialog.SecondaryButtonText(L"Remove comment");
			dialog.IsSecondaryButtonEnabled(contentsPresent &&
			                                target.has_value());
		}
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(L"Page " + to_hstring(page + 1) +
		              (markup
		                   ? L" \u00b7 Removing the comment keeps its markup."
		                   : L" \u00b7 Clear the text to delete this note."));
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		TextBox contents;
		contents.Header(box_value(markup ? L"Comment" : L"Note"));
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
		dialog.PrimaryButtonText(!markup && contents.Text().empty() ? L"Delete"
		                                                            : L"Apply");
		dialog.IsPrimaryButtonEnabled(!contents.Text().empty() ||
		                              (!markup && target.has_value()));
		contents.TextChanged(
		    [weak = get_weak(), prompt = make_weak(dialog), markup,
		     target](const Windows::Foundation::IInspectable& sender, auto&&) {
			    if (const auto self = weak.get(); self && !self->m_noteBusy) {
				    if (const auto view = prompt.get()) {
					    const bool empty = sender.as<TextBox>().Text().empty();
					    view.PrimaryButtonText(!markup && empty ? L"Delete"
					                                            : L"Apply");
					    view.IsPrimaryButtonEnabled(
					        !empty || (!markup && target.has_value()));
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
		const auto submit = [weak = get_weak(), prompt = make_weak(dialog),
		                     text = make_weak(contents),
		                     failure = make_weak(error), initial, page, index,
		                     markup, contentsPresent, target, session,
		                     expected =
		                         std::move(expected)](bool clearComment) {
			const auto self = weak.get();
			const auto view = prompt.get();
			const auto field = text.get();
			const auto message = failure.get();
			if (!self || self->m_closed || self->m_noteBusy || !view ||
			    self->m_session.get() != session || !field || !message ||
			    (clearComment && (!markup || !contentsPresent || !target)) ||
			    (!clearComment && field.Text().empty() &&
			     (markup || !target))) {
				return;
			}
			const auto value = field.Text();
			const bool deleteNote = !markup && value.empty();
			const auto replacement =
			    value == *initial ? expected
			                      : std::u16string(value.begin(), value.end());
			self->m_noteBusy = true;
			view.IsPrimaryButtonEnabled(false);
			view.IsSecondaryButtonEnabled(false);
			field.IsReadOnly(true);
			message.Text(deleteNote     ? L"Deleting note\u2026"
			             : clearComment ? L"Removing comment\u2026"
			                            : L"Applying changes\u2026");
			try {
				const auto reply = [weak, prompt, text, failure, markup,
				                    session, contentsPresent, target,
				                    queue =
				                        self->DispatcherQueue()](auto result) {
					queue.TryEnqueue([weak, prompt, text, failure, markup,
					                  contentsPresent, target, session,
					                  result] {
						const auto owner = weak.get();
						if (!owner || owner->m_closed ||
						    owner->m_session.get() != session) {
							return;
						}
						owner->m_noteBusy = false;
						owner->m_notesDirty =
						    owner->m_notesDirty || result.changed;
						try {
							if (const auto dialog = prompt.get()) {
								if (result.index || result.changed) {
									dialog.Hide();
								} else {
									if (const auto field = text.get()) {
										field.IsReadOnly(false);
										dialog.IsPrimaryButtonEnabled(
										    !field.Text().empty() ||
										    (!markup && target.has_value()));
										dialog.IsSecondaryButtonEnabled(
										    markup && contentsPresent &&
										    target.has_value());
									}
									if (const auto message = failure.get()) {
										message.Text(writeNotice(
										    result.refusal,
										    L"Couldn't apply this "
										    L"change. The annotation may have "
										    L"changed. Your text is "
										    L"still here."));
									}
								}
							}
							if (result.changed) {
								owner->refreshNotes(result.page);
							}
							owner->showComments();
						} catch (...) {
							diagnostics::caught(
							    diagnostics::Level::Error,
							    "Couldn't refresh the edited note.");
						}
					});
				};
				const bool queued =
				    deleteNote
				        ? self->m_session->removeAnnotation(*target, reply)
				    : clearComment
				        ? self->m_session->clearMarkupComment(
				              *target,
				              pdf::CommentContents{expected, contentsPresent},
				              reply)
				    : markup
				        ? self->m_session->updateMarkupComment(
				              page, index, *markup,
				              pdf::CommentContents{expected, contentsPresent},
				              replacement, reply, target ? target->revision : 0)
				        : self->m_session->updateTextNote(
				              page, index, expected, replacement, reply,
				              target ? target->revision : 0);
				if (queued) {
					self->showComments();
					return;
				}
			} catch (...) {
				diagnostics::caught(diagnostics::Level::Error,
				                    "Could not queue the note edit");
			}
			self->m_noteBusy = false;
			field.IsReadOnly(false);
			view.IsPrimaryButtonEnabled(true);
			view.IsSecondaryButtonEnabled(markup && contentsPresent &&
			                              target.has_value());
			message.Text(L"Couldn't queue this edit. Your text is still here.");
			self->showComments();
		};
		dialog.PrimaryButtonClick(
		    [submit](auto&&, const ContentDialogButtonClickEventArgs& args) {
			    args.Cancel(true);
			    submit(false);
		    });
		dialog.SecondaryButtonClick(
		    [submit](auto&&, const ContentDialogButtonClickEventArgs& args) {
			    args.Cancel(true);
			    submit(true);
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
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open the note editor");
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
