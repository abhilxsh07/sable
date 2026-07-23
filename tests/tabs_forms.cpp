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
#include "tabs_test.h"

namespace winrt::Sable::implementation {
void TabsTest::passwordFormTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (v.m_noteBusy || v.m_noteSaving ||
	    (notesSave &&
	     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
		return;
	}
	using namespace Microsoft::UI::Xaml::Automation;
	const auto dialog =
	    step < 2 || step == 11 ? v.m_passwordDialog : v.m_noteDialog;
	std::function<Button(DependencyObject, const wchar_t*)> find;
	find = [&](const DependencyObject& node, const wchar_t* name) -> Button {
		if (const auto item = node.try_as<Button>();
		    item && item.Name() == name) {
			return item;
		}
		for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node);
		     ++i) {
			if (const auto item =
			        find(Media::VisualTreeHelper::GetChild(node, i), name)) {
				return item;
			}
		}
		return nullptr;
	};
	if (dialog && !find(dialog, L"PrimaryButton")) {
		return;
	}
	const auto button = [&](const wchar_t* name) {
		const auto item = find(dialog, name);
		check(item && item.IsEnabled(), "password form button unavailable");
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	if (step < 2 || step == 11) {
		if (!dialog) {
			return;
		}
		const auto children = dialog.Content().as<StackPanel>().Children();
		if (step == 1) {
			check(children.GetAt(0).as<TextBlock>().Text() ==
			          L"Invalid password. Please try again.",
			      "wrong password did not return to the prompt");
		}
		children.GetAt(1).as<PasswordBox>().Password(step == 0 ? L"wrong"
		                                                       : L"sable");
		button(L"PrimaryButton");
		++step;
		ticks = 0;
		return;
	}
	if (!ready(v)) {
		return;
	}
	const auto read = [&] {
		const auto reply =
		    std::make_shared<std::promise<pdf::FormFieldsResult>>();
		auto future = reply->get_future();
		check(v.m_session->formFields(0,
		                              [reply](auto result) {
			                              reply->set_value(std::move(result));
		                              }),
		      "password form read not queued");
		check(future.wait_for(std::chrono::seconds(10)) ==
		          std::future_status::ready,
		      "password form read timed out");
		const auto result = future.get();
		check(result.fields && !result.fields->limited &&
		          result.fields->entries.size() == 2,
		      "password form snapshot missing");
		return result.fields;
	};
	const auto open = [&](int index = 0) {
		const auto fields = read();
		const auto& bounds = fields->entries[index].displayBounds;
		check(bounds.has_value(), "password form hit bounds missing");
		const auto& page = v.m_layout->pages()[0];
		v.requestFormAt(
		    {static_cast<float>(
		         page.x + page.width * (bounds->left + bounds->right) / 2),
		     static_cast<float>(
		         page.y + page.height * (bounds->top + bounds->bottom) / 2)});
	};
	const auto input = [&] {
		check(v.m_noteDialog != nullptr, "password form editor missing");
		return v.m_noteDialog.Content()
		    .as<StackPanel>()
		    .Children()
		    .GetAt(1)
		    .as<TextBox>();
	};
	switch (step) {
	case 2:
		mixedRevision = read()->revision;
		open();
		break;
	case 3:
	case 13:
		if (!dialog) {
			return;
		}
		check(input().Text() == (step == 3 ? L"old" : L"Caf\u00e9"),
		      "password form opened with wrong text");
		button(L"PrimaryButton");
		break;
	case 4:
		if (dialog) {
			return;
		}
		check(!v.m_formsDirty && read()->revision == mixedRevision,
		      "password form no-op changed the document");
		open();
		break;
	case 5:
		if (!dialog) {
			return;
		}
		input().Text(L"too long for field");
		button(L"PrimaryButton");
		check(v.m_noteBusy && input().IsReadOnly(),
		      "queued password form stayed editable");
		break;
	case 6: {
		const auto text = input();
		check(text.Text() == L"too long for field" && !text.IsReadOnly() &&
		          dialog.IsPrimaryButtonEnabled() &&
		          !dialog.Content()
		               .as<StackPanel>()
		               .Children()
		               .GetAt(2)
		               .as<TextBlock>()
		               .Text()
		               .empty() &&
		          !v.m_formsDirty && read()->revision == mixedRevision,
		      "password form refusal lost its draft");
		const auto tab = owner.Tabs().SelectedIndex();
		check(!owner.routeKey('W', helpers::SHORTCUT_CTRL, false) &&
		          owner.routeKey(VK_TAB, helpers::SHORTCUT_CTRL, false) &&
		          owner.Tabs().SelectedIndex() == tab &&
		          text.Text() == L"too long for field",
		      "password form modal shortcut escaped");
		text.Text(L"Caf\u00e9");
		button(L"PrimaryButton");
		break;
	}
	case 7:
		if (dialog) {
			return;
		}
		check(v.m_formsDirty && read()->entries[0].value == "Caf\u00e9",
		      "password form corrected retry failed");
		open();
		break;
	case 8:
	case 15:
		if (!dialog) {
			return;
		}
		input().Text(L"cancel this");
		button(L"CloseButton");
		break;
	case 9:
		if (dialog) {
			return;
		}
		check(read()->entries[0].value == "Caf\u00e9",
		      "Cancel changed password form");
		notesPath = buttonFieldPath.parent_path() / "password copy.pdf";
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		break;
	case 10:
		check(notesSave.GetResults() && !v.m_notesDirty && !v.m_formsDirty,
		      "password form savepoint failed");
		check(engine
		          .run([path = notesPath] {
			          const auto saved = pdf::openFile(path, "nightpdf");
			          return pdf::openFile(path).error ==
			                     pdf::Error::PasswordRequired &&
			                 saved.document &&
			                 saved.document->textNote(0, 2) ==
			                     u"Keep this note";
		          })
		          .get(),
		      "password form copy lost protection or its note");
		owner.openDocument(engine, notesPath, 1);
		break;
	case 12:
		check(read()->entries[0].value == "Caf\u00e9" &&
		          read()->entries[1].value == "keep",
		      "fresh password form lost its values");
		mixedRevision = read()->revision;
		open();
		break;
	case 14:
		if (dialog) {
			return;
		}
		check(!v.m_formsDirty && read()->revision == mixedRevision,
		      "reopened password form no-op changed savepoint");
		open(1);
		break;
	case 16: {
		if (dialog) {
			return;
		}
		check(!v.m_notesDirty && !v.m_formsDirty &&
		          read()->entries[1].value == "keep",
		      "reopened password form Cancel changed neighbor");
		std::ifstream source(buttonFieldPath, std::ios::binary);
		std::ifstream fixture("tests/fixtures/password_form.pdf",
		                      std::ios::binary);
		check(std::string(std::istreambuf_iterator<char>(source), {}) ==
		          std::string(std::istreambuf_iterator<char>(fixture), {}),
		      "password form source changed");
		std::fwprintf(stderr,
		              L"PASS: password prompt, native field, no-op, "
		              L"refusal/retry, modal keys, Cancel and encrypted "
		              L"reopen; %ls\n",
		              notesPath.c_str());
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	}
	++step;
	ticks = 0;
}

void TabsTest::mixedFormTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_noteSaving ||
	    (notesSave &&
	     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
		return;
	}
	using namespace Microsoft::UI::Xaml::Automation;
	const int kind = step < 45 ? step / 9 : (step - 47) / 2;
	const int listSelection = clearListOnly && !defaultListOnly ? -1 : 1;
	const int positions[] = {0, 1, 3, 4, 5};
	const hstring textDraft = combOnly        ? L"caf\u00e9"
	                          : multilineOnly ? L"Edited caf\u00e9\n\nthird"
	                                          : L"Edited caf\u00e9";
	const hstring overlong = combOnly ? L"12345"
	                         : !multilineOnly
	                             ? hstring(std::wstring(40, L'x'))
	                             : hstring(std::wstring(40, L'x') + L"\nlast");
	const auto paragraphs = [](std::wstring_view value) {
		std::wstring result;
		for (std::size_t i = 0; i < value.size(); ++i) {
			if (value[i] == L'\r') {
				result += L'\n';
				if (i + 1 < value.size() && value[i + 1] == L'\n') {
					++i;
				}
			} else {
				result += value[i];
			}
		}
		return result;
	};
	const auto read = [&] {
		const auto reply =
		    std::make_shared<std::promise<pdf::FormFieldsResult>>();
		auto future = reply->get_future();
		check(v.m_session->formFields(0,
		                              [reply](auto result) {
			                              reply->set_value(std::move(result));
		                              }),
		      "mixed snapshot not queued");
		check(future.wait_for(std::chrono::seconds(10)) ==
		          std::future_status::ready,
		      "mixed snapshot timed out");
		const auto result = future.get();
		check(result.fields && !result.fields->limited &&
		          result.fields->entries.size() == 6,
		      "mixed snapshot failed");
		return result.fields;
	};
	const auto verify = [&](int completed) {
		const auto fields = read();
		const auto& e = fields->entries;
		check(e[0].name == "text" &&
		          e[0].flags == ((inheritedOnly   ? (multilineOnly ? 4102 : 6)
		                          : combOnly      ? 16777216
		                          : multilineOnly ? 4096
		                                          : 0) |
		                         (noSpellCheckOnly ? 4194304 : 0)) &&
		          e[0].annotationFlags == 4 &&
		          e[0].value ==
		              (completed > 0
		                   ? (combOnly        ? "caf\u00e9"
		                      : multilineOnly ? "Edited caf\u00e9\r\n\r\nthird"
		                                      : "Edited caf\u00e9")
		                   : (combOnly        ? "AB"
		                      : multilineOnly ? "first\nsecond"
		                                      : "initial")) &&
		          e[1].name == "box" &&
		          e[1].value == (completed > 1 && !inheritedCheckBoxOnly
		                             ? "Approved now"
		                             : "Off") &&
		          e[1].checked == (completed > 1 && !inheritedCheckBoxOnly),
		      "mixed text or checkbox value changed");
		if (inheritedCheckBoxOnly) {
			check(e[1].flags == 6 && e[1].control && e[1].control->count == 1 &&
			          e[1].control->index == 0 &&
			          e[1].control->exportValue == "Approved now",
			      "inherited checkbox identity changed");
		}
		for (int i = 0; i < 2; ++i) {
			const auto& member = e[i + 2];
			check(member.name == "radio" &&
			          member.value ==
			              (completed > 2 ? "Next choice" : "First") &&
			          member.checked == ((completed > 2) == (i == 1)) &&
			          member.control && member.control->index == i &&
			          member.control->count == 2,
			      "mixed radio state changed");
		}
		for (int i = 3; i < 5; ++i) {
			const auto& field = e[i + 1];
			check(field.name == (i == 3 ? "combo" : "list") &&
			          field.flags == ((i == 3 ? 131072 : 0) |
			                          (sortedChoiceOnly ? 524288 : 0)) &&
			          field.value == (completed <= i ? "old"
			                          : i == 4 && listSelection < 0
			                              ? ""
			                              : "caf\u00e9") &&
			          field.options && field.options->size() == 3,
			      "mixed choice value changed");
			for (int j = 0; j < 3; ++j) {
				const auto& option = (*field.options)[j];
				check(
				    option.index == j &&
				        option.label == (sortedChoiceOnly
				                             ? (j == 0 ? "Zulu" : "Alpha")
				                             : (j == 0 ? "First" : "Same")) &&
				        option.selected ==
				            (j == (completed > i ? (i == 3 ? 2 : listSelection)
				                                 : 0)),
				    "mixed choice position changed");
			}
		}
		return fields->revision;
	};
	const auto open = [&](int position) {
		const auto fields = read();
		const auto& b = fields->entries[position].displayBounds;
		check(b.has_value(), "mixed field bounds missing");
		const auto& p = v.m_layout->pages()[0];
		v.requestFormAt(
		    {static_cast<float>(p.x + p.width * (b->left + b->right) / 2),
		     static_cast<float>(p.y + p.height * (b->top + b->bottom) / 2)});
	};
	const auto input = [&] {
		check(v.m_noteDialog != nullptr, "mixed field dialog missing");
		const auto control =
		    v.m_noteDialog.Content().as<StackPanel>().Children().GetAt(1);
		check(kind == 0   ? control.try_as<TextBox>() != nullptr
		      : kind == 1 ? control.try_as<CheckBox>() != nullptr
		      : kind == 2 ? control.try_as<RadioButton>() != nullptr
		      : kind == 3 ? control.try_as<ComboBox>() != nullptr
		                  : control.try_as<ListBox>() != nullptr,
		      "mixed field has wrong native control");
		if (kind == 0) {
			check(control.as<TextBox>().IsSpellCheckEnabled() ==
			          !noSpellCheckOnly,
			      "mixed text spell-check flag changed");
		}
		if (kind >= 3) {
			const auto children =
			    v.m_noteDialog.Content().as<StackPanel>().Children();
			check(children.Size() == (kind == 3 ? 3u : 4u),
			      "mixed choice clear button has wrong field type");
			if (kind == 4) {
				const auto clear = children.GetAt(3).as<Button>();
				check(clear.Name() == L"ClearFieldSelection" &&
				          unbox_value<hstring>(clear.Content()) ==
				              L"Clear selection" &&
				          clear.IsEnabled() == !v.m_noteBusy,
				      "mixed list clear button has wrong state");
			}
			const auto items = control.as<Primitives::Selector>().Items();
			check(items.Size() == 3, "mixed native option count changed");
			for (uint32_t i = 0; i < 3; ++i) {
				const auto item = items.GetAt(i).as<ContentControl>();
				check(kind == 3 ? item.try_as<ComboBoxItem>() != nullptr
				                : item.try_as<ListBoxItem>() != nullptr,
				      "mixed native option container changed");
				check(unbox_value<hstring>(item.Content()) ==
				          (sortedChoiceOnly ? (i == 0 ? L"Zulu" : L"Alpha")
				                            : (i == 0 ? L"First" : L"Same")),
				      "mixed native option order changed");
			}
			check(items.GetAt(1) != items.GetAt(2),
			      "mixed duplicate labels share an option container");
		}
		return control;
	};
	const auto value = [&]() -> hstring {
		const auto control = input();
		if (kind == 0) {
			const auto text = control.as<TextBox>().Text();
			return multilineOnly ? hstring(paragraphs(text)) : text;
		}
		if (kind < 3) {
			return unbox_value_or<bool>(
			           control.as<Primitives::ToggleButton>().IsChecked(),
			           false)
			           ? L"on"
			           : L"off";
		}
		return to_hstring(control.as<Primitives::Selector>().SelectedIndex());
	};
	const auto draft = [&] {
		const auto control = input();
		if (kind == 0) {
			control.as<TextBox>().Text(textDraft);
		} else if (kind < 3) {
			const auto peer =
			    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
			        control.as<FrameworkElement>());
			if (kind == 1) {
				peer.GetPattern(Peers::PatternInterface::Toggle)
				    .as<Provider::IToggleProvider>()
				    .Toggle();
			} else {
				peer.GetPattern(Peers::PatternInterface::SelectionItem)
				    .as<Provider::ISelectionItemProvider>()
				    .Select();
			}
		} else if (kind == 4 && clearListOnly) {
			const auto clear = v.m_noteDialog.Content()
			                       .as<StackPanel>()
			                       .Children()
			                       .GetAt(3)
			                       .as<Button>();
			Peers::FrameworkElementAutomationPeer::CreatePeerForElement(clear)
			    .GetPattern(Peers::PatternInterface::Invoke)
			    .as<Provider::IInvokeProvider>()
			    .Invoke();
			check(value() == L"-1" && v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "mixed list clear did not enable an empty draft");
		} else {
			control.as<Primitives::Selector>().SelectedIndex(kind == 3 ? 2 : 1);
		}
	};
	const auto findButton = [&](const wchar_t* name) {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& node) -> Button {
			if (const auto item = node.try_as<Button>();
			    item && item.Name() == name) {
				return item;
			}
			for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node);
			     ++i) {
				if (const auto item =
				        find(Media::VisualTreeHelper::GetChild(node, i))) {
					return item;
				}
			}
			return nullptr;
		};
		return find(v.m_noteDialog);
	};
	const auto button = [&](const wchar_t* name) {
		const auto item = findButton(name);
		check(item != nullptr, "mixed dialog button missing");
		check(item.IsEnabled(), "mixed dialog button disabled");
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto modal = [&] {
		const auto before = value();
		const auto tab = owner.Tabs().SelectedIndex();
		const auto count = owner.Tabs().TabItems().Size();
		const auto zoom = v.m_zoom;
		const auto rotation = v.m_rotation;
		const auto page = v.m_current;
		constexpr auto ctrl = helpers::SHORTCUT_CTRL;
		check(
		    owner.routeKey(VK_TAB, ctrl, false) &&
		        owner.routeKey(VK_TAB, ctrl | helpers::SHORTCUT_SHIFT, false) &&
		        !owner.routeKey('W', ctrl, false) &&
		        !owner.routeKey('F', ctrl, false) &&
		        !owner.routeKey(VK_ADD, ctrl, false) &&
		        !owner.routeKey('R', 0, false) &&
		        !owner.routeKey('S', ctrl, false) &&
		        !owner.routeKey(VK_F11, 0, false),
		    "mixed modal reader key escaped");
		check(owner.Viewer() == current &&
		          owner.Tabs().SelectedIndex() == tab &&
		          owner.Tabs().TabItems().Size() == count && count >= 2 &&
		          value() == before && v.m_zoom == zoom &&
		          v.m_rotation == rotation && v.m_current == page &&
		          !v.findOpen() && !owner.m_saveCopyCancel &&
		          !owner.m_notesSaving,
		      "mixed modal keys changed the draft or reader");
	};
	if (v.m_noteDialog &&
	    (!findButton(L"PrimaryButton") || !findButton(L"CloseButton"))) {
		return;
	}
	if ((multilineOnly || combOnly || inheritedOnly || noSpellCheckOnly) &&
	    boundedTextStage < 4) {
		switch (boundedTextStage) {
		case 0:
			mixedRevision = verify(0);
			open(0);
			break;
		case 1: {
			if (!v.m_noteDialog) {
				return;
			}
			const auto text = input().as<TextBox>();
			check(!multilineOnly
			          ? !text.AcceptsReturn() &&
			                value() == (combOnly ? L"AB" : L"initial")
			          : text.AcceptsReturn() &&
			                text.TextWrapping() == TextWrapping::Wrap &&
			                text.MinHeight() >= 100 &&
			                text.MaxHeight() <= 180 &&
			                value() == L"first\nsecond",
			      "bounded native text editor has wrong value or behavior");
			text.Text(overlong);
			modal();
			button(L"PrimaryButton");
			break;
		}
		case 2: {
			const auto text = input().as<TextBox>();
			const auto error = v.m_noteDialog.Content()
			                       .as<StackPanel>()
			                       .Children()
			                       .GetAt(2)
			                       .as<TextBlock>();
			check(!text.IsReadOnly() &&
			          v.m_noteDialog.IsPrimaryButtonEnabled() &&
			          !error.Text().empty() && value() == overlong &&
			          verify(0) == mixedRevision && !v.m_formsDirty &&
			          !v.m_notesDirty,
			      "bounded text refusal lost the draft or changed live "
			      "state");
			draft();
			modal();
			button(L"CloseButton");
			break;
		}
		case 3:
			if (v.m_noteDialog) {
				return;
			}
			check(verify(0) == mixedRevision && !v.m_formsDirty,
			      "bounded text canceled retry changed the document");
			break;
		}
		++boundedTextStage;
		ticks = 0;
		return;
	}
	if (inheritedCheckBoxOnly && step == 9) {
		const auto refused = [&](hstring expected) {
			check(v.m_noteDialog != nullptr,
			      "inherited checkbox refusal closed the editor");
			const auto error = v.m_noteDialog.Content()
			                       .as<StackPanel>()
			                       .Children()
			                       .GetAt(2)
			                       .as<TextBlock>();
			check(value() == expected && input().as<Control>().IsEnabled() &&
			          v.m_noteDialog.IsPrimaryButtonEnabled() &&
			          !error.Text().empty() && verify(1) == mixedRevision &&
			          v.m_formsDirty && v.m_notesDirty,
			      "inherited checkbox refusal lost draft or changed state");
		};
		switch (inheritedCheckBoxStage) {
		case 0:
			mixedRevision = verify(1);
			open(1);
			break;
		case 1:
			if (!v.m_noteDialog) {
				return;
			}
			check(value() == L"off", "inherited checkbox opened checked");
			modal();
			button(L"PrimaryButton");
			break;
		case 2:
			refused(L"off");
			draft();
			modal();
			button(L"PrimaryButton");
			break;
		case 3:
			refused(L"on");
			button(L"CloseButton");
			break;
		case 4:
			if (v.m_noteDialog) {
				return;
			}
			check(verify(1) == mixedRevision && v.m_formsDirty &&
			          v.m_notesDirty,
			      "inherited checkbox Cancel changed state");
			open(1);
			break;
		case 5:
			if (!v.m_noteDialog) {
				return;
			}
			check(value() == L"off", "inherited checkbox kept refused draft");
			modal();
			button(L"CloseButton");
			break;
		case 6:
			if (v.m_noteDialog) {
				return;
			}
			check(verify(1) == mixedRevision && v.m_formsDirty &&
			          v.m_notesDirty,
			      "inherited checkbox reopen changed state");
			step = 18;
			break;
		}
		++inheritedCheckBoxStage;
		ticks = 0;
		return;
	}
	if (defaultListOnly && step == 36) {
		const auto apply = [&] {
			button(L"PrimaryButton");
			check(v.m_noteBusy && !v.m_noteDialog.IsPrimaryButtonEnabled() &&
			          !input().as<Control>().IsEnabled(),
			      "default list queued draft stayed editable");
		};
		const auto refused = [&] {
			check(v.m_noteDialog != nullptr,
			      "default list refusal closed the editor");
			const auto error = v.m_noteDialog.Content()
			                       .as<StackPanel>()
			                       .Children()
			                       .GetAt(2)
			                       .as<TextBlock>();
			check(value() == L"-1" && input().as<Control>().IsEnabled() &&
			          v.m_noteDialog.IsPrimaryButtonEnabled() &&
			          !error.Text().empty() && verify(4) == mixedRevision &&
			          v.m_formsDirty && v.m_notesDirty,
			      "default list refusal lost draft or changed live state");
		};
		switch (defaultListStage) {
		case 0:
			mixedRevision = verify(4);
			open(5);
			break;
		case 1:
		case 4:
			if (!v.m_noteDialog) {
				return;
			}
			check(value() == L"0", "default list lost its saved selection");
			draft();
			modal();
			apply();
			break;
		case 2:
			refused();
			button(L"CloseButton");
			break;
		case 3:
			if (v.m_noteDialog) {
				return;
			}
			check(verify(4) == mixedRevision,
			      "default list Cancel changed the document");
			open(5);
			break;
		case 5:
			refused();
			input().as<Primitives::Selector>().SelectedIndex(1);
			modal();
			apply();
			break;
		case 6:
			if (v.m_noteDialog) {
				return;
			}
			check(verify(5) > mixedRevision && v.m_formsDirty && v.m_notesDirty,
			      "default list selection retry lost the edit");
			step = 45;
			break;
		}
		++defaultListStage;
		ticks = 0;
		return;
	}
	if (step < 45) {
		switch (step % 9) {
		case 0:
			mixedRevision = verify(kind);
			open(kind == 2 ? 2 : positions[kind]);
			break;
		case 1:
			if (!v.m_noteDialog) {
				return;
			}
			modal();
			button(L"PrimaryButton");
			break;
		case 2:
			if (v.m_noteDialog) {
				return;
			}
			check(verify(kind) == mixedRevision && v.m_formsDirty == (kind > 0),
			      "mixed no-op changed the revision or savepoint");
			open(positions[kind]);
			break;
		case 3:
		case 6:
			if (!v.m_noteDialog) {
				return;
			}
			draft();
			modal();
			break;
		case 4:
			button(L"CloseButton");
			break;
		case 5:
			if (v.m_noteDialog) {
				return;
			}
			check(verify(kind) == mixedRevision && v.m_formsDirty == (kind > 0),
			      "mixed Cancel changed the document");
			open(positions[kind]);
			break;
		case 7:
			button(L"PrimaryButton");
			check(v.m_noteBusy && !v.m_noteDialog.IsPrimaryButtonEnabled() &&
			          (kind == 0 ? input().as<TextBox>().IsReadOnly()
			                     : !input().as<Control>().IsEnabled()),
			      "mixed queued input stayed editable");
			break;
		case 8:
			if (v.m_noteDialog) {
				return;
			}
			check(verify(kind + 1) > mixedRevision && v.m_formsDirty &&
			          v.m_notesDirty,
			      "mixed Apply lost an edit or dirty state");
			break;
		}
	} else if (step == 45) {
		notesPath = buttonFieldPath.parent_path() / "mixed copy.pdf";
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
	} else if (step == 46) {
		check(notesSave.GetResults() && !v.m_formsDirty && !v.m_notesDirty,
		      "mixed save did not clear the savepoint");
		owner.openDocument(engine, notesPath, 1);
	} else if (step < 57) {
		check(v.m_path == notesPath, "mixed saved copy did not reopen");
		if ((step - 47) % 2 == 0) {
			if (v.m_noteDialog) {
				return;
			}
			const auto revision = verify(5);
			if (step == 47) {
				mixedRevision = revision;
			}
			check(revision == mixedRevision && !v.m_formsDirty &&
			          !v.m_notesDirty,
			      "mixed reopened no-op dirtied the copy");
			open(positions[kind]);
		} else {
			if (!v.m_noteDialog) {
				return;
			}
			check(value() ==
			          (kind == 0 ? textDraft
			           : kind < 3
			               ? hstring(kind == 1 && inheritedCheckBoxOnly ? L"off"
			                                                            : L"on")
			               : to_hstring(kind == 3 ? 2 : listSelection)),
			      "mixed reopened editor lost its saved value");
			modal();
			button(kind == 1 && inheritedCheckBoxOnly ? L"CloseButton"
			                                          : L"PrimaryButton");
		}
	} else {
		if (v.m_noteDialog) {
			return;
		}
		check(verify(5) == mixedRevision && !v.m_formsDirty && !v.m_notesDirty,
		      "mixed final no-op changed the saved copy");
		check(engine
		          .run([path = notesPath] {
			          const auto saved = pdf::openFile(path);
			          if (!saved.document) {
				          return false;
			          }
			          const auto note = saved.document->textNote(0, 0);
			          const auto outline = saved.document->outline();
			          const auto link = saved.document->outlineLink(0);
			          return note == u"Original" && outline &&
			                 outline->entries.size() == 1 &&
			                 outline->entries[0].title == "Fields" && link &&
			                 link->destination && link->destination->page == 0;
		          })
		          .get(),
		      "mixed saved note or bookmark changed");
		std::ifstream original(buttonFieldPath, std::ios::binary);
		check(std::string(std::istreambuf_iterator<char>(original), {}) ==
		          mixedFormPdf({.multiline = multilineOnly,
		                        .comb = combOnly,
		                        .inherited = inheritedOnly,
		                        .inheritedCheckBox = inheritedCheckBoxOnly,
		                        .noSpellCheck = noSpellCheckOnly,
		                        .sortedChoices = sortedChoiceOnly,
		                        .listDefault = defaultListOnly,
		                        .textLimit = combOnly ? 4 : 32}),
		      "mixed source changed");
		std::fwprintf(
		    stderr,
		    L"PASS: mixed native fields, modal keys, no-op, "
		    L"Cancel, Apply, save/reopen and source preservation; %ls\n",
		    notesPath.c_str());
		if (multilineOnly) {
			std::fwprintf(stderr,
			              L"PASS: multiline paragraphs, blank lines, "
			              L"Unicode, raw no-op and retained MaxLen refusal\n");
		}
		if (combOnly) {
			std::fwprintf(stderr,
			              L"PASS: comb text, exact cells, Unicode, no-op "
			              L"and retained overlength refusal\n");
		}
		if (inheritedCheckBoxOnly) {
			std::fwprintf(stderr,
			              L"PASS: inherited checkbox retained unchanged and "
			              L"checked refusals beside saved supported edits\n");
		}
		if (noSpellCheckOnly) {
			std::fwprintf(stderr,
			              L"PASS: no-spell-check text flag and native editor, "
			              L"retained limit refusal and saved identity\n");
		}
		if (sortedChoiceOnly) {
			std::fwprintf(stderr,
			              L"PASS: sorted choice flag keeps native option "
			              L"order and saved positions\n");
		}
		if (clearListOnly) {
			std::fwprintf(stderr, L"%ls",
			              defaultListOnly
			                  ? L"PASS: default list clear refusal retains "
			                    L"draft and selection retry\n"
			                  : L"PASS: cleared list native draft and "
			                    L"saved empty selection\n");
		}
		if (inheritedOnly) {
			std::fwprintf(stderr,
			              L"PASS: inherited text identity, Unicode, no-op "
			              L"and retained parent MaxLen refusal\n");
			if (multilineOnly) {
				std::fwprintf(stderr,
				              L"PASS: inherited multiline native editor and "
				              L"saved paragraph identity\n");
			}
		}
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::choiceFieldTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_noteSaving ||
	    (notesSave &&
	     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
		return;
	}
	using namespace Microsoft::UI::Xaml::Automation;
	const auto button = [&](const wchar_t* name) {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& node) -> Button {
			if (const auto item = node.try_as<Button>();
			    item && item.Name() == name) {
				return item;
			}
			for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node);
			     ++i) {
				if (const auto item =
				        find(Media::VisualTreeHelper::GetChild(node, i))) {
					return item;
				}
			}
			return nullptr;
		};
		const auto item = find(v.m_noteDialog);
		check(item != nullptr, "choice dialog button missing");
		check(item.IsEnabled(), "choice dialog button disabled");
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto read = [&] {
		const auto reply =
		    std::make_shared<std::promise<pdf::FormFieldsResult>>();
		auto future = reply->get_future();
		check(v.m_session->formFields(0,
		                              [reply](auto result) {
			                              reply->set_value(std::move(result));
		                              }),
		      "choice snapshot not queued");
		check(future.wait_for(std::chrono::seconds(10)) ==
		          std::future_status::ready,
		      "choice snapshot timed out");
		const auto result = future.get();
		check(result.fields && !result.fields->limited &&
		          result.fields->entries.size() == 1,
		      "choice snapshot failed");
		return result.fields;
	};
	const auto open = [&] {
		const auto field = read()->entries[0];
		check(field.displayBounds.has_value(), "choice bounds missing");
		const auto& b = *field.displayBounds;
		const auto& p = v.m_layout->pages()[0];
		v.requestFormAt(
		    {static_cast<float>(p.x + p.width * (b.left + b.right) / 2),
		     static_cast<float>(p.y + p.height * (b.top + b.bottom) / 2)});
	};
	const auto options = [&] {
		check(v.m_noteDialog != nullptr, "choice dialog missing");
		const auto control = v.m_noteDialog.Content()
		                         .as<StackPanel>()
		                         .Children()
		                         .GetAt(1)
		                         .as<Primitives::Selector>();
		check(choiceList ? control.try_as<ListBox>() != nullptr
		                 : control.try_as<ComboBox>() != nullptr,
		      "wrong native choice control");
		check(AutomationProperties::GetName(control) == L"Field option",
		      "choice automation name missing");
		return control;
	};
	const auto selected = [list = choiceList](const pdf::FormFields& fields) {
		if (fields.entries.size() != 1) {
			return false;
		}
		const auto& field = fields.entries[0];
		return field.value == "caf\u00e9" && field.name == "choice" &&
		       field.type == (list ? pdf::FormFieldType::ListBox
		                           : pdf::FormFieldType::ComboBox) &&
		       field.options && field.options->size() == 3 &&
		       (*field.options)[0].index == 0 &&
		       (*field.options)[0].label == "First" &&
		       !(*field.options)[0].selected &&
		       (*field.options)[1].index == 1 &&
		       (*field.options)[1].label == "Same" &&
		       !(*field.options)[1].selected &&
		       (*field.options)[2].index == 2 &&
		       (*field.options)[2].label == "Same" &&
		       (*field.options)[2].selected;
	};
	switch (step) {
	case 0:
		open();
		break;
	case 1:
		if (!v.m_noteDialog) {
			return;
		}
		check(options().SelectedIndex() == 0 && options().Items().Size() == 3,
		      "choice initial state changed");
		for (uint32_t i = 0; i < 3; ++i) {
			const auto item = options().Items().GetAt(i).as<ContentControl>();
			check(choiceList ? item.try_as<ListBoxItem>() != nullptr
			                 : item.try_as<ComboBoxItem>() != nullptr,
			      "choice container type changed");
			check(unbox_value<hstring>(item.Content()) ==
			          (i == 0 ? L"First" : L"Same"),
			      "choice labels changed");
		}
		check(options().Items().GetAt(1) != options().Items().GetAt(2),
		      "duplicate labels share an automation item");
		options().SelectedIndex(-1);
		check(!v.m_noteDialog.IsPrimaryButtonEnabled(),
		      "empty selection allowed Apply");
		options().SelectedIndex(0);
		if (!choiceList) {
			options().as<ComboBox>().IsDropDownOpen(true);
		}
		break;
	case 2:
		if (!choiceList) {
			const auto combo = options().as<ComboBox>();
			check(combo.IsDropDownOpen(), "choice popup did not open");
			combo.IsDropDownOpen(false);
		}
		break;
	case 3:
		check(v.m_noteDialog && options().SelectedIndex() == 0 &&
		          v.m_noteDialog.IsPrimaryButtonEnabled(),
		      "restored choice did not enable Apply");
		check(choiceList || !options().as<ComboBox>().IsDropDownOpen(),
		      "choice popup did not close");
		button(L"PrimaryButton");
		break;
	case 4:
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty && !v.m_formsDirty, "choice no-op dirtied PDF");
		open();
		break;
	case 5:
		if (!v.m_noteDialog) {
			return;
		}
		options().SelectedIndex(2);
		button(L"PrimaryButton");
		check(v.m_noteBusy && !options().IsEnabled() &&
		          !v.m_noteDialog.IsPrimaryButtonEnabled(),
		      "queued choice stayed enabled");
		break;
	case 6:
		if (v.m_noteDialog) {
			return;
		}
		check(v.m_notesDirty && v.m_formsDirty && selected(*read()),
		      "choice Apply failed");
		notesPath = buttonFieldPath.parent_path() / "selected copy.pdf";
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		break;
	case 7:
		check(notesSave.GetResults() && !v.m_notesDirty && !v.m_formsDirty,
		      "choice save did not clear dirty state");
		check(engine
		          .run([path = notesPath, selected] {
			          const auto saved = pdf::openFile(path);
			          const auto fields = saved.document
			                                  ? saved.document->formFields(0)
			                                  : std::nullopt;
			          return fields && selected(*fields);
		          })
		          .get(),
		      "saved choice value/position changed");
		open();
		break;
	case 8:
		if (!v.m_noteDialog) {
			return;
		}
		check(options().SelectedIndex() == 2,
		      "saved choice opened incorrectly");
		options().SelectedIndex(0);
		button(L"CloseButton");
		break;
	case 9: {
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty && !v.m_formsDirty && selected(*read()),
		      "Cancel changed choice field");
		const auto snapshot = read();
		auto field = snapshot->entries[0];
		field.name = "wrong";
		v.editFormField(0, snapshot->revision, std::move(field));
		break;
	}
	case 10:
		if (!v.m_noteDialog) {
			return;
		}
		options().SelectedIndex(1);
		button(L"PrimaryButton");
		break;
	case 11:
		check(v.m_noteDialog && options().IsEnabled() &&
		          options().SelectedIndex() == 1 &&
		          v.m_noteDialog.IsPrimaryButtonEnabled() &&
		          !v.m_noteDialog.Content()
		               .as<StackPanel>()
		               .Children()
		               .GetAt(2)
		               .as<TextBlock>()
		               .Text()
		               .empty(),
		      "refused choice draft lost or disabled");
		button(L"CloseButton");
		break;
	case 12: {
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty && !v.m_formsDirty && selected(*read()),
		      "refused choice changed PDF");
		const auto snapshot = read();
		auto field = snapshot->entries[0];
		check(field.options.has_value(), "unset choice options missing");
		field.value.clear();
		for (auto& option : *field.options) {
			option.selected = false;
		}
		v.editFormField(0, snapshot->revision, std::move(field));
		break;
	}
	case 13:
		if (!v.m_noteDialog) {
			return;
		}
		check(options().SelectedIndex() == -1 &&
		          !v.m_noteDialog.IsPrimaryButtonEnabled(),
		      "unset choice opened with Apply enabled");
		options().SelectedIndex(2);
		check(v.m_noteDialog.IsPrimaryButtonEnabled(),
		      "unset choice did not enable Apply after selection");
		button(L"CloseButton");
		break;
	case 14: {
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty && !v.m_formsDirty && selected(*read()),
		      "unset choice Cancel changed PDF");
		std::ifstream original(buttonFieldPath, std::ios::binary);
		check(std::string(std::istreambuf_iterator<char>(original), {}) ==
		          choicePdf(!choiceList),
		      "choice source changed");
		std::fwprintf(stderr,
		              L"PASS: %ls choice %lsno-op, selection, Apply, "
		              L"save/reopen, Cancel, refusal and unset draft; %ls\n",
		              choiceList ? L"list" : L"combo",
		              choiceList ? L"" : L"popup cycle, ", notesPath.c_str());
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	}
	++step;
	ticks = 0;
}

void TabsTest::buttonFieldTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_noteSaving ||
	    (notesSave &&
	     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
		return;
	}
	using namespace Microsoft::UI::Xaml::Automation;
	const auto peer = [](const FrameworkElement& element) {
		return Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
		    element);
	};
	const auto button = [&](const wchar_t* name) {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& node) -> Button {
			if (const auto item = node.try_as<Button>();
			    item && item.Name() == name) {
				return item;
			}
			for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node);
			     ++i) {
				if (const auto item =
				        find(Media::VisualTreeHelper::GetChild(node, i))) {
					return item;
				}
			}
			return nullptr;
		};
		const auto item = find(v.m_noteDialog);
		check(item && item.IsEnabled(),
		      "button field dialog button unavailable");
		peer(item)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto read = [&] {
		const auto reply =
		    std::make_shared<std::promise<pdf::FormFieldsResult>>();
		auto future = reply->get_future();
		check(
		    v.m_session->formFields(
		        0, [reply](auto value) { reply->set_value(std::move(value)); }),
		    "button field snapshot not queued");
		check(future.wait_for(std::chrono::seconds(10)) ==
		          std::future_status::ready,
		      "button field snapshot timed out");
		const auto result = future.get();
		check(result.fields && !result.fields->limited &&
		          result.fields->entries.size() == (radioOnly ? 2u : 1u),
		      "button field snapshot failed");
		return result.fields;
	};
	const auto open = [&](int index = 0) {
		const auto snapshot = read();
		const auto& b = snapshot->entries[index].displayBounds;
		check(b.has_value(), "button field has no visible bounds");
		const auto& p = v.m_layout->pages()[0];
		v.requestFormAt(
		    {static_cast<float>(p.x + p.width * (b->left + b->right) / 2),
		     static_cast<float>(p.y + p.height * (b->top + b->bottom) / 2)});
	};
	const auto choice = [&] {
		check(v.m_noteDialog != nullptr, "button field dialog missing");
		const auto control = v.m_noteDialog.Content()
		                         .as<StackPanel>()
		                         .Children()
		                         .GetAt(1)
		                         .as<Primitives::ToggleButton>();
		check(radioOnly ? control.try_as<RadioButton>() != nullptr
		                : control.try_as<CheckBox>() != nullptr,
		      "wrong native button field control");
		return control;
	};
	const auto select = [&] {
		if (radioOnly) {
			peer(choice())
			    .GetPattern(Peers::PatternInterface::SelectionItem)
			    .as<Provider::ISelectionItemProvider>()
			    .Select();
		} else {
			peer(choice())
			    .GetPattern(Peers::PatternInterface::Toggle)
			    .as<Provider::IToggleProvider>()
			    .Toggle();
		}
	};
	switch (step) {
	case 0:
		open();
		break;
	case 1:
		if (!v.m_noteDialog) {
			return;
		}
		check(unbox_value_or<bool>(choice().IsChecked(), !radioOnly) ==
		          radioOnly,
		      "button field snapshot state changed");
		button(L"PrimaryButton");
		break;
	case 2:
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty && !v.m_formsDirty,
		      "button field no-op dirtied PDF");
		open(radioOnly ? 1 : 0);
		break;
	case 3:
		if (!v.m_noteDialog) {
			return;
		}
		if (radioOnly) {
			check(!v.m_noteDialog.IsPrimaryButtonEnabled() &&
			          !unbox_value_or<bool>(choice().IsChecked(), true),
			      "unselected radio option allowed Apply");
		}
		select();
		check(unbox_value_or<bool>(choice().IsChecked(), false),
		      "native button field did not select");
		button(L"PrimaryButton");
		check(v.m_noteBusy && !choice().IsEnabled() &&
		          !v.m_noteDialog.IsPrimaryButtonEnabled(),
		      "queued button field stayed enabled");
		break;
	case 4:
		if (v.m_noteDialog) {
			return;
		}
		check(v.m_notesDirty && v.m_formsDirty &&
		          read()->entries[radioOnly ? 1 : 0].checked == true &&
		          (!radioOnly || read()->entries[0].checked == false),
		      "button field Apply failed");
		notesPath = buttonFieldPath.parent_path() / "selected copy.pdf";
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		break;
	case 5:
		check(notesSave.GetResults() && !v.m_notesDirty && !v.m_formsDirty,
		      "button field save did not clear dirty state");
		check(engine
		          .run([path = notesPath, radio = radioOnly] {
			          const auto saved = pdf::openFile(path);
			          const auto fields = saved.document
			                                  ? saved.document->formFields(0)
			                                  : std::nullopt;
			          return fields &&
			                 fields->entries.size() == (radio ? 2u : 1u) &&
			                 fields->entries[radio ? 1 : 0].checked == true &&
			                 (!radio || fields->entries[0].checked == false) &&
			                 fields->entries[0].value ==
			                     (radio ? "Next choice" : "Approved now");
		          })
		          .get(),
		      "saved button field export/state missing");
		open();
		break;
	case 6:
		if (!v.m_noteDialog) {
			return;
		}
		check(unbox_value_or<bool>(choice().IsChecked(), radioOnly) !=
		          radioOnly,
		      "saved button field opened with wrong state");
		select();
		check(unbox_value_or<bool>(choice().IsChecked(), !radioOnly) ==
		          radioOnly,
		      "Cancel draft did not change state");
		button(L"CloseButton");
		break;
	case 7: {
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty && !v.m_formsDirty &&
		          read()->entries[radioOnly ? 1 : 0].checked == true &&
		          (!radioOnly || read()->entries[0].checked == false),
		      "Cancel changed button field");
		if (radioOnly) {
			const auto snapshot = read();
			auto field = snapshot->entries[1];
			check(field.control.has_value(), "radio control metadata missing");
			++field.control->count;
			v.editFormField(0, snapshot->revision, std::move(field));
			break;
		}
		[[fallthrough]];
	}
	case 10: {
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty && !v.m_formsDirty,
		      "refused button field dirtied PDF");
		std::ifstream original(buttonFieldPath, std::ios::binary);
		check(std::string(std::istreambuf_iterator<char>(original), {}) ==
		          (radioOnly ? radioPdf() : checkBoxPdf()),
		      "button field source changed");
		std::fwprintf(stderr,
		              L"PASS: %ls no-op, native selection, Apply, "
		              L"saved export/state and Cancel; %ls\n",
		              radioOnly ? L"radio/refusal" : L"checkbox",
		              notesPath.c_str());
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	case 8:
		if (!v.m_noteDialog) {
			return;
		}
		button(L"PrimaryButton");
		break;
	case 9:
		check(v.m_noteDialog && choice().IsEnabled() &&
		          unbox_value_or<bool>(choice().IsChecked(), false) &&
		          v.m_noteDialog.IsPrimaryButtonEnabled() &&
		          !v.m_noteDialog.Content()
		               .as<StackPanel>()
		               .Children()
		               .GetAt(2)
		               .as<TextBlock>()
		               .Text()
		               .empty(),
		      "refused radio choice was lost or disabled");
		button(L"CloseButton");
		break;
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
