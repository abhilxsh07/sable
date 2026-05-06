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
#include <winrt/Microsoft.UI.Xaml.Automation.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
void ViewerPage::beginNotePlacement(PlacementTool tool) {
	clearAnnotationSelection();
	if (m_notePlacing && m_placementTool == tool) {
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
	m_shapeStart.reset();
	m_placementTool = tool;
	m_notePlacing = true;
	showComments();
	Scroller().Focus(FocusState::Programmatic);
}

bool ViewerPage::cancelNotePlacement() {
	m_shapeStart.reset();
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
			const NoteTarget target{static_cast<int>(page), x, y,
			                        key(page).rotation};
			if (m_placementTool == PlacementTool::Rectangle ||
			    m_placementTool == PlacementTool::Ellipse) {
				placeShape(target);
				break;
			}
			m_notePlacing = false;
			showComments();
			if (m_placementTool == PlacementTool::TextBox) {
				editTextBox(target);
			} else {
				editNote(target);
			}
			break;
		}
	}
	return true;
}

void ViewerPage::placeShape(NoteTarget target) {
	const bool ellipse = m_placementTool == PlacementTool::Ellipse;
	const auto session = m_session.get();
	const auto revision = session ? session->annotationRevision() : 0;
	if (!revision || !m_noteAllowed || m_noteBusy || m_noteSaving ||
	    m_noteDialog || m_passwordDialog) {
		cancelNotePlacement();
		return;
	}
	if (!m_shapeStart) {
		m_shapeStart = ShapeStart{target, revision};
		showComments();
		return;
	}
	const auto [first, expected] = *m_shapeStart;
	if (revision != expected) {
		m_shapeStart.reset();
		showComments();
		showNotice(L"The PDF changed. Click the first corner again.");
		return;
	}
	if (first.page != target.page) {
		showNotice(L"Choose the opposite corner on the same page.");
		return;
	}
	m_shapeStart.reset();
	m_notePlacing = false;
	m_noteBusy = true;
	try {
		const auto write = ellipse ? &pdf::Session::addEllipseAt
		                           : &pdf::Session::addRectangleAt;
		if ((session->*write)(
		        target.page, {first.x, first.y}, {target.x, target.y},
		        first.rotation, expected,
		        [weak = get_weak(), queue = DispatcherQueue(), session,
		         ellipse](pdf::NoteResult result) {
			        queue.TryEnqueue([weak, session, result, ellipse] {
				        const auto self = weak.get();
				        if (!self || self->m_closed ||
				            self->m_session.get() != session) {
					        return;
				        }
				        self->m_noteBusy = false;
				        self->m_notesDirty =
				            self->m_notesDirty || result.changed;
				        if (result.changed) {
					        self->Message().IsOpen(false);
					        self->refreshNotes();
				        } else if (!result.canceled) {
					        self->showNotice(
					            ellipse
					                ? L"Couldn't add this ellipse. Try a "
					                  L"larger area. "
					                  L"This PDF may restrict comments."
					                : L"Couldn't add this rectangle. Try a "
					                  L"larger "
					                  L"area. This PDF may restrict comments.");
				        }
				        self->showComments();
			        });
		        })) {
			showComments();
			return;
		}
	} catch (...) {
	}
	m_noteBusy = false;
	showComments();
	showNotice(ellipse ? L"Couldn't queue this ellipse. Try again."
	                   : L"Couldn't queue this rectangle. Try again.");
}

fire_and_forget
ViewerPage::editTextBox(NoteTarget target,
                        std::optional<pdf::Annotation> existing) {
	const auto lifetime = get_strong();
	try {
		if ((existing && !existing->bounds) || m_closed || !m_layout ||
		    !m_session || !m_noteAllowed || m_noteBusy || m_noteSaving ||
		    m_noteDialog || m_passwordDialog || !commentsOpen() ||
		    !XamlRoot() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    (m_notesAvailable && !m_notesAvailable())) {
			co_return;
		}
		leavePageBox();
		const auto session = m_session.get();
		std::uint64_t revision = existing ? existing->target.revision : 0;
		if (!existing) {
			struct Snapshot {
				handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
				pdf::FormFieldsResult result;
			};
			const auto snapshot = std::make_shared<Snapshot>();
			check_bool(static_cast<bool>(snapshot->ready));
			m_noteBusy = true;
			showComments();
			const apartment_context ui;
			check_bool(
			    session->formFields(target.page, [snapshot](auto result) {
				    snapshot->result = std::move(result);
				    SetEvent(snapshot->ready.get());
			    }));
			co_await resume_on_signal(snapshot->ready.get());
			co_await ui;
			m_noteBusy = false;
			if (m_closed || m_session.get() != session || !commentsOpen() ||
			    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
				co_return;
			}
			if (!snapshot->result.fields || snapshot->result.canceled) {
				showNotice(
				    L"Couldn't read this page for a text box. Try again.");
				showComments();
				co_return;
			}
			revision = snapshot->result.fields->revision;
		}
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(existing ? L"Edit text box" : L"Add text box"));
		dialog.PrimaryButtonText(existing ? L"Apply" : L"Add");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(
		    L"Page " + to_hstring(target.page + 1) +
		    L" \u00b7 Up to 8 lines and 128 characters. Use English letters, "
		    L"numbers, spaces and standard punctuation. Press Enter for a "
		    L"new line. The text must fit inside the box.");
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		TextBox author;
		author.Header(box_value(existing ? L"Author" : L"Author (optional)"));
		author.MaxLength(32767);
		author.Text(existing ? to_hstring(existing->author) : m_textBoxAuthor);
		author.IsReadOnly(existing.has_value());
		fields.Children().Append(author);
		TextBox contents;
		contents.Header(box_value(L"Text"));
		contents.AcceptsReturn(true);
		contents.TextWrapping(TextWrapping::Wrap);
		contents.MinHeight(100);
		contents.MaxHeight(180);
		contents.MinWidth(280);
		contents.Text(existing ? to_hstring(existing->contents)
		                       : m_textBoxDraft);
		Automation::AutomationProperties::SetName(contents, L"Text box text");
		fields.Children().Append(contents);
		TextBox width, height;
		if (existing) {
			width.Header(box_value(L"Width (pt)"));
			height.Header(box_value(L"Height (pt)"));
			width.MaxLength(64);
			height.MaxLength(64);
			const auto& rect = *existing->bounds;
			width.Text(
			    to_hstring(helpers::textBoxDimension(rect.right - rect.left)));
			height.Text(
			    to_hstring(helpers::textBoxDimension(rect.top - rect.bottom)));
			Automation::AutomationProperties::SetName(
			    width, L"Text box width in PDF points");
			Automation::AutomationProperties::SetName(
			    height, L"Text box height in PDF points");
			Grid sizes;
			sizes.ColumnSpacing(12);
			sizes.ColumnDefinitions().Append(ColumnDefinition{});
			sizes.ColumnDefinitions().Append(ColumnDefinition{});
			Grid::SetColumn(height, 1);
			sizes.Children().Append(width);
			sizes.Children().Append(height);
			fields.Children().Append(sizes);
			TextBlock units;
			units.Text(
			    L"Size uses the PDF's original orientation. 72 pt = 1 inch. "
			    L"The top-left corner stays fixed.");
			units.TextWrapping(TextWrapping::Wrap);
			fields.Children().Append(units);
		}
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
		dialog.IsPrimaryButtonEnabled(!contents.Text().empty());
		contents.TextChanged(
		    [prompt = make_weak(dialog)](auto&& sender, auto&&) {
			    if (const auto view = prompt.get()) {
				    view.IsPrimaryButtonEnabled(
				        !sender.template as<TextBox>().Text().empty());
			    }
		    });
		dialog.Opened([contents](auto&&, auto&&) {
			contents.Focus(FocusState::Programmatic);
			contents.SelectAll();
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
		                      editing = existing.has_value(),
		                      prompt = make_weak(dialog),
		                      text = make_weak(contents),
		                      writer = make_weak(author),
		                      wide = make_weak(width), tall = make_weak(height),
		                      failure = make_weak(error)](hstring message) {
			const auto self = weak.get();
			const auto view = prompt.get();
			const auto field = text.get();
			const auto name = writer.get();
			if (!self || self->m_closed || self->m_session.get() != session ||
			    !view || !field || !name || self->m_noteDialog != view) {
				return;
			}
			self->m_noteBusy = false;
			field.IsReadOnly(false);
			name.IsReadOnly(editing);
			if (const auto dimension = wide.get()) {
				dimension.IsReadOnly(false);
			}
			if (const auto dimension = tall.get()) {
				dimension.IsReadOnly(false);
			}
			view.IsPrimaryButtonEnabled(!field.Text().empty());
			if (const auto label = failure.get()) {
				label.Text(message);
			}
			self->showComments();
		};
		dialog.PrimaryButtonClick([weak = get_weak(), session, target, revision,
		                           existing, prompt = make_weak(dialog),
		                           text = make_weak(contents),
		                           writer = make_weak(author),
		                           failure = make_weak(error),
		                           wide = make_weak(width),
		                           tall = make_weak(height), recover](
		                              auto&&,
		                              const ContentDialogButtonClickEventArgs&
		                                  args) {
			args.Cancel(true);
			const auto self = weak.get();
			const auto view = prompt.get();
			const auto field = text.get();
			const auto name = writer.get();
			if (!self || self->m_closed || self->m_noteBusy || !view ||
			    !field || !name || field.Text().empty() ||
			    self->m_session.get() != session) {
				return;
			}
			std::optional<pdf::TextBoxSize> size;
			if (existing) {
				const auto w = wide.get(), h = tall.get();
				if (!w || !h) {
					return;
				}
				const auto parsedWidth = helpers::parseTextBoxDimension(
				    to_string(w.Text()),
				    existing->bounds->right - existing->bounds->left);
				const auto parsedHeight = helpers::parseTextBoxDimension(
				    to_string(h.Text()),
				    existing->bounds->top - existing->bounds->bottom);
				if (!parsedWidth || !parsedHeight) {
					if (const auto label = failure.get()) {
						label.Text(
						    L"Enter positive width and height in PDF points. "
						    L"Use a decimal point for fractional sizes. Your "
						    L"draft is still here.");
					}
					return;
				}
				size = pdf::TextBoxSize{*parsedWidth, *parsedHeight};
			}
			const auto value = field.Text();
			std::u16string replacement;
			constexpr auto layout = pdf::TextBoxLayout::ExplicitLines;
			replacement.reserve(
			    (std::min)(value.size(), pdf::textBoxMaxCharacters + 1));
			for (hstring::size_type i = 0;
			     i < value.size() &&
			     replacement.size() <= pdf::textBoxMaxCharacters;
			     ++i) {
				if (value[i] == L'\r') {
					replacement.push_back(u'\n');
					if (i + 1 < value.size() && value[i + 1] == L'\n') {
						++i;
					}
				} else {
					replacement.push_back(static_cast<char16_t>(value[i]));
				}
			}
			const wchar_t* problem = nullptr;
			switch (pdf::validateTextBoxText(replacement, layout)) {
			case pdf::TextBoxTextError::None:
				break;
			case pdf::TextBoxTextError::Blank:
				problem = L"Enter text on at least one line.";
				break;
			case pdf::TextBoxTextError::Length:
				problem = L"Use up to 128 characters, including line breaks.";
				break;
			case pdf::TextBoxTextError::Lines:
				problem = L"Use up to 8 lines, including blank lines.";
				break;
			case pdf::TextBoxTextError::Characters:
				problem = L"Use English letters, numbers, spaces and standard "
				          L"punctuation.";
				break;
			}
			if (problem) {
				if (const auto label = failure.get()) {
					label.Text(hstring(problem) +
					           L" Your draft is still here.");
				}
				return;
			}
			self->m_noteBusy = true;
			view.IsPrimaryButtonEnabled(false);
			field.IsReadOnly(true);
			name.IsReadOnly(true);
			if (const auto dimension = wide.get()) {
				dimension.IsReadOnly(true);
			}
			if (const auto dimension = tall.get()) {
				dimension.IsReadOnly(true);
			}
			if (const auto label = failure.get()) {
				label.Text(existing ? L"Applying text box\u2026"
				                    : L"Adding text box\u2026");
			}
			try {
				const auto by = name.Text();
				const auto completed =
				    [weak, session, prompt, text, recover,
				     editing = existing.has_value(),
				     queue = self->DispatcherQueue()](auto result) {
					    queue.TryEnqueue([weak, session, prompt, text, recover,
					                      editing, result] {
						    const auto owner = weak.get();
						    if (!owner || owner->m_closed ||
						        owner->m_session.get() != session) {
							    return;
						    }
						    try {
							    if (result.index) {
								    owner->m_noteBusy = false;
								    owner->m_notesDirty =
								        owner->m_notesDirty || result.changed;
								    if (const auto input = text.get();
								        input && !editing) {
									    input.Text(L"");
								    }
								    if (const auto dialog = prompt.get()) {
									    dialog.Hide();
								    }
								    if (result.changed) {
									    owner->refreshNotes();
								    }
								    owner->showComments();
							    } else {
								    recover(L"Couldn't apply this text box. "
								            L"The PDF may restrict "
								            L"this change, or the text may "
								            L"not fit inside the page or box. "
								            L"Check the text and box size. "
								            L"Your draft is still here.");
							    }
						    } catch (...) {
							    diagnostics::write(
							        diagnostics::Level::Error,
							        "Couldn't refresh the text box.");
						    }
					    });
				    };
				const bool queued =
				    existing ? session->updateTextBox(*existing,
				                                      std::move(replacement),
				                                      *size, completed, layout)
				             : session->addTextBoxAt(
				                   target.page, target.x, target.y,
				                   target.rotation, std::move(replacement),
				                   std::u16string(by.begin(), by.end()),
				                   revision, completed, layout);
				if (queued) {
					self->showComments();
					return;
				}
			} catch (...) {
			}
			recover(L"Couldn't queue this text box. Your draft is still here.");
		});
		m_noteDialog = dialog;
		co_await dialog.ShowAsync();
		if (m_noteDialog == dialog) {
			m_noteDialog = nullptr;
		}
		if (!existing) {
			m_textBoxDraft = contents.Text();
			m_textBoxAuthor = author.Text();
		}
		if (!m_closed) {
			showComments();
		}
	} catch (...) {
		m_noteBusy = false;
		if (m_noteDialog) {
			m_noteDialog.Hide();
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showNotice(L"Couldn't open the text box editor. Try again.");
			showComments();
		}
	}
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
		location.Text(L"Page " + to_hstring(page + 1));
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
	clearAnnotationSelection();
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
