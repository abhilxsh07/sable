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
		owner.openTool(L"organize");
		check(v.organizing() && v.OrganizeGrid().Items().Size() == 3,
		      "Organize did not show a tile per page");
		break;
	case 1:
		if (v.m_organizeTile < 3) {
			check(ticks < 100, "Organize tiles did not render");
			return;
		}
		check(v.OrganizeStatus().Text() == L"3 pages" &&
		          !v.OrganizeDelete().IsEnabled(),
		      "Organize state without a selection");
		select({0, 2});
		check(v.OrganizeStatus().Text() == L"3 pages · 2 selected",
		      "Organize did not count the selection");
		invoke(v.OrganizeRotateRight());
		break;
	case 2:
		check(v.m_notesDirty && sized(0, 842, 595) && sized(1, 792, 612) &&
		          sized(2, 300, 200) && v.OrganizeGrid().Items().Size() == 3,
		      "Rotate right did not turn the selected pages");
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
		invoke(v.OrganizeDelete());
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
		invoke(v.OrganizeDone());
		break;
	case 5:
		check(!v.organizing() && v.m_layout && v.m_layout->pages().size() == 2,
		      "Done did not return to the changed pages");
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
} // namespace winrt::Sable::implementation
