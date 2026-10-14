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
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <algorithm>
#include <charconv>
#include <exception>
#include <filesystem>
#include <utility>
#include <microsoft.ui.xaml.window.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>

#include "helpers/settings.h"
#include "helpers/version.h"
#include "helpers/logging.h"
#include "SettingsWindow.h"
#include "Appearance.h"
#include "MusicPane.h"
#include "NightRunPane.h"

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
// each Morty alert stays this long, and they leave at least MORTY_GAP apart
// so a burst of presses still fades one at a time
constexpr auto MORTY_LIFE = std::chrono::seconds(4);
constexpr auto MORTY_GAP = std::chrono::milliseconds(700);
constexpr auto MORTY_FADE = std::chrono::milliseconds(800);
constexpr std::size_t MORTY_MAX = 8;

struct SliderToolTip : implements<SliderToolTip, Data::IValueConverter> {
	explicit SliderToolTip(helpers::SliderSpec spec) : m_spec(spec) {}
	Windows::Foundation::IInspectable
	Convert(const Windows::Foundation::IInspectable& value,
	        const Windows::UI::Xaml::Interop::TypeName&,
	        const Windows::Foundation::IInspectable&, const hstring&) {
		return box_value(to_hstring(
		    helpers::sliderTooltip(m_spec, unbox_value<double>(value))));
	}
	Windows::Foundation::IInspectable
	ConvertBack(const Windows::Foundation::IInspectable&,
	            const Windows::UI::Xaml::Interop::TypeName&,
	            const Windows::Foundation::IInspectable&, const hstring&) {
		throw hresult_not_implemented();
	}

  private:
	helpers::SliderSpec m_spec;
};

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

helpers::NightPDFSettings windowSettings() {
	auto settings =
	    helpers::nightpdf_default_settings(helpers::versionString());
	try {
		const auto file = configFile();
		if (!file.empty()) {
			settings = helpers::loadSettings(file, helpers::versionString());
		}
	} catch (...) {
		OutputDebugStringW(L"Couldn't load window settings; using defaults.\n");
	}
	return settings;
}

// see:
// https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
unsigned shortcutKey(const std::string& name) {
	const auto text = winrt::to_hstring(name);
	if (text.size() == 1) {
		const SHORT code = VkKeyScanW(text[0]);
		return code == -1 ? 0 : code & 0xff;
	}
	if (name.starts_with("F")) {
		unsigned number = 0;
		const auto end = name.data() + name.size();
		const auto parsed = std::from_chars(name.data() + 1, end, number);
		if (parsed.ec == std::errc{} && parsed.ptr == end && number >= 1 &&
		    number <= 24) {
			return VK_F1 + number - 1;
		}
	}
	const std::pair<const char*, unsigned> names[] = {
	    {"Tab", VK_TAB},        {"PageUp", VK_PRIOR},
	    {"PageDown", VK_NEXT},  {"Home", VK_HOME},
	    {"End", VK_END},        {"Enter", VK_RETURN},
	    {"Return", VK_RETURN},  {"Escape", VK_ESCAPE},
	    {"Esc", VK_ESCAPE},     {"Space", VK_SPACE},
	    {"Backspace", VK_BACK}, {"Delete", VK_DELETE},
	    {"Insert", VK_INSERT},  {"ArrowLeft", VK_LEFT},
	    {"Left", VK_LEFT},      {"ArrowRight", VK_RIGHT},
	    {"Right", VK_RIGHT},    {"ArrowUp", VK_UP},
	    {"Up", VK_UP},          {"ArrowDown", VK_DOWN},
	    {"Down", VK_DOWN}};
	for (const auto& [label, code] : names) {
		if (name == label) {
			return code;
		}
	}
	return 0;
}
} // namespace

namespace winrt::Sable::implementation {
MainWindow::MainWindow() : MainWindow(windowSettings()) {}

MainWindow::MainWindow(helpers::NightPDFSettings settings)
    : m_settings(std::move(settings)) {
	// Read Aloud's voice and speed, next to config.json like night-run.json
	const auto config = configFile();
	ViewerPage::s_readAloudFile =
	    config.empty() ? std::filesystem::path{}
	                   : config.parent_path() / L"read-aloud.json";
	// the tool pane's widths, beside it too
	ViewerPage::s_panesFile = config.empty()
	                              ? std::filesystem::path{}
	                              : config.parent_path() / L"panes.json";
	// the update check's on/off choice, beside them
	::SettingsWindow::s_updateCheckFile =
	    config.empty() ? std::filesystem::path{}
	                   : config.parent_path() / L"update-check.json";
	setupWindow();
}

// C++/WinRT loads the XAML after the constructor returns, so anything that
// needs a named element has to wait until here
void MainWindow::InitializeComponent() {
	MainWindowT::InitializeComponent();
	appearance::initialize(m_settings.general.Theme == "obsidian");
	setupTitleBar();
	setupSliders();
	paintPresetChips();
	setupPresentation();
	setupTabKeys();
	setupOpen();
	setupEdit();
	setupMusic();
	checkForUpdates();
	Activated([weak = get_weak()](auto&&, const WindowActivatedEventArgs& e) {
		const auto self = weak.get();
		if (!self) {
			return;
		}
		if (e.WindowActivationState() == WindowActivationState::Deactivated) {
			if (self->m_nightRun) {
				self->m_nightRun->pause();
			}
			for (const auto& entry : self->m_tabs) {
				get_self<ViewerPage>(entry.viewer)->stopPan(true);
			}
			return;
		}
		// the focus is put back after this, so look once it has been
		self->DispatcherQueue().TryEnqueue([weak] {
			if (const auto window = weak.get()) {
				window->keepFocus();
			}
		});
	});
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

	// the settings dispatcher owns these, even with focus in the strip
	for (const auto& accelerator : Tabs().KeyboardAccelerators()) {
		accelerator.IsEnabled(false);
	}
	// see:
	// https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/tab-view
	Tabs().TabDragStarting([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->beginTabDrag();
		}
	});
	Tabs().TabDragCompleted([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->endTabDrag();
		}
	});
	// see:
	// https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/title-bar
	Tabs().SizeChanged([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->AppTitleBar().RecomputeDragRegions();
		}
	});

	RootGrid().SizeChanged([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->updateTabWidths();
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
		m_nightRun.reset();
		m_nightRunTab = nullptr;
		for (const auto& morty : m_morties) {
			morty.timer.Stop();
		}
		if (m_saveCopyCancel) {
			m_saveCopyCancel->store(true);
		}
		m_presentationViewer = nullptr;
		m_beforePresentation = nullptr;
		if (m_music) {
			m_music->close();
		}
		if (const auto settings = m_settingsWindow) {
			settings->window.Close();
		}
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
	Nav().ItemInvoked([weak = get_weak()](auto&&, const auto& args) {
		const auto self = weak.get();
		if (!self || !args.InvokedItemContainer()) {
			return;
		}
		const auto tag = unbox_value_or<hstring>(
		    args.InvokedItemContainer().Tag(), hstring{});
		if (tag == L"settings") {
			self->onOpenSettings({}, {});
		} else if (tag == L"organize" || tag == L"combine" ||
		           tag == L"export" || tag == L"fillsign" ||
		           tag == L"comment" || tag == L"readaloud") {
			self->openTool(tag);
		}
	});
	get_self<HomePage>(Home())->ToolRequested(
	    [weak = get_weak()](std::wstring_view tool) {
		    if (auto self = weak.get()) {
			    self->openTool(tool);
		    }
	    });
	get_self<HomePage>(Home())->PresetRequested(
	    [weak = get_weak()](const std::string& preset) {
		    if (const auto self = weak.get(); self && !self->m_closing) {
			    self->setPreset(preset);
		    }
	    });
	get_self<HomePage>(Home())->OpenRequested([weak = get_weak()] {
		if (auto self = weak.get()) {
			self->pickDocuments();
		}
	});
	get_self<HomePage>(Home())->OpenFileRequested(
	    [weak = get_weak()](const std::filesystem::path& path) {
		    if (auto self = weak.get(); self && self->m_engine) {
			    self->openDocument(*self->m_engine, path, std::nullopt);
		    }
	    });
	// port of app/render/index.ts splash drop handlers (L436-473)
	// see:
	// https://learn.microsoft.com/en-us/windows/apps/design/input/drag-and-drop
	RootGrid().DragOver([weak = get_weak()](auto&&, const DragEventArgs& args) {
		using namespace Windows::ApplicationModel::DataTransfer;
		if (!args.DataView().Contains(StandardDataFormats::StorageItems())) {
			return;
		}
		auto operation = DataPackageOperation::None;
		if (auto self = weak.get();
		    self && !self->m_closing && self->m_engine) {
			operation = DataPackageOperation::Copy;
		}
		args.AcceptedOperation(operation);
		args.Handled(true);
	});
	RootGrid().Drop([weak = get_weak()](auto&&, const DragEventArgs& args) {
		using Windows::ApplicationModel::DataTransfer::StandardDataFormats;
		if (!args.DataView().Contains(StandardDataFormats::StorageItems())) {
			return;
		}
		args.Handled(true);
		if (auto self = weak.get()) {
			self->dropDocuments(args.DataView());
		}
	});
}

fire_and_forget MainWindow::pickDocuments() {
	commitMenuEdit();
	weak_ref<MainWindow> weak;
	const auto report = [&weak](const wchar_t* message) noexcept {
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't open the PDF picker");
		OutputDebugStringW(L"PDF picker: ");
		OutputDebugStringW(message);
		OutputDebugStringW(L"\n");
		try {
			if (auto self = weak.get()) {
				self->m_picking = false;
				self->m_toolNext.clear();
				if (!self->m_closing) {
					self->PickerError().Message(message);
					self->PickerError().IsOpen(true);
				}
			}
		} catch (...) {
			OutputDebugStringW(L"Couldn't show the PDF picker error.\n");
		}
	};
	try {
		weak = get_weak();
		const auto tool = std::exchange(m_toolNext, {});
		if (m_closing || m_picking || !m_engine) {
			co_return;
		}
		m_picking = true;
		PickerError().Title(L"Couldn't open the file dialog");
		PickerError().IsOpen(false);
		// see:
		// https://learn.microsoft.com/en-us/windows/apps/develop/files/using-file-folder-pickers
		std::vector<std::filesystem::path> paths;
#ifdef SABLE_SURFACE_TEST
		if (m_documentPicker) {
			const auto files = co_await m_documentPicker();
			for (const auto& path : files) {
				paths.emplace_back(path.c_str());
			}
		} else
#endif
		{
			Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(
			    AppWindow().Id());
			picker.FileTypeFilter().Append(L".pdf");
			const auto files = co_await picker.PickMultipleFilesAsync();
			for (const auto& file : files) {
				paths.emplace_back(file.Path().c_str());
			}
		}
		if (auto self = weak.get(); self && !self->m_closing) {
			self->m_picking = false;
			self->m_toolNext = tool;
			if (tool == L"combine" && !paths.empty()) {
				const auto firstPath = paths.front();
				self->openDocument(*self->m_engine, firstPath, std::nullopt,
				                   std::move(paths));
			} else {
				for (const auto& path : paths) {
					self->openDocument(*self->m_engine, path, std::nullopt);
				}
			}
			// a tool waits only for the files picked for it.
			self->m_toolNext.clear();
		}
	} catch (const hresult_error& error) {
		try {
			report(error.message().c_str());
		} catch (...) {
			diagnostics::caught(diagnostics::Level::Error,
			                    "Could not read the PDF picker error");
			report(L"Couldn't open the PDF picker.");
		}
	} catch (const std::exception& error) {
		OutputDebugStringA(error.what());
		report(L"Couldn't open the selected PDFs.");
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open selected PDFs");
		report(L"Couldn't open the selected PDFs: unknown error.");
	}
}

fire_and_forget MainWindow::dropDocuments(
    Windows::ApplicationModel::DataTransfer::DataPackageView data) {
	weak_ref<MainWindow> weak;
	const auto report = [&weak](const wchar_t* message) noexcept {
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't read dropped files");
		OutputDebugStringW(L"Dropped files: ");
		OutputDebugStringW(message);
		OutputDebugStringW(L"\n");
		try {
			if (auto self = weak.get(); self && !self->m_closing) {
				self->PickerError().Message(message);
				self->PickerError().IsOpen(true);
			}
		} catch (...) {
			OutputDebugStringW(L"Couldn't show the dropped-file error.\n");
		}
	};
	try {
		weak = get_weak();
		using Windows::ApplicationModel::DataTransfer::StandardDataFormats;
		if (m_closing || !m_engine ||
		    !data.Contains(StandardDataFormats::StorageItems())) {
			co_return;
		}
		PickerError().IsOpen(false);
		const auto items = co_await data.GetStorageItemsAsync();
		if (auto self = weak.get();
		    self && !self->m_closing && self->m_engine) {
			std::vector<std::filesystem::path> files;
			for (const auto& item : items) {
				if (!item.try_as<Windows::Storage::StorageFile>()) {
					continue;
				}
				const auto path = item.Path();
				if (path.empty()) {
					report(
					    L"Couldn't open a dropped item without a local path.");
					continue;
				}
				files.emplace_back(path.c_str());
			}
			self->openDroppedFiles(files);
		}
	} catch (const hresult_error& error) {
		try {
			report(error.message().c_str());
		} catch (...) {
			diagnostics::caught(diagnostics::Level::Error,
			                    "Could not read the dropped-file error");
			report(L"Couldn't read the dropped files.");
		}
	} catch (const std::exception& error) {
		OutputDebugStringA(error.what());
		report(L"Couldn't open the dropped files.");
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open dropped PDFs");
		report(L"Couldn't open the dropped files: unknown error.");
	}
}

void MainWindow::openDroppedFiles(
    const std::vector<std::filesystem::path>& files) {
	if (m_closing || !m_engine) {
		return;
	}
	for (const auto& path : files) {
		if (path.empty() || _wcsicmp(path.extension().c_str(), L".pdf") != 0) {
			continue;
		}
		std::error_code error;
		if (!std::filesystem::is_directory(path, error)) {
			openDocument(*m_engine, path, std::nullopt);
		}
	}
}

void MainWindow::openDocument(pdf::Engine& engine,
                              const std::filesystem::path& path,
                              std::optional<int> page,
                              std::vector<std::filesystem::path> combine) {
	if (m_closing || m_closePending) {
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
			if (!combine.empty()) {
				get_self<ViewerPage>(entry.viewer)->m_combineSeed =
				    std::move(combine);
			}
			if (!m_toolNext.empty()) {
				get_self<ViewerPage>(entry.viewer)
				    ->requestTool(std::exchange(m_toolNext, {}));
			}
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
	tab.MaxWidth(RootGrid().ActualWidth() * 0.15);
	tab.Header(title);
	Controls::ToolTipService::SetToolTip(tab, box_value(resolved.wstring()));
	Automation::AutomationProperties::SetName(tab, path.filename().wstring());
	const auto pdf = get_self<ViewerPage>(viewer);
	pdf->m_combineSeed = std::move(combine);
	pdf->m_combineRequest = [weak = get_weak()](auto files, auto name) {
		if (const auto self = weak.get()) {
			self->saveCombined(std::move(files), std::move(name));
		}
	};
	// the command bar's save, print and the rest are this window's
	pdf->m_commandRequested = [weak = get_weak()](std::wstring_view command) {
		if (const auto self = weak.get()) {
			self->viewerCommand(command);
		}
	};
	pdf->m_notesAvailable = [weak = get_weak()] {
		const auto self = weak.get();
		return self && !self->m_closing && !self->m_closePending;
	};
	get_self<ViewerPage>(viewer)->setValues(m_values);
	if (!m_toolNext.empty()) {
		get_self<ViewerPage>(viewer)->requestTool(
		    std::exchange(m_toolNext, {}));
	}
	// a file only goes into Recent once it has actually opened
	get_self<ViewerPage>(viewer)->onOpened([weak = get_weak(), resolved] {
		if (const auto self = weak.get(); self && !self->m_closing) {
			self->selectTab();
			get_self<HomePage>(self->Home())->Remember(resolved);
		}
	});
	m_tabs.push_back({resolved, tab, viewer, page});
	Documents().Children().Append(viewer);
	Tabs().TabItems().Append(tab);
	Tabs().SelectedItem(tab);
	selectTab();
}

void MainWindow::openNightRun() {
	if (!m_nightRun) {
		const auto config = configFile();
		m_nightRun = night_run::Pane::create(
		    DispatcherQueue(), config.empty()
		                           ? std::filesystem::path{}
		                           : config.parent_path() / L"night-run.json");
		m_nightRunTab = Controls::TabViewItem{};
		m_nightRunTab.Header(box_value(L"Night Run"));
		Automation::AutomationProperties::SetName(m_nightRunTab, L"Night Run");
		Documents().Children().Append(m_nightRun->element());
		Tabs().TabItems().Append(m_nightRunTab);
	}
	Tabs().SelectedItem(m_nightRunTab);
	selectTab();
	DispatcherQueue().TryEnqueue(
	    Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
	    [weak = get_weak()] {
		    if (const auto self = weak.get();
		        self && self->nightRunSelected()) {
			    self->m_nightRun->focus();
		    }
	    });
}

bool MainWindow::nightRunSelected() {
	return m_nightRun && Tabs().SelectedItem() == m_nightRunTab;
}

winrt::Sable::ViewerPage MainWindow::Viewer() {
	for (const auto& entry : m_tabs) {
		if (entry.tab == Tabs().SelectedItem()) {
			return entry.viewer;
		}
	}
	return nullptr;
}

void MainWindow::rebuildShortcuts() {
	const auto& bindings =
	    m_settingsStore ? m_settingsStore->get().keybinds : m_settings.keybinds;
	const helpers::KeybindsHelper keybinds(bindings, "win32");
	auto shortcuts = helpers::buildShortcuts(keybinds, shortcutKey);
	// browser key names share characters with the numeric keypad
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mapvirtualkeyexw
	for (unsigned key = VK_NUMPAD0; key <= VK_DIVIDE; ++key) {
		const auto character =
		    MapVirtualKeyExW(key, MAPVK_VK_TO_CHAR, GetKeyboardLayout(0));
		if (!character || character > 0xffff) {
			continue;
		}
		const wchar_t text[] = {static_cast<wchar_t>(character), 0};
		const auto name = winrt::to_string(text);
		const auto aliases = helpers::buildShortcuts(
		    keybinds, [key, name](const std::string& stored) {
			    return stored == name ? key : 0;
		    });
		shortcuts.insert(shortcuts.end(), aliases.begin(), aliases.end());
	}
	m_shortcuts = std::move(shortcuts);
	m_shortcutBindings = bindings;
}

void MainWindow::setupTabKeys() {
	rebuildShortcuts();
	RootGrid().PreviewKeyDown([weak = get_weak()](auto&&, const auto& args) {
		auto self = weak.get();
		if (!self || self->m_closing || args.Handled()) {
			return;
		}
		const auto held = [](int key) {
			return (GetKeyState(key) & 0x8000) != 0;
		};
		const unsigned modifiers =
		    (held(VK_CONTROL) ? helpers::SHORTCUT_CTRL : 0) |
		    (held(VK_MENU) ? helpers::SHORTCUT_ALT : 0) |
		    (held(VK_SHIFT) ? helpers::SHORTCUT_SHIFT : 0) |
		    (held(VK_LWIN) || held(VK_RWIN) ? helpers::SHORTCUT_WIN : 0);
		const auto key = static_cast<unsigned>(args.Key());
		args.Handled(
		    self->routeKey(key, modifiers, args.KeyStatus().WasKeyDown));
	});
	RootGrid().KeyboardAcceleratorPlacementMode(
	    Input::KeyboardAcceleratorPlacementMode::Hidden);
}

void MainWindow::dispatchShortcut(const helpers::Shortcut& shortcut) {
	const auto& action = shortcut.action;
	const auto data = shortcut.data.value_or("");
	if (action == "openNewPDF") {
		pickDocuments();
	} else if (action == "reopen-tab") {
		reopenTab();
	} else if (Viewer() || nightRunSelected()) {
		const int current = Tabs().SelectedIndex();
		if (action == "close-tab") {
			closeTab(Tabs().SelectedItem().as<Controls::TabViewItem>());
		} else if (action == "switch-tab" &&
		           (data == "next" || data == "prev")) {
			selectTabPosition(current + (data == "next" ? 1 : -1));
		} else if (action == "move-tab") {
			if (data == "next" || data == "prev") {
				moveTab(current + (data == "next" ? 1 : -1));
			} else if (data == "start" || data == "end") {
				moveTab(data == "start"
				            ? 0
				            : static_cast<int>(Tabs().TabItems().Size()) - 1);
			}
		}
	}
}

void MainWindow::selectTab() {
	if (m_closing || m_movingTab || m_dragSelection) {
		return;
	}
	const auto viewer = Viewer();
	const bool hasDocument = viewer && get_self<ViewerPage>(viewer)->m_layout;
	const bool gameSelected = nightRunSelected();
	const bool hasContent = viewer || gameSelected;
	if (m_saveCopyCancel && m_saveCopyViewer.get() != viewer) {
		m_saveCopyCancel->store(true);
	}
	if (m_presentationViewer && m_presentationViewer != viewer) {
		endPresentation();
	}
	Nav().Visibility(hasContent ? Visibility::Collapsed : Visibility::Visible);
	Documents().Visibility(hasContent ? Visibility::Visible
	                                  : Visibility::Collapsed);
	Tabs().Visibility(hasContent ? Visibility::Visible : Visibility::Collapsed);
	if (m_nightRun) {
		const bool showingGame =
		    gameSelected &&
		    m_nightRun->element().Visibility() == Visibility::Collapsed;
		m_nightRun->element().Visibility(gameSelected ? Visibility::Visible
		                                              : Visibility::Collapsed);
		if (!gameSelected) {
			m_nightRun->pause();
		} else if (showingGame) {
			DispatcherQueue().TryEnqueue([weak = get_weak()] {
				if (const auto self = weak.get();
				    self && self->nightRunSelected()) {
					self->keepFocus();
				}
			});
		}
	}
	Presets().Visibility(hasDocument ? Visibility::Visible
	                                 : Visibility::Collapsed);
	ResetZoomItem().IsEnabled(hasDocument);
	FindItem().IsEnabled(hasDocument);
	ZoomInItem().IsEnabled(hasDocument);
	ZoomOutItem().IsEnabled(hasDocument);
	RotateClockwiseItem().IsEnabled(hasDocument);
	RotateCounterClockwiseItem().IsEnabled(hasDocument);
	PrintItem().IsEnabled(hasDocument && !m_printing);
	SaveCopyItem().IsEnabled(hasDocument && !m_saveCopyCancel);
	PropertiesItem().IsEnabled(hasDocument);
	PresentationItem().IsEnabled(hasDocument);
	updateCursorToolMenu();
	updateLayoutMenu();
	DarkConfigurator().Visibility(Visibility::Collapsed);
	updateTitleBar();
	for (auto& entry : m_tabs) {
		const bool selected = entry.viewer == viewer;
		if (!selected) {
			get_self<ViewerPage>(entry.viewer)->cancelFormRequest();
			get_self<ViewerPage>(entry.viewer)->stopPan(true);
			// its tool pane stays as it was, properties too: it's the tab's
		}
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

void MainWindow::removeTab(Controls::TabViewItem tab) {
	if (m_closing) {
		return;
	}
	const bool closingGame = m_nightRun && tab == m_nightRunTab;
	const bool restoreDocument = closingGame && nightRunSelected();
	uint32_t index = 0;
	if (closingGame) {
		m_nightRun->pause();
		if (Documents().Children().IndexOf(m_nightRun->element(), index)) {
			Documents().Children().RemoveAt(index);
		}
		m_nightRun.reset();
		m_nightRunTab = nullptr;
	} else {
		const auto found = std::find_if(
		    m_tabs.begin(), m_tabs.end(),
		    [&tab](const auto& entry) { return entry.tab == tab; });
		if (found == m_tabs.end()) {
			return;
		}
		if (found->viewer == m_presentationViewer) {
			endPresentation();
		}
		// port of app/helpers/file.ts close handler (L125-132)
		m_closedTabs.push_back(found->path);
		get_self<ViewerPage>(found->viewer)->close();
		if (Documents().Children().IndexOf(found->viewer, index)) {
			Documents().Children().RemoveAt(index);
		}
		m_tabs.erase(found);
	}
	auto items = Tabs().TabItems();
	if (items.IndexOf(tab, index)) {
		// select by item: the next PDF can inherit the game's old index
		if (restoreDocument && items.Size() > 1) {
			const auto next = index + 1 < items.Size() ? index + 1 : index - 1;
			Tabs().SelectedItem(items.GetAt(next));
		}
		items.RemoveAt(index);
	}
	selectTab();
	// the removed tab may have had the focus
	DispatcherQueue().TryEnqueue([weak = get_weak()] {
		if (const auto self = weak.get()) {
			self->keepFocus();
		}
	});
}

// With nothing focused no key reaches routeKey, so the shortcuts stop until
// something is clicked. Closing the focused tab leaves it like that when the
// next tab only shows an error, and so does a window that became active
// after its document opened. The pages take the focus, or else the tab.
void MainWindow::keepFocus() {
	const auto root = RootGrid().XamlRoot();
	if (m_closing || !root || Input::FocusManager::GetFocusedElement(root)) {
		return;
	}
	if (const auto viewer = Viewer();
	    viewer && get_self<ViewerPage>(viewer)->takeFocus()) {
		return;
	}
	if (nightRunSelected() && m_nightRun->focus()) {
		return;
	}
	if (const auto tab =
	        Tabs().SelectedItem().try_as<Controls::TabViewItem>()) {
		tab.Focus(FocusState::Programmatic);
		return;
	}
	// Home has no tab to hold the focus, so its Open button keeps the
	// shortcuts alive after the last tab closes or on a fresh start.
	if (Home().Visibility() == Visibility::Visible) {
		get_self<HomePage>(Home())->OpenButton().Focus(
		    FocusState::Programmatic);
	}
}

// Organize, Export, Fill & Sign and Comment work on the open PDF. With
// none open they ask for one and start once it has loaded.
void MainWindow::viewerCommand(std::wstring_view command) {
	if (m_closing) {
		return;
	}
	if (command == L"save") {
		onSaveCopy(nullptr, nullptr);
	} else if (command == L"print") {
		onPrint(nullptr, nullptr);
	} else if (command == L"properties") {
		onDocumentProperties(nullptr, nullptr);
	} else if (command == L"presentation") {
		onPresentation(nullptr, nullptr);
	} else if (command == L"combine") {
		openTool(L"combine");
	}
}

void MainWindow::openTool(std::wstring_view tool) {
	if (m_closing) {
		return;
	}
	// Combine starts from its own list of files.
	if (tool == L"combine") {
		combinePdfs();
		return;
	}
	if (const auto viewer = Viewer()) {
		get_self<ViewerPage>(viewer)->requestTool(tool);
		return;
	}
	m_toolNext = tool;
	pickDocuments();
}

// the history holds paths, not viewers, passwords or old reading positions
void MainWindow::reopenTab() {
	if (m_closing || !m_engine || m_closedTabs.empty()) {
		return;
	}
	auto path = std::move(m_closedTabs.back());
	m_closedTabs.pop_back();
	openDocument(*m_engine, path, std::nullopt);
}

void MainWindow::selectTabPosition(int index) {
	if (!m_closing && index >= 0 &&
	    index < static_cast<int>(Tabs().TabItems().Size())) {
		Tabs().SelectedIndex(index);
	}
}

void MainWindow::moveTab(int index) {
	auto items = Tabs().TabItems();
	const int current = Tabs().SelectedIndex();
	if (m_closing || current < 0 || index == current || index < 0 ||
	    index >= static_cast<int>(items.Size())) {
		return;
	}
	const auto selected = Tabs().SelectedItem();
	// only the strip changes order; the viewers stay mounted. Don't activate
	// an intervening tab (and its password prompt) while I move the header.
	m_movingTab = true;
	try {
		items.RemoveAt(static_cast<uint32_t>(current));
#ifdef SABLE_SURFACE_TEST
		if (m_shellFault) {
			m_shellFault(L"move");
		}
#endif
		items.InsertAt(static_cast<uint32_t>(index), selected);
		Tabs().SelectedItem(selected);
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error, "Couldn't move tab");
		try {
			uint32_t found = 0;
			if (!items.IndexOf(selected, found)) {
				items.InsertAt(
				    (std::min)(static_cast<uint32_t>(current), items.Size()),
				    selected);
			}
			Tabs().SelectedItem(selected);
		} catch (...) {
			diagnostics::caught(diagnostics::Level::Error,
			                    "Couldn't restore moved tab");
		}
		showShellError(L"Couldn't move the tab", L"Try moving it again.");
	}
	m_movingTab = false;
	selectTab();
}

void MainWindow::beginTabDrag() {
	if (!m_closing) {
		m_dragSelection = Tabs().SelectedItem();
	}
}

void MainWindow::endTabDrag() {
	if (m_closing) {
		m_dragSelection = nullptr;
		return;
	}
	try {
		uint32_t index = 0;
#ifdef SABLE_SURFACE_TEST
		if (m_shellFault) {
			m_shellFault(L"drag");
		}
#endif
		if (m_dragSelection &&
		    Tabs().TabItems().IndexOf(m_dragSelection, index)) {
			Tabs().SelectedItem(m_dragSelection);
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Couldn't finish tab drag");
		showShellError(L"Couldn't finish moving the tab",
		               L"Select the tab and try again.");
	}
	m_dragSelection = nullptr;
	selectTab();
}

void MainWindow::updateTabWidths() {
	// cap the whole tab, including its close button, in logical pixels
	const double limit = RootGrid().ActualWidth() * 0.15;
	for (const auto& entry : m_tabs) {
		entry.tab.MaxWidth(limit);
	}
}

std::filesystem::path MainWindow::settingsPath() {
	return configFile();
}

void MainWindow::setupEdit() {
	MainMenu().Opening([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->beginEditMenu();
			self->updateCursorToolMenu();
			self->updateLayoutMenu();
		}
	});
	MainMenu().Closing([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->m_editClosing = true;
			if (const auto box = self->editTarget()) {
				self->m_editSelectionStart = box.SelectionStart();
				self->m_editSelectionLength = box.SelectionLength();
			}
		}
	});
	MainMenu().Closed([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			const auto generation = self->m_editGeneration;
			// Item Click may run after Closed; keep the target until it has.
			self->DispatcherQueue().TryEnqueue([weak, generation] {
				if (auto owner = weak.get();
				    owner && owner->m_editGeneration == generation) {
					owner->endEditMenu();
				}
			});
		}
	});
}

void MainWindow::setupMusic() {
	m_music = music::Pane::attach(
	    MusicButton(),
	    [weak = get_weak()] {
		    if (auto self = weak.get(); self && !self->m_closing) {
			    self->updateTitleBar();
			    self->AppTitleBar().RecomputeDragRegions();
		    }
	    }
#ifdef SABLE_SURFACE_TEST
	    ,
	    false // Native probes inject state without controlling a real player.
#endif
	);
}

Controls::TextBox MainWindow::editTarget() {
	const auto box = m_editTarget.get();
	if (!box || !box.IsLoaded() || !box.IsEnabled() ||
	    box.XamlRoot() != RootGrid().XamlRoot()) {
		return nullptr;
	}
	DependencyObject element = box;
	while (element) {
		if (const auto ui = element.try_as<UIElement>();
		    ui && ui.Visibility() != Visibility::Visible) {
			return nullptr;
		}
		if (element == RootGrid()) {
			return box;
		}
		element = Media::VisualTreeHelper::GetParent(element);
	}
	return nullptr;
}

void MainWindow::beginEditMenu() {
	const auto focused =
	    Input::FocusManager::GetFocusedElement(RootGrid().XamlRoot());
	++m_editGeneration;
	commitMenuEdit();
	m_editClosing = false;
	if (const auto box = focused.try_as<Controls::TextBox>()) {
		m_editTarget = make_weak(box);
		if (const auto viewer = Viewer()) {
			get_self<ViewerPage>(viewer)->holdPageEdit(true);
			m_editViewer = make_weak(viewer);
		}
	} else if (const auto viewer = Viewer();
	           viewer && get_self<ViewerPage>(viewer)->hasTextSelection()) {
		m_editPdf = make_weak(viewer);
	}
	updateEditMenu();
}

void MainWindow::endEditMenu() {
	if (m_closing) {
		return;
	}
	if (const auto box = editTarget()) {
		box.Focus(FocusState::Programmatic);
		box.Select(m_editSelectionStart, m_editSelectionLength);
	}
	if (const auto pdf = m_editPdf.get(); pdf && pdf == Viewer()) {
		get_self<ViewerPage>(pdf)->Scroller().Focus(FocusState::Programmatic);
	}
	commitMenuEdit();
}

void MainWindow::commitMenuEdit() {
	if (const auto viewer = m_editViewer.get()) {
		get_self<ViewerPage>(viewer)->holdPageEdit(false);
	}
	m_editTarget = nullptr;
	m_editViewer = nullptr;
	m_editPdf = nullptr;
}

void MainWindow::updateEditMenu() {
	const auto box = editTarget();
	const bool writable = box && !box.IsReadOnly();
	const bool selection = box && box.SelectionLength() > 0;
	UndoItem().IsEnabled(writable && box.CanUndo());
	RedoItem().IsEnabled(writable && box.CanRedo());
	CutItem().IsEnabled(writable && selection);
	const auto pdf = m_editPdf.get();
	CopyItem().IsEnabled(selection ||
	                     (pdf && pdf == Viewer() &&
	                      get_self<ViewerPage>(pdf)->canCopySelection()));
	bool paste = false;
	if (writable) {
		try {
			using namespace Windows::ApplicationModel::DataTransfer;
			paste =
			    Clipboard::GetContent().Contains(StandardDataFormats::Text());
		} catch (...) {
			diagnostics::caught(diagnostics::Level::Warning,
			                    "Couldn't inspect clipboard text availability");
		}
	}
	PasteItem().IsEnabled(paste);
}

void MainWindow::onEditCommand(const Windows::Foundation::IInspectable& sender,
                               const RoutedEventArgs&) {
	if (m_closing) {
		return;
	}
	const auto item = sender.try_as<Controls::MenuFlyoutItem>();
	const auto box = editTarget();
	if (!item || !item.IsEnabled()) {
		return;
	}
	if (unbox_value_or<hstring>(item.Tag(), L"") == L"find") {
		commitMenuEdit();
		if (const auto viewer = Viewer()) {
			get_self<ViewerPage>(viewer)->openFind();
		}
		return;
	}
	if (!box) {
		const auto pdf = m_editPdf.get();
		if (pdf && pdf == Viewer() &&
		    unbox_value_or<hstring>(item.Tag(), L"") == L"copy") {
			get_self<ViewerPage>(pdf)->copySelection();
		}
		return;
	}
	try {
		if (m_editClosing) {
			box.Select(m_editSelectionStart, m_editSelectionLength);
		}
		const auto action = unbox_value_or<hstring>(item.Tag(), L"");
		if (action == L"copy") {
			box.CopySelectionToClipboard();
		} else if (!box.IsReadOnly()) {
			if (action == L"cut") {
				box.CutSelectionToClipboard();
			} else if (action == L"paste") {
				box.PasteFromClipboard();
			} else if (action == L"undo" && box.CanUndo()) {
				box.Undo();
			} else if (action == L"redo" && box.CanRedo()) {
				box.Redo();
			}
		}
		m_editSelectionStart = box.SelectionStart();
		m_editSelectionLength = box.SelectionLength();
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Warning,
		                    "Couldn't complete the text edit command");
		PickerError().Title(L"Couldn't edit text");
		PickerError().Message(
		    L"Couldn't complete the edit command. Try again.");
		PickerError().IsOpen(true);
	}
}

void MainWindow::showMorty() {
	if (m_closing) {
		return;
	}
	Controls::InfoBar alert;
	alert.Style(Application::Current()
	                .Resources()
	                .Lookup(box_value(L"SableMortyAlertStyle"))
	                .as<Style>());
	diagnostics::write(diagnostics::Level::Info,
	                   winrt::to_string(alert.Message()));
	ScalarTransition fade;
	fade.Duration(MORTY_FADE);
	alert.OpacityTransition(fade);
	alert.IsOpen(true);
	// the close button takes just that one away
	alert.Closed([weak = get_weak()](const Controls::InfoBar& sender, auto&&) {
		if (const auto self = weak.get()) {
			self->removeMorty(sender);
		}
	});
	// a full pile lets its oldest go straight away
	std::size_t showing = 0;
	for (const auto& morty : m_morties) {
		showing += morty.fading ? 0 : 1;
	}
	if (showing >= MORTY_MAX) {
		for (const auto& morty : m_morties) {
			if (!morty.fading) {
				morty.timer.Stop();
				onMortyTick(morty.alert);
				break;
			}
		}
	}
	const auto now = std::chrono::steady_clock::now();
	auto fadeAt = now + MORTY_LIFE;
	if (!m_morties.empty()) {
		// parenthesised because windows.h defines a max macro
		fadeAt = (std::max)(fadeAt, m_morties.back().fadeAt + MORTY_GAP);
	}
	auto timer = DispatcherQueue().CreateTimer();
	timer.IsRepeating(false);
	timer.Interval(std::chrono::duration_cast<Windows::Foundation::TimeSpan>(
	    fadeAt - now));
	timer.Tick([weak = get_weak(), alert](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->onMortyTick(alert);
		}
	});
	MortyStack().Children().InsertAt(0, alert);
	m_morties.push_back({alert, fadeAt, timer});
	timer.Start();
}

// the first tick starts the fade, the second one removes the faded alert
void MainWindow::onMortyTick(Controls::InfoBar alert) {
	const auto found = std::find_if(
	    m_morties.begin(), m_morties.end(),
	    [&alert](const auto& morty) { return morty.alert == alert; });
	if (found == m_morties.end()) {
		return;
	}
	if (found->fading) {
		removeMorty(alert);
		return;
	}
	found->fading = true;
	alert.IsHitTestVisible(false);
	alert.Opacity(0);
	found->timer.Stop();
	found->timer.Interval(MORTY_FADE);
	found->timer.Start();
}

void MainWindow::removeMorty(Controls::InfoBar alert) {
	const auto found = std::find_if(
	    m_morties.begin(), m_morties.end(),
	    [&alert](const auto& morty) { return morty.alert == alert; });
	if (found == m_morties.end()) {
		return;
	}
	found->timer.Stop();
	m_morties.erase(found);
	uint32_t index = 0;
	if (MortyStack().Children().IndexOf(alert, index)) {
		MortyStack().Children().RemoveAt(index);
	}
}

void MainWindow::onPrint(const Windows::Foundation::IInspectable&,
                         const RoutedEventArgs&) {
	commitMenuEdit();
	const auto viewer = Viewer();
	if (m_closing) {
		return;
	}
	if (m_printing) {
		showShellError(L"Print already in progress",
		               L"Finish or cancel the current print job before "
		               L"printing again.");
		return;
	}
	if (!viewer) {
		return;
	}
	try {
		HWND owner{};
#ifdef SABLE_SURFACE_TEST
		if (m_shellFault) {
			m_shellFault(L"print");
		}
#endif
		const auto native = this->try_as<::IWindowNative>();
		check_bool(static_cast<bool>(native));
		check_hresult(native->get_WindowHandle(&owner));
		check_bool(owner != nullptr);
		m_printing = true;
		PrintItem().IsEnabled(false);
		get_self<ViewerPage>(viewer)->print(owner, [weak = get_weak()] {
			if (auto self = weak.get()) {
				self->m_printing = false;
				if (!self->m_closing) {
					const auto current = self->Viewer();
					self->PrintItem().IsEnabled(
					    current && get_self<ViewerPage>(current)->m_layout);
				}
			}
		});
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error, "Couldn't start print");
		m_printing = false;
		try {
			const auto current = Viewer();
			PrintItem().IsEnabled(!m_closing && current &&
			                      get_self<ViewerPage>(current)->m_layout);
		} catch (...) {
			diagnostics::caught(diagnostics::Level::Warning,
			                    "Couldn't restore Print command");
		}
		showShellError(L"Couldn't print", L"Try printing the document again.");
	}
}

void MainWindow::showShellError(const hstring& title,
                                const hstring& message) noexcept {
	try {
		if (!m_closing) {
			PickerError().Title(title);
			PickerError().Message(message);
			PickerError().IsOpen(true);
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Couldn't show window error");
	}
}

void MainWindow::updateLayoutMenu() {
	const auto current = Viewer();
	const auto* v = current ? get_self<ViewerPage>(current) : nullptr;
	const bool ready = v && v->m_layout;
	ScrollModeMenu().IsEnabled(ready);
	SpreadModeMenu().IsEnabled(ready);
	const auto scroll = v ? v->scrollMode() : helpers::ScrollMode::Vertical;
	const auto spread = v ? v->spreadMode() : helpers::SpreadMode::None;
	ScrollVerticalItem().IsChecked(scroll == helpers::ScrollMode::Vertical);
	ScrollHorizontalItem().IsChecked(scroll == helpers::ScrollMode::Horizontal);
	ScrollWrappedItem().IsChecked(scroll == helpers::ScrollMode::Wrapped);
	ScrollPageItem().IsChecked(scroll == helpers::ScrollMode::Page);
	SpreadNoneItem().IsChecked(spread == helpers::SpreadMode::None);
	SpreadOddItem().IsChecked(spread == helpers::SpreadMode::Odd);
	SpreadEvenItem().IsChecked(spread == helpers::SpreadMode::Even);
	ClickSpreadsItem().IsChecked(v && v->m_clickSpreads);
	ClickSpreadsItem().IsEnabled(ready && v->spreadClicksEnabled());
}
void MainWindow::onClickSpreads(const Windows::Foundation::IInspectable&,
                                const RoutedEventArgs&) {
	commitMenuEdit();
	if (const auto current = Viewer(); current && !m_closing) {
		auto* viewer = get_self<ViewerPage>(current);
		viewer->m_clickSpreads = ClickSpreadsItem().IsChecked();
		updateLayoutMenu();
	}
}
void MainWindow::onPageLayout(const Windows::Foundation::IInspectable& sender,
                              const RoutedEventArgs&) {
	commitMenuEdit();
	if (const auto current = Viewer(); current && !m_closing) {
		const auto item = sender.try_as<Controls::RadioMenuFlyoutItem>();
		if (!item) {
			return;
		}
		auto* v = get_self<ViewerPage>(current);
		const auto tag = unbox_value_or<hstring>(item.Tag(), L"");
		auto scroll = v->scrollMode();
		auto spread = v->spreadMode();
		if (tag == L"scroll:0") {
			scroll = helpers::ScrollMode::Vertical;
		} else if (tag == L"scroll:1") {
			scroll = helpers::ScrollMode::Horizontal;
		} else if (tag == L"scroll:2") {
			scroll = helpers::ScrollMode::Wrapped;
		} else if (tag == L"scroll:3") {
			scroll = helpers::ScrollMode::Page;
		} else if (tag == L"spread:0") {
			spread = helpers::SpreadMode::None;
		} else if (tag == L"spread:1") {
			spread = helpers::SpreadMode::Odd;
		} else if (tag == L"spread:2") {
			spread = helpers::SpreadMode::Even;
		} else {
			return;
		}
		v->setLayout(scroll, spread);
		updateLayoutMenu();
	}
}

void MainWindow::updateCursorToolMenu() {
	const auto viewer = Viewer();
	CursorToolMenu().IsEnabled(viewer &&
	                           get_self<ViewerPage>(viewer)->m_layout);
	const bool hand = viewer && get_self<ViewerPage>(viewer)->cursorTool() ==
	                                ViewerPage::CursorTool::Hand;
	HandToolItem().IsChecked(hand);
	SelectToolItem().IsChecked(viewer &&
	                           get_self<ViewerPage>(viewer)->m_activeTool ==
	                               ViewerPage::ActiveTool::Select);
}
void MainWindow::onCursorTool(const Windows::Foundation::IInspectable& sender,
                              const RoutedEventArgs&) {
	commitMenuEdit();
	if (const auto viewer = Viewer(); viewer && !m_closing) {
		const auto item = sender.try_as<Controls::RadioMenuFlyoutItem>();
		if (!item) {
			return;
		}
		get_self<ViewerPage>(viewer)->setCursorTool(
		    unbox_value_or<hstring>(item.Tag(), L"") == L"hand"
		        ? ViewerPage::CursorTool::Hand
		        : ViewerPage::CursorTool::Select);
		updateCursorToolMenu();
	}
}

void MainWindow::onDocumentProperties(const Windows::Foundation::IInspectable&,
                                      const RoutedEventArgs&) {
	commitMenuEdit();
	if (!m_closing) {
		if (auto viewer = Viewer()) {
			get_self<ViewerPage>(viewer)->showProperties();
		}
	}
}

void MainWindow::onOpenSettings(const Windows::Foundation::IInspectable&,
                                const RoutedEventArgs&) {
	commitMenuEdit();
	if (m_closing || !m_settingsStore) {
		return;
	}
	try {
		if (m_settingsWindow) {
			m_settingsWindow->window.Activate();
			return;
		}
		m_settingsWindow =
		    ::SettingsWindow::open(this->try_as<Window>(), *m_settingsStore,
		                           exeDir() / L"assets/icon.ico");
		m_settingsWindow->window.Closed([weak = get_weak()](auto&&, auto&&) {
			if (auto self = weak.get()) {
				self->m_settingsWindow.reset();
			}
		});
	} catch (...) {
		OutputDebugStringW(to_message().c_str());
		PickerError().Title(L"Couldn't open settings");
		PickerError().Message(L"Close the settings window and try again.");
		PickerError().IsOpen(true);
	}
}

// port of app/main/app.ts createWindow() (L135-148), just the window part.
// Electron's sizes are DIPs but AppWindow works in physical pixels.
void MainWindow::setupWindow() {
	auto appWindow = AppWindow();
	appWindow.Closing([weak = get_weak()](auto&&, const auto& args) {
		if (const auto self = weak.get(); self && !self->m_allowNoteClose) {
			const bool needsReview =
			    self->m_notesSaving ||
			    std::any_of(
			        self->m_tabs.begin(), self->m_tabs.end(),
			        [](const auto& entry) {
				        const auto pdf = get_self<ViewerPage>(entry.viewer);
				        return pdf->m_notesDirty || pdf->m_noteBusy ||
				               pdf->m_noteDialog || pdf->notePlacing() ||
				               pdf->m_noteSaving || pdf->pageOperationBusy();
			        });
			if (needsReview || self->m_closePending) {
				args.Cancel(true);
				self->closeWithNotes();
			}
		}
	});
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
	// i keep the home-screen startup choice too (docs/DECISIONS.md)
	appWindow.Resize({px(WIDTH), px(HEIGHT)});
	if (m_settings.general.MaximizeOnOpen) {
		m_presenter.Maximize();
	}
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
	const bool compact =
	    Presets().Visibility() == Visibility::Visible &&
	    AppTitleBar().ActualWidth() <
	        (MusicButton().Visibility() == Visibility::Visible ? 748 : 700);
	if (compact != m_compactTitle) {
		m_compactTitle = compact;
		// leave room for all four presets and the window buttons
		AppTitleBar().Title(compact ? L"" : L"Sable");
		AppTitleBar().IconSource(compact ? nullptr : m_titleIcon);
	}
}

void MainWindow::onResetZoom(const Windows::Foundation::IInspectable&,
                             const RoutedEventArgs&) {
	commitMenuEdit();
	if (auto viewer = Viewer()) {
		get_self<ViewerPage>(viewer)->zoomReset();
	}
}

void MainWindow::onRotateView(const Windows::Foundation::IInspectable& sender,
                              const RoutedEventArgs&) {
	commitMenuEdit();
	if (auto viewer = Viewer()) {
		const auto item = sender.as<Controls::MenuFlyoutItem>();
		get_self<ViewerPage>(viewer)->rotateView(
		    unbox_value_or<hstring>(item.Tag(), L"") == L"-1" ? -1 : 1);
	}
}

void MainWindow::onZoomIn(const Windows::Foundation::IInspectable&,
                          const RoutedEventArgs&) {
	commitMenuEdit();
	if (auto viewer = Viewer()) {
		get_self<ViewerPage>(viewer)->zoomIn();
	}
}

void MainWindow::onZoomOut(const Windows::Foundation::IInspectable&,
                           const RoutedEventArgs&) {
	commitMenuEdit();
	if (auto viewer = Viewer()) {
		get_self<ViewerPage>(viewer)->zoomOut();
	}
}

// View > Toggle Full Screen (Electron's togglefullscreen role)
void MainWindow::onToggleFullScreen(const Windows::Foundation::IInspectable&,
                                    const RoutedEventArgs&) {
	if (m_presentationViewer) {
		endPresentation();
		return;
	}
	commitMenuEdit();
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
	commitMenuEdit();
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
	commitMenuEdit();
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
		slider.IsThumbToolTipEnabled(true);
		slider.ThumbToolTipValueConverter(make<SliderToolTip>(spec));
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
	get_self<HomePage>(Home())->ShowPreset(m_preset, m_values);
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
		setPreset(preset);
	}
	if (wasActive || preset == "custom") {
		toggleConfigurator();
	}
}

void MainWindow::setPreset(const std::string& preset) {
	if (preset == m_preset) {
		return;
	}
	m_preset = preset;
	helpers::handlePresetChange(preset == "custom" ? "original" : preset,
	                            m_values);
	refreshSliders();
	updateFilter();
	paintPresetChips();
}

void MainWindow::paintPresetChips() {
	for (const auto& chip :
	     {DefaultPreset(), SepiaPreset(), RedeyePreset(), CustomPreset()}) {
		const bool active =
		    unbox_value<hstring>(chip.Tag()) == to_hstring(m_preset);
		appearance::presetChip(chip, active);
		Automation::AutomationProperties::SetItemStatus(
		    chip, active ? L"Selected" : L"");
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
