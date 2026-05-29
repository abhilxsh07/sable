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
#pragma once

#include "NightRunAudio.h"
#include "helpers/night_run.h"
#include <chrono>
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

namespace winrt::Sable::implementation {
struct TabsTest;
}

namespace night_run {
class Pane {
  public:
	static std::shared_ptr<Pane>
	create(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue);
	~Pane();

	winrt::Microsoft::UI::Xaml::FrameworkElement element() const {
		return m_root;
	}
	bool focus();
	void key();
	void steer(int direction);
	void pause();

  private:
#ifdef SABLE_SURFACE_TEST
	friend struct winrt::Sable::implementation::TabsTest;
#endif
	using Clock = std::chrono::steady_clock;
	explicit Pane(
	    const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue);
	void start(bool restart);
	void tick();
	void render();
	void updateControls();
	void sound(Cue cue);

	Run m_run{static_cast<unsigned>(Clock::now().time_since_epoch().count())};
	Audio m_audio;
	bool m_muted = false;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{nullptr};
	Clock::time_point m_next;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_root;
	winrt::Microsoft::UI::Xaml::Controls::ContentControl m_field;
	winrt::Microsoft::UI::Xaml::Controls::Button m_play, m_restart, m_mute;
	winrt::Microsoft::UI::Xaml::Controls::TextBlock m_score, m_status;
	winrt::Microsoft::UI::Xaml::Shapes::Rectangle m_runner, m_obstacle;
	winrt::Microsoft::UI::Xaml::Shapes::Rectangle m_page;
	winrt::Microsoft::UI::Xaml::Shapes::Ellipse m_head, m_shield;
	std::vector<winrt::Microsoft::UI::Xaml::Shapes::Rectangle> m_waves;
};
} // namespace night_run
