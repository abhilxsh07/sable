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
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

namespace winrt::Sable::implementation {
namespace {
// layout faults I know about and haven't fixed yet. A check that finds one
// of these reports it and carries on; once the fix lands the check passes,
// and the probe fails until the name comes off this list, so it can't go
// stale.
//   tools-over-page    All tools and the quick strip lie over the pages
//   notice-overlap     notices float over the bars and the pages
//   failed-tab-chrome  a tab that didn't open keeps its command bar
//   min-size           the window shrinks until the rail overlaps itself
constexpr std::array<std::string_view, 4> OPEN_FAULTS = {
    "tools-over-page", "notice-overlap", "failed-tab-chrome", "min-size"};

constexpr std::array<const wchar_t*, 3> SIZES = {L"maximized", L"1366x768",
                                                 L"1024x768"};
constexpr std::array<const wchar_t*, 8> STATES = {
    L"thumbnails", L"outline", L"comments", L"attachments",
    L"All tools",  L"Find",    L"Organize", L"notice"};

std::string text(const wchar_t* value) {
	return to_string(value);
}
} // namespace

// pdf.js and NightPDF had one toolbar and one sidebar, so nothing could
// cover anything else. Sable has more surfaces, so I open each one in
// turn at three window sizes and check that the visible panels don't
// overlap, stay inside the window, and that Organize's tiles fit.
void TabsTest::panelsTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	const auto root = owner.Content().as<FrameworkElement>();

	const auto expect = [&](bool ok, std::string_view fault,
	                        const std::string& what) {
		if (ok) {
			return;
		}
		const bool open =
		    !fault.empty() && std::find(OPEN_FAULTS.begin(), OPEN_FAULTS.end(),
		                                fault) != OPEN_FAULTS.end();
		if (!open) {
			panelsFaults.push_back(
			    fault.empty() ? what : what + " (" + std::string(fault) + ")");
		} else if (std::find(panelsSeen.begin(), panelsSeen.end(), fault) ==
		           panelsSeen.end()) {
			panelsSeen.emplace_back(fault);
		}
	};
	const auto shown = [&](const FrameworkElement& element) {
		if (!element || element.ActualWidth() < 1 ||
		    element.ActualHeight() < 1) {
			return false;
		}
		for (DependencyObject at = element; at;
		     at = Media::VisualTreeHelper::GetParent(at)) {
			if (const auto ui = at.try_as<UIElement>()) {
				if (ui.Visibility() != Visibility::Visible ||
				    ui.Opacity() <= 0) {
					return false;
				}
			}
			if (at == root) {
				return true;
			}
		}
		return false;
	};
	const auto box = [&](const FrameworkElement& element) {
		return element.TransformToVisual(root).TransformBounds(
		    {0, 0, static_cast<float>(element.ActualWidth()),
		     static_cast<float>(element.ActualHeight())});
	};
	const auto overlap = [](const Windows::Foundation::Rect& a,
	                        const Windows::Foundation::Rect& b) {
		const auto w =
		    (std::min)(a.X + a.Width, b.X + b.Width) - (std::max)(a.X, b.X);
		const auto h =
		    (std::min)(a.Y + a.Height, b.Y + b.Height) - (std::max)(a.Y, b.Y);
		return w > 1 && h > 1 ? std::to_string(static_cast<int>(w)) + "x" +
		                            std::to_string(static_cast<int>(h))
		                      : std::string();
	};
	const auto inside = [](const Windows::Foundation::Rect& part,
	                       const Windows::Foundation::Rect& whole) {
		return part.X >= whole.X - 1 && part.Y >= whole.Y - 1 &&
		       part.X + part.Width <= whole.X + whole.Width + 1 &&
		       part.Y + part.Height <= whole.Y + whole.Height + 1;
	};

	// every top-level surface of the viewer, then whatever overlaps on
	// purpose: Organize lies over the disabled pages and the Read Aloud
	// bar floats at their foot.
	const auto measure = [&](const std::string& where,
	                         std::string_view everything = {}) {
		const std::array<std::pair<const char*, FrameworkElement>, 13> parts{{
		    {"command bar", v.DocumentBar()},
		    {"Find", v.FindBar()},
		    {"pages", v.Scroller()},
		    {"Organize", v.OrganizePanel()},
		    {"All tools", v.AllToolsPanel()},
		    {"quick tools", v.QuickTools()},
		    {"Read Aloud bar", v.ReadAloudBar()},
		    {"thumbnails", v.ThumbnailPanel()},
		    {"outline", v.OutlinePanel()},
		    {"attachments", v.AttachmentPanel()},
		    {"comments", v.CommentsPanel()},
		    {"rail", v.Rail()},
		    {"notice", v.Message()},
		}};
		const Windows::Foundation::Rect window{
		    0, 0, static_cast<float>(root.ActualWidth()),
		    static_cast<float>(root.ActualHeight())};
		const auto fault = [&](std::string_view a, std::string_view b) {
			const auto pair = [&](std::string_view x, std::string_view y) {
				return (a == x && b == y) || (a == y && b == x);
			};
			if (pair("Organize", "pages") || pair("Read Aloud bar", "pages")) {
				return std::string_view("allowed");
			}
			if (pair("All tools", "pages") || pair("quick tools", "pages")) {
				return std::string_view("tools-over-page");
			}
			if (a == "notice" || b == "notice") {
				return std::string_view("notice-overlap");
			}
			return everything;
		};
		for (std::size_t i = 0; i < parts.size(); ++i) {
			if (!shown(parts[i].second)) {
				continue;
			}
			const auto a = box(parts[i].second);
			expect(inside(a, window),
			       everything.empty() &&
			               parts[i].first == std::string_view("notice")
			           ? std::string_view("notice-overlap")
			           : everything,
			       where + ": " + parts[i].first + " leaves the window");
			for (std::size_t j = i + 1; j < parts.size(); ++j) {
				if (!shown(parts[j].second)) {
					continue;
				}
				const auto kind = fault(parts[i].first, parts[j].first);
				if (kind == "allowed") {
					continue;
				}
				const auto by = overlap(a, box(parts[j].second));
				expect(by.empty(), kind,
				       where + ": " + parts[i].first + " overlaps " +
				           parts[j].first + " by " + by);
			}
		}
		// the rail's own controls, which collide first when it gets short
		if (shown(v.Rail())) {
			const auto rail = box(v.Rail());
			const std::array<std::pair<const char*, FrameworkElement>, 11>
			    controls{{
			        {"thumbnails button", v.ThumbnailsButton()},
			        {"outline button", v.OutlineButton()},
			        {"comments button", v.CommentsButton()},
			        {"attachments button", v.AttachmentButton()},
			        {"page box", v.PageBox()},
			        {"page total", v.PageTotal()},
			        {"previous page", v.PreviousButton()},
			        {"next page", v.NextButton()},
			        {"zoom in", v.ZoomInButton()},
			        {"zoom menu", v.ZoomButton()},
			        {"zoom out", v.ZoomOutButton()},
			    }};
			for (std::size_t i = 0; i < controls.size(); ++i) {
				if (!shown(controls[i].second)) {
					continue;
				}
				const auto a = box(controls[i].second);
				expect(inside(a, rail), everything,
				       where + ": " + controls[i].first +
				           " sticks out of the rail");
				for (std::size_t j = i + 1; j < controls.size(); ++j) {
					if (!shown(controls[j].second)) {
						continue;
					}
					const auto by = overlap(a, box(controls[j].second));
					expect(by.empty(), everything,
					       where + ": " + controls[i].first + " overlaps " +
					           controls[j].first + " by " + by);
				}
			}
		}
		// Organize's tiles share one cell and stay inside the grid's width
		if (v.organizing() && shown(v.OrganizeGrid())) {
			const auto grid = box(v.OrganizeGrid());
			// and its page count reads like the rail's "of 1,000"
			const std::wstring pages =
			    std::wstring(to_hstring(helpers::grouped(v.m_sizes.size()))) +
			    L" page";
			expect(
			    std::wstring_view(v.OrganizeStatus().Text()).starts_with(pages),
			    everything,
			    where + ": Organize says \"" +
			        to_string(v.OrganizeStatus().Text()) + "\"");
			std::optional<Windows::Foundation::Rect> cell;
			const auto count =
			    (std::min<std::uint32_t>)(v.OrganizeGrid().Items().Size(), 60);
			for (std::uint32_t i = 0; i < count; ++i) {
				const auto tile = v.OrganizeGrid()
				                      .ContainerFromIndex(i)
				                      .try_as<FrameworkElement>();
				if (!tile || !shown(tile)) {
					continue;
				}
				const auto b = box(tile);
				const auto page = std::to_string(i + 1);
				expect(b.X >= grid.X - 1 &&
				           b.X + b.Width <= grid.X + grid.Width + 1,
				       everything,
				       where + ": tile " + page + " is cut off at the side");
				if (!cell) {
					cell = b;
				}
				expect(std::abs(b.Width - cell->Width) < 1 &&
				           std::abs(b.Height - cell->Height) < 1,
				       everything,
				       where + ": tile " + page +
				           " isn't the first one's size");
			}
			expect(cell.has_value(), everything,
			       where + ": Organize laid out no tiles");
		}
	};
	const auto reset = [&] {
		if (v.organizing()) {
			v.closeOrganize();
		}
		if (v.findOpen()) {
			v.closeFind();
		}
		v.setAllToolsOpen(false);
		v.setOutlineOpen(false);
		v.setCommentsOpen(false);
		v.setAttachmentsOpen(false);
		v.setThumbnailsOpen(true);
		v.Message().IsOpen(false);
		v.Message().Visibility(Visibility::Collapsed);
	};
	const auto resize = [&](int size) {
		if (size == 0) {
			owner.m_presenter.Maximize();
		} else {
			owner.m_presenter.Restore();
			owner.AppWindow().Resize(
			    size == 1 ? Windows::Graphics::SizeInt32{1366, 768}
			              : Windows::Graphics::SizeInt32{1024, 768});
		}
	};
	const auto enter = [&](int state) {
		switch (state) {
		case 1:
			v.setOutlineOpen(true);
			break;
		case 2:
			v.setCommentsOpen(true);
			break;
		case 3:
			v.setAttachmentsOpen(true);
			break;
		case 4:
			v.setAllToolsOpen(true);
			break;
		case 5:
			v.openFind();
			break;
		case 6:
			v.openOrganize();
			break;
		case 7:
			v.showNotice(L"A PDF needs at least one page.");
			break;
		default:
			break;
		}
	};
	// every open fault has to have shown up, and nothing else may
	const auto finish = [&](const wchar_t* checked) {
		for (const auto& fault : OPEN_FAULTS) {
			expect(std::find(panelsSeen.begin(), panelsSeen.end(), fault) !=
			           panelsSeen.end(),
			       {},
			       std::string(fault) +
			           " no longer happens: take it off OPEN_FAULTS");
		}
		if (!panelsFaults.empty()) {
			std::string message =
			    "panels: " + std::to_string(panelsFaults.size()) +
			    " problem(s): ";
			for (std::size_t i = 0;
			     i < (std::min<std::size_t>)(panelsFaults.size(), 3); ++i) {
				message += (i ? "; " : "") + panelsFaults[i];
			}
			check(false, message);
		}
		std::wstring open;
		for (const auto& fault : panelsSeen) {
			open += open.empty() ? L"" : L", ";
			open += to_hstring(fault).c_str();
		}
		status.Text(hstring(std::wstring(L"PASS: ") + checked +
		                    L" (still open: " + open + L")"));
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
	};
	constexpr int ROUNDS = static_cast<int>(SIZES.size() * STATES.size());

	switch (step) {
	case 0:
		if (!ready(v)) {
			return;
		}
		first = v.get_strong();
		reset();
		resize(0);
		break;
	case 1: {
		check(&v == first.get(),
		      "panels: the long document's tab lost the selection");
		check(ticks < 115, "panels: the long document didn't settle");
		if (!ready(v) || ticks < 4 || v.m_organizeBusy) {
			return;
		}
		const auto size = panelsRound / static_cast<int>(STATES.size());
		const auto state = panelsRound % static_cast<int>(STATES.size());
		if (state == 0 && ticks < 6) {
			return;
		}
		measure(text(SIZES[static_cast<std::size_t>(size)]) + " " +
		        text(STATES[static_cast<std::size_t>(state)]));
		reset();
		if (++panelsRound < ROUNDS) {
			const auto next = panelsRound % static_cast<int>(STATES.size());
			if (next == 0) {
				resize(panelsRound / static_cast<int>(STATES.size()));
			}
			enter(next);
			ticks = 0;
			return;
		}
		// a short document, where the thumbnail list ends above the
		// panel's foot
		resize(1);
		owner.openDocument(engine, L"tests/fixtures/pages.pdf", 1);
		break;
	}
	case 2:
		check(ticks < 115, "panels: the short document didn't open");
		if (&v == first.get() || !ready(v) || ticks < 4) {
			return;
		}
		panelsShort = &v;
		v.setThumbnailsOpen(true);
		break;
	case 3:
		if (ticks < 4) {
			return;
		}
		expect(box(v.Thumbnails()).Y - box(v.ThumbnailScroller()).Y < 1,
		       "thumbs-top",
		       "short document: the thumbnails don't start at the top");
		measure("1366x768 short document");
		owner.openDocument(engine, L"tests/fixtures/broken.pdf", 1);
		break;
	case 4:
		// the broken file's tab, once it says why it didn't open
		check(ticks < 115, "panels: the broken file's tab never said why");
		if (&v == first.get() || &v == panelsShort || ticks < 4 ||
		    !shown(v.Message())) {
			return;
		}
		expect(!shown(v.DocumentBar()), "failed-tab-chrome",
		       "failed tab: the command bar is still there");
		expect(!shown(v.QuickTools()), "failed-tab-chrome",
		       "failed tab: the quick strip is still there");
		measure("failed tab");
		owner.Tabs().SelectedItem(owner.m_tabs.front().tab);
		owner.m_presenter.Restore();
		// the window stops at its minimum size
		owner.AppWindow().Resize({1, 1});
		break;
	case 5: {
		check(ticks < 115, "panels: the window didn't come back");
		if (&v != first.get() || !ready(v) || ticks < 6) {
			return;
		}
		const auto scale = root.XamlRoot().RasterizationScale();
		const auto outer = owner.AppWindow().Size();
		expect(outer.Width >= 720 * scale - 1 &&
		           outer.Height >= 540 * scale - 1,
		       "min-size",
		       "minimum window is " + std::to_string(outer.Width) + "x" +
		           std::to_string(outer.Height) + " px");
		measure("minimum size", "min-size");
		finish(L"panels at three sizes, a short document, a failed "
		       L"tab and the minimum size");
		return;
	}
	default:
		return;
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
