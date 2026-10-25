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

TEST(PageText, NumberAndCount) {
	EXPECT_EQ(helpers::pageText(2, 128, NO_LABELS),
	          (helpers::PageText{"3", "of 128"}));
	EXPECT_EQ(helpers::pageText(999, 1000, NO_LABELS),
	          (helpers::PageText{"1000", "of 1,000"}));
	EXPECT_EQ(helpers::pageText(0, 1234567, NO_LABELS),
	          (helpers::PageText{"1", "of 1,234,567"}));
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
} // namespace
