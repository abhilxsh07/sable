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
#include <ShlObj.h>
#include <shellapi.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>

#include "helpers/settings.h"
#include "helpers/version.h"

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

// %APPDATA%\Sable\config.json, the settings file in the shape NightPDF wrote
std::filesystem::path configFile() {
	PWSTR raw = nullptr;
	const HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData,
	                                        KF_FLAG_DEFAULT, nullptr, &raw);
	std::filesystem::path dir;
	if (SUCCEEDED(hr)) {
		dir = raw;
	}
	CoTaskMemFree(raw);
	return dir.empty() ? std::filesystem::path{}
	                   : dir / L"Sable" / L"config.json";
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
	setupZoomKeys();
	setupTabKeys();
	setupOpen();
	// see:
	// https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/tab-view
	Tabs().SelectionChanged([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->selectTab();
		}
	});
	Tabs().TabCloseRequested([weak = get_weak()](auto&&, const auto& args) {
		if (auto self = weak.get()) {
			self->closeTab(args.Tab());
		}
	});

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
	Closed([this](auto&&, auto&&) {
		m_closing = true;
		for (const auto& entry : m_tabs) {
			get_self<ViewerPage>(entry.viewer)->close();
		}
		Documents().Children().Clear();
		Tabs().TabItems().Clear();
		m_tabs.clear();
	});
}

// port of app/main/app.ts openNewPDF() (L284-309): one PDF picker
// for the menu, tab button, splash and OpenWindow shortcut.
void MainWindow::setupOpen() {
	const auto request = [weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->pickDocuments();
		}
	};
	OpenItem().Click(request);
	Tabs().AddTabButtonClick(request);
	get_self<HomePage>(Home())->OpenRequested([weak = get_weak()] {
		if (auto self = weak.get()) {
			self->pickDocuments();
		}
	});
	for (const auto key :
	     {Windows::System::VirtualKey::O, Windows::System::VirtualKey::T}) {
		Input::KeyboardAccelerator accelerator;
		accelerator.Key(key);
		accelerator.Modifiers(Windows::System::VirtualKeyModifiers::Control);
		accelerator.Invoked([weak = get_weak()](auto&&, const auto& args) {
			if (auto self = weak.get()) {
				self->pickDocuments();
				args.Handled(true);
			}
		});
		RootGrid().KeyboardAccelerators().Append(accelerator);
	}
}

fire_and_forget MainWindow::pickDocuments() {
	if (m_closing || m_picking || !m_engine) {
		co_return;
	}
	m_picking = true;
	PickerError().IsOpen(false);
	auto weak = get_weak();
	try {
		// see:
		// https://learn.microsoft.com/en-us/windows/apps/develop/files/using-file-folder-pickers
		Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(
		    AppWindow().Id());
		picker.FileTypeFilter().Append(L".pdf");
		const auto files = co_await picker.PickMultipleFilesAsync();
		if (auto self = weak.get(); self && !self->m_closing) {
			self->m_picking = false;
			for (const auto& file : files) {
				self->openDocument(*self->m_engine, file.Path().c_str(),
				                   std::nullopt);
			}
		}
	} catch (const hresult_error& error) {
		if (auto self = weak.get(); self && !self->m_closing) {
			self->m_picking = false;
			self->PickerError().Message(error.message());
			self->PickerError().IsOpen(true);
		}
	}
}

void MainWindow::openDocument(pdf::Engine& engine,
                              const std::filesystem::path& path,
                              std::optional<int> page) {
	if (m_closing) {
		return;
	}
	m_engine = &engine;
	// port of app/helpers/file.ts openFile() (L44-55): path.resolve, then
	// select an existing tab rather than opening another copy
	std::error_code ec;
	auto resolved = std::filesystem::absolute(path, ec);
	resolved = (ec ? path : resolved).lexically_normal();
	for (const auto& entry : m_tabs) {
		if (entry.path == resolved) {
			Tabs().SelectedItem(entry.tab);
			return;
		}
	}
	auto viewer = make<ViewerPage>();
	viewer.Visibility(Visibility::Collapsed);
	viewer.Loaded([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->DispatcherQueue().TryEnqueue([weak] {
				if (auto window = weak.get()) {
					window->selectTab();
				}
			});
		}
	});
	Controls::TabViewItem tab;
	Controls::TextBlock title;
	// RTL puts the ellipsis at the start; the directional marks keep the
	// filename's own text direction, as in file.ts
	title.Text(L"\u202a" + path.filename().wstring() + L"\u202c");
	title.FlowDirection(FlowDirection::RightToLeft);
	title.TextTrimming(TextTrimming::CharacterEllipsis);
	title.MaxWidth(200);
	tab.Header(title);
	Controls::ToolTipService::SetToolTip(tab, box_value(resolved.wstring()));
	Automation::AutomationProperties::SetName(tab, path.filename().wstring());
	get_self<ViewerPage>(viewer)->setValues(m_values);
	m_tabs.push_back({resolved, tab, viewer, page});
	Documents().Children().Append(viewer);
	Tabs().TabItems().Append(tab);
	Tabs().SelectedItem(tab);
	selectTab();
}

winrt::Sable::ViewerPage MainWindow::Viewer() {
	for (const auto& entry : m_tabs) {
		if (entry.tab == Tabs().SelectedItem()) {
			return entry.viewer;
		}
	}
	return nullptr;
}

void MainWindow::setupTabKeys() {
	using Windows::System::VirtualKey;
	RootGrid().PreviewKeyDown([weak = get_weak()](auto&&, const auto& args) {
		auto self = weak.get();
		if (!self || !self->Viewer() || args.Handled() ||
		    (GetKeyState(VK_CONTROL) & 0x8000) == 0 ||
		    (GetKeyState(VK_MENU) & 0x8000) != 0 ||
		    (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
		    (GetKeyState(VK_RWIN) & 0x8000) != 0) {
			return;
		}
		const auto key = args.Key();
		const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		if (!shift && (key == VirtualKey::W || key == VirtualKey::F4)) {
			self->closeTab(
			    self->Tabs().SelectedItem().as<Controls::TabViewItem>());
			args.Handled(true);
		} else if (key == VirtualKey::Tab ||
		           (!shift && (key == VirtualKey::PageUp ||
		                       key == VirtualKey::PageDown))) {
			const int direction = shift || key == VirtualKey::PageUp ? -1 : 1;
			const int index = self->Tabs().SelectedIndex() + direction;
			if (index >= 0 && index < static_cast<int>(self->m_tabs.size())) {
				self->Tabs().SelectedIndex(index);
			}
			// TabView wraps by default; electron-tabs stopped at either end
			args.Handled(true);
		}
	});
}

void MainWindow::selectTab() {
	if (m_closing) {
		return;
	}
	const auto viewer = Viewer();
	const bool hasDocument = viewer != nullptr;
	Nav().Visibility(hasDocument ? Visibility::Collapsed : Visibility::Visible);
	Documents().Visibility(hasDocument ? Visibility::Visible
	                                   : Visibility::Collapsed);
	Tabs().Visibility(hasDocument ? Visibility::Visible
	                              : Visibility::Collapsed);
	Presets().Visibility(hasDocument ? Visibility::Visible
	                                 : Visibility::Collapsed);
	ResetZoomItem().IsEnabled(hasDocument);
	ZoomInItem().IsEnabled(hasDocument);
	ZoomOutItem().IsEnabled(hasDocument);
	DarkConfigurator().Visibility(Visibility::Collapsed);
	updateTitleBar();
	for (auto& entry : m_tabs) {
		const bool selected = entry.viewer == viewer;
		entry.viewer.Visibility(selected ? Visibility::Visible
		                                 : Visibility::Collapsed);
		if (!entry.viewer.IsLoaded()) {
			continue;
		}
		if (!entry.opened) {
			entry.opened = true;
			bool thumbnails = true;
			try {
				const auto file = configFile();
				if (!file.empty()) {
					thumbnails =
					    helpers::loadSettings(file, helpers::versionString())
					        .general.DisplayThumbs;
				}
			} catch (...) {
				// the defaults, like a missing file
			}
			get_self<ViewerPage>(entry.viewer)
			    ->open(*m_engine, entry.path, entry.page, thumbnails);
		}
		if (selected) {
			get_self<ViewerPage>(entry.viewer)->activate();
		}
	}
}

void MainWindow::closeTab(Controls::TabViewItem tab) {
	const auto found =
	    std::find_if(m_tabs.begin(), m_tabs.end(),
	                 [&tab](const auto& entry) { return entry.tab == tab; });
	if (m_closing || found == m_tabs.end()) {
		return;
	}
	get_self<ViewerPage>(found->viewer)->close();
	uint32_t index = 0;
	if (Documents().Children().IndexOf(found->viewer, index)) {
		Documents().Children().RemoveAt(index);
	}
	m_tabs.erase(found);
	if (Tabs().TabItems().IndexOf(tab, index)) {
		Tabs().TabItems().RemoveAt(index);
	}
	selectTab();
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

// Electron's resetZoom, zoomIn and zoomOut accelerators, plus the keys
// pdf.js took: Ctrl (or Ctrl+Shift) with =, numpad +, -, numpad -, 0. They
// sit on the window's grid, since a closed menu's items aren't in the tree.
void MainWindow::setupZoomKeys() {
	using Windows::System::VirtualKey;
	using Modifiers = Windows::System::VirtualKeyModifiers;
	using Zoom = void (*)(ViewerPage&);
	const auto add = [this](VirtualKey key, Controls::MenuFlyoutItem item,
	                        Zoom zoom) {
		for (const auto modifiers :
		     {Modifiers::Control, Modifiers::Control | Modifiers::Shift}) {
			Input::KeyboardAccelerator accelerator;
			accelerator.Key(key);
			accelerator.Modifiers(modifiers);
			accelerator.Invoked(
			    [weak = get_weak(), item, zoom](auto&&, const auto& args) {
				    if (auto self = weak.get(); self && item.IsEnabled()) {
					    zoom(*get_self<ViewerPage>(self->Viewer()));
					    args.Handled(true);
				    }
			    });
			RootGrid().KeyboardAccelerators().Append(accelerator);
		}
	};
	// the = and - keys have no names in VirtualKey
	const Zoom in = [](ViewerPage& viewer) { viewer.zoomIn(); };
	const Zoom out = [](ViewerPage& viewer) { viewer.zoomOut(); };
	const Zoom reset = [](ViewerPage& viewer) { viewer.zoomReset(); };
	add(static_cast<VirtualKey>(VK_OEM_PLUS), ZoomInItem(), in);
	add(VirtualKey::Add, ZoomInItem(), in);
	add(static_cast<VirtualKey>(VK_OEM_MINUS), ZoomOutItem(), out);
	add(VirtualKey::Subtract, ZoomOutItem(), out);
	add(VirtualKey::Number0, ResetZoomItem(), reset);
	add(VirtualKey::NumberPad0, ResetZoomItem(), reset);
	// a TextBox takes Ctrl+- for a soft hyphen before the accelerators see
	// it, and pdf.js zoomed out from its page box too (app.js L2878-2915),
	// so this one is caught on the way down
	RootGrid().PreviewKeyDown([weak = get_weak()](auto&&, const auto& args) {
		const auto key = args.Key();
		if (args.Handled() ||
		    (key != static_cast<VirtualKey>(VK_OEM_MINUS) &&
		     key != VirtualKey::Subtract) ||
		    (GetKeyState(VK_CONTROL) & 0x8000) == 0 ||
		    (GetKeyState(VK_MENU) & 0x8000) != 0 ||
		    (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
		    (GetKeyState(VK_RWIN) & 0x8000) != 0) {
			return;
		}
		if (auto self = weak.get(); self && self->ZoomOutItem().IsEnabled()) {
			get_self<ViewerPage>(self->Viewer())->zoomOut();
			args.Handled(true);
		}
	});
	// no hint bubbles for these on a grid
	RootGrid().KeyboardAcceleratorPlacementMode(
	    Input::KeyboardAcceleratorPlacementMode::Hidden);
}

void MainWindow::onResetZoom(const Windows::Foundation::IInspectable&,
                             const RoutedEventArgs&) {
	if (auto viewer = Viewer()) {
		get_self<ViewerPage>(viewer)->zoomReset();
	}
}

void MainWindow::onZoomIn(const Windows::Foundation::IInspectable&,
                          const RoutedEventArgs&) {
	if (auto viewer = Viewer()) {
		get_self<ViewerPage>(viewer)->zoomIn();
	}
}

void MainWindow::onZoomOut(const Windows::Foundation::IInspectable&,
                           const RoutedEventArgs&) {
	if (auto viewer = Viewer()) {
		get_self<ViewerPage>(viewer)->zoomOut();
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
	for (const auto& entry : m_tabs) {
		get_self<ViewerPage>(entry.viewer)->setValues(m_values);
	}
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
