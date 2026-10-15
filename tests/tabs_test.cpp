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
#include "../app/pch.h"
#include "../app/MainWindow.xaml.h"
#include "../app/pdf/engine.h"
#include "surface_test.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string_view>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
struct TabsTest {
	weak_ref<MainWindow> window;
	pdf::Engine& engine;
	TextBlock status;
	Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
	bool shortcuts = false;
	int step = 0;
	int ticks = 0;
	int dragStarts = 0;
	int dragFinishes = 0;
	int dragResult = 0;
	double zoom = 0;
	bool positioned = false;
	com_ptr<ViewerPage> first;

	TabsTest(MainWindow& owner, pdf::Engine& pdf)
	    : window(owner.get_weak()), engine(pdf) {}

	void focus(bool pageBox) {
		if (auto owner = window.get()) {
			if (auto current = owner->Viewer()) {
				auto v = get_self<ViewerPage>(current);
				if (pageBox) {
					v->PageBox().Focus(FocusState::Programmatic);
				} else {
					v->Scroller().Focus(FocusState::Programmatic);
				}
			}
		}
	}
	void clearBindings() {
		if (auto owner = window.get()) {
			owner->m_shortcuts.clear();
			owner->Tabs().SelectedItem().as<TabViewItem>().Focus(
			    FocusState::Programmatic);
		}
	}
	void check(bool condition) {
		if (!condition) {
			throw std::runtime_error("tab check failed");
		}
	}
	bool ready(ViewerPage& viewer) {
		return viewer.m_layout && !viewer.m_startPage && !viewer.m_settling &&
		       !viewer.m_landingAt;
	}
	void tick() {
		auto self = window.get();
		if (!self) {
			timer.Stop();
			return;
		}
		try {
			check(probeCheck());
			if (shortcuts) {
				const auto current = self->Viewer();
				const auto focus = Input::FocusManager::GetFocusedElement(
				    self->RootGrid().XamlRoot());
				hstring where = L"other";
				if (current) {
					const auto v = get_self<ViewerPage>(current);
					where = focus == v->Scroller()  ? L"document"
					        : focus == v->PageBox() ? L"page box"
					        : focus == self->Tabs().SelectedItem()
					            ? L"tab header"
					            : L"other";
				}
				status.Text(L"Focus: " + where + L"; tab " +
				            to_hstring(self->Tabs().SelectedIndex() + 1) +
				            L" of " + to_hstring(self->m_tabs.size()) +
				            L"; drag events " + to_hstring(dragStarts) + L"/" +
				            to_hstring(dragFinishes) + L"; result " +
				            to_hstring(dragResult));
				return;
			}
			if (++ticks > 120) {
				throw std::runtime_error("tab check timed out");
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
				check(self->Tabs().CanDragTabs() &&
				      self->Tabs().CanReorderTabs());
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
				check(self->m_tabs.empty() && !self->Viewer() &&
				      first->m_closed);
				check(self->Nav().Visibility() == Visibility::Visible &&
				      !self->ZoomInItem().IsEnabled());
				self->openDocument(engine, L"tests/fixtures/password.pdf", {});
				self->openDocument(engine, L"tests/fixtures/pages.pdf", {});
				get_self<ViewerPage>(self->m_tabs[0].viewer)
				    ->askPassword(false);
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
				while (!self->m_tabs.empty()) {
					self->closeTab(self->m_tabs.back().tab);
				}
				check(!self->Viewer());
				status.Text(
				    L"PASS: duplicate paths, per-tab page/zoom, shared filter, "
				    L"error isolation, password activation, home, reopen, "
				    L"order, drag lifecycle, width cap");
				probePass(self.as<Window>());
				timer.Stop();
				return;
			}
			++step;
			ticks = 0;
		} catch (...) {
			status.Text(L"FAIL at step " + to_hstring(step) + L": " +
			            to_message());
			if (auto current = self->Viewer()) {
				const auto* viewer = get_self<ViewerPage>(current);
				status.Text(status.Text() + L", page " +
				            to_hstring(viewer->m_current + 1) + L", zoom " +
				            to_hstring(viewer->m_zoom));
			}
			timer.Stop();
			probeFail(status.Text());
		}
	}
};
} // namespace winrt::Sable::implementation

Window tabsTest(pdf::Engine& engine) {
	auto window = make_self<Sable::implementation::MainWindow>();
	auto state =
	    std::make_shared<Sable::implementation::TabsTest>(*window, engine);
	std::weak_ptr<Sable::implementation::TabsTest> weak = state;
	wchar_t mode[8]{};
	state->shortcuts = GetEnvironmentVariableW(L"SableShortcutTest", mode, 8) &&
	                   std::wstring_view(mode) == L"true";
	state->status.Text(L"Checking document tabs");
	state->status.TextWrapping(TextWrapping::Wrap);
	Border report;
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
	window->openDocument(engine, L"tests/fixtures/pages.pdf", 2);
	window->openDocument(engine, L"tests/fixtures/long.pdf", 500);
	window->openDocument(engine, L"tests/fixtures/./pages.pdf", 1);
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
