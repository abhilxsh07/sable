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
#include "SettingsWindow.h"
#include "helpers/version.h"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Windowing;

namespace {
// see:
// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-tounicodeex
std::string captureKey(unsigned key) {
	const std::pair<unsigned, const char*> names[] = {
	    {VK_CONTROL, "Control"}, {VK_MENU, "Alt"},
	    {VK_SHIFT, "Shift"},     {VK_LWIN, "Meta"},
	    {VK_RWIN, "Meta"},       {VK_TAB, "Tab"},
	    {VK_PRIOR, "PageUp"},    {VK_NEXT, "PageDown"},
	    {VK_HOME, "Home"},       {VK_END, "End"},
	    {VK_RETURN, "Enter"},    {VK_BACK, "Backspace"},
	    {VK_DELETE, "Delete"},   {VK_INSERT, "Insert"},
	    {VK_LEFT, "ArrowLeft"},  {VK_RIGHT, "ArrowRight"},
	    {VK_UP, "ArrowUp"},      {VK_DOWN, "ArrowDown"}};
	for (const auto& [code, name] : names) {
		if (key == code) {
			return name;
		}
	}
	if (key >= VK_F1 && key <= VK_F24) {
		return "F" + std::to_string(key - VK_F1 + 1);
	}
	BYTE state[256]{};
	GetKeyboardState(state);
	// Ctrl/Alt would turn letters into control characters. Keep Shift/Caps.
	for (auto code :
	     {VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU}) {
		state[code] = 0;
	}
	wchar_t text[8]{};
	const auto layout = GetKeyboardLayout(0);
	const int count =
	    ToUnicodeEx(key, MapVirtualKeyExW(key, MAPVK_VK_TO_VSC, layout), state,
	                text, 8, 4, layout);
	return count == 1 && text[0] >= L' ' ? to_string(hstring(text, 1)) : "";
}

HWND windowHandle(const Window& window) {
	HWND hwnd{};
	check_hresult(window.as<::IWindowNative>()->get_WindowHandle(&hwnd));
	return hwnd;
}
} // namespace

std::shared_ptr<SettingsWindow>
SettingsWindow::open(const Window& owner, helpers::SettingsStore& store,
                     const std::filesystem::path& icon) {
	auto self = std::shared_ptr<SettingsWindow>(new SettingsWindow(store));
	try {
		self->initialize(owner, icon);
	} catch (...) {
		if (self->window) {
			self->window.Close();
		}
		throw;
	}
	return self;
}

// port of app/render/settings.ts nightPDFSettings() (L34-136)
void SettingsWindow::initialize(const Window& owner,
                                const std::filesystem::path& icon) {
	window = Window();
	window.Title(L"Settings - Sable");
	window.AppWindow().SetIcon(icon.wstring());
	auto titleBar = window.AppWindow().TitleBar();
	const Windows::UI::Color background{255, 0x1e, 0x1e, 0x1e};
	const Windows::UI::Color foreground{255, 0xcc, 0xcc, 0xcc};
	titleBar.BackgroundColor(background);
	titleBar.InactiveBackgroundColor(background);
	titleBar.ForegroundColor(foreground);
	titleBar.InactiveForegroundColor(foreground);
	titleBar.ButtonBackgroundColor(background);
	titleBar.ButtonInactiveBackgroundColor(background);
	titleBar.ButtonForegroundColor(foreground);
	titleBar.ButtonInactiveForegroundColor(foreground);
	const auto weak = weak_from_this();
	const Media::SolidColorBrush text({255, 0xcc, 0xcc, 0xcc});
	Grid root;
	root.Background(Media::SolidColorBrush({255, 0x1e, 0x1e, 0x1e}));
	root.Padding({32, 16, 32, 16});
	ColumnDefinition left;
	left.Width({144, GridUnitType::Pixel});
	root.ColumnDefinitions().Append(left);
	root.ColumnDefinitions().Append(ColumnDefinition());
	StackPanel menu;
	menu.Margin({0, 0, 48, 0});
	const helpers::Json settings = m_store.get();
	for (auto it = settings.begin(); it != settings.end(); ++it) {
		Button item;
		const auto section = it.key();
		item.Content(box_value(to_hstring(section)));
		item.Tag(box_value(to_hstring(section)));
		item.FontSize(16);
		item.HorizontalAlignment(HorizontalAlignment::Stretch);
		item.HorizontalContentAlignment(HorizontalAlignment::Right);
		item.BorderThickness({0, 1, 1, 0});
		item.BorderBrush(text);
		item.Foreground(text);
		item.Background(Media::SolidColorBrush({255, 0, 0, 0}));
		item.Margin({0, 8, 0, 8});
		item.Click([weak, section](auto&&, auto&&) {
			if (auto self = weak.lock()) {
				self->select(section);
			}
		});
		menu.Children().Append(item);
		m_sections.push_back(item);
	}
	root.Children().Append(menu);
	Grid content;
	Grid::SetColumn(content, 1);
	content.RowDefinitions().Append(RowDefinition());
	RowDefinition errorRow;
	errorRow.Height({1, GridUnitType::Auto});
	content.RowDefinitions().Append(errorRow);
	m_version.Text(to_hstring(helpers::versionString()));
	m_version.TextWrapping(TextWrapping::Wrap);
	m_version.Foreground(text);
	m_version.FontSize(16);
	m_version.Margin({0, 16, 0, 0});
	content.Children().Append(m_version);
	m_general.Width(312);
	m_general.HorizontalAlignment(HorizontalAlignment::Left);
	const char* keys[] = {"MaximizeOnOpen", "DisplayThumbs"};
	const wchar_t* labels[] = {L"Maximize On Open",
	                           L"Display PDF page thumbnails"};
	for (std::size_t i = 0; i < m_checks.size(); ++i) {
		Grid row;
		row.Margin({0, 16, 0, 0});
		ColumnDefinition labelColumn;
		labelColumn.Width({128, GridUnitType::Pixel});
		row.ColumnDefinitions().Append(labelColumn);
		row.ColumnDefinitions().Append(ColumnDefinition());
		TextBlock label;
		label.Text(labels[i]);
		label.Foreground(text);
		label.FontSize(16);
		label.TextWrapping(TextWrapping::Wrap);
		label.VerticalAlignment(VerticalAlignment::Center);
		row.Children().Append(label);
		auto box = m_checks[i];
		box.MinWidth(0);
		box.Width(32);
		box.Tag(box_value(to_hstring(keys[i])));
		box.IsChecked(i == 0 ? m_store.get().general.MaximizeOnOpen
		                     : m_store.get().general.DisplayThumbs);
		Automation::AutomationProperties::SetName(box, labels[i]);
		box.HorizontalAlignment(HorizontalAlignment::Right);
		Grid::SetColumn(box, 1);
		box.Click([weak](const auto& sender, auto&&) {
			if (auto self = weak.lock()) {
				self->save(sender.template as<CheckBox>());
			}
		});
		row.Children().Append(box);
		m_general.Children().Append(row);
	}
	content.Children().Append(m_general);
	m_bindingScroll.Content(m_keybinds);
	m_bindingScroll.HorizontalScrollBarVisibility(
	    ScrollBarVisibility::Disabled);
	content.Children().Append(m_bindingScroll);
	buildKeybinds();
	Grid::SetRow(m_error, 1);
	content.Children().Append(m_error);
	root.Children().Append(content);
	Grid::SetColumnSpan(m_overlay, 2);
	m_overlay.Background(Media::SolidColorBrush({180, 0, 0, 0}));
	m_overlay.Visibility(Visibility::Collapsed);
	window.Activated([weak](auto&&, const WindowActivatedEventArgs& e) {
		if (e.WindowActivationState() == WindowActivationState::Deactivated) {
			if (auto self = weak.lock()) {
				self->dismiss();
			}
		}
	});
	Border card;
	card.Background(Media::SolidColorBrush({255, 0x1e, 0x1e, 0x1e}));
	card.Padding({24, 24, 24, 24});
	card.HorizontalAlignment(HorizontalAlignment::Center);
	card.VerticalAlignment(VerticalAlignment::Center);
	card.PointerPressed([](auto&&, const Input::PointerRoutedEventArgs& e) {
		e.Handled(true);
	});
	StackPanel capturePanel;
	capturePanel.Spacing(16);
	TextBlock prompt;
	prompt.Text(L"Press a key");
	prompt.FontSize(24);
	capturePanel.Children().Append(prompt);
	m_captureText.FontSize(20);
	m_captureText.TextWrapping(TextWrapping::Wrap);
	m_captureText.MaxWidth(400);
	capturePanel.Children().Append(m_captureText);
	StackPanel buttons;
	buttons.Orientation(Orientation::Horizontal);
	buttons.Spacing(12);
	m_cancelBinding.Content(box_value(L"Cancel"));
	m_cancelBinding.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->dismiss();
		}
	});
	m_saveBinding.Content(box_value(L"Save"));
	m_saveBinding.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			if (self->m_capture.binding().key &&
			    self->saveBinding(self->m_action, self->m_bindingIndex,
			                      self->m_capture.binding())) {
				self->dismiss();
			}
		}
	});
	buttons.Children().Append(m_cancelBinding);
	buttons.Children().Append(m_saveBinding);
	capturePanel.Children().Append(buttons);
	card.Child(capturePanel);
	m_overlay.Child(card);
	root.PointerPressed([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->dismiss();
		}
	});
	root.Children().Append(m_overlay);
	root.PreviewKeyDown([weak](auto&&, const Input::KeyRoutedEventArgs& e) {
		if (auto self = weak.lock()) {
			if (self->capture(static_cast<unsigned>(e.Key()), true,
			                  e.KeyStatus().WasKeyDown)) {
				e.Handled(true);
			}
		}
	});
	root.PreviewKeyUp([weak](auto&&, const Input::KeyRoutedEventArgs& e) {
		if (auto self = weak.lock()) {
			self->capture(static_cast<unsigned>(e.Key()), false);
		}
	});
	window.Content(root);
	select("version");
	// see:
	// https://learn.microsoft.com/en-us/windows/apps/develop/ui/manage-app-windows
	const auto hwnd = windowHandle(window);
	const auto ownerHwnd = windowHandle(owner);
	const bool ownerEnabled = IsWindowEnabled(ownerHwnd) != FALSE;
	SetLastError(0);
	if (!SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT,
	                       reinterpret_cast<LONG_PTR>(ownerHwnd))) {
		check_hresult(HRESULT_FROM_WIN32(GetLastError()));
	}
	auto presenter = window.AppWindow().Presenter().as<OverlappedPresenter>();
	presenter.IsMinimizable(false);
	presenter.IsMaximizable(false);
	presenter.IsModal(true);
	const double scale = GetDpiForWindow(ownerHwnd) / 96.0;
	window.AppWindow().Resize(
	    {static_cast<int32_t>(800 * scale), static_cast<int32_t>(600 * scale)});
	window.Closed([ownerHwnd, ownerEnabled](auto&&, auto&&) {
		if (ownerEnabled && IsWindow(ownerHwnd)) {
			EnableWindow(ownerHwnd, TRUE);
		}
	});
	EnableWindow(ownerHwnd, FALSE);
	window.Activate();
}

void SettingsWindow::select(const std::string& section) {
	for (const auto& item : m_sections) {
		const std::uint8_t shade =
		    unbox_value<hstring>(item.Tag()) == to_hstring(section) ? 0 : 0x1e;
		item.Background(Media::SolidColorBrush({255, shade, shade, shade}));
	}
	m_version.Visibility(section == "version" ? Visibility::Visible
	                                          : Visibility::Collapsed);
	m_general.Visibility(section == "general" ? Visibility::Visible
	                                          : Visibility::Collapsed);
	m_bindingScroll.Visibility(section == "keybinds" ? Visibility::Visible
	                                                 : Visibility::Collapsed);
}

// port of app/render/settings.ts createKeybindAction/Setting() (L375-515)
void SettingsWindow::buildKeybinds() {
	m_keybinds.Children().Clear();
	helpers::KeybindsHelper bindings(m_store.get().keybinds, "win32");
	const auto weak = weak_from_this();
	for (const auto& action : bindings.actions) {
		Grid row;
		row.Margin({0, 8, 0, 8});
		for (int i = 0; i < 3; ++i) {
			row.ColumnDefinitions().Append(ColumnDefinition());
		}
		TextBlock label;
		const auto name = to_hstring(bindings.getActionDisplayName(action));
		label.Text(name);
		label.TextWrapping(TextWrapping::Wrap);
		label.VerticalAlignment(VerticalAlignment::Center);
		row.Children().Append(label);
		const auto keys = bindings.getActionKeybindsString(action);
		for (std::size_t i = 0; i < 2; ++i) {
			StackPanel slot;
			slot.Orientation(Orientation::Horizontal);
			Grid::SetColumn(slot, static_cast<int>(i + 1));
			Button binding;
			binding.Content(
			    box_value(i < keys.size() ? to_hstring(keys[i]) : L""));
			binding.MinWidth(100);
			binding.FontSize(12);
			Automation::AutomationProperties::SetName(
			    binding,
			    name + (i == 0 ? L" primary binding" : L" secondary binding"));
			if (action == m_action && i == m_bindingIndex) {
				m_returnFocus = binding;
			}
			binding.Click([weak, action, i](const auto& sender, auto&&) {
				if (auto self = weak.lock()) {
					self->m_returnFocus = sender.template as<Button>();
					self->edit(action, i);
				}
			});
			slot.Children().Append(binding);
			Button clear;
			clear.Content(box_value(L"X"));
			clear.Padding({4, 4, 4, 4});
			clear.Foreground(Media::SolidColorBrush({255, 0x99, 0x19, 0}));
			clear.Background(Media::SolidColorBrush({0, 0, 0, 0}));
			clear.BorderThickness({0, 0, 0, 0});
			clear.IsEnabled(i < keys.size());
			clear.Opacity(0);
			Automation::AutomationProperties::SetName(
			    clear,
			    L"Clear " + name +
			        (i == 0 ? L" primary binding" : L" secondary binding"));
			clear.Click([weak, action, i](auto&&, auto&&) {
				if (auto self = weak.lock()) {
					self->saveBinding(action, i, std::nullopt);
				}
			});
			row.PointerEntered([clear](auto&&, auto&&) { clear.Opacity(1); });
			row.PointerExited([clear](auto&&, auto&&) { clear.Opacity(0); });
			clear.GotFocus([](const auto& sender, auto&&) {
				sender.template as<Button>().Opacity(1);
			});
			clear.LostFocus([](const auto& sender, auto&&) {
				sender.template as<Button>().Opacity(0);
			});
			slot.Children().Append(clear);
			row.Children().Append(slot);
		}
		m_keybinds.Children().Append(row);
	}
}

void SettingsWindow::edit(const std::string& action, std::size_t index) {
	m_action = action;
	m_bindingIndex = index;
	m_capture.reset();
	m_keyNames = {};
	for (const auto& section : m_sections) {
		section.IsEnabled(false);
	}
	m_bindingScroll.IsEnabled(false);
	m_captureText.Text(L"");
	m_saveBinding.IsEnabled(false);
	m_overlay.Visibility(Visibility::Visible);
	m_cancelBinding.Focus(FocusState::Programmatic);
}

void SettingsWindow::dismiss() {
	if (m_overlay.Visibility() != Visibility::Visible) {
		return;
	}
	m_overlay.Visibility(Visibility::Collapsed);
	m_capture.reset();
	m_keyNames = {};
	for (const auto& section : m_sections) {
		section.IsEnabled(true);
	}
	m_bindingScroll.IsEnabled(true);
	if (m_returnFocus) {
		m_returnFocus.Focus(FocusState::Programmatic);
	}
	m_returnFocus = nullptr;
	m_action.clear();
}

// port of app/render/settings.ts overlay capture (L183-281)
bool SettingsWindow::capture(unsigned key, bool down, bool repeated) {
	if (m_overlay.Visibility() != Visibility::Visible) {
		return false;
	}
	if (key >= m_keyNames.size()) {
		return false;
	}
	if (!down) {
		m_capture.release(m_keyNames[key]);
		m_keyNames[key].clear();
		return false;
	}
	if (key == VK_ESCAPE) {
		dismiss();
		return true;
	}
	const bool modifier =
	    (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
	    (GetKeyState(VK_SHIFT) & 0x8000) || (GetKeyState(VK_LWIN) & 0x8000) ||
	    (GetKeyState(VK_RWIN) & 0x8000);
	if (!modifier && (key == VK_TAB || key == VK_RETURN || key == VK_SPACE)) {
		return false;
	}
	if (!repeated) {
		m_keyNames[key] = captureKey(key);
		if (m_keyNames[key].empty()) {
			m_capture.reset();
		}
		m_capture.press(m_keyNames[key]);
		m_captureText.Text(to_hstring(m_capture.display()));
		m_saveBinding.IsEnabled(m_capture.binding().key.has_value());
	}
	return true;
}

bool SettingsWindow::saveBinding(
    const std::string& action, std::size_t index,
    const std::optional<helpers::Keybind>& binding) {
	try {
		helpers::KeybindsHelper edits(m_store.get().keybinds, "win32");
		// adding to an empty secondary slot still makes the first binding
		index = (std::min)(index, edits.config.at(action).keybind.size());
		const auto command =
		    binding ? edits.updateActionKeybind(action, index, *binding)
		            : edits.removeActionKeybind(action, index);
		if (m_store.setKeybind(action, command)) {
			m_error.IsOpen(false);
			buildKeybinds();
			return true;
		}
	} catch (...) {
		OutputDebugStringW(to_message().c_str());
	}
	m_error.Severity(InfoBarSeverity::Error);
	m_error.Title(L"Couldn't save settings");
	m_error.Message(L"The binding was not changed. Check that the settings "
	                L"folder is writable and try again.");
	m_error.IsOpen(true);
	if (m_overlay.Visibility() == Visibility::Visible) {
		m_captureText.Text(L"Couldn't save settings. Try again.");
	}
	return false;
}

void SettingsWindow::save(const CheckBox& box) {
	const auto key = to_string(unbox_value<hstring>(box.Tag()));
	try {
		if (m_store.setGeneral(key, box.IsChecked().Value())) {
			m_error.IsOpen(false);
			return;
		}
	} catch (...) {
		OutputDebugStringW(to_message().c_str());
	}
	OutputDebugStringW(L"Couldn't save settings.\n");
	box.IsChecked(key == "MaximizeOnOpen"
	                  ? m_store.get().general.MaximizeOnOpen
	                  : m_store.get().general.DisplayThumbs);
	m_error.Severity(InfoBarSeverity::Error);
	m_error.Title(L"Couldn't save settings");
	m_error.Message(L"The setting was restored. Check that the settings "
	                L"folder is writable and try again.");
	m_error.IsOpen(true);
}
