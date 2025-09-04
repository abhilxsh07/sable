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

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
struct TabsTest {
	weak_ref<MainWindow> window;
	pdf::Engine& engine;
	TextBlock status;
	Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
	int step = 0;
	int ticks = 0;
	double zoom = 0;
	bool positioned = false;
	com_ptr<ViewerPage> first;

	TabsTest(MainWindow& owner, pdf::Engine& pdf)
	    : window(owner.get_weak()), engine(pdf) {}

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
				self->closeTab(self->m_tabs.front().tab);
				check(self->m_tabs.empty() && !self->Viewer());
				status.Text(
				    L"PASS: duplicate paths, per-tab page/zoom, shared filter, "
				    L"error isolation, password activation, close/open, home");
				timer.Stop();
				return;
			}
			++step;
			ticks = 0;
		} catch (...) {
			status.Text(L"FAIL at step " + to_hstring(step));
			if (auto current = self->Viewer()) {
				const auto* viewer = get_self<ViewerPage>(current);
				status.Text(status.Text() + L", page " +
				            to_hstring(viewer->m_current + 1) + L", zoom " +
				            to_hstring(viewer->m_zoom));
			}
			timer.Stop();
		}
	}
};
} // namespace winrt::Sable::implementation

Window tabsTest(pdf::Engine& engine) {
	auto window = make_self<Sable::implementation::MainWindow>();
	auto state =
	    std::make_shared<Sable::implementation::TabsTest>(*window, engine);
	std::weak_ptr<Sable::implementation::TabsTest> weak = state;
	state->status.Text(L"Checking document tabs");
	state->status.TextWrapping(TextWrapping::Wrap);
	Border report;
	report.Child(state->status);
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
