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

#include <algorithm>
#include <filesystem>
#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.System.h>

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
	setupSliders();
	m_titleIcon = AppTitleBar().IconSource();
	AppTitleBar().SizeChanged([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->updateTitleBar();
			if (self->DarkConfigurator().Visibility() == Visibility::Visible) {
				self->alignConfigurator();
			}
		}
	});
	// the document goes before App's PDFium engine does
	Closed([this](auto&&, auto&&) { get_self<ViewerPage>(Viewer())->close(); });
}

void MainWindow::openDocument(pdf::Engine& engine,
                              const std::filesystem::path& path,
                              std::optional<int> page) {
	Nav().Visibility(Visibility::Collapsed);
	Viewer().Visibility(Visibility::Visible);
	Presets().Visibility(Visibility::Visible);
	updateTitleBar();
	get_self<ViewerPage>(Viewer())->setValues(m_values);
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

void MainWindow::updateTitleBar() {
	const bool compact = Presets().Visibility() == Visibility::Visible &&
	                     AppTitleBar().ActualWidth() < 700;
	if (compact != m_compactTitle) {
		m_compactTitle = compact;
		// leave room for all four presets and the window buttons
		AppTitleBar().Title(compact ? L"" : L"Sable");
		AppTitleBar().IconSource(compact ? nullptr : m_titleIcon);
	}
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

// port of app/helpers/private.ts setupSliders() (L150-296)
void MainWindow::setupSliders() {
	for (std::size_t i = 0; i < helpers::SLIDERS.size(); ++i) {
		const auto& spec = helpers::SLIDERS[i];
		Controls::TextBlock label;
		Controls::Slider slider;
		slider.Minimum(spec.min);
		slider.Maximum(spec.max);
		slider.StepFrequency(spec.step);
		slider.SmallChange(spec.step);
		slider.IsThumbToolTipEnabled(false);
		Automation::AutomationProperties::SetName(slider,
		                                          to_hstring(spec.label));
		slider.ValueChanged(
		    [weak = get_weak(),
		     i](auto&&,
		        const Controls::Primitives::RangeBaseValueChangedEventArgs&
		            args) {
			    if (auto self = weak.get(); self && !self->m_settingSliders) {
				    double* values[] = {&self->m_values.brightness,
				                        &self->m_values.grayness,
				                        &self->m_values.inversion,
				                        &self->m_values.sepia,
				                        &self->m_values.hue,
				                        &self->m_values.extraBrightness};
				    *values[i] = args.NewValue();
				    self->m_sliderLabels[i].Text(to_hstring(
				        std::string(helpers::SLIDERS[i].label) + " " +
				        helpers::sliderTooltip(helpers::SLIDERS[i],
				                               args.NewValue())));
				    self->updateFilter();
			    }
		    });
		m_sliders.push_back(slider);
		m_sliderLabels.push_back(label);
		FilterSliders().Children().Append(label);
		FilterSliders().Children().Append(slider);
	}
	refreshSliders();
}

void MainWindow::refreshSliders() {
	m_settingSliders = true;
	const double values[] = {m_values.brightness, m_values.grayness,
	                         m_values.inversion,  m_values.sepia,
	                         m_values.hue,        m_values.extraBrightness};
	for (std::size_t i = 0; i < m_sliders.size(); ++i) {
		m_sliders[i].Value(values[i]);
		m_sliderLabels[i].Text(
		    to_hstring(std::string(helpers::SLIDERS[i].label) + " " +
		               helpers::sliderTooltip(helpers::SLIDERS[i], values[i])));
	}
	m_settingSliders = false;
}

void MainWindow::updateFilter() {
	get_self<ViewerPage>(Viewer())->setValues(m_values);
}

// port of app/render/index.ts preset click handlers (L329-417)
void MainWindow::onPresetClick(const Windows::Foundation::IInspectable& sender,
                               const RoutedEventArgs&) {
	const auto button = sender.as<Controls::Button>();
	const auto preset = to_string(unbox_value<hstring>(button.Tag()));
	const bool wasActive = preset == m_preset;
	if (!wasActive) {
		m_preset = preset;
		helpers::handlePresetChange(preset == "custom" ? "original" : preset,
		                            m_values);
		refreshSliders();
		updateFilter();
		for (const auto& chip :
		     {DefaultPreset(), SepiaPreset(), RedeyePreset(), CustomPreset()}) {
			const bool active =
			    unbox_value<hstring>(chip.Tag()) == to_hstring(preset);
			const std::uint8_t shade = active ? 0x79 : 0x1e;
			chip.BorderBrush(
			    Media::SolidColorBrush({255, shade, shade, shade}));
			Automation::AutomationProperties::SetItemStatus(
			    chip, active ? L"Selected" : L"");
		}
	}
	if (wasActive || preset == "custom") {
		toggleConfigurator();
	}
}

// port of app/helpers/css.ts toggleDarkConfigurator() (L1-7)
void MainWindow::toggleConfigurator() {
	const auto panel = DarkConfigurator();
	if (panel.Visibility() == Visibility::Visible) {
		panel.Visibility(Visibility::Collapsed);
		return;
	}
	alignConfigurator();
	panel.Visibility(Visibility::Visible);
}

// index.sass pins the panel under #menu, here its right edge meets the last
// preset. Done again whenever the title bar changes size.
void MainWindow::alignConfigurator() {
	const auto panel = DarkConfigurator();
	const auto root = panel.Parent().as<FrameworkElement>();
	const auto origin =
	    Presets().TransformToVisual(root).TransformPoint({0, 0});
	const double right =
	    root.ActualWidth() - (origin.X + Presets().ActualWidth());
	panel.Margin({0, 0, (std::max)(8.0, right), 8});
}

// port of app/helpers/css.ts hideDarkConfigurator() (L9-13)
void MainWindow::onHideConfigurator(const Windows::Foundation::IInspectable&,
                                    const Input::TappedRoutedEventArgs&) {
	DarkConfigurator().Visibility(Visibility::Collapsed);
}

void MainWindow::onKeepConfigurator(const Windows::Foundation::IInspectable&,
                                    const Input::TappedRoutedEventArgs& args) {
	args.Handled(true);
}

void MainWindow::onFilterKeyDown(const Windows::Foundation::IInspectable&,
                                 const Input::KeyRoutedEventArgs& args) {
	if (args.Key() == Windows::System::VirtualKey::Escape &&
	    DarkConfigurator().Visibility() == Visibility::Visible) {
		DarkConfigurator().Visibility(Visibility::Collapsed);
		args.Handled(true);
	}
}
} // namespace winrt::Sable::implementation
