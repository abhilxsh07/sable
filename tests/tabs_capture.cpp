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
#include "tabs_test.h"

namespace winrt::Sable::implementation {
void TabsTest::settingsCaptureTick(MainWindow& owner) {
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
		settingsWindow.AppWindow().Resize({static_cast<int32_t>(660 * scale),
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
		settingsWindow.AppWindow().Resize({static_cast<int32_t>(660 * scale),
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
		check_hresult(owner.try_as<IWindowNative>()->get_WindowHandle(&reader));
		captureMusicWindow(reader,
		                   settingsCaptureDirectory / L"obsidian-reader.png");
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

void TabsTest::keybindSettings(SettingsWindow& settings) {
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
	check(settings.m_saveBinding.IsEnabled(), "capture did not enable save");
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
	check(settings.saveBinding("OpenWindow", 1, settings.m_capture.binding()),
	      "secondary binding save");
	settings.dismiss();
	check(uiStore->get().keybinds.at("OpenWindow").keybind.size() == 2);
	check(settings.saveBinding("OpenWindow", 0, std::nullopt));
	check(helpers::KeybindHelper::fromKeybind(
	          uiStore->get().keybinds.at("OpenWindow").keybind.front(), "win32")
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
	    helpers::KeybindHelper::fromTrigger("Alt+F8", "win32").getKeybind()));
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

void TabsTest::propertiesTick(MainWindow& owner) {
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
		const auto rows =
		    viewer->PropertiesHost().Children().GetAt(0).as<Grid>();
		const auto text = rows.Children().GetAt(index * 2 + 1).as<TextBlock>();
		check(text.IsTextSelectionEnabled(),
		      "property values cannot be selected");
		return text.Text();
	};
	const auto retryShown = [&] {
		for (const auto& child : viewer->PropertiesHost().Children()) {
			if (const auto button = child.try_as<Button>();
			    button &&
			    unbox_value_or<hstring>(button.Content(), L"") == L"Retry") {
				return true;
			}
		}
		return false;
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
		check(viewer->propertiesOpen(), "properties pane did not open");
		const auto rows = viewer->PropertiesHost().Children().Size();
		owner.onDocumentProperties({}, {});
		check(viewer->propertiesOpen() &&
		          viewer->PropertiesHost().Children().Size() == rows,
		      "properties pane opened twice");
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
		viewer->PropertiesScroll().ChangeView(nullptr, 10000., nullptr, true);
		break;
	case 2:
		capture(L"properties-details.png");
		appearance::apply(true);
		viewer->PropertiesScroll().ChangeView(nullptr, 0., nullptr, true);
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
		check(viewer->m_propertiesVersion == propertiesRequest &&
		          value(13) == L"2" &&
		          value(12) == L"297.0 x 210.0 mm (A4, landscape)",
		      "cached properties did not follow current page/view rotation");
		owner.openDocument(engine, L"tests/fixtures/properties_missing.pdf", 1);
		// the pane belongs to the tab, so the one left behind keeps it
		check(first->propertiesOpen(),
		      "the hidden tab lost its properties pane");
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
		check(viewer == first.get() && viewer->propertiesOpen(),
		      "the first tab's properties didn't come back with it");
		viewer->dismissProperties();
		viewer->m_properties.reset();
		auto healthy = std::move(viewer->m_session);
		viewer->m_session = std::make_unique<pdf::Session>(engine);
		viewer->showProperties();
		check(viewer->m_propertiesFailed && retryShown(),
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
		          !retryShown(),
		      "properties retry failed");
		viewer->dismissProperties();
		break;
	case 11: {
		// A delayed reply can populate the cache, but cannot reopen a
		// dismissed pane.
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
		check(viewer->m_properties && !viewer->propertiesOpen(),
		      "late metadata resurrected the pane");
		viewer->showProperties();
		break;
	case 13:
		check(viewer->propertiesOpen());
		owner.closeTab(owner.m_tabs.front().tab);
		check(first->m_closed && !first->propertiesOpen() &&
		          !owner.PropertiesItem().IsEnabled(),
		      "closing a document kept properties active");
		peerWindow = app.createWindow();
		peerWindow->openDocument(engine, L"tests/fixtures/properties.pdf", 1);
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
		auto other = get_self<ViewerPage>(peerWindow->Viewer())->get_strong();
		if (!other->m_properties || other->m_propertiesPending) {
			return;
		}
		check(other->propertiesOpen(), "peer properties did not open");
		peerWindow->Close();
		check(other->m_closed && !other->propertiesOpen() &&
		          app.m_windows.size() == 1,
		      "closing a reader kept its properties pane alive");
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

void TabsTest::filterValues(MainWindow& owner,
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
		check(slider.Minimum() == spec.min && slider.Maximum() == spec.max &&
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

void TabsTest::filterTick(MainWindow& owner) {
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
				check(Automation::AutomationProperties::GetItemStatus(chip) ==
				          (chip == preset.button ? L"Selected" : L""),
				      "preset selection");
			}
			check(owner.DarkConfigurator().Visibility() ==
			          Visibility::Collapsed,
			      "inactive preset opened configurator");
			owner.onPresetClick(preset.button, {});
			check(owner.DarkConfigurator().Visibility() == Visibility::Visible,
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
		check(owner.m_preset == "custom", "slider changed preset selection");
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
		check(owner.DarkConfigurator().Visibility() == Visibility::Collapsed);
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
		const auto origin =
		    panel.TransformToVisual(owner.RootGrid()).TransformPoint({0, 0});
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
		status.Text(L"PASS: preset toggles, slider ranges/tooltips, shared "
		            L"filters, new tabs, window isolation and narrow layout");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	}
	++step;
	ticks = 0;
}

void TabsTest::musicTick(MainWindow& owner) {
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
		check(closedMusic.expired(), "music request retained a closed owner");
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
		check(Automation::AutomationProperties::GetName(pane.m_play) == L"Play",
		      "paused play label");
		state.controls.seek = state.controls.shuffle = state.controls.repeat =
		    true;
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
		state.timeline.updated = winrt::clock::now().time_since_epoch().count();
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
		check(pane.m_seek.Value() < 1, "new track kept the previous position");
		break;
	case 8: {
		if (musicArt.Status() == Windows::Foundation::AsyncStatus::Started) {
			return;
		}
		const auto encoded = musicArt.GetResults();
		auto bytes =
		    std::make_shared<std::vector<std::uint8_t>>(encoded.Length());
		Windows::Storage::Streams::DataReader::FromBuffer(encoded).ReadBytes(
		    *bytes);
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
		const auto position =
		    music.TransformToVisual(owner.RootGrid()).TransformPoint({0, 0});
		check(music.ActualWidth() > 0 && position.X >= 0,
		      "narrow music button");
		for (const auto chip : {owner.DefaultPreset(), owner.SepiaPreset(),
		                        owner.RedeyePreset(), owner.CustomPreset()}) {
			const auto point =
			    chip.TransformToVisual(owner.RootGrid()).TransformPoint({0, 0});
			check(chip.ActualWidth() > 0 &&
			          point.X >= position.X + music.ActualWidth() &&
			          point.X + chip.ActualWidth() <=
			              owner.RootGrid().ActualWidth(),
			      "music displaced a preset outside the narrow window");
		}
		wchar_t directory[2048]{};
		if (GetEnvironmentVariableW(L"SableMusicCaptureDirectory", directory,
		                            2048)) {
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
		state.timeline = {
		    0,    260 * music::TICKS_PER_SECOND, 195 * music::TICKS_PER_SECOND,
		    0,    260 * music::TICKS_PER_SECOND, 1,
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
		if (musicArt.Status() == Windows::Foundation::AsyncStatus::Started) {
			return;
		}
		const auto encoded = musicArt.GetResults();
		auto bytes =
		    std::make_shared<std::vector<std::uint8_t>>(encoded.Length());
		Windows::Storage::Streams::DataReader::FromBuffer(encoded).ReadBytes(
		    *bytes);
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
		if (ticks < 5 || !pane.m_open || (step != 30 && !pane.m_art.Source())) {
			return;
		}
		wchar_t directory[2048]{};
		check(GetEnvironmentVariableW(L"SableMusicCaptureDirectory", directory,
		                              2048) > 0);
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

void TabsTest::presentationTick(MainWindow& owner) {
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
			captureMusicWindow(handle, std::filesystem::path(directory) / name);
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
		v.setLayout(helpers::ScrollMode::Wrapped, helpers::SpreadMode::Even);
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
		check(v.m_presentation && v.mode() == ViewerMode::Present &&
		          owner.m_presentationViewer == current &&
		          owner.AppWindow().Presenter().Kind() == Kind::FullScreen,
		      "presentation did not enter full screen");
		check(v.m_current == outlinePage &&
		          v.scrollMode() == helpers::ScrollMode::Page &&
		          v.spreadMode() == helpers::SpreadMode::None &&
		          v.m_preset == helpers::ZoomPreset::PageFit,
		      "presentation page/layout/fit or mixed-size spread");
		check(owner.AppTitleBar().Visibility() == Visibility::Collapsed &&
		          v.Rail().Visibility() == Visibility::Collapsed &&
		          v.ThumbnailPanel().Visibility() == Visibility::Collapsed &&
		          v.PresentationBackdrop().Visibility() == Visibility::Visible,
		      "presentation chrome still visible");
		check(v.m_layout->width() <= v.Scroller().ViewportWidth() + 1 &&
		          v.m_layout->height() <= v.Scroller().ViewportHeight() + 1,
		      "presentation page does not fit viewport");
		check(!v.beginPan({40, 40}), "presentation started hand dragging");
		v.zoomIn();
		v.zoomReset();
		v.rotateView(1);
		check(v.m_preset == helpers::ZoomPreset::PageFit && v.m_rotation == 0,
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
		check(v.PresentationControls().Visibility() == Visibility::Collapsed,
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
		check(!v.m_presentation && v.mode() == ViewerMode::Read &&
		          !owner.m_presentationViewer &&
		          owner.AppWindow().Presenter().Kind() == Kind::Overlapped &&
		          owner.AppTitleBar().Visibility() == Visibility::Visible,
		      "presentation did not restore window chrome/presenter");
		check(v.scrollMode() == helpers::ScrollMode::Wrapped &&
		          v.spreadMode() == helpers::SpreadMode::Even &&
		          v.m_preset == helpers::ZoomPreset::None && v.m_zoom == zoom &&
		          v.m_current == 0 && v.thumbnailsOpen() &&
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
		v.requestLink({static_cast<float>(rect.x + 80. / 360 * rect.width),
		               static_cast<float>(rect.y + 130. / 540 * rect.height)},
		              1);
		break;
	}
	case 9:
		check(v.m_current == 1 && v.m_preset == helpers::ZoomPreset::PageFit,
		      "presentation internal link changed fit or failed");
		v.goToPage(0);
		break;
	case 10: {
		const auto& rect = v.m_layout->pages()[0];
		v.requestLink({static_cast<float>(rect.x + 80. / 360 * rect.width),
		               static_cast<float>(rect.y + 50. / 540 * rect.height)},
		              1);
		break;
	}
	case 11:
		check(v.m_current == 1 && v.m_launchedUri.empty(),
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

void TabsTest::layoutTick(MainWindow& owner) {
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
		if (GetEnvironmentVariableW(L"SableLayoutCaptureDirectory", directory,
		                            2048)) {
			std::filesystem::create_directories(directory);
			HWND reader{};
			check_hresult(
			    owner.try_as<IWindowNative>()->get_WindowHandle(&reader));
			captureMusicWindow(reader, std::filesystem::path(directory) /
			                               (std::to_wstring(layoutRound / 3) +
			                                L"-" + theme + L".png"));
		}
	};
	const auto nextLayout = [&] {
		if (++layoutRound < 12) {
			const auto selection = v.m_selection;
			v.setLayout(static_cast<Scroll>(layoutRound / 3),
			            static_cast<Spread>(layoutRound % 3));
			check(selection == v.m_selection, "layout cleared selected text");
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
		if (!v.m_text.contains(0) || !v.m_text.contains(1) || !v.m_findActive) {
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
			    static_cast<float>(rect.x +
			                       (box.left + box.right) * .5 * rect.width),
			    static_cast<float>(rect.y +
			                       (box.top + box.bottom) * .5 * rect.height)};
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
		check(std::abs(v.m_layoutViewWidth - v.Scroller().ActualWidth()) < 1 &&
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
		v.requestLink({static_cast<float>(rect.x + 80. / 360 * rect.width),
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
		v.requestLink({static_cast<float>(rect.x + 80. / 360 * rect.width),
		               static_cast<float>(rect.y + 50. / 540 * rect.height)});
		v.setLayout(Scroll::Horizontal, Spread::None);
		break;
	}
	case 15:
		check(v.m_launchedUri.empty(), "stale link acted after layout change");
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
		          std::to_string(v.Scroller().HorizontalOffset()) + " max " +
		          std::to_string(v.Scroller().ScrollableWidth()) + " target " +
		          std::to_string(v.m_layout->pages()[1].x));
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
} // namespace winrt::Sable::implementation
