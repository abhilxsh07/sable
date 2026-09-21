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
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
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
		check(!v.m_noteDialog && v.ToolPaneTitle().Text() != L"Export",
		      "Export opened during a page edit");
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
		v.requestProperties();
		owner.openTool(L"organize");
		check(v.organizing() && v.OrganizeGrid().Items().Size() == 3,
		      "Organize did not show a tile per page");
		break;
	case 1:
		if (v.m_organizeTile < 3 || v.m_propertiesPending) {
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
		check(v.m_properties && v.m_properties->pages == 3,
		      "couldn't cache the original document properties");
		select({1});
		{
			const auto version = v.m_organizeVersion;
			v.Visibility(Visibility::Collapsed);
			check(!v.m_organizeTimer.IsRunning() &&
			          v.OrganizeGrid().Items().Size() == 0 &&
			          !v.m_organizeRendering && v.m_organizeVersion > version,
			      "hidden Organize kept its tile timer or bitmaps");
			v.Visibility(Visibility::Visible);
			v.activate();
			check(v.organizing() && v.OrganizeGrid().Items().Size() == 3 &&
			          v.organizeSelection() == std::vector<int>({1}),
			      "showing Organize did not restore its tiles and selection");
		}
		select({});
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
		          v.OrganizeRotateRight().IsEnabled() && v.m_pagesDirty,
		      "rotation lost the selected pages");
		check(!v.m_properties && !v.m_propertiesPending,
		      "page edits kept cached document properties");
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
		v.m_readAloudSilent = true;
		v.m_readAloudPage = 2;
		v.m_readAloudPaused = true;
		v.m_readAloudWords = "stale page text";
		check(owner.routeKey(VK_DELETE, 0, false), "Organize ignored Delete");
		break;
	case 4:
		check(v.m_sizes.size() == 2 && sized(0, 842, 595) &&
		          v.OrganizeGrid().Items().Size() == 2,
		      "Delete did not remove the selected page");
		check(v.m_readAloudPage == 1u && v.m_readAloudPaused &&
		          v.m_readAloudWords != "stale page text",
		      "page edits kept stale Read Aloud page numbers or text");
		v.stopReadAloud();
		v.requestProperties();
		select({0, 1});
		invoke(v.OrganizeDelete());
		check(v.Message().IsOpen() &&
		          v.Message().Message() == L"A PDF needs at least one page." &&
		          v.m_sizes.size() == 2,
		      "deleting every page was not refused");
		check(owner.routeKey(VK_ESCAPE, 0, false), "Organize ignored Esc");
		break;
	case 5:
		if (v.m_propertiesPending) {
			return;
		}
		check(v.m_properties && v.m_properties->pages == 2,
		      "Properties did not reload the changed page count");
		check(!v.organizing() && v.m_layout && v.m_layout->pages().size() == 2,
		      "Done did not return to the changed pages");
		check(v.thumbnailsOpen() && v.findOpen() &&
		          v.AllToolsPanel().Visibility() == Visibility::Visible &&
		          v.ThumbnailsButton().IsEnabled(),
		      "Done did not restore the reader surfaces");
		v.closeFind();
		v.setAllToolsOpen(false);
		owner.m_notesChoice = [this, &v] {
			check(v.m_noteDialog &&
			          unbox_value<hstring>(v.m_noteDialog.Title()) ==
			              L"Save your changes?" &&
			          v.m_noteDialog.PrimaryButtonText() == L"Save changes",
			      "page edits were described as notes at close");
			return notesChoice(ContentDialogResult::None);
		};
		{
			const auto close = owner.confirmNotes(current);
			check(close.Status() ==
			              Windows::Foundation::AsyncStatus::Completed &&
			          !close.GetResults() && v.m_pagesDirty,
			      "cancelling close discarded the page edit state");
		}
		owner.m_notesChoice = {};
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
		check(unbox_value<hstring>(v.SaveNotesButton().Content()) ==
		          L"Save changes",
		      "Comments described page edits as notes");
		v.CommentsMoreButton().Flyout().Hide();
		v.openOrganize();
		v.close();
		check(!v.m_organizeTimer.IsRunning() &&
		          v.OrganizeGrid().Items().Size() == 0 &&
		          !v.m_organizeRendering,
		      "closing Organize kept its timer or bitmaps");
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

// Combine: a tab-owned draft, input details, then a saved and opened PDF.
void TabsTest::combineTick(MainWindow& owner) {
	using namespace Microsoft::UI::Xaml::Automation;
	const auto invoke = [](const Button& button) {
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	if (step == 0) {
		owner.setEngine(engine);
		const auto home = get_self<HomePage>(owner.Home());
		check(home->FillSignButton().IsEnabled() &&
		          home->CommentButton().IsEnabled() &&
		          home->ReadAloudButton().IsEnabled(),
		      "Home's tool buttons have the wrong state");
		owner.openDocument(engine, L"tests/fixtures/pages.pdf", {});
		++step;
		ticks = 0;
		return;
	}
	const auto current = owner.Viewer();
	if (!current || !ready(*get_self<ViewerPage>(current))) {
		check(ticks < 100, "the Combine document did not open");
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (v.m_combineLoading) {
		check(ticks < 100, "the Combine input details did not load");
		return;
	}
	switch (step) {
	case 1:
		owner.openTool(L"combine");
		check(!owner.m_combineDialog &&
		          v.FindName(L"CombineList").try_as<ListView>() &&
		          v.ToolPaneTitle().Text() == L"Combine files",
		      "Combine did not open in the tool pane");
		if (v.m_combineLoading) {
			return;
		}
		check(v.m_combineInputs.size() == 1 &&
		          v.m_combineInputs.front().pages == 3 &&
		          !v.CombineButton().IsEnabled(),
		      "Combine did not start with the current file and its page count");
		v.addCombineFiles({L"tests/fixtures/text.pdf",
		                   L"tests/fixtures/password.pdf",
		                   L"tests/fixtures/note_signed.pdf"});
		break;
	case 2: {
		check(v.m_combineInputs.size() == 4 &&
		          v.m_combineInputs[1].pages == 5 &&
		          v.m_combineInputs[2].note == L"Needs a password" &&
		          v.m_combineInputs[3].note == L"(signed PDF)" &&
		          !v.CombineButton().IsEnabled(),
		      "Combine lost page counts or input warnings");
		const auto remove = [&](uint32_t index) {
			const auto row = v.CombineList().Items().GetAt(index).as<Grid>();
			const auto button = row.Children().GetAt(1).as<Button>();
			check(!AutomationProperties::GetName(button).empty(),
			      "the input's Remove button has no name");
			invoke(button);
		};
		remove(3);
		remove(2);
		check(v.CombineTotal().Text() == L"8 pages total" &&
		          v.CombineButton().IsEnabled(),
		      "removing inputs did not update the total and Combine button");
		v.closePane();
		owner.openTool(L"combine");
		check(v.m_combineInputs.size() == 2 &&
		          v.CombineList().Items().Size() == 2,
		      "reopening Combine lost its draft");
		check(v.CombineList().AllowDrop() && v.CombineList().CanDragItems() &&
		          v.CombineList().CanReorderItems() &&
		          v.FindName(L"CombineName").try_as<TextBox>(),
		      "Combine is missing drop, reorder or the result name");
		v.CombineList().SelectedIndex(0);
		v.UpdateLayout();
		const auto row =
		    v.CombineList().ContainerFromIndex(0).try_as<Control>();
		check(row && row.Focus(FocusState::Keyboard) && v.combineHasFocus(),
		      "the Combine input row could not take keyboard focus");
		const auto page = v.m_current;
		check(!owner.routeKey(VK_DOWN, 0, false) && v.m_current == page,
		      "Combine's plain arrow changed the PDF page");
		check(owner.routeKey(VK_DOWN, helpers::SHORTCUT_ALT, false) &&
		          v.m_combineInputs.front().path.filename() == L"text.pdf" &&
		          v.CombineList().SelectedIndex() == 1,
		      "Alt+Down did not move the selected Combine input");
		check(owner.routeKey(VK_UP, helpers::SHORTCUT_ALT, false) &&
		          v.m_combineInputs.front().path.filename() == L"pages.pdf" &&
		          v.CombineList().SelectedIndex() == 0,
		      "Alt+Up did not restore the selected Combine input");
		const auto firstRow = v.CombineList().Items().GetAt(0);
		v.CombineList().Items().RemoveAt(0);
		v.CombineList().Items().Append(firstRow);
		v.reorderCombine();
		check(v.m_combineInputs.front().path.filename() == L"text.pdf",
		      "drag order did not reach the Combine draft");
		v.CombineName().Text(L"My report");
		notesPath = makeTempDir("combine") / L"combined.pdf";
		owner.m_notesPicker = [this, path = hstring(notesPath.wstring())](
		                          hstring name) {
			check(name == L"My report.pdf", "Combine ignored the result name");
			return attachmentChoice(path, {});
		};
		invoke(v.CombineButton());
		break;
	}
	case 3:
		if (owner.m_combining || v.m_path != notesPath) {
			check(ticks < 100, "Combine did not save and open its result");
			return;
		}
		check(!owner.PickerError().IsOpen() &&
		          std::filesystem::exists(notesPath) && v.m_sizes.size() == 8 &&
		          std::abs(v.m_sizes[5].width - 595) < .5,
		      "Combine lost pages or reported an error");
		owner.Tabs().SelectedItem(owner.m_tabs.front().tab);
		break;
	case 4:
		check(v.m_combineInputs.size() == 2 &&
		          v.ToolPaneTitle().Text() == L"Combine files" &&
		          v.CombineTotal().Text() == L"8 pages total" &&
		          v.CombineName().Text() == L"My report",
		      "switching tabs lost the Combine pane or draft");
		status.Text(
		    L"PASS: Combine pane, input details, Remove, total and saved PDF");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
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
	// opens the Export pane with a format and pages (0 all, 1 current,
	// 2 selection, 3 range); the next tick presses its Export button
	const auto open = [&](int format, int which, const wchar_t* range) {
		v.exportPages();
		const auto formats = v.FindName(L"ExportFormat").try_as<RadioButtons>();
		check(!v.m_noteDialog && formats &&
		          v.AllToolsPanel().Visibility() == Visibility::Visible &&
		          v.ToolPaneTitle().Text() == L"Export",
		      "Export did not open in the tool pane");
		formats.SelectedIndex(format);
		check(v.FindName(L"ExportPreviewImage").try_as<Image>() &&
		          v.FindName(L"ExportPreviewText").try_as<TextBlock>(),
		      "Export has no current-page preview");
		check(v.FindName(L"ExportProgress").try_as<ProgressBar>() &&
		          v.FindName(L"ExportProgressText").try_as<TextBlock>() &&
		          v.FindName(L"ExportCancel").try_as<Button>(),
		      "Export has no progress or Cancel");
		check(v.FindName(L"ExportSelection").try_as<RadioButton>() &&
		          v.FindName(L"ExportFolder").try_as<TextBlock>() &&
		          v.FindName(L"ExportFolderChange").try_as<Button>(),
		      "Export is missing Selection or the saved folder line");
		v.FindName(L"ExportPages").as<RadioButtons>().SelectedIndex(which);
		if (range) {
			v.FindName(L"ExportRange").as<TextBox>().Text(range);
		}
	};
	const auto press = [&] {
		const int kind =
		    v.FindName(L"ExportFormat").as<RadioButtons>().SelectedIndex();
		const auto image = v.FindName(L"ExportPreviewImage").as<Image>();
		const auto text = v.FindName(L"ExportPreviewText").as<TextBlock>();
		if (kind == 2 ? text.Text() == L"Loading preview..."
		              : !image.Source()) {
			check(ticks < 80, "the current-page preview did not load");
			return false;
		}
		const auto label = L"Page " + to_hstring(v.m_current + 1) + L": " +
		                   (kind == 2   ? L"Text"
		                    : kind == 1 ? L"JPEG"
		                                : L"PNG") +
		                   L" preview";
		check(v.FindName(L"ExportPreviewLabel").as<TextBlock>().Text() == label,
		      "the preview label did not follow the page and format");
		if (kind == 2) {
			check(std::wstring_view(text.Text())
			                  .find(L"Sable test page " +
			                        std::to_wstring(v.m_current + 1)) !=
			              std::wstring_view::npos &&
			          image.Visibility() == Visibility::Collapsed,
			      "Text preview did not show the current page's first lines");
		} else {
			const auto bitmap =
			    image.Source().as<Media::Imaging::BitmapImage>();
			check(bitmap.PixelWidth() > 0 && bitmap.PixelWidth() <= 240 &&
			          bitmap.PixelHeight() > 0 && bitmap.PixelHeight() <= 220 &&
			          image.Visibility() == Visibility::Visible &&
			          text.Visibility() == Visibility::Collapsed &&
			          AutomationProperties::GetName(image) == label,
			      "image preview was empty, too large or unnamed");
		}
		const auto button = v.FindName(L"ExportButton").try_as<Button>();
		if (!button || !button.IsEnabled()) {
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
		v.setThumbnailsOpen(false);
		v.m_exportSelection.clear();
		v.m_exportPicker = [this, folder = notesPath](bool wantsFolder) {
			check(wantsFolder,
			      "text export asked for a file instead of a folder");
			return attachmentChoice(hstring(folder.wstring()), {});
		};
		open(0, 0, nullptr);
		check(!v.FindName(L"ExportSelection").as<RadioButton>().IsEnabled(),
		      "Selection was enabled without selected thumbnails");
		break;
	case 1:
	case 3:
	case 5:
	case 7:
		if (!press()) {
			return;
		}
		break;
	case 2:
		if (!std::filesystem::exists(notesPath / L"pages - page 003.png")) {
			check(ticks < 200, "the PNG export didn't finish");
			return;
		}
		for (const auto* name :
		     {L"pages - page 001.png", L"pages - page 002.png",
		      L"pages - page 003.png"}) {
			check(bytes(notesPath / name).rfind("\x89PNG", 0) == 0,
			      "a PNG page is missing or isn't a PNG");
		}
		check(std::wstring_view(
		          v.FindName(L"ExportFolder").as<TextBlock>().Text())
		              .find(notesPath.wstring()) != std::wstring_view::npos,
		      "the chosen folder was not shown");
		v.m_exportPicker = [this](bool) {
			check(false, "Export asked for the saved folder again");
			return attachmentChoice({}, {});
		};
		open(1, 3, L"2");
		break;
	case 4:
		if (!std::filesystem::exists(notesPath / L"pages - page 002.jpg")) {
			check(ticks < 200, "the JPEG export didn't finish");
			return;
		}
		check(bytes(notesPath / L"pages - page 002.jpg").rfind("\xff\xd8", 0) ==
		              0 &&
		          !std::filesystem::exists(notesPath / L"pages - page 001.jpg"),
		      "the JPEG range did not export page 2 alone");
		v.goToPage(1);
		open(2, 1, nullptr);
		break;
	case 6: {
		if (!std::filesystem::exists(notesPath / L"pages.txt")) {
			check(ticks < 200, "the text export didn't finish");
			return;
		}
		const auto text = bytes(notesPath / L"pages.txt");
		check(text.find("Sable test page 1") == std::string::npos &&
		          text.find("Sable test page 2") != std::string::npos &&
		          text.find('\f') == std::string::npos,
		      "the text export wasn't the current page alone");
		v.setThumbnailsOpen(true);
		v.openOrganize();
		v.OrganizeGrid().SelectedItems().Clear();
		v.OrganizeGrid().SelectedItems().Append(
		    v.OrganizeGrid().Items().GetAt(0));
		v.closeOrganize();
		open(2, 2, nullptr);
		check(v.FindName(L"ExportSelection").as<RadioButton>().IsEnabled(),
		      "Organize's selection did not survive Done");
		break;
	}
	case 8: {
		const auto text = bytes(notesPath / L"pages.txt");
		check(text.find("Sable test page 1") != std::string::npos &&
		          text.find("Sable test page 2") == std::string::npos,
		      "Selection did not export its page instead of the current page");
		v.m_exportPicker = [](bool) { return attachmentChoice({}, {}); };
		v.changeExportFolder();
		break;
	}
	case 9: {
		check(v.m_exportFolder == notesPath,
		      "cancelling Change lost the saved folder");
		const auto changed = notesPath / L"changed";
		std::filesystem::create_directory(changed);
		v.m_exportPicker = [folder = changed](bool) {
			return attachmentChoice(hstring(folder.wstring()), {});
		};
		v.changeExportFolder();
		break;
	}
	case 10:
		check(v.m_exportFolder == notesPath / L"changed",
		      "Change did not keep the new folder");
		if (!press()) {
			return;
		}
		break;
	case 11:
		check(std::filesystem::exists(notesPath / L"changed" / L"pages.txt"),
		      "Export did not use the changed folder");
		std::filesystem::copy_file(L"tests/fixtures/pages.pdf",
		                           notesPath / L"other.pdf");
		owner.openDocument(engine, notesPath / L"other.pdf", {});
		break;
	case 12:
		if (owner.m_tabs.size() != 2 ||
		    current == owner.m_tabs.front().viewer) {
			check(ticks < 40, "the second export tab did not open");
			return;
		}
		check(v.m_exportFolder.empty(),
		      "another tab inherited the export folder");
		owner.Tabs().SelectedItem(owner.m_tabs.front().tab);
		break;
	case 13:
		check(v.m_exportFolder == notesPath / L"changed" &&
		          v.m_exportSelection == std::vector<int>{0},
		      "switching tabs lost the export folder or selection");
		open(0, 1, nullptr);
		v.FindName(L"ExportFormat").as<RadioButtons>().SelectedIndex(1);
		v.FindName(L"ExportFormat").as<RadioButtons>().SelectedIndex(2);
		v.goToPage(2);
		v.goToPage(0);
		break;
	case 14: {
		const auto text = v.FindName(L"ExportPreviewText").as<TextBlock>();
		if (text.Text() == L"Loading preview...") {
			check(ticks < 80, "the preview did not follow rapid changes");
			return;
		}
		check(std::wstring_view(text.Text()).find(L"Sable test page 1") !=
		              std::wstring_view::npos &&
		          v.FindName(L"ExportPreviewImage").as<Image>().Visibility() ==
		              Visibility::Collapsed,
		      "an older image or text reply replaced the current preview");
		v.FindName(L"ExportFormat").as<RadioButtons>().SelectedIndex(0);
		v.goToPage(1);
		break;
	}
	case 15: {
		const auto image = v.FindName(L"ExportPreviewImage").as<Image>();
		if (!image.Source()) {
			check(ticks < 80, "the image preview did not follow navigation");
			return;
		}
		const auto bitmap = image.Source().as<Media::Imaging::BitmapImage>();
		check(bitmap.PixelWidth() > bitmap.PixelHeight() &&
		          v.FindName(L"ExportPreviewLabel").as<TextBlock>().Text() ==
		              L"Page 2: PNG preview",
		      "navigation kept the previous page's portrait preview");
		owner.openDocument(engine, L"tests/fixtures/long.pdf", {});
		break;
	}
	case 16: {
		if (v.m_sizes.size() != 1000) {
			check(ticks < 80, "the long export document did not open");
			return;
		}
		const auto folder = notesPath / L"cancel";
		std::filesystem::create_directory(folder);
		v.m_exportFolder = folder;
		open(0, 0, nullptr);
		v.FindName(L"ExportResolution").as<ComboBox>().SelectedIndex(0);
		v.FindName(L"ExportProgressText")
		    .as<TextBlock>()
		    .RegisterPropertyChangedCallback(
		        TextBlock::TextProperty(),
		        [this, weak = v.get_weak()](const auto& sender, auto&&) {
			        if (sender.template as<TextBlock>().Text() !=
			            L"2 of 1000") {
				        return;
			        }
			        const auto self = weak.get();
			        if (!self) {
				        return;
			        }
			        check(self->m_exporting && self->pageOperationBusy(),
			              "export progress did not hold the page operation");
			        self->openOrganize();
			        check(!self->organizing(), "Organize opened during export");
			        const auto cancel =
			            self->FindName(L"ExportCancel").as<Button>();
			        check(cancel.IsEnabled(),
			              "Cancel was disabled during export");
			        Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
			            cancel)
			            .GetPattern(Peers::PatternInterface::Invoke)
			            .as<Provider::IInvokeProvider>()
			            .Invoke();
		        });
		v.runExport();
		break;
	}
	case 17: {
		const auto folder = notesPath / L"cancel";
		check(bytes(folder / L"long - page 0001.png").rfind("\x89PNG", 0) ==
		              0 &&
		          bytes(folder / L"long - page 0002.png").rfind("\x89PNG", 0) ==
		              0 &&
		          !std::filesystem::exists(folder / L"long - page 0003.png"),
		      "Cancel did not stop after writing the current page");
		const std::wstring message(v.Message().Message());
		check(message.find(L"Stopped export: 2 files") != std::wstring::npos &&
		          message.find(folder.wstring()) != std::wstring::npos &&
		          v.FindName(L"ExportButton").as<Button>().IsEnabled() &&
		          v.FindName(L"ExportProgressRow")
		                  .as<StackPanel>()
		                  .Visibility() == Visibility::Collapsed,
		      "export completion lost the count, folder or idle state");
		open(2, 0, nullptr);
		v.runExport();
		break;
	}
	case 18: {
		const auto text = bytes(notesPath / L"cancel" / L"long.txt");
		check(text.find("Page 1") != std::string::npos &&
		          text.find("Page 2") != std::string::npos &&
		          text.find("Page 3") == std::string::npos &&
		          std::wstring_view(v.Message().Message())
		                  .find(L"Stopped export: 1 file (2 pages)") !=
		              std::wstring_view::npos,
		      "text cancellation lost the current page or its file count");
		v.close();
		status.Text(
		    L"PASS: Export pane, per-tab folder, Change/Cancel, PNG, "
		    L"JPEG range, text, preview, progress and Cancel at page 2");
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
		// the pages keep the strip's 56 px margin; the pane has a column of
		// its own (Panels checks where it sits)
		check(visible(v.AllToolsPanel()) && buttons == 7 &&
		          v.Scroller().Margin().Left > 55,
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
		check(!visible(v.AllToolsPanel()) && v.Scroller().Margin().Left > 55,
		      "All tools didn't close");
		// the menu item toggles like a check box
		invoke(v.BarQuickToolsItem());
		check(!visible(v.QuickTools()) && v.Scroller().Margin().Left < 1,
		      "Hide quick tools didn't hide them or give the pages its room");
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
