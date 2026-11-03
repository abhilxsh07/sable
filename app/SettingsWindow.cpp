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
#include "helpers/update_check.h"
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
	const auto weak = weak_from_this();
	auto root = buildLayout();
	window.Activated([weak](auto&&, const WindowActivatedEventArgs& e) {
		if (e.WindowActivationState() == WindowActivationState::Deactivated) {
			if (auto self = weak.lock()) {
				self->dismiss();
			}
		}
	});
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
	m_themeChanged = appearance::observe([weak] {
		if (auto self = weak.lock()) {
			self->themeChanged();
		}
	});
	themeChanged();
	select("general");
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
	    {static_cast<int32_t>(840 * scale), static_cast<int32_t>(640 * scale)});
	window.Closed([weak, ownerHwnd, ownerEnabled](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->m_themeChanged.reset();
		}
		if (ownerEnabled && IsWindow(ownerHwnd)) {
			EnableWindow(ownerHwnd, TRUE);
		}
	});
	EnableWindow(ownerHwnd, FALSE);
	window.Activate();
}

void SettingsWindow::select(const std::string& section) {
	for (const auto& item : m_sections) {
		const bool selected =
		    unbox_value<hstring>(item.Tag()) == to_hstring(section);
		using Windows::UI::ViewManagement::UIElementType;
		item.Background(appearance::themed(
		    selected ? Windows::UI::Color{255, 0x2c, 0x2c, 0x30}
		             : Windows::UI::Color{0, 0, 0, 0},
		    selected ? UIElementType::Highlight : UIElementType::Window));
		item.Foreground(appearance::themed(selected ? 0xf1f1f3 : 0xa7a7ae,
		                                   selected
		                                       ? UIElementType::HighlightText
		                                       : UIElementType::WindowText));
	}
	m_heading.Text(section == "general"    ? L"General"
	               : section == "keybinds" ? L"Keyboard shortcuts"
	                                       : L"About");
	m_description.Text(section == "general" ? L"Make Sable feel like yours."
	                   : section == "keybinds"
	                       ? L"Click a shortcut to change it."
	                       : L"Version and application details.");
	m_about.Visibility(section == "version" ? Visibility::Visible
	                                        : Visibility::Collapsed);
	m_shortcuts.Visibility(section == "keybinds" ? Visibility::Visible
	                                             : Visibility::Collapsed);
	m_detailScroll.Visibility(section == "keybinds" ? Visibility::Collapsed
	                                                : Visibility::Visible);
	m_version.Visibility(section == "version" ? Visibility::Visible
	                                          : Visibility::Collapsed);
	m_general.Visibility(section == "general" ? Visibility::Visible
	                                          : Visibility::Collapsed);
	m_bindingScroll.Visibility(section == "keybinds" ? Visibility::Visible
	                                                 : Visibility::Collapsed);
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
	m_captureText.Text(L"Waiting for shortcut...");
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
		if (m_store.editKeybind(action, index, binding)) {
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

void SettingsWindow::saveUpdateCheck() {
	const bool enabled = m_updateCheck.IsChecked().Value();
	if (helpers::saveUpdateCheck(s_updateCheckFile, {enabled})) {
		m_error.IsOpen(false);
		return;
	}
	m_updateCheck.IsChecked(!enabled);
	m_error.Severity(InfoBarSeverity::Error);
	m_error.Title(L"Couldn't save settings");
	m_error.Message(L"The setting was restored. Check that the settings "
	                L"folder is writable and try again.");
	m_error.IsOpen(true);
}

void SettingsWindow::themeChanged() {
	const auto titleBar = window.AppWindow().TitleBar();
	const auto background = appearance::surface(0x1e1e20).Color();
	const auto foreground = appearance::text(0xcccccc).Color();
	titleBar.BackgroundColor(background);
	titleBar.InactiveBackgroundColor(background);
	titleBar.ButtonBackgroundColor(background);
	titleBar.ButtonInactiveBackgroundColor(background);
	titleBar.ForegroundColor(foreground);
	titleBar.InactiveForegroundColor(foreground);
	titleBar.ButtonForegroundColor(foreground);
	titleBar.ButtonInactiveForegroundColor(foreground);
	m_restoringTheme = true;
	m_theme.SelectedIndex(appearance::obsidian() ? 1 : 0);
	m_restoringTheme = false;
}

void SettingsWindow::saveTheme() {
	if (m_restoringTheme) {
		return;
	}
	const auto theme = m_theme.SelectedIndex() == 1 ? "obsidian" : "dark";
	try {
		if (m_store.setTheme(theme)) {
			appearance::apply(m_store.get().general.Theme == "obsidian");
			m_error.IsOpen(false);
			return;
		}
	} catch (...) {
		OutputDebugStringW(to_message().c_str());
	}
	m_restoringTheme = true;
	m_theme.SelectedIndex(m_store.get().general.Theme == "obsidian" ? 1 : 0);
	m_restoringTheme = false;
	m_error.Severity(InfoBarSeverity::Error);
	m_error.Title(L"Couldn't save theme");
	m_error.Message(L"Your previous theme was kept. Check that the settings "
	                L"folder is writable and try again.");
	m_error.IsOpen(true);
}
