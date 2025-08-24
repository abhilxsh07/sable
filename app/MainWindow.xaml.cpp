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
// trans rights
#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <filesystem>
#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Windowing.h>

using namespace winrt;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;

namespace {
// same numbers as NightPDF's BrowserWindow. Yes, the minimum width is bigger
// than the starting width there too, kept as is.
constexpr int WIDTH = 550;
constexpr int HEIGHT = 420;
constexpr int MIN_WIDTH = 565;
constexpr int MIN_HEIGHT = 200;

std::filesystem::path exeDir() {
	// GetModuleFileNameW truncates instead of failing when the buffer is too
	// small, so keep growing it until the whole path fits
	std::wstring buf(MAX_PATH, L'\0');
	DWORD len = 0;
	while ((len = GetModuleFileNameW(nullptr, buf.data(),
	                                 static_cast<DWORD>(buf.size()))) ==
	       buf.size()) {
		buf.resize(buf.size() * 2);
	}
	buf.resize(len);
	return std::filesystem::path(buf).parent_path();
}
} // namespace

namespace winrt::Sable::implementation {
MainWindow::MainWindow() {
	setupWindow();
}

// C++/WinRT loads the XAML after the constructor returns, so anything that
// needs a named element has to wait until here
void MainWindow::InitializeComponent() {
	MainWindowT::InitializeComponent();
	setupTitleBar();
	// the document goes before App's PDFium engine does
	Closed([this](auto&&, auto&&) { get_self<ViewerPage>(Viewer())->close(); });
}

void MainWindow::openDocument(pdf::Engine& engine,
                              const std::filesystem::path& path,
                              std::optional<int> page) {
	Nav().Visibility(Visibility::Collapsed);
	Viewer().Visibility(Visibility::Visible);
	get_self<ViewerPage>(Viewer())->open(engine, path, page);
}

// port of app/main/app.ts createWindow() (L135-148), just the window part.
// Electron's sizes are DIPs but AppWindow works in physical pixels.
void MainWindow::setupWindow() {
	auto appWindow = AppWindow();
	// relative paths resolve against the working directory, not the exe
	appWindow.SetIcon((exeDir() / L"assets" / L"icon.ico").wstring());

	auto windowNative = this->try_as<::IWindowNative>();
	if (!windowNative) {
		return; // every XAML Window has one, but don't crash over a size
	}
	HWND hwnd{};
	check_hresult(windowNative->get_WindowHandle(&hwnd));
	const double scale = GetDpiForWindow(hwnd) / 96.0;
	auto px = [scale](int dip) {
		return static_cast<int32_t>(dip * scale + 0.5);
	};

	m_presenter = OverlappedPresenter::Create();
	m_presenter.PreferredMinimumWidth(px(MIN_WIDTH));
	m_presenter.PreferredMinimumHeight(px(MIN_HEIGHT));
	appWindow.SetPresenter(m_presenter);
	// NightPDF's size is still what you get back when you restore the window,
	// but it starts maximized now (docs/DECISIONS.md)
	appWindow.Resize({px(WIDTH), px(HEIGHT)});
	m_presenter.Maximize();
}

void MainWindow::setupTitleBar() {
	ExtendsContentIntoTitleBar(true);
	SetTitleBar(AppTitleBar());

	// the min/max/close buttons are still drawn by Windows, match them to the
	// bar. Tall so they line up with the 48px TitleBar control.
	auto titleBar = AppWindow().TitleBar();
	titleBar.PreferredHeightOption(TitleBarHeightOption::Tall);
	const auto transparent = Microsoft::UI::Colors::Transparent();
	titleBar.ButtonBackgroundColor(transparent);
	titleBar.ButtonInactiveBackgroundColor(transparent);
	titleBar.ButtonForegroundColor(Windows::UI::Color{255, 0xe8, 0xe8, 0xe8});
	titleBar.ButtonInactiveForegroundColor(
	    Windows::UI::Color{255, 0x7a, 0x7a, 0x7a});
	titleBar.ButtonHoverBackgroundColor(
	    Windows::UI::Color{255, 0x2d, 0x2d, 0x2d});
	titleBar.ButtonHoverForegroundColor(
	    Windows::UI::Color{255, 0xff, 0xff, 0xff});
	titleBar.ButtonPressedBackgroundColor(
	    Windows::UI::Color{255, 0x38, 0x38, 0x38});
	titleBar.ButtonPressedForegroundColor(
	    Windows::UI::Color{255, 0xff, 0xff, 0xff});
}

// View > Toggle Full Screen (Electron's togglefullscreen role)
void MainWindow::onToggleFullScreen(const Windows::Foundation::IInspectable&,
                                    const RoutedEventArgs&) {
	auto appWindow = AppWindow();
	if (appWindow.Presenter().Kind() == AppWindowPresenterKind::FullScreen) {
		appWindow.SetPresenter(m_presenter);
	} else {
		appWindow.SetPresenter(FullScreenPresenter::Create());
	}
}

// Window > Minimize
void MainWindow::onMinimize(const Windows::Foundation::IInspectable&,
                            const RoutedEventArgs&) {
	m_presenter.Minimize();
}

// Recent and Starred are the only items that stay selected
void MainWindow::onNavSelectionChanged(
    const Controls::NavigationView&,
    const Controls::NavigationViewSelectionChangedEventArgs& args) {
	auto item = args.SelectedItemContainer();
	if (!item) {
		return;
	}
	const auto tag = unbox_value_or<hstring>(item.Tag(), hstring{});
	// IsSelected in the XAML can fire this while the page is still loading
	if (auto home = Home()) {
		home.ShowStarredOnly(tag == L"starred");
	}
}

// the Help items keep their link in Tag
void MainWindow::onOpenLink(const Windows::Foundation::IInspectable& sender,
                            const RoutedEventArgs&) {
	auto item = sender.try_as<Controls::MenuFlyoutItem>();
	if (!item) {
		return;
	}
	const auto url = unbox_value_or<hstring>(item.Tag(), hstring{});
	if (url.empty()) {
		return;
	}
	ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr,
	              SW_SHOWNORMAL);
}
} // namespace winrt::Sable::implementation
