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
#include "helpers/logging.h"
#include "ViewerPage.xaml.h"
#include "pdf/session.h"
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
					        if (found != fields.rend() &&
					            found->supportsEditor()) {
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
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not inspect the form field");
	}
	requestAnnotationAt(point);
}

fire_and_forget ViewerPage::editFormField(int page, std::uint64_t revision,
                                          pdf::FormField field) {
	const auto lifetime = get_strong();
	try {
		const bool checkbox = field.type == pdf::FormFieldType::CheckBox;
		const bool radio = field.type == pdf::FormFieldType::RadioButton;
		const bool button = checkbox || radio;
		const bool selection = field.singleChoice();
		const bool clearList =
		    selection && field.type == pdf::FormFieldType::ListBox;
		if ((button && !field.checked.has_value()) ||
		    (radio && !field.control) ||
		    (!button && !selection && field.type != pdf::FormFieldType::Text)) {
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
		dialog.Title(box_value(selection  ? L"Select field option"
		                       : radio    ? L"Select radio option"
		                       : checkbox ? L"Edit checkbox"
		                                  : L"Edit text field"));
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
		if (field.noSpellCheck()) {
			contents.IsSpellCheckEnabled(false);
		}
		if (field.multiline()) {
			contents.AcceptsReturn(true);
			contents.TextWrapping(TextWrapping::Wrap);
			contents.MinHeight(100);
			contents.MaxHeight(180);
		}
		const auto raw = to_hstring(field.value);
		contents.Text(raw);
		Automation::AutomationProperties::SetName(contents, L"Field value");
		Primitives::ToggleButton choice = CheckBox();
		if (radio) {
			choice = RadioButton();
		}
		choice.Content(box_value(radio ? L"Selected" : L"Checked"));
		choice.IsChecked(field.checked.value_or(false));
		Automation::AutomationProperties::SetName(
		    choice, radio ? L"Field selected" : L"Field checked");
		Primitives::Selector options = ComboBox();
		if (field.type == pdf::FormFieldType::ListBox) {
			options = ListBox();
			options.MaxHeight(240);
		}
		options.MinWidth(320);
		Automation::AutomationProperties::SetName(options, L"Field option");
		if (selection) {
			int selected = -1;
			for (const auto& option : *field.options) {
				// i give repeated labels separate identities for automation.
				// see:
				// https://github.com/microsoft/microsoft-ui-xaml/issues/11226
				ContentControl item = ComboBoxItem();
				if (field.type == pdf::FormFieldType::ListBox) {
					item = ListBoxItem();
				}
				item.Content(box_value(to_hstring(option.label)));
				options.Items().Append(item);
				if (option.selected) {
					selected = option.index;
				}
			}
			options.SelectedIndex(selected);
			dialog.IsPrimaryButtonEnabled(clearList || selected >= 0);
			options.SelectionChanged([prompt = make_weak(dialog),
			                          list = make_weak(options),
			                          clearList](auto&&, auto&&) {
				const auto view = prompt.get();
				const auto control = list.get();
				if (view && control) {
					view.IsPrimaryButtonEnabled(clearList ||
					                            control.SelectedIndex() >= 0);
				}
			});
		}
		if (radio) {
			dialog.IsPrimaryButtonEnabled(field.checked.value_or(false));
			choice.Checked([prompt = make_weak(dialog)](auto&&, auto&&) {
				if (const auto view = prompt.get()) {
					view.IsPrimaryButtonEnabled(true);
				}
			});
		}
		fields.MinWidth(320);
		fields.Children().Append(selection ? options.as<UIElement>()
		                         : button  ? choice.as<UIElement>()
		                                   : contents.as<UIElement>());
		TextBlock error;
		error.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(error);
		Button clearSelection;
		if (clearList) {
			clearSelection.Name(L"ClearFieldSelection");
			clearSelection.Content(box_value(L"Clear selection"));
			Automation::AutomationProperties::SetName(clearSelection,
			                                          L"Clear selection");
			clearSelection.Click([list = make_weak(options)](auto&&, auto&&) {
				if (const auto field = list.get(); field && field.IsEnabled()) {
					field.SelectedIndex(-1);
				}
			});
			fields.Children().Append(clearSelection);
		}
		dialog.Content(fields);
		const std::u16string expected(raw.begin(), raw.end());
		const auto initial = std::make_shared<hstring>(contents.Text());
		dialog.Opened([initial, text = make_weak(contents),
		               check = make_weak(choice), list = make_weak(options),
		               button, selection](auto&&, auto&&) {
			if (selection) {
				if (const auto field = list.get()) {
					field.Focus(FocusState::Programmatic);
				}
			} else if (button) {
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
		                           check = make_weak(choice), checkbox, radio,
		                           list = make_weak(options), selection,
		                           clear = make_weak(clearSelection), clearList,
		                           target = field, control = field.control,
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
			const auto options = list.get();
			const auto clearButton = clear.get();
			const auto message = failure.get();
			if (!self || self->m_closed || self->m_noteBusy ||
			    self->m_noteSaving || self->m_session.get() != session ||
			    !self->m_fillAllowed || !view || self->m_noteDialog != view ||
			    !field || !choice || !options || !message ||
			    self->Visibility() != XVisibility::Visible ||
			    (self->m_notesAvailable && !self->m_notesAvailable())) {
				return;
			}
			if (radio && !unbox_value_or<bool>(choice.IsChecked(), false)) {
				return;
			}
			if (selection && !clearList && options.SelectedIndex() < 0) {
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
			options.IsEnabled(false);
			if (clearButton) {
				clearButton.IsEnabled(false);
			}
			message.Text(L"Applying changes\u2026");
			try {
				auto reply = [weak, prompt, text, check, list, clear,
				              button = checkbox || radio || selection, failure,
				              session, queue = self->DispatcherQueue()](
				                 pdf::FormWriteResult result) {
					queue.TryEnqueue([weak, prompt, text, check, list, clear,
					                  button, failure, session, result] {
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
								if (const auto field = list.get()) {
									field.IsEnabled(true);
								}
								if (const auto field = clear.get()) {
									field.IsEnabled(true);
								}
								dialog.IsPrimaryButtonEnabled(true);
								if (const auto message = failure.get()) {
									message.Text(writeNotice(
									    result.refusal,
									    button
									        ? L"Couldn't apply this choice. "
									          L"The field may be protected or "
									          L"use "
									          L"an unsupported form feature. "
									          L"Your choice is still here."
									        : L"Couldn't apply this value. "
									          L"The field may be protected, "
									          L"have a length limit, or use "
									          L"an unsupported form feature. "
									          L"Your text is still here."));
								}
							}
						}
						if (result.changed) {
							owner->refreshNotes();
							owner->showNotice(
							    L"Field updated. Save a copy to keep "
							    L"your changes.",
							    InfoBarSeverity::Success);
						}
						owner->showComments();
					});
				};
				const bool queued =
				    selection ? self->m_session->updateChoiceField(
				                    page, target, options.SelectedIndex(),
				                    revision, std::move(reply))
				    : radio ? self->m_session->updateRadioButton(
				                  page, index, expected, *control, revision,
				                  std::move(reply))
				    : checkbox
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
				diagnostics::caught(diagnostics::Level::Error,
				                    "Could not queue the field edit");
			}
			self->m_noteBusy = false;
			field.IsReadOnly(false);
			choice.IsEnabled(true);
			options.IsEnabled(true);
			if (clearButton) {
				clearButton.IsEnabled(true);
			}
			view.IsPrimaryButtonEnabled(true);
			message.Text(
			    checkbox || radio || selection
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
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open the field editor");
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
