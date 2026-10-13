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
#include "../app/ViewerPage.xaml.h"
#include <winrt/Microsoft.UI.Windowing.h>
#include <cmath>
#include <memory>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
// the real viewer with injected scales; no display settings are changed
struct ScaleTest {
	com_ptr<ViewerPage> viewer = make_self<ViewerPage>();
	TextBlock status;
	Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
	int step = -1;
	int ticks = 0;
	double zoom = 0;
	double x = 0;
	double y = 0;
	std::size_t page = 0;

	void tick() {
		auto& v = *viewer;
		constexpr double scales[]{1.5, 2.0, 1.25, 1.0};
		if (++ticks > 120) {
			status.Text(L"FAIL: timed out waiting for sharp pages");
			timer.Stop();
			return;
		}
		if (!v.m_layout || v.m_startPage || v.m_settling || v.m_rendering ||
		    v.m_landingAt || v.m_wanted.first == v.m_wanted.end) {
			return;
		}
		for (auto i = v.m_wanted.first; i < v.m_wanted.end; ++i) {
			const auto bitmap = v.m_device.bitmaps.find(i);
			const auto key = v.key(i);
			if (bitmap == v.m_device.bitmaps.end()) {
				return;
			}
			const auto size = bitmap->second->GetPixelSize();
			if (size.width != static_cast<UINT32>(key.width) ||
			    size.height != static_cast<UINT32>(key.height)) {
				return;
			}
		}
		if (step < 0) {
			// the monitor stays at its real scale during this injected check
			v.m_rootChanged.revoke();
			zoom = v.m_zoom;
			x = v.Scroller().HorizontalOffset();
			y = v.Scroller().VerticalOffset();
			page = v.m_current;
		} else if (v.m_scale != scales[step] || zoom != v.m_zoom ||
		           page != v.m_current ||
		           std::abs(x - v.Scroller().HorizontalOffset()) > 1 ||
		           std::abs(y - v.Scroller().VerticalOffset()) > 1) {
			status.Text(L"FAIL: wrong scale, reading position or zoom");
			timer.Stop();
			return;
		}
		if (++step == 4) {
			if (v.setScale(0) || v.setScale(1e308) || v.m_scale != 1.0) {
				status.Text(L"FAIL: invalid scale changed the surface");
			} else {
				status.Text(L"PASS: 150%, 200%, 125%, 100%; sharp pages, "
				            L"same page/zoom/offsets; invalid scales rejected");
			}
			timer.Stop();
			return;
		}
		ticks = 0;
		status.Text(L"Checking scale " + to_hstring(scales[step]));
		// a render already in flight must be discarded on the next change
		v.setScale(1.75);
		v.m_settling = false;
		v.updateVisible();
		if (!v.setScale(scales[step])) {
			status.Text(L"FAIL: scale change was rejected");
			timer.Stop();
		}
	}

	void closeDuringRender() {
		timer.Stop();
		viewer->setScale(2.0);
		viewer->m_settling = false;
		viewer->updateVisible();
		viewer->queueScale();
		viewer->close();
		status.Text(L"Closed with render and scale callback pending");
	}
};
} // namespace winrt::Sable::implementation

Window scaleTest(pdf::Engine& engine) {
	Window window;
	window.Title(L"Sable scale test");
	window.AppWindow().Resize({1100, 900});
	auto state = std::make_shared<Sable::implementation::ScaleTest>();
	std::weak_ptr<Sable::implementation::ScaleTest> weak = state;
	Grid grid;
	RowDefinition top;
	top.Height({80, GridUnitType::Pixel});
	grid.RowDefinitions().Append(top);
	grid.RowDefinitions().Append(RowDefinition());
	state->status.Text(L"Waiting for page 500");
	StackPanel controls;
	controls.Children().Append(state->status);
	Button close;
	close.Content(box_value(L"Close during render"));
	close.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->closeDuringRender();
		}
	});
	controls.Children().Append(close);
	grid.Children().Append(controls);
	auto viewer = state->viewer.as<Sable::ViewerPage>();
	Grid::SetRow(viewer, 1);
	grid.Children().Append(viewer);
	window.Content(grid);
	window.Closed([state](auto&&, auto&&) {
		state->timer.Stop();
		state->viewer->close();
	});
	state->viewer->open(engine, L"tests/fixtures/long.pdf", 500);
	state->timer = state->viewer->DispatcherQueue().CreateTimer();
	state->timer.Interval(std::chrono::milliseconds(250));
	state->timer.Tick([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->tick();
		}
	});
	state->timer.Start();
	return window;
}
