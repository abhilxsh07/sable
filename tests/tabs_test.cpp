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
#include "../app/App.xaml.h"
#include "../app/SettingsWindow.h"
#include "../app/helpers/version.h"
#include "../app/MainWindow.xaml.h"
#include "../app/pdf/engine.h"
#include "surface_test.h"

#include <chrono>
#include <memory>
#include <microsoft.ui.xaml.window.h>
#include <stdexcept>
#include <string_view>
#include <winrt/Windows.Storage.h>
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
	void check(bool condition, const char* message = "tab check failed") {
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
	state->shortcuts = GetEnvironmentVariableW(L"SableShortcutTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	if (state->settingsOnly || state->filtersOnly || state->linksOnly) {
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
	if (state->linksOnly) {
		window->openDocument(engine, L"tests/fixtures/links.pdf", {});
	} else if (!state->settingsOnly) {
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
