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
#include "../app/pch.h"
#include "../app/pdf/session.h"
#include "../app/App.xaml.h"
#include "../app/SettingsWindow.h"
#include "../app/MusicPane.h"
#include "../app/helpers/version.h"
#include "../app/helpers/logging.h"
#include "../app/MainWindow.xaml.h"
#include "../app/pdf/engine.h"
#include "../app/pdf/printing.h"
#include "surface_test.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>
#include <microsoft.ui.xaml.window.h>
#include <stdexcept>
#include <string_view>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
struct TabsTest {
	weak_ref<MainWindow> window;
	pdf::Engine& engine;
	App& app;
	TextBlock status;
	Border report;
	Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
	bool shortcuts = false;
	bool settingsOnly = false;
	bool filtersOnly = false;
	bool linksOnly = false;
	bool printOnly = false;
	bool finishOnly = false;
	bool editOnly = false;
	bool musicOnly = false;
	bool selectionOnly = false;
	bool findOnly = false;
	bool outlineOnly = false;
	bool attachmentsOnly = false;
	ListViewItem attachmentItem{nullptr};
	std::size_t outlinePage = 0;
	std::string copiedText;
	int copyCalls = 0;
	music::Snapshot musicSnapshot;
	Windows::Foundation::IAsyncOperation<
	    Windows::Storage::Streams::IRandomAccessStreamReference>
	    musicArt{nullptr};
	std::weak_ptr<music::Session> closedMusic;
	int editCopies = 0;
	int editCuts = 0;
	int step = 0;
	int ticks = 0;
	int dragStarts = 0;
	int dragFinishes = 0;
	int dragResult = 0;
	using DropView = Windows::ApplicationModel::DataTransfer::DataPackageView;
	Windows::Foundation::IAsyncOperation<DropView> dropSetup{nullptr};
	DropView dropData{nullptr};
	com_ptr<MainWindow> closedDropWindow;
	com_ptr<MainWindow> sizingWindow;
	com_ptr<MainWindow> peerWindow;
	std::filesystem::path settingsPath;
	std::unique_ptr<helpers::SettingsStore> uiStore;
	double peerZoom = 0;

	static Windows::Foundation::IAsyncOperation<DropView> makeDropData() {
		using namespace Windows::Storage;
		const auto pages = co_await StorageFile::GetFileFromPathAsync(
		    std::filesystem::absolute(L"tests/fixtures/pages.pdf").wstring());
		const auto invalid = co_await StorageFile::GetFileFromPathAsync(
		    std::filesystem::absolute(L"tests/fixtures/not_pdf.pdf").wstring());
		Windows::ApplicationModel::DataTransfer::DataPackage data;
		data.SetStorageItems(std::vector<IStorageItem>{pages, pages, invalid});
		co_return data.GetView();
	}

	double zoom = 0;
	bool positioned = false;
	com_ptr<ViewerPage> first;

	TabsTest(MainWindow& owner, pdf::Engine& pdf, App& application)
	    : window(owner.get_weak()), engine(pdf), app(application) {
		settingsPath =
		    std::filesystem::temp_directory_path() /
		    ("sable_settings_ui_" + std::to_string(GetCurrentProcessId()) +
		     "_" +
		     std::to_string(
		         std::chrono::steady_clock::now().time_since_epoch().count()) +
		     ".json");
		uiStore = std::make_unique<helpers::SettingsStore>(
		    settingsPath, helpers::versionString());
	}
	~TabsTest() {
		std::error_code ignored;
		std::filesystem::remove(settingsPath, ignored);
	}

	void focus(bool pageBox) {
		if (auto owner = window.get()) {
			if (auto current = owner->Viewer()) {
				auto v = get_self<ViewerPage>(current);
				if (pageBox) {
					v->PageBox().Focus(FocusState::Programmatic);
				} else {
					v->Scroller().Focus(FocusState::Programmatic);
				}
			}
		}
	}
	void clearBindings() {
		if (auto owner = window.get()) {
			owner->m_shortcuts.clear();
			if (auto tab = owner->Tabs().SelectedItem().try_as<TabViewItem>()) {
				tab.Focus(FocusState::Programmatic);
			}
		}
	}
	void check(bool condition,
	           const std::string& message = "tab check failed") {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}
	bool ready(ViewerPage& viewer) {
		return viewer.m_layout && !viewer.m_startPage && !viewer.m_settling &&
		       !viewer.m_landingAt;
	}
	void keybindSettings(SettingsWindow& settings) {
		settings.select("keybinds");
		check(settings.m_bindingScroll.Visibility() == Visibility::Visible);
		check(settings.m_keybinds.Children().Size() ==
		          uiStore->get().keybinds.size(),
		      "missing keybind rows");
		const auto snapshot = uiStore->get();
		settings.edit("OpenWindow", 0);
		check(!settings.m_saveBinding.IsEnabled());
		BYTE original[256]{};
		check(GetKeyboardState(original) != FALSE);
		struct RestoreKeyboard {
			BYTE* state;
			~RestoreKeyboard() {
				SetKeyboardState(state);
			}
		} restore{original};
		BYTE state[256]{};
		state[VK_CONTROL] = 0x80;
		state[VK_SHIFT] = 0x80;
		check(SetKeyboardState(state) != FALSE);
		check(settings.capture(VK_CONTROL, true));
		check(!settings.m_saveBinding.IsEnabled(), "modifier-only save");
		settings.capture(VK_SHIFT, true);
		settings.capture('Q', true);
		check(settings.m_saveBinding.IsEnabled(),
		      "capture did not enable save");
		settings.capture('Q', true, true);
		check(settings.m_capture.binding().key == "Q", "shifted letter");
		state[VK_SHIFT] = 0;
		check(SetKeyboardState(state) != FALSE);
		settings.capture(VK_SHIFT, false);
		settings.capture('Q', false);
		settings.capture('W', true);
		check(settings.m_capture.binding().key == "w", "stale released key");
		settings.capture('E', true);
		check(!settings.m_saveBinding.IsEnabled() &&
		          settings.m_captureText.Text() == L"Invalid",
		      "two-key capture");
		settings.capture(VK_ESCAPE, true);
		check(SetKeyboardState(original) != FALSE);
		check(uiStore->get() == snapshot, "cancel saved a binding");
		check(settings.m_overlay.Visibility() == Visibility::Collapsed &&
		      settings.m_capture.display().empty());
		settings.edit("OpenWindow", 1);
		settings.m_capture.press("Alt");
		settings.m_capture.press("F8");
		check(
		    settings.saveBinding("OpenWindow", 1, settings.m_capture.binding()),
		    "secondary binding save");
		settings.dismiss();
		check(uiStore->get().keybinds.at("OpenWindow").keybind.size() == 2);
		check(settings.saveBinding("OpenWindow", 0, std::nullopt));
		check(helpers::KeybindHelper::fromKeybind(
		          uiStore->get().keybinds.at("OpenWindow").keybind.front(),
		          "win32")
		              .toTrigger() == "Alt+F8",
		      "clear did not shift secondary");
		check(settings.saveBinding("OpenWindow", 0, std::nullopt));
		check(uiStore->get().keybinds.at("OpenWindow").keybind.empty());
		check(helpers::loadSettings(settingsPath, helpers::versionString()) ==
		          uiStore->get(),
		      "cleared binding not persisted");
		settings.edit("OpenWindow", 1);
		check(settings.saveBinding(
		    "OpenWindow", 1,
		    helpers::KeybindHelper::fromTrigger("Alt+F8", "win32")
		        .getKeybind()));
		settings.dismiss();
		check(uiStore->get().keybinds.at("OpenWindow").keybind.size() == 1,
		      "empty secondary did not become primary");
		check(uiStore->get().general == snapshot.general);
		auto numeric = uiStore->get();
		numeric.keybinds.at("OpenWindow").keybind =
		    helpers::KeybindHelper::keybindFromTriggerArray({"Alt+1", "Alt+Q"});
		// '+' is a key here, not a separator in a trigger string
		numeric.keybinds.at("OpenWindow").keybind[1].key = "+";
		auto reader = make_self<MainWindow>(numeric);
		check(helpers::matchShortcut(reader->m_shortcuts, VK_NUMPAD1,
		                             helpers::SHORTCUT_ALT) != nullptr,
		      "numeric keypad digit");
		check(helpers::matchShortcut(reader->m_shortcuts, VK_ADD,
		                             helpers::SHORTCUT_ALT) != nullptr,
		      "numeric keypad add");
		reader->Close();
		numeric.keybinds.at("OpenWindow").keybind =
		    helpers::KeybindHelper::keybindFromTriggerArray({"Alt+="});
		reader = make_self<MainWindow>(numeric);
		check(helpers::matchShortcut(reader->m_shortcuts, VK_ADD,
		                             helpers::SHORTCUT_ALT) == nullptr,
		      "numeric keypad add matched equals");
		reader->Close();
	}
	void filterValues(MainWindow& owner,
	                  const helpers::SliderValues& expected) {
		check(owner.m_values == expected, "preset values");
		for (const auto& entry : owner.m_tabs) {
			check(get_self<ViewerPage>(entry.viewer)->m_values == expected,
			      "tab filter values");
		}
		const double values[] = {expected.brightness, expected.grayness,
		                         expected.inversion,  expected.sepia,
		                         expected.hue,        expected.extraBrightness};
		for (std::size_t i = 0; i < helpers::SLIDERS.size(); ++i) {
			const auto& spec = helpers::SLIDERS[i];
			const auto slider = owner.m_sliders[i];
			check(slider.Minimum() == spec.min &&
			          slider.Maximum() == spec.max &&
			          slider.StepFrequency() == spec.step &&
			          slider.Value() == values[i],
			      "slider range/value");
			check(slider.IsThumbToolTipEnabled(), "disabled slider tooltip");
			const auto text = slider.ThumbToolTipValueConverter().Convert(
			    box_value(values[i]), {}, nullptr, L"");
			check(unbox_value<hstring>(text) ==
			          to_hstring(helpers::sliderTooltip(spec, values[i])),
			      "slider tooltip");
		}
	}
	void filterTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current || !ready(*get_self<ViewerPage>(current))) {
			return;
		}
		switch (step) {
		case 0: {
			check(owner.Presets().Visibility() == Visibility::Visible &&
			          owner.m_sliders.size() == 6,
			      "filter controls");
			filterValues(owner, {});
			const struct {
				Button button;
				helpers::SliderValues values;
			} presets[] = {{owner.SepiaPreset(), {0, 0, 25, 100, 0, -30}},
			               {owner.RedeyePreset(), {8, 100, 92, 100, 295, -6}},
			               {owner.DefaultPreset(), {7, 95, 95, 55, 180, 0}}};
			for (const auto& preset : presets) {
				owner.onPresetClick(preset.button, {});
				filterValues(owner, preset.values);
				for (const auto& chip :
				     {owner.DefaultPreset(), owner.SepiaPreset(),
				      owner.RedeyePreset(), owner.CustomPreset()}) {
					check(
					    Automation::AutomationProperties::GetItemStatus(chip) ==
					        (chip == preset.button ? L"Selected" : L""),
					    "preset selection");
				}
				check(owner.DarkConfigurator().Visibility() ==
				          Visibility::Collapsed,
				      "inactive preset opened configurator");
				owner.onPresetClick(preset.button, {});
				check(owner.DarkConfigurator().Visibility() ==
				          Visibility::Visible,
				      "active preset did not open configurator");
				owner.m_sliders[0].Value(42);
				auto edited = preset.values;
				edited.brightness = 42;
				filterValues(owner, edited);
				owner.onPresetClick(preset.button, {});
				check(owner.DarkConfigurator().Visibility() ==
				          Visibility::Collapsed,
				      "active preset did not close configurator");
				filterValues(owner, edited);
			}
			owner.onPresetClick(owner.CustomPreset(), {});
			filterValues(owner, {0, 0, 0, 0, 0, 0});
			check(owner.DarkConfigurator().Visibility() == Visibility::Visible);
			for (std::size_t i = 0; i < owner.m_sliders.size(); ++i) {
				owner.m_sliders[i].Value(helpers::SLIDERS[i].max);
			}
			filterValues(owner, {100, 100, 100, 100, 360, 200});
			for (std::size_t i = 0; i < owner.m_sliders.size(); ++i) {
				owner.m_sliders[i].Value(helpers::SLIDERS[i].min);
			}
			filterValues(owner, {0, 0, 0, 0, 0, -100});
			check(owner.m_preset == "custom",
			      "slider changed preset selection");
			owner.onPresetClick(owner.CustomPreset(), {});
			filterValues(owner, {0, 0, 0, 0, 0, -100});
			owner.openDocument(engine, L"tests/fixtures/restricted.pdf", {});
			break;
		}
		case 1:
			check(owner.m_tabs.size() == 3, "new filter tab");
			filterValues(owner, {0, 0, 0, 0, 0, -100});
			peerWindow = app.createWindow();
			check(peerWindow->m_values == helpers::SliderValues{} &&
			          peerWindow->m_preset == "default",
			      "window filter isolation");
			peerWindow->Close();
			owner.onPresetClick(owner.DefaultPreset(), {});
			filterValues(owner, {});
			owner.onPresetClick(owner.DefaultPreset(), {});
			check(owner.DarkConfigurator().Visibility() == Visibility::Visible);
			owner.onHideConfigurator({}, nullptr);
			check(owner.DarkConfigurator().Visibility() ==
			      Visibility::Collapsed);
			owner.m_presenter.Restore();
			owner.AppWindow().Resize({565, 700});
			break;
		case 2:
			check(owner.Presets().ActualWidth() > 0, "narrow preset bar");
			owner.onPresetClick(owner.DefaultPreset(), {});
			break;
		case 3: {
			const auto panel = owner.DarkConfigurator();
			if (panel.ActualWidth() <= 0) {
				return;
			}
			check(panel.ActualWidth() == 200,
			      ("configurator width " + std::to_string(panel.ActualWidth()) +
			       ", visibility " +
			       std::to_string(static_cast<int>(panel.Visibility())))
			          .c_str());
			const auto origin = panel.TransformToVisual(owner.RootGrid())
			                        .TransformPoint({0, 0});
			check(origin.X >= 0 && origin.X + panel.ActualWidth() <=
			                           owner.RootGrid().ActualWidth() + 1,
			      "configurator outside window");
			while (!owner.m_tabs.empty()) {
				owner.closeTab(owner.m_tabs.back().tab);
			}
			check(owner.Presets().Visibility() == Visibility::Collapsed &&
			          panel.Visibility() == Visibility::Collapsed,
			      "home filter controls");
			status.Text(
			    L"PASS: preset toggles, slider ranges/tooltips, shared "
			    L"filters, new tabs, window isolation and narrow layout");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		}
		++step;
		ticks = 0;
	}
	void linkTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& viewer = *get_self<ViewerPage>(current);
		if ((step != 10 && !ready(viewer)) || viewer.m_linkPending) {
			return;
		}
		const auto click = [&viewer](std::size_t page, double x, double y) {
			const auto& rect = viewer.m_layout->pages()[page];
			viewer.requestLink({static_cast<float>(rect.x + x * rect.width),
			                    static_cast<float>(rect.y + y * rect.height)});
		};
		switch (step) {
		case 0:
			owner.m_presenter.Restore();
			owner.AppWindow().Resize({1000, 800});
			check(viewer.setScale(1.5), "link display scale");
			click(0, 80.0 / 360, 50.0 / 540);
			break;
		case 1:
			check(viewer.m_launchedUri == "https://example.com/sable?x=1#part",
			      "external PDF link");
			viewer.m_launchedUri.clear();
			click(0, 80.0 / 360, 130.0 / 540);
			break;
		case 2: {
			check(std::abs(viewer.m_zoom - 2) < 0.001 && viewer.m_current == 1,
			      "XYZ page and zoom");
			const auto& rect = viewer.m_layout->pages()[1];
			const double expected =
			    (std::min)(viewer.imageLeft() + rect.x + 0.8 * rect.width,
			               viewer.Scroller().ScrollableWidth());
			check(std::abs(viewer.Scroller().HorizontalOffset() - expected) < 1,
			      "rotated XYZ horizontal destination");
			click(1, 0.28, 0.2);
			break;
		}
		case 3:
			check(viewer.m_launchedUri == "HTTP://example.com/rotated",
			      "rotated external PDF link");
			viewer.m_launchedUri.clear();
			click(1, 0.28, 160.0 / 300);
			break;
		case 4:
			check(viewer.m_current == 0 && std::abs(viewer.m_zoom - 2) < 0.001,
			      "null XYZ zoom");
			click(0, 80.0 / 360, 210.0 / 540);
			break;
		case 5:
			check(viewer.m_current == 1 &&
			          viewer.m_preset == helpers::ZoomPreset::PageFit,
			      "Fit destination");
			click(0, 80.0 / 360, 290.0 / 540);
			break;
		case 6:
			check(viewer.m_preset == helpers::ZoomPreset::PageWidth,
			      "named FitH destination");
			click(1, 0.5, 0.2);
			break;
		case 7:
			check(viewer.m_current == 0 &&
			          viewer.m_preset == helpers::ZoomPreset::None,
			      "FitR destination");
			zoom = viewer.m_zoom;
			click(0, 80.0 / 360, 370.0 / 540);
			break;
		case 8:
			check(viewer.m_launchedUri.empty() && viewer.m_zoom == zoom &&
			          viewer.m_current == 0,
			      "unsupported action navigated");
			viewer.requestLink({-1, -1});
			check(!viewer.m_linkPending, "page gap queried");
			click(0, 80.0 / 360, 50.0 / 540);
			viewer.Visibility(Visibility::Collapsed);
			break;
		case 9:
			check(viewer.m_launchedUri.empty(), "hidden viewer launched link");
			viewer.Visibility(Visibility::Visible);
			// Even a result already queued for the UI must not act after close.
			click(0, 80.0 / 360, 50.0 / 540);
			viewer.close();
			break;
		case 10:
			check(viewer.m_launchedUri.empty(), "closed viewer launched link");
			status.Text(
			    L"PASS: PDF links, rotated/cropped hit tests, XYZ, Fit, "
			    L"named FitH, FitR, unsupported actions and close");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void printTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& viewer = *get_self<ViewerPage>(current);
		if (!ready(viewer) || owner.m_printing) {
			return;
		}
		switch (step) {
		case 0: {
			check(owner.PrintItem().IsEnabled(),
			      "print disabled with document");
			bool shortcut = false;
			for (const auto& key : owner.RootGrid().KeyboardAccelerators()) {
				shortcut |= key.Key() == Windows::System::VirtualKey::P &&
				            key.Modifiers() ==
				                Windows::System::VirtualKeyModifiers::Control;
			}
			check(shortcut, "missing fixed print accelerator");
			int calls = 0;
			viewer.m_printDialog = [&](void* handle, int count, int page,
			                           const std::wstring& title) {
				HWND expected{};
				check_hresult(
				    owner.get_strong().as<::IWindowNative>()->get_WindowHandle(
				        &expected));
				check(handle == expected && count == 1 && page == 0 &&
				          title == L"print.pdf",
				      "print dialog active-document snapshot");
				++calls;
				owner.onPrint({}, {});
				return pdf::PrintDialogResult{};
			};
			owner.onPrint({}, {});
			viewer.m_printDialog = {};
			check(calls == 1 && !owner.m_printing &&
			          owner.PrintItem().IsEnabled(),
			      "print cancel/reentry");
			owner.selectTabPosition(0);
			break;
		}
		case 1:
			check(viewer.m_current == 1, "print current page setup");
			viewer.m_printDialog = [](void*, int count, int page,
			                          const std::wstring& title) {
				if (count != 3 || page != 1 || title != L"pages.pdf") {
					throw std::runtime_error("print switched tab");
				}
				return pdf::PrintDialogResult{{}, true};
			};
			owner.onPrint({}, {});
			check(!owner.m_printing && viewer.Message().IsOpen(),
			      "print dialog failure");
			check(viewer.Scroller().Visibility() == Visibility::Visible &&
			          viewer.Rail().Visibility() == Visibility::Visible &&
			          viewer.Message().IsClosable(),
			      "print failure hid the document");
			viewer.Message().IsOpen(false);
			viewer.m_printDialog = [](void*, int, int, const std::wstring&) {
				pdf::PrintJob job;
				job.device = std::shared_ptr<void>(reinterpret_cast<void*>(1),
				                                   [](void*) {});
				job.pages = {
				    -1}; // fails before any GDI call; no physical printer job
				return pdf::PrintDialogResult{std::move(job), false};
			};
			owner.onPrint({}, {});
			check(owner.m_printing && !owner.PrintItem().IsEnabled(),
			      "print job busy state");
			break;
		case 2:
			check(viewer.Message().IsOpen() && owner.PrintItem().IsEnabled(),
			      "print job failure/reset");
			check(viewer.Scroller().Visibility() == Visibility::Visible,
			      "spool failure hid the document");
			viewer.Message().IsOpen(false);
			viewer.openLink("");
			check(viewer.Message().IsOpen() && viewer.Message().IsClosable() &&
			          viewer.Scroller().Visibility() == Visibility::Visible,
			      "invalid browser link hid the document");
			viewer.m_printDialog = {};
			while (!owner.m_tabs.empty()) {
				owner.closeTab(owner.m_tabs.back().tab);
			}
			check(!owner.PrintItem().IsEnabled(), "print enabled on home");
			check(owner.Tabs().Visibility() == Visibility::Collapsed &&
			          owner.Nav().Visibility() == Visibility::Visible &&
			          owner.Documents().Visibility() == Visibility::Collapsed,
			      "last-tab home state");
			owner.onPrint({}, {});
			check(!owner.m_printing, "print without document");
			status.Text(L"PASS: print menu/accelerator, active tab/current "
			            L"page, cancel, reentry, failures and home");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void finishTick(MainWindow& owner) {
		check(Application::Current().RequestedTheme() ==
		              ApplicationTheme::Dark &&
		          owner.RootGrid().ActualTheme() == ElementTheme::Dark,
		      "reader did not force dark theme");
		const auto xml = notices::Service::notification().Payload();
		const std::wstring_view payload(xml.c_str(), xml.size());
		check(payload.find(notices::TITLE) != std::wstring_view::npos &&
		          payload.find(notices::BODY) != std::wstring_view::npos,
		      "notification payload changed");
		check(!owner.m_easterEgg, "notification enabled without opt-in");
		owner.showEasterEgg();
		int calls = 0;
		const auto before = owner.RootGrid().KeyboardAccelerators().Size();
		owner.setEasterEgg([&calls] { ++calls; });
		owner.setEasterEgg([&calls] { ++calls; });
		check(owner.RootGrid().KeyboardAccelerators().Size() == before + 1,
		      "duplicate notification accelerator");
		const auto key = owner.RootGrid().KeyboardAccelerators().GetAt(before);
		check(key.Key() == Windows::System::VirtualKey::T &&
		          key.Modifiers() ==
		              (Windows::System::VirtualKeyModifiers::Shift |
		               Windows::System::VirtualKeyModifiers::Menu),
		      "notification shortcut");
		owner.showEasterEgg();
		check(calls == 1, "notification dispatch");
		owner.setEasterEgg({});
		owner.showEasterEgg();
		check(calls == 1, "disabled notification dispatch");
		check(!diagnostics::environmentEnabled(L"EASTER_EGG"),
		      "finish test requires EASTER_EGG unset");
		peerWindow = app.createWindow();
		check(!peerWindow->m_easterEgg,
		      "app enabled notification without environment flag");
		peerWindow->Close();
		SetEnvironmentVariableW(L"EASTER_EGG", L"0");
		try {
			peerWindow = app.createWindow();
		} catch (...) {
			SetEnvironmentVariableW(L"EASTER_EGG", nullptr);
			throw;
		}
		SetEnvironmentVariableW(L"EASTER_EGG", nullptr);
		check(static_cast<bool>(peerWindow->m_easterEgg),
		      "app ignored nonempty notification environment flag");
		peerWindow->Close();
		diagnostics::write(diagnostics::Level::Info, "native logging marker");
		const auto file =
		    std::filesystem::temp_directory_path() / L"Sable-probe-logs" /
		    (L"Sable-" + std::to_wstring(GetCurrentProcessId()) + L".log");
		std::ifstream input(file, std::ios::binary);
		const std::string log{std::istreambuf_iterator<char>(input), {}};
		check(log.find("native logging marker") != std::string::npos &&
		          log.find("Verbose logging enabled") != std::string::npos &&
		          log.find(to_string(notices::BODY)) != std::string::npos,
		      "startup/debug/notification file logging");
		status.Text(L"PASS: opt-in notification shortcut/payload and "
		            L"startup/debug/file logging");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
	}
	static Windows::Foundation::IAsyncOperation<
	    Windows::Storage::Streams::IRandomAccessStreamReference>
	makeMusicArtwork() {
		using namespace Windows::Graphics::Imaging;
		Windows::Storage::Streams::InMemoryRandomAccessStream stream;
		const auto encoder = co_await BitmapEncoder::CreateAsync(
		    BitmapEncoder::PngEncoderId(), stream);
		const std::array<std::uint8_t, 16> red{0, 0, 255, 255, 0, 0, 255, 255,
		                                       0, 0, 255, 255, 0, 0, 255, 255};
		encoder.SetPixelData(BitmapPixelFormat::Bgra8,
		                     BitmapAlphaMode::Straight, 2, 2, 96, 96, red);
		co_await encoder.FlushAsync();
		stream.Seek(0);
		co_return Windows::Storage::Streams::RandomAccessStreamReference::
		    CreateFromStream(stream);
	}
	void musicTick(MainWindow& owner) {
		auto& pane = *owner.m_music;
		auto& state = musicSnapshot.state;
		switch (step) {
		case 0: {
			check(owner.MusicButton().Visibility() == Visibility::Collapsed,
			      "music button without a session");
			state.player = music::Player::Spotify;
			state.playback = music::Playback::Playing;
			state.title = "A test track";
			state.artist = "A test artist";
			state.controls.previous = true;
			state.controls.toggle = true;
			pane.update(musicSnapshot);
			check(owner.MusicButton().Visibility() == Visibility::Visible &&
			          pane.m_title.Text() == L"A test track" &&
			          pane.m_artist.Text() == L"A test artist" &&
			          pane.m_player.Text() == L"Spotify",
			      "music metadata");
			check(pane.m_previous.IsEnabled() && pane.m_play.IsEnabled() &&
			          !pane.m_next.IsEnabled() && !pane.m_seek.IsEnabled() &&
			          !pane.m_shuffle.IsEnabled() && !pane.m_repeat.IsEnabled(),
			      "music capability gates");
			pane.m_flyout.ShowAt(owner.MusicButton());
			const auto closed = std::make_shared<bool>(false);
			auto session = music::Session::create(
			    owner.DispatcherQueue(), [closed](const auto&) {
				    if (*closed) {
					    probeFail(L"closed music session callback");
				    }
			    });
			session->close();
			*closed = true;
			closedMusic = session;
			musicArt = makeMusicArtwork();
			break;
		}
		case 1:
			check(pane.m_open && pane.m_timer.IsRunning(),
			      "music flyout did not open");
			check(closedMusic.expired(),
			      "music request retained a closed owner");
			state.timeline = {10 * music::TICKS_PER_SECOND,
			                  130 * music::TICKS_PER_SECOND,
			                  40 * music::TICKS_PER_SECOND,
			                  10 * music::TICKS_PER_SECOND,
			                  130 * music::TICKS_PER_SECOND,
			                  1,
			                  false};
			state.playback = music::Playback::Paused;
			state.shuffle = true;
			state.repeat = music::Repeat::One;
			pane.update(musicSnapshot);
			check(pane.m_seek.Maximum() == 120 && pane.m_seek.Value() == 30 &&
			          pane.m_elapsed.Text() == L"0:30" &&
			          pane.m_remaining.Text() == L"-1:30",
			      "read-only music timeline");
			check(!pane.m_seek.IsEnabled() && !pane.m_repeat.IsEnabled() &&
			          pane.m_shuffle.IsChecked().Value() &&
			          unbox_value<hstring>(pane.m_repeat.Content()) ==
			              L"Repeat: one",
			      "read-only shuffle/repeat state");
			check(Automation::AutomationProperties::GetName(pane.m_play) ==
			          L"Play",
			      "paused play label");
			state.controls.seek = state.controls.shuffle =
			    state.controls.repeat = true;
			pane.update(musicSnapshot);
			check(pane.m_seek.IsEnabled() && pane.m_shuffle.IsEnabled() &&
			          pane.m_repeat.IsEnabled(),
			      "supported music controls disabled");
			state.busy = true;
			pane.update(musicSnapshot);
			check(!pane.m_play.IsEnabled() && !pane.m_previous.IsEnabled() &&
			          !pane.m_seek.IsEnabled(),
			      "busy music controls");
			state.busy = false;
			break;
		case 2:
			if (musicArt.Status() ==
			    Windows::Foundation::AsyncStatus::Started) {
				return;
			}
			musicSnapshot.artwork = musicArt.GetResults();
			++musicSnapshot.artworkVersion;
			pane.update(musicSnapshot);
			break;
		case 3:
			if (!pane.m_art.Source() || pane.m_background.TintColor().R != 80 ||
			    pane.m_background.TintColor().G != 0) {
				return;
			}
			check(pane.m_placeholder.Visibility() == Visibility::Collapsed,
			      "music artwork placeholder");
			// Start another artwork read, then switch tracks before it
			// finishes.
			++musicSnapshot.artworkVersion;
			pane.update(musicSnapshot);
			musicSnapshot.artwork = nullptr;
			++musicSnapshot.artworkVersion;
			state.player = music::Player::AppleMusic;
			state.title = "Another track";
			pane.update(musicSnapshot);
			break;
		case 4:
			if (pane.m_background.TintColor().R != 45) {
				return;
			}
			check(!pane.m_art.Source() &&
			          pane.m_placeholder.Visibility() == Visibility::Visible &&
			          pane.m_player.Text() == L"Apple Music",
			      "stale music artwork");
			pane.m_flyout.Hide();
			owner.m_presenter.Restore();
			owner.AppWindow().Resize({565, 700});
			owner.openDocument(engine, L"tests/fixtures/pages.pdf", 1);
			break;
		case 5: {
			const auto current = owner.Viewer();
			if (!current || !ready(*get_self<ViewerPage>(current))) {
				return;
			}
			const auto music = owner.MusicButton();
			const auto position = music.TransformToVisual(owner.RootGrid())
			                          .TransformPoint({0, 0});
			check(music.ActualWidth() > 0 && position.X >= 0,
			      "narrow music button");
			for (const auto chip :
			     {owner.DefaultPreset(), owner.SepiaPreset(),
			      owner.RedeyePreset(), owner.CustomPreset()}) {
				const auto point = chip.TransformToVisual(owner.RootGrid())
				                       .TransformPoint({0, 0});
				check(chip.ActualWidth() > 0 &&
				          point.X >= position.X + music.ActualWidth() &&
				          point.X + chip.ActualWidth() <=
				              owner.RootGrid().ActualWidth(),
				      "music displaced a preset outside the narrow window");
			}
			while (!owner.m_tabs.empty()) {
				owner.closeTab(owner.m_tabs.back().tab);
			}
			check(owner.MusicButton().Visibility() == Visibility::Visible,
			      "last-tab close hid active music");
			musicSnapshot = {};
			pane.update(musicSnapshot);
			break;
		}
		case 6:
			check(owner.MusicButton().Visibility() == Visibility::Collapsed &&
			          !pane.m_open && !pane.m_timer.IsRunning(),
			      "inactive music did not dismiss");
			pane.close();
			musicSnapshot.state.player = music::Player::Spotify;
			musicSnapshot.state.playback = music::Playback::Playing;
			pane.update(musicSnapshot);
			check(owner.MusicButton().Visibility() == Visibility::Collapsed,
			      "closed music pane accepted an update");
			status.Text(L"PASS: music metadata, capability gates, timeline, "
			            L"artwork/tint, stale updates and close");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void editTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& viewer = *get_self<ViewerPage>(current);
		if (!ready(viewer)) {
			return;
		}
		const auto box = viewer.PageBox();
		switch (step) {
		case 0:
			check(!owner.MenuButton().AllowFocusOnInteraction(),
			      "menu pointer focus");
			check(box.Focus(FocusState::Programmatic), "page box focus");
			box.Text(L"3");
			box.SelectAll();
			box.CopyingToClipboard([this](auto&&, const auto& args) {
				++editCopies;
				args.Handled(true);
			});
			box.CuttingToClipboard([this](auto&&, const auto& args) {
				++editCuts;
				args.Handled(true);
			});
			owner.MainMenu().ShowAt(owner.MenuButton());
			break;
		case 1:
			viewer.showPosition();
			check(viewer.m_current == 1 && box.Text() == L"3" &&
			          viewer.m_pageEditHeld,
			      "menu committed the unfinished page edit");
			check(owner.CopyItem().IsEnabled() && owner.CutItem().IsEnabled(),
			      "selected text edit availability");
			owner.onEditCommand(owner.CopyItem(), {});
			owner.onEditCommand(owner.CutItem(), {});
			check(editCopies == 1 && editCuts == 1, "native copy/cut dispatch");
			box.IsReadOnly(true);
			owner.updateEditMenu();
			check(owner.CopyItem().IsEnabled() &&
			          !owner.CutItem().IsEnabled() &&
			          !owner.PasteItem().IsEnabled() &&
			          !owner.UndoItem().IsEnabled() &&
			          !owner.RedoItem().IsEnabled(),
			      "read-only edit commands");
			owner.onEditCommand(owner.CutItem(), {});
			check(editCuts == 1, "read-only cut ran");
			box.IsReadOnly(false);
			box.SelectedText(L"2");
			owner.updateEditMenu();
			check(owner.UndoItem().IsEnabled() == box.CanUndo(), "undo state");
			if (box.CanUndo()) {
				owner.onEditCommand(owner.UndoItem(), {});
				check(box.Text() == L"3", "native undo");
				owner.updateEditMenu();
				check(owner.RedoItem().IsEnabled(), "redo state");
				owner.onEditCommand(owner.RedoItem(), {});
				check(box.Text() == L"2", "native redo");
			}
			box.Select(1, 0);
			owner.MainMenu().Hide();
			break;
		case 2:
			check(!viewer.m_pageEditHeld &&
			          box.FocusState() != FocusState::Unfocused &&
			          box.Text() == L"2",
			      "menu did not restore the edit");
			check(box.SelectionStart() == 1 && box.SelectionLength() == 0,
			      "menu did not restore the caret: " +
			          std::to_string(box.SelectionStart()) + "/" +
			          std::to_string(box.SelectionLength()) + " captured " +
			          std::to_string(owner.m_editSelectionStart) + "/" +
			          std::to_string(owner.m_editSelectionLength));
			viewer.Scroller().Focus(FocusState::Programmatic);
			owner.MainMenu().ShowAt(owner.MenuButton());
			check(!owner.CopyItem().IsEnabled() &&
			          !owner.CutItem().IsEnabled() &&
			          !owner.PasteItem().IsEnabled() &&
			          !owner.UndoItem().IsEnabled() &&
			          !owner.RedoItem().IsEnabled(),
			      "non-text edit commands");
			owner.MainMenu().Hide();
			break;
		case 3:
			box.Focus(FocusState::Programmatic);
			box.Text(L"3");
			box.Select(1, 0);
			owner.MainMenu().ShowAt(owner.MenuButton());
			break;
		case 4:
			owner.MainMenu().Hide();
			break;
		case 5:
			check(!viewer.m_pageEditHeld && viewer.m_current == 1 &&
			          box.Text() == L"3" && box.SelectionStart() == 1 &&
			          box.SelectionLength() == 0,
			      "cancelled menu did not preserve the unfinished edit");
			owner.MainMenu().ShowAt(owner.MenuButton());
			break;
		case 6:
			owner.onResetZoom({}, {});
			check(!viewer.m_pageEditHeld && viewer.m_current == 2,
			      "non-edit menu action did not commit the draft");
			owner.MainMenu().Hide();
			break;
		case 7:
			box.Focus(FocusState::Programmatic);
			box.SelectAll();
			owner.MainMenu().ShowAt(owner.MenuButton());
			owner.closeTab(owner.m_tabs.front().tab);
			owner.onEditCommand(owner.CopyItem(), {});
			check(editCopies == 1, "closed tab received an edit command");
			owner.MainMenu().Hide();
			status.Text(
			    L"PASS: Edit menu target, draft/selection, native copy/cut, "
			    L"undo/redo state, read-only/non-text/closed targets");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}

	void attachmentsTick(MainWindow& owner) {
		if (step == 6) {
			check(first && first->m_closed && !first->m_attachments &&
			          !first->m_attachmentsPending &&
			          first->AttachmentList().Items().Size() == 0,
			      "closed attachment panel accepted a late result");
			status.Text(L"PASS: attachment names, cache, sidebar switching, "
			            L"empty/retry and close guards");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_attachmentsPending) {
			return;
		}
		switch (step) {
		case 0:
			check(!v.m_attachments, "attachments loaded before sidebar open");
			v.setAttachmentsOpen(true);
			check(v.attachmentsOpen() && !v.thumbnailsOpen() &&
			          !v.outlineOpen(),
			      "attachment sidebar exclusivity");
			break;
		case 1:
			if (!v.m_attachmentsBuilt) {
				return;
			}
			check(v.AttachmentList().Items().Size() == 4,
			      "attachment list count");
			attachmentItem =
			    v.AttachmentList().Items().GetAt(0).as<ListViewItem>();
			check(unbox_value<hstring>(attachmentItem.Content()) ==
			          L"notes \u03a9\U0001f600.txt",
			      "attachment Unicode name");
			v.AttachmentList().Focus(FocusState::Programmatic);
			if (!v.attachmentsHasFocus()) {
				return;
			}
			v.toggleSidebar();
			check(!v.attachmentsHasFocus(),
			      "closed attachment panel kept focus");
			v.toggleSidebar();
			check(v.attachmentsOpen(), "F4 forgot attachments");
			v.zoomIn();
			break;
		case 2:
			check(v.AttachmentList().Items().GetAt(0) == attachmentItem,
			      "zoom rebuilt cached attachments");
			v.setOutlineOpen(true);
			check(!v.attachmentsOpen() && v.outlineOpen(),
			      "outline did not close attachments");
			v.setAttachmentsOpen(true);
			v.setThumbnailsOpen(true);
			check(v.thumbnailsOpen() && !v.attachmentsOpen(),
			      "thumbnails did not close attachments");
			owner.openDocument(engine, L"tests/fixtures/pages.pdf", 1);
			break;
		case 3:
			check(!v.m_attachments && !v.attachmentsOpen(),
			      "attachment state leaked between tabs");
			v.setAttachmentsOpen(true);
			break;
		case 4:
			if (!v.m_attachmentsBuilt) {
				return;
			}
			check(v.AttachmentList().Items().Size() == 0 &&
			          v.AttachmentStatus().Text() == L"No attachments.",
			      "empty attachment state");
			v.m_attachments.reset();
			v.m_attachmentsFailed = true;
			v.showAttachments();
			check(v.AttachmentRetry().Visibility() == Visibility::Visible,
			      "attachment load error has no retry");
			v.requestAttachments();
			break;
		case 5:
			if (!v.m_attachments) {
				return;
			}
			check(!v.m_attachmentsFailed &&
			          v.AttachmentRetry().Visibility() == Visibility::Collapsed,
			      "attachment retry did not recover");
			v.m_attachments.reset();
			v.requestAttachments();
			first.copy_from(&v);
			owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
			break;
		}
		++step;
		ticks = 0;
	}
	void outlineTick(MainWindow& owner) {
		if (step == 11) {
			check(first && first->m_closed && !first->m_outline &&
			          !first->m_outlinePending && first->m_outlineNodes.empty(),
			      "closed outline accepted a late result");
			const auto current = owner.Viewer();
			check(static_cast<bool>(current));
			const auto v = get_self<ViewerPage>(current);
			check(v->m_current == outlinePage && !v->outlineOpen() &&
			          !v->m_outlineLinkPending,
			      "closed outline followed a late destination");
			status.Text(
			    L"PASS: outline hierarchy, expansion, destinations, URI "
			    L"guards, sidebar switching, empty/retry and stale results");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_outlinePending || v.m_outlineLinkPending) {
			return;
		}
		switch (step) {
		case 0:
			check(!v.m_outline, "outline loaded before opening sidebar");
			v.setThumbnailsOpen(true);
			v.setOutlineOpen(true);
			check(v.outlineOpen() && !v.thumbnailsOpen(),
			      "outline/thumbnail exclusivity");
			break;
		case 1:
			if (!v.m_outline || !v.m_outlineBuilt) {
				return;
			}
			check(v.m_outlineNodes.size() == 8 &&
			          v.OutlineTree().RootNodes().Size() == 5,
			      "outline hierarchy roots");
			check(v.m_outlineNodes[0].Children().Size() == 2 &&
			          v.m_outlineNodes[2].Children().Size() == 1,
			      "outline hierarchy children");
			check(v.m_outlineNodes[0].IsExpanded() &&
			          !v.m_outlineNodes[2].IsExpanded(),
			      "PDF expansion state");
			check(unbox_value<hstring>(v.m_outlineNodes[0].Content()) ==
			          L"Chapters \u03a9\U0001f600",
			      "outline Unicode label");
			v.OutlineTree().Focus(FocusState::Programmatic);
			if (!v.outlineHasFocus()) {
				return;
			}
			v.followOutline(1);
			break;
		case 2:
			check(v.m_current == 1 && std::abs(v.m_zoom - 1.5) < 1e-6,
			      "outline rotated XYZ destination");
			check(v.OutlineTree().SelectedNode() == v.m_outlineNodes[1],
			      "outline selected node");
			v.followOutline(2);
			break;
		case 3:
			check(v.m_current == 2 &&
			          v.m_preset == helpers::ZoomPreset::PageWidth,
			      "outline named destination");
			v.m_outlineNodes[2].IsExpanded(true);
			v.followOutline(3);
			break;
		case 4:
			check(v.m_launchedUri == "https://example.com/outline",
			      "outline web destination");
			for (std::size_t index = 4; index < 8; ++index) {
				v.followOutline(index);
			}
			break;
		case 5: {
			check(v.m_current == 2 &&
			          v.m_preset == helpers::ZoomPreset::PageWidth &&
			          v.m_launchedUri == "https://example.com/outline",
			      "unsupported outline action navigated");
			const auto root = v.m_outlineNodes[0];
			v.toggleSidebar();
			check(!v.outlineOpen() && !v.thumbnailsOpen(), "outline F4 close");
			v.toggleSidebar();
			check(v.outlineOpen() && v.m_outlineNodes[0] == root &&
			          v.m_outlineNodes[2].IsExpanded(),
			      "outline reopen lost cached expansion");
			v.zoomIn();
			break;
		}
		case 6:
			check(v.m_outlineNodes.size() == 8 && v.m_outlineBuilt,
			      "zoom lost outline");
			v.setThumbnailsOpen(true);
			check(v.thumbnailsOpen() && !v.outlineOpen(),
			      "thumbnail did not close outline");
			v.toggleSidebar();
			v.toggleSidebar();
			check(v.thumbnailsOpen() && !v.outlineOpen(),
			      "F4 did not remember thumbnails");
			v.setOutlineOpen(true);
			owner.openDocument(engine, L"tests/fixtures/pages.pdf", 1);
			break;
		case 7:
			check(!v.m_outline && !v.outlineOpen(),
			      "outline state leaked between tabs");
			v.setOutlineOpen(true);
			break;
		case 8:
			if (!v.m_outline) {
				return;
			}
			check(v.m_outlineNodes.empty() &&
			          v.OutlineTree().RootNodes().Size() == 0 &&
			          v.OutlineStatus().Text() == L"No document outline.",
			      "empty outline state");
			v.m_outline.reset();
			v.m_outlineFailed = true;
			v.showOutline();
			check(v.OutlineRetry().Visibility() == Visibility::Visible,
			      "outline error has no retry");
			v.requestOutline();
			break;
		case 9:
			if (!v.m_outline) {
				return;
			}
			check(!v.m_outlineFailed &&
			          v.OutlineRetry().Visibility() == Visibility::Collapsed,
			      "outline retry did not recover");
			v.m_outline.reset();
			v.m_outlineBuilt = false;
			v.requestOutline();
			first.copy_from(&v);
			owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
			break;
		case 10:
			outlinePage = v.m_current;
			v.followOutline(1);
			v.setOutlineOpen(false);
			break;
		}
		++step;
		ticks = 0;
	}
	void findTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_finding ||
		    (v.m_findTimer && v.m_findTimer.IsRunning())) {
			return;
		}
		switch (step) {
		case 0:
			check(owner.FindItem().IsEnabled(), "Find menu disabled");
			v.goToPage(2);
			break;
		case 1:
			v.openFind();
			v.FindBox().Text(L"alpha");
			break;
		case 2:
			check(v.m_findMatches.size() == 4 && v.m_findActive == 2,
			      "initial Find match near current page");
			check(v.FindCount().Text() == L"3 of 4", "Find count");
			v.navigateFind(1);
			check(v.m_findActive == 3, "Find next");
			v.navigateFind(1);
			check(v.m_findActive == 0, "Find next wrap");
			v.navigateFind(-1);
			check(v.m_findActive == 3, "Find previous wrap");
			v.FindCase().IsChecked(true);
			break;
		case 3:
			check(v.m_findMatches.empty() &&
			          v.FindCount().Text() == L"Phrase not found",
			      "Find match case");
			v.FindCase().IsChecked(false);
			v.FindBox().Text(L"Alph");
			v.FindWords().IsChecked(true);
			break;
		case 4:
			check(v.m_findMatches.empty(), "Find whole word");
			v.FindWords().IsChecked(false);
			v.FindBox().Text(L"e");
			break;
		case 5:
			check(std::any_of(v.m_findMatches.begin(), v.m_findMatches.end(),
			                  [](const auto& m) { return m.page == 4; }),
			      "accent-insensitive Find");
			v.FindDiacritics().IsChecked(true);
			break;
		case 6:
			check(std::none_of(v.m_findMatches.begin(), v.m_findMatches.end(),
			                   [](const auto& m) { return m.page == 4; }),
			      "Find diacritics");
			v.FindBox().Text(L"Alpha");
			break;
		case 7:
			if (!v.m_findActive ||
			    !v.m_text.contains(v.m_findMatches[*v.m_findActive].page)) {
				return;
			}
			check(!v.m_findNeedsReveal, "Find geometry not revealed");
			v.FindHighlight().IsChecked(false);
			check(v.hasFindOnPage(v.m_findMatches[*v.m_findActive].page),
			      "active Find highlight");
			v.zoomIn();
			break;
		case 8:
			check(v.m_findMatches.size() == 4, "zoom lost Find results");
			v.FindBox().Text(L"missing");
			v.startFind();
			v.FindBox().Text(L"Second");
			v.startFind();
			break;
		case 9:
			check(v.m_findMatches.size() == 4,
			      "stale Find callback replaced query");
			v.closeFind();
			check(!v.findOpen() && v.m_findMatches.empty(), "Find close");
			v.openFind();
			break;
		case 10:
			check(v.m_findMatches.size() == 4 &&
			          v.FindBox().Text() == L"Second",
			      "Find reopen retains query");
			v.FindBox().Text(L"Alpha");
			v.startFind();
			v.closeFind();
			break;
		case 11:
			check(v.m_findMatches.empty(),
			      "closed Find accepted stale callback");
			v.openFind();
			v.m_findTimer.Stop();
			v.m_findStartPage = 0;
			pdf::FindUpdate partial;
			partial.matches = {{0, 0, 5}};
			partial.pagesScanned = 1;
			partial.pageCount = 5;
			v.applyFind(std::move(partial));
			v.navigateFind(1);
			v.navigateFind(-1);
			check(v.m_findActive == 0 && !v.FindNext().IsEnabled() &&
			          !v.FindPrevious().IsEnabled(),
			      "partial Find wrapped before the scan finished");
			pdf::FindUpdate more;
			more.matches = {{1, 0, 5}};
			more.pagesScanned = 2;
			more.pageCount = 5;
			v.applyFind(std::move(more));
			check(v.FindNext().IsEnabled(),
			      "partial Find did not enable the next known match");
			v.navigateFind(1);
			v.navigateFind(1);
			check(v.m_findActive == 1, "partial Find next wrapped");
			pdf::FindUpdate complete;
			complete.pagesScanned = 5;
			complete.pageCount = 5;
			complete.done = true;
			v.applyFind(std::move(complete));
			v.navigateFind(1);
			check(v.m_findActive == 0, "completed Find did not wrap");
			v.navigateFind(-1);
			check(v.m_findActive == 1, "completed Find previous did not wrap");
			v.closeFind();
			status.Text(L"PASS: Find ranges, counts, navigation, options, "
			            L"highlights, zoom and stale callbacks");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void selectionTick(MainWindow& owner) {
		if (step == 10) {
			check(copyCalls == 4, "closed viewer copied text");
			status.Text(
			    L"PASS: text drag/copy, menu/context, zoom, reverse "
			    L"multi-page, Unicode, permissions and stale callbacks");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& viewer = *get_self<ViewerPage>(current);
		if (!ready(viewer) || viewer.m_copying) {
			return;
		}
		const auto point = [&viewer](int page, int index) {
			const auto& box =
			    viewer.m_text.at(page).text->characters.at(index).box;
			const auto& rect = viewer.m_layout->pages()[page];
			return Windows::Foundation::Point{
			    static_cast<float>(rect.x +
			                       (box.left + box.right) * .5 * rect.width),
			    static_cast<float>(rect.y +
			                       (box.top + box.bottom) * .5 * rect.height)};
		};
		switch (step) {
		case 0:
			viewer.m_copySink = [this](const std::string& text) {
				copiedText = text;
				++copyCalls;
			};
			viewer.requestText(0);
			break;
		case 1:
			if (!viewer.m_text.contains(0)) {
				return;
			}
			viewer.beginSelection(point(0, 0));
			viewer.extendSelection(point(0, 4));
			viewer.endSelection();
			check(viewer.canCopySelection() && viewer.m_ignoreTextTap,
			      "drag did not select text or suppress link taps");
			viewer.copySelection();
			break;
		case 2:
			check(copiedText == "Alpha" && copyCalls == 1,
			      "selected word copy");
			viewer.Scroller().Focus(FocusState::Programmatic);
			owner.MainMenu().ShowAt(owner.MenuButton());
			check(owner.CopyItem().IsEnabled() &&
			          !owner.CutItem().IsEnabled() &&
			          !owner.UndoItem().IsEnabled() &&
			          !owner.PasteItem().IsEnabled(),
			      "PDF Edit menu gates");
			owner.onEditCommand(owner.CopyItem(), {});
			owner.MainMenu().Hide();
			break;
		case 3:
			check(copiedText == "Alpha" && copyCalls == 2, "PDF Edit Copy");
			viewer.zoomIn();
			viewer.requestText(1);
			break;
		case 4:
			if (!viewer.m_text.contains(1)) {
				return;
			}
			check(viewer.m_selection == pdf::TextSelection{{0, 0}, {0, 4}},
			      "zoom changed selected characters");
			viewer.m_textMenu.ShowAt(viewer.Scroller());
			check(viewer.m_textCopy.IsEnabled(), "PDF context Copy");
			viewer.m_textMenu.Hide();
			viewer.beginSelection(point(1, 4));
			viewer.extendSelection(point(0, 6));
			viewer.endSelection();
			check(viewer.m_selection == pdf::TextSelection{{1, 4}, {0, 6}},
			      "reverse page drag");
			viewer.copySelection();
			break;
		case 5:
			check(copiedText == "beta\r\nSecond line\r\nSecon" &&
			          copyCalls == 3,
			      "reverse multi-page copy");
			viewer.selectAllText();
			viewer.copySelection();
			break;
		case 6:
			check(copyCalls == 4 && copiedText.starts_with("Alpha") &&
			          copiedText.find("\xCE\xA9\xF0\x9F\x98\x80\xC3\xA9") !=
			              std::string::npos,
			      "select-all Unicode copy");
			viewer.copySelection();
			viewer.clearTextSelection();
			break;
		case 7:
			check(!viewer.hasTextSelection() && copyCalls == 4,
			      "cleared selection copied late");
			owner.openDocument(engine, L"tests/fixtures/restricted.pdf", {});
			break;
		case 8:
			viewer.m_copySink = [](const auto&) {
				probeFail(L"restricted PDF copied text");
			};
			viewer.selectAllText();
			check(viewer.hasTextSelection() && !viewer.canCopySelection(),
			      "PDF copy permission");
			viewer.copySelection();
			owner.closeTab(owner.m_tabs.back().tab);
			break;
		case 9:
			viewer.selectAllText();
			viewer.copySelection();
			owner.closeTab(owner.m_tabs.front().tab);
			break;
		}
		++step;
		ticks = 0;
	}
	void tick() {
		auto self = window.get();
		if (!self) {
			timer.Stop();
			return;
		}
		try {
			check(probeCheck());
			if (shortcuts) {
				const auto current = self->Viewer();
				report.VerticalAlignment(current ? VerticalAlignment::Bottom
				                                 : VerticalAlignment::Top);
				const auto focus = Input::FocusManager::GetFocusedElement(
				    self->RootGrid().XamlRoot());
				hstring where = L"other";
				if (current) {
					const auto v = get_self<ViewerPage>(current);
					where = focus == v->Scroller()  ? L"document"
					        : focus == v->PageBox() ? L"page box"
					        : focus == self->Tabs().SelectedItem()
					            ? L"tab header"
					            : L"other";
				}
				status.Text(L"Focus: " + where + L"; tab " +
				            to_hstring(self->Tabs().SelectedIndex() + 1) +
				            L" of " + to_hstring(self->m_tabs.size()) +
				            L"; drag events " + to_hstring(dragStarts) + L"/" +
				            to_hstring(dragFinishes) + L"; result " +
				            to_hstring(dragResult));
				return;
			}
			if (++ticks > 120) {
				throw std::runtime_error("tab check timed out");
			}
			if (editOnly) {
				editTick(*self);
				return;
			}
			if (musicOnly) {
				musicTick(*self);
				return;
			}
			if (attachmentsOnly) {
				attachmentsTick(*self);
				return;
			}
			if (outlineOnly) {
				outlineTick(*self);
				return;
			}
			if (findOnly) {
				findTick(*self);
				return;
			}
			if (selectionOnly) {
				selectionTick(*self);
				return;
			}
			if (finishOnly) {
				finishTick(*self);
				return;
			}
			if (printOnly) {
				printTick(*self);
				return;
			}
			if (linksOnly) {
				linkTick(*self);
				return;
			}
			if (filtersOnly) {
				filterTick(*self);
				return;
			}
			if (settingsOnly && step == 0) {
				self->m_presenter.Maximize();
				clearBindings();
				self->onOpenSettings({}, {});
				step = 23;
				return;
			}
			auto current = self->Viewer();
			auto* viewer = current ? get_self<ViewerPage>(current) : nullptr;
			switch (step) {
			case 0:
				if (!viewer || !ready(*viewer)) {
					return;
				}
				if (!positioned) {
					check(self->m_tabs.size() == 2 && viewer->m_current == 1);
					for (const auto& entry : self->m_tabs) {
						check(entry.opened);
					}
					first.copy_from(viewer);
					viewer->zoomIn();
					viewer->goToPage(2);
					positioned = true;
					return;
				}
				check(viewer->m_current == 2);
				check(self->Tabs().CanDragTabs() &&
				      self->Tabs().CanReorderTabs());
				for (const auto& accelerator :
				     self->Tabs().KeyboardAccelerators()) {
					check(!accelerator.IsEnabled());
				}
				zoom = viewer->m_zoom;
				self->Tabs().SelectedItem(self->m_tabs[1].tab);
				break;
			case 1:
				if (!viewer || !ready(*viewer)) {
					return;
				}
				check(viewer->m_current == 499);
				self->m_values.brightness = 42;
				self->updateFilter();
				for (const auto& entry : self->m_tabs) {
					check(get_self<ViewerPage>(entry.viewer)->m_values ==
					      self->m_values);
				}
				self->Tabs().SelectedItem(self->m_tabs[0].tab);
				break;
			case 2:
				if (!viewer || !ready(*viewer)) {
					return;
				}
				check(viewer == first.get() && viewer->m_zoom == zoom &&
				      viewer->m_current == 2);
				self->closeTab(self->m_tabs[1].tab);
				check(self->Viewer() == first.as<Sable::ViewerPage>());
				self->openDocument(engine, L"tests/fixtures/missing.pdf", {});
				break;
			case 3:
				if (!viewer || !viewer->Message().IsOpen()) {
					return;
				}
				check(self->m_tabs.size() == 2);
				self->closeTab(self->m_tabs.back().tab);
				self->closeTab(self->m_tabs.front().tab);
				check(self->m_tabs.empty() && !self->Viewer() &&
				      first->m_closed);
				check(self->Nav().Visibility() == Visibility::Visible &&
				      !self->ZoomInItem().IsEnabled());
				self->openDocument(engine, L"tests/fixtures/password.pdf", {});
				self->openDocument(engine, L"tests/fixtures/pages.pdf", {});
				get_self<ViewerPage>(self->m_tabs[0].viewer)
				    ->askPassword(false);
				check(!get_self<ViewerPage>(self->m_tabs[0].viewer)
				           ->m_passwordDialog);
				break;
			case 4:
				if (!viewer || !ready(*viewer)) {
					return;
				}
				self->Tabs().SelectedItem(self->m_tabs[0].tab);
				break;
			case 5:
				if (!viewer || !viewer->m_passwordDialog) {
					return;
				}
				self->closeTab(self->m_tabs[0].tab);
				self->openDocument(engine, L"tests/fixtures/long.pdf", {});
				self->closeTab(self->m_tabs.back().tab);
				break;
			case 6:
				check(self->m_tabs.size() == 1 && self->Viewer());
				self->m_closedTabs.clear();
				self->closeTab(self->m_tabs.front().tab);
				check(self->m_tabs.empty() && !self->Viewer());
				check(self->m_closedTabs.size() == 1);
				self->reopenTab();
				check(self->m_tabs.size() == 1 && self->m_closedTabs.empty());
				break;
			case 7: {
				if (!viewer || !ready(*viewer)) {
					return;
				}
				check(viewer->m_current == 0 && viewer != first.get());
				const auto saved = self->Viewer();
				const auto a = self->Tabs().SelectedItem();
				self->openDocument(engine, L"tests/fixtures/long.pdf", {});
				const auto b = self->Tabs().SelectedItem();
				self->openDocument(engine, L"tests/fixtures/missing.pdf", {});
				const auto c = self->Tabs().SelectedItem();
				auto items = self->Tabs().TabItems();
				auto order = [&](const auto& x, const auto& y, const auto& z) {
					check(items.Size() == 3 && items.GetAt(0) == x &&
					      items.GetAt(1) == y && items.GetAt(2) == z);
					check(self->Tabs().SelectedItem() == a &&
					      self->Viewer() == saved);
				};
				self->selectTabPosition(0);
				self->moveTab(2);
				order(b, c, a);
				self->moveTab(0);
				order(a, b, c);
				self->moveTab(1);
				order(b, a, c);
				self->moveTab(0);
				self->moveTab(-1);
				self->moveTab(3);
				self->selectTabPosition(8);
				self->selectTabPosition(-1);
				order(a, b, c);
				// the native drag temporarily changes selection while
				// reordering
				self->beginTabDrag();
				items.RemoveAt(0);
				items.Append(a);
				self->Tabs().SelectedItem(b);
				check(saved.Visibility() == Visibility::Visible);
				self->endTabDrag();
				order(b, c, a);
				self->beginTabDrag();
				self->endTabDrag();
				order(b, c, a);
				self->moveTab(0);
				order(a, b, c);
				self->selectTabPosition(2);
				check(self->Tabs().SelectedItem() == c);
				self->closeTab(c.as<TabViewItem>());
				self->closeTab(b.as<TabViewItem>());
				self->reopenTab();
				check(self->m_tabs.back().path.filename() == L"long.pdf");
				self->reopenTab();
				check(self->m_tabs.back().path.filename() == L"missing.pdf");
				self->closeTab(self->m_tabs.back().tab);
				self->openDocument(engine, L"tests/fixtures/missing.pdf", {});
				self->reopenTab();
				check(self->m_tabs.size() == 3 && self->m_closedTabs.empty());
				self->reopenTab();
				check(self->m_tabs.size() == 3);
				break;
			}
			case 8:
				self->m_presenter.Restore();
				self->AppWindow().Resize({900, 700});
				break;
			case 9:
			case 11:
			case 13:
				if (self->RootGrid().ActualWidth() <= 0) {
					return;
				}
				for (const auto& entry : self->m_tabs) {
					check(entry.tab.ActualWidth() > 0);
					check(entry.tab.ActualWidth() <=
					      self->RootGrid().ActualWidth() * 0.15 + 1);
				}
				break;
			case 10:
				self->AppWindow().Resize({1200, 700});
				break;
			case 12:
				self->AppWindow().Resize({565, 700});
				break;
			case 14:
				if (!viewer || !viewer->Message().IsOpen()) {
					return;
				}
				if (dropSetup.Status() ==
				    Windows::Foundation::AsyncStatus::Started) {
					return;
				}
				dropData = dropSetup.GetResults();
				while (!self->m_tabs.empty()) {
					self->closeTab(self->m_tabs.back().tab);
				}
				check(!self->Viewer());
				check(get_self<HomePage>(self->Home())->DropCard().AllowDrop());

				{
					Windows::ApplicationModel::DataTransfer::DataPackage text;
					text.SetText(L"not a file drop");
					self->dropDocuments(text.GetView());
					check(self->m_tabs.empty());
				}
				self->dropDocuments(dropData);
				break;
			case 15:
				if (self->m_tabs.size() != 2 || !viewer ||
				    !viewer->Message().IsOpen()) {
					return;
				}
				check(self->m_tabs[0].path.filename() == L"pages.pdf" &&
				      self->m_tabs[1].path.filename() == L"not_pdf.pdf");
				if (!get_self<ViewerPage>(self->m_tabs[0].viewer)->m_layout) {
					return;
				}
				self->dropDocuments(dropData);
				check(self->m_tabs.size() == 2);
				while (!self->m_tabs.empty()) {
					self->closeTab(self->m_tabs.back().tab);
				}
				closedDropWindow = make_self<MainWindow>();
				closedDropWindow->setEngine(engine);
				closedDropWindow->dropDocuments(dropData);
				check(closedDropWindow->m_tabs.empty());
				closedDropWindow->Close();
				break;
			case 16:
				check(closedDropWindow->m_closing &&
				      closedDropWindow->m_tabs.empty());
				{
					auto settings = self->m_settings;
					settings.general.MaximizeOnOpen = false;
					sizingWindow = make_self<MainWindow>(settings);
					sizingWindow->setEngine(engine);
					sizingWindow->Activate();
				}
				break;
			case 17:
				check(sizingWindow->m_presenter.State() ==
				      Microsoft::UI::Windowing::OverlappedPresenterState::
				          Restored);
				check(sizingWindow->m_tabs.empty());
				sizingWindow->openDocument(engine, L"tests/fixtures/pages.pdf",
				                           {});
				break;
			case 18:
				if (!ready(*get_self<ViewerPage>(sizingWindow->Viewer()))) {
					return;
				}
				check(sizingWindow->m_presenter.State() ==
				      Microsoft::UI::Windowing::OverlappedPresenterState::
				          Restored);
				sizingWindow->Close();
				{
					auto settings = self->m_settings;
					settings.general.MaximizeOnOpen = true;
					sizingWindow = make_self<MainWindow>(settings);
					sizingWindow->setEngine(engine);
					sizingWindow->Activate();
				}
				break;
			case 19:
				check(sizingWindow->m_presenter.State() ==
				      Microsoft::UI::Windowing::OverlappedPresenterState::
				          Maximized);
				check(sizingWindow->m_tabs.empty());
				sizingWindow->openDocument(engine, L"tests/fixtures/pages.pdf",
				                           {});
				break;
			case 20:
				if (!ready(*get_self<ViewerPage>(sizingWindow->Viewer()))) {
					return;
				}
				check(sizingWindow->m_presenter.State() ==
				      Microsoft::UI::Windowing::OverlappedPresenterState::
				          Maximized);
				sizingWindow->Close();
				{
					check(app.m_windows.size() == 1);
					sizingWindow =
					    app.createWindow({L"tests/fixtures/pages.pdf"}, 1);
					peerWindow =
					    app.createWindow({L"tests/fixtures/pages.pdf"}, 2);
					check(app.m_windows.size() == 3);
				}
				break;
			case 21: {
				auto left = get_self<ViewerPage>(sizingWindow->Viewer());
				auto right = get_self<ViewerPage>(peerWindow->Viewer());
				if (!ready(*left) || !ready(*right)) {
					return;
				}
				check(left->m_current == 0 && right->m_current == 1);
				check(sizingWindow->m_tabs.size() == 1 &&
				      peerWindow->m_tabs.size() == 1);
				check(sizingWindow->m_engine == &engine &&
				      peerWindow->m_engine == &engine);
				sizingWindow->onPresetClick(sizingWindow->SepiaPreset(), {});
				sizingWindow->m_sliders[0].Value(42);
				check(sizingWindow->m_preset == "sepia" &&
				      peerWindow->m_preset == "default");
				check(left->m_values == sizingWindow->m_values &&
				      right->m_values == peerWindow->m_values &&
				      left->m_values != right->m_values &&
				      left->m_values.brightness == 42);
				sizingWindow->openDocument(engine,
				                           L"tests/fixtures/missing.pdf", {});
				sizingWindow->Close();
				check(app.m_windows.size() == 2);
				check(sizingWindow->m_closing && sizingWindow->m_tabs.empty());
				peerZoom = right->m_zoom;
				right->zoomIn();
				right->goToPage(2);
				break;
			}
			case 22: {
				auto right = get_self<ViewerPage>(peerWindow->Viewer());
				if (!ready(*right)) {
					return;
				}
				check(right->m_current == 2 && right->m_zoom > peerZoom);
				check(!peerWindow->m_closing &&
				      peerWindow->m_preset == "default");
				sizingWindow = app.createWindow();
				check(app.m_windows.size() == 3);
				check(sizingWindow->m_tabs.empty() &&
				      sizingWindow->m_closedTabs.empty() &&
				      sizingWindow->m_preset == "default");
				sizingWindow->Close();
				check(app.m_windows.size() == 2);
				peerWindow->Close();
				check(app.m_windows.size() == 1 &&
				      app.m_windows.front() == self.as<Window>());
				self->m_presenter.Maximize();
				clearBindings();
				self->onOpenSettings({}, {});
				break;
			}
			case 23: {
				const auto settings = self->m_settingsWindow;
				check(settings != nullptr, "settings did not open");
				check(settings->m_version.Text() ==
				          to_hstring(helpers::versionString()),
				      "settings version");
				HWND owner{}, child{};
				check_hresult(
				    self.as<::IWindowNative>()->get_WindowHandle(&owner));
				check_hresult(
				    settings->window.as<::IWindowNative>()->get_WindowHandle(
				        &child));
				check(GetWindow(child, GW_OWNER) == owner, "settings owner");
				check(!IsWindowEnabled(owner), "settings owner is not blocked");
				self->onOpenSettings({}, {});
				check(self->m_settingsWindow == settings, "duplicate settings");
				settings->select("general");
				check(settings->m_general.Visibility() == Visibility::Visible &&
				      settings->m_version.Visibility() ==
				          Visibility::Collapsed);
				for (const auto& box : settings->m_checks) {
					check(box.IsChecked().Value());
					box.IsChecked(false);
					settings->save(box);
					check(!settings->m_error.IsOpen(), "general save failed");
				}
				check(!uiStore->get().general.MaximizeOnOpen &&
				      !uiStore->get().general.DisplayThumbs);
				check(helpers::loadSettings(settingsPath,
				                            helpers::versionString()) ==
				          uiStore->get(),
				      "general values were not persisted");
				keybindSettings(*settings);
				settings->window.Close();
				check(!self->m_settingsWindow && IsWindowEnabled(owner),
				      "settings close did not restore owner");
				check(self->m_presenter.State() ==
				          Microsoft::UI::Windowing::OverlappedPresenterState::
				              Maximized,
				      "settings changed the reader's window state");
				self->onOpenSettings({}, {});
				for (const auto& box : self->m_settingsWindow->m_checks) {
					check(!box.IsChecked().Value(), "settings did not reload");
				}
				self->m_settingsWindow->window.Close();
				helpers::SettingsStore blocked(settingsPath / "config.json",
				                               helpers::versionString());
				closedDropWindow = app.createWindow();
				closedDropWindow->setSettingsStore(blocked);
				closedDropWindow->onOpenSettings({}, {});
				check(closedDropWindow->m_settingsWindow != nullptr);
				const auto failing = closedDropWindow->m_settingsWindow;
				failing->m_checks[0].IsChecked(false);
				failing->save(failing->m_checks[0]);
				check(failing->m_error.IsOpen() &&
				          failing->m_checks[0].IsChecked().Value() &&
				          blocked.get().general.MaximizeOnOpen,
				      "failed settings save was not restored");
				const auto saved = blocked.get();
				check(!failing->saveBinding("CloseWindow", 0, std::nullopt));
				check(failing->m_error.IsOpen() && blocked.get() == saved,
				      "failed keybind save changed the store");
				closedDropWindow->Close();
				check(!closedDropWindow->m_settingsWindow &&
				      app.m_windows.size() == 1);
				status.Text(
				    L"PASS: duplicate paths, per-tab page/zoom, shared filter, "
				    L"error isolation, password activation, home, reopen, "
				    L"order, drag lifecycle, width cap, splash drop, startup "
				    L"size, independent windows, general/keybind settings");
				if (settingsOnly) {
					status.Text(
					    L"PASS: settings owner, General, keybind "
					    L"capture, persistence, clear and failed saves");
				}
				probePass(self.as<Window>());
				timer.Stop();
				return;
			}
			}
			++step;
			ticks = 0;
		} catch (...) {
			status.Text(L"FAIL at step " + to_hstring(step) + L": " +
			            to_message());
			if (auto current = self->Viewer()) {
				const auto* viewer = get_self<ViewerPage>(current);
				status.Text(status.Text() + L", page " +
				            to_hstring(viewer->m_current + 1) + L", zoom " +
				            to_hstring(viewer->m_zoom));
			}
			timer.Stop();
			probeFail(status.Text());
		}
	}
};
} // namespace winrt::Sable::implementation

Window tabsTest(pdf::Engine& engine, Sable::implementation::App& app) {
	auto window = make_self<Sable::implementation::MainWindow>();
	auto state =
	    std::make_shared<Sable::implementation::TabsTest>(*window, engine, app);
	window->setSettingsStore(*state->uiStore);
	std::weak_ptr<Sable::implementation::TabsTest> weak = state;
	wchar_t mode[8]{};
	state->settingsOnly =
	    GetEnvironmentVariableW(L"SableSettingsTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->filtersOnly = GetEnvironmentVariableW(L"SableFilterTest", mode, 8) &&
	                     std::wstring_view(mode) == L"true";
	state->linksOnly = GetEnvironmentVariableW(L"SableLinkTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	state->printOnly = GetEnvironmentVariableW(L"SablePrintTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	state->finishOnly = GetEnvironmentVariableW(L"SableFinishTest", mode, 8) &&
	                    std::wstring_view(mode) == L"true";
	state->editOnly = GetEnvironmentVariableW(L"SableEditTest", mode, 8) &&
	                  std::wstring_view(mode) == L"true";
	state->musicOnly = GetEnvironmentVariableW(L"SableMusicTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	state->selectionOnly =
	    GetEnvironmentVariableW(L"SableSelectionTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->outlineOnly =
	    GetEnvironmentVariableW(L"SableOutlineTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->attachmentsOnly =
	    GetEnvironmentVariableW(L"SableAttachmentsTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->findOnly = GetEnvironmentVariableW(L"SableFindTest", mode, 8) &&
	                  std::wstring_view(mode) == L"true";
	state->shortcuts = GetEnvironmentVariableW(L"SableShortcutTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	if (state->settingsOnly || state->filtersOnly || state->linksOnly ||
	    state->printOnly || state->finishOnly || state->editOnly ||
	    state->musicOnly || state->selectionOnly || state->findOnly ||
	    state->outlineOnly || state->attachmentsOnly) {
		state->shortcuts = false;
	} else {
		state->dropSetup = state->makeDropData();
	}
	state->status.Text(L"Checking document tabs");
	state->status.TextWrapping(TextWrapping::Wrap);
	Border report;
	state->report = report;
	StackPanel controls;
	controls.Children().Append(state->status);
	if (state->shortcuts) {
		window->Tabs().TabDragStarting([weak](auto&&, auto&&) {
			if (auto state = weak.lock()) {
				++state->dragStarts;
			}
		});
		window->Tabs().TabDragCompleted([weak](auto&&, const auto& args) {
			if (auto state = weak.lock()) {
				++state->dragFinishes;
				state->dragResult = static_cast<int>(args.DropResult());
			}
		});
		for (const bool pageBox : {false, true}) {
			Button focus;
			focus.Content(
			    box_value(pageBox ? L"Focus page box" : L"Focus document"));
			focus.Click([weak, pageBox](auto&&, auto&&) {
				if (auto state = weak.lock()) {
					state->focus(pageBox);
				}
			});
			controls.Children().Append(focus);
		}
		Button clear;
		clear.Content(box_value(L"Clear tab bindings and focus header"));
		clear.Click([weak](auto&&, auto&&) {
			if (auto state = weak.lock()) {
				state->clearBindings();
			}
		});
		controls.Children().Append(clear);
	}
	report.Child(controls);
	report.Background(
	    Microsoft::UI::Xaml::Media::SolidColorBrush({255, 30, 30, 30}));
	report.VerticalAlignment(VerticalAlignment::Bottom);
	report.Padding({8, 8, 8, 8});
	Grid::SetRow(report, 1);
	window->Content().as<Grid>().Children().Append(report);
	window->Closed([state](auto&&, auto&&) { state->timer.Stop(); });
	if (state->attachmentsOnly) {
		window->openDocument(engine, L"tests/fixtures/attachments.pdf", 1);
	} else if (state->outlineOnly) {
		window->openDocument(engine, L"tests/fixtures/outline.pdf", 1);
	} else if (state->selectionOnly || state->findOnly) {
		window->openDocument(engine, L"tests/fixtures/text.pdf", 1);
	} else if (state->editOnly) {
		window->openDocument(engine, L"tests/fixtures/pages.pdf", 2);
	} else if (state->printOnly) {
		window->openDocument(engine, L"tests/fixtures/pages.pdf", 2);
		window->openDocument(engine, L"tests/fixtures/print.pdf", {});
	} else if (state->linksOnly) {
		window->openDocument(engine, L"tests/fixtures/links.pdf", {});
	} else if (!state->settingsOnly && !state->finishOnly &&
	           !state->musicOnly) {
		window->openDocument(engine, L"tests/fixtures/pages.pdf", 2);
		window->openDocument(engine, L"tests/fixtures/long.pdf", 500);
		window->openDocument(engine, L"tests/fixtures/./pages.pdf", 1);
	}
	state->timer = window->DispatcherQueue().CreateTimer();
	state->timer.Interval(std::chrono::milliseconds(250));
	state->timer.Tick([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->tick();
		}
	});
	state->timer.Start();
	return window.as<Window>();
}
