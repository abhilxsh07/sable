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
#include <fstream>
#include <iterator>

namespace winrt::Sable::implementation {
// pages.pdf: 595 x 842, 792 x 612 and 300 x 200 turned 90 degrees.
void TabsTest::organizeTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_organizeBusy) {
		return;
	}
	const auto invoke = [](const FrameworkElement& element) {
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto select = [&](std::initializer_list<int> pages) {
		v.OrganizeGrid().SelectedItems().Clear();
		for (const int page : pages) {
			v.OrganizeGrid().SelectRange(Data::ItemIndexRange(page, 1));
		}
	};
	const auto sized = [&](std::size_t page, double width, double height) {
		return page < v.m_sizes.size() &&
		       std::abs(v.m_sizes[page].width - width) < .5 &&
		       std::abs(v.m_sizes[page].height - height) < .5;
	};
	switch (step) {
	case 0:
		v.m_exporting = true;
		v.openOrganize();
		check(!v.organizing(), "Organize opened during Export");
		{
			const auto close = owner.confirmNotes(current);
			check(close.Status() ==
			              Windows::Foundation::AsyncStatus::Completed &&
			          !close.GetResults(),
			      "close was allowed during Export");
		}
		v.m_exporting = false;
		v.m_organizeBusy = true;
		v.exportPages();
		check(!v.m_noteDialog, "Export opened during a page edit");
		{
			const auto close = owner.confirmNotes(current);
			check(close.Status() ==
			              Windows::Foundation::AsyncStatus::Completed &&
			          !close.GetResults(),
			      "close was allowed during a page edit");
		}
		v.m_organizeBusy = false;
		v.m_noteSaving = true;
		v.openOrganize();
		check(!v.organizing(), "Organize opened during a save");
		v.m_noteSaving = false;
		v.setThumbnailsOpen(true);
		v.setAllToolsOpen(true);
		v.openFind();
		owner.openTool(L"organize");
		check(v.organizing() && v.OrganizeGrid().Items().Size() == 3,
		      "Organize did not show a tile per page");
		break;
	case 1:
		if (v.m_organizeTile < 3) {
			check(ticks < 100, "Organize tiles did not render");
			return;
		}
		check(!v.thumbnailsOpen() && !v.findOpen() &&
		          v.AllToolsPanel().Visibility() == Visibility::Collapsed &&
		          v.QuickTools().Visibility() == Visibility::Collapsed &&
		          !v.ThumbnailsButton().IsEnabled() &&
		          !v.AllToolsButton().IsEnabled(),
		      "reader surfaces stayed active under Organize");
		owner.m_presenter.Restore();
		for (const auto& size : {Windows::Graphics::SizeInt32{1920, 1080},
		                         Windows::Graphics::SizeInt32{1366, 768},
		                         Windows::Graphics::SizeInt32{1024, 768}}) {
			owner.AppWindow().Resize(size);
			owner.RootGrid().UpdateLayout();
			v.UpdateLayout();
			const auto bounds = [&](const FrameworkElement& element) {
				return element.TransformToVisual(v).TransformBounds(
				    {0, 0, static_cast<float>(element.ActualWidth()),
				     static_cast<float>(element.ActualHeight())});
			};
			const auto grid = bounds(v.OrganizePanel());
			const auto rail = bounds(v.Rail());
			check(grid.Width > 0 && grid.X >= 0 &&
			          grid.X + grid.Width <= rail.X + 1 &&
			          grid.Y + grid.Height <= v.ActualHeight() + 1,
			      "Organize overlaps the rail or leaves the viewer bounds");
		}
		check(v.ThumbnailPanel().Background() &&
		          v.OutlinePanel().Background() &&
		          v.AttachmentPanel().Background(),
		      "a navigation panel has no opaque background");
		check(v.OrganizeStatus().Text() == L"3 pages" &&
		          !v.OrganizeDelete().IsEnabled(),
		      "Organize state without a selection");
		{
			// a drop can finish after another operation took the edit lock.
			auto items = v.OrganizeGrid().Items();
			const auto last = items.GetAt(2);
			items.RemoveAt(2);
			items.InsertAt(0, last);
			v.m_noteSaving = true;
			v.reorderPages({2});
			v.m_noteSaving = false;
			check(unbox_value<int>(
			          items.GetAt(0).as<FrameworkElement>().Tag()) == 0,
			      "a refused reorder left the tiles ahead of the PDF");
			v.showOrganizeState();
		}
		owner.m_shortcuts.push_back({VK_F9, 0, "close-tab", {}});
		v.m_exporting = true;
		v.showNotice(L"Checking the remapped close binding.");
		check(owner.routeKey(VK_F9, 0, false) &&
		          v.Message().Message() ==
		              L"Wait for the page operation to finish before closing.",
		      "Organize swallowed a remapped close-tab binding");
		v.m_exporting = false;
		owner.m_shortcuts.pop_back();
		v.showOrganizeState();
		check(owner.routeKey('F', helpers::SHORTCUT_CTRL, false) &&
		          !v.findOpen(),
		      "Organize let Ctrl+F open the hidden reader");
		check(owner.routeKey('A', helpers::SHORTCUT_CTRL, false) &&
		          v.organizeSelection().size() == 3 && !v.hasTextSelection(),
		      "Ctrl+A selected reader text instead of all tiles");
		select({0, 2});
		check(v.OrganizeStatus().Text() == L"3 pages · 2 selected",
		      "Organize did not count the selection");
		invoke(v.OrganizeRotateRight());
		break;
	case 2:
		check(v.m_notesDirty && sized(0, 842, 595) && sized(1, 792, 612) &&
		          sized(2, 300, 200) && v.OrganizeGrid().Items().Size() == 3,
		      "Rotate right did not turn the selected pages");
		check(v.organizeSelection() == std::vector<int>({0, 2}) &&
		          v.OrganizeRotateRight().IsEnabled(),
		      "rotation lost the selected pages");
		{
			// the first page is landscape now; it mustn't size the other cells.
			v.OrganizeGrid().UpdateLayout();
			double height = 0;
			for (uint32_t page = 0; page < 3; ++page) {
				const auto tile =
				    v.OrganizeGrid().Items().GetAt(page).as<StackPanel>();
				const auto cell = v.OrganizeGrid()
				                      .ContainerFromIndex(page)
				                      .as<FrameworkElement>();
				check(tile.ActualHeight() > 256 &&
				          cell.ActualHeight() >= tile.ActualHeight(),
				      "a mixed-size Organize tile was clipped");
				check(page == 0 || cell.ActualHeight() == height,
				      "Organize cells changed size with the page shape");
				height = cell.ActualHeight();
			}
		}
		{
			// what the grid does after a drag: the last tile goes first.
			auto items = v.OrganizeGrid().Items();
			const auto last = items.GetAt(2);
			items.RemoveAt(2);
			items.InsertAt(0, last);
			v.reorderPages({2});
		}
		break;
	case 3:
		check(sized(0, 300, 200) && sized(1, 842, 595) && sized(2, 792, 612),
		      "dragging the last page first did not move it");
		select({0});
		check(owner.routeKey(VK_DELETE, 0, false), "Organize ignored Delete");
		break;
	case 4:
		check(v.m_sizes.size() == 2 && sized(0, 842, 595) &&
		          v.OrganizeGrid().Items().Size() == 2,
		      "Delete did not remove the selected page");
		select({0, 1});
		invoke(v.OrganizeDelete());
		check(v.Message().IsOpen() &&
		          v.Message().Message() == L"A PDF needs at least one page." &&
		          v.m_sizes.size() == 2,
		      "deleting every page was not refused");
		check(owner.routeKey(VK_ESCAPE, 0, false), "Organize ignored Esc");
		break;
	case 5:
		check(!v.organizing() && v.m_layout && v.m_layout->pages().size() == 2,
		      "Done did not return to the changed pages");
		check(v.thumbnailsOpen() && v.findOpen() &&
		          v.AllToolsPanel().Visibility() == Visibility::Visible &&
		          v.ThumbnailsButton().IsEnabled(),
		      "Done did not restore the reader surfaces");
		v.closeFind();
		v.setAllToolsOpen(false);
		owner.openTool(L"comment");
		check(v.commentsOpen(), "Comment did not open Comments");
		v.setCommentsOpen(false);
		owner.openTool(L"fillsign");
		check(v.commentsOpen(), "Fill & Sign did not open Comments");
		break;
	case 6:
		if (!v.CommentsMoreButton().Flyout().IsOpen()) {
			check(ticks < 40, "Fill & Sign did not open its menu");
			return;
		}
		v.CommentsMoreButton().Flyout().Hide();
		v.close();
		status.Text(L"PASS: Organize rotate, move, delete and refusal");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected organize step");
	}
	++step;
	ticks = 0;
}

// Combine: two fixtures through the dialog into one saved, opened PDF.
void TabsTest::combineTick(MainWindow& owner) {
	using namespace Microsoft::UI::Xaml::Automation;
	const auto pressPrimary = [&](const ContentDialog& dialog) {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& node) -> Button {
			if (const auto button = node.try_as<Button>();
			    button && button.Name() == L"PrimaryButton") {
				return button;
			}
			for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node);
			     ++i) {
				if (const auto button =
				        find(Media::VisualTreeHelper::GetChild(node, i))) {
					return button;
				}
			}
			return nullptr;
		};
		const auto button = find(dialog);
		check(button && button.IsEnabled(), "Combine and save is disabled");
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	switch (step) {
	case 0:
		// the app hands its window the engine at launch; this probe opens
		// no PDF that would.
		owner.setEngine(engine);
		{
			// Fill & Sign, Comment and Read aloud are all real tools now
			const auto home = get_self<HomePage>(owner.Home());
			check(home->FillSignButton().IsEnabled() &&
			          home->CommentButton().IsEnabled() &&
			          home->ReadAloudButton().IsEnabled(),
			      "Home's tool buttons have the wrong state");
		}
		owner.openTool(L"combine");
		check(owner.m_combining && owner.m_combineDialog,
		      "Combine files did not open its dialog");
		break;
	case 1: {
		const auto dialog = owner.m_combineDialog;
		check(static_cast<bool>(dialog), "the Combine dialog closed early");
		const auto list = dialog.Content()
		                      .as<StackPanel>()
		                      .Children()
		                      .GetAt(1)
		                      .as<ListView>();
		for (const auto* name :
		     {L"tests/fixtures/pages.pdf", L"tests/fixtures/text.pdf"}) {
			TextBlock row;
			row.Text(name);
			row.Tag(
			    box_value(hstring(std::filesystem::absolute(name).wstring())));
			list.Items().Append(row);
		}
		list.SelectedIndex(1);
		notesPath = makeTempDir("combine") / L"combined.pdf";
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		pressPrimary(dialog);
		break;
	}
	case 2: {
		if (owner.m_combining) {
			check(ticks < 100, "Combine did not finish");
			return;
		}
		check(!owner.PickerError().IsOpen(),
		      "Combine reported: " + to_string(owner.PickerError().Message()));
		check(std::filesystem::exists(notesPath),
		      "the combined PDF was not saved");
		const auto current = owner.Viewer();
		check(current &&
		          get_self<ViewerPage>(current)->m_path ==
		              std::filesystem::absolute(notesPath).lexically_normal(),
		      "the combined PDF was not opened");
		break;
	}
	case 3: {
		const auto current = owner.Viewer();
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v)) {
			return;
		}
		check(v.m_sizes.size() > 3, "the combined PDF lost pages");
		status.Text(L"PASS: Combine files saved and opened one PDF");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	default:
		check(false, "unexpected combine step");
	}
	++step;
	ticks = 0;
}

// Export: PNG for every page, JPEG for a range, then the text.
void TabsTest::exportTick(MainWindow& owner) {
	using namespace Microsoft::UI::Xaml::Automation;
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_exporting) {
		return;
	}
	const auto bytes = [](const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file), {});
	};
	// opens the dialog with a format, pages and range; the next tick
	// presses Export once its buttons exist.
	const auto open = [&](int format, const wchar_t* range) {
		v.exportPages();
		const auto dialog = v.m_noteDialog;
		check(dialog && unbox_value<hstring>(dialog.Title()) == L"Export pages",
		      "Export did not open its dialog");
		const auto fields = dialog.Content().as<StackPanel>().Children();
		fields.GetAt(0).as<RadioButtons>().SelectedIndex(format);
		if (range) {
			fields.GetAt(1).as<RadioButtons>().SelectedIndex(1);
			fields.GetAt(2).as<TextBox>().Text(range);
		}
	};
	const auto press = [&] {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& node) -> Button {
			if (const auto button = node.try_as<Button>();
			    button && button.Name() == L"PrimaryButton") {
				return button;
			}
			for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node);
			     ++i) {
				if (const auto button =
				        find(Media::VisualTreeHelper::GetChild(node, i))) {
					return button;
				}
			}
			return nullptr;
		};
		const auto button = v.m_noteDialog ? find(v.m_noteDialog) : nullptr;
		if (!button) {
			check(ticks < 40, "Export had no Export button");
			return false;
		}
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
		return true;
	};
	switch (step) {
	case 0:
		notesPath = makeTempDir("export");
		v.m_exportPicker = [folder = notesPath](bool wantsFolder) {
			const auto path = wantsFolder ? folder : folder / L"pages text.txt";
			return attachmentChoice(hstring(path.wstring()), {});
		};
		open(0, nullptr);
		break;
	case 1:
	case 3:
	case 5:
		if (!press()) {
			return;
		}
		break;
	case 2:
		if (v.m_noteDialog) {
			return;
		}
		for (const auto* name :
		     {L"pages - page 001.png", L"pages - page 002.png",
		      L"pages - page 003.png"}) {
			check(bytes(notesPath / name).rfind("\x89PNG", 0) == 0,
			      "a PNG page is missing or isn't a PNG");
		}
		open(1, L"2");
		break;
	case 4:
		if (v.m_noteDialog) {
			return;
		}
		check(bytes(notesPath / L"pages - page 002.jpg").rfind("\xff\xd8", 0) ==
		              0 &&
		          !std::filesystem::exists(notesPath / L"pages - page 001.jpg"),
		      "the JPEG range did not export page 2 alone");
		open(2, nullptr);
		break;
	case 6: {
		if (v.m_noteDialog) {
			return;
		}
		const auto text = bytes(notesPath / L"pages text.txt");
		check(text.find("Sable test page 1") != std::string::npos &&
		          text.find("Sable test page 2") != std::string::npos &&
		          text.find('\f') != std::string::npos,
		      "the text export lost a page");
		v.close();
		status.Text(L"PASS: Export PNG, JPEG range and text");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	default:
		check(false, "unexpected export step");
	}
	++step;
	ticks = 0;
}
// ReadAloud: Home's tool starts on the current page with the bar and the
// page outline, Next and the end of a page move on and the view follows,
// Pause and Play flip, and Stop puts it all away. The probe build reads the
// words instead of speaking them.
void TabsTest::readAloudTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v)) {
		return;
	}
	const auto visible = [](const UIElement& element) {
		return element.Visibility() == Visibility::Visible;
	};
	const auto invoke = [](const Button& button) {
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	switch (step) {
	case 0:
		check(v.m_sizes.size() >= 3, "the probe needs three pages");
		owner.openTool(L"readaloud");
		check(v.readingAloud() && v.m_readAloudSilent &&
		          visible(v.ReadAloudBar()) && v.m_readAloudPage == 0u,
		      "Read aloud didn't start on the first page");
		break;
	case 1:
		if (v.m_readAloudPending) {
			check(ticks < 40, "the first page's text didn't come");
			return;
		}
		check(!v.m_readAloudWords.empty() &&
		          v.ReadAloudPosition().Text() ==
		              L"Reading page 1 of " + to_hstring(v.m_sizes.size()) &&
		          visible(v.ReadAloudFrame()) &&
		          !v.ReadAloudPrevious().IsEnabled(),
		      "the first page isn't being read");
		invoke(v.ReadAloudNext());
		break;
	case 2:
		if (v.m_readAloudPending) {
			check(ticks < 40, "the second page's text didn't come");
			return;
		}
		check(v.m_readAloudPage == 1u && v.ReadAloudPrevious().IsEnabled() &&
		          std::wstring_view(v.ReadAloudPosition().Text())
		              .starts_with(L"Reading page 2"),
		      "Next didn't move Read aloud to page 2");
		invoke(v.ReadAloudPlay());
		check(v.m_readAloudPaused &&
		          unbox_value<hstring>(
		              ToolTipService::GetToolTip(v.ReadAloudPlay())) == L"Play",
		      "Pause didn't pause");
		// a paused page doesn't move on when it would have ended
		v.readAloudEnded();
		check(v.m_readAloudPage == 1u, "a paused page moved on");
		invoke(v.ReadAloudPlay());
		check(!v.m_readAloudPaused, "Play didn't carry on");
		v.readAloudEnded();
		check(v.m_readAloudPage == 2u, "the end of a page didn't move on");
		break;
	case 3:
		if (v.m_readAloudPending || v.m_current != 2u) {
			check(ticks < 60, "the view didn't follow Read aloud to page 3");
			return;
		}
		invoke(v.ReadAloudStop());
		check(!v.readingAloud() && !visible(v.ReadAloudBar()) &&
		          !visible(v.ReadAloudFrame()),
		      "Stop didn't put Read aloud away");
		v.close();
		status.Text(L"PASS: Read aloud start, Next, Pause, Play, page end "
		            L"and Stop");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected Read aloud step");
	}
	++step;
	ticks = 0;
}
// ToolSurfaces: the command bar, the All tools panel and the quick-tools
// strip start the tools they list, show which one is on, and step aside
// for each other.
void TabsTest::toolSurfacesTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy) {
		return;
	}
	const auto visible = [](const UIElement& element) {
		return element.Visibility() == Visibility::Visible;
	};
	const auto invoke = [](const auto& button) {
		using namespace Microsoft::UI::Xaml::Automation;
		const auto peer =
		    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button);
		if (const auto toggle =
		        peer.GetPattern(Peers::PatternInterface::Toggle)) {
			toggle.as<Provider::IToggleProvider>().Toggle();
		} else {
			peer.GetPattern(Peers::PatternInterface::Invoke)
			    .as<Provider::IInvokeProvider>()
			    .Invoke();
		}
	};

	const auto quick = [&](std::wstring_view tool) {
		for (const auto& [button, name] : v.m_quickToolButtons) {
			if (name == tool) {
				return button;
			}
		}
		return Primitives::ToggleButton{nullptr};
	};
	const auto panelButton = [&](std::wstring_view label) {
		for (const auto& child : v.AllToolsList().Children()) {
			if (const auto button = child.try_as<Button>();
			    button &&
			    Automation::AutomationProperties::GetName(button) == label) {
				return button;
			}
		}
		return Button{nullptr};
	};
	const auto checked = [](const Primitives::ToggleButton& button) {
		const auto state = button.IsChecked();
		return state && state.Value();
	};
	switch (step) {
	case 0: {
		check(visible(v.DocumentBar()) && visible(v.QuickTools()) &&
		          !visible(v.AllToolsPanel()) &&
		          v.m_quickToolButtons.size() == 8u,
		      "the command bar or quick tools are missing");
		invoke(v.AllToolsButton());
		int buttons = 0;
		for (const auto& child : v.AllToolsList().Children()) {
			buttons += child.try_as<Button>() ? 1 : 0;
		}
		check(visible(v.AllToolsPanel()) && buttons == 7 &&
		          v.QuickTools().Margin().Left > 200,
		      "All tools didn't open with every tool");
		invoke(quick(L"hand"));
		check(v.m_cursorTool == ViewerPage::CursorTool::Hand &&
		          checked(quick(L"hand")) && !checked(quick(L"select")),
		      "the hand quick tool didn't take over");
		invoke(quick(L"select"));
		check(v.m_cursorTool == ViewerPage::CursorTool::Select &&
		          checked(quick(L"select")),
		      "the select quick tool didn't come back");
		invoke(quick(L"note"));
		check(v.m_notePlacing &&
		          v.m_placementTool == ViewerPage::PlacementTool::Note &&
		          checked(quick(L"note")),
		      "the sticky note quick tool didn't arm");
		invoke(quick(L"note"));
		check(!v.m_notePlacing && !checked(quick(L"note")),
		      "a second press didn't cancel the note");
		invoke(quick(L"highlight"));
		check(!v.m_noteBusy, "Highlight with nothing selected did something");
		invoke(panelButton(L"Organize pages"));
		check(v.organizing(), "All tools didn't open Organize pages");
		invoke(v.OrganizeDone());
		break;
	}
	case 1:
		if (v.organizing()) {
			check(ticks < 40, "Organize didn't close");
			return;
		}
		invoke(panelButton(L"Read aloud"));
		check(v.readingAloud() && checked(quick(L"readaloud")),
		      "All tools didn't start Read aloud");
		v.stopReadAloud();
		v.updateQuickTools();
		check(!checked(quick(L"readaloud")), "Read aloud stayed checked");
		invoke(v.BarFindButton());
		check(visible(v.FindBar()), "the command bar's Find didn't open");
		v.closeFind();
		invoke(v.AllToolsButton());
		check(!visible(v.AllToolsPanel()) && v.QuickTools().Margin().Left < 20,
		      "All tools didn't close");
		// the menu item toggles like a check box
		invoke(v.BarQuickToolsItem());
		check(!visible(v.QuickTools()), "Hide quick tools didn't hide them");
		invoke(v.BarQuickToolsItem());
		check(visible(v.QuickTools()), "quick tools didn't come back");
		v.close();
		status.Text(L"PASS: command bar, All tools panel and quick tools");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected tool surfaces step");
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
