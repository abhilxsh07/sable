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
void TabsTest::tick() {
	auto self = window.get();
	if (!self) {
		timer.Stop();
		return;
	}
	if (ticking) {
		return;
	}
	ticking = true;
	const struct Reset {
		bool& flag;
		~Reset() {
			flag = false;
		}
	} reset{ticking};
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
				where = focus == v->Scroller()                 ? L"document"
				        : focus == v->PageBox()                ? L"page box"
				        : focus == self->Tabs().SelectedItem() ? L"tab header"
				                                               : L"other";
			}
			status.Text(L"Focus: " + where + L"; tab " +
			            to_hstring(self->Tabs().SelectedIndex() + 1) + L" of " +
			            to_hstring(self->m_tabs.size()) + L"; drag events " +
			            to_hstring(dragStarts) + L"/" +
			            to_hstring(dragFinishes) + L"; result " +
			            to_hstring(dragResult));
			return;
		}
		if (++ticks > 120) {
			throw std::runtime_error("tab check timed out");
		}
		if (passwordFormOnly) {
			passwordFormTick(*self);
			return;
		}
		if (mixedFormOnly) {
			mixedFormTick(*self);
			return;
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
		if (redrawOnly) {
			redrawTick(*self);
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
		if (organizeOnly) {
			organizeTick(*self);
			return;
		}
		if (combineOnly) {
			combineTick(*self);
			return;
		}
		if (exportOnly) {
			exportTick(*self);
			return;
		}
		if (readAloudOnly) {
			readAloudTick(*self);
			return;
		}
		if (toolSurfacesOnly) {
			toolSurfacesTick(*self);
			return;
		}
		if (highlightOnly) {
			highlightTick(*self);
			return;
		}
		if (notesOnly) {
			if (noteEditOnly) {
				noteEditTick(*self);
			} else if (inkDrawingOnly) {
				inkDrawingTick(*self);
			} else if (inkEditOnly) {
				inkEditTick(*self);
			} else if (stampEditOnly) {
				stampEditTick(*self);
			} else if (lineArrowOnly) {
				lineArrowTick(*self);
			} else if (textBoxUnicodeOnly) {
				textBoxUnicodeTick(*self);
			} else if (stampOnly) {
				stampTick(*self);
			} else if (fillMarksOnly) {
				fillMarksTick(*self);
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
			check(self->Tabs().CanDragTabs() && self->Tabs().CanReorderTabs());
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
			check(self->m_tabs.empty() && !self->Viewer() && first->m_closed);
			check(self->Nav().Visibility() == Visibility::Visible &&
			      !self->ZoomInItem().IsEnabled());
			self->openDocument(engine, L"tests/fixtures/password.pdf", {});
			self->openDocument(engine, L"tests/fixtures/pages.pdf", {});
			get_self<ViewerPage>(self->m_tabs[0].viewer)->askPassword(false);
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
			      Microsoft::UI::Windowing::OverlappedPresenterState::Restored);
			check(sizingWindow->m_tabs.empty());
			sizingWindow->openDocument(engine, L"tests/fixtures/pages.pdf", {});
			break;
		case 18:
			if (!ready(*get_self<ViewerPage>(sizingWindow->Viewer()))) {
				return;
			}
			check(sizingWindow->m_presenter.State() ==
			      Microsoft::UI::Windowing::OverlappedPresenterState::Restored);
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
			check(
			    sizingWindow->m_presenter.State() ==
			    Microsoft::UI::Windowing::OverlappedPresenterState::Maximized);
			check(sizingWindow->m_tabs.empty());
			sizingWindow->openDocument(engine, L"tests/fixtures/pages.pdf", {});
			break;
		case 20:
			if (!ready(*get_self<ViewerPage>(sizingWindow->Viewer()))) {
				return;
			}
			check(
			    sizingWindow->m_presenter.State() ==
			    Microsoft::UI::Windowing::OverlappedPresenterState::Maximized);
			sizingWindow->Close();
			{
				check(app.m_windows.size() == 1);
				sizingWindow =
				    app.createWindow({L"tests/fixtures/pages.pdf"}, 1);
				peerWindow = app.createWindow({L"tests/fixtures/pages.pdf"}, 2);
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
			sizingWindow->openDocument(engine, L"tests/fixtures/missing.pdf",
			                           {});
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
			check(!peerWindow->m_closing && peerWindow->m_preset == "default");
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
			check_hresult(self.as<::IWindowNative>()->get_WindowHandle(&owner));
			check_hresult(
			    settings->window.as<::IWindowNative>()->get_WindowHandle(
			        &child));
			check(GetWindow(child, GW_OWNER) == owner, "settings owner");
			check(!IsWindowEnabled(owner), "settings owner is not blocked");
			self->onOpenSettings({}, {});
			check(self->m_settingsWindow == settings, "duplicate settings");
			settings->select("general");
			check(settings->m_general.Visibility() == Visibility::Visible &&
			      settings->m_version.Visibility() == Visibility::Collapsed);
			for (const auto& box : settings->m_checks) {
				check(box.IsChecked().Value());
				box.IsChecked(false);
				settings->save(box);
				check(!settings->m_error.IsOpen(), "general save failed");
			}
			check(!uiStore->get().general.MaximizeOnOpen &&
			      !uiStore->get().general.DisplayThumbs);
			check(helpers::loadSettings(
			          settingsPath, helpers::versionString()) == uiStore->get(),
			      "general values were not persisted");
			// an Obsidian start goes back to Dark first, so the round trip
			// below begins where a Dark start does.
			if (appearance::obsidian()) {
				settings->m_theme.SelectedIndex(0);
				check(!appearance::obsidian() &&
				          uiStore->get().general.Theme == "dark",
				      "an Obsidian start did not return to Dark");
			}
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
			check(isBlack(settings->window.Content().as<Grid>().Background()),
			      "settings is not pure black");
			check(helpers::loadSettings(settingsPath, helpers::versionString())
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
				status.Text(L"PASS: settings owner, General, keybind "
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
		status.Text(L"FAIL at step " + to_hstring(step) + L": " + to_message());
		if (auto current = self->Viewer()) {
			auto* viewer = get_self<ViewerPage>(current);
			status.Text(status.Text() + L", page " +
			            to_hstring(viewer->m_current + 1) + L", zoom " +
			            to_hstring(viewer->m_zoom));
			if (gesturesOnly) {
				status.Text(status.Text() + L", height " +
				            to_hstring(viewer->Pages().Height()) + L"/" +
				            to_hstring(viewer->Scroller().ExtentHeight()));
				status.Text(
				    status.Text() + L", factor " +
				    to_hstring(viewer->Scroller().ZoomFactor()) + L", reset " +
				    to_hstring(viewer->m_resettingGesture) + L", start " +
				    to_hstring(viewer->m_startPage.has_value()) + L", settle " +
				    to_hstring(viewer->m_settling) + L", landing " +
				    to_hstring(viewer->m_landingAt.has_value()));
			}
		}
		timer.Stop();
		probeFail(status.Text());
	}
}
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
	// the isolated store has to agree with the theme the window started in.
	if (blackStartup) {
		state->uiStore->setTheme("obsidian");
	}
	window->setSettingsStore(*state->uiStore);
	std::weak_ptr<Sable::implementation::TabsTest> weak = state;
	wchar_t mode[8]{};
	state->defaultListOnly =
	    GetEnvironmentVariableW(L"SableDefaultListTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->clearListOnly =
	    state->defaultListOnly ||
	    (GetEnvironmentVariableW(L"SableClearListTest", mode, 8) &&
	     std::wstring_view(mode) == L"true");
	state->sortedChoiceOnly =
	    GetEnvironmentVariableW(L"SableSortedChoiceTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->noSpellCheckOnly =
	    GetEnvironmentVariableW(L"SableNoSpellCheckTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->inheritedCheckBoxOnly =
	    GetEnvironmentVariableW(L"SableInheritedCheckBoxTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->inheritedOnly =
	    !state->inheritedCheckBoxOnly &&
	    GetEnvironmentVariableW(L"SableInheritedTextTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->combOnly = !state->inheritedCheckBoxOnly && !state->inheritedOnly &&
	                  GetEnvironmentVariableW(L"SableCombTest", mode, 8) &&
	                  std::wstring_view(mode) == L"true";
	state->multilineOnly =
	    !state->inheritedCheckBoxOnly && !state->combOnly &&
	    GetEnvironmentVariableW(L"SableMultilineTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->passwordFormOnly =
	    GetEnvironmentVariableW(L"SablePasswordFormTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->mixedFormOnly =
	    state->multilineOnly || state->combOnly || state->inheritedOnly ||
	    state->inheritedCheckBoxOnly || state->noSpellCheckOnly ||
	    state->sortedChoiceOnly || state->clearListOnly ||
	    (GetEnvironmentVariableW(L"SableMixedFormTest", mode, 8) &&
	     std::wstring_view(mode) == L"true");
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
	state->ellipsePlacementOnly =
	    GetEnvironmentVariableW(L"SableEllipsePlacementTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->notePlacementOnly =
	    state->notePlacementOnly || state->ellipsePlacementOnly;
	state->inkDrawingOnly = GetEnvironmentVariableW(L"SableInkTest", mode, 8) &&
	                        std::wstring_view(mode) == L"true";
	state->stampOnly = GetEnvironmentVariableW(L"SableStampTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	state->inkEditOnly =
	    GetEnvironmentVariableW(L"SableInkEditTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->stampEditOnly =
	    GetEnvironmentVariableW(L"SableStampEditTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->lineArrowOnly =
	    GetEnvironmentVariableW(L"SableLineArrowTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->textBoxUnicodeOnly =
	    GetEnvironmentVariableW(L"SableTextBoxUnicodeTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->fillMarksOnly =
	    GetEnvironmentVariableW(L"SableFillMarksTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->organizeOnly =
	    GetEnvironmentVariableW(L"SableOrganizeTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->combineOnly =
	    GetEnvironmentVariableW(L"SableCombineTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->exportOnly = GetEnvironmentVariableW(L"SableExportTest", mode, 8) &&
	                    std::wstring_view(mode) == L"true";
	state->readAloudOnly =
	    GetEnvironmentVariableW(L"SableReadAloudTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->toolSurfacesOnly =
	    GetEnvironmentVariableW(L"SableToolSurfacesTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
	state->notesOnly = GetEnvironmentVariableW(L"SableNotesTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	state->notesOnly = state->notesOnly || state->notePlacementOnly ||
	                   state->noteEditOnly || state->inkDrawingOnly ||
	                   state->inkEditOnly || state->stampEditOnly ||
	                   state->lineArrowOnly || state->textBoxUnicodeOnly ||
	                   state->stampOnly || state->fillMarksOnly;
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
	state->redrawOnly = GetEnvironmentVariableW(L"SableRedrawTest", mode, 8) &&
	                    std::wstring_view(mode) == L"true";
	state->shortcuts = GetEnvironmentVariableW(L"SableShortcutTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	if (state->passwordFormOnly || state->mixedFormOnly || state->choiceOnly ||
	    state->checkBoxOnly || state->radioOnly || state->settingsOnly ||
	    state->windowsOnly || state->filtersOnly || state->linksOnly ||
	    state->printOnly || state->finishOnly || state->editOnly ||
	    state->musicOnly || state->selectionOnly || state->findOnly ||
	    state->outlineOnly || state->attachmentsOnly || state->commentsOnly ||
	    state->attachmentSaveOnly || state->annotationActionsOnly ||
	    state->rotationOnly || state->redrawOnly || state->propertiesOnly ||
	    state->panOnly || state->layoutOnly || state->presentationOnly ||
	    state->saveCopyOnly || state->keyboardOnly ||
	    state->keyboardInputOnly || state->gesturesOnly ||
	    state->longGesturesOnly || state->organizeOnly || state->combineOnly ||
	    state->exportOnly || state->readAloudOnly || state->toolSurfacesOnly) {
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
	if (state->passwordFormOnly) {
		state->buttonFieldPath = makeTempDir("password_ui") / "password.pdf";
		std::filesystem::copy_file("tests/fixtures/password_form.pdf",
		                           state->buttonFieldPath);
		window->openDocument(engine, L"tests/fixtures/pages.pdf", 1);
		window->openDocument(engine, state->buttonFieldPath, 1);
	} else if (state->mixedFormOnly || state->choiceOnly ||
	           state->checkBoxOnly || state->radioOnly) {
		state->buttonFieldPath = makeTempDir("button_ui") / "button.pdf";
		{
			std::ofstream source(state->buttonFieldPath, std::ios::binary);
			source << (state->mixedFormOnly
			               ? mixedFormPdf(
			                     {.multiline = state->multilineOnly,
			                      .comb = state->combOnly,
			                      .inherited = state->inheritedOnly,
			                      .inheritedCheckBox =
			                          state->inheritedCheckBoxOnly,
			                      .noSpellCheck = state->noSpellCheckOnly,
			                      .sortedChoices = state->sortedChoiceOnly,
			                      .listDefault = state->defaultListOnly,
			                      .textLimit = state->combOnly ? 4 : 32})
			           : state->choiceOnly ? choicePdf(!state->choiceList)
			           : state->radioOnly  ? radioPdf()
			                               : checkBoxPdf());
		}
		if (state->mixedFormOnly) {
			window->openDocument(engine, L"tests/fixtures/pages.pdf", 1);
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
	           state->redrawOnly || state->panOnly || state->gesturesOnly ||
	           state->longGesturesOnly || state->layoutOnly ||
	           state->presentationOnly || state->keyboardOnly ||
	           state->keyboardInputOnly) {
		window->openDocument(engine, L"tests/fixtures/text.pdf", 1);
	} else if (state->organizeOnly || state->exportOnly ||
	           state->readAloudOnly || state->toolSurfacesOnly) {
		window->openDocument(engine, L"tests/fixtures/pages.pdf", 1);
	} else if (state->editOnly || state->saveCopyOnly) {
		window->openDocument(engine, L"tests/fixtures/pages.pdf", 2);
	} else if (state->printOnly) {
		window->openDocument(engine, L"tests/fixtures/pages.pdf", 2);
		window->openDocument(engine, L"tests/fixtures/print.pdf", {});
	} else if (state->linksOnly) {
		window->openDocument(engine, L"tests/fixtures/links.pdf", {});
	} else if (!state->settingsOnly && !state->windowsOnly &&
	           !state->finishOnly && !state->musicOnly && !state->combineOnly) {
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
