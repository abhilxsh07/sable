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
#pragma once

#include "App.xaml.g.h"

#include "pdf/engine.h"
#include "helpers/settings.h"
#include "Notifications.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace winrt::Sable::implementation {
struct MainWindow;

struct App : AppT<App> {
	App();
	~App();

	void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
	winrt::com_ptr<MainWindow>
	createWindow(const std::vector<std::filesystem::path>& files = {},
	             std::optional<int> page = {});
	void showEasterEgg() noexcept {
		m_notifications.show();
	}

  private:
#ifdef SABLE_SURFACE_TEST
	friend struct TabsTest;
#endif
	void trackWindow(const winrt::Microsoft::UI::Xaml::Window& window);
	void activateReader();
	// only document windows need PDFium. Declared first so it's torn down
	// last, after every window and anything they hold.
	std::unique_ptr<pdf::Engine> m_pdf;
	std::unique_ptr<helpers::SettingsStore> m_settings;
	std::vector<winrt::Microsoft::UI::Xaml::Window> m_windows;
	notices::Service m_notifications;
};
} // namespace winrt::Sable::implementation
