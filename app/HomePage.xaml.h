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

#include "HomePage.g.h"

#include <chrono>
#include <filesystem>
#include <string>

#include "helpers/recents.h"
#include "helpers/sliders.h"

namespace winrt::Sable::implementation {
struct HomePage : HomePageT<HomePage> {
	HomePage() = default;
	void InitializeComponent();

	void ShowStarredOnly(bool starredOnly);

	void onPresetClick(const winrt::Windows::Foundation::IInspectable& sender,
	                   const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onSearchChanged(
	    const winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBox& sender,
	    const winrt::Microsoft::UI::Xaml::Controls::
	        AutoSuggestBoxTextChangedEventArgs&);
	void onClearRecent(const winrt::Windows::Foundation::IInspectable&,
	                   const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);

  private:
	void onSizeChanged(const winrt::Windows::Foundation::IInspectable&,
	                   const winrt::Microsoft::UI::Xaml::SizeChangedEventArgs&
	                       args);
	void showPreset(const std::string& preset);
	void rebuildList();
	winrt::Microsoft::UI::Xaml::Controls::Grid
	makeRow(const helpers::RecentFile& file, std::chrono::sys_seconds now,
	        std::chrono::minutes utcOffset);
	void toggleStar(const std::filesystem::path& path);
	void save();

	helpers::RecentFiles m_recents;
	std::filesystem::path m_recentsFile;
	helpers::SliderValues m_values;
	std::string m_preset;
	std::wstring m_query; // lower case
	bool m_starredOnly = false;
	bool m_compact = false;
};
} // namespace winrt::Sable::implementation

namespace winrt::Sable::factory_implementation {
struct HomePage : HomePageT<HomePage, implementation::HomePage> {};
} // namespace winrt::Sable::factory_implementation
