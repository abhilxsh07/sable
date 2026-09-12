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
void TabsTest::openNightRun(MainWindow& owner) {
	const auto modifiers = helpers::SHORTCUT_ALT | helpers::SHORTCUT_SHIFT;
	check(!owner.m_nightRun && owner.Tabs().TabItems().Size() == 0,
	      "Night Run showed at startup");
	check(owner.routeKey('T', modifiers, true) && !owner.m_nightRun,
	      "a held shortcut opened Night Run");
	check(owner.routeKey('T', modifiers) && owner.nightRunSelected() &&
	          owner.Documents().Visibility() == Visibility::Visible &&
	          owner.Tabs().Visibility() == Visibility::Visible &&
	          owner.Nav().Visibility() == Visibility::Collapsed &&
	          !owner.PrintItem().IsEnabled(),
	      "the shortcut did not open the game from Home");
}

void TabsTest::checkNightRunKeys(MainWindow& owner) {
	using night_run::Phase;
	const auto modifiers = helpers::SHORTCUT_ALT | helpers::SHORTCUT_SHIFT;
	const auto game = owner.m_nightRun;
	const auto root = owner.RootGrid().XamlRoot();
	check(game->playfieldFocused(Input::FocusManager::GetFocusedElement(root)),
	      "the opening shortcut did not focus the playfield");
	check(game->m_run.phase() == Phase::Ready,
	      "the game was not ready (phase " +
	          std::to_string(static_cast<int>(game->m_run.phase())) + ")");
	check(owner.routeKey(VK_UP, 0), "the up arrow did not reach the game");
	check(game->m_run.phase() == Phase::Running,
	      "the game did not start with the keyboard");
	game->m_next = std::chrono::steady_clock::now();
	check(owner.routeKey(VK_UP, 0, true), "held jump was not handled");
	game->tick();
	check(game->m_run.lift() == 0, "held jump made the runner jump");
	check(owner.routeKey(VK_UP, 0), "jump was not handled");
	game->m_next = std::chrono::steady_clock::now();
	game->tick();
	check(game->m_run.lift() > 0, "jump did not lift the runner");
	check(owner.m_nightRunTab.Focus(FocusState::Keyboard) &&
	          !owner.routeKey(VK_LEFT, 0) && game->m_run.lane() == 1,
	      "the game took a tab strip arrow");
	check(game->m_mute.Focus(FocusState::Keyboard) &&
	          !owner.routeKey(VK_UP, 0) && !owner.routeKey(VK_SPACE, 0),
	      "the game took keys from Mute");
	check(game->focus() && owner.routeKey('D', 0) && game->m_run.lane() == 2 &&
	          owner.routeKey('A', 0) && game->m_run.lane() == 1,
	      "focused gameplay did not steer the runner");
	owner.openDocument(engine, L"tests/fixtures/pages.pdf", 1);
	const auto viewer = owner.Viewer();
	const auto lift = game->m_run.lift();
	check(viewer && !owner.nightRunSelected() &&
	          game->element().Visibility() == Visibility::Collapsed &&
	          game->m_run.phase() == Phase::Paused,
	      "opening a PDF did not pause and hide the game");
	check(owner.routeKey('T', modifiers) && owner.nightRunSelected() &&
	          owner.Tabs().TabItems().Size() == 2 &&
	          game->m_run.phase() == Phase::Paused &&
	          game->m_run.lift() == lift,
	      "reselecting the game did not keep the paused run");
}

void TabsTest::closeNightRun(MainWindow& owner) {
	using night_run::Phase;
	const auto game = owner.m_nightRun;
	const auto viewer = owner.m_tabs.front().viewer;
	check(game->playfieldFocused(Input::FocusManager::GetFocusedElement(
	          owner.RootGrid().XamlRoot())),
	      "a background PDF took the focus from the track");
	check(owner.routeKey(VK_SPACE, 0) && game->m_run.phase() == Phase::Running,
	      "the game did not resume");
	check(owner.routeKey('W', helpers::SHORTCUT_CTRL),
	      "the close-tab shortcut was not handled");
	check(!owner.m_nightRun, "the close-tab shortcut left the game open");
	check(owner.Viewer() == viewer, "closing Night Run did not select the PDF");
	check(owner.m_closedTabs.empty() && game->m_run.phase() == Phase::Paused,
	      "closing the game changed the reopen history or left it running");
	owner.closeTab(owner.m_tabs.front().tab);
	check(owner.Tabs().TabItems().Size() == 0 &&
	          owner.Nav().Visibility() == Visibility::Visible,
	      "closing the last tab did not return Home");
}

void TabsTest::finishTick(MainWindow& owner) {
	if (step == 0) {
		// a window in the background pauses the game, so the game opens
		// only once the reader holds the foreground.
		HWND handle{};
		check_hresult(
		    owner.get_strong().as<IWindowNative>()->get_WindowHandle(&handle));
		if (!bringToFront(handle)) {
			if (ticks >= 40) {
				check(false, "the reader never took the foreground from the " +
				                 foregroundOwner());
			}
			return;
		}
		openNightRun(owner);
		++step;
		return;
	}
	if (step == 1) {
		checkNightRunKeys(owner);
		++step;
		return;
	}
	if (!get_self<ViewerPage>(owner.m_tabs.front().viewer)->m_layout) {
		check(ticks < 40, "the background PDF did not load");
		return;
	}
	closeNightRun(owner);
	check(Application::Current().RequestedTheme() == ApplicationTheme::Dark &&
	          owner.RootGrid().ActualTheme() == ElementTheme::Dark,
	      "reader did not force dark theme");
	const auto stack = owner.MortyStack();
	const auto modifiers = helpers::SHORTCUT_ALT | helpers::SHORTCUT_SHIFT;
	check(stack.Children().Size() == 0 && owner.m_morties.empty(),
	      "Morty alert showing at start");
	owner.routeKey('R', helpers::SHORTCUT_SHIFT);
	owner.routeKey('M', helpers::SHORTCUT_ALT);
	check(stack.Children().Size() == 0, "Shift+R or Alt+M added a Morty alert");
	for (int press = 0; press < 3; ++press) {
		check(owner.routeKey('M', modifiers), "Shift+Alt+M wasn't handled");
	}
	check(owner.routeKey('M', modifiers, true) &&
	          stack.Children().Size() == 3 && owner.m_morties.size() == 3,
	      "Morty alerts didn't pile up, or a held key added one");
	const auto top = stack.Children().GetAt(0).as<Controls::InfoBar>();
	check(top == owner.m_morties.back().alert && top.IsOpen() &&
	          top.Title() == L"MORTY!" &&
	          top.Message() ==
	              L"I'm a PDF reader Morty! I'm PDF reader Rick!!!",
	      "newest Morty alert isn't on top or its text changed");
	const auto now = std::chrono::steady_clock::now();
	check(owner.m_morties.front().fadeAt > now + std::chrono::seconds(3),
	      "Morty alert fades too soon");
	for (std::size_t i = 1; i < owner.m_morties.size(); ++i) {
		check(owner.m_morties[i].fadeAt - owner.m_morties[i - 1].fadeAt >=
		          std::chrono::milliseconds(700),
		      "Morty alerts don't fade one by one");
	}
	const auto kept = owner.m_morties.front().alert;
	owner.removeMorty(owner.m_morties[1].alert);
	check(stack.Children().Size() == 2 && owner.m_morties.front().alert == kept,
	      "closing one Morty alert touched the others");
	for (int press = 0; press < 10; ++press) {
		owner.routeKey('M', modifiers);
	}
	std::size_t showing = 0;
	for (const auto& morty : owner.m_morties) {
		showing += morty.fading ? 0 : 1;
	}
	check(showing == 8, "Morty pile isn't capped at eight");
	while (!owner.m_morties.empty()) {
		owner.removeMorty(owner.m_morties.front().alert);
	}
	check(stack.Children().Size() == 0, "Morty alerts left behind");
	peerWindow = app.createWindow();
	// the new window has no XamlRoot yet, so it can't route keys
	peerWindow->showMorty();
	check(peerWindow->MortyStack().Children().Size() == 1,
	      "new window has no Morty alert");
	peerWindow->Close();
	diagnostics::write(diagnostics::Level::Info, "native logging marker");
	const auto file =
	    std::filesystem::temp_directory_path() / L"Sable-probe-logs" /
	    (L"Sable-" + std::to_wstring(GetCurrentProcessId()) + L".log");
	std::ifstream input(file, std::ios::binary);
	const std::string log{std::istreambuf_iterator<char>(input), {}};
	check(log.find("native logging marker") != std::string::npos &&
	          log.find("Verbose logging enabled") != std::string::npos &&
	          log.find("I'm a PDF reader Morty!") != std::string::npos,
	      "startup/debug/Morty file logging");
	status.Text(
	    L"PASS: Night Run, Morty alerts and startup/debug/file logging");
	probePass(owner.get_strong().as<Window>());
	timer.Stop();
}

void TabsTest::editTick(MainWindow& owner) {
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
		check(owner.CopyItem().IsEnabled() && !owner.CutItem().IsEnabled() &&
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
		check(!owner.CopyItem().IsEnabled() && !owner.CutItem().IsEnabled() &&
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

void TabsTest::deliverKeys(
    std::initializer_list<std::pair<unsigned, unsigned>> chords) {
	DWORD process = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &process);
	check(process == GetCurrentProcessId() &&
	          GetForegroundWindow() == inputWindow,
	      "input check lost the reader foreground; no keys sent");
	for (const auto modifier : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN,
	                            VK_LBUTTON, VK_RBUTTON, VK_MBUTTON}) {
		check((GetAsyncKeyState(modifier) & 0x8000) == 0,
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
	const auto sent = SendInput(static_cast<UINT>(events.size()), events.data(),
	                            sizeof(::INPUT));
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

void TabsTest::keyboardInputTick(MainWindow& owner) {
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
		check_hresult(owner.get_strong().as<IWindowNative>()->get_WindowHandle(
		    &inputWindow));
		owner.Activate();
		bringToFront(inputWindow);
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
		check(v.m_rotation == 0 &&
		          (v.PageBox().Text() == L"3r" || v.PageBox().Text() == L"3R"),
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
		check(v.findOpen() && Input::FocusManager::GetFocusedElement(
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
		          Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen,
		      "Windows-delivered F11 did not enter full screen");
		deliverKeys({{VK_F11, 0}});
		break;
	case 25:
		check(owner.AppWindow().Presenter().Kind() !=
		          Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen,
		      "second F11 did not leave full screen");
		v.showProperties();
		break;
	case 26:
		check(v.propertiesOpen(), "properties pane did not open");
		inputZoom = v.m_zoom;
		deliverKeys({{VK_ADD, ctrl}, {'R', 0}});
		break;
	case 27:
		// the pane isn't modal: the reader's keys still work beside it
		check(v.m_zoom != inputZoom && v.m_rotation == 1,
		      "the properties pane swallowed OS-delivered reader keys");
		v.dismissProperties();
		break;
	case 28:
		check(!v.propertiesOpen(), "properties pane did not close");
		status.Text(L"PASS: Windows input delivery, text editing, zoom, Find, "
		            L"rotation, tools, presentation and the properties pane");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::redrawTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current || ticks < 3) {
		return;
	}
	auto& viewer = *get_self<ViewerPage>(current);
	if (!ready(viewer) || viewer.m_rendering) {
		return;
	}
	const auto wanted = viewer.key(0);
	switch (step) {
	case 0:
		if (!viewer.m_device.bitmaps.contains(0)) {
			return;
		}
		viewer.m_loggedPlaceholders.clear();
		viewer.m_cache.clear();
		viewer.m_device.bitmaps.erase(0);
		viewer.rendered({wanted, {}, false});
		check(viewer.m_retriedRenders.contains(wanted),
		      "an empty render did not start its retry");
		break;
	case 1:
		if (!viewer.m_device.bitmaps.contains(0)) {
			return;
		}
		check(!viewer.m_failed.contains(wanted),
		      "the successful retry left a failed raster");
		viewer.m_cache.clear();
		viewer.m_device.bitmaps.erase(0);
		viewer.rendered({wanted, {}, false});
		check(viewer.m_failed.contains(wanted),
		      "the repeated empty render was not bounded");
		viewer.invalidate(0);
		break;
	case 2:
		if (!viewer.m_loggedPlaceholders.contains(wanted)) {
			return;
		}
		check(!viewer.m_device.bitmaps.contains(0),
		      "the failed page did not stay on its placeholder");
		viewer.invalidate(0);
		break;
	case 3:
		check(viewer.m_loggedPlaceholders.count(wanted) == 1,
		      "the idle placeholder diagnostic was not retained");
		viewer.m_failed.erase(wanted);
		viewer.m_retriedRenders.erase(wanted);
		viewer.requestNext();
		break;
	case 4: {
		if (!viewer.m_device.bitmaps.contains(0)) {
			return;
		}
		redrawDevice = viewer.m_device.device;
		viewer.m_device.bitmaps.erase(0);
		const auto maximum = viewer.m_device.context->GetMaximumBitmapSize();
		const pdf::Bitmap oversized{static_cast<int>(maximum) + 1, 1, 0, {}};
		check(!viewer.showRaster(0, &oversized) &&
		          viewer.m_deviceRecoveryPending &&
		          viewer.m_retriedUploads.contains(wanted),
		      "a failed upload did not queue device recovery");
		check(!viewer.showRaster(0, &oversized),
		      "a repeated oversized upload unexpectedly succeeded");
		break;
	}
	case 5: {
		if (viewer.m_deviceRecoveryPending ||
		    !viewer.m_device.bitmaps.contains(0)) {
			return;
		}
		check(viewer.m_device.device.get() != redrawDevice.get(),
		      "device recovery did not replace the failed device");
		const auto maximum = viewer.m_device.context->GetMaximumBitmapSize();
		const pdf::Bitmap oversized{static_cast<int>(maximum) + 1, 1, 0, {}};
		check(!viewer.showRaster(0, &oversized) &&
		          !viewer.m_deviceRecoveryPending,
		      "the upload recovery kept queuing rebuilds");
		status.Text(L"PASS: bounded render retry and upload device recovery");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	}
	++step;
	ticks = 0;
}

void TabsTest::keyboardTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current || ticks < 3) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	constexpr auto ctrl = helpers::SHORTCUT_CTRL;
	constexpr auto shift = helpers::SHORTCUT_SHIFT;
	constexpr auto alt = helpers::SHORTCUT_ALT;
	const auto key = [&](unsigned code, unsigned modifiers = 0) {
		return owner.routeKey(code, modifiers);
	};
	// the password prompt is modal: the reader's keys stay off the page
	// behind it, and Esc (from Windows, as the dialog handles it) closes it
	if (step == 10) {
		check(ticks < 115, "the password prompt didn't open");
		if (!v.m_passwordDialog || ticks < 6) {
			return;
		}
		const auto before = v.m_zoom;
		check(!key('F', ctrl) && !v.findOpen() && !key(VK_ADD, ctrl) &&
		          v.m_zoom == before && !key('R') && v.m_rotation == 0,
		      "the password prompt let Ctrl-F, Ctrl-plus or R through");
		check_hresult(owner.get_strong().as<IWindowNative>()->get_WindowHandle(
		    &inputWindow));
		owner.Activate();
		bringToFront(inputWindow);
		++step;
		ticks = 0;
		return;
	}
	if (step == 11) {
		if (ticks < 3) {
			return;
		}
		deliverKeys({{VK_ESCAPE, 0}});
		++step;
		ticks = 0;
		return;
	}
	if (step == 12) {
		if (v.m_passwordDialog) {
			check(ticks < 60, "Esc didn't close the password prompt");
			return;
		}
		owner.closeTab(owner.m_tabs.back().tab);
		owner.Tabs().SelectedIndex(0);
		++step;
		ticks = 0;
		return;
	}
	if (!ready(v) || v.m_finding || v.m_findNeedsReveal ||
	    (v.m_findTimer && v.m_findTimer.IsRunning())) {
		return;
	}
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
		          key('S') && v.cursorTool() == ViewerPage::CursorTool::Select,
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
		          key(VK_F3) && v.m_findActive != before && key(VK_F3, shift) &&
		          v.m_findActive == before,
		      "Find next/previous shortcuts failed");
		key(VK_ESCAPE);
		check(!v.findOpen(), "Escape did not close Find");
		v.showProperties();
		break;
	}
	case 9: {
		check(v.propertiesOpen(), "properties pane did not open");
		const auto beforeZoom = v.m_zoom;
		check(key('F', ctrl) && v.findOpen(),
		      "the properties pane swallowed Ctrl-F");
		key(VK_ESCAPE);
		check(key(VK_ADD, ctrl) && v.m_zoom != beforeZoom,
		      "the properties pane swallowed zoom");
		v.dismissProperties();
		// a modal prompt: the password one, in a tab of its own
		owner.openDocument(engine, L"tests/fixtures/password.pdf", 1);
		break;
	}
	case 13:
		key('P', ctrl | alt);
		break;
	case 14:
		check(v.m_presentation.has_value(), "presentation key did not enter");
		key(VK_DOWN, ctrl);
		break;
	case 15:
		check(v.m_current + 1 == v.m_sizes.size(),
		      "presentation Ctrl-Down missed last page");
		key(VK_UP, ctrl | shift);
		break;
	case 16:
		check(v.m_current == 0, "presentation Ctrl-Up missed first page");
		owner.m_copyPicker = [](hstring) { return attachmentChoice(L"", {}); };
		key('S', ctrl);
		check(!v.m_presentation, "Save did not leave presentation");
		break;
	case 17:
		check(!owner.m_saveCopyCancel, "cancelled keyboard save stayed active");
		key(VK_F11);
		check(owner.AppWindow().Presenter().Kind() ==
		          Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen,
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
	case 18:
		check(owner.Tabs().SelectedIndex() == 0 && key('J') &&
		          owner.Tabs().SelectedIndex() == 1 && first->m_current == 0,
		      "custom binding lost priority over viewer letters");
		status.Text(L"PASS: global focus, navigation, zoom aliases, Find, "
		            L"tools, modal guards, presentation and custom bindings");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
