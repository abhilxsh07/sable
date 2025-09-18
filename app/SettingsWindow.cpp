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

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Windowing;

namespace {
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
		item.HorizontalAlignment(HorizontalAlignment::Stretch);
		item.HorizontalContentAlignment(HorizontalAlignment::Right);
		item.BorderThickness({0, 1, 1, 0});
		item.BorderBrush(text);
		item.Foreground(text);
		item.Background(Media::SolidColorBrush({255, 0, 0, 0}));
		item.Margin({0, 8, 0, 8});
		item.IsEnabled(section == "version");
		item.Click([weak, section](auto&&, auto&&) {
			if (auto self = weak.lock()) {
				self->select(section);
			}
		});
		menu.Children().Append(item);
	}
	root.Children().Append(menu);
	StackPanel content;
	Grid::SetColumn(content, 1);
	m_version.Text(to_hstring(helpers::versionString()));
	m_version.TextWrapping(TextWrapping::Wrap);
	m_version.Foreground(text);
	m_version.Margin({0, 16, 0, 0});
	content.Children().Append(m_version);
	root.Children().Append(content);
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
	m_version.Visibility(section == "version" ? Visibility::Visible
	                                          : Visibility::Collapsed);
}
