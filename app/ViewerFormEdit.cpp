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
#include <fpdf_annot.h>
#include <algorithm>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using XVisibility = Microsoft::UI::Xaml::Visibility;
namespace winrt::Sable::implementation {
void ViewerPage::cancelFormRequest() {
	++m_formVersion;
	if (m_session) {
		m_session->cancelForms();
	}
}

void ViewerPage::requestFormAt(Windows::Foundation::Point point) {
	if (!m_fillAllowed) {
		requestAnnotationAt(point);
		return;
	}
	if (m_closed || !m_session || !m_layout || m_presentation ||
	    m_directManipulation || m_noteBusy || m_noteSaving || m_noteDialog ||
	    m_notePlacing || Visibility() != XVisibility::Visible ||
	    (m_notesAvailable && !m_notesAvailable())) {
		return;
	}
	clearAnnotationSelection();
	const auto page = m_layout->pageAt(point.X, point.Y);
	const auto& bounds = m_layout->pages()[page];
	const auto source =
	    helpers::rotatePoint({(point.X - bounds.x) / bounds.width,
	                          (point.Y - bounds.y) / bounds.height},
	                         -m_rotation);
	if (source.x < 0 || source.x > 1 || source.y < 0 || source.y > 1) {
		return;
	}
	const auto session = m_session.get();
	const auto version = m_formVersion, rotation = m_rotationVersion,
	           links = m_linkVersion, render = m_renderVersion;
	const auto zoom = m_zoom, horizontal = Scroller().HorizontalOffset(),
	           vertical = Scroller().VerticalOffset();
	const auto at = pixels(page);
	try {
		if (session->formFields(
		        static_cast<int>(page),
		        [weak = get_weak(), queue = DispatcherQueue(), session, version,
		         rotation, links, render, zoom, horizontal, vertical, at, page,
		         point, source](pdf::FormFieldsResult result) {
			        queue.TryEnqueue([weak, session, version, rotation, links,
			                          render, zoom, horizontal, vertical, at,
			                          page, point, source,
			                          result = std::move(result)] {
				        const auto self = weak.get();
				        if (!self || self->m_closed ||
				            self->m_session.get() != session ||
				            self->m_formVersion != version || !self->m_layout ||
				            self->m_rotationVersion != rotation ||
				            self->m_linkVersion != links ||
				            self->m_renderVersion != render ||
				            self->m_zoom != zoom || self->pixels(page) != at ||
				            self->m_directManipulation ||
				            self->Scroller().HorizontalOffset() != horizontal ||
				            self->Scroller().VerticalOffset() != vertical ||
				            self->Visibility() != XVisibility::Visible ||
				            result.canceled || !self->m_fillAllowed ||
				            self->m_noteBusy || self->m_noteSaving ||
				            self->m_noteDialog || self->m_notePlacing ||
				            (self->m_notesAvailable &&
				             !self->m_notesAvailable())) {
					        return;
				        }
				        if (result.fields && !result.fields->limited) {
					        const auto& fields = result.fields->entries;
					        const auto found = std::find_if(
					            fields.rbegin(), fields.rend(),
					            [source](const auto& field) {
						            const auto& b = field.displayBounds;
						            return b && source.x >= b->left &&
						                   source.x <= b->right &&
						                   source.y >= b->top &&
						                   source.y <= b->bottom;
					            });
					        constexpr int plain =
					            FPDF_FORMFLAG_REQUIRED | FPDF_FORMFLAG_NOEXPORT;
					        constexpr int locked =
					            FPDF_ANNOT_FLAG_READONLY |
					            FPDF_ANNOT_FLAG_LOCKED |
					            FPDF_ANNOT_FLAG_NOZOOM |
					            FPDF_ANNOT_FLAG_NOROTATE |
					            FPDF_ANNOT_FLAG_TOGGLENOVIEW | 0x200;
					        if (found != fields.rend() &&
					            (found->type == pdf::FormFieldType::Text ||
					             (found->type == pdf::FormFieldType::CheckBox &&
					              found->checked.has_value())) &&
					            !(found->flags & ~plain) &&
					            !(found->annotationFlags & locked)) {
						        self->editFormField(static_cast<int>(page),
						                            result.fields->revision,
						                            *found);
						        return;
					        }
				        }
				        self->requestAnnotationAt(point);
			        });
		        })) {
			return;
		}
	} catch (...) {
	}
	requestAnnotationAt(point);
}

fire_and_forget ViewerPage::editFormField(int page, std::uint64_t revision,
                                          pdf::FormField field) {
	const auto lifetime = get_strong();
	try {
		const bool checkbox = field.type == pdf::FormFieldType::CheckBox;
		if ((checkbox && !field.checked.has_value()) ||
		    (!checkbox && field.type != pdf::FormFieldType::Text)) {
			co_return;
		}
		if (m_closed || !m_session || !m_fillAllowed || m_presentation ||
		    m_noteBusy || m_noteSaving || m_noteDialog || m_notePlacing ||
		    m_passwordDialog || !XamlRoot() ||
		    Visibility() != XVisibility::Visible ||
		    (m_notesAvailable && !m_notesAvailable())) {
			co_return;
		}
		leavePageBox();
		clearTextSelection();
		if (m_textMenu) {
			m_textMenu.Hide();
		}
		const auto session = m_session.get();
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(
		    box_value(checkbox ? L"Edit checkbox" : L"Edit text field"));
		dialog.PrimaryButtonText(L"Apply");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		dialog.PreviewKeyDown(
		    [](auto&&, const Input::KeyRoutedEventArgs& args) {
			    if (args.Key() == Windows::System::VirtualKey::Tab &&
			        (GetKeyState(VK_CONTROL) & 0x8000) &&
			        !(GetKeyState(VK_MENU) & 0x8000)) {
				    args.Handled(true);
			    }
		    });
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(L"Page " + to_hstring(page + 1) +
		              (field.name.empty()
		                   ? hstring{}
		                   : L" \u00b7 " + to_hstring(field.name)));
		location.TextWrapping(TextWrapping::Wrap);
		location.MaxLines(2);
		location.TextTrimming(TextTrimming::CharacterEllipsis);
		fields.Children().Append(location);
		TextBox contents;
		contents.Header(box_value(L"Value"));
		contents.MinWidth(320);
		contents.MaxLength(32767);
		const auto raw = to_hstring(field.value);
		contents.Text(raw);
		Automation::AutomationProperties::SetName(contents, L"Field value");
		CheckBox choice;
		choice.Content(box_value(L"Checked"));
		choice.IsChecked(field.checked.value_or(false));
		Automation::AutomationProperties::SetName(choice, L"Field checked");
		fields.MinWidth(320);
		fields.Children().Append(checkbox ? choice.as<UIElement>()
		                                  : contents.as<UIElement>());
		TextBlock error;
		error.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(error);
		dialog.Content(fields);
		const std::u16string expected(raw.begin(), raw.end());
		const auto initial = std::make_shared<hstring>(contents.Text());
		dialog.Opened([initial, text = make_weak(contents),
		               check = make_weak(choice), checkbox](auto&&, auto&&) {
			if (checkbox) {
				if (const auto field = check.get()) {
					field.Focus(FocusState::Programmatic);
				}
			} else if (const auto field = text.get()) {
				*initial = field.Text();
				field.Focus(FocusState::Programmatic);
				field.SelectAll();
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
		                           check = make_weak(choice), checkbox,
		                           failure = make_weak(error), initial,
		                           expected, session, page, index = field.index,
		                           revision](
		                              auto&&,
		                              const ContentDialogButtonClickEventArgs&
		                                  args) {
			args.Cancel(true);
			const auto self = weak.get();
			const auto view = prompt.get();
			const auto field = text.get();
			const auto choice = check.get();
			const auto message = failure.get();
			if (!self || self->m_closed || self->m_noteBusy ||
			    self->m_noteSaving || self->m_session.get() != session ||
			    !self->m_fillAllowed || !view || self->m_noteDialog != view ||
			    !field || !choice || !message ||
			    self->Visibility() != XVisibility::Visible ||
			    (self->m_notesAvailable && !self->m_notesAvailable())) {
				return;
			}
			const auto value = field.Text();
			const auto replacement =
			    value == *initial ? expected
			                      : std::u16string(value.begin(), value.end());
			self->m_noteBusy = true;
			view.IsPrimaryButtonEnabled(false);
			field.IsReadOnly(true);
			choice.IsEnabled(false);
			message.Text(L"Applying changes\u2026");
			try {
				auto reply = [weak, prompt, text, check, checkbox, failure,
				              session, queue = self->DispatcherQueue()](
				                 pdf::FormWriteResult result) {
					queue.TryEnqueue([weak, prompt, text, check, checkbox,
					                  failure, session, result] {
						const auto owner = weak.get();
						if (!owner || owner->m_closed ||
						    owner->m_session.get() != session) {
							return;
						}
						owner->m_noteBusy = false;
						owner->m_notesDirty =
						    owner->m_notesDirty || result.changed;
						owner->m_formsDirty =
						    owner->m_formsDirty || result.changed;
						if (const auto dialog = prompt.get()) {
							if (result.accepted) {
								dialog.Hide();
							} else {
								if (const auto field = text.get()) {
									field.IsReadOnly(false);
								}
								if (const auto field = check.get()) {
									field.IsEnabled(true);
								}
								dialog.IsPrimaryButtonEnabled(true);
								if (const auto message = failure.get()) {
									message.Text(
									    checkbox
									        ? L"Couldn't apply this choice. "
									          L"The field may be protected or "
									          L"use "
									          L"an unsupported form feature. "
									          L"Your choice is still here."
									        : L"Couldn't apply this value. "
									          L"The field may be protected, "
									          L"have a length limit, or use "
									          L"an unsupported form feature. "
									          L"Your text is still here.");
								}
							}
						}
						if (result.changed) {
							owner->refreshNotes();
							owner->showNotice(
							    L"Field updated. Save a copy to keep "
							    L"your changes.");
							owner->Message().Severity(InfoBarSeverity::Success);
						}
						owner->showComments();
					});
				};
				const bool queued =
				    checkbox
				        ? self->m_session->updateCheckBox(
				              page, index, expected,
				              unbox_value_or<bool>(choice.IsChecked(), false),
				              revision, std::move(reply))
				        : self->m_session->updateTextField(
				              page, index, expected, replacement, revision,
				              std::move(reply));
				if (queued) {
					self->showComments();
					return;
				}
			} catch (...) {
			}
			self->m_noteBusy = false;
			field.IsReadOnly(false);
			choice.IsEnabled(true);
			view.IsPrimaryButtonEnabled(true);
			message.Text(
			    checkbox
			        ? L"Couldn't queue this edit. Your choice is still here."
			        : L"Couldn't queue this edit. Your text is still here.");
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
			showNotice(L"Couldn't open the field editor. Try again.");
		}
	}
}
} // namespace winrt::Sable::implementation
