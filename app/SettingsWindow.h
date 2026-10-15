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

#include "helpers/settings.h"
#include <array>
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::Sable::implementation {
struct TabsTest;
}

struct SettingsWindow : std::enable_shared_from_this<SettingsWindow> {
	static std::shared_ptr<SettingsWindow>
	open(const winrt::Microsoft::UI::Xaml::Window& owner,
	     helpers::SettingsStore& store, const std::filesystem::path& icon);
	winrt::Microsoft::UI::Xaml::Window window{nullptr};

  private:
#ifdef SABLE_SURFACE_TEST
	friend struct winrt::Sable::implementation::TabsTest;
#endif
	explicit SettingsWindow(helpers::SettingsStore& store) : m_store(store) {}
	void initialize(const winrt::Microsoft::UI::Xaml::Window& owner,
	                const std::filesystem::path& icon);
	void select(const std::string& section);
	void save(const winrt::Microsoft::UI::Xaml::Controls::CheckBox& box);
	helpers::SettingsStore& m_store;
	winrt::Microsoft::UI::Xaml::Controls::TextBlock m_version;
	winrt::Microsoft::UI::Xaml::Controls::StackPanel m_general;
	winrt::Microsoft::UI::Xaml::Controls::InfoBar m_error;
	std::array<winrt::Microsoft::UI::Xaml::Controls::CheckBox, 2> m_checks;
	std::vector<winrt::Microsoft::UI::Xaml::Controls::Button> m_sections;
};
