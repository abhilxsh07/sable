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
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "navigation.h"

namespace {
using helpers::NavAction;
using helpers::NavFocus;
using helpers::NavKey;
using helpers::NavModifiers;
using helpers::NavState;

const std::vector<std::string> NO_LABELS;

TEST(PageLabels, IgnoredWhenPdfJsWouldIgnoreThem) {
	EXPECT_TRUE(helpers::pageLabels({}, 0).empty());
	EXPECT_TRUE(helpers::pageLabels({"i", "ii"}, 3).empty());
	EXPECT_TRUE(helpers::pageLabels({"1", "2", "3"}, 3).empty());
	EXPECT_TRUE(helpers::pageLabels({"", "", ""}, 3).empty());
}

TEST(PageLabels, KeptOtherwise) {
	const std::vector<std::string> roman = {"i", "ii", "1", "2"};
	EXPECT_EQ(helpers::pageLabels(roman, 4), roman);
	// app.js only counts until the first label that's neither, so a mix of
	// standard and empty labels is kept too
	const std::vector<std::string> mixed = {"1", "2", ""};
	EXPECT_EQ(helpers::pageLabels(mixed, 3), mixed);
	const std::vector<std::string> late = {"", "", "1"};
	EXPECT_EQ(helpers::pageLabels(late, 3), late);
}

TEST(PageLabelsRules, DroppedWhenCountDiffers) {
	EXPECT_TRUE(helpers::pageLabels({"i", "ii", "iii", "iv"}, 3).empty());
	EXPECT_TRUE(helpers::pageLabels({"i", "ii"}, 3).empty());
	EXPECT_TRUE(helpers::pageLabels({"1", "2", "3"}, 2).empty());
	EXPECT_TRUE(helpers::pageLabels({"A"}, 0).empty());
}

TEST(PageLabelsRules, NoLabelsMeansNone) {
	EXPECT_TRUE(helpers::pageLabels({}, 5).empty());
}

TEST(PageLabelsRules, SinglePage) {
	EXPECT_TRUE(helpers::pageLabels({"1"}, 1).empty());
	EXPECT_TRUE(helpers::pageLabels({""}, 1).empty());
	const std::vector<std::string> cover = {"A"};
	EXPECT_EQ(helpers::pageLabels(cover, 1), cover);
}

TEST(PageLabelsRules, KeptWhenTextDiffersFromNumber) {
	const std::vector<std::string> padded = {"01", "02"};
	EXPECT_EQ(helpers::pageLabels(padded, 2), padded);
	const std::vector<std::string> gap = {"1", "2", "4"};
	EXPECT_EQ(helpers::pageLabels(gap, 3), gap);
	const std::vector<std::string> spaced = {"1", "2", "3", "4", "5", " 6"};
	EXPECT_EQ(helpers::pageLabels(spaced, 6), spaced);
	const std::vector<std::string> zeros = {"1", "2", "3", "4",  "5",  "6",
	                                        "7", "8", "9", "10", "011"};
	EXPECT_EQ(helpers::pageLabels(zeros, 11), zeros);
}

TEST(PageLabelsRules, MixOfNumbersAndEmptyIsKept) {
	const std::vector<std::string> mixed = {"1", "", "3"};
	EXPECT_EQ(helpers::pageLabels(mixed, 3), mixed);
}

TEST(PageLabelsRules, LongNumberRunIsDropped) {
	std::vector<std::string> labels;
	for (int page = 1; page <= 120; ++page) {
		labels.push_back(std::to_string(page));
	}
	EXPECT_TRUE(helpers::pageLabels(labels, 120).empty());
	labels.back() = "120 ";
	EXPECT_EQ(helpers::pageLabels(labels, 120), labels);
}

TEST(PageLabelsRules, AllEmptyIsDropped) {
	EXPECT_TRUE(helpers::pageLabels({"", "", "", "", ""}, 5).empty());
}

TEST(PageText, NumberAndCount) {
	EXPECT_EQ(helpers::pageText(2, 128, NO_LABELS),
	          (helpers::PageText{"3", "of 128"}));
	EXPECT_EQ(helpers::pageText(999, 1000, NO_LABELS),
	          (helpers::PageText{"1000", "of 1,000"}));
	EXPECT_EQ(helpers::pageText(0, 1234567, NO_LABELS),
	          (helpers::PageText{"1", "of 1,234,567"}));
}

// Organize, Comments, Export and Read Aloud print their counts like the
// rail does
TEST(PageText, GroupedCounts) {
	EXPECT_EQ(helpers::grouped(0), "0");
	EXPECT_EQ(helpers::grouped(999), "999");
	EXPECT_EQ(helpers::grouped(1000), "1,000");
	EXPECT_EQ(helpers::grouped(2400), "2,400");
	EXPECT_EQ(helpers::grouped(1234567), "1,234,567");
}

TEST(PageText, Labels) {
	const std::vector<std::string> labels = {"i", "ii", "", "1"};
	EXPECT_EQ(helpers::pageText(1, 4, labels),
	          (helpers::PageText{"ii", "(2 of 4)"}));
	// an empty label leaves the box empty, as in pdf.js
	EXPECT_EQ(helpers::pageText(2, 4, labels),
	          (helpers::PageText{"", "(3 of 4)"}));
	const std::vector<std::string> many(1500, "x");
	EXPECT_EQ(helpers::pageText(1234, 1500, many),
	          (helpers::PageText{"x", "(1,235 of 1,500)"}));
}

TEST(PageFromText, NumbersLikeJavaScript) {
	const auto page = [](const char* text) {
		return helpers::pageFromText(text, NO_LABELS, 20);
	};
	EXPECT_EQ(page("7"), 6u);
	EXPECT_EQ(page(" 7 "), 6u);
	EXPECT_EQ(page("\t7\n"), 6u);
	EXPECT_EQ(page("7.9"), 6u);
	EXPECT_EQ(page("+7"), 6u);
	EXPECT_EQ(page("007"), 6u);
	EXPECT_EQ(page("5."), 4u);
	EXPECT_EQ(page("1e1"), 9u);
	EXPECT_EQ(page("0x0A"), 9u);
	EXPECT_EQ(page("0o7"), 6u);
	EXPECT_EQ(page("0b11"), 2u);
	EXPECT_EQ(page("20"), 19u);
}

TEST(PageFromText, NotAPage) {
	const auto page = [](const char* text) {
		return helpers::pageFromText(text, NO_LABELS, 20);
	};
	for (const char* text :
	     {"",      " ",           "0",    "-0",       "-1",        "21",
	      "abc",   "7e",          ".",    ".5",       "- 5",       "++5",
	      "+0x10", "0x",          "0x1g", "Infinity", "-Infinity", "NaN",
	      "inf",   "99999999999", "1e400"}) {
		EXPECT_EQ(page(text), std::nullopt) << '"' << text << '"';
	}
}

TEST(PageFromText, LabelsFirst) {
	const std::vector<std::string> labels = {"i", "ii", "iii", "1", "2", "ii"};
	const auto page = [&labels](const char* text) {
		return helpers::pageFromText(text, labels, labels.size());
	};
	EXPECT_EQ(page("ii"), 1u); // the first page with that label
	EXPECT_EQ(page("1"), 3u);  // the label, not page 1
	EXPECT_EQ(page("5"), 4u);  // not a label, so a page number
	EXPECT_EQ(page("iv"), std::nullopt);
	EXPECT_EQ(page(" ii"), std::nullopt); // labels have to match exactly
}

// 600 x 300 DIP pages: zoom 0.75 turns points into DIPs one to one. They sit
// at y 10, 321, 632, 943 and x 10.
helpers::PageLayout shortPages() {
	const std::vector<helpers::PageSize> sizes(4, {600, 300});
	return helpers::PageLayout(sizes, 0.75, 0, 11, 10);
}

TEST(CurrentPage, MostVisibleShareWins) {
	const auto layout = shortPages();
	// page 0 shows 110 of 300, page 1 shows 179
	EXPECT_EQ(helpers::currentPage(layout, {0, 200, 620, 300}, 0), 1u);
	// page 0 shows 290, page 1 nothing
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 300}, 3), 0u);
}

TEST(CurrentPage, StaysWhileFullyVisible) {
	const auto layout = shortPages();
	// pages 0-2 are all in view, page 3 only partly
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 1000}, 1), 1u);
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 1000}, 2), 2u);
	// partly in view isn't enough, the first fully visible one wins
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 1000}, 3), 0u);
}

TEST(CurrentPage, WholePixelsAndPercentages) {
	const auto layout = shortPages();
	// a view a fraction past page 0's top still has all of it, as the
	// browser's whole pixels would
	EXPECT_EQ(helpers::currentPage(layout, {0, 10.4, 620, 700}, 0), 0u);
	EXPECT_EQ(helpers::currentPage(layout, {0, 9.6, 620, 700}, 0), 0u);
	// one pixel hidden: 99.7% counts as 99, so page 1 (all of it) takes over
	EXPECT_EQ(helpers::currentPage(layout, {0, 11, 620, 700}, 0), 1u);
}

TEST(CurrentPage, AcrossToo) {
	const auto layout = shortPages();
	// a sixth of each page's width is in view
	EXPECT_EQ(helpers::currentPage(layout, {410, 300, 100, 400}, 0), 1u);
	// nothing in view keeps the current page
	EXPECT_EQ(helpers::currentPage(layout, {700, 0, 100, 400}, 2), 2u);
	EXPECT_EQ(helpers::currentPage(layout, {0, 5000, 620, 400}, 2), 2u);
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 0}, 2), 2u);
}

TEST(CurrentPage, NarrowerThanTheView) {
	const auto layout = shortPages();
	// pages centred in a wider viewport start left of the view's 0
	EXPECT_EQ(helpers::currentPage(layout, {-150, 321, 920, 300}, 0), 1u);
	EXPECT_EQ(helpers::currentPage(layout, {-150, 0, 920, 1000}, 2), 2u);
}

TEST(CurrentPageRules, TieGoesToTheEarlierPage) {
	const auto layout = shortPages();
	// view y 300-331: page 0 shows 10 of 300, page 1 shows 10 of 300 (3%)
	EXPECT_EQ(helpers::currentPage(layout, {0, 300, 620, 31}, 3), 0u);
	// the later tied page being current doesn't help it
	EXPECT_EQ(helpers::currentPage(layout, {0, 300, 620, 31}, 1), 0u);
	// view y 611-642: page 1 shows 10 (611-621), page 2 shows 10 (632-642)
	EXPECT_EQ(helpers::currentPage(layout, {0, 611, 620, 31}, 3), 1u);
	EXPECT_EQ(helpers::currentPage(layout, {0, 611, 620, 31}, 2), 1u);
}

TEST(CurrentPageRules, FullyVisibleLaterPageStaysCurrent) {
	const auto layout = shortPages();
	// view y 0-700: pages 0 and 1 are whole, page 2 shows 68 of 300
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 700}, 1), 1u);
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 700}, 0), 0u);
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 700}, 3), 0u);
}

TEST(CurrentPageRules, TouchingTheEdgeDoesNotCount) {
	const auto layout = shortPages();
	// view y 310-321: page 0 ends at 310, page 1 starts at 321
	EXPECT_EQ(helpers::currentPage(layout, {0, 310, 620, 11}, 3), 3u);
	// view y 310-322: page 1 now overlaps by one pixel (0%), and counts
	EXPECT_EQ(helpers::currentPage(layout, {0, 310, 620, 12}, 3), 1u);
	// view x 610-710: pages end at x 610
	EXPECT_EQ(helpers::currentPage(layout, {610, 0, 100, 1000}, 2), 2u);
	// view x -100-10: pages start at x 10
	EXPECT_EQ(helpers::currentPage(layout, {-100, 0, 110, 1000}, 1), 1u);
}

TEST(CurrentPageRules, NothingInViewKeepsTheCurrentPage) {
	const auto layout = shortPages();
	// view y -500 to -100 is above every page
	EXPECT_EQ(helpers::currentPage(layout, {0, -500, 620, 400}, 1), 1u);
	EXPECT_EQ(helpers::currentPage(layout, {0, -500, 620, 400}, 99), 99u);
	// zero width view
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 0, 1000}, 3), 3u);
}

TEST(CurrentPageRules, EmptyLayoutKeepsTheCurrentPage) {
	const std::vector<helpers::PageSize> none;
	const helpers::PageLayout layout(none, 0.75, 0, 11, 10);
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 1000}, 0), 0u);
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 1000}, 5), 5u);
}

TEST(CurrentPageRules, CurrentOutOfRange) {
	const auto layout = shortPages();
	// pages 0-2 are whole in view; the first one wins
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 1000}, 4), 0u);
	EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 1000}, 7), 0u);
	// view y 321-621 shows only page 1
	EXPECT_EQ(helpers::currentPage(layout, {0, 321, 620, 300}, 7), 1u);
}

// zoom 0.75 makes one point one DIP in the layouts below
TEST(CurrentPageRules, ShareCountsNotPixels) {
	const std::vector<helpers::PageSize> sizes{{600, 300}, {600, 100}};
	// page 0 at y 10-310, page 1 at y 321-421
	const helpers::PageLayout layout(sizes, 0.75, 0, 11, 10);
	// view y 160-421: page 0 shows 150 of 300 (50%), page 1 all of 100 (100%)
	EXPECT_EQ(helpers::currentPage(layout, {0, 160, 620, 261}, 0), 1u);
}

TEST(CurrentPageRules, FractionIsDroppedBeforeComparing) {
	const std::vector<helpers::PageSize> sizes(3, {600, 1000});
	// pages at y 10-1010, 1021-2021, 2032-3032
	const helpers::PageLayout layout(sizes, 0.75, 0, 11, 10);
	// view y 419-1620: page 0 shows 591 of 1000 (59.1%), page 1 shows 599
	// (59.9%); both are 59, so the earlier page wins
	EXPECT_EQ(helpers::currentPage(layout, {0, 419, 620, 1201}, 9), 0u);
}

TEST(CurrentPageRules, RoundingDecidesBetweenFullAndNinetyNine) {
	const std::vector<helpers::PageSize> sizes(2, {600, 300});
	// page 0 at y 10.4-310.4 (10-310), page 1 at y 321.4-621.4 (321-621)
	const helpers::PageLayout layout(sizes, 0.75, 0, 11, 10.4);
	// view y 10.4-710.4 rounds to 10-710: page 0 is whole, so it stays
	EXPECT_EQ(helpers::currentPage(layout, {0, 10.4, 620, 700}, 0), 0u);
	// view y 10.6-710.6 rounds to 11-711: page 0 shows 299 of 300 (99%),
	// page 1 is whole
	EXPECT_EQ(helpers::currentPage(layout, {0, 10.6, 620, 700}, 0), 1u);
}

TEST(CurrentPageRules, PagesWithoutSizeAfterRoundingNeverCount) {
	{
		// page 1 is 0.2 high: y 321-321.2 rounds to 321-321
		const std::vector<helpers::PageSize> sizes{
		    {600, 300}, {600, 0.2}, {600, 300}};
		const helpers::PageLayout layout(sizes, 0.75, 0, 11, 10);
		// page 2 at y 332.2-632.2 (332-632)
		// view y 300-330: page 0 shows 10 of 300 (3%), nothing else counts
		EXPECT_EQ(helpers::currentPage(layout, {0, 300, 620, 30}, 2), 0u);
		// view y 315-325 meets only the empty page
		EXPECT_EQ(helpers::currentPage(layout, {0, 315, 620, 10}, 2), 2u);
	}
	{
		// page 0 is 0.2 wide: x 10-10.2 rounds to 10-10
		const std::vector<helpers::PageSize> sizes{{0.2, 300}, {600, 300}};
		const helpers::PageLayout layout(sizes, 0.75, 0, 11, 10);
		// page 1 is whole and the only page that counts
		EXPECT_EQ(helpers::currentPage(layout, {0, 0, 620, 1000}, 0), 1u);
	}
}

TEST(CurrentPageRules, EachEdgeIsRoundedOnItsOwn) {
	// both pages sit at x 10.4-610.6, which rounds to 10-611: 601 wide,
	// where rounding the width alone would give 600. Same-width pages are
	// never shifted by the layout's centring.
	const std::vector<helpers::PageSize> sizes(2, {600.2, 300});
	const helpers::PageLayout layout(sizes, 0.75, 0, 11, 10.4);
	// view x 10-610 leaves one pixel of each page out (600 of 601, 99%), so
	// the current page doesn't stay and the earlier one wins
	EXPECT_EQ(helpers::currentPage(layout, {10, 10, 600, 700}, 1), 0u);
	// view x 10.4-610.8 rounds to 10-611: both pages are whole, so the
	// current one stays, and with none current the earlier one wins
	EXPECT_EQ(helpers::currentPage(layout, {10.4, 10, 600.4, 700}, 1), 1u);
	EXPECT_EQ(helpers::currentPage(layout, {10.4, 10, 600.4, 700}, 5), 0u);
}

NavAction act(NavKey key, NavModifiers modifiers = {},
              NavFocus focus = NavFocus::Viewer, NavState state = {2, 5}) {
	return helpers::navAction(key, modifiers, focus, state);
}

constexpr NavModifiers CTRL{true, false, false};
constexpr NavModifiers CTRL_SHIFT{true, false, true};
constexpr NavModifiers CTRL_ALT{true, true, false};
constexpr NavModifiers SHIFT{false, false, true};
constexpr NavModifiers ALT{false, true, false};

TEST(NavAction, CtrlUpAndDown) {
	EXPECT_EQ(act(NavKey::Up, CTRL), NavAction::First);
	EXPECT_EQ(act(NavKey::Down, CTRL), NavAction::Last);
	EXPECT_EQ(act(NavKey::Up, CTRL_SHIFT), NavAction::First);
	// even from the page box
	EXPECT_EQ(act(NavKey::Down, CTRL, NavFocus::TextBox), NavAction::Last);
	// already there: left to the scroller
	EXPECT_EQ(act(NavKey::Up, CTRL, NavFocus::Viewer, {0, 5}), NavAction::None);
	EXPECT_EQ(act(NavKey::Down, CTRL, NavFocus::Viewer, {4, 5}),
	          NavAction::None);
	EXPECT_EQ(act(NavKey::Home, CTRL), NavAction::None);
	EXPECT_EQ(act(NavKey::J, CTRL), NavAction::None);
}

TEST(NavAction, CtrlAltG) {
	EXPECT_EQ(act(NavKey::G, CTRL_ALT), NavAction::FocusPageBox);
	EXPECT_EQ(act(NavKey::G, CTRL_ALT, NavFocus::TextBox),
	          NavAction::FocusPageBox);
	EXPECT_EQ(act(NavKey::G, {true, true, true}), NavAction::None);
	EXPECT_EQ(act(NavKey::G), NavAction::None);
	EXPECT_EQ(act(NavKey::Up, CTRL_ALT), NavAction::None);
}

TEST(NavAction, LeftToTextBoxesAndButtons) {
	EXPECT_EQ(act(NavKey::Home, {}, NavFocus::TextBox), NavAction::None);
	EXPECT_EQ(act(NavKey::J, {}, NavFocus::TextBox), NavAction::None);
	EXPECT_EQ(act(NavKey::Left, {}, NavFocus::TextBox), NavAction::None);
	const NavState pageFit{2, 5, true, false, true};
	EXPECT_EQ(act(NavKey::Space, {}, NavFocus::Button, pageFit),
	          NavAction::None);
	EXPECT_EQ(act(NavKey::Enter, {}, NavFocus::Button, pageFit),
	          NavAction::None);
	EXPECT_EQ(act(NavKey::Home, {}, NavFocus::Button), NavAction::First);
	EXPECT_EQ(act(NavKey::N, {}, NavFocus::Button), NavAction::Next);
}

TEST(NavAction, ScrollingKeysTurnPagesWithNothingToScroll) {
	const NavState scrolls{2, 5, true, true, false};
	const NavState fits{2, 5, false, false, false};
	for (const auto key : {NavKey::Up, NavKey::PageUp}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, scrolls), NavAction::None);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, fits), NavAction::Previous);
	}
	for (const auto key : {NavKey::Down, NavKey::PageDown}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, scrolls), NavAction::None);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, fits), NavAction::Next);
	}
	EXPECT_EQ(act(NavKey::Left, {}, NavFocus::Viewer, scrolls),
	          NavAction::None);
	EXPECT_EQ(act(NavKey::Left, {}, NavFocus::Viewer, fits),
	          NavAction::Previous);
	EXPECT_EQ(act(NavKey::Right, {}, NavFocus::Viewer, scrolls),
	          NavAction::None);
	EXPECT_EQ(act(NavKey::Right, {}, NavFocus::Viewer, fits), NavAction::Next);
	// only the horizontal scrollbar matters for left and right
	const NavState tall{2, 5, true, false, false};
	EXPECT_EQ(act(NavKey::Right, {}, NavFocus::Viewer, tall), NavAction::Next);
	EXPECT_EQ(act(NavKey::Down, {}, NavFocus::Viewer, tall), NavAction::None);
}

TEST(NavAction, PageFitTurnsPages) {
	const NavState pageFit{2, 5, true, true, true};
	const auto at = [&pageFit](NavKey key, NavModifiers modifiers = {}) {
		return act(key, modifiers, NavFocus::Viewer, pageFit);
	};
	EXPECT_EQ(at(NavKey::PageDown), NavAction::Next);
	EXPECT_EQ(at(NavKey::Up), NavAction::Previous);
	EXPECT_EQ(at(NavKey::Space), NavAction::Next);
	EXPECT_EQ(at(NavKey::Enter), NavAction::Next);
	EXPECT_EQ(at(NavKey::Backspace), NavAction::Previous);
	EXPECT_EQ(at(NavKey::Space, SHIFT), NavAction::Previous);
	EXPECT_EQ(at(NavKey::Enter, SHIFT), NavAction::Previous);
}

TEST(NavAction, SpaceEnterAndBackspaceScrollOtherwise) {
	const NavState fits{2, 5, false, false, false};
	for (const auto key : {NavKey::Space, NavKey::Enter, NavKey::Backspace}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, fits), NavAction::None);
		EXPECT_EQ(act(key, SHIFT, NavFocus::Viewer, fits), NavAction::None);
	}
}

TEST(NavAction, Letters) {
	const NavState scrolls{2, 5, true, true, false};
	for (const auto key : {NavKey::J, NavKey::N}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, scrolls), NavAction::Next);
		EXPECT_EQ(act(key, SHIFT, NavFocus::Viewer, scrolls), NavAction::None);
	}
	for (const auto key : {NavKey::K, NavKey::P}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, scrolls), NavAction::Previous);
	}
	// taken even with nowhere to go
	EXPECT_EQ(act(NavKey::J, {}, NavFocus::Viewer, {4, 5}), NavAction::Next);
	EXPECT_EQ(act(NavKey::K, {}, NavFocus::Viewer, {0, 5}),
	          NavAction::Previous);
}

TEST(NavAction, HomeAndEnd) {
	EXPECT_EQ(act(NavKey::Home), NavAction::First);
	EXPECT_EQ(act(NavKey::End), NavAction::Last);
	// on the first or last page they're left to the scroller
	EXPECT_EQ(act(NavKey::Home, {}, NavFocus::Viewer, {0, 5}), NavAction::None);
	EXPECT_EQ(act(NavKey::End, {}, NavFocus::Viewer, {4, 5}), NavAction::None);
	EXPECT_EQ(act(NavKey::End, SHIFT), NavAction::None);
}

TEST(NavAction, F4TogglesTheSidebar) {
	EXPECT_EQ(act(NavKey::F4), NavAction::ToggleSidebar);
	EXPECT_EQ(act(NavKey::F4, {}, NavFocus::Button), NavAction::ToggleSidebar);
	// not from the page box, and not with Shift, Ctrl or Alt
	EXPECT_EQ(act(NavKey::F4, {}, NavFocus::TextBox), NavAction::None);
	EXPECT_EQ(act(NavKey::F4, SHIFT), NavAction::None);
	EXPECT_EQ(act(NavKey::F4, CTRL), NavAction::None);
	EXPECT_EQ(act(NavKey::F4, ALT), NavAction::None);
}

TEST(NavAction, RotationLeavesTextAndModifiedKeysAlone) {
	EXPECT_EQ(act(NavKey::R), NavAction::RotateClockwise);
	EXPECT_EQ(act(NavKey::R, SHIFT), NavAction::RotateCounterClockwise);
	EXPECT_EQ(act(NavKey::R, {}, NavFocus::Button), NavAction::RotateClockwise);
	for (const auto modifiers : {NavModifiers{}, SHIFT, CTRL, ALT}) {
		EXPECT_EQ(act(NavKey::R, modifiers, NavFocus::TextBox),
		          NavAction::None);
	}
	EXPECT_EQ(act(NavKey::R, CTRL), NavAction::None);
	EXPECT_EQ(act(NavKey::R, ALT), NavAction::None);
	EXPECT_EQ(act(NavKey::R, {true, true, true}), NavAction::None);
}

TEST(PageName, LabelOrGroupedNumber) {
	EXPECT_EQ(helpers::pageName(2, NO_LABELS), "3");
	EXPECT_EQ(helpers::pageName(999, NO_LABELS), "1,000");
	const std::vector<std::string> labels = {"i", "", "1"};
	EXPECT_EQ(helpers::pageName(0, labels), "i");
	EXPECT_EQ(helpers::pageName(1, labels), "");
}

TEST(NavAction, OtherKeysAndAlt) {
	EXPECT_EQ(act(NavKey::Other), NavAction::None);
	EXPECT_EQ(act(NavKey::G), NavAction::None);
	EXPECT_EQ(act(NavKey::Left, ALT), NavAction::None);
	EXPECT_EQ(act(NavKey::Home, ALT), NavAction::None);
}
TEST(NavAction, CursorToolsUseUnmodifiedKeysOutsideTextInputs) {
	EXPECT_EQ(act(NavKey::H), NavAction::HandTool);
	EXPECT_EQ(act(NavKey::S), NavAction::SelectTool);
	EXPECT_EQ(act(NavKey::H, {}, NavFocus::Button), NavAction::HandTool);
	for (const auto key : {NavKey::H, NavKey::S}) {
		EXPECT_EQ(act(key, {}, NavFocus::TextBox), NavAction::None);
		for (const auto modifiers : {SHIFT, CTRL, ALT, CTRL_SHIFT, CTRL_ALT}) {
			EXPECT_EQ(act(key, modifiers), NavAction::None);
		}
	}
}
TEST(NavAction, WindowsChordsStayWithTheShell) {
	for (const auto key : {NavKey::R, NavKey::H, NavKey::S, NavKey::Home,
	                       NavKey::Down, NavKey::F4, NavKey::G}) {
		EXPECT_EQ(act(key, {false, false, false, true}), NavAction::None);
		EXPECT_EQ(act(key, {true, true, false, true}), NavAction::None);
	}
}
// Tests for the rules in the keys description, one rule per group.
constexpr NavKey ALL_KEYS[] = {
    NavKey::Other, NavKey::Up,     NavKey::Down,     NavKey::Left,
    NavKey::Right, NavKey::PageUp, NavKey::PageDown, NavKey::Home,
    NavKey::End,   NavKey::Space,  NavKey::Enter,    NavKey::Backspace,
    NavKey::G,     NavKey::J,      NavKey::K,        NavKey::N,
    NavKey::P,     NavKey::R,      NavKey::H,        NavKey::S,
    NavKey::F4,
};
constexpr NavFocus ALL_FOCUSES[] = {NavFocus::Viewer, NavFocus::Button,
                                    NavFocus::TextBox};
constexpr NavModifiers CTRL_ALT_SHIFT{true, true, true};
constexpr NavModifiers ALT_SHIFT{false, true, true};
// bit 0 ctrl, bit 1 alt, bit 2 shift, bit 3 Windows key
constexpr NavModifiers modifiersFrom(unsigned bits) {
	return {(bits & 1u) != 0, (bits & 2u) != 0, (bits & 4u) != 0,
	        (bits & 8u) != 0};
}
// bit 0 vertical scrollbar, bit 1 horizontal scrollbar, bit 2 Page Fit
constexpr NavState stateFrom(std::size_t page, std::size_t count,
                             unsigned bits) {
	return {page, count, (bits & 1u) != 0, (bits & 2u) != 0, (bits & 4u) != 0};
}
constexpr unsigned STATE_COMBOS = 8;
constexpr unsigned MODIFIER_COMBOS = 16;
constexpr NavState FIRST_PAGE{0, 5};
constexpr NavState LAST_PAGE{4, 5};
constexpr NavState NO_PAGES{0, 0};
constexpr NavState ONLY_PAGE{0, 1};
// nothing to scroll in either direction, and the zoom is not Page Fit
constexpr NavState NOTHING_TO_SCROLL{2, 5, false, false, false};
constexpr NavState PAGE_FIT{2, 5, false, false, true};

// rule 1: the Windows key leaves everything to the shell
TEST(NavActionRules, WindowsKeyBeatsEverything) {
	for (const auto key : ALL_KEYS) {
		for (const auto focus : ALL_FOCUSES) {
			for (unsigned m = 0; m < MODIFIER_COMBOS; ++m) {
				const NavModifiers modifiers = modifiersFrom(m | 8u);
				for (unsigned s = 0; s < STATE_COMBOS; ++s) {
					EXPECT_EQ(act(key, modifiers, focus, stateFrom(2, 5, s)),
					          NavAction::None);
				}
			}
		}
	}
	// Ctrl+Up in a text field, from a page that is not the first
	EXPECT_EQ(act(NavKey::Up, {true, false, false, true}, NavFocus::TextBox),
	          NavAction::None);
}

// rule 2: Ctrl+Up and Ctrl+Down jump to the ends, from any focus
TEST(NavActionRules, CtrlUpAndDownFromAnyFocus) {
	for (const auto focus : ALL_FOCUSES) {
		for (const auto modifiers : {CTRL, CTRL_SHIFT}) {
			EXPECT_EQ(act(NavKey::Up, modifiers, focus), NavAction::First);
			EXPECT_EQ(act(NavKey::Down, modifiers, focus), NavAction::Last);
			// from the other end
			EXPECT_EQ(act(NavKey::Up, modifiers, focus, LAST_PAGE),
			          NavAction::First);
			EXPECT_EQ(act(NavKey::Down, modifiers, focus, FIRST_PAGE),
			          NavAction::Last);
		}
	}
}
TEST(NavActionRules, CtrlUpAndDownDoNothingAtTheEnds) {
	for (const auto focus : ALL_FOCUSES) {
		for (const auto modifiers : {CTRL, CTRL_SHIFT}) {
			EXPECT_EQ(act(NavKey::Up, modifiers, focus, FIRST_PAGE),
			          NavAction::None);
			EXPECT_EQ(act(NavKey::Down, modifiers, focus, LAST_PAGE),
			          NavAction::None);
		}
	}
	// next to the end is not the end
	EXPECT_EQ(act(NavKey::Down, CTRL, NavFocus::Viewer, {3, 5}),
	          NavAction::Last);
	EXPECT_EQ(act(NavKey::Up, CTRL, NavFocus::Viewer, {1, 5}),
	          NavAction::First);
	// a one-page document and an empty one are at both ends
	for (const auto& state : {ONLY_PAGE, NO_PAGES}) {
		EXPECT_EQ(act(NavKey::Up, CTRL, NavFocus::Viewer, state),
		          NavAction::None);
		EXPECT_EQ(act(NavKey::Down, CTRL, NavFocus::Viewer, state),
		          NavAction::None);
	}
}
TEST(NavActionRules, CtrlUpAndDownIgnoreScrollbarsAndPageFit) {
	for (unsigned s = 0; s < STATE_COMBOS; ++s) {
		const NavState state = stateFrom(2, 5, s);
		EXPECT_EQ(act(NavKey::Up, CTRL, NavFocus::Viewer, state),
		          NavAction::First);
		EXPECT_EQ(act(NavKey::Down, CTRL, NavFocus::Viewer, state),
		          NavAction::Last);
	}
}
TEST(NavActionRules, CtrlWithAnyOtherKeyDoesNothing) {
	for (const auto key : ALL_KEYS) {
		if (key == NavKey::Up || key == NavKey::Down) {
			continue;
		}
		for (const auto focus : ALL_FOCUSES) {
			for (const auto modifiers : {CTRL, CTRL_SHIFT}) {
				for (unsigned s = 0; s < STATE_COMBOS; ++s) {
					EXPECT_EQ(act(key, modifiers, focus, stateFrom(2, 5, s)),
					          NavAction::None);
				}
			}
		}
	}
}

// rule 3: Ctrl+Alt+G picks the page box, everything else with both is idle
TEST(NavActionRules, CtrlAltGSelectsThePageBoxFromAnyFocus) {
	for (const auto focus : ALL_FOCUSES) {
		for (unsigned s = 0; s < STATE_COMBOS; ++s) {
			EXPECT_EQ(act(NavKey::G, CTRL_ALT, focus, stateFrom(2, 5, s)),
			          NavAction::FocusPageBox);
		}
		EXPECT_EQ(act(NavKey::G, CTRL_ALT, focus, FIRST_PAGE),
		          NavAction::FocusPageBox);
		EXPECT_EQ(act(NavKey::G, CTRL_ALT, focus, NO_PAGES),
		          NavAction::FocusPageBox);
		EXPECT_EQ(act(NavKey::G, CTRL_ALT_SHIFT, focus), NavAction::None);
	}
}
TEST(NavActionRules, CtrlAltWithAnyOtherKeyDoesNothing) {
	for (const auto key : ALL_KEYS) {
		if (key == NavKey::G) {
			continue;
		}
		for (const auto focus : ALL_FOCUSES) {
			EXPECT_EQ(act(key, CTRL_ALT, focus), NavAction::None);
			EXPECT_EQ(act(key, CTRL_ALT_SHIFT, focus), NavAction::None);
			EXPECT_EQ(act(key, CTRL_ALT, focus, PAGE_FIT), NavAction::None);
		}
	}
}

// rule 4: text fields keep every key
TEST(NavActionRules, TextFieldKeepsEveryUnmodifiedKey) {
	for (const auto key : ALL_KEYS) {
		for (unsigned s = 0; s < STATE_COMBOS; ++s) {
			EXPECT_EQ(act(key, {}, NavFocus::TextBox, stateFrom(2, 5, s)),
			          NavAction::None);
		}
		EXPECT_EQ(act(key, {}, NavFocus::TextBox, FIRST_PAGE), NavAction::None);
		EXPECT_EQ(act(key, {}, NavFocus::TextBox, LAST_PAGE), NavAction::None);
	}
	EXPECT_EQ(act(NavKey::Backspace, {}, NavFocus::TextBox, PAGE_FIT),
	          NavAction::None);
	EXPECT_EQ(act(NavKey::Up, {}, NavFocus::TextBox, NOTHING_TO_SCROLL),
	          NavAction::None);
}
TEST(NavActionRules, TextFieldKeepsShiftedAndAltKeys) {
	for (const auto key : ALL_KEYS) {
		for (const auto modifiers : {SHIFT, ALT, ALT_SHIFT}) {
			for (unsigned s = 0; s < STATE_COMBOS; ++s) {
				EXPECT_EQ(
				    act(key, modifiers, NavFocus::TextBox, stateFrom(2, 5, s)),
				    NavAction::None);
			}
		}
	}
}
TEST(NavActionRules, ButtonKeepsOnlyEnterAndSpace) {
	for (const auto key : {NavKey::Enter, NavKey::Space}) {
		for (unsigned s = 0; s < STATE_COMBOS; ++s) {
			const NavState state = stateFrom(2, 5, s);
			EXPECT_EQ(act(key, {}, NavFocus::Button, state), NavAction::None);
			EXPECT_EQ(act(key, SHIFT, NavFocus::Button, state),
			          NavAction::None);
		}
	}
	// the same keys do turn pages from the viewer
	EXPECT_EQ(act(NavKey::Enter, {}, NavFocus::Viewer, PAGE_FIT),
	          NavAction::Next);
	EXPECT_EQ(act(NavKey::Space, SHIFT, NavFocus::Viewer, PAGE_FIT),
	          NavAction::Previous);
}
TEST(NavActionRules, ButtonFocusActsLikeTheViewerForOtherKeys) {
	const NavState scrolls{2, 5, true, true, false};
	EXPECT_EQ(act(NavKey::K, {}, NavFocus::Button, scrolls),
	          NavAction::Previous);
	EXPECT_EQ(act(NavKey::P, {}, NavFocus::Button, scrolls),
	          NavAction::Previous);
	EXPECT_EQ(act(NavKey::J, {}, NavFocus::Button, scrolls), NavAction::Next);
	EXPECT_EQ(act(NavKey::N, {}, NavFocus::Button, scrolls), NavAction::Next);
	EXPECT_EQ(act(NavKey::R, {}, NavFocus::Button, scrolls),
	          NavAction::RotateClockwise);
	EXPECT_EQ(act(NavKey::R, SHIFT, NavFocus::Button, scrolls),
	          NavAction::RotateCounterClockwise);
	EXPECT_EQ(act(NavKey::H, {}, NavFocus::Button, scrolls),
	          NavAction::HandTool);
	EXPECT_EQ(act(NavKey::S, {}, NavFocus::Button, scrolls),
	          NavAction::SelectTool);
	EXPECT_EQ(act(NavKey::F4, {}, NavFocus::Button, scrolls),
	          NavAction::ToggleSidebar);
	EXPECT_EQ(act(NavKey::Home, {}, NavFocus::Button, scrolls),
	          NavAction::First);
	EXPECT_EQ(act(NavKey::End, {}, NavFocus::Button, scrolls), NavAction::Last);
	EXPECT_EQ(act(NavKey::Up, {}, NavFocus::Button, NOTHING_TO_SCROLL),
	          NavAction::Previous);
	EXPECT_EQ(act(NavKey::Right, {}, NavFocus::Button, scrolls),
	          NavAction::None);
	EXPECT_EQ(act(NavKey::Backspace, {}, NavFocus::Button, PAGE_FIT),
	          NavAction::Previous);
	EXPECT_EQ(act(NavKey::G, {}, NavFocus::Button), NavAction::None);
	EXPECT_EQ(act(NavKey::Other, {}, NavFocus::Button), NavAction::None);
}
TEST(NavActionRules, ButtonFocusStillChecksTheEnds) {
	EXPECT_EQ(act(NavKey::Home, {}, NavFocus::Button, FIRST_PAGE),
	          NavAction::None);
	EXPECT_EQ(act(NavKey::End, {}, NavFocus::Button, LAST_PAGE),
	          NavAction::None);
	// Ctrl+Up and Ctrl+Down come before the button rule
	EXPECT_EQ(act(NavKey::Up, CTRL, NavFocus::Button), NavAction::First);
	EXPECT_EQ(act(NavKey::Down, CTRL, NavFocus::Button), NavAction::Last);
}

// rule 5: Alt alone takes the key away from every action
TEST(NavActionRules, AltAloneDoesNothing) {
	for (const auto key : ALL_KEYS) {
		for (const auto focus : ALL_FOCUSES) {
			for (const auto modifiers : {ALT, ALT_SHIFT}) {
				for (unsigned s = 0; s < STATE_COMBOS; ++s) {
					EXPECT_EQ(act(key, modifiers, focus, stateFrom(2, 5, s)),
					          NavAction::None);
				}
				EXPECT_EQ(act(key, modifiers, focus, FIRST_PAGE),
				          NavAction::None);
				EXPECT_EQ(act(key, modifiers, focus, LAST_PAGE),
				          NavAction::None);
			}
		}
	}
	EXPECT_EQ(act(NavKey::F4, ALT, NavFocus::Button), NavAction::None);
	EXPECT_EQ(act(NavKey::R, ALT_SHIFT), NavAction::None);
	EXPECT_EQ(act(NavKey::Left, ALT, NavFocus::Viewer, NOTHING_TO_SCROLL),
	          NavAction::None);
}

// rule 6: R rotates, Shift+R rotates the other way
TEST(NavActionRules, RotationWorksInEveryViewState) {
	for (unsigned s = 0; s < STATE_COMBOS; ++s) {
		const NavState state = stateFrom(2, 5, s);
		EXPECT_EQ(act(NavKey::R, {}, NavFocus::Viewer, state),
		          NavAction::RotateClockwise);
		EXPECT_EQ(act(NavKey::R, SHIFT, NavFocus::Viewer, state),
		          NavAction::RotateCounterClockwise);
	}
	for (const auto& state : {FIRST_PAGE, LAST_PAGE, NO_PAGES, ONLY_PAGE}) {
		EXPECT_EQ(act(NavKey::R, {}, NavFocus::Viewer, state),
		          NavAction::RotateClockwise);
		EXPECT_EQ(act(NavKey::R, SHIFT, NavFocus::Button, state),
		          NavAction::RotateCounterClockwise);
	}
}

// rule 7: with Shift only Enter and Space in Page Fit do anything
TEST(NavActionRules, ShiftedKeysDoNothingExceptEnterAndSpaceInPageFit) {
	for (const auto key : ALL_KEYS) {
		if (key == NavKey::R || key == NavKey::Enter || key == NavKey::Space) {
			continue;
		}
		for (unsigned s = 0; s < STATE_COMBOS; ++s) {
			EXPECT_EQ(act(key, SHIFT, NavFocus::Viewer, stateFrom(2, 5, s)),
			          NavAction::None);
		}
		EXPECT_EQ(act(key, SHIFT, NavFocus::Viewer, FIRST_PAGE),
		          NavAction::None);
		EXPECT_EQ(act(key, SHIFT, NavFocus::Viewer, LAST_PAGE),
		          NavAction::None);
	}
}
TEST(NavActionRules, ShiftEnterAndSpaceGoBackOnlyInPageFit) {
	for (const auto key : {NavKey::Enter, NavKey::Space}) {
		for (unsigned s = 0; s < STATE_COMBOS; ++s) {
			const NavState state = stateFrom(2, 5, s);
			const bool pageFit = (s & 4u) != 0;
			EXPECT_EQ(act(key, SHIFT, NavFocus::Viewer, state),
			          pageFit ? NavAction::Previous : NavAction::None);
		}
		// the key is used up even on the first page
		EXPECT_EQ(act(key, SHIFT, NavFocus::Viewer, {0, 5, false, false, true}),
		          NavAction::Previous);
		EXPECT_EQ(act(key, SHIFT, NavFocus::Viewer, {4, 5, true, true, true}),
		          NavAction::Previous);
	}
}

// rule 8: H and S pick the cursor tools
TEST(NavActionRules, ToolKeysWorkInEveryViewState) {
	for (unsigned s = 0; s < STATE_COMBOS; ++s) {
		const NavState state = stateFrom(2, 5, s);
		EXPECT_EQ(act(NavKey::H, {}, NavFocus::Viewer, state),
		          NavAction::HandTool);
		EXPECT_EQ(act(NavKey::S, {}, NavFocus::Viewer, state),
		          NavAction::SelectTool);
	}
	for (const auto& state : {FIRST_PAGE, LAST_PAGE, NO_PAGES}) {
		EXPECT_EQ(act(NavKey::H, {}, NavFocus::Viewer, state),
		          NavAction::HandTool);
		EXPECT_EQ(act(NavKey::S, {}, NavFocus::Button, state),
		          NavAction::SelectTool);
	}
}

// rule 9: keys without modifiers
TEST(NavActionRules, LetterPageKeysAlwaysTurnPages) {
	for (unsigned s = 0; s < STATE_COMBOS; ++s) {
		const NavState state = stateFrom(2, 5, s);
		for (const auto key : {NavKey::K, NavKey::P}) {
			EXPECT_EQ(act(key, {}, NavFocus::Viewer, state),
			          NavAction::Previous);
		}
		for (const auto key : {NavKey::J, NavKey::N}) {
			EXPECT_EQ(act(key, {}, NavFocus::Viewer, state), NavAction::Next);
		}
	}
	// the key is used up even when the page cannot change
	for (const auto key : {NavKey::K, NavKey::P}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, FIRST_PAGE),
		          NavAction::Previous);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, NO_PAGES),
		          NavAction::Previous);
	}
	for (const auto key : {NavKey::J, NavKey::N}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, LAST_PAGE), NavAction::Next);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, NO_PAGES), NavAction::Next);
	}
}
TEST(NavActionRules, VerticalKeysScrollWhenThePagesScrollVertically) {
	for (const auto key : {NavKey::Up, NavKey::PageUp}) {
		const NavAction turn = NavAction::Previous;
		// no vertical scrollbar: turn, whatever the horizontal one and zoom
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 0)), turn);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 2)), turn);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 4)), turn);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 6)), turn);
		// vertical scrollbar: scroll, unless Page Fit
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 1)),
		          NavAction::None);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 3)),
		          NavAction::None);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 5)), turn);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 7)), turn);
	}
	for (const auto key : {NavKey::Down, NavKey::PageDown}) {
		const NavAction turn = NavAction::Next;
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 0)), turn);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 2)), turn);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 4)), turn);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 6)), turn);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 1)),
		          NavAction::None);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 3)),
		          NavAction::None);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 5)), turn);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, stateFrom(2, 5, 7)), turn);
	}
}
TEST(NavActionRules, HorizontalKeysScrollWhenThePagesScrollHorizontally) {
	for (unsigned s = 0; s < STATE_COMBOS; ++s) {
		const NavState state = stateFrom(2, 5, s);
		const bool horizontal = (s & 2u) != 0;
		const bool pageFit = (s & 4u) != 0;
		const bool turns = !horizontal || pageFit;
		EXPECT_EQ(act(NavKey::Left, {}, NavFocus::Viewer, state),
		          turns ? NavAction::Previous : NavAction::None);
		EXPECT_EQ(act(NavKey::Right, {}, NavFocus::Viewer, state),
		          turns ? NavAction::Next : NavAction::None);
	}
}
TEST(NavActionRules, ScrollingKeysAtTheEndsStillTurnWhenNothingScrolls) {
	// nothing to scroll: the key is used up even when the page stays
	for (const auto key : {NavKey::Up, NavKey::PageUp, NavKey::Left}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, {0, 5}), NavAction::Previous);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, {0, 0}), NavAction::Previous);
	}
	for (const auto key : {NavKey::Down, NavKey::PageDown, NavKey::Right}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, {4, 5}), NavAction::Next);
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, {0, 0}), NavAction::Next);
	}
	// and in Page Fit with scroll bars showing
	const NavState firstFit{0, 5, true, true, true};
	const NavState lastFit{4, 5, true, true, true};
	for (const auto key :
	     {NavKey::Up, NavKey::PageUp, NavKey::Left, NavKey::Backspace}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, firstFit),
		          NavAction::Previous);
	}
	for (const auto key : {NavKey::Down, NavKey::PageDown, NavKey::Right,
	                       NavKey::Space, NavKey::Enter}) {
		EXPECT_EQ(act(key, {}, NavFocus::Viewer, lastFit), NavAction::Next);
	}
}
TEST(NavActionRules, BackspaceEnterAndSpaceTurnPagesOnlyInPageFit) {
	for (unsigned s = 0; s < STATE_COMBOS; ++s) {
		const NavState state = stateFrom(2, 5, s);
		const bool pageFit = (s & 4u) != 0;
		EXPECT_EQ(act(NavKey::Backspace, {}, NavFocus::Viewer, state),
		          pageFit ? NavAction::Previous : NavAction::None);
		EXPECT_EQ(act(NavKey::Enter, {}, NavFocus::Viewer, state),
		          pageFit ? NavAction::Next : NavAction::None);
		EXPECT_EQ(act(NavKey::Space, {}, NavFocus::Viewer, state),
		          pageFit ? NavAction::Next : NavAction::None);
	}
	// still Previous or Next at the ends of the document
	EXPECT_EQ(act(NavKey::Backspace, {}, NavFocus::Viewer,
	              {0, 5, false, false, true}),
	          NavAction::Previous);
	EXPECT_EQ(
	    act(NavKey::Space, {}, NavFocus::Viewer, {4, 5, false, false, true}),
	    NavAction::Next);
	EXPECT_EQ(
	    act(NavKey::Enter, {}, NavFocus::Viewer, {4, 5, false, false, true}),
	    NavAction::Next);
}
TEST(NavActionRules, HomeAndEndCheckTheEnds) {
	for (unsigned s = 0; s < STATE_COMBOS; ++s) {
		const NavState middle = stateFrom(2, 5, s);
		EXPECT_EQ(act(NavKey::Home, {}, NavFocus::Viewer, middle),
		          NavAction::First);
		EXPECT_EQ(act(NavKey::End, {}, NavFocus::Viewer, middle),
		          NavAction::Last);
		EXPECT_EQ(act(NavKey::Home, {}, NavFocus::Viewer, stateFrom(0, 5, s)),
		          NavAction::None);
		EXPECT_EQ(act(NavKey::End, {}, NavFocus::Viewer, stateFrom(4, 5, s)),
		          NavAction::None);
	}
	// from the opposite end
	EXPECT_EQ(act(NavKey::Home, {}, NavFocus::Viewer, LAST_PAGE),
	          NavAction::First);
	EXPECT_EQ(act(NavKey::End, {}, NavFocus::Viewer, FIRST_PAGE),
	          NavAction::Last);
	EXPECT_EQ(act(NavKey::Home, {}, NavFocus::Viewer, {1, 5}),
	          NavAction::First);
	EXPECT_EQ(act(NavKey::End, {}, NavFocus::Viewer, {3, 5}), NavAction::Last);
}
TEST(NavActionRules, EmptyAndSinglePageDocumentsAreAtBothEnds) {
	for (const auto& state : {NO_PAGES, ONLY_PAGE}) {
		EXPECT_EQ(act(NavKey::Home, {}, NavFocus::Viewer, state),
		          NavAction::None);
		EXPECT_EQ(act(NavKey::End, {}, NavFocus::Viewer, state),
		          NavAction::None);
		EXPECT_EQ(act(NavKey::Down, CTRL, NavFocus::Viewer, state),
		          NavAction::None);
		EXPECT_EQ(act(NavKey::Up, CTRL, NavFocus::Viewer, state),
		          NavAction::None);
	}
}
TEST(NavActionRules, F4TogglesTheSidebarEverywhereExceptTextFields) {
	for (unsigned s = 0; s < STATE_COMBOS; ++s) {
		EXPECT_EQ(act(NavKey::F4, {}, NavFocus::Viewer, stateFrom(2, 5, s)),
		          NavAction::ToggleSidebar);
		EXPECT_EQ(act(NavKey::F4, {}, NavFocus::Button, stateFrom(2, 5, s)),
		          NavAction::ToggleSidebar);
	}
	for (const auto& state : {FIRST_PAGE, LAST_PAGE, NO_PAGES}) {
		EXPECT_EQ(act(NavKey::F4, {}, NavFocus::Viewer, state),
		          NavAction::ToggleSidebar);
	}
	for (const auto modifiers :
	     {SHIFT, CTRL, ALT, CTRL_SHIFT, CTRL_ALT, ALT_SHIFT, CTRL_ALT_SHIFT}) {
		EXPECT_EQ(act(NavKey::F4, modifiers), NavAction::None);
	}
}

// keys this module does not know
TEST(NavActionRules, OtherKeyNeverDoesAnything) {
	for (const auto focus : ALL_FOCUSES) {
		for (unsigned m = 0; m < MODIFIER_COMBOS; ++m) {
			for (unsigned s = 0; s < STATE_COMBOS; ++s) {
				EXPECT_EQ(act(NavKey::Other, modifiersFrom(m), focus,
				              stateFrom(2, 5, s)),
				          NavAction::None);
			}
		}
	}
}
} // namespace
