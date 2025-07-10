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

	if (auto presenter = appWindow.Presenter().try_as<OverlappedPresenter>()) {
		presenter.PreferredMinimumWidth(px(MIN_WIDTH));
		presenter.PreferredMinimumHeight(px(MIN_HEIGHT));
	}
	appWindow.Resize({px(WIDTH), px(HEIGHT)});
}
} // namespace winrt::Sable::implementation
