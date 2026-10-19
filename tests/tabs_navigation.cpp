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
void TabsTest::linkTick(MainWindow& owner) {
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
		      "null XYZ zoom; version " + std::to_string(viewer.m_linkVersion) +
		          "/" + std::to_string(propertiesRequest) + ", direct " +
		          std::to_string(viewer.m_directManipulation) + ", offset " +
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
		status.Text(L"PASS: PDF links, rotated/cropped hit tests, XYZ, Fit, "
		            L"named FitH, FitR, unsupported actions and close");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::outlineTick(MainWindow& owner) {
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
		check(!v.m_outlineNodes[0].IsExpanded() && !v.m_outlineNodes[1] &&
		          v.m_outlineNodes[0].HasUnrealizedChildren(),
		      "outline built closed children");
		v.m_outlineNodes[0].IsExpanded(true);
		check(v.m_outlineNodes[0].Children().Size() == 2 &&
		          !v.m_outlineNodes[3] && !v.m_outlineNodes[2].IsExpanded(),
		      "outline hierarchy children");
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
		check(v.m_current == 2 && v.m_preset == helpers::ZoomPreset::PageWidth,
		      "outline named destination");
		v.m_outlineNodes[2].IsExpanded(true);
		check(v.m_outlineNodes[2].Children().Size() == 1,
		      "outline expansion did not build direct children");
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
		{
			const auto saved = *v.m_outline;
			for (const bool nested : {false, true}) {
				++v.m_outlineVersion;
				v.m_outline = pdf::Outline{};
				for (int i = 0; i < 10000; ++i) {
					v.m_outline->entries.push_back(
					    {"Entry", false, nested && i ? 0 : -1, true});
				}
				v.m_outlineBuilt = false;
				v.showOutline();
				const auto built = std::count_if(
				    v.m_outlineNodes.begin(), v.m_outlineNodes.end(),
				    [](const auto& node) { return bool(node); });
				check(built <= 128,
				      "large outline eagerly built every UI node");
				for (const auto& node : v.m_outlineNodes) {
					check(!node || !node.IsExpanded(),
					      "large outline expanded itself");
				}
				if (nested) {
					check(built == 1, "closed outline built descendants");
					v.m_outlineNodes[0].IsExpanded(true);
					check(v.m_outlineNodes[0].Children().Size() == 128,
					      "wide outline expansion was not bounded");
				}
			}
			++v.m_outlineVersion;
			v.m_outline = saved;
			v.m_outlineBuilt = false;
			v.showOutline();
			v.m_outlineNodes[0].IsExpanded(true);
		}

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

namespace {
float s_findPageTop = 0;
}
void TabsTest::findTick(MainWindow& owner) {
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
		s_findPageTop =
		    v.Scroller().TransformToVisual(current).TransformPoint({0, 0}).Y;
		v.goToPage(2);
		break;
	case 1:
		v.openFind();
		v.FindBox().Text(L"alpha");
		break;
	case 2: {
		// Find is the 40 px command bar: the page area stays put
		const auto top =
		    v.Scroller().TransformToVisual(current).TransformPoint({0, 0}).Y;
		check(std::abs(top - s_findPageTop) < 1,
		      "Find moved the page area down");
		const auto boxAt = v.FindBox()
		                       .TransformToVisual(v.DocumentBar())
		                       .TransformPoint({0, 0});
		check(v.findOpen() && v.FindBox().ActualWidth() > 0,
		      "the Find box didn't open");
		check(boxAt.Y >= 0 && boxAt.Y + v.FindBox().ActualHeight() <=
		                          v.DocumentBar().ActualHeight() + 1,
		      "the Find box isn't in the command bar");
		check(v.FindBox().ActualWidth() <= 340,
		      "the Find box is wider than 320 px");
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
	}
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
		check(v.AllToolsButton().Visibility() ==
		              Microsoft::UI::Xaml::Visibility::Visible &&
		          v.BarActions().Visibility() ==
		              Microsoft::UI::Xaml::Visibility::Visible,
		      "closing Find didn't bring the command bar back");
		v.openFind();
		break;
	case 10:
		check(v.m_findMatches.size() == 4 && v.FindBox().Text() == L"Second",
		      "Find reopen retains query");
		v.FindBox().Text(L"Alpha");
		v.startFind();
		v.closeFind();
		break;
	case 11:
		check(v.m_findMatches.empty(), "closed Find accepted stale callback");
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

void TabsTest::selectionTick(MainWindow& owner) {
	if (step == 10) {
		check(copyCalls == 4, "closed viewer copied text");
		status.Text(L"PASS: text drag/copy, menu/context, zoom, reverse "
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
		const auto& box = viewer.m_text.at(page).text->characters.at(index).box;
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
		check(copiedText == "Alpha" && copyCalls == 1, "selected word copy");
		viewer.Scroller().Focus(FocusState::Programmatic);
		owner.MainMenu().ShowAt(owner.MenuButton());
		check(owner.CopyItem().IsEnabled() && !owner.CutItem().IsEnabled() &&
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
		check(copiedText == "beta\r\nSecond line\r\nSecon" && copyCalls == 3,
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

void TabsTest::longGesturesTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	check(!v.Message().IsOpen(), to_string(v.Message().Message()));
	if (!ready(v) || ticks < 3 || v.m_resettingGesture || v.m_linkPending ||
	    v.m_freezeStage != ViewerPage::FreezeStage::None) {
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
		freezeShows = v.m_freezeShows;
		// a gesture starting again under the freeze frame takes it down
		// and keeps its own scale
		v.finishNativeZoom();
		check(v.m_freezeStage == ViewerPage::FreezeStage::Showing &&
		          v.GestureFreeze().Visibility() == Visibility::Visible &&
		          v.m_resettingGesture &&
		          std::abs(v.Scroller().ZoomFactor() - .6f) < 1e-5,
		      "ending a gesture didn't put the freeze frame up first");
		v.nativeGestureStarted();
		check(v.m_freezeStage == ViewerPage::FreezeStage::None &&
		          !v.m_freezeSurface &&
		          v.GestureFreeze().Visibility() == Visibility::Collapsed &&
		          !v.m_resettingGesture && !v.m_nativeZoom,
		      "a restarted gesture kept the freeze frame");
		v.m_directManipulation = false;
		v.queueNativeZoom();
		break;
	case 5:
		check(std::abs(v.Scroller().ZoomFactor() - 1.f) < 1e-5 &&
		          v.m_current >= 998 && v.m_wanted.end == 1000,
		      "finishing zoom jumped away from the final page");
		check(v.m_freezeShows >= freezeShows + 2 && !v.m_freezeSurface &&
		          v.GestureFreeze().Visibility() == Visibility::Collapsed,
		      "the freeze frame didn't cover the reset or stayed up");
		if (gestureRound < 2) {
			if (gestureRound == 0) {
				const auto wanted = v.m_wanted;
				const auto landing = v.m_landingAt;
				v.m_holdCurrent = true;
				v.m_landingAt = std::pair{0., 0.};
				v.updateVisible();
				check(v.m_wanted == wanted && v.m_device.bitmaps.contains(999),
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
		check(firstInSpread() == 996, "left page did not turn back a spread");
		v.requestLink(click(997), 1);
		break;
	case 8:
		check(firstInSpread() == 998,
		      "right page did not turn forward a spread");
		v.requestLink(click(999), 1);
		break;
	case 9:
		check(firstInSpread() == 998, "last spread moved past the document");
		v.goToPage(0);
		v.requestLink(click(0), -1);
		break;
	case 10:
		check(firstInSpread() == 0, "first spread moved before the document");
		v.setLayout(helpers::ScrollMode::Page, helpers::SpreadMode::Even);
		check(v.spreadClickDirection(click(0, .25)) == -1 &&
		          v.spreadClickDirection(click(0, .75)) == 1,
		      "single cover must expose both directions");
		v.requestLink(click(0, .75), 1);
		break;
	case 11:
		check(firstInSpread() == 1, "even cover did not advance to its spread");
		owner.ClickSpreadsItem().IsChecked(false);
		owner.onClickSpreads({}, {});
		v.requestLink(click(2), 1);
		break;
	case 12:
		check(firstInSpread() == 1, "disabled spread clicks still navigated");
		owner.openDocument(engine, L"tests/fixtures/links.pdf", 1);
		break;
	case 13: {
		v.setLayout(helpers::ScrollMode::Page, helpers::SpreadMode::Odd);
		v.m_clickSpreads = true;
		const auto& rect = v.m_layout->pages()[0];
		v.requestLink({static_cast<float>(rect.x + 80. / 360 * rect.width),
		               static_cast<float>(rect.y + 50. / 540 * rect.height)},
		              1);
		break;
	}
	case 14:
		check(!v.m_launchedUri.empty() && firstInSpread() == 0,
		      "spread clicks swallowed an existing PDF link");
		status.Text(L"PASS: long-document gesture rendering, last-page anchor, "
		            L"live zoom, spread clicks, endpoints and link priority");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::gesturesTick(MainWindow& owner) {
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
		          v.Scroller().HorizontalScrollMode() == ScrollMode::Enabled &&
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
		check(std::abs(v.m_zoom - 1.5) < 1e-5 && v.ZoomText().Text() == L"150%",
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
		v.setLayout(helpers::ScrollMode::Vertical, helpers::SpreadMode::None);
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

void TabsTest::panTick(MainWindow& owner) {
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
		check(v.cursorTool() == Tool::Hand && owner.HandToolItem().IsChecked(),
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
		check(v.m_selection == selection && v.m_zoom == 3 && v.m_rotation == 0,
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
		check(v.m_launchedUri.empty(), "stale tap followed link during drag");
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

void TabsTest::rotationTick(MainWindow& owner) {
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
		check(std::abs(v.m_strip->imageHeight(0) / helpers::THUMBNAIL_WIDTH -
		               shown.height / shown.width) < .01,
		      "rotated thumbnail shape");
		check(std::abs(v.m_zoom -
		               helpers::presetZoom(helpers::ZoomPreset::PageFit, shown,
		                                   v.Scroller().ActualWidth(),
		                                   v.Scroller().ActualHeight())) < .001,
		      "rotation fitted zoom at turn " + std::to_string(rotationRound));
		const auto point = [&v](int index) {
			const auto box =
			    v.viewBox(v.m_text.at(0).text->characters[index].box);
			const auto& rect = v.m_layout->pages()[0];
			return Windows::Foundation::Point{
			    static_cast<float>(rect.x +
			                       (box.left + box.right) * .5 * rect.width),
			    static_cast<float>(rect.y +
			                       (box.top + box.bottom) * .5 * rect.height)};
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
		             std::abs(*mapped.y - (rotationRound == 1 ? .25 : .75)) <
		                 1e-9)
		          : (!mapped.y && mapped.x &&
		             std::abs(*mapped.x - (rotationRound == 0 ? .25 : .75)) <
		                 1e-9),
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
		check(
		    rectangle.x && rectangle.y &&
		        std::abs(*rectangle.x - expected[rotationRound][0]) < 1e-9 &&
		        std::abs(*rectangle.y - expected[rotationRound][1]) < 1e-9 &&
		        std::abs(rectangle.width - expected[rotationRound][2]) < 1e-9 &&
		        std::abs(rectangle.height - expected[rotationRound][3]) < 1e-9,
		    "rotated rectangle destination");
		break;
	}
	case 3: {
		check(copiedText == "Alpha" && copyCalls == rotationRound + 1,
		      "rotated selection copy");
		if (!v.m_device.bitmaps.contains(0)) {
			return;
		}
		const auto previousRaster = v.m_device.bitmaps.at(0).bitmap;
		v.m_cache.clear();
		const auto active = v.m_findActive;
		const auto selection = v.m_selection;
		v.rotateView(1);
		check(v.m_device.bitmaps.contains(0) &&
		          v.m_device.bitmaps.at(0).bitmap.get() == previousRaster.get(),
		      "a quarter turn discarded the displayed raster");
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
		const auto oldGpu = v.m_device.bitmaps.at(0).bitmap;
		const auto oldRotation = v.m_device.bitmaps.at(0).rotation;
		v.m_cache.clear();
		v.rotateView(2);
		check(v.key(0).width == oldKey.width &&
		          v.key(0).height == oldKey.height && v.key(0).rotation == 2,
		      "half-turn raster size");
		const auto gpu = v.m_device.bitmaps.find(0);
		const auto fresh = gpu == v.m_device.bitmaps.end()
		                       ? nullptr
		                       : gpu->second.bitmap.get();
		check(fresh == oldGpu.get() && gpu->second.rotation == oldRotation,
		      "a half turn discarded the previous raster before replacement");
		v.rendered(std::move(old));
		const auto after = v.m_device.bitmaps.find(0);
		check((after == v.m_device.bitmaps.end()
		           ? nullptr
		           : after->second.bitmap.get()) == fresh,
		      "late half-turn raster replaced current pixels");
		check(v.m_sizes[0].width == rotationSize.width &&
		          v.m_sizes[0].height == rotationSize.height,
		      "view changed canonical print size");
		owner.openDocument(engine, L"tests/fixtures/links.pdf", {});
		break;
	}
	case 5:
		check(&v != first.get() && v.m_rotation == 0 && first->m_rotation == 2,
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
		status.Text(L"PASS: view rotation, rasters, text, Find, thumbnails, "
		            L"destinations, tab isolation and late callbacks");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
