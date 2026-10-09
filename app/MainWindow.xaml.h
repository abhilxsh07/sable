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

#include "MainWindow.g.h"

#include <winrt/Microsoft.UI.Windowing.h>

namespace winrt::Sable::implementation {
struct MainWindow : MainWindowT<MainWindow> {
	MainWindow();

	void onToggleFullScreen(const winrt::Windows::Foundation::IInspectable&,
	                        const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onMinimize(const winrt::Windows::Foundation::IInspectable&,
	                const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onOpenLink(const winrt::Windows::Foundation::IInspectable& sender,
	                const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);

  private:
	void setupWindow();
	void setupTitleBar();

	// kept around so leaving full screen gets the same window back, minimum
	// size and all
	winrt::Microsoft::UI::Windowing::OverlappedPresenter m_presenter{nullptr};
};
} // namespace winrt::Sable::implementation

namespace winrt::Sable::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
} // namespace winrt::Sable::factory_implementation
