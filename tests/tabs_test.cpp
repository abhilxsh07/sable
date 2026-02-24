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
#include "../app/printing/controller.h"
#include "surface_test.h"
#include "music_capture.h"
#include "pdf_test_files.h"

#include <chrono>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <microsoft.ui.xaml.window.h>
#include <PrintManagerInterop.h>
#include <stdexcept>
#include <string_view>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>

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
	bool windowsOnly = false;
	int settingsCapturePhase = -1;
	std::filesystem::path settingsCaptureDirectory;
	SIZE settingsCaptureSize{};
	std::optional<helpers::NightPDFSettings> settingsCaptureSnapshot;
	bool filtersOnly = false;
	bool propertiesOnly = false;
	bool panOnly = false;
	bool gesturesOnly = false;
	bool longGesturesOnly = false;
	Windows::Foundation::Point gesturePoint{};
	int gestureRound = 0;
	helpers::PageSize gestureSize{};
	bool layoutOnly = false;
	bool presentationOnly = false;
	bool saveCopyOnly = false;
	bool keyboardOnly = false;
	bool keyboardInputOnly = false;
	HWND inputWindow = nullptr;
	double inputZoom = 0;
	hstring inputText;
	std::size_t inputMatch = 0;
	int layoutRound = 0;
	pdf::PageKey layoutKey{};
	double panX = 0, panY = 0;
	std::uint64_t propertiesRequest = 0;
	bool linksOnly = false;
	bool printOnly = false;
	std::shared_ptr<printing::Controller> printPreviewController;
	bool finishOnly = false;
	bool editOnly = false;
	bool musicOnly = false;
	bool selectionOnly = false;
	bool rotationOnly = false;
	int rotationRound = 0;
	helpers::PageSize rotationSize{};
	bool findOnly = false;
	bool outlineOnly = false;
	bool highlightOnly = false;
	bool annotationActionsOnly = false;
	int annotationMenus = 0;
	ListViewItem annotationRow{nullptr};
	std::optional<pdf::Annotation> annotationSnapshot;
	pdf::TextMarkup selectionMarkup = pdf::TextMarkup::Highlight;
	std::string markupSubtype = "Highlight";
	bool noteEditOnly = false;
	bool checkBoxOnly = false;
	bool radioOnly = false;
	bool choiceOnly = false, choiceList = false;
	std::filesystem::path buttonFieldPath;
	bool markupCommentOnly = false;
	bool notePlacementOnly = false;
	bool notesOnly = false;
	bool notesRefusalTried = false;
	Windows::Foundation::IAsyncOperation<bool> notesSave{nullptr};
	std::filesystem::path notesPath;
	std::shared_ptr<std::promise<void>> notesGate;
	static Windows::Foundation::IAsyncOperation<ContentDialogResult>
	notesChoice(ContentDialogResult choice) {
		co_return choice;
	}
	bool commentsOnly = false;
	bool commentLocationsOnly = false;
	bool commentFactorStarted = false, commentFactorSeen = false;
	bool commentFactorCorrect = false;
	ScrollViewer::ViewChanged_revoker commentFactorChanged;
	std::uint64_t commentVersion = 0;
	bool attachmentsOnly = false;
	bool attachmentSaveOnly = false;
	std::filesystem::path attachmentTarget;
	std::shared_ptr<winrt::handle> attachmentGate;
	hstring attachmentName;
	int attachmentPicks = 0;
	static Windows::Foundation::IAsyncOperation<hstring>
	attachmentChoice(hstring path, std::shared_ptr<winrt::handle> gate) {
		if (gate) {
			co_await resume_on_signal(gate->get());
		}
		co_return path;
	}
	ListViewItem attachmentItem{nullptr};
	std::size_t outlinePage = 0;
	std::string copiedText;
	int copyCalls = 0;
	int musicSeeks = 0;
	music::Snapshot musicSnapshot;
	Windows::Foundation::IAsyncOperation<Windows::Storage::Streams::IBuffer>
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
		if (!attachmentTarget.empty()) {
			std::filesystem::remove(attachmentTarget, ignored);
		}
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
	void settingsCaptureTick(MainWindow& owner) {
		if (ticks < 3) {
			return;
		}
		const auto settings = owner.m_settingsWindow;
		check(settings != nullptr, "settings closed during capture");
		auto& settingsWindow = settings->window;
		HWND handle{};
		check_hresult(
		    settingsWindow.as<IWindowNative>()->get_WindowHandle(&handle));
		const auto capture = [&](const wchar_t* name) {
			settingsWindow.Content().as<FrameworkElement>().UpdateLayout();
			check(!settings->m_heading.Text().empty() &&
			          !settings->m_description.Text().empty(),
			      "settings page heading or description missing");
			captureMusicWindow(handle, settingsCaptureDirectory / name);
		};
		switch (settingsCapturePhase) {
		case 0: {
			std::filesystem::create_directories(settingsCaptureDirectory);
			const auto size = settingsWindow.AppWindow().Size();
			settingsCaptureSize = {size.Width, size.Height};
			settingsCaptureSnapshot = uiStore->get();
			settings->select("version");
			break;
		}
		case 1:
			check(settings->m_version.Visibility() == Visibility::Visible &&
			          settings->m_version.Text() ==
			              to_hstring(helpers::versionString()),
			      "settings About version");
			capture(L"about.png");
			settings->m_buildDetails.IsExpanded(true);
			break;
		case 2:
			check(settings->m_buildDetails.IsExpanded(),
			      "settings build details did not expand");
			capture(L"about-details.png");
			settings->m_buildDetails.IsExpanded(false);
			settings->select("general");
			break;
		case 3:
			capture(L"general.png");
			settings->select("keybinds");
			break;
		case 4:
			capture(L"keyboard-shortcuts.png");
			settings->edit("OpenWindow", 0);
			break;
		case 5: {
			check(settings->m_overlay.Visibility() == Visibility::Visible &&
			          !settings->m_saveBinding.IsEnabled(),
			      "shortcut capture prompt");
			capture(L"shortcut-capture.png");
			settings->dismiss();
			const double scale = GetDpiForWindow(handle) / 96.0;
			settingsWindow.AppWindow().Resize(
			    {static_cast<int32_t>(660 * scale),
			     static_cast<int32_t>(620 * scale)});
			break;
		}
		case 6:
			capture(L"keyboard-shortcuts-narrow.png");
			settings->select("general");
			break;
		case 7: {
			capture(L"general-narrow.png");
			const double scale = GetDpiForWindow(handle) / 96.0;
			settingsWindow.AppWindow().Resize(
			    {static_cast<int32_t>(660 * scale),
			     static_cast<int32_t>(360 * scale)});
			break;
		}
		case 8:
			check(settings->m_detailScroll.ScrollableHeight() > 0,
			      "short General page cannot scroll");
			settings->m_detailScroll.ChangeView(
			    nullptr, settings->m_detailScroll.ScrollableHeight(), nullptr,
			    true);
			break;
		case 9:
			check(settings->m_detailScroll.VerticalOffset() > 0,
			      "short General page did not scroll");
			capture(L"general-short.png");
			settingsWindow.AppWindow().Resize(
			    {settingsCaptureSize.cx, settingsCaptureSize.cy});
			settings->m_detailScroll.ChangeView(nullptr, 0., nullptr, true);
			settings->select("version");
			break;
		case 10:
			settings->select("general");
			settings->m_theme.SelectedIndex(1);
			break;
		case 11: {
			check(appearance::obsidian(), "Obsidian did not apply live");
			capture(L"obsidian-general.png");
			HWND reader{};
			check_hresult(
			    owner.try_as<IWindowNative>()->get_WindowHandle(&reader));
			captureMusicWindow(reader, settingsCaptureDirectory /
			                               L"obsidian-reader.png");
			settings->select("keybinds");
			break;
		}
		case 12:
			capture(L"obsidian-shortcuts.png");
			settings->edit("OpenWindow", 0);
			break;
		case 13:
			capture(L"obsidian-capture.png");
			settings->dismiss();
			settings->select("version");
			settings->m_buildDetails.IsExpanded(true);
			break;
		case 14:
			capture(L"obsidian-about.png");
			settings->m_buildDetails.IsExpanded(false);
			settings->m_theme.SelectedIndex(0);
			break;
		case 15:
			check(!appearance::obsidian(), "Dark did not restore live");
			capture(L"dark-restored.png");
			check(uiStore->get() == *settingsCaptureSnapshot &&
			          settings->m_overlay.Visibility() == Visibility::Collapsed,
			      "settings capture changed stored values or left a dialog");
			settingsCapturePhase = -1;
			ticks = 0;
			return;
		}
		++settingsCapturePhase;
		ticks = 0;
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
	void propertiesTick(MainWindow& owner) {
		if (ticks < 3) {
			return;
		}
		auto current = owner.Viewer();
		auto* viewer = current ? get_self<ViewerPage>(current) : nullptr;
		if (viewer && !ready(*viewer)) {
			return;
		}
		const auto capture = [&](const wchar_t* name) {
			wchar_t directory[2048]{};
			const auto count = GetEnvironmentVariableW(
			    L"SablePropertiesCaptureDirectory", directory, 2048);
			if (count && count < 2048) {
				const std::filesystem::path destination(directory);
				std::filesystem::create_directories(destination);
				HWND reader{};
				check_hresult(
				    owner.try_as<IWindowNative>()->get_WindowHandle(&reader));
				captureMusicWindow(reader, destination / name);
			}
		};
		const auto value = [&](int index) {
			const auto scroll =
			    viewer->m_propertiesDialog.Content().as<ScrollViewer>();
			const auto rows = scroll.Content()
			                      .as<StackPanel>()
			                      .Children()
			                      .GetAt(0)
			                      .as<Grid>();
			const auto text =
			    rows.Children().GetAt(index * 2 + 1).as<TextBlock>();
			check(text.IsTextSelectionEnabled(),
			      "property values cannot be selected");
			return text.Text();
		};
		switch (step) {
		case 0: {
			if (!viewer) {
				return;
			}
			check(owner.PropertiesItem().IsEnabled(),
			      "properties menu is disabled");
			first.copy_from(viewer);
			zoom = viewer->m_zoom;
			owner.onDocumentProperties({}, {});
			const auto dialog = viewer->m_propertiesDialog;
			check(dialog != nullptr, "properties dialog did not open");
			owner.onDocumentProperties({}, {});
			check(viewer->m_propertiesDialog == dialog,
			      "duplicate properties dialog");
			break;
		}
		case 1:
			if (!viewer->m_properties || viewer->m_propertiesPending) {
				return;
			}
			check(value(0) == L"properties.pdf" &&
			          value(2) == L"Field notes \u03a9\U0001f600" &&
			          value(3) == L"Abhilash \u03a9",
			      "property filename or Unicode metadata");
			check(value(6) == L"2026-01-02  12:34:56 UTC+05:30" &&
			          value(10) == L"1.7" && value(11) == L"2" &&
			          value(12) == L"279.4 x 215.9 mm (Letter, landscape)" &&
			          value(13) == L"1",
			      "property dates, version or crop/rotation size");
			check(viewer->m_zoom == zoom && viewer->m_current == 0,
			      "properties changed the document view");
			propertiesRequest = viewer->m_propertiesVersion;
			capture(L"properties-dark.png");
			viewer->m_propertiesDialog.Content().as<ScrollViewer>().ChangeView(
			    nullptr, 10000., nullptr, true);
			break;
		case 2:
			capture(L"properties-details.png");
			appearance::apply(true);
			viewer->m_propertiesDialog.Content().as<ScrollViewer>().ChangeView(
			    nullptr, 0., nullptr, true);
			break;
		case 3:
			capture(L"properties-obsidian.png");
			owner.m_presenter.Restore();
			owner.AppWindow().Resize({565, 620});
			break;
		case 4:
			capture(L"properties-narrow.png");
			viewer->dismissProperties();
			appearance::apply(false);
			viewer->rotateView(1);
			// Keep page 2 genuinely current after the landing guard expires.
			// At Auto both pages can fit in the viewport after the narrow
			// resize.
			viewer->zoomTo(1.5, {0, 0});
			viewer->goToPage(1);
			break;
		case 5:
			viewer->showProperties();
			break;
		case 6:
			check(
			    viewer->m_propertiesVersion == propertiesRequest &&
			        value(13) == L"2" &&
			        value(12) == L"297.0 x 210.0 mm (A4, landscape)",
			    "cached properties did not follow current page/view rotation");
			owner.openDocument(engine, L"tests/fixtures/properties_missing.pdf",
			                   1);
			check(!first->m_propertiesDialog,
			      "hidden tab kept its properties dialog");
			break;
		case 7:
			if (!viewer || viewer == first.get()) {
				return;
			}
			viewer->showProperties();
			break;
		case 8:
			if (!viewer->m_properties || viewer->m_propertiesPending) {
				return;
			}
			check(value(2) == L"\u2014" && value(3) == L"\u2014",
			      "missing metadata display");
			capture(L"properties-missing.png");
			owner.closeTab(owner.m_tabs.back().tab);
			break;
		case 9: {
			check(viewer == first.get() && !viewer->m_propertiesDialog,
			      "closed or switched properties returned");
			viewer->m_properties.reset();
			auto healthy = std::move(viewer->m_session);
			viewer->m_session = std::make_unique<pdf::Session>(engine);
			viewer->showProperties();
			check(viewer->m_propertiesFailed &&
			          viewer->m_propertiesDialog.PrimaryButtonText() ==
			              L"Retry",
			      "properties failure has no retry");
			viewer->m_session = std::move(healthy);
			capture(L"properties-retry.png");
			viewer->requestProperties();
			break;
		}
		case 10:
			if (viewer->m_propertiesPending) {
				return;
			}
			check(viewer->m_properties && !viewer->m_propertiesFailed &&
			          viewer->m_propertiesDialog.PrimaryButtonText().empty(),
			      "properties retry failed");
			viewer->dismissProperties();
			break;
		case 11: {
			// A delayed reply can populate the cache, but cannot reopen a
			// dismissed dialog.
			viewer->m_properties.reset();
			auto release = std::make_shared<std::promise<void>>();
			auto wait = release->get_future().share();
			engine.post([wait] { wait.wait(); });
			viewer->showProperties();
			const bool pending = viewer->m_propertiesPending;
			viewer->dismissProperties();
			release->set_value();
			check(pending, "properties request was not queued");
			break;
		}
		case 12:
			if (viewer->m_propertiesPending) {
				return;
			}
			check(viewer->m_properties && !viewer->m_propertiesDialog,
			      "late metadata resurrected the dialog");
			viewer->showProperties();
			break;
		case 13:
			check(viewer->m_propertiesDialog != nullptr);
			owner.closeTab(owner.m_tabs.front().tab);
			check(first->m_closed && !first->m_propertiesDialog &&
			          !owner.PropertiesItem().IsEnabled(),
			      "closing a document kept properties active");
			peerWindow = app.createWindow();
			peerWindow->openDocument(engine, L"tests/fixtures/properties.pdf",
			                         1);
			break;
		case 14: {
			const auto other = peerWindow->Viewer();
			if (!other || !ready(*get_self<ViewerPage>(other))) {
				return;
			}
			get_self<ViewerPage>(other)->showProperties();
			break;
		}
		case 15: {
			auto other =
			    get_self<ViewerPage>(peerWindow->Viewer())->get_strong();
			if (!other->m_properties || other->m_propertiesPending) {
				return;
			}
			check(other->m_propertiesDialog != nullptr,
			      "peer properties did not open");
			peerWindow->Close();
			check(other->m_closed && !other->m_propertiesDialog &&
			          app.m_windows.size() == 1,
			      "closing a reader kept its properties dialog alive");
			status.Text(L"PASS: properties metadata, Unicode, dates, "
			            L"page/rotation size, themes, "
			            L"cache, retry, dismissal, tabs and close");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		}
		++step;
		ticks = 0;
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
		const auto captureTheme = [&](const wchar_t* name) {
			wchar_t directory[2048]{};
			const auto count = GetEnvironmentVariableW(
			    L"SableThemeCaptureDirectory", directory, 2048);
			if (count && count < 2048) {
				const std::filesystem::path destination(directory);
				std::filesystem::create_directories(destination);
				HWND reader{};
				check_hresult(
				    owner.try_as<IWindowNative>()->get_WindowHandle(&reader));
				captureMusicWindow(reader, destination / name);
			}
		};
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
		case 2: {
			auto& viewer = *get_self<ViewerPage>(current);
			const auto key = viewer.key(0);
			const auto zoomBefore = viewer.m_zoom;
			appearance::apply(true);
			filterValues(owner, {});
			check(viewer.key(0) == key && viewer.m_zoom == zoomBefore,
			      "appearance changed PDF geometry or raster identity");
			check(current.Content()
			              .as<Grid>()
			              .Background()
			              .as<Media::SolidColorBrush>()
			              .Color() == Windows::UI::Color{255, 0, 0, 0},
			      "PDF viewer background is not black");
			check(owner.Presets().ActualWidth() > 0, "narrow preset bar");
			owner.onPresetClick(owner.DefaultPreset(), {});
			break;
		}
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
			check(panel.Background().as<Media::SolidColorBrush>().Color() ==
			          Windows::UI::Color{255, 0, 0, 0},
			      "filter panel is not black");
			captureTheme(L"obsidian-pdf.png");
			owner.onHideConfigurator({}, nullptr);
			owner.MainMenu().ShowAt(owner.MenuButton());
			break;
		}
		case 4:
			captureTheme(L"obsidian-menu.png");
			owner.MainMenu().Hide();
			owner.m_music->tint({120, 50, 80});
			owner.m_music->show();
			break;
		case 5:
			check(owner.m_music->m_surface.Background()
			                  .as<Media::SolidColorBrush>()
			                  .Color() == Windows::UI::Color{255, 0, 0, 0} &&
			          owner.m_music->m_gradientTop.Color().A == 0,
			      "music artwork tint overrode Obsidian");
			captureTheme(L"obsidian-music.png");
			appearance::apply(false);
			check(owner.m_music->m_surface.Background() ==
			              owner.m_music->m_background &&
			          owner.m_music->m_background.TintColor() ==
			              Windows::UI::Color{255, 120, 50, 80},
			      "Dark did not restore music tint");
			owner.m_music->m_allowClose = true;
			owner.m_music->m_flyout.Hide();
			break;
		case 6: {
			filterValues(owner, {});
			captureTheme(L"dark-pdf-restored.png");
			while (!owner.m_tabs.empty()) {
				owner.closeTab(owner.m_tabs.back().tab);
			}
			check(owner.Presets().Visibility() == Visibility::Collapsed &&
			          owner.DarkConfigurator().Visibility() ==
			              Visibility::Collapsed,
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
			propertiesRequest = viewer.m_linkVersion;
			break;
		case 4:
			check(viewer.m_current == 0 && std::abs(viewer.m_zoom - 2) < 0.001,
			      "null XYZ zoom; version " +
			          std::to_string(viewer.m_linkVersion) + "/" +
			          std::to_string(propertiesRequest) + ", direct " +
			          std::to_string(viewer.m_directManipulation) +
			          ", offset " +
			          std::to_string(viewer.Scroller().VerticalOffset()));
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
		if ((step != 6 && !ready(viewer)) ||
		    (owner.m_printing && step != 3 && step != 6)) {
			return;
		}
		switch (step) {
		case 0: {
			check(owner.PrintItem().IsEnabled(),
			      "print disabled with document");
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
			check(owner.routeKey('P', helpers::SHORTCUT_CTRL),
			      "fixed print shortcut did not dispatch");
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
			viewer.Message().IsOpen(false);
			viewer.m_printDialog = {};
			viewer.m_printUi = [](auto) {};
			owner.onPrint({}, {});
			check(owner.m_printing && viewer.m_printController,
			      "modern print did not retain its document");
			{
				const auto controller = viewer.m_printController;
				owner.onPrint({}, {});
				check(controller == viewer.m_printController,
				      "modern print accepted a second dialog");
				controller->shown(true);
				check(!controller->m_finished && owner.m_printing &&
				          !viewer.Message().IsOpen(),
				      "opened print UI was treated as a failure");
				controller->shown(false);
			}
			check(!owner.m_printing && viewer.Message().IsOpen() &&
			          viewer.Scroller().Visibility() == Visibility::Visible,
			      "modern print failure hid or disabled the document");
			viewer.Message().IsOpen(false);
			owner.onPrint({}, {});
			viewer.m_printController->finish(pdf::PrintResult::Completed);
			check(owner.m_printing && viewer.m_printController,
			      "submitted print discarded the unfinished writer");
			break;
		case 3:
			check(owner.m_printing && viewer.m_printController,
			      "writer guard ended before completion");
			viewer.m_printController->m_source->jobFinished();
			break;
		case 4: {
			check(!viewer.m_printController && !viewer.Message().IsOpen(),
			      "finished writer kept the reader busy");
			int fallbacks = 0;
			viewer.m_printDialog = [&fallbacks](void*, int, int,
			                                    const std::wstring&) {
				++fallbacks;
				return pdf::PrintDialogResult{};
			};
			owner.onPrint({}, {});
			{
				const auto controller = viewer.m_printController;
				controller->finish(std::nullopt);
			}
			check(fallbacks == 1 && !owner.m_printing,
			      "unavailable modern UI did not fall back once");
			viewer.m_printDialog = {};
			owner.onPrint({}, {});
			const auto controller = viewer.m_printController;
			owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
			check(!owner.m_printing && controller->m_finished &&
			          controller->m_source->MakePage(1, 300, 400) ==
			              HRESULT_FROM_WIN32(ERROR_CANCELLED),
			      "closed tab retained its print job");
			break;
		}
		case 5:
			try {
				using Windows::Graphics::Printing::PrintManager;
				HWND handle{};
				check_hresult(
				    owner.get_strong().as<::IWindowNative>()->get_WindowHandle(
				        &handle));
				auto interop = get_activation_factory<PrintManager,
				                                      IPrintManagerInterop>();
				PrintManager manager{nullptr};
				check_hresult(interop->GetForWindow(
				    handle, guid_of<PrintManager>(), put_abi(manager)));
				diagnostics::write(diagnostics::Level::Info,
				                   "Modern print window interop is available");
			} catch (const hresult_error& error) {
				diagnostics::write(diagnostics::Level::Warning,
				                   "Modern print window interop unavailable: " +
				                       to_string(error.message()));
			}
			if (!diagnostics::environmentEnabled(L"SablePrintUiTest")) {
				step = 7;
				return;
			}
			viewer.m_printDialog = {};
			viewer.m_printUi = {};
			owner.onPrint({}, {});
			printPreviewController = viewer.m_printController;
			check(static_cast<bool>(printPreviewController),
			      "modern print UI rejected the live preview request");
			break;
		case 6:
			if (printPreviewController->m_source->m_previewFrames == 0) {
				bool target = false;
				{
					std::lock_guard lock(
					    printPreviewController->m_source->m_mutex);
					target = static_cast<bool>(
					    printPreviewController->m_source->m_preview);
				}
				check(ticks < 60 && !printPreviewController->m_finished,
				      "system preview unavailable: task " +
				          std::to_string(printPreviewController->m_created) +
				          ", target " + std::to_string(target));
				return;
			}
			diagnostics::write(diagnostics::Level::Info,
			                   "System print preview received a DXGI page");
			printPreviewController->cancel();
			break;
		case 7:
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
			            L"page, modern lifecycle/fallback, cancel and home");
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
		owner.setEasterEgg([&calls] { ++calls; });
		owner.setEasterEgg([&calls] { ++calls; });
		const auto modifiers = helpers::SHORTCUT_ALT | helpers::SHORTCUT_SHIFT;
		check(owner.routeKey('T', modifiers) && calls == 1,
		      "notification shortcut missing or duplicated");
		check(owner.routeKey('T', modifiers, true) && calls == 1,
		      "held notification shortcut repeated");
		owner.setEasterEgg({});
		check(!owner.routeKey('T', modifiers) && calls == 1,
		      "disabled notification shortcut dispatched");
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
	    Windows::Storage::Streams::IBuffer>
	makeMusicArtwork(int kind = 0) {
		using namespace Windows::Graphics::Imaging;
		Windows::Storage::Streams::InMemoryRandomAccessStream stream;
		const auto encoder = co_await BitmapEncoder::CreateAsync(
		    BitmapEncoder::PngEncoderId(), stream);
		std::vector<std::uint8_t> pixels(96 * 96 * 4);
		for (int y = 0; y < 96; ++y) {
			for (int x = 0; x < 96; ++x) {
				const auto at = static_cast<std::size_t>((y * 96 + x) * 4);
				const bool circle =
				    (x - 48) * (x - 48) + (y - 48) * (y - 48) < 700;
				pixels[at] = kind == 0   ? 0
				             : kind == 1 ? (circle ? 60 : 140)
				                         : (circle ? 70 : 25);
				pixels[at + 1] = kind == 0   ? 0
				                 : kind == 1 ? (circle ? 180 : 225)
				                             : (circle ? 42 : 15);
				pixels[at + 2] = kind == 0   ? 255
				                 : kind == 1 ? 255
				                             : (circle ? 28 : 8);
				pixels[at + 3] = 255;
			}
		}
		encoder.SetPixelData(BitmapPixelFormat::Bgra8,
		                     BitmapAlphaMode::Straight, 96, 96, 96, 96, pixels);
		co_await encoder.FlushAsync();
		stream.Seek(0);
		Windows::Storage::Streams::DataReader reader(stream);
		const auto size = static_cast<std::uint32_t>(stream.Size());
		co_await reader.LoadAsync(size);
		co_return reader.ReadBuffer(size);
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
			pane.show();
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
			{
				const auto origin =
				    pane.m_surface.TransformToVisual(owner.RootGrid())
				        .TransformPoint({0, 0});
				check(origin.Y >= owner.AppTitleBar().ActualHeight() + 7 &&
				          origin.X >= 0 &&
				          origin.X + pane.m_surface.ActualWidth() <=
				              owner.RootGrid().ActualWidth() + 1,
				      "music flyout overlaps title bar or leaves the window: " +
				          std::to_string(origin.X) + "/" +
				          std::to_string(origin.Y));
				const auto artOrigin =
				    pane.m_art.TransformToVisual(owner.RootGrid())
				        .TransformPoint({0, 0});
				const auto seekOrigin =
				    pane.m_seek.TransformToVisual(owner.RootGrid())
				        .TransformPoint({0, 0});
				check(artOrigin.X >= origin.X + 15 &&
				          artOrigin.Y >= origin.Y + 15 &&
				          seekOrigin.X + pane.m_seek.ActualWidth() <=
				              origin.X + pane.m_surface.ActualWidth() - 15,
				      "music content lost its top or right padding");
				check(pane.m_surface.CornerRadius().TopLeft == 12 &&
				          pane.m_surface.CornerRadius().TopRight == 12 &&
				          pane.m_surface.CornerRadius().BottomLeft == 12 &&
				          pane.m_surface.CornerRadius().BottomRight == 12 &&
				          pane.m_play.CornerRadius().TopLeft == 22 &&
				          pane.m_title.FontSize() == 16,
				      "music surface and primary control geometry");
			}
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
			          pane.m_repeatIcon.Glyph() == L"\uE8ED" &&
			          pane.m_repeat.Visibility() == Visibility::Collapsed,
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
			pane.m_animationsOverride = false;
			pane.tint({20, 25, 30});
			pane.motion(true);
			check(pane.m_transition.Children().Size() == 0 &&
			          pane.m_motion.Children().Size() == 0 &&
			          pane.m_surface.Opacity() == 1 && pane.m_slide.Y() == 0,
			      "reduced motion still animated the pane");
			pane.tint({});
			pane.m_animationsOverride.reset();
			pane.update(musicSnapshot);
			check(!pane.m_timer.IsRunning(), "paused music kept ticking");
			pane.m_seekSink = [this](double value) {
				check(value == 70, "wrong seek sent");
				++musicSeeks;
			};
			pane.m_seek.Value(70);
			break;
		case 3:
			if (!pane.m_pendingSeek) {
				return;
			}
			check(musicSeeks == 1, "seek not dispatched once");
			state.busy = true;
			pane.update(musicSnapshot);
			check(pane.m_seeking && pane.m_seek.Value() == 70,
			      "busy/old timeline snapped the seek back");
			state.busy = false;
			break;
		case 4:
			++musicSnapshot.artworkVersion;
			pane.update(musicSnapshot);
			check(pane.m_seeking && pane.m_seek.Value() == 70,
			      "artwork refresh discarded the pending seek");
			state.timeline.position = 80 * music::TICKS_PER_SECOND;
			state.timeline.updated =
			    winrt::clock::now().time_since_epoch().count();
			pane.update(musicSnapshot);
			check(!pane.m_seeking && !pane.m_pendingSeek &&
			          pane.m_seek.Value() == 70,
			      "fresh seek acknowledgement was ignored");
			break;
		case 5:
			state.playback = music::Playback::Playing;
			state.timeline.playing = true;
			musicSnapshot.sampledAt = std::chrono::steady_clock::now();
			pane.update(musicSnapshot);
			check(pane.m_timer.IsRunning() &&
			          pane.m_timer.Interval() == std::chrono::milliseconds(250),
			      "playing clock does not tick at four Hz");
			pane.m_flyout.Hide();
			break;
		case 6:
			if (pane.m_open) {
				return;
			}
			check(!pane.m_timer.IsRunning(), "closed music kept ticking");
			pane.show();
			break;
		case 7:
			if (!pane.m_open) {
				return;
			}
			++musicSnapshot.trackVersion;
			state.timeline.position = state.timeline.start;
			musicSnapshot.sampledAt = std::chrono::steady_clock::now();
			pane.update(musicSnapshot);
			check(pane.m_seek.Value() < 1,
			      "new track kept the previous position");
			break;
		case 8: {
			if (musicArt.Status() ==
			    Windows::Foundation::AsyncStatus::Started) {
				return;
			}
			const auto encoded = musicArt.GetResults();
			auto bytes =
			    std::make_shared<std::vector<std::uint8_t>>(encoded.Length());
			Windows::Storage::Streams::DataReader::FromBuffer(encoded)
			    .ReadBytes(*bytes);
			musicSnapshot.artwork = std::move(bytes);
			++musicSnapshot.artworkVersion;
			pane.update(musicSnapshot);
			break;
		}
		case 9:
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
		case 10:
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
		case 11: {
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
			wchar_t directory[2048]{};
			if (GetEnvironmentVariableW(L"SableMusicCaptureDirectory",
			                            directory, 2048)) {
				owner.AppWindow().Resize({1100, 800});
				step = 20;
				ticks = 0;
				return;
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
		case 20:
		case 24:
		case 28:
			if (ticks < 3) {
				return;
			}
			report.Visibility(Visibility::Collapsed);
			state.title = step == 20   ? "Bright artwork"
			              : step == 24 ? "Dark artwork"
			                           : "No artwork";
			state.artist = "Sample artist";
			state.player = music::Player::Spotify;
			state.controls = {true, true, true, true, true, true, true, true};
			state.shuffle = true;
			state.repeat = music::Repeat::One;
			state.playback = music::Playback::Paused;
			state.timeline = {0,
			                  260 * music::TICKS_PER_SECOND,
			                  195 * music::TICKS_PER_SECOND,
			                  0,
			                  260 * music::TICKS_PER_SECOND,
			                  1,
			                  false};
			++musicSnapshot.trackVersion;
			musicSnapshot.artwork = nullptr;
			++musicSnapshot.artworkVersion;
			pane.update(musicSnapshot);
			if (step == 28) {
				pane.show();
				step = 30;
				ticks = 0;
				return;
			}
			musicArt = makeMusicArtwork(step == 20 ? 1 : 2);
			break;
		case 21:
		case 25: {
			if (musicArt.Status() ==
			    Windows::Foundation::AsyncStatus::Started) {
				return;
			}
			const auto encoded = musicArt.GetResults();
			auto bytes =
			    std::make_shared<std::vector<std::uint8_t>>(encoded.Length());
			Windows::Storage::Streams::DataReader::FromBuffer(encoded)
			    .ReadBytes(*bytes);
			musicSnapshot.artwork = std::move(bytes);
			++musicSnapshot.artworkVersion;
			pane.update(musicSnapshot);
			pane.show();
			break;
		}
		case 22:
		case 26:
		case 30: {
			if (ticks == 3) {
				pane.m_play.Focus(FocusState::Pointer);
			}
			if (ticks < 5 || !pane.m_open ||
			    (step != 30 && !pane.m_art.Source())) {
				return;
			}
			wchar_t directory[2048]{};
			check(GetEnvironmentVariableW(L"SableMusicCaptureDirectory",
			                              directory, 2048) > 0);
			const std::filesystem::path target(directory);
			std::filesystem::create_directories(target);
			const auto file = step == 22   ? L"bright-art.png"
			                  : step == 26 ? L"dark-art.png"
			                               : L"no-art.png";
			HWND captureWindow{};
			check_hresult(owner.get_strong()
			                  .as<Window>()
			                  .as<IWindowNative>()
			                  ->get_WindowHandle(&captureWindow));
			captureMusicWindow(captureWindow, target / file);
			pane.m_flyout.Hide();
			break;
		}
		case 23:
		case 27:
			if (pane.m_open) {
				return;
			}
			break;
		case 31:
			musicSnapshot = {};
			pane.update(musicSnapshot);
			step = 12;
			ticks = 0;
			return;
		case 12:
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

	void
	deliverKeys(std::initializer_list<std::pair<unsigned, unsigned>> chords) {
		DWORD process = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &process);
		check(process == GetCurrentProcessId() &&
		          GetForegroundWindow() == inputWindow,
		      "input check lost the reader foreground; no keys sent");
		for (const auto modifier :
		     {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN, VK_LBUTTON,
		      VK_RBUTTON, VK_MBUTTON}) {
			check(
			    (GetAsyncKeyState(modifier) & 0x8000) == 0,
			    "input check found a held key or pointer button; no keys sent");
		}
		std::vector<::INPUT> events;
		const auto append = [&](unsigned code, bool up) {
			::INPUT event{};
			event.type = INPUT_KEYBOARD;
			event.ki.wVk = static_cast<WORD>(code);
			event.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
			if ((code >= VK_PRIOR && code <= VK_DOWN) || code == VK_INSERT ||
			    code == VK_DELETE || code == VK_DIVIDE) {
				event.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
			}
			events.push_back(event);
		};
		for (const auto& [code, modifiers] : chords) {
			const std::pair<unsigned, unsigned> held[] = {
			    {helpers::SHORTCUT_CTRL, VK_CONTROL},
			    {helpers::SHORTCUT_ALT, VK_MENU},
			    {helpers::SHORTCUT_SHIFT, VK_SHIFT}};
			for (const auto& [flag, modifier] : held) {
				if (modifiers & flag) {
					append(modifier, false);
				}
			}
			append(code, false);
			append(code, true);
			for (auto index = std::size(held); index > 0; --index) {
				const auto [flag, modifier] = held[index - 1];
				if (modifiers & flag) {
					append(modifier, true);
				}
			}
		}
		SetLastError(ERROR_SUCCESS);
		const auto sent = SendInput(static_cast<UINT>(events.size()),
		                            events.data(), sizeof(::INPUT));
		const auto error = GetLastError();
		if (sent != events.size()) {
			std::vector<::INPUT> release;
			for (auto index = sent; index > 0; --index) {
				if (!(events[index - 1].ki.dwFlags & KEYEVENTF_KEYUP)) {
					auto event = events[index - 1];
					event.ki.dwFlags |= KEYEVENTF_KEYUP;
					release.push_back(event);
				}
			}
			if (!release.empty()) {
				SendInput(static_cast<UINT>(release.size()), release.data(),
				          sizeof(::INPUT));
			}
			check(false, "Windows input unavailable: accepted " +
			                 std::to_string(sent) + "/" +
			                 std::to_string(events.size()) + ", error " +
			                 std::to_string(error));
		}
	}

	void keyboardInputTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current || ticks < 3) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_finding || v.m_findNeedsReveal ||
		    (v.m_findTimer && v.m_findTimer.IsRunning())) {
			return;
		}
		constexpr auto ctrl = helpers::SHORTCUT_CTRL;
		constexpr auto shift = helpers::SHORTCUT_SHIFT;
		constexpr auto alt = helpers::SHORTCUT_ALT;
		switch (step) {
		case 0:
			check_hresult(
			    owner.get_strong().as<IWindowNative>()->get_WindowHandle(
			        &inputWindow));
			owner.Activate();
			SetForegroundWindow(inputWindow);
			v.Scroller().Focus(FocusState::Keyboard);
			break;
		case 1:
			deliverKeys({{'J', 0}});
			break;
		case 2:
			check(v.m_current == 1, "Windows-delivered J did not turn a page");
			deliverKeys({{'K', 0}});
			break;
		case 3:
			check(v.m_current == 0, "Windows-delivered K did not go back");
			owner.Tabs().SelectedItem().as<TabViewItem>().Focus(
			    FocusState::Keyboard);
			deliverKeys({{'G', ctrl | alt}});
			break;
		case 4:
			check(Input::FocusManager::GetFocusedElement(
			          owner.RootGrid().XamlRoot()) == v.PageBox(),
			      "Windows-delivered Ctrl-Alt-G lost page-box focus");
			deliverKeys({{'3', 0}});
			break;
		case 5:
			check(v.PageBox().Text() == L"3" && v.m_current == 0,
			      "page draft navigated before Enter");
			deliverKeys({{'R', 0}});
			break;
		case 6:
			check(v.m_rotation == 0 && (v.PageBox().Text() == L"3r" ||
			                            v.PageBox().Text() == L"3R"),
			      "reader stole typed R");
			deliverKeys({{'A', ctrl}, {'2', 0}, {VK_RETURN, 0}});
			break;
		case 7:
			check(v.m_current == 1, "Enter did not commit the page draft");
			v.PageBox().Focus(FocusState::Keyboard);
			inputZoom = v.m_zoom;
			inputText = v.PageBox().Text();
			deliverKeys({{VK_OEM_MINUS, ctrl}});
			break;
		case 8:
			check(v.m_zoom < inputZoom && v.PageBox().Text() == inputText,
			      "Ctrl-minus edited the page box instead of zooming");
			deliverKeys({{'0', ctrl}});
			break;
		case 9:
			check(v.m_preset == helpers::ZoomPreset::Auto,
			      "Ctrl-0 did not reset zoom");
			v.Scroller().Focus(FocusState::Keyboard);
			deliverKeys({{'R', 0}});
			break;
		case 10:
			check(v.m_rotation == 1, "Windows-delivered R did not rotate");
			deliverKeys({{'R', shift}});
			break;
		case 11:
			check(v.m_rotation == 0,
			      "Windows-delivered Shift-R did not restore rotation");
			deliverKeys({{'H', 0}});
			break;
		case 12:
			check(v.cursorTool() == ViewerPage::CursorTool::Hand,
			      "H did not select Hand");
			deliverKeys({{'S', 0}});
			break;
		case 13:
			check(v.cursorTool() == ViewerPage::CursorTool::Select,
			      "S did not select text");
			deliverKeys({{'F', ctrl}});
			break;
		case 14:
			check(v.findOpen() &&
			          Input::FocusManager::GetFocusedElement(
			              owner.RootGrid().XamlRoot()) == v.FindBox(),
			      "Ctrl-F focus failed");
			deliverKeys({{'A', shift}, {'L', 0}, {'P', 0}, {'H', 0}, {'A', 0}});
			break;
		case 15:
			check(v.m_findActive && v.m_findMatches.size() > 1,
			      "typed Find query did not produce matches");
			inputMatch = *v.m_findActive;
			deliverKeys({{'G', ctrl}});
			break;
		case 16:
			check(v.m_findActive && v.m_findActive != inputMatch,
			      "Ctrl-G did not advance");
			deliverKeys({{'G', ctrl | shift}});
			break;
		case 17:
			check(v.m_findActive == inputMatch, "Ctrl-Shift-G did not go back");
			deliverKeys({{VK_ESCAPE, 0}});
			break;
		case 18:
			check(!v.findOpen(), "Escape did not close Find");
			v.Scroller().Focus(FocusState::Keyboard);
			deliverKeys({{'P', ctrl | alt}});
			break;
		case 19:
			check(v.m_presentation.has_value(),
			      "Ctrl-Alt-P did not enter presentation");
			deliverKeys({{VK_DOWN, ctrl}});
			break;
		case 20:
			check(v.m_current + 1 == v.m_sizes.size(),
			      "presentation Ctrl-Down failed");
			deliverKeys({{VK_UP, ctrl}});
			break;
		case 21:
			check(v.m_current == 0, "presentation Ctrl-Up failed");
			deliverKeys({{VK_ESCAPE, 0}});
			break;
		case 22:
			check(!v.m_presentation, "Escape did not leave presentation");
			owner.m_copyPicker = [this](hstring) {
				++attachmentPicks;
				return attachmentChoice(L"", {});
			};
			deliverKeys({{'S', ctrl}});
			break;
		case 23:
			check(attachmentPicks == 1 && !owner.m_saveCopyCancel,
			      "Windows-delivered Ctrl-S did not reach the save picker");
			deliverKeys({{VK_F11, 0}});
			break;
		case 24:
			check(owner.AppWindow().Presenter().Kind() ==
			          Microsoft::UI::Windowing::AppWindowPresenterKind::
			              FullScreen,
			      "Windows-delivered F11 did not enter full screen");
			deliverKeys({{VK_F11, 0}});
			break;
		case 25:
			check(owner.AppWindow().Presenter().Kind() !=
			          Microsoft::UI::Windowing::AppWindowPresenterKind::
			              FullScreen,
			      "second F11 did not leave full screen");
			v.showProperties();
			break;
		case 26:
			check(v.m_propertiesDialog != nullptr,
			      "properties dialog did not open");
			inputZoom = v.m_zoom;
			deliverKeys({{VK_ADD, ctrl}, {'R', 0}});
			break;
		case 27:
			check(v.m_zoom == inputZoom && v.m_rotation == 0,
			      "modal dialog leaked OS-delivered reader keys");
			deliverKeys({{VK_ESCAPE, 0}});
			break;
		case 28:
			check(!v.m_propertiesDialog, "Escape did not dismiss properties");
			status.Text(
			    L"PASS: Windows input delivery, text editing, zoom, Find, "
			    L"rotation, tools, presentation and dialog isolation");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}

	void keyboardTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current || ticks < 3) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_finding || v.m_findNeedsReveal ||
		    (v.m_findTimer && v.m_findTimer.IsRunning())) {
			return;
		}
		constexpr auto ctrl = helpers::SHORTCUT_CTRL;
		constexpr auto shift = helpers::SHORTCUT_SHIFT;
		constexpr auto alt = helpers::SHORTCUT_ALT;
		const auto key = [&](unsigned code, unsigned modifiers = 0) {
			return owner.routeKey(code, modifiers);
		};
		switch (step) {
		case 0: {
			owner.Tabs().SelectedItem().as<TabViewItem>().Focus(
			    FocusState::Keyboard);
			check(key('G', ctrl | alt), "global page-box command missing");
			check(Input::FocusManager::GetFocusedElement(
			          owner.RootGrid().XamlRoot()) == v.PageBox(),
			      "tab-header shortcut did not focus page box");
			const auto page = v.m_current;
			check(!key('J') && !key('R') && v.m_current == page &&
			          v.m_rotation == 0,
			      "reader letters changed a text field");
			check(!key('A', ctrl) && !key('C', ctrl),
			      "PDF commands stole native text editing");
			const auto beforeZoom = v.m_zoom;
			check(key(VK_OEM_MINUS, ctrl) && v.m_zoom < beforeZoom,
			      "text-field Ctrl-minus did not zoom");
			check(key(VK_ADD, ctrl | shift) && key(VK_SUBTRACT, ctrl | shift) &&
			          key(VK_OEM_PLUS, ctrl) && key(VK_NUMPAD0, ctrl | shift) &&
			          v.m_preset == helpers::ZoomPreset::Auto,
			      "keypad or Shift zoom aliases failed");
			v.Scroller().Focus(FocusState::Keyboard);
			check(!key('R', helpers::SHORTCUT_WIN) && v.m_rotation == 0,
			      "Windows chord reached the reader");
			check(key('H') && v.cursorTool() == ViewerPage::CursorTool::Hand &&
			          key('S') &&
			          v.cursorTool() == ViewerPage::CursorTool::Select,
			      "cursor-tool shortcuts failed");
			check(key('R') && v.m_rotation == 1 && key('R', shift) &&
			          v.m_rotation == 0,
			      "rotation shortcuts failed");
			const bool sidebar = v.thumbnailsOpen();
			check(key(VK_F4) && v.thumbnailsOpen() != sidebar && key(VK_F4) &&
			          v.thumbnailsOpen() == sidebar,
			      "sidebar key failed");
			v.goToPage(0);
			break;
		}
		case 1:
			key('J');
			break;
		case 2:
			check(v.m_current == 1, "J did not turn a page");
			key('K');
			break;
		case 3:
			check(v.m_current == 0, "K did not turn back");
			v.PageBox().Focus(FocusState::Keyboard);
			key(VK_DOWN, ctrl | shift);
			break;
		case 4:
			check(v.m_current + 1 == v.m_sizes.size(),
			      "Ctrl-Down missed last page");
			key(VK_UP, ctrl);
			break;
		case 5:
			check(v.m_current == 0, "Ctrl-Up missed first page");
			v.Scroller().Focus(FocusState::Keyboard);
			v.setPreset(helpers::ZoomPreset::PageFit);
			key(VK_SPACE);
			break;
		case 6:
			check(v.m_current == 1, "page-fit Space did not advance");
			key(VK_SPACE, shift);
			break;
		case 7:
			check(v.m_current == 0, "Shift-Space did not go back");
			check(key('F', ctrl) && v.findOpen(), "Ctrl-F failed");
			v.FindBox().Text(L"Alpha");
			break;
		case 8: {
			check(v.m_findActive && v.m_findMatches.size() > 1,
			      "shortcut Find query did not finish");
			const auto before = *v.m_findActive;
			check(key('G', ctrl) && v.m_findActive != before &&
			          key('G', ctrl | shift) && v.m_findActive == before &&
			          key(VK_F3) && v.m_findActive != before &&
			          key(VK_F3, shift) && v.m_findActive == before,
			      "Find next/previous shortcuts failed");
			key(VK_ESCAPE);
			check(!v.findOpen(), "Escape did not close Find");
			v.showProperties();
			break;
		}
		case 9: {
			check(v.m_propertiesDialog != nullptr,
			      "properties dialog did not open");
			const auto beforeZoom = v.m_zoom;
			check(!key('F', ctrl) && !key(VK_ADD, ctrl) && !key('R') &&
			          !key('G', ctrl | alt) && v.m_zoom == beforeZoom &&
			          !v.findOpen(),
			      "modal dialog leaked reader shortcuts");
			v.dismissProperties();
			break;
		}
		case 10:
			key('P', ctrl | alt);
			break;
		case 11:
			check(v.m_presentation.has_value(),
			      "presentation key did not enter");
			key(VK_DOWN, ctrl);
			break;
		case 12:
			check(v.m_current + 1 == v.m_sizes.size(),
			      "presentation Ctrl-Down missed last page");
			key(VK_UP, ctrl | shift);
			break;
		case 13:
			check(v.m_current == 0, "presentation Ctrl-Up missed first page");
			owner.m_copyPicker = [](hstring) {
				return attachmentChoice(L"", {});
			};
			key('S', ctrl);
			check(!v.m_presentation, "Save did not leave presentation");
			break;
		case 14:
			check(!owner.m_saveCopyCancel,
			      "cancelled keyboard save stayed active");
			key(VK_F11);
			check(owner.AppWindow().Presenter().Kind() ==
			          Microsoft::UI::Windowing::AppWindowPresenterKind::
			              FullScreen,
			      "F11 did not enter full screen");
			check(owner.routeKey(VK_F11, 0, true) &&
			          owner.AppWindow().Presenter().Kind() ==
			              Microsoft::UI::Windowing::AppWindowPresenterKind::
			                  FullScreen,
			      "repeat F11 toggled full screen");
			key(VK_F11);
			first.copy_from(&v);
			owner.m_shortcuts.push_back({'J', 0, "switch-tab", "next"});
			owner.openDocument(engine, L"tests/fixtures/pages.pdf", 1);
			owner.Tabs().SelectedIndex(0);
			break;
		case 15:
			check(owner.Tabs().SelectedIndex() == 0 && key('J') &&
			          owner.Tabs().SelectedIndex() == 1 &&
			          first->m_current == 0,
			      "custom binding lost priority over viewer letters");
			status.Text(
			    L"PASS: global focus, navigation, zoom aliases, Find, "
			    L"tools, modal guards, presentation and custom bindings");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}

	void saveCopyTick(MainWindow& owner) {
		if (ticks < 3 || owner.m_saveCopyCancel || owner.m_notesSaving) {
			return;
		}
		if (step == 12) {
			check(first && first->m_closed, "closed save retained its viewer");
			check(!std::filesystem::exists(attachmentTarget),
			      "closed tab accepted a late picker result");
			status.Text(L"PASS: PDF copy, cancel, source protection, errors "
			            L"and late picker guards");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_noteBusy || v.m_noteSaving) {
			return;
		}
		const auto choose = [&](const std::wstring& path) {
			owner.m_copyPicker = [this, path](hstring name) {
				attachmentName = name;
				++attachmentPicks;
				return attachmentChoice(hstring(path), attachmentGate);
			};
		};
		const auto chooseNotes = [&](const std::wstring& path) {
			owner.m_notesPicker = [this, path](hstring name) {
				attachmentName = name;
				++attachmentPicks;
				return attachmentChoice(hstring(path), {});
			};
		};
		const auto bytes = [](const std::filesystem::path& path) {
			std::ifstream in(path, std::ios::binary);
			return std::string((std::istreambuf_iterator<char>(in)), {});
		};
		switch (step) {
		case 0:
			attachmentTarget = settingsPath;
			attachmentTarget.replace_extension(L".pdf");
			choose(L"");
			owner.onSaveCopy(nullptr, nullptr);
			break;
		case 1:
			check(attachmentPicks == 1 && attachmentName == L"pages copy.pdf" &&
			          !std::filesystem::exists(attachmentTarget) &&
			          !v.Message().IsOpen() && owner.SaveCopyItem().IsEnabled(),
			      "cancel changed document state or suggested name");
			v.rotateView(1);
			v.zoomIn();
			choose(attachmentTarget.wstring());
			owner.onSaveCopy(nullptr, nullptr);
			owner.onSaveCopy(nullptr, nullptr);
			check(attachmentPicks == 2, "duplicate save opened another picker");
			break;
		case 2:
			check(bytes(attachmentTarget) == bytes(v.m_path),
			      "saving applied view changes to the PDF");
			choose(std::filesystem::absolute(v.m_path).wstring());
			owner.onSaveCopy(nullptr, nullptr);
			break;
		case 3:
			check(v.Message().IsOpen() &&
			          std::wstring_view(v.Message().Message())
			                  .find(L"Choose a different") == 0 &&
			          bytes(attachmentTarget) == bytes(v.m_path),
			      "source protection missing or previous copy changed");
			v.Message().IsOpen(false);
			choose((attachmentTarget / L"missing" / L"copy.pdf").wstring());
			owner.onSaveCopy(nullptr, nullptr);
			break;
		case 4:
			check(v.Message().IsOpen() &&
			          std::wstring_view(v.Message().Message())
			                  .find(L"Couldn't save") == 0 &&
			          bytes(attachmentTarget) == bytes(v.m_path),
			      "failed copy has no notice or changed existing output");
			v.Message().IsOpen(false);
			owner.m_copyPicker =
			    [](hstring) -> Windows::Foundation::IAsyncOperation<hstring> {
				throw hresult_access_denied();
			};
			owner.onSaveCopy(nullptr, nullptr);
			break;
		case 5:
			check(v.Message().IsOpen(), "picker failure has no notice");
			v.Message().IsOpen(false);
			v.submitNote(0, u"Copy retains notes", {});
			owner.onSaveCopy(nullptr, nullptr);
			check(!owner.m_saveCopyCancel && v.m_noteBusy &&
			          v.Message().IsOpen(),
			      "copy ran before the pending note finished");
			v.Message().IsOpen(false);
			break;
		case 6:
			check(v.m_notesDirty, "copy test note did not become dirty");
			chooseNotes(L"");
			v.Scroller().Focus(FocusState::Programmatic);
			check(owner.routeKey('S', helpers::SHORTCUT_CTRL),
			      "Ctrl-S did not reach edited-copy saving");
			break;
		case 7:
			check(v.m_notesDirty && !v.m_noteSaving &&
			          !owner.m_saveCopyCancel && attachmentPicks == 5 &&
			          attachmentName == L"pages notes.pdf" &&
			          bytes(attachmentTarget) == bytes(v.m_path),
			      "dirty copy cancel lost notes or opened another picker");
			chooseNotes(
			    (attachmentTarget / L"missing" / L"notes.pdf").wstring());
			owner.onSaveCopy(nullptr, nullptr);
			break;
		case 8:
			check(v.m_notesDirty && !v.m_noteSaving && attachmentPicks == 6 &&
			          v.Message().IsOpen() &&
			          v.Message().Severity() == InfoBarSeverity::Error &&
			          bytes(attachmentTarget) == bytes(v.m_path),
			      "dirty copy failure lost notes or changed prior output");
			v.Message().IsOpen(false);
			chooseNotes(attachmentTarget.wstring());
			owner.onSaveCopy(nullptr, nullptr);
			break;
		case 9:
			check(!v.m_notesDirty && !v.m_noteSaving && attachmentPicks == 7 &&
			          !owner.m_saveCopyCancel && v.Message().IsOpen() &&
			          v.Message().Severity() == InfoBarSeverity::Success,
			      "dirty copy success kept the wrong save state");
			v.showNotice(L"Couldn't save this test copy.");
			check(v.Message().Severity() == InfoBarSeverity::Error,
			      "an error kept the previous save success style");
			v.Message().IsOpen(false);
			check(engine
			          .run([path = attachmentTarget] {
				          const auto saved = pdf::openFile(path);
				          const auto notes =
				              saved.document ? saved.document->annotations(0)
				                             : std::nullopt;
				          return notes && notes->entries.size() == 1 &&
				                 notes->entries[0].contents ==
				                     "Copy retains notes";
			          })
			          .get(),
			      "Ctrl-S saved a copy without the unsaved note");
			first.copy_from(&v);
			std::filesystem::remove(attachmentTarget);
			attachmentGate = std::make_shared<winrt::handle>(
			    CreateEventW(nullptr, TRUE, FALSE, nullptr));
			check(static_cast<bool>(*attachmentGate));
			choose(attachmentTarget.wstring());
			owner.onSaveCopy(nullptr, nullptr);
			owner.openDocument(engine, L"tests/fixtures/links.pdf", 1);
			SetEvent(attachmentGate->get());
			break;
		case 10:
			check(!std::filesystem::exists(attachmentTarget),
			      "tab switch accepted a late picker result");
			owner.Tabs().SelectedIndex(0);
			break;
		case 11:
			ResetEvent(attachmentGate->get());
			choose(attachmentTarget.wstring());
			owner.onSaveCopy(nullptr, nullptr);
			owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
			SetEvent(attachmentGate->get());
			break;
		}
		++step;
		ticks = 0;
	}

	void attachmentSaveTick(MainWindow& owner) {
		if (step == 8) {
			check(first && first->m_closed && !first->m_attachmentSaving,
			      "closed attachment save stayed active");
			check(!std::filesystem::exists(attachmentTarget),
			      "closed tab saved a late picker result");
			status.Text(L"PASS: attachment save, cancel, read/write errors "
			            L"and closed picker guards");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_attachmentsPending ||
		    (v.m_attachmentSaving && step != 7)) {
			return;
		}
		const auto choose = [&](const std::wstring& path) {
			v.m_attachmentPicker = [this, path](hstring name) {
				attachmentName = name;
				++attachmentPicks;
				return attachmentChoice(hstring(path), attachmentGate);
			};
		};
		switch (step) {
		case 0:
			attachmentTarget = settingsPath;
			attachmentTarget.replace_extension(L".bin");
			v.setAttachmentsOpen(true);
			break;
		case 1:
			if (!v.m_attachmentsBuilt) {
				return;
			}
			check(v.AttachmentList().IsItemClickEnabled(),
			      "attachment rows do not invoke Save As");
			{
				// UI Automation's Invoke takes the same ItemClick path as a
				// click or Enter on the row
				using namespace Microsoft::UI::Xaml::Automation;
				const auto row =
				    v.AttachmentList().Items().GetAt(0).as<ListViewItem>();
				const auto peer =
				    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
				        row);
				const auto invoke =
				    peer ? peer.GetPattern(Peers::PatternInterface::Invoke)
				               .try_as<Provider::IInvokeProvider>()
				         : nullptr;
				if (!invoke) {
					check(ticks < 30, "attachment row has no Invoke");
					return;
				}
				choose(L"");
				invoke.Invoke();
			}
			break;
		case 2:
			check(attachmentPicks == 1,
			      "clicking an attachment row didn't open Save As");
			check(!std::filesystem::exists(attachmentTarget) &&
			          !v.Message().IsOpen(),
			      "cancel changed attachment state");
			check(attachmentName == L"notes \u03a9\U0001f600.txt",
			      "picker lost Unicode filename");
			choose(attachmentTarget.wstring());
			v.saveAttachment(0);
			v.saveAttachment(1);
			check(attachmentPicks == 2, "opened duplicate attachment picker");
			break;
		case 3: {
			std::ifstream in(attachmentTarget, std::ios::binary);
			const std::string bytes((std::istreambuf_iterator<char>(in)), {});
			check(bytes == std::string("hello\0\xff\n", 8),
			      "attachment bytes changed while saving");
			choose(attachmentTarget.wstring());
			v.saveAttachment(1);
			check(attachmentName == L"empty.bin", "picker kept a PDF path");
			break;
		}
		case 4:
			check(std::filesystem::file_size(attachmentTarget) == 0,
			      "empty attachment was not saved");
			v.saveAttachment(2);
			break;
		case 5:
			check(v.Message().IsOpen() &&
			          std::wstring_view(v.Message().Message())
			                  .find(L"Couldn't read") == 0 &&
			          std::filesystem::file_size(attachmentTarget) == 0,
			      "damaged attachment replaced a saved file");
			v.Message().IsOpen(false);
			choose(std::filesystem::absolute(v.m_path).wstring());
			v.saveAttachment(0);
			break;
		case 6:
			check(v.Message().IsOpen() &&
			          std::wstring_view(v.Message().Message())
			                  .find(L"Couldn't save") == 0,
			      "source PDF overwrite has no error");
			v.Message().IsOpen(false);
			std::filesystem::remove(attachmentTarget);
			attachmentGate = std::make_shared<winrt::handle>(
			    CreateEventW(nullptr, TRUE, FALSE, nullptr));
			check(static_cast<bool>(*attachmentGate));
			choose(attachmentTarget.wstring());
			v.saveAttachment(0);
			check(v.m_attachmentSaving, "picker didn't suspend saving");
			break;
		case 7:
			first.copy_from(&v);
			owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
			SetEvent(attachmentGate->get());
			break;
		}
		++step;
		ticks = 0;
	}

	void commentLocationsTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || ticks < 3 || v.m_commentsPending) {
			return;
		}
		if (step == 0) {
			owner.AppWindow().Resize({1000, 700});
			v.zoomTo(4, {0, 0});
			v.setCommentsOpen(true);
			++step;
			ticks = 0;
			return;
		}
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.CommentsList().Items().Size() == 13, "location row count");
		constexpr int rows[] = {0, 1, 10, 11, 12, 3};
		constexpr int pages[] = {0, 0, 1, 2, 3, 0};
		constexpr double points[][2] = {
		    {.2, .75}, {.8, .25}, {.25, .2}, {.8, .25}, {.75, .8}};
		const int round = (step - 1) / 2;
		if (round == 6) {
			if (!commentFactorStarted) {
				commentFactorStarted = true;
				v.Scroller().ZoomMode(ZoomMode::Enabled);
				commentFactorChanged = v.Scroller().ViewChanged(
				    auto_revoke, [this, weak = v.get_weak()](auto&&, auto&&) {
					    const auto self = weak.get();
					    if (!self || commentFactorSeen ||
					        self->Scroller().ZoomFactor() < 1.1f) {
						    return;
					    }
					    commentFactorSeen = true;
					    const auto scroll = self->Scroller();
					    const auto& rect = self->m_layout->pages()[0];
					    const double factor = scroll.ZoomFactor();
					    const double y = std::clamp(
					        (rect.y + .75 * rect.height) * factor -
					            scroll.ViewportHeight() / 2,
					        0.,
					        (std::max)(0., self->Pages().Height() * factor -
					                           scroll.ViewportHeight()));
					    self->followComment(0);
					    const double actual = self->m_landingAt
					                              ? self->m_landingAt->second
					                              : scroll.VerticalOffset();
					    commentFactorCorrect = std::abs(actual - y) < 1.5;
				    });
				v.Scroller().ChangeView(nullptr, nullptr, 1.5f, true);
				ticks = 0;
				return;
			}
			if (!commentFactorSeen) {
				check(ticks < 30, "comment native scale did not apply");
				return;
			}
			commentFactorChanged.revoke();
			check(commentFactorCorrect, "comment ignored native zoom factor");
			const auto scroll = v.Scroller();
			const auto& rect = v.m_layout->pages()[0];
			const double factor = scroll.ZoomFactor();
			const double centered =
			    std::clamp((rect.y + .75 * rect.height) * factor -
			                   scroll.ViewportHeight() / 2,
			               0.,
			               (std::max)(0., v.Pages().Height() * factor -
			                                  scroll.ViewportHeight()));
			check(std::abs(scroll.VerticalOffset() - centered) < 1.5,
			      "native zoom completion lost the comment center");
			const auto page = v.m_current;
			const auto offset = v.Scroller().VerticalOffset();
			v.followComment(-1);
			v.followComment(2000);
			check(v.m_current == page &&
			          v.Scroller().VerticalOffset() == offset,
			      "invalid comment row moved the view");
			status.Text(L"PASS: comment centers, row identity, crop, "
			            L"rotation, unchanged zoom and missing bounds");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		if (step % 2) {
			using namespace Microsoft::UI::Xaml::Automation;
			const auto item =
			    v.CommentsList().Items().GetAt(rows[round]).as<ListViewItem>();
			check(unbox_value<int>(item.Tag()) == rows[round],
			      "comment tag lost its row identity");
			v.CommentsList().ScrollIntoView(item);
			v.CommentsList().UpdateLayout();
			const auto peer =
			    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
			        item);
			const auto action =
			    peer ? peer.GetPattern(Peers::PatternInterface::Invoke)
			               .try_as<Provider::IInvokeProvider>()
			         : nullptr;
			if (!action) {
				check(ticks < 30, "comment location has no Invoke");
				return;
			}
			action.Invoke();
		} else {
			check(v.m_current == static_cast<std::size_t>(pages[round]),
			      "comment location reached another page");
			check(std::abs(v.m_zoom - 4) < 1e-6, "comment changed zoom");
			if (round < 5) {
				const auto& rect = v.m_layout->pages()[pages[round]];
				const auto scroll = v.Scroller();
				const double x = std::clamp(
				    v.imageLeft() + rect.x + points[round][0] * rect.width -
				        scroll.ViewportWidth() / 2,
				    0.,
				    (std::max)(0., v.Pages().Width() - scroll.ViewportWidth()));
				const double y =
				    std::clamp(rect.y + points[round][1] * rect.height -
				                   scroll.ViewportHeight() / 2,
				               0.,
				               (std::max)(0., v.Pages().Height() -
				                                  scroll.ViewportHeight()));
				check(std::abs(scroll.HorizontalOffset() - x) < 1.5 &&
				          std::abs(scroll.VerticalOffset() - y) < 1.5,
				      "comment position was not centered: actual " +
				          std::to_string(scroll.HorizontalOffset()) + "," +
				          std::to_string(scroll.VerticalOffset()) +
				          " expected " + std::to_string(x) + "," +
				          std::to_string(y) + " factor " +
				          std::to_string(scroll.ZoomFactor()) + " size " +
				          std::to_string(rect.width) + "," +
				          std::to_string(rect.height));
			}
		}
		++step;
		ticks = 0;
	}
	void annotationActionsTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
		    v.m_annotationPending) {
			return;
		}
		const auto invoke = [&](const FrameworkElement& item) {
			using namespace Microsoft::UI::Xaml::Automation;
			Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item)
			    .GetPattern(Peers::PatternInterface::Invoke)
			    .as<Provider::IInvokeProvider>()
			    .Invoke();
		};
		const auto dialogButton = [&](const wchar_t* name) {
			std::function<Button(DependencyObject)> find;
			find = [&](const DependencyObject& node) -> Button {
				if (const auto button = node.try_as<Button>();
				    button && button.Name() == name) {
					return button;
				}
				for (int i = 0;
				     i < Media::VisualTreeHelper::GetChildrenCount(node); ++i) {
					if (const auto button =
					        find(Media::VisualTreeHelper::GetChild(node, i))) {
						return button;
					}
				}
				return nullptr;
			};
			const auto button = find(v.m_noteDialog);
			check(button && button.IsEnabled(), "annotation dialog action");
			invoke(button);
		};
		const auto annotations = [&] {
			const auto reply =
			    std::make_shared<std::promise<pdf::AnnotationsResult>>();
			auto future = reply->get_future();
			check(v.m_session->annotations(0,
			                               [reply](auto value) {
				                               reply->set_value(
				                                   std::move(value));
			                               }),
			      "annotation snapshot not queued");
			check(future.wait_for(std::chrono::seconds(10)) ==
			          std::future_status::ready,
			      "annotation snapshot timed out");
			auto result = future.get();
			check(!result.canceled && result.annotations.has_value(),
			      "annotation snapshot missing");
			return std::move(*result.annotations);
		};
		switch (step) {
		case 0: {
			check(v.m_textHighlight.Items().Size() == 5,
			      "highlight palette is incomplete");
			const wchar_t* labels[]{L"Yellow", L"Green", L"Light blue", L"Pink",
			                        L"Red"};
			const Windows::UI::Color colors[]{{255, 255, 240, 102},
			                                  {255, 125, 240, 102},
			                                  {255, 143, 222, 249},
			                                  {255, 247, 153, 209},
			                                  {255, 235, 73, 73}};
			for (unsigned i = 0; i < 5; ++i) {
				const auto item =
				    v.m_textHighlight.Items().GetAt(i).as<MenuFlyoutItem>();
				check(item.Text() == labels[i] && item.Icon() &&
				          item.Icon()
				                  .as<FontIcon>()
				                  .Foreground()
				                  .as<Media::SolidColorBrush>()
				                  .Color() == colors[i],
				      "highlight color label or swatch differs");
			}
			v.m_textMenu.Opened([this](auto&&, auto&&) { ++annotationMenus; });
			v.requestText(0);
			break;
		}
		case 1: {
			if (!v.m_text.contains(0)) {
				return;
			}
			const auto point = [&](int index) {
				const auto box =
				    v.viewBox(v.m_text.at(0).text->characters[index].box);
				const auto& page = v.m_layout->pages()[0];
				return Windows::Foundation::Point{
				    static_cast<float>(page.x +
				                       (box.left + box.right) / 2 * page.width),
				    static_cast<float>(page.y + (box.top + box.bottom) / 2 *
				                                    page.height)};
			};
			v.beginSelection(point(0));
			v.extendSelection(point(4));
			v.endSelection();
			break;
		}
		case 2:
			check(annotationMenus == 1 && v.m_selection &&
			          !v.m_selectionMenuPending,
			      "completed selection did not open its menu once");
			v.m_textMenu.Hide();
			v.Scroller().Focus(FocusState::Programmatic);
			break;
		case 3:
			check(owner.routeKey('A', helpers::SHORTCUT_CTRL, false),
			      "Ctrl+A did not reach the viewer");
			break;
		case 4:
			check(annotationMenus == 2 && v.m_selection &&
			          v.m_selection->anchor.page == 0 &&
			          v.m_selection->focus.page ==
			              static_cast<int>(v.m_sizes.size() - 1),
			      "Select All menu or document range missing");
			v.m_selection = pdf::TextSelection{{0, 0}, {0, 4}};
			++v.m_selectionVersion;
			invoke(v.m_textHighlight.Items().GetAt(3).as<MenuFlyoutItem>());
			v.m_textMenu.Hide();
			v.setCommentsOpen(true);
			break;
		case 5: {
			if (!v.m_commentScan.done()) {
				return;
			}
			const auto snapshot = annotations();
			check(snapshot.entries.size() == 1 &&
			          snapshot.entries[0].subtype == "Highlight",
			      "palette item did not create a highlight");
			annotationSnapshot = snapshot.entries[0];
			check(!annotationSnapshot->displayQuads.empty() &&
			          annotationSnapshot->displayBounds.has_value(),
			      "markup lacks display hit and outline geometry");
			const auto& q = annotationSnapshot->displayQuads[0];
			const auto center =
			    helpers::rotatePoint({(q.x1 + q.x2 + q.x3 + q.x4) / 4.,
			                          (q.y1 + q.y2 + q.y3 + q.y4) / 4.},
			                         v.m_rotation);
			const auto& page = v.m_layout->pages()[0];
			v.requestAnnotationAt(
			    {static_cast<float>(page.x + center.x * page.width),
			     static_cast<float>(page.y + center.y * page.height)});
			break;
		}
		case 6:
			check(v.m_selectedAnnotation &&
			          v.m_selectedAnnotation->target ==
			              annotationSnapshot->target &&
			          v.m_annotationMenu.Items().Size() == 2 &&
			          v.m_device.annotationDashes &&
			          v.m_device.annotationOutline,
			      "markup click did not select its menu and dashed outline");
			v.m_annotationMenu.Hide();
			v.PageBox().Focus(FocusState::Programmatic);
			check(!owner.routeKey(VK_DELETE, 0, false) && !v.m_noteBusy &&
			          v.m_selectedAnnotation,
			      "Delete in a text field reached the annotation");
			v.Scroller().Focus(FocusState::Programmatic);
			check(owner.routeKey(VK_DELETE, 0, true) && !v.m_noteBusy,
			      "repeated Delete removed an annotation");
			annotationRow =
			    v.CommentsList().Items().GetAt(0).as<ListViewItem>();
			check(owner.routeKey(VK_DELETE, 0, false) && v.m_noteBusy,
			      "Delete did not queue the selected markup");
			break;
		case 7: {
			if (!v.m_commentScan.done()) {
				return;
			}
			check(annotations().entries.empty() && v.m_notesDirty &&
			          !v.m_selectedAnnotation,
			      "markup deletion kept an annotation or stale selection");
			const auto stale = annotationRow.ContextFlyout().as<MenuFlyout>();
			stale.ShowAt(v.CommentsList());
			check(!stale.Items().GetAt(1).as<MenuFlyoutItem>().IsEnabled(),
			      "stale row context menu can redirect a deletion");
			stale.Hide();
			v.submitNote(0, u"native note", {});
			break;
		}
		case 8:
			if (!v.m_commentScan.done()) {
				return;
			}
			check(v.m_commentScan.rows().size() == 1 &&
			          v.m_commentScan.rows()[0].subtype == "Text",
			      "new hidden note missing from Comments");
			v.CommentsList().SelectedIndex(0);
			invoke(v.EditNoteButton());
			break;
		case 9:
			if (!v.m_noteDialog) {
				return;
			}
			v.m_noteDialog.Content()
			    .as<StackPanel>()
			    .Children()
			    .GetAt(1)
			    .as<TextBox>()
			    .Text(L"");
			if (v.m_noteDialog.PrimaryButtonText() != L"Delete" ||
			    !v.m_noteDialog.IsPrimaryButtonEnabled()) {
				check(ticks < 20, "blank existing note did not offer Delete");
				return;
			}
			check(v.m_noteDialog.PrimaryButtonText() == L"Delete" &&
			          v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "blank existing note did not offer Delete");
			dialogButton(L"PrimaryButton");
			break;
		case 10:
			if (v.m_noteDialog || !v.m_commentScan.done()) {
				return;
			}
			check(annotations().entries.empty(), "blank note was not deleted");
			v.m_selection = pdf::TextSelection{{0, 0}, {0, 4}};
			++v.m_selectionVersion;
			v.markSelection(pdf::TextMarkup::Underline);
			break;
		case 11: {
			if (!v.m_commentScan.done()) {
				return;
			}
			const auto snapshot = annotations();
			check(snapshot.entries.size() == 1 &&
			          snapshot.entries[0].subtype == "Underline",
			      "comment-removal markup setup failed");
			annotationSnapshot = snapshot.entries[0];
			const auto target = annotationSnapshot->target;
			const auto reply =
			    std::make_shared<std::promise<pdf::NoteResult>>();
			auto future = reply->get_future();
			check(v.m_session->updateMarkupComment(
			          0, target.index, pdf::TextMarkup::Underline, {u"", false},
			          u"native comment",
			          [reply](auto r) { reply->set_value(r); },
			          target.revision),
			      "comment setup not queued");
			check(future.wait_for(std::chrono::seconds(10)) ==
			              std::future_status::ready &&
			          future.get().changed,
			      "comment setup failed");
			v.refreshNotes();
			v.showComments();
			break;
		}
		case 12:
			if (!v.m_commentScan.done()) {
				return;
			}
			v.CommentsList().SelectedIndex(0);
			invoke(v.EditNoteButton());
			break;
		case 13:
			if (!v.m_noteDialog) {
				return;
			}
			check(v.m_noteDialog.SecondaryButtonText() == L"Remove comment" &&
			          v.m_noteDialog.IsSecondaryButtonEnabled(),
			      "markup editor lacks Remove comment");
			dialogButton(L"SecondaryButton");
			break;
		case 14: {
			if (v.m_noteDialog || !v.m_commentScan.done()) {
				return;
			}
			const auto snapshot = annotations();
			check(snapshot.entries.size() == 1 &&
			          snapshot.entries[0].contents.empty() &&
			          !snapshot.entries[0].quads.empty() &&
			          snapshot.entries[0].quads.size() ==
			              annotationSnapshot->quads.size() &&
			          snapshot.entries[0].quads[0].x1 ==
			              annotationSnapshot->quads[0].x1 &&
			          snapshot.entries[0].quads[0].y4 ==
			              annotationSnapshot->quads[0].y4,
			      "removing comment deleted or moved its markup");
			const auto row =
			    v.CommentsList().Items().GetAt(0).as<ListViewItem>();
			const auto menu = row.ContextFlyout().as<MenuFlyout>();
			menu.ShowAt(v.CommentsList());
			check(v.annotationTarget() == snapshot.entries[0].target &&
			          menu.Items().GetAt(1).as<MenuFlyoutItem>().IsEnabled(),
			      "row context menu did not capture its exact target");
			invoke(menu.Items().GetAt(1).as<MenuFlyoutItem>());
			menu.Hide();
			break;
		}
		case 15:
			if (!v.m_commentScan.done()) {
				return;
			}
			check(annotations().entries.empty() &&
			          !v.DeleteNoteButton().IsEnabled(),
			      "row context deletion kept its target or enabled Delete");
			status.Text(L"PASS: selection menus, palette, markup hit/outline, "
			            L"Delete focus/repeat guards, stale row, blank-note "
			            L"deletion and comment removal preserving markup");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void highlightTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || (v.m_noteBusy && step != 2) || v.m_commentsPending ||
		    (notesSave &&
		     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
			return;
		}
		const auto invoke = [&](const FrameworkElement& item) {
			using namespace Microsoft::UI::Xaml::Automation;
			Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item)
			    .GetPattern(Peers::PatternInterface::Invoke)
			    .as<Provider::IInvokeProvider>()
			    .Invoke();
		};
		const auto item =
		    selectionMarkup == pdf::TextMarkup::Underline ? v.m_textUnderline
		    : selectionMarkup == pdf::TextMarkup::StrikeOut
		        ? v.m_textStrikeOut
		        : v.m_textHighlight.Items().GetAt(0).as<MenuFlyoutItem>();
		switch (step) {
		case 0:
			check(!v.canMarkSelection(), "empty selection could highlight");
			v.m_selection = pdf::TextSelection{{1, 4}, {0, 0}};
			++v.m_selectionVersion;
			v.m_copyAllowed = false;
			v.m_textMenu.ShowAt(v.Scroller());
			break;
		case 1:
			check(v.m_textHighlight.IsEnabled() &&
			          v.m_textUnderline.IsEnabled() &&
			          v.m_textStrikeOut.IsEnabled() &&
			          !v.m_textCopy.IsEnabled(),
			      "highlight depends on copy permission");
			notesGate = std::make_shared<std::promise<void>>();
			engine.post(
			    [wait = notesGate->get_future().share()] { wait.wait(); });
			invoke(item);
			v.m_textMenu.Hide();
			check(v.m_noteBusy && !v.canMarkSelection(),
			      "highlight did not hold writes busy");
			v.m_selection = pdf::TextSelection{{4, 1}, {4, 2}};
			++v.m_selectionVersion;
			v.Visibility(Visibility::Collapsed);
			break;
		case 2: {
			owner.closeTab(owner.m_tabs.front().tab);
			check(owner.m_tabs.size() == 1,
			      "close discarded pending highlight");
			HWND handle{};
			check_hresult(
			    owner.try_as<::IWindowNative>()->get_WindowHandle(&handle));
			SendMessageW(handle, WM_CLOSE, 0, 0);
			check(!owner.m_closing, "window closed during highlight");
			const auto blocked = owner.saveNotes(current);
			check(blocked.Status() ==
			              Windows::Foundation::AsyncStatus::Completed &&
			          !blocked.GetResults(),
			      "save ran before highlight completed");
			notesGate->set_value();
			notesGate.reset();
			break;
		}
		case 3:
			check(v.m_notesDirty &&
			          v.m_selection == pdf::TextSelection{{4, 1}, {4, 2}},
			      "completed hidden edit lost dirty state or newer selection");
			v.Visibility(Visibility::Visible);
			v.setCommentsOpen(true);
			break;
		case 4:
			if (!v.m_commentScan.done()) {
				return;
			}
			check(v.m_commentScan.rows().size() == 2 &&
			          v.m_commentScan.rows()[0].subtype == markupSubtype &&
			          v.m_commentScan.rows()[1].page == 1,
			      "highlight comments not refreshed");
			notesPath = std::filesystem::temp_directory_path() /
			            (L"sable_highlight_" +
			             std::to_wstring(GetCurrentProcessId()) + L".pdf");
			owner.m_notesPicker = [path =
			                           hstring(notesPath.wstring())](hstring) {
				return attachmentChoice(path, {});
			};
			notesSave = owner.saveNotes(current);
			break;
		case 5:
			check(notesSave.GetResults() && !v.m_notesDirty,
			      "highlight save failed");
			check(engine
			          .run([path = notesPath, subtype = markupSubtype] {
				          const auto saved = pdf::openFile(path);
				          if (!saved.document) {
					          return false;
				          }
				          for (int page = 0; page < 2; ++page) {
					          const auto notes =
					              saved.document->annotations(page);
					          if (!notes || notes->entries.size() != 1 ||
					              notes->entries[0].subtype != subtype) {
						          return false;
					          }
				          }
				          const auto other = saved.document->annotations(4);
				          return other && other->entries.empty();
			          })
			          .get(),
			      "saved highlight geometry/pages missing");
			v.m_textMenu.ShowAt(v.Scroller());
			break;
		case 6:
			invoke(item);
			v.m_textMenu.Hide();
			break;
		case 7:
			if (!v.m_commentScan.done()) {
				return;
			}
			check(
			    v.m_notesDirty && !v.m_selection &&
			        v.m_commentScan.rows().size() == 3,
			    "successful highlight kept old selection or lost dirty state");
			v.CommentsList().SelectedIndex(2);
			check(v.EditNoteButton().IsEnabled(),
			      "Text markup comment editor unavailable");
			v.followComment(2);
			notesSave = owner.saveNotes(current);
			break;
		case 8:
			check(notesSave.GetResults() && !v.m_notesDirty && v.m_current == 4,
			      "second save or highlight navigation failed");
			v.m_selection = pdf::TextSelection{{-1, 0}, {-1, 4}};
			++v.m_selectionVersion;
			v.markSelection(selectionMarkup);
			break;
		case 9:
			check(!v.m_notesDirty &&
			          v.m_selection == pdf::TextSelection{{-1, 0}, {-1, 4}} &&
			          v.Message().IsOpen(),
			      "refused highlight lost selection or savepoint");
			v.clearTextSelection();
			v.m_textMenu.ShowAt(v.Scroller());
			check(!v.m_textHighlight.IsEnabled() &&
			          !v.m_textUnderline.IsEnabled() &&
			          !v.m_textStrikeOut.IsEnabled(),
			      "empty context highlight still enabled");
			v.m_textMenu.Hide();
			std::filesystem::remove(notesPath);
			status.Text(
			    L"PASS: text markup menu, independent copy permission, "
			    L"pending close/save, hidden completion, newer selection, "
			    L"comments, saved copy and retained refused selection");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void choiceFieldTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_noteBusy || v.m_noteSaving ||
		    (notesSave &&
		     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
			return;
		}
		using namespace Microsoft::UI::Xaml::Automation;
		const auto button = [&](const wchar_t* name) {
			std::function<Button(DependencyObject)> find;
			find = [&](const DependencyObject& node) -> Button {
				if (const auto item = node.try_as<Button>();
				    item && item.Name() == name) {
					return item;
				}
				for (int i = 0;
				     i < Media::VisualTreeHelper::GetChildrenCount(node); ++i) {
					if (const auto item =
					        find(Media::VisualTreeHelper::GetChild(node, i))) {
						return item;
					}
				}
				return nullptr;
			};
			const auto item = find(v.m_noteDialog);
			check(item != nullptr, "choice dialog button missing");
			check(item.IsEnabled(), "choice dialog button disabled");
			Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item)
			    .GetPattern(Peers::PatternInterface::Invoke)
			    .as<Provider::IInvokeProvider>()
			    .Invoke();
		};
		const auto read = [&] {
			const auto reply =
			    std::make_shared<std::promise<pdf::FormFieldsResult>>();
			auto future = reply->get_future();
			check(v.m_session->formFields(0,
			                              [reply](auto result) {
				                              reply->set_value(
				                                  std::move(result));
			                              }),
			      "choice snapshot not queued");
			check(future.wait_for(std::chrono::seconds(10)) ==
			          std::future_status::ready,
			      "choice snapshot timed out");
			const auto result = future.get();
			check(result.fields && !result.fields->limited &&
			          result.fields->entries.size() == 1,
			      "choice snapshot failed");
			return result.fields;
		};
		const auto open = [&] {
			const auto field = read()->entries[0];
			check(field.displayBounds.has_value(), "choice bounds missing");
			const auto& b = *field.displayBounds;
			const auto& p = v.m_layout->pages()[0];
			v.requestFormAt(
			    {static_cast<float>(p.x + p.width * (b.left + b.right) / 2),
			     static_cast<float>(p.y + p.height * (b.top + b.bottom) / 2)});
		};
		const auto options = [&] {
			check(v.m_noteDialog != nullptr, "choice dialog missing");
			const auto control = v.m_noteDialog.Content()
			                         .as<StackPanel>()
			                         .Children()
			                         .GetAt(1)
			                         .as<Primitives::Selector>();
			check(choiceList ? control.try_as<ListBox>() != nullptr
			                 : control.try_as<ComboBox>() != nullptr,
			      "wrong native choice control");
			check(AutomationProperties::GetName(control) == L"Field option",
			      "choice automation name missing");
			return control;
		};
		const auto selected = [list =
		                           choiceList](const pdf::FormFields& fields) {
			if (fields.entries.size() != 1) {
				return false;
			}
			const auto& field = fields.entries[0];
			return field.value == "caf\u00e9" && field.name == "choice" &&
			       field.type == (list ? pdf::FormFieldType::ListBox
			                           : pdf::FormFieldType::ComboBox) &&
			       field.options && field.options->size() == 3 &&
			       (*field.options)[0].index == 0 &&
			       (*field.options)[0].label == "First" &&
			       !(*field.options)[0].selected &&
			       (*field.options)[1].index == 1 &&
			       (*field.options)[1].label == "Same" &&
			       !(*field.options)[1].selected &&
			       (*field.options)[2].index == 2 &&
			       (*field.options)[2].label == "Same" &&
			       (*field.options)[2].selected;
		};
		switch (step) {
		case 0:
			open();
			break;
		case 1:
			if (!v.m_noteDialog) {
				return;
			}
			check(options().SelectedIndex() == 0 &&
			          options().Items().Size() == 3,
			      "choice initial state changed");
			for (uint32_t i = 0; i < 3; ++i) {
				const auto item =
				    options().Items().GetAt(i).as<ContentControl>();
				check(choiceList ? item.try_as<ListBoxItem>() != nullptr
				                 : item.try_as<ComboBoxItem>() != nullptr,
				      "choice container type changed");
				check(unbox_value<hstring>(item.Content()) ==
				          (i == 0 ? L"First" : L"Same"),
				      "choice labels changed");
			}
			check(options().Items().GetAt(1) != options().Items().GetAt(2),
			      "duplicate labels share an automation item");
			options().SelectedIndex(-1);
			check(!v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "empty selection allowed Apply");
			options().SelectedIndex(0);
			if (!choiceList) {
				options().as<ComboBox>().IsDropDownOpen(true);
			}
			break;
		case 2:
			if (!choiceList) {
				const auto combo = options().as<ComboBox>();
				check(combo.IsDropDownOpen(), "choice popup did not open");
				combo.IsDropDownOpen(false);
			}
			break;
		case 3:
			check(v.m_noteDialog && options().SelectedIndex() == 0 &&
			          v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "restored choice did not enable Apply");
			check(choiceList || !options().as<ComboBox>().IsDropDownOpen(),
			      "choice popup did not close");
			button(L"PrimaryButton");
			break;
		case 4:
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty && !v.m_formsDirty,
			      "choice no-op dirtied PDF");
			open();
			break;
		case 5:
			if (!v.m_noteDialog) {
				return;
			}
			options().SelectedIndex(2);
			button(L"PrimaryButton");
			check(v.m_noteBusy && !options().IsEnabled() &&
			          !v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "queued choice stayed enabled");
			break;
		case 6:
			if (v.m_noteDialog) {
				return;
			}
			check(v.m_notesDirty && v.m_formsDirty && selected(*read()),
			      "choice Apply failed");
			notesPath = buttonFieldPath.parent_path() / "selected copy.pdf";
			owner.m_notesPicker = [path =
			                           hstring(notesPath.wstring())](hstring) {
				return attachmentChoice(path, {});
			};
			notesSave = owner.saveNotes(current);
			break;
		case 7:
			check(notesSave.GetResults() && !v.m_notesDirty && !v.m_formsDirty,
			      "choice save did not clear dirty state");
			check(engine
			          .run([path = notesPath, selected] {
				          const auto saved = pdf::openFile(path);
				          const auto fields =
				              saved.document ? saved.document->formFields(0)
				                             : std::nullopt;
				          return fields && selected(*fields);
			          })
			          .get(),
			      "saved choice value/position changed");
			open();
			break;
		case 8:
			if (!v.m_noteDialog) {
				return;
			}
			check(options().SelectedIndex() == 2,
			      "saved choice opened incorrectly");
			options().SelectedIndex(0);
			button(L"CloseButton");
			break;
		case 9: {
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty && !v.m_formsDirty && selected(*read()),
			      "Cancel changed choice field");
			const auto snapshot = read();
			auto field = snapshot->entries[0];
			field.name = "wrong";
			v.editFormField(0, snapshot->revision, std::move(field));
			break;
		}
		case 10:
			if (!v.m_noteDialog) {
				return;
			}
			options().SelectedIndex(1);
			button(L"PrimaryButton");
			break;
		case 11:
			check(v.m_noteDialog && options().IsEnabled() &&
			          options().SelectedIndex() == 1 &&
			          v.m_noteDialog.IsPrimaryButtonEnabled() &&
			          !v.m_noteDialog.Content()
			               .as<StackPanel>()
			               .Children()
			               .GetAt(2)
			               .as<TextBlock>()
			               .Text()
			               .empty(),
			      "refused choice draft lost or disabled");
			button(L"CloseButton");
			break;
		case 12: {
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty && !v.m_formsDirty && selected(*read()),
			      "refused choice changed PDF");
			const auto snapshot = read();
			auto field = snapshot->entries[0];
			check(field.options.has_value(), "unset choice options missing");
			field.value.clear();
			for (auto& option : *field.options) {
				option.selected = false;
			}
			v.editFormField(0, snapshot->revision, std::move(field));
			break;
		}
		case 13:
			if (!v.m_noteDialog) {
				return;
			}
			check(options().SelectedIndex() == -1 &&
			          !v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "unset choice opened with Apply enabled");
			options().SelectedIndex(2);
			check(v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "unset choice did not enable Apply after selection");
			button(L"CloseButton");
			break;
		case 14: {
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty && !v.m_formsDirty && selected(*read()),
			      "unset choice Cancel changed PDF");
			std::ifstream original(buttonFieldPath, std::ios::binary);
			check(std::string(std::istreambuf_iterator<char>(original), {}) ==
			          choicePdf(!choiceList),
			      "choice source changed");
			std::fwprintf(
			    stderr,
			    L"PASS: %ls choice %lsno-op, selection, Apply, "
			    L"save/reopen, Cancel, refusal and unset draft; %ls\n",
			    choiceList ? L"list" : L"combo",
			    choiceList ? L"" : L"popup cycle, ", notesPath.c_str());
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		}
		++step;
		ticks = 0;
	}
	void buttonFieldTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_noteBusy || v.m_noteSaving ||
		    (notesSave &&
		     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
			return;
		}
		using namespace Microsoft::UI::Xaml::Automation;
		const auto peer = [](const FrameworkElement& element) {
			return Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
			    element);
		};
		const auto button = [&](const wchar_t* name) {
			std::function<Button(DependencyObject)> find;
			find = [&](const DependencyObject& node) -> Button {
				if (const auto item = node.try_as<Button>();
				    item && item.Name() == name) {
					return item;
				}
				for (int i = 0;
				     i < Media::VisualTreeHelper::GetChildrenCount(node); ++i) {
					if (const auto item =
					        find(Media::VisualTreeHelper::GetChild(node, i))) {
						return item;
					}
				}
				return nullptr;
			};
			const auto item = find(v.m_noteDialog);
			check(item && item.IsEnabled(),
			      "button field dialog button unavailable");
			peer(item)
			    .GetPattern(Peers::PatternInterface::Invoke)
			    .as<Provider::IInvokeProvider>()
			    .Invoke();
		};
		const auto read = [&] {
			const auto reply =
			    std::make_shared<std::promise<pdf::FormFieldsResult>>();
			auto future = reply->get_future();
			check(v.m_session->formFields(0,
			                              [reply](auto value) {
				                              reply->set_value(
				                                  std::move(value));
			                              }),
			      "button field snapshot not queued");
			check(future.wait_for(std::chrono::seconds(10)) ==
			          std::future_status::ready,
			      "button field snapshot timed out");
			const auto result = future.get();
			check(result.fields && !result.fields->limited &&
			          result.fields->entries.size() == (radioOnly ? 2u : 1u),
			      "button field snapshot failed");
			return result.fields;
		};
		const auto open = [&](int index = 0) {
			const auto snapshot = read();
			const auto& b = snapshot->entries[index].displayBounds;
			check(b.has_value(), "button field has no visible bounds");
			const auto& p = v.m_layout->pages()[0];
			v.requestFormAt(
			    {static_cast<float>(p.x + p.width * (b->left + b->right) / 2),
			     static_cast<float>(p.y +
			                        p.height * (b->top + b->bottom) / 2)});
		};
		const auto choice = [&] {
			check(v.m_noteDialog != nullptr, "button field dialog missing");
			const auto control = v.m_noteDialog.Content()
			                         .as<StackPanel>()
			                         .Children()
			                         .GetAt(1)
			                         .as<Primitives::ToggleButton>();
			check(radioOnly ? control.try_as<RadioButton>() != nullptr
			                : control.try_as<CheckBox>() != nullptr,
			      "wrong native button field control");
			return control;
		};
		const auto select = [&] {
			if (radioOnly) {
				peer(choice())
				    .GetPattern(Peers::PatternInterface::SelectionItem)
				    .as<Provider::ISelectionItemProvider>()
				    .Select();
			} else {
				peer(choice())
				    .GetPattern(Peers::PatternInterface::Toggle)
				    .as<Provider::IToggleProvider>()
				    .Toggle();
			}
		};
		switch (step) {
		case 0:
			open();
			break;
		case 1:
			if (!v.m_noteDialog) {
				return;
			}
			check(unbox_value_or<bool>(choice().IsChecked(), !radioOnly) ==
			          radioOnly,
			      "button field snapshot state changed");
			button(L"PrimaryButton");
			break;
		case 2:
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty && !v.m_formsDirty,
			      "button field no-op dirtied PDF");
			open(radioOnly ? 1 : 0);
			break;
		case 3:
			if (!v.m_noteDialog) {
				return;
			}
			if (radioOnly) {
				check(!v.m_noteDialog.IsPrimaryButtonEnabled() &&
				          !unbox_value_or<bool>(choice().IsChecked(), true),
				      "unselected radio option allowed Apply");
			}
			select();
			check(unbox_value_or<bool>(choice().IsChecked(), false),
			      "native button field did not select");
			button(L"PrimaryButton");
			check(v.m_noteBusy && !choice().IsEnabled() &&
			          !v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "queued button field stayed enabled");
			break;
		case 4:
			if (v.m_noteDialog) {
				return;
			}
			check(v.m_notesDirty && v.m_formsDirty &&
			          read()->entries[radioOnly ? 1 : 0].checked == true &&
			          (!radioOnly || read()->entries[0].checked == false),
			      "button field Apply failed");
			notesPath = buttonFieldPath.parent_path() / "selected copy.pdf";
			owner.m_notesPicker = [path =
			                           hstring(notesPath.wstring())](hstring) {
				return attachmentChoice(path, {});
			};
			notesSave = owner.saveNotes(current);
			break;
		case 5:
			check(notesSave.GetResults() && !v.m_notesDirty && !v.m_formsDirty,
			      "button field save did not clear dirty state");
			check(engine
			          .run([path = notesPath, radio = radioOnly] {
				          const auto saved = pdf::openFile(path);
				          const auto fields =
				              saved.document ? saved.document->formFields(0)
				                             : std::nullopt;
				          return fields &&
				                 fields->entries.size() == (radio ? 2u : 1u) &&
				                 fields->entries[radio ? 1 : 0].checked ==
				                     true &&
				                 (!radio ||
				                  fields->entries[0].checked == false) &&
				                 fields->entries[0].value ==
				                     (radio ? "Next choice" : "Approved now");
			          })
			          .get(),
			      "saved button field export/state missing");
			open();
			break;
		case 6:
			if (!v.m_noteDialog) {
				return;
			}
			check(unbox_value_or<bool>(choice().IsChecked(), radioOnly) !=
			          radioOnly,
			      "saved button field opened with wrong state");
			select();
			check(unbox_value_or<bool>(choice().IsChecked(), !radioOnly) ==
			          radioOnly,
			      "Cancel draft did not change state");
			button(L"CloseButton");
			break;
		case 7: {
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty && !v.m_formsDirty &&
			          read()->entries[radioOnly ? 1 : 0].checked == true &&
			          (!radioOnly || read()->entries[0].checked == false),
			      "Cancel changed button field");
			if (radioOnly) {
				const auto snapshot = read();
				auto field = snapshot->entries[1];
				check(field.control.has_value(),
				      "radio control metadata missing");
				++field.control->count;
				v.editFormField(0, snapshot->revision, std::move(field));
				break;
			}
			[[fallthrough]];
		}
		case 10: {
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty && !v.m_formsDirty,
			      "refused button field dirtied PDF");
			std::ifstream original(buttonFieldPath, std::ios::binary);
			check(std::string(std::istreambuf_iterator<char>(original), {}) ==
			          (radioOnly ? radioPdf() : checkBoxPdf()),
			      "button field source changed");
			std::fwprintf(stderr,
			              L"PASS: %ls no-op, native selection, Apply, "
			              L"saved export/state and Cancel; %ls\n",
			              radioOnly ? L"radio/refusal" : L"checkbox",
			              notesPath.c_str());
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		case 8:
			if (!v.m_noteDialog) {
				return;
			}
			button(L"PrimaryButton");
			break;
		case 9:
			check(v.m_noteDialog && choice().IsEnabled() &&
			          unbox_value_or<bool>(choice().IsChecked(), false) &&
			          v.m_noteDialog.IsPrimaryButtonEnabled() &&
			          !v.m_noteDialog.Content()
			               .as<StackPanel>()
			               .Children()
			               .GetAt(2)
			               .as<TextBlock>()
			               .Text()
			               .empty(),
			      "refused radio choice was lost or disabled");
			button(L"CloseButton");
			break;
		}
		++step;
		ticks = 0;
	}
	void noteEditTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || (v.m_noteBusy && step != 4 && step != 5) ||
		    v.m_commentsPending ||
		    (notesSave &&
		     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
			return;
		}
		const auto invoke = [&](const FrameworkElement& element) {
			using namespace Microsoft::UI::Xaml::Automation;
			Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)
			    .GetPattern(Peers::PatternInterface::Invoke)
			    .as<Provider::IInvokeProvider>()
			    .Invoke();
		};
		const auto button = [&](const wchar_t* name) {
			std::function<Button(DependencyObject)> find;
			find = [&](const DependencyObject& element) -> Button {
				if (const auto item = element.try_as<Button>();
				    item && item.Name() == name) {
					return item;
				}
				for (int i = 0;
				     i < Media::VisualTreeHelper::GetChildrenCount(element);
				     ++i) {
					if (const auto item = find(
					        Media::VisualTreeHelper::GetChild(element, i))) {
						return item;
					}
				}
				return nullptr;
			};
			const auto item = find(v.m_noteDialog);
			check(item && item.IsEnabled(), "note edit button unavailable");
			invoke(item);
		};
		const auto change = [&](std::u16string oldText,
		                        std::u16string newText) {
			const auto reply =
			    std::make_shared<std::promise<pdf::NoteResult>>();
			auto future = reply->get_future();
			const auto callback = [reply](auto result) {
				reply->set_value(result);
			};
			bool present = true;
			if (markupCommentOnly && oldText == u"Location 1") {
				oldText.clear();
				present = false;
			}
			check(markupCommentOnly
			          ? v.m_session->updateMarkupComment(0, 0, selectionMarkup,
			                                             {oldText, present},
			                                             newText, callback)
			          : v.m_session->updateTextNote(0, 0, oldText, newText,
			                                        callback),
			      "external edit not queued");
			check(future.wait_for(std::chrono::seconds(10)) ==
			          std::future_status::ready,
			      "external edit reply missing");
			check(future.get().index == 0, "external edit refused");
		};
		const std::u16string raw = u"Raw\r\nline\tend";
		switch (step) {
		case 0:
			v.setCommentsOpen(true);
			if (!v.m_commentScan.done()) {
				return;
			}
			check(!v.EditNoteButton().IsEnabled(), "unselected note editable");
			if (markupCommentOnly && !commentFactorStarted) {
				commentFactorStarted = true;
				const auto reply =
				    std::make_shared<std::promise<pdf::MarkupResult>>();
				auto future = reply->get_future();
				check(v.m_session->markText(
				          {{0, 0}, {0, 4}}, selectionMarkup,
				          [reply](auto r) { reply->set_value(r); }),
				      "markup setup not queued");
				check(future.wait_for(std::chrono::seconds(10)) ==
				              std::future_status::ready &&
				          future.get().changed,
				      "markup setup failed");
				v.refreshNotes();
			}
			if (!markupCommentOnly || !commentFactorSeen) {
				change(u"Location 1", raw);
				commentFactorSeen = true;
				if (markupCommentOnly) {
					v.refreshNotes();
					v.setCommentsOpen(true);
					return;
				}
			}
			v.CommentsList().SelectedIndex(0);
			check(v.EditNoteButton().IsEnabled(),
			      "selected Text note disabled");
			invoke(v.EditNoteButton());
			break;
		case 1:
			if (!v.m_noteDialog) {
				return;
			}
			check(unbox_value<hstring>(v.m_noteDialog.Title()) ==
			          (markupCommentOnly ? L"Edit comment" : L"Edit note"),
			      "wrong editor");
			owner.closeTab(owner.m_tabs.front().tab);
			check(owner.m_tabs.size() == 1,
			      "close dismissed existing-note editor");
			button(L"PrimaryButton");
			break;
		case 2: {
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty,
			      "unchanged normalized text dirtied savepoint");
			const auto reply =
			    std::make_shared<std::promise<pdf::TextNoteResult>>();
			auto future = reply->get_future();
			const auto callback = [reply](auto result) {
				reply->set_value(result);
			};
			check(markupCommentOnly ? v.m_session->markupComment(
			                              0, 0, selectionMarkup, callback)
			                        : v.m_session->textNote(0, 0, callback),
			      "raw note read not queued");
			check(future.wait_for(std::chrono::seconds(10)) ==
			          std::future_status::ready,
			      "raw note read missing");
			check(future.get().contents == raw,
			      "unchanged editor normalized PDF contents");
			invoke(v.EditNoteButton());
			break;
		}
		case 3:
			if (!v.m_noteDialog) {
				return;
			}
			v.m_noteDialog.Content()
			    .as<StackPanel>()
			    .Children()
			    .GetAt(1)
			    .as<TextBox>()
			    .Text(L"Edited native \u03a9\U0001f600");
			notesGate = std::make_shared<std::promise<void>>();
			engine.post(
			    [wait = notesGate->get_future().share()] { wait.wait(); });
			button(L"PrimaryButton");
			check(v.m_noteBusy, "Apply did not hold edit busy");
			break;
		case 4:
			// the queued write deliberately holds the engine until the next
			// tick.
			break;
		case 5:
			check(v.m_noteBusy && v.m_noteDialog &&
			          !v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "pending edit lost its dialog");
			v.m_noteDialog.Hide();
			owner.closeTab(owner.m_tabs.front().tab);
			check(owner.m_tabs.size() == 1 && v.m_noteDialog,
			      "close lost pending edit");
			notesGate->set_value();
			notesGate.reset();
			break;
		case 6:
			if (v.m_noteDialog || !v.m_commentScan.done()) {
				return;
			}
			check(v.m_notesDirty && v.m_commentScan.rows()[0].text ==
			                            "Edited native \u03a9\U0001f600",
			      "edited contents did not refresh");
			notesPath = std::filesystem::temp_directory_path() /
			            (L"sable_note_edit_" +
			             std::to_wstring(GetCurrentProcessId()) + L".pdf");
			owner.m_notesPicker = [path =
			                           hstring(notesPath.wstring())](hstring) {
				return attachmentChoice(path, {});
			};
			notesSave = owner.saveNotes(current);
			break;
		case 7:
			check(notesSave.GetResults() && !v.m_notesDirty,
			      "edited note save failed");
			check(engine
			          .run([path = notesPath, markup = markupCommentOnly,
			                type = selectionMarkup] {
				          const auto saved = pdf::openFile(path);
				          if (!saved.document) {
					          return false;
				          }
				          if (markup) {
					          const auto value =
					              saved.document->markupComment(0, 0, type);
					          return value && value->present &&
					                 value->text ==
					                     u"Edited native \u03a9\U0001f600";
				          }
				          return saved.document->textNote(0, 0) ==
				                 u"Edited native \u03a9\U0001f600";
			          })
			          .get(),
			      "reopened note edit missing");
			v.CommentsList().SelectedIndex(0);
			invoke(v.EditNoteButton());
			break;
		case 8:
			if (!v.m_noteDialog) {
				return;
			}
			v.m_noteDialog.Content()
			    .as<StackPanel>()
			    .Children()
			    .GetAt(1)
			    .as<TextBox>()
			    .Text(L"Keep this draft");
			change(u"Edited native \u03a9\U0001f600", u"Changed elsewhere");
			button(L"PrimaryButton");
			break;
		case 9: {
			check(v.m_noteDialog && !v.m_notesDirty,
			      "refused edit lost dialog or savepoint");
			const auto fields = v.m_noteDialog.Content().as<StackPanel>();
			check(
			    fields.Children().GetAt(1).as<TextBox>().Text() ==
			            L"Keep this draft" &&
			        !fields.Children().GetAt(1).as<TextBox>().IsReadOnly() &&
			        !fields.Children().GetAt(2).as<TextBlock>().Text().empty(),
			    "refused edit lost draft or error");
			button(L"CloseButton");
			break;
		}
		case 10:
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty, "Cancel dirtied savepoint");
			std::filesystem::remove(notesPath);
			status.Text(
			    L"PASS: selected note editor, exact no-op, Unicode Apply, "
			    L"pending close, refreshed comments, saved copy and retained "
			    L"stale draft");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void notePlacementTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
		    (notesSave &&
		     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
			return;
		}
		const auto invoke = [&](const FrameworkElement& element) {
			using namespace Microsoft::UI::Xaml::Automation;
			const auto action =
			    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
			        element)
			        .GetPattern(Peers::PatternInterface::Invoke)
			        .as<Provider::IInvokeProvider>();
			action.Invoke();
		};
		const auto dialogButton = [&](const wchar_t* name) {
			std::function<Button(DependencyObject)> find;
			find = [&](const DependencyObject& element) -> Button {
				if (const auto button = element.try_as<Button>();
				    button && button.Name() == name) {
					return button;
				}
				for (int i = 0;
				     i < Media::VisualTreeHelper::GetChildrenCount(element);
				     ++i) {
					if (const auto button = find(
					        Media::VisualTreeHelper::GetChild(element, i))) {
						return button;
					}
				}
				return nullptr;
			};
			const auto button = find(v.m_noteDialog);
			check(button != nullptr && button.IsEnabled(),
			      "placement editor button unavailable");
			invoke(button);
		};
		const auto choosePoint = [&] {
			const auto& rect = v.m_layout->pages()[1];
			return Windows::Foundation::Point{
			    static_cast<float>(rect.x + .25 * rect.width),
			    static_cast<float>(rect.y + .7 * rect.height)};
		};
		switch (step) {
		case 0:
			v.setCommentsOpen(true);
			invoke(v.PlaceNoteButton());
			check(v.m_notePlacing && !v.m_notesDirty &&
			          !v.AddNoteButton().IsEnabled(),
			      "placement did not arm independently of centered Add");
			break;
		case 1: {
			check(v.placeNote({-1, -1}) && v.m_notePlacing && !v.m_noteDialog,
			      "page gap opened editor or activated a link");
			owner.closeTab(owner.m_tabs.front().tab);
			check(owner.m_tabs.size() == 1 && v.m_notePlacing,
			      "close lost armed placement");
			HWND handle{};
			check_hresult(
			    owner.try_as<::IWindowNative>()->get_WindowHandle(&handle));
			SendMessageW(handle, WM_CLOSE, 0, 0);
			check(!owner.m_closing && v.m_notePlacing,
			      "window close lost placement");
			invoke(v.PlaceNoteButton());
			check(!v.m_notePlacing && !v.m_notesDirty,
			      "Cancel placement changed PDF");
			v.openFind();
			v.beginNotePlacement();
			check(v.cancelNotePlacement() && v.findOpen(),
			      "placement cancel closed Find");
			v.closeFind();
			v.beginNotePlacement();
			v.Visibility(Visibility::Collapsed);
			check(!v.m_notePlacing, "hidden viewer kept placement armed");
			v.Visibility(Visibility::Visible);
			v.beginNotePlacement();
			v.setCommentsOpen(false);
			check(!v.m_notePlacing, "hidden comments kept placement armed");
			v.setCommentsOpen(true);
			break;
		}
		case 2:
			v.zoomTo(1.5, {0, 0});
			v.goToPage(1);
			invoke(v.PlaceNoteButton());
			check(v.placeNote(choosePoint()) && v.m_noteDialog &&
			          !v.m_notePlacing && !v.m_notesDirty,
			      "chosen location mutated before Add");
			break;
		case 3: {
			const auto fields = v.m_noteDialog.Content().as<StackPanel>();
			check(fields.Children().GetAt(0).as<TextBlock>().Text() ==
			          L"Page 2",
			      "wrong placement target in editor");
			fields.Children().GetAt(1).as<TextBox>().Text(L"Author");
			fields.Children().GetAt(2).as<TextBox>().Text(
			    L"Placed native \u03a9");
			dialogButton(L"CloseButton");
			break;
		}
		case 4:
			if (v.m_noteDialog) {
				return;
			}
			check(!v.m_notesDirty && v.m_noteDraft == L"Placed native \u03a9",
			      "editor cancellation wrote or lost draft");
			v.beginNotePlacement();
			check(v.placeNote(choosePoint()) && v.m_noteDialog,
			      "retry target did not open");
			// i change the reading view after choosing the original location.
			v.zoomTo(.8, {0, 0});
			v.goToPage(3);
			break;
		case 5:
			dialogButton(L"PrimaryButton");
			break;
		case 6:
			if (v.m_noteDialog || !v.m_commentScan.done()) {
				return;
			}
			check(v.m_notesDirty &&
			          std::any_of(
			              v.m_commentScan.rows().begin(),
			              v.m_commentScan.rows().end(),
			              [](const auto& row) {
				              return row.page == 1 &&
				                     row.text == "Placed native \u03a9" &&
				                     row.position &&
				                     std::abs(row.position->x - .25) < 2e-6 &&
				                     std::abs(row.position->y - .7) < 2e-6;
			              }),
			      "saved target followed later navigation or zoom");
			notesPath = std::filesystem::temp_directory_path() /
			            (L"sable_placed_" +
			             std::to_wstring(GetCurrentProcessId()) + L".pdf");
			owner.m_notesPicker = [path =
			                           hstring(notesPath.wstring())](hstring) {
				return attachmentChoice(path, {});
			};
			notesSave = owner.saveNotes(current);
			break;
		case 7:
			check(notesSave.GetResults() && !v.m_notesDirty,
			      "placed note save failed");
			check(engine
			          .run([path = notesPath] {
				          const auto saved = pdf::openFile(path);
				          if (!saved.document) {
					          return false;
				          }
				          const auto notes = saved.document->annotations(1);
				          if (!notes || notes->entries.empty()) {
					          return false;
				          }
				          const auto& note = notes->entries.back();
				          return note.contents == "Placed native \u03a9" &&
				                 note.position &&
				                 std::abs(note.position->x - .25) < 2e-6 &&
				                 std::abs(note.position->y - .7) < 2e-6;
			          })
			          .get(),
			      "reopened note moved from chosen location");
			std::filesystem::remove(notesPath);
			v.beginNotePlacement();
			v.close();
			check(!v.m_notePlacing && !v.m_noteDialog,
			      "forced close kept placement state");
			status.Text(L"PASS: placement arm/cancel, gap, hidden, close, "
			            L"editor draft, "
			            L"immutable target and saved position");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void notesTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
		    (notesSave &&
		     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
			return;
		}
		const auto choose = [&](ContentDialogResult choice) {
			owner.m_notesChoice = [choice] { return notesChoice(choice); };
		};
		const auto pick = [&](const std::filesystem::path& path) {
			owner.m_notesPicker = [value = hstring(path.wstring())](hstring) {
				return attachmentChoice(value, {});
			};
		};
		switch (step) {
		case 0:
			v.setCommentsOpen(true);
			check(v.AddNoteButton().IsEnabled(), "note creation disabled");
			v.editNote();
			break;
		case 1: {
			check(v.m_noteDialog != nullptr, "note editor did not open");
			const auto fields = v.m_noteDialog.Content().as<StackPanel>();
			if (fields.Children().GetAt(2).as<TextBox>().Text().empty()) {
				check(!v.m_noteDialog.IsPrimaryButtonEnabled(),
				      "empty note could be submitted");
				fields.Children().GetAt(1).as<TextBox>().Text(L"Abhilash");
				fields.Children().GetAt(2).as<TextBox>().Text(
				    L"Native \u03a9\U0001f600");
				return;
			}
			std::function<Button(DependencyObject)> find;
			find = [&](const DependencyObject& element) -> Button {
				if (const auto button = element.try_as<Button>();
				    button && button.Name() == L"PrimaryButton") {
					return button;
				}
				for (int i = 0;
				     i < Media::VisualTreeHelper::GetChildrenCount(element);
				     ++i) {
					if (const auto button = find(
					        Media::VisualTreeHelper::GetChild(element, i))) {
						return button;
					}
				}
				return nullptr;
			};
			const auto button = find(v.m_noteDialog);
			check(button != nullptr, "editor Add button missing");
			check(button.IsEnabled(), "editor Add button still disabled");
			using namespace Microsoft::UI::Xaml::Automation;
			Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button)
			    .GetPattern(Peers::PatternInterface::Invoke)
			    .as<Provider::IInvokeProvider>()
			    .Invoke();
			break;
		}
		case 2:
			if (v.m_noteDialog || !v.m_commentScan.done()) {
				return;
			}
			check(v.m_notesDirty && v.m_renderVersion > 0 &&
			          v.SaveNotesButton().IsEnabled() &&
			          v.NotesState().Text() == L"Unsaved changes",
			      "completed note did not mark unsaved state");
			check(std::any_of(v.m_commentScan.rows().begin(),
			                  v.m_commentScan.rows().end(),
			                  [](const auto& row) {
				                  return row.text ==
				                             "Native \u03a9\U0001f600" &&
				                         row.author == "Abhilash";
			                  }),
			      "new note missing from refreshed comments");
			choose(ContentDialogResult::None);
			owner.closeTab(owner.m_tabs.front().tab);
			break;
		case 3:
			owner.m_notesChoice = []()
			    -> Windows::Foundation::IAsyncOperation<ContentDialogResult> {
				throw hresult_error(E_ILLEGAL_METHOD_CALL);
			};
			owner.m_allowNoteClose = true;
			owner.closeWithNotes();
			break;
		case 4: {
			if (owner.m_closePending) {
				return;
			}
			check(owner.m_tabs.size() == 1 && v.m_notesDirty,
			      "Cancel lost the edited tab");
			check(
			    !owner.m_closing && !owner.m_allowNoteClose &&
			        !v.m_noteDialog && v.Message().IsOpen() &&
			        std::wstring_view(v.Message().Message())
			                .find(L"Couldn't review unsaved notes") == 0,
			    "close review exception lost its notice or left close allowed");
			v.Message().IsOpen(false);
			choose(ContentDialogResult::None);
			HWND handle{};
			check_hresult(
			    owner.try_as<::IWindowNative>()->get_WindowHandle(&handle));
			SendMessageW(handle, WM_CLOSE, 0, 0);
			check(!owner.m_closing, "native close bypassed unsaved prompt");
			pick({});
			notesSave = owner.saveNotes(current);
			break;
		}
		case 5:
			check(!notesSave.GetResults() && v.m_notesDirty,
			      "canceled picker cleared notes");
			pick(v.m_path);
			notesSave = owner.saveNotes(current);
			break;
		case 6:
			check(!notesSave.GetResults() && v.m_notesDirty,
			      "source destination accepted or cleared notes");
			notesPath = std::filesystem::temp_directory_path() /
			            (L"sable_notes_" +
			             std::to_wstring(GetCurrentProcessId()) + L".pdf");
			pick(notesPath);
			notesSave = owner.saveNotes(current);
			break;
		case 7:
			check(notesSave.GetResults() && !v.m_notesDirty,
			      "successful save did not clear the UI savepoint");
			if (!notesRefusalTried) {
				notesRefusalTried = true;
				v.submitNote(-1, u"Rejected after save", {});
				return;
			}
			check(engine
			          .run([path = notesPath] {
				          const auto saved = pdf::openFile(path);
				          return saved.document &&
				                 saved.document->annotations(0)
				                         ->entries.back()
				                         .contents == "Native \u03a9\U0001f600";
			          })
			          .get(),
			      "saved copy lost Unicode note");
			notesGate = std::make_shared<std::promise<void>>();
			engine.post(
			    [wait = notesGate->get_future().share()] { wait.wait(); });
			v.submitNote(0, u"Second", {});
			owner.closeTab(owner.m_tabs.front().tab);
			check(owner.m_tabs.size() == 1 && v.m_noteBusy,
			      "pending edit was closed");
			notesGate->set_value();
			notesGate.reset();
			break;
		case 8:
			check(v.m_notesDirty, "later note did not mark unsaved state");
			pick(notesPath / L"missing" / L"notes.pdf");
			choose(ContentDialogResult::Primary);
			owner.closeTab(owner.m_tabs.front().tab);
			break;
		case 9:
			if (owner.m_closePending) {
				return;
			}
			check(owner.m_tabs.size() == 1 && v.m_notesDirty,
			      "failed save closed the edited tab");
			choose(ContentDialogResult::Secondary);
			owner.closeTab(owner.m_tabs.front().tab);
			check(owner.m_tabs.empty(), "Discard did not close tab");
			owner.openDocument(engine, L"tests/fixtures/attachments.pdf", 1);
			break;
		case 10:
			v.submitNote(0, u"Must not lose attachments", {});
			break;
		case 11:
			check(!v.m_notesDirty, "attachment refusal changed the document");
			owner.closeTab(owner.m_tabs.front().tab);
			owner.openDocument(engine, L"tests/fixtures/notes_denied.pdf", 1);
			break;
		case 12:
			v.setCommentsOpen(true);
			check(!v.AddNoteButton().IsEnabled(), "denied PDF enabled notes");
			v.editNote();
			check(!v.m_noteDialog, "denied PDF opened editor");
			peerWindow = make_self<MainWindow>(owner.m_settings);
			peerWindow->setEngine(engine);
			peerWindow->openDocument(
			    engine, L"tests/fixtures/comment_locations.pdf", 1);
			peerWindow->Activate();
			break;
		case 13: {
			const auto peer = peerWindow->Viewer();
			if (!peer || !ready(*get_self<ViewerPage>(peer))) {
				return;
			}
			get_self<ViewerPage>(peer)->submitNote(0, u"Window close", {});
			break;
		}
		case 14: {
			const auto pdf = get_self<ViewerPage>(peerWindow->Viewer());
			if (pdf->m_noteBusy) {
				return;
			}
			check(pdf->m_notesDirty, "peer window note missing");
			peerWindow->m_notesChoice = [] {
				return notesChoice(ContentDialogResult::None);
			};
			HWND handle{};
			check_hresult(
			    peerWindow->try_as<::IWindowNative>()->get_WindowHandle(
			        &handle));
			SendMessageW(handle, WM_SYSCOMMAND, SC_CLOSE, 0);
			check(!peerWindow->m_closing, "system close ignored Cancel");
			peerWindow->m_notesChoice = [] {
				return notesChoice(ContentDialogResult::Primary);
			};
			peerWindow->m_notesPicker =
			    [path = hstring(notesPath.wstring())](hstring) {
				    return attachmentChoice(path, {});
			    };
			SendMessageW(handle, WM_CLOSE, 0, 0);
			break;
		}
		case 15:
			if (peerWindow->m_closePending) {
				return;
			}
			check(peerWindow->m_closing, "saved window did not close");
			peerWindow = nullptr;
			std::filesystem::remove(notesPath);
			status.Text(
			    L"PASS: note editor, refresh, Unicode save/reopen, "
			    L"source protection, unsaved close, pending edit and refusals");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void commentsTick(MainWindow& owner) {
		if (step == 11) {
			check(first && first->m_closed && !first->m_commentsPending &&
			          first->CommentsList().Items().Size() == 0 &&
			          first->m_commentScan.rows().empty(),
			      "closed comments accepted a late result");
			status.Text(
			    L"PASS: comments ordering, Unicode, navigation, sidebar "
			    L"memory, pause/resume, empty/retry/limited and close");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_commentsPending) {
			return;
		}
		const auto invoke = [](const FrameworkElement& element) {
			using namespace Microsoft::UI::Xaml::Automation;
			const auto peer =
			    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
			        element);
			const auto action =
			    peer ? peer.GetPattern(Peers::PatternInterface::Invoke)
			               .try_as<Provider::IInvokeProvider>()
			         : nullptr;
			if (action) {
				action.Invoke();
			}
			return static_cast<bool>(action);
		};
		const auto pause = [&](bool rail) {
			auto release = std::make_shared<std::promise<void>>();
			auto wait = release->get_future().share();
			engine.post([wait] { wait.wait(); });
			v.m_commentScan.reset(3);
			v.CommentsList().Items().Clear();
			v.showComments();
			const bool pending = v.m_commentsPending;
			commentVersion = v.m_commentVersion;
			if (rail) {
				release->set_value();
				auto complete = std::make_shared<std::promise<void>>();
				auto finished = complete->get_future();
				engine.post([complete] { complete->set_value(); });
				// i hold this ui turn until the old reply is queued.
				check(finished.wait_for(std::chrono::seconds(2)) ==
				          std::future_status::ready,
				      "comments reply did not reach the UI queue");
				v.Rail().Visibility(Visibility::Collapsed);
			} else {
				v.Visibility(Visibility::Collapsed);
				release->set_value();
			}
			check(pending && !v.m_commentsPending &&
			          v.m_commentVersion > commentVersion &&
			          v.CommentsPanel().Visibility() == Visibility::Collapsed,
			      "hidden comments did not cancel their pending request");
			commentVersion = v.m_commentVersion;
		};
		switch (step) {
		case 0:
			check(v.m_commentScan.nextPage() == 0 &&
			          v.CommentsList().Items().Size() == 0 && !v.commentsOpen(),
			      "comments loaded before opening the panel");
			v.setCommentsOpen(true);
			break;
		case 1: {
			if (!v.m_commentScan.done()) {
				return;
			}
			const auto& rows = v.m_commentScan.rows();
			check(rows.size() == 3 && rows[0].page == 0 && rows[1].page == 1 &&
			          rows[2].page == 2 && rows[1].subtype == "Highlight" &&
			          v.CommentsCount().Text() == L"3" &&
			          v.CommentsStatus().Text().empty(),
			      "document-wide comment count or order");
			const auto row =
			    v.CommentsList().Items().GetAt(0).as<ListViewItem>();
			check(std::wstring_view(
			          Automation::AutomationProperties::GetName(row))
			              .find(L"Abhilash \u03a9 Notes "
			                    L"\u03a9\U0001f600\nsecond line") !=
			          std::wstring_view::npos,
			      "comments lost Unicode author or text");
			check(v.CommentsList().IsItemClickEnabled(),
			      "comment rows do not invoke navigation");
			if (!invoke(v.CommentsList().Items().GetAt(2).as<ListViewItem>())) {
				check(ticks < 30, "comment row has no Invoke");
				return;
			}
			break;
		}
		case 2:
			check(v.m_current == 2, "comment row did not navigate to its page");
			v.CommentsList().Focus(FocusState::Programmatic);
			if (!v.commentsHasFocus()) {
				return;
			}
			v.toggleSidebar();
			check(!v.commentsOpen() && !v.commentsHasFocus(),
			      "closed comments kept focus");
			v.toggleSidebar();
			check(v.commentsOpen(), "F4 forgot comments");
			v.setThumbnailsOpen(true);
			check(!v.commentsOpen(), "thumbnails left comments open");
			v.setCommentsOpen(true);
			v.setOutlineOpen(true);
			check(!v.commentsOpen(), "outline left comments open");
			v.setCommentsOpen(true);
			v.setAttachmentsOpen(true);
			check(!v.commentsOpen(), "attachments left comments open");
			v.setCommentsOpen(true);
			check(!v.thumbnailsOpen() && !v.outlineOpen() &&
			          !v.attachmentsOpen(),
			      "comments sidebar exclusivity");
			pause(false);
			break;
		case 3:
		case 5:
			check(v.m_commentScan.nextPage() == 0 &&
			          v.m_commentVersion == commentVersion,
			      "hidden comments accepted stale progress");
			if (step == 3) {
				v.Visibility(Visibility::Visible);
			} else {
				v.Rail().Visibility(Visibility::Visible);
			}
			break;
		case 4:
			if (!v.m_commentScan.done()) {
				return;
			}
			check(v.CommentsList().Items().Size() == 3,
			      "viewer visibility did not resume comments");
			pause(true);
			break;
		case 6:
			if (!v.m_commentScan.done()) {
				return;
			}
			check(v.CommentsList().Items().Size() == 3,
			      "rail visibility did not resume comments");
			v.m_commentScan.reset(3);
			v.CommentsList().Items().Clear();
			v.m_commentScan.fail();
			v.showComments();
			check(v.CommentsRetry().Visibility() == Visibility::Visible &&
			          v.CommentsStatus().Text() ==
			              L"Couldn't load comments on page 1.",
			      "comments failure has no visible retry");
			check(invoke(v.CommentsRetry()), "comments Retry has no Invoke");
			break;
		case 7:
			if (!v.m_commentScan.done()) {
				return;
			}
			check(!v.m_commentScan.failed() &&
			          v.CommentsRetry().Visibility() == Visibility::Collapsed,
			      "comments retry failed");
			v.m_commentScan.reset(1);
			v.CommentsList().Items().Clear();
			check(v.m_commentScan.append({0, {}, true}));
			v.showComments();
			check(v.CommentsStatus().Text() ==
			          L"Some comments could not be listed.",
			      "comments limitation has no visible status");
			owner.openDocument(engine, L"tests/fixtures/pages.pdf", 1);
			break;
		case 8:
			check(!v.commentsOpen() && v.m_commentScan.nextPage() == 0,
			      "comments state leaked between tabs");
			v.setCommentsOpen(true);
			break;
		case 9:
			if (!v.m_commentScan.done()) {
				return;
			}
			check(std::wstring_view(v.CommentsStatus().Text())
			              .starts_with(L"No comments yet.") &&
			          v.CommentsList().Items().Size() == 0,
			      "empty document comments state");
			break;
		case 10: {
			auto release = std::make_shared<std::promise<void>>();
			auto wait = release->get_future().share();
			engine.post([wait] { wait.wait(); });
			v.m_commentScan.reset(3);
			v.showComments();
			const bool pending = v.m_commentsPending;
			first.copy_from(&v);
			owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
			release->set_value();
			check(pending, "closing comments had no queued request");
			break;
		}
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
			// closing the focused tab hands the keys to the next document
			const auto next = owner.Viewer();
			const auto focus = Input::FocusManager::GetFocusedElement(
			    owner.RootGrid().XamlRoot());
			if (!next || focus != get_self<ViewerPage>(next)->Scroller()) {
				check(ticks < 30, "closing a tab left the keys nowhere");
				return;
			}
			status.Text(L"PASS: attachment names, cache, sidebar switching, "
			            L"empty/retry, close guards and Recent");
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
			check(attachmentItem.Content().as<TextBlock>().Text() ==
			              L"notes \u03a9\U0001f600.txt" &&
			          Automation::AutomationProperties::GetName(
			              attachmentItem) == L"notes \u03a9\U0001f600.txt",
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
		case 3: {
			check(!v.m_attachments && !v.attachmentsOpen(),
			      "attachment state leaked between tabs");
			// both files opened, so both are in Recent, newest first
			const auto& recent =
			    get_self<HomePage>(owner.Home())->m_recents.items();
			check(recent.size() == 2 &&
			          recent[0].path.filename() == L"pages.pdf" &&
			          recent[1].path.filename() == L"attachments.pdf",
			      "opened files missing from Recent");
			v.setAttachmentsOpen(true);
			break;
		}
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
			viewer.m_textMenu.Hide();
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
	void presentationTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || ticks < 3 || v.m_linkPending) {
			return;
		}
		using Kind = Microsoft::UI::Windowing::AppWindowPresenterKind;
		const auto capture = [&](const wchar_t* name) {
			wchar_t directory[2048]{};
			if (GetEnvironmentVariableW(L"SablePresentationCaptureDirectory",
			                            directory, 2048)) {
				std::filesystem::create_directories(directory);
				HWND handle{};
				check_hresult(
				    owner.try_as<IWindowNative>()->get_WindowHandle(&handle));
				captureMusicWindow(handle,
				                   std::filesystem::path(directory) / name);
				captureMusicWindow(handle,
				                   std::filesystem::path(directory) /
				                       (std::wstring(L"screen-") + name),
				                   true);
			}
		};
		switch (step) {
		case 0:
			first = v.get_strong();
			owner.m_presenter.Restore();
			owner.AppWindow().Resize({1100, 800});
			v.setLayout(helpers::ScrollMode::Wrapped,
			            helpers::SpreadMode::Even);
			v.zoomTo(.6, {0, 0});
			v.setThumbnailsOpen(true);
			v.setCursorTool(ViewerPage::CursorTool::Hand);
			v.goToPage(1);
			break;
		case 1:
			zoom = v.m_zoom;
			outlinePage = v.m_current;
			owner.onPresentation({}, {});
			break;
		case 2:
			check(v.m_presentation && owner.m_presentationViewer == current &&
			          owner.AppWindow().Presenter().Kind() == Kind::FullScreen,
			      "presentation did not enter full screen");
			check(v.m_current == outlinePage &&
			          v.scrollMode() == helpers::ScrollMode::Page &&
			          v.spreadMode() == helpers::SpreadMode::None &&
			          v.m_preset == helpers::ZoomPreset::PageFit,
			      "presentation page/layout/fit or mixed-size spread");
			check(owner.AppTitleBar().Visibility() == Visibility::Collapsed &&
			          v.Rail().Visibility() == Visibility::Collapsed &&
			          v.ThumbnailPanel().Visibility() ==
			              Visibility::Collapsed &&
			          v.PresentationBackdrop().Visibility() ==
			              Visibility::Visible,
			      "presentation chrome still visible");
			check(v.m_layout->width() <= v.Scroller().ViewportWidth() + 1 &&
			          v.m_layout->height() <= v.Scroller().ViewportHeight() + 1,
			      "presentation page does not fit viewport");
			check(!v.beginPan({40, 40}), "presentation started hand dragging");
			v.zoomIn();
			v.zoomReset();
			v.rotateView(1);
			check(v.m_preset == helpers::ZoomPreset::PageFit &&
			          v.m_rotation == 0,
			      "presentation allowed reading zoom/rotation commands");
			capture(L"dark.png");
			appearance::apply(true);
			break;
		case 3:
			capture(L"obsidian.png");
			appearance::apply(false);
			break;
		case 4:
			if (ticks < 36) {
				return;
			}
			check(v.PresentationControls().Visibility() ==
			          Visibility::Collapsed,
			      "presentation controls did not hide when idle");
			v.showPresentationControls();
			v.presentationKey(VK_HOME, false);
			v.presentationWheel(-6);
			check(v.m_current == 0, "small wheel motion turned a page");
			v.presentationWheel(-6);
			check(v.m_current == 1, "wheel accumulation did not turn a page");
			v.presentationWheel(-120);
			check(v.m_current == 1, "wheel cooldown did not hold");
			v.m_presentationWheelTime = {};
			v.presentationWheel(120);
			check(v.m_current == 0, "reverse wheel did not go back");
			v.presentationKey('N', false);
			check(v.m_current == 1, "presentation next key");
			v.presentationKey(VK_SPACE, true);
			check(v.m_current == 0, "presentation shifted space");
			v.presentationKey(VK_END, false);
			check(v.m_current == v.m_sizes.size() - 1, "presentation End");
			v.presentationKey(VK_HOME, false);
			owner.endPresentation();
			break;
		case 5:
			check(!v.m_presentation && !owner.m_presentationViewer &&
			          owner.AppWindow().Presenter().Kind() ==
			              Kind::Overlapped &&
			          owner.AppTitleBar().Visibility() == Visibility::Visible,
			      "presentation did not restore window chrome/presenter");
			check(v.scrollMode() == helpers::ScrollMode::Wrapped &&
			          v.spreadMode() == helpers::SpreadMode::Even &&
			          v.m_preset == helpers::ZoomPreset::None &&
			          v.m_zoom == zoom && v.m_current == 0 &&
			          v.thumbnailsOpen() &&
			          v.cursorTool() == ViewerPage::CursorTool::Hand,
			      "presentation lost reading layout/zoom/page/sidebar/tool");
			capture(L"restored.png");
			owner.onToggleFullScreen({}, {});
			owner.onPresentation({}, {});
			break;
		case 6:
			owner.onToggleFullScreen({}, {});
			check(!v.m_presentation &&
			          owner.AppWindow().Presenter().Kind() == Kind::FullScreen,
			      "presentation lost pre-existing full screen");
			owner.onToggleFullScreen({}, {});
			owner.onPresentation({}, {});
			owner.openDocument(engine, L"tests/fixtures/links.pdf", {});
			break;
		case 7:
			check(&v != first.get() && !owner.m_presentationViewer &&
			          !first->m_presentation &&
			          owner.AppTitleBar().Visibility() == Visibility::Visible,
			      "tab change kept presentation active");
			owner.onPresentation({}, {});
			v.goToPage(0);
			break;
		case 8: {
			const auto& rect = v.m_layout->pages()[0];
			v.requestLink(
			    {static_cast<float>(rect.x + 80. / 360 * rect.width),
			     static_cast<float>(rect.y + 130. / 540 * rect.height)},
			    1);
			break;
		}
		case 9:
			check(v.m_current == 1 &&
			          v.m_preset == helpers::ZoomPreset::PageFit,
			      "presentation internal link changed fit or failed");
			v.goToPage(0);
			break;
		case 10: {
			const auto& rect = v.m_layout->pages()[0];
			v.requestLink(
			    {static_cast<float>(rect.x + 80. / 360 * rect.width),
			     static_cast<float>(rect.y + 50. / 540 * rect.height)},
			    1);
			break;
		}
		case 11:
			check(
			    v.m_current == 1 && v.m_launchedUri.empty(),
			    "presentation external link did not advance without launching");
			owner.endPresentation();
			owner.Tabs().SelectedItem(owner.m_tabs.front().tab);
			break;
		case 12:
			owner.onPresentation({}, {});
			check(first->m_presentation.has_value(), "presentation re-entry");
			owner.closeTab(owner.m_tabs.front().tab);
			check(!owner.m_presentationViewer && !first->m_presentation &&
			          first->m_closed && !first->beginPresentation([] {}),
			      "presentation survived tab closure");
			status.Text(L"PASS: presentation fit, navigation, links, controls, "
			            L"restoration, full screen and tab lifetime");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void layoutTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || ticks < 3 || v.m_copying || v.m_finding ||
		    (v.m_findTimer && v.m_findTimer.IsRunning()) || v.m_linkPending) {
			return;
		}
		using Scroll = helpers::ScrollMode;
		using Spread = helpers::SpreadMode;
		const auto capture = [&](const wchar_t* theme) {
			wchar_t directory[2048]{};
			if (GetEnvironmentVariableW(L"SableLayoutCaptureDirectory",
			                            directory, 2048)) {
				std::filesystem::create_directories(directory);
				HWND reader{};
				check_hresult(
				    owner.try_as<IWindowNative>()->get_WindowHandle(&reader));
				captureMusicWindow(reader,
				                   std::filesystem::path(directory) /
				                       (std::to_wstring(layoutRound / 3) +
				                        L"-" + theme + L".png"));
			}
		};
		const auto nextLayout = [&] {
			if (++layoutRound < 12) {
				const auto selection = v.m_selection;
				v.setLayout(static_cast<Scroll>(layoutRound / 3),
				            static_cast<Spread>(layoutRound % 3));
				check(selection == v.m_selection,
				      "layout cleared selected text");
				v.goToPage(0);
				step = 2;
			} else {
				step = 4;
			}
			ticks = 0;
		};
		switch (step) {
		case 0:
			first = v.get_strong();
			owner.m_presenter.Restore();
			owner.AppWindow().Resize({1100, 800});
			v.setThumbnailsOpen(false);
			v.requestText(0);
			v.requestText(1);
			v.openFind();
			v.FindBox().Text(L"Alpha");
			break;
		case 1:
			if (!v.m_text.contains(0) || !v.m_text.contains(1) ||
			    !v.m_findActive) {
				if (!v.m_text.contains(0)) {
					v.requestText(0);
				} else if (!v.m_text.contains(1)) {
					v.requestText(1);
				}
				return;
			}
			check(v.m_findMatches.size() == 4, "layout Find setup");
			v.zoomTo(.6, {0, 0});
			v.goToPage(0);
			layoutKey = v.key(0);
			v.selectAllText();
			break;
		case 2: {
			check(v.scrollMode() == static_cast<Scroll>(layoutRound / 3) &&
			          v.spreadMode() == static_cast<Spread>(layoutRound % 3),
			      "layout choice");
			check(v.m_current == 0 && v.m_zoom == .6 && v.key(0) == layoutKey &&
			          v.m_findMatches.size() == 4 && v.canCopySelection(),
			      "layout changed page/zoom/rasters/Find/selection at round " +
			          std::to_string(layoutRound));
			const int page = v.m_layout->contains(1) ? 1 : 0;
			const auto& rect = v.m_layout->pages()[page];
			const auto point = [&](int index) {
				const auto box =
				    v.viewBox(v.m_text.at(page).text->characters[index].box);
				return Windows::Foundation::Point{
				    static_cast<float>(rect.x + (box.left + box.right) * .5 *
				                                    rect.width),
				    static_cast<float>(rect.y + (box.top + box.bottom) * .5 *
				                                    rect.height)};
			};
			check(v.m_layout->pageAt(point(0).X, point(0).Y) ==
			          static_cast<std::size_t>(page),
			      "layout text page hit");
			v.beginSelection(point(0));
			v.extendSelection(point(4));
			v.endSelection();
			check(v.canCopySelection() && v.m_selection->anchor.page == page,
			      "layout text selection page");
			owner.updateLayoutMenu();
			check(owner.ScrollModeMenu().IsEnabled() &&
			          owner.SpreadModeMenu().IsEnabled(),
			      "layout menus disabled");
			if (layoutRound % 3 == 0) {
				capture(L"dark");
				appearance::apply(true);
				step = 3;
				ticks = 0;
				return;
			}
			nextLayout();
			return;
		}
		case 3:
			capture(L"obsidian");
			appearance::apply(false);
			nextLayout();
			return;
		case 4:
			v.setLayout(Scroll::Page, Spread::Odd);
			v.setPreset(helpers::ZoomPreset::PageFit);
			v.goToPage(0);
			break;
		case 5:
			check(v.m_visible == helpers::PageRange{0, 2},
			      "paged spread visible range");
			check(v.m_layout->pages()[1].x + v.m_layout->pages()[1].width <=
			          v.Scroller().ViewportWidth() + 1,
			      "page-fit did not fit spread width");
			v.onNextPage({}, {});
			break;
		case 6:
			check(v.m_current == 2 && v.m_layout->contains(2) &&
			          !v.m_layout->contains(0),
			      "next spread navigation");
			v.onPreviousPage({}, {});
			break;
		case 7:
			check(v.m_current <= 1 && v.m_layout->contains(0),
			      "previous spread navigation");
			v.m_findActive = v.m_findMatches.size() - 1;
			v.m_findNeedsReveal = true;
			v.revealFindMatch();
			break;
		case 8:
			if (v.m_findNeedsReveal) {
				return;
			}
			check(v.m_layout->contains(
			          static_cast<std::size_t>(v.m_findMatches.back().page)),
			      "Find did not change active spread");
			v.closeFind();
			v.setLayout(Scroll::Wrapped, Spread::Even);
			v.zoomTo(.6, {0, 0});
			v.goToPage(2);
			owner.AppWindow().Resize({660, 700});
			break;
		case 9:
			check(std::abs(v.m_layoutViewWidth - v.Scroller().ActualWidth()) <
			              1 &&
			          v.m_zoom == .6,
			      "numeric zoom did not rewrap on resize");
			v.rotateView(1);
			break;
		case 10:
			check(v.m_rotation == 1 && v.scrollMode() == Scroll::Wrapped &&
			          v.spreadMode() == Spread::Even,
			      "rotation lost layout");
			owner.openDocument(engine, L"tests/fixtures/links.pdf", {});
			break;
		case 11:
			check(&v != first.get() && v.scrollMode() == Scroll::Vertical &&
			          v.spreadMode() == Spread::None,
			      "layout leaked into new tab");
			owner.onPageLayout(owner.ScrollPageItem(), {});
			owner.onPageLayout(owner.SpreadEvenItem(), {});
			v.setPreset(helpers::ZoomPreset::PageFit);
			v.goToPage(0);
			break;
		case 12: {
			check(owner.ScrollPageItem().IsChecked() &&
			          owner.SpreadEvenItem().IsChecked(),
			      "layout menu handlers");
			const auto& rect = v.m_layout->pages()[0];
			v.requestLink(
			    {static_cast<float>(rect.x + 80. / 360 * rect.width),
			     static_cast<float>(rect.y + 130. / 540 * rect.height)});
			break;
		}
		case 13:
			check(v.m_current == 1 && v.m_layout->contains(1) && v.m_zoom == 2,
			      "XYZ link failed to change active spread");
			v.goToPage(0);
			break;
		case 14: {
			const auto& rect = v.m_layout->pages()[0];
			v.requestLink(
			    {static_cast<float>(rect.x + 80. / 360 * rect.width),
			     static_cast<float>(rect.y + 50. / 540 * rect.height)});
			v.setLayout(Scroll::Horizontal, Spread::None);
			break;
		}
		case 15:
			check(v.m_launchedUri.empty(),
			      "stale link acted after layout change");
			owner.Tabs().SelectedItem(owner.m_tabs.front().tab);
			break;
		case 16:
			check(&v == first.get() && v.scrollMode() == Scroll::Wrapped &&
			          v.spreadMode() == Spread::Even,
			      "returning tab lost layout");
			const auto oldSize = v.m_sizes[0];
			v.m_sizes[0] = {50'000'000, 50'000'000};
			v.setLayout(Scroll::Vertical, Spread::None);
			v.m_sizes[0] = oldSize;
			check(v.scrollMode() == Scroll::Wrapped &&
			          v.spreadMode() == Spread::Even,
			      "oversized layout changed readable state");
			v.setLayout(Scroll::Page, Spread::Odd);
			v.zoomTo(3, {0, 0});
			v.goToPage(0);
			break;
		case 17:
			check(v.m_current == 0 && v.Scroller().ScrollableWidth() > 0,
			      "wide spread fixture did not settle");
			v.advancePage(1);
			break;
		case 18:
			check(v.m_current == 1,
			      "wide spread skipped its other page; offset " +
			          std::to_string(v.Scroller().HorizontalOffset()) +
			          " max " + std::to_string(v.Scroller().ScrollableWidth()) +
			          " target " + std::to_string(v.m_layout->pages()[1].x));
			v.setLayout(Scroll::Page, Spread::None);
			v.goToPage(0);
			v.close();
			v.setLayout(Scroll::Horizontal, Spread::Odd);
			check(v.scrollMode() == Scroll::Page && !v.m_pan.active(),
			      "closed viewer changed layout");
			status.Text(L"PASS: scroll/spread layouts, text, Find, zoom, "
			            L"links, menus and tab lifetime");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void longGesturesTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		check(!v.Message().IsOpen(), to_string(v.Message().Message()));
		if (!ready(v) || ticks < 3 || v.m_resettingGesture || v.m_linkPending) {
			return;
		}
		const auto click = [&v](std::size_t page, double x = .5) {
			const auto& rect = v.m_layout->pages()[page];
			return Windows::Foundation::Point{
			    static_cast<float>(rect.x + rect.width * x),
			    static_cast<float>(rect.y + rect.height * .5)};
		};
		const auto firstInSpread = [&v] {
			return helpers::spreadPages(v.m_sizes.size(), v.m_current,
			                            v.m_spreadMode)
			    .first;
		};
		switch (step) {
		case 0:
			owner.openDocument(engine, L"tests/fixtures/long.pdf", 1000);
			break;
		case 1:
			check(v.m_sizes.size() == 1000, "long fixture not open");
			v.setThumbnailsOpen(false);
			v.zoomTo(1.5, {0, 0});
			v.goToPage(999);
			break;
		case 2: {
			check(v.m_current >= 998, "last page did not settle");
			v.m_directManipulation = true;
			const auto view = v.viewport();
			v.Scroller().ChangeView(nullptr,
			                        (view.y + view.height / 2) * .6 -
			                            v.Scroller().ViewportHeight() / 2,
			                        .6f, true);
			break;
		}
		case 3:
			check(v.m_zoom == 1.5 && !v.m_resettingGesture &&
			          std::abs(v.Scroller().ZoomFactor() - .6f) < 1e-5 &&
			          v.ZoomText().Text() == L"90%",
			      "active gesture baked early or lost its live percentage");
			check(v.m_wanted.first >= 997 && v.m_wanted.end == 1000,
			      "native zoom used scaled offsets as PDF coordinates");
			v.m_device.bitmaps.clear();
			v.m_cache.clear();
			v.invalidate(999);
			v.updateVisible();
			break;
		case 4:
			if (!v.m_device.bitmaps.contains(999)) {
				return;
			}
			check(v.m_visible.end == 1000,
			      "last-page raster vanished during active native zoom");
			v.m_directManipulation = false;
			v.queueNativeZoom();
			break;
		case 5:
			check(std::abs(v.Scroller().ZoomFactor() - 1.f) < 1e-5 &&
			          v.m_current >= 998 && v.m_wanted.end == 1000,
			      "finishing zoom jumped away from the final page");
			if (gestureRound < 2) {
				if (gestureRound == 0) {
					const auto wanted = v.m_wanted;
					const auto landing = v.m_landingAt;
					v.m_holdCurrent = true;
					v.m_landingAt = std::pair{0., 0.};
					v.updateVisible();
					check(v.m_wanted == wanted &&
					          v.m_device.bitmaps.contains(999),
					      "transient zoom layout evicted the visible page");
					v.m_landingAt = landing;
					v.m_holdCurrent = false;
					const auto previous = v.m_current;
					v.m_current = 0;
					v.queueNativeZoom();
					check(v.m_current >= 990,
					      "completed native pan retained an old page number");
					v.m_current = previous;
				}
				v.zoomTo(gestureRound++ == 0 ? 2.2 : .6, {0, 0});
				ticks = 0;
				return;
			}
			v.setLayout(helpers::ScrollMode::Page, helpers::SpreadMode::Odd);
			v.goToPage(999);
			owner.updateLayoutMenu();
			check(owner.ClickSpreadsItem().IsEnabled() &&
			          !owner.ClickSpreadsItem().IsChecked(),
			      "spread click option must start off");
			owner.ClickSpreadsItem().IsChecked(true);
			owner.onClickSpreads({}, {});
			break;
		case 6:
			check(v.spreadClickDirection(click(998)) == -1 &&
			          v.spreadClickDirection(click(999)) == 1 &&
			          v.spreadClickDirection({0, 0}) == 0,
			      "left/right spread click hit testing");
			v.requestLink(click(998), -1);
			break;
		case 7:
			check(firstInSpread() == 996,
			      "left page did not turn back a spread");
			v.requestLink(click(997), 1);
			break;
		case 8:
			check(firstInSpread() == 998,
			      "right page did not turn forward a spread");
			v.requestLink(click(999), 1);
			break;
		case 9:
			check(firstInSpread() == 998,
			      "last spread moved past the document");
			v.goToPage(0);
			v.requestLink(click(0), -1);
			break;
		case 10:
			check(firstInSpread() == 0,
			      "first spread moved before the document");
			v.setLayout(helpers::ScrollMode::Page, helpers::SpreadMode::Even);
			check(v.spreadClickDirection(click(0, .25)) == -1 &&
			          v.spreadClickDirection(click(0, .75)) == 1,
			      "single cover must expose both directions");
			v.requestLink(click(0, .75), 1);
			break;
		case 11:
			check(firstInSpread() == 1,
			      "even cover did not advance to its spread");
			owner.ClickSpreadsItem().IsChecked(false);
			owner.onClickSpreads({}, {});
			v.requestLink(click(2), 1);
			break;
		case 12:
			check(firstInSpread() == 1,
			      "disabled spread clicks still navigated");
			owner.openDocument(engine, L"tests/fixtures/links.pdf", 1);
			break;
		case 13: {
			v.setLayout(helpers::ScrollMode::Page, helpers::SpreadMode::Odd);
			v.m_clickSpreads = true;
			const auto& rect = v.m_layout->pages()[0];
			v.requestLink(
			    {static_cast<float>(rect.x + 80. / 360 * rect.width),
			     static_cast<float>(rect.y + 50. / 540 * rect.height)},
			    1);
			break;
		}
		case 14:
			check(!v.m_launchedUri.empty() && firstInSpread() == 0,
			      "spread clicks swallowed an existing PDF link");
			status.Text(
			    L"PASS: long-document gesture rendering, last-page anchor, "
			    L"live zoom, spread clicks, endpoints and link priority");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void gesturesTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (step < 10) {
			check(!v.Message().IsOpen(), to_string(v.Message().Message()));
		}
		if (!ready(v) || ticks < 3 || v.m_resettingGesture) {
			return;
		}
		const auto center = [&v] {
			return Windows::Foundation::Point{
			    static_cast<float>(v.Scroller().ViewportWidth() / 2),
			    static_cast<float>(v.Scroller().ViewportHeight() / 2)};
		};
		const auto localCenter = [&] {
			return v.Pages()
			    .TransformToVisual(v.Scroller())
			    .Inverse()
			    .TransformPoint(center());
		};
		switch (step) {
		case 0:
			owner.m_presenter.Restore();
			owner.AppWindow().Resize({900, 700});
			v.setThumbnailsOpen(false);
			v.zoomTo(2, {0, 0});
			check(v.Scroller().ZoomMode() == ZoomMode::Enabled &&
			          v.Scroller().HorizontalScrollMode() ==
			              ScrollMode::Enabled &&
			          v.Scroller().VerticalScrollMode() == ScrollMode::Enabled,
			      "native pinch and both scroll axes must be enabled");
			break;
		case 1:
			v.Scroller().ChangeView(100., 150., nullptr, true);
			break;
		case 2:
			check(std::abs(v.Scroller().HorizontalOffset() - 100) < 1 &&
			          std::abs(v.Scroller().VerticalOffset() - 150) < 1,
			      "native scroll did not move both axes");
			gesturePoint = localCenter();
			// Hold the final callback so the real transformed center can be
			// saved.
			v.m_viewChanged.revoke();
			check(v.Scroller().ChangeView(200., 300., 1.5f, true),
			      "native zoom request failed");
			break;
		case 3:
			check(std::abs(v.Scroller().ZoomFactor() - 1.5f) < 1e-5,
			      "native transform did not settle");
			gesturePoint = localCenter();
			v.nativeViewChanged(true);
			check(v.m_zoom == 2 && v.ZoomText().Text() == L"300%",
			      "intermediate gesture changed layout or lost zoom label");
			v.m_viewChanged = v.Scroller().ViewChanged(
			    auto_revoke,
			    [&v](auto&&, const ScrollViewerViewChangedEventArgs& e) {
				    v.nativeViewChanged(e.IsIntermediate());
			    });
			v.nativeViewChanged(false);
			break;
		case 4: {
			check(std::abs(v.m_zoom - 3) < 1e-5 &&
			          std::abs(v.Scroller().ZoomFactor() - 1.f) < 1e-5 &&
			          v.m_preset == helpers::ZoomPreset::None,
			      "gesture scale was not baked into PDF zoom");
			const auto point = localCenter();
			const auto page = v.m_layout->pageAt(point.X, point.Y);
			check(page == 0 && std::abs(point.Y - gesturePoint.Y * 1.5) < 12,
			      "gesture lost the reading position");
			check(v.Scroller().MinZoomFactor() >= .1f &&
			          v.Scroller().MinZoomFactor() * v.m_zoom >=
			              helpers::MIN_ZOOM &&
			          std::abs(v.Scroller().MaxZoomFactor() * v.m_zoom -
			                   helpers::MAX_ZOOM) < 1e-5,
			      "native limits no longer match PDF zoom limits");
			v.Scroller().ChangeView(nullptr, nullptr, .5f, true);
			break;
		}
		case 5:
			check(std::abs(v.m_zoom - 1.5) < 1e-5 &&
			          v.ZoomText().Text() == L"150%",
			      "second gesture compounded the previous transform");
			v.zoomIn();
			check(v.m_zoom > 1.5, "keyboard zoom stopped after pinch");
			v.setPreset(helpers::ZoomPreset::PageFit);
			check(v.m_preset == helpers::ZoomPreset::PageFit,
			      "fit preset stopped after pinch");
			check(v.beginPresentation([] {}), "presentation start after pinch");
			check(v.Scroller().ZoomMode() == ZoomMode::Disabled,
			      "presentation allowed native zoom");
			v.endPresentation();
			check(v.Scroller().ZoomMode() == ZoomMode::Enabled,
			      "presentation did not restore native zoom");
			break;
		case 6:
			v.zoomTo(helpers::MAX_ZOOM, {0, 0});
			check(v.Scroller().MaxZoomFactor() == 1.f,
			      "pinch can exceed the PDF maximum");
			v.Scroller().ChangeView(0., 0., .5f, true);
			break;
		case 7:
			check(std::abs(v.m_zoom - 5) < 1e-5, "pinch away from maximum");
			v.zoomTo(helpers::MIN_ZOOM, {0, 0});
			check(v.Scroller().MinZoomFactor() == 1.f &&
			          v.Scroller().MaxZoomFactor() == 10.f,
			      "native limits at the PDF minimum");
			v.Scroller().ChangeView(0., 0., 10.f, true);
			break;
		case 8:
			if (gestureRound == 0) {
				check(std::abs(v.m_zoom - 1) < 1e-5, "pinch away from minimum");
				v.rotateView(1);
			}
			v.setLayout(static_cast<helpers::ScrollMode>(gestureRound / 3),
			            static_cast<helpers::SpreadMode>(gestureRound % 3));
			v.zoomTo(1.5, {0, 0});
			v.Scroller().ChangeView(nullptr, nullptr, 1.2f, true);
			break;
		case 9:
			check(std::abs(v.m_zoom - 1.8) < 1e-5 &&
			          std::abs(v.Scroller().ZoomFactor() - 1.f) < 1e-5 &&
			          !v.m_holdCurrent,
			      "pinch failed in a rotated scroll/spread layout");
			if (++gestureRound < 12) {
				step = 8;
				ticks = 0;
				return;
			}
			break;
		case 10:
			gestureSize = v.m_sizes[0];
			v.m_sizes[0] = {50'000'000, 50'000'000};
			v.Scroller().ChangeView(nullptr, nullptr, 1.1f, true);
			break;
		case 11:
			v.m_sizes[0] = gestureSize;
			check(std::abs(v.m_zoom - 1.8) < 1e-5 &&
			          std::abs(v.Scroller().ZoomFactor() - 1.f) < 1e-5 &&
			          v.Message().IsOpen() && !v.m_holdCurrent,
			      "oversized pinch did not restore readable state");
			v.Message().IsOpen(false);
			break;
		case 12:
			// A hidden tab can miss the end of an ordinary pan, at native 1x.
			v.m_directManipulation = true;
			v.activate();
			check(!v.m_directManipulation,
			      "reactivating a tab retained a completed native pan");
			v.zoomIn();
			check(v.m_zoom > 1.8, "zoom stayed blocked after tab activation");
			v.setLayout(helpers::ScrollMode::Vertical,
			            helpers::SpreadMode::None);
			v.zoomTo(2, {0, 0});
			v.goToPage(0);
			v.m_directManipulation = true;
			v.Scroller().ChangeView(0., 0., 2.f, true);
			break;
		case 13:
			check(std::abs(v.Scroller().ZoomFactor() - 2.f) < 1e-5,
			      "navigation interruption needs an active native scale");
			v.goToPage(v.m_sizes.size() - 1);
			break;
		case 14:
			v.m_directManipulation = false;
			v.queueNativeZoom();
			break;
		case 15:
			check(v.m_current == v.m_sizes.size() - 1,
			      "page navigation during pinch landed on the wrong page");
			v.m_directManipulation = true;
			v.Scroller().ChangeView(nullptr, nullptr, .5f, true);
			break;
		case 16: {
			const auto rotation = v.m_rotation;
			v.rotateView(1);
			v.setLayout(helpers::ScrollMode::Page, helpers::SpreadMode::Odd);
			check(v.m_rotation == rotation &&
			          v.m_scrollMode == helpers::ScrollMode::Vertical,
			      "active pinch changed its underlying page geometry");
			if (v.beginPresentation([] {})) {
				check(std::abs(v.Scroller().ZoomFactor() - 1.f) < 1e-5 &&
				          !v.m_resettingGesture,
				      "presentation retained a native pinch transform");
				v.endPresentation();
			}
			v.m_directManipulation = false;
			v.queueNativeZoom();
			break;
		}
		case 17:
			v.close();
			v.nativeViewChanged(false);
			status.Text(L"PASS: native zoom synchronization, anchor, limits, "
			            L"both scroll axes, presets and presentation");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void panTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || ticks < 3 || v.m_linkPending) {
			return;
		}
		using Tool = ViewerPage::CursorTool;
		const auto linkPoint = [&v] {
			const auto& rect = v.m_layout->pages()[0];
			return v.Pages()
			    .TransformToVisual(v.Scroller())
			    .TransformPoint(
			        {static_cast<float>(rect.x + 80. / 360 * rect.width),
			         static_cast<float>(rect.y + 50. / 540 * rect.height)});
		};
		switch (step) {
		case 0:
			first = v.get_strong();
			owner.m_presenter.Restore();
			owner.AppWindow().Resize({900, 700});
			check(v.cursorTool() == Tool::Select, "default text tool");
			check(!v.beginPan({100, 100}), "text mode started pan");
			v.setThumbnailsOpen(false);
			v.zoomTo(3, {0, 0});
			v.selectAllText();
			owner.onCursorTool(owner.HandToolItem(), {});
			check(v.cursorTool() == Tool::Hand &&
			          owner.HandToolItem().IsChecked(),
			      "hand menu selection");
			break;
		case 1:
			check(v.Scroller().ScrollableWidth() > 100 &&
			          v.Scroller().ScrollableHeight() > 100,
			      "pan fixture extents");
			v.Scroller().ChangeView(100., 100., nullptr, true);
			break;
		case 2: {
			panX = v.Scroller().HorizontalOffset();
			panY = v.Scroller().VerticalOffset();
			const auto selection = v.m_selection;
			v.beginSelection({100, 100});
			check(selection == v.m_selection && !v.m_textPointerDown,
			      "hand press changed text selection");
			check(v.beginPan({200, 200}), "hand gesture start");
			v.movePan({150, 120});
			check(v.m_pan.active() && v.m_pan.moved() && v.m_ignorePanTap,
			      "hand drag state");
			check(v.m_selection == selection && v.m_zoom == 3 &&
			          v.m_rotation == 0,
			      "pan changed reading state");
			v.finishPan({150, 120});
			check(!v.m_pan.active() && !v.m_linkPending, "drag followed link");
			break;
		}
		case 3:
			check(std::abs(v.Scroller().HorizontalOffset() - panX - 50) < 1 &&
			          std::abs(v.Scroller().VerticalOffset() - panY - 80) < 1,
			      "hand drag did not move both scroll axes");
			check(v.beginPan({100, 100}), "zoom pan start");
			v.zoomTo(2, {0, 0});
			check(!v.m_pan.active(), "zoom retained drag");
			check(v.beginPan({100, 100}), "rotation pan start");
			v.rotateView(1);
			check(!v.m_pan.active(), "rotation retained drag");
			check(v.beginPan({100, 100}), "Find pan start");
			v.openFind();
			check(!v.m_pan.active(), "Find retained drag");
			v.closeFind();
			v.m_panHover = true;
			v.beginPan({100, 100});
			v.stopPan(true);
			check(!v.m_pan.active() && !v.m_panHover && !v.ProtectedCursor(),
			      "deactivation retained drag cursor");
			v.beginPan({100, 100});
			owner.openDocument(engine, L"tests/fixtures/links.pdf", {});
			break;
		case 4:
			check(&v != first.get() && v.cursorTool() == Tool::Select &&
			          !first->m_pan.active() && !first->m_panHover,
			      "tab switch leaked cursor/drag");
			owner.Tabs().SelectedItem(owner.m_tabs.front().tab);
			break;
		case 5:
			check(&v == first.get() && v.cursorTool() == Tool::Hand,
			      "tab lost hand mode");
			owner.updateCursorToolMenu();
			check(owner.HandToolItem().IsChecked(), "menu lost per-tab tool");
			v.beginPan({100, 100});
			owner.onCursorTool(owner.SelectToolItem(), {});
			check(!v.m_pan.active() && !v.ProtectedCursor() &&
			          v.cursorTool() == Tool::Select && v.canCopySelection(),
			      "text tool lost selection or retained cursor");
			owner.Tabs().SelectedItem(owner.m_tabs.back().tab);
			break;
		case 6:
			v.setCursorTool(Tool::Hand);
			v.setPreset(helpers::ZoomPreset::PageFit);
			v.goToPage(0);
			break;
		case 7: {
			const auto point = linkPoint();
			check(v.beginPan(point), "link tap start");
			v.finishPan(point);
			check(v.m_linkPending, "hand tap did not query link");
			break;
		}
		case 8: {
			check(v.m_launchedUri == "https://example.com/sable?x=1#part",
			      "hand tap failed to follow link");
			v.m_launchedUri.clear();
			const auto point = linkPoint();
			v.beginPan(point);
			v.finishPan(point);
			// The queued reply cannot run on this UI thread before the next
			// gesture.
			check(v.beginPan(point), "new gesture after link tap");
			v.movePan({point.X - 10, point.Y - 10});
			v.stopPan();
			break;
		}
		case 9:
			check(v.m_launchedUri.empty(),
			      "stale tap followed link during drag");
			v.beginPan({100, 100});
			v.close();
			check(!v.m_pan.active() && !v.ProtectedCursor() &&
			          !v.beginPan({100, 100}),
			      "closed viewer retained hand gesture");
			owner.closeTab(owner.m_tabs.back().tab);
			break;
		case 10:
			check(&v == first.get() && v.cursorTool() == Tool::Select,
			      "remaining tab tool changed");
			status.Text(L"PASS: hand drag, links, selection, menus, tabs and "
			            L"cancellation");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		++step;
		ticks = 0;
	}
	void rotationTick(MainWindow& owner) {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if ((step != 9 && !ready(v)) || v.m_copying || v.m_finding ||
		    (v.m_findTimer && v.m_findTimer.IsRunning()) || v.m_linkPending) {
			return;
		}
		const auto click = [&v](int page, double x, double y) {
			const auto point = helpers::rotatePoint({x, y}, v.m_rotation);
			const auto& rect = v.m_layout->pages()[page];
			v.requestLink({static_cast<float>(rect.x + point.x * rect.width),
			               static_cast<float>(rect.y + point.y * rect.height)});
		};
		switch (step) {
		case 0:
			first = v.get_strong();
			rotationSize = v.m_sizes[0];
			v.m_copySink = [this](const std::string& text) {
				copiedText = text;
				++copyCalls;
			};
			v.setThumbnailsOpen(true);
			v.setPreset(helpers::ZoomPreset::PageFit);
			v.requestText(0);
			v.openFind();
			v.FindBox().Text(L"Alpha");
			break;
		case 1:
			if (!v.m_text.contains(0) || !v.m_findActive) {
				return;
			}
			check(v.m_findMatches.size() == 4, "rotation Find setup");
			v.m_findActive = 0;
			v.m_current = 0;
			v.applyPreset();
			v.goToPage(0);
			break;
		case 2: {
			check(v.m_rotation == rotationRound, "quarter-turn sequence");
			check(v.m_current == 0, "rotation check moved off first page");
			const auto source = v.m_sizes[0];
			const auto shown = v.displaySizes()[0];
			check(shown.width ==
			              (rotationRound % 2 ? source.height : source.width) &&
			          shown.height ==
			              (rotationRound % 2 ? source.width : source.height),
			      "rotation displayed size");
			check(v.key(0).rotation == rotationRound &&
			          v.thumbKey(0).rotation == rotationRound,
			      "rotation raster keys");
			check(
			    std::abs(v.m_strip->imageHeight(0) / helpers::THUMBNAIL_WIDTH -
			             shown.height / shown.width) < .01,
			    "rotated thumbnail shape");
			check(std::abs(v.m_zoom - helpers::presetZoom(
			                              helpers::ZoomPreset::PageFit, shown,
			                              v.Scroller().ActualWidth(),
			                              v.Scroller().ActualHeight())) < .001,
			      "rotation fitted zoom at turn " +
			          std::to_string(rotationRound));
			const auto point = [&v](int index) {
				const auto box =
				    v.viewBox(v.m_text.at(0).text->characters[index].box);
				const auto& rect = v.m_layout->pages()[0];
				return Windows::Foundation::Point{
				    static_cast<float>(rect.x + (box.left + box.right) * .5 *
				                                    rect.width),
				    static_cast<float>(rect.y + (box.top + box.bottom) * .5 *
				                                    rect.height)};
			};
			check(v.hitText(0, point(0), false) == 0 &&
			          v.hitText(0, point(4), false) == 4,
			      "rotated text hit");
			v.beginSelection(point(0));
			v.extendSelection(point(4));
			v.endSelection();
			v.copySelection();
			pdf::LinkDestination partial;
			partial.x = .25;
			partial.fit = pdf::LinkFit::Width;
			const auto mapped = v.viewDestination(partial);
			check(mapped.fit == (rotationRound % 2 ? pdf::LinkFit::Height
			                                       : pdf::LinkFit::Width),
			      "rotated destination fit");
			check(rotationRound % 2
			          ? (!mapped.x && mapped.y &&
			             std::abs(*mapped.y -
			                      (rotationRound == 1 ? .25 : .75)) < 1e-9)
			          : (!mapped.y && mapped.x &&
			             std::abs(*mapped.x -
			                      (rotationRound == 0 ? .25 : .75)) < 1e-9),
			      "partial destination axis");
			partial.fit = pdf::LinkFit::Rectangle;
			partial.x = .1;
			partial.y = .2;
			partial.width = .3;
			partial.height = .4;
			const auto rectangle = v.viewDestination(partial);
			const double expected[4][4] = {{.1, .2, .3, .4},
			                               {.4, .1, .4, .3},
			                               {.6, .4, .3, .4},
			                               {.2, .6, .4, .3}};
			check(rectangle.x && rectangle.y &&
			          std::abs(*rectangle.x - expected[rotationRound][0]) <
			              1e-9 &&
			          std::abs(*rectangle.y - expected[rotationRound][1]) <
			              1e-9 &&
			          std::abs(rectangle.width - expected[rotationRound][2]) <
			              1e-9 &&
			          std::abs(rectangle.height - expected[rotationRound][3]) <
			              1e-9,
			      "rotated rectangle destination");
			break;
		}
		case 3: {
			check(copiedText == "Alpha" && copyCalls == rotationRound + 1,
			      "rotated selection copy");
			const auto active = v.m_findActive;
			const auto selection = v.m_selection;
			v.rotateView(1);
			check(v.m_selection == selection && v.m_findActive == active &&
			          v.m_findMatches.size() == 4,
			      "rotation discarded reading state");
			v.m_findNeedsReveal = true;
			v.revealFindMatch();
			check(!v.m_findNeedsReveal, "rotated Find reveal");
			// an upside-down hit is near the foot; return before checking fit
			v.goToPage(0);
			if (++rotationRound < 4) {
				step = 2;
				ticks = 0;
				return;
			}
			break;
		}
		case 4: {
			const auto oldKey = v.key(0);
			const auto bitmap = v.m_cache.find(oldKey);
			if (!bitmap || !v.m_device.bitmaps.contains(0)) {
				return;
			}
			pdf::RenderResult old{oldKey, *bitmap, false};
			const auto oldGpu = v.m_device.bitmaps.at(0);
			v.rotateView(2);
			check(v.key(0).width == oldKey.width &&
			          v.key(0).height == oldKey.height &&
			          v.key(0).rotation == 2,
			      "half-turn raster size");
			const auto gpu = v.m_device.bitmaps.find(0);
			const auto fresh =
			    gpu == v.m_device.bitmaps.end() ? nullptr : gpu->second.get();
			check(fresh != oldGpu.get(), "half turn kept old GPU pixels");
			v.rendered(std::move(old));
			const auto after = v.m_device.bitmaps.find(0);
			check((after == v.m_device.bitmaps.end()
			           ? nullptr
			           : after->second.get()) == fresh,
			      "late half-turn raster replaced current pixels");
			check(v.m_sizes[0].width == rotationSize.width &&
			          v.m_sizes[0].height == rotationSize.height,
			      "view changed canonical print size");
			owner.openDocument(engine, L"tests/fixtures/links.pdf", {});
			break;
		}
		case 5:
			check(&v != first.get() && v.m_rotation == 0 &&
			          first->m_rotation == 2,
			      "rotation leaked between tabs");
			v.rotateView(1);
			click(0, 80.0 / 360, 50.0 / 540);
			break;
		case 6:
			check(v.m_launchedUri == "https://example.com/sable?x=1#part",
			      "view-rotated external link");
			v.m_launchedUri.clear();
			click(1, .28, .2);
			break;
		case 7:
			check(v.m_launchedUri == "HTTP://example.com/rotated",
			      "intrinsic and view rotation link");
			v.m_launchedUri.clear();
			click(0, 80.0 / 360, 50.0 / 540);
			v.rotateView(1);
			break;
		case 8:
			check(v.m_launchedUri.empty(), "late link acted after rotation");
			click(0, 80.0 / 360, 50.0 / 540);
			v.close();
			v.rotateView(1);
			check(v.m_rotation == 2, "closed view rotated");
			break;
		case 9:
			check(v.m_launchedUri.empty() && first->m_rotation == 2,
			      "closed link or tab rotation isolation");
			owner.closeTab(owner.m_tabs.back().tab);
			break;
		case 10:
			check(&v == first.get() && v.m_rotation == 2 &&
			          v.m_findMatches.size() == 4 && v.canCopySelection(),
			      "returning to tab lost rotated reading state");
			v.PageBox().Focus(FocusState::Programmatic);
			v.PageBox().Text(L"2");
			owner.MainMenu().ShowAt(owner.MenuButton());
			owner.onRotateView(owner.RotateCounterClockwiseItem(), {});
			owner.MainMenu().Hide();
			check(v.m_rotation == 1 && v.m_current == 1,
			      "menu rotation lost pending page-number edit");
			break;
		case 11:
			v.zoomTo(1.25, {0, 0});
			v.goToPage(1);
			v.rotateView(-1);
			check(v.m_rotation == 0 && v.m_zoom == 1.25 && v.m_current == 1,
			      "numeric zoom changed during counterclockwise rotation");
			break;
		case 12: {
			const auto before = v.key(0);
			const auto size = v.m_sizes[0];
			v.m_sizes[0] = {20'000'000, 20'000'000};
			v.rotateView(1);
			v.m_sizes[0] = size;
			check(v.m_rotation == 0 && v.key(0) == before && v.m_zoom == 1.25,
			      "oversized rotation changed the readable view");
			status.Text(
			    L"PASS: view rotation, rasters, text, Find, thumbnails, "
			    L"destinations, tab isolation and late callbacks");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
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
			if (choiceOnly) {
				choiceFieldTick(*self);
				return;
			}
			if (checkBoxOnly || radioOnly) {
				buttonFieldTick(*self);
				return;
			}
			if (editOnly) {
				editTick(*self);
				return;
			}
			if (layoutOnly) {
				layoutTick(*self);
				return;
			}
			if (presentationOnly) {
				presentationTick(*self);
				return;
			}
			if (gesturesOnly) {
				gesturesTick(*self);
				return;
			}
			if (longGesturesOnly) {
				longGesturesTick(*self);
				return;
			}
			if (panOnly) {
				panTick(*self);
				return;
			}
			if (rotationOnly) {
				rotationTick(*self);
				return;
			}
			if (propertiesOnly) {
				propertiesTick(*self);
				return;
			}
			if (musicOnly) {
				musicTick(*self);
				return;
			}
			if (keyboardInputOnly) {
				keyboardInputTick(*self);
				return;
			}
			if (keyboardOnly) {
				keyboardTick(*self);
				return;
			}
			if (saveCopyOnly) {
				saveCopyTick(*self);
				return;
			}
			if (attachmentSaveOnly) {
				attachmentSaveTick(*self);
				return;
			}
			if (attachmentsOnly) {
				attachmentsTick(*self);
				return;
			}
			if (annotationActionsOnly) {
				annotationActionsTick(*self);
				return;
			}
			if (highlightOnly) {
				highlightTick(*self);
				return;
			}
			if (notesOnly) {
				if (noteEditOnly) {
					noteEditTick(*self);
				} else if (notePlacementOnly) {
					notePlacementTick(*self);
				} else {
					notesTick(*self);
				}
				return;
			}
			if (commentsOnly) {
				if (commentLocationsOnly) {
					commentLocationsTick(*self);
				} else {
					commentsTick(*self);
				}
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
				wchar_t directory[2048]{};
				const auto length = GetEnvironmentVariableW(
				    L"SableSettingsCaptureDirectory", directory, 2048);
				if (length) {
					check(length < 2048, "settings capture path is too long");
					settingsCaptureDirectory = directory;
					settingsCapturePhase = 0;
				}
				ticks = 0;
				return;
			}
			if (settingsOnly && settingsCapturePhase >= 0) {
				settingsCaptureTick(*self);
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
				if (!windowsOnly) {
					check(closedDropWindow->m_closing &&
					      closedDropWindow->m_tabs.empty());
				}
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
				const auto beforeTheme = uiStore->get();
				const auto originalFill = self->RootGrid()
				                              .Background()
				                              .as<Media::SolidColorBrush>()
				                              .Color();
				settings->m_theme.SelectedIndex(1);
				check(appearance::obsidian() &&
				          uiStore->get().general.Theme == "obsidian" &&
				          !settings->m_error.IsOpen(),
				      "Obsidian selection failed");
				const auto isBlack = [](const Media::Brush& brush) {
					const auto solid = brush.try_as<Media::SolidColorBrush>();
					return solid &&
					       solid.Color() == Windows::UI::Color{255, 0, 0, 0};
				};
				check(isBlack(self->RootGrid().Background()),
				      "reader is not pure black");
				check(
				    isBlack(settings->window.Content().as<Grid>().Background()),
				    "settings is not pure black");
				check(helpers::loadSettings(settingsPath,
				                            helpers::versionString())
				              .general.Theme == "obsidian",
				      "theme did not persist");
				check(uiStore->get().keybinds == beforeTheme.keybinds,
				      "theme changed keybinds");
				peerWindow = app.createWindow();
				check(isBlack(peerWindow->RootGrid().Background()),
				      "new window did not inherit Obsidian");
				settings->window.Close();
				self->onOpenSettings({}, {});
				const auto reopened = self->m_settingsWindow;
				check(reopened->m_theme.SelectedIndex() == 1,
				      "reopened settings lost theme");
				reopened->m_theme.SelectedIndex(0);
				check(!appearance::obsidian() &&
				          self->RootGrid()
				                  .Background()
				                  .as<Media::SolidColorBrush>()
				                  .Color() == originalFill &&
				          uiStore->get() == beforeTheme,
				      "returning to Dark did not restore palette and "
				      "preferences");
				check(peerWindow->RootGrid()
				              .Background()
				              .as<Media::SolidColorBrush>()
				              .Color() == originalFill,
				      "existing peer did not return to Dark");
				peerWindow->Close();
				keybindSettings(*reopened);
				reopened->window.Close();
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
				failing->m_theme.SelectedIndex(1);
				check(failing->m_error.IsOpen() &&
				          failing->m_theme.SelectedIndex() == 0 &&
				          !appearance::obsidian() &&
				          blocked.get().general.Theme == "dark",
				      "failed theme save was not restored");
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
				} else if (windowsOnly) {
					status.Text(
					    L"PASS: startup size, independent windows, pending "
					    L"close, settings ownership and shared preferences");
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
				auto* viewer = get_self<ViewerPage>(current);
				status.Text(status.Text() + L", page " +
				            to_hstring(viewer->m_current + 1) + L", zoom " +
				            to_hstring(viewer->m_zoom));
				if (gesturesOnly) {
					status.Text(status.Text() + L", height " +
					            to_hstring(viewer->Pages().Height()) + L"/" +
					            to_hstring(viewer->Scroller().ExtentHeight()));
					status.Text(status.Text() + L", factor " +
					            to_hstring(viewer->Scroller().ZoomFactor()) +
					            L", reset " +
					            to_hstring(viewer->m_resettingGesture) +
					            L", start " +
					            to_hstring(viewer->m_startPage.has_value()) +
					            L", settle " + to_hstring(viewer->m_settling) +
					            L", landing " +
					            to_hstring(viewer->m_landingAt.has_value()));
				}
			}
			timer.Stop();
			probeFail(status.Text());
		}
	}
};
} // namespace winrt::Sable::implementation

Window tabsTest(pdf::Engine& engine, Sable::implementation::App& app) {
	wchar_t appearanceMode[8]{};
	const bool blackStartup =
	    GetEnvironmentVariableW(L"SableObsidianStartupTest", appearanceMode,
	                            8) &&
	    std::wstring_view(appearanceMode) == L"true";
	auto startup = helpers::nightpdf_default_settings(helpers::versionString());
	startup.general.Theme = blackStartup ? "obsidian" : "dark";
	// Native probes use isolated defaults, regardless of the owner's saved
	// theme.
	auto window = make_self<Sable::implementation::MainWindow>(startup);
	if (blackStartup &&
	    (!appearance::obsidian() ||
	     window->RootGrid().Background().as<Media::SolidColorBrush>().Color() !=
	         Windows::UI::Color{255, 0, 0, 0})) {
		throw std::runtime_error("Obsidian did not apply at startup");
	}
	auto state =
	    std::make_shared<Sable::implementation::TabsTest>(*window, engine, app);
	window->setSettingsStore(*state->uiStore);
	std::weak_ptr<Sable::implementation::TabsTest> weak = state;
	wchar_t mode[8]{};
	state->checkBoxOnly =
	    GetEnvironmentVariableW(L"SableCheckBoxTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->radioOnly = GetEnvironmentVariableW(L"SableRadioTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	state->choiceOnly = GetEnvironmentVariableW(L"SableChoiceTest", mode, 8) &&
	                    std::wstring_view(mode) == L"true";
	state->choiceList =
	    GetEnvironmentVariableW(L"SableChoiceListTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->settingsOnly =
	    GetEnvironmentVariableW(L"SableSettingsTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->windowsOnly =
	    GetEnvironmentVariableW(L"SableWindowsTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	if (state->windowsOnly) {
		// I can check windows separately when the storage broker is blocked.
		state->step = 16;
	}
	state->propertiesOnly =
	    GetEnvironmentVariableW(L"SablePropertiesTest", mode, 8) &&
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
	state->annotationActionsOnly =
	    GetEnvironmentVariableW(L"SableAnnotationActionsTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->highlightOnly =
	    GetEnvironmentVariableW(L"SableHighlightTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	if (GetEnvironmentVariableW(L"SableUnderlineTest", mode, 8) &&
	    std::wstring_view(mode) == L"true") {
		state->highlightOnly = true;
		state->selectionMarkup = pdf::TextMarkup::Underline;
		state->markupSubtype = "Underline";
	}
	if (GetEnvironmentVariableW(L"SableStrikeOutTest", mode, 8) &&
	    std::wstring_view(mode) == L"true") {
		state->highlightOnly = true;
		state->selectionMarkup = pdf::TextMarkup::StrikeOut;
		state->markupSubtype = "StrikeOut";
	}
	state->noteEditOnly =
	    GetEnvironmentVariableW(L"SableNoteEditTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	if (GetEnvironmentVariableW(L"SableMarkupCommentTest", mode, 8) &&
	    std::wstring_view(mode) == L"true") {
		state->markupCommentOnly = true;
		state->noteEditOnly = true;
		state->highlightOnly = false;
	}
	state->notePlacementOnly =
	    GetEnvironmentVariableW(L"SableNotePlacementTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->notesOnly = GetEnvironmentVariableW(L"SableNotesTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	state->notesOnly =
	    state->notesOnly || state->notePlacementOnly || state->noteEditOnly;
	state->commentsOnly =
	    GetEnvironmentVariableW(L"SableCommentsTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->commentLocationsOnly =
	    GetEnvironmentVariableW(L"SableCommentLocationsTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->commentsOnly =
	    state->commentsOnly || state->commentLocationsOnly || state->notesOnly;
	state->keyboardInputOnly =
	    GetEnvironmentVariableW(L"SableKeyboardInputTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->keyboardOnly =
	    GetEnvironmentVariableW(L"SableKeyboardTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->saveCopyOnly =
	    GetEnvironmentVariableW(L"SableSaveCopyTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->attachmentSaveOnly =
	    GetEnvironmentVariableW(L"SableAttachmentSaveTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->attachmentsOnly =
	    GetEnvironmentVariableW(L"SableAttachmentsTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->findOnly = GetEnvironmentVariableW(L"SableFindTest", mode, 8) &&
	                  std::wstring_view(mode) == L"true";
	state->layoutOnly = GetEnvironmentVariableW(L"SableLayoutTest", mode, 8) &&
	                    std::wstring_view(mode) == L"true";
	state->presentationOnly =
	    GetEnvironmentVariableW(L"SablePresentationTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->panOnly = GetEnvironmentVariableW(L"SablePanTest", mode, 8) &&
	                 std::wstring_view(mode) == L"true";
	state->gesturesOnly =
	    GetEnvironmentVariableW(L"SableGesturesTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->longGesturesOnly =
	    GetEnvironmentVariableW(L"SableLongGesturesTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->rotationOnly =
	    GetEnvironmentVariableW(L"SableRotationTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->shortcuts = GetEnvironmentVariableW(L"SableShortcutTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	if (state->choiceOnly || state->checkBoxOnly || state->radioOnly ||
	    state->settingsOnly || state->windowsOnly || state->filtersOnly ||
	    state->linksOnly || state->printOnly || state->finishOnly ||
	    state->editOnly || state->musicOnly || state->selectionOnly ||
	    state->findOnly || state->outlineOnly || state->attachmentsOnly ||
	    state->commentsOnly || state->attachmentSaveOnly ||
	    state->annotationActionsOnly || state->rotationOnly ||
	    state->propertiesOnly || state->panOnly || state->layoutOnly ||
	    state->presentationOnly || state->saveCopyOnly || state->keyboardOnly ||
	    state->keyboardInputOnly || state->gesturesOnly ||
	    state->longGesturesOnly) {
		state->shortcuts = false;
	} else {
		state->dropSetup = state->makeDropData();
	}
	state->status.Text(L"Checking document tabs");
	state->status.TextWrapping(TextWrapping::Wrap);
	Border report;
	state->report = report;
	if (state->presentationOnly) {
		report.Visibility(Visibility::Collapsed);
	}
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
	if (state->choiceOnly || state->checkBoxOnly || state->radioOnly) {
		state->buttonFieldPath = makeTempDir("button_ui") / "button.pdf";
		{
			std::ofstream source(state->buttonFieldPath, std::ios::binary);
			source << (state->choiceOnly  ? choicePdf(!state->choiceList)
			           : state->radioOnly ? radioPdf()
			                              : checkBoxPdf());
		}
		window->openDocument(engine, state->buttonFieldPath, 1);
	} else if (state->propertiesOnly) {
		window->openDocument(engine, L"tests/fixtures/properties.pdf", 1);
	} else if (state->attachmentsOnly || state->attachmentSaveOnly) {
		window->openDocument(engine, L"tests/fixtures/attachments.pdf", 1);
	} else if (state->outlineOnly) {
		window->openDocument(engine, L"tests/fixtures/outline.pdf", 1);
	} else if (state->highlightOnly || state->markupCommentOnly ||
	           state->annotationActionsOnly) {
		window->openDocument(engine, L"tests/fixtures/text.pdf", 1);
	} else if (state->notesOnly) {
		window->openDocument(engine, L"tests/fixtures/comment_locations.pdf",
		                     1);
	} else if (state->commentsOnly) {
		window->openDocument(engine,
		                     state->commentLocationsOnly
		                         ? L"tests/fixtures/comment_locations.pdf"
		                         : L"tests/fixtures/comments.pdf",
		                     1);
	} else if (state->selectionOnly || state->findOnly || state->rotationOnly ||
	           state->panOnly || state->gesturesOnly ||
	           state->longGesturesOnly || state->layoutOnly ||
	           state->presentationOnly || state->keyboardOnly ||
	           state->keyboardInputOnly) {
		window->openDocument(engine, L"tests/fixtures/text.pdf", 1);
	} else if (state->editOnly || state->saveCopyOnly) {
		window->openDocument(engine, L"tests/fixtures/pages.pdf", 2);
	} else if (state->printOnly) {
		window->openDocument(engine, L"tests/fixtures/pages.pdf", 2);
		window->openDocument(engine, L"tests/fixtures/print.pdf", {});
	} else if (state->linksOnly) {
		window->openDocument(engine, L"tests/fixtures/links.pdf", {});
	} else if (!state->settingsOnly && !state->windowsOnly &&
	           !state->finishOnly && !state->musicOnly) {
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
