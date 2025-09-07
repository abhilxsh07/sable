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
#include "HomePage.xaml.h"
#include "ViewerPage.xaml.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace winrt::Sable::implementation {
struct MainWindow : MainWindowT<MainWindow> {
	MainWindow();
	void InitializeComponent();

	void setEngine(pdf::Engine& engine) {
		m_engine = &engine;
	}

	// adds a document tab, or selects the one already open for this path
	void openDocument(pdf::Engine& engine, const std::filesystem::path& path,
	                  std::optional<int> page);

	void onToggleFullScreen(const winrt::Windows::Foundation::IInspectable&,
	                        const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onMinimize(const winrt::Windows::Foundation::IInspectable&,
	                const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onOpenLink(const winrt::Windows::Foundation::IInspectable& sender,
	                const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onNavSelectionChanged(
	    const winrt::Microsoft::UI::Xaml::Controls::NavigationView&,
	    const winrt::Microsoft::UI::Xaml::Controls::
	        NavigationViewSelectionChangedEventArgs& args);
	void onPresetClick(const winrt::Windows::Foundation::IInspectable& sender,
	                   const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onResetZoom(const winrt::Windows::Foundation::IInspectable&,
	                 const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onZoomIn(const winrt::Windows::Foundation::IInspectable&,
	              const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onZoomOut(const winrt::Windows::Foundation::IInspectable&,
	               const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onHideConfigurator(
	    const winrt::Windows::Foundation::IInspectable&,
	    const winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs&);
	void onKeepConfigurator(
	    const winrt::Windows::Foundation::IInspectable&,
	    const winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs& args);
	void onFilterKeyDown(
	    const winrt::Windows::Foundation::IInspectable&,
	    const winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs& args);

  private:
#ifdef SABLE_SURFACE_TEST
	friend struct TabsTest;
#endif
	struct DocumentTab {
		std::filesystem::path path;
		winrt::Microsoft::UI::Xaml::Controls::TabViewItem tab;
		winrt::Sable::ViewerPage viewer;
		std::optional<int> page;
		bool opened = false;
	};
	winrt::Sable::ViewerPage Viewer();
	winrt::fire_and_forget pickDocuments();
	void setupOpen();
	void selectTab();
	void closeTab(winrt::Microsoft::UI::Xaml::Controls::TabViewItem tab);
	void reopenTab();
	void selectTabPosition(int index);
	void moveTab(int index);
	std::vector<DocumentTab> m_tabs;
	std::vector<std::filesystem::path> m_closedTabs;
	pdf::Engine* m_engine = nullptr;
	bool m_closing = false;
	bool m_picking = false;
	bool m_movingTab = false;
	void setupWindow();
	void setupTitleBar();
	void setupZoomKeys();
	void setupTabKeys();
	void updateTitleBar();
	void setupSliders();
	void refreshSliders();
	void updateFilter();
	void toggleConfigurator();
	void alignConfigurator();
	helpers::SliderValues m_values;
	std::string m_preset = "default";
	bool m_settingSliders = false;
	bool m_compactTitle = false;
	winrt::Microsoft::UI::Xaml::Controls::IconSource m_titleIcon{nullptr};
	std::vector<winrt::Microsoft::UI::Xaml::Controls::Slider> m_sliders;
	std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_sliderLabels;

	// kept around so leaving full screen gets the same window back, minimum
	// size and all
	winrt::Microsoft::UI::Windowing::OverlappedPresenter m_presenter{nullptr};
};
} // namespace winrt::Sable::implementation

namespace winrt::Sable::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
} // namespace winrt::Sable::factory_implementation
