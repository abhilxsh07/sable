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
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

namespace winrt::Sable::implementation {
struct TabsTest;
}

namespace night_run {
class Pane {
  public:
	// best is the file the best distance is kept in; empty keeps it in memory
	static std::shared_ptr<Pane>
	create(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
	       std::filesystem::path best);
	~Pane();

	winrt::Microsoft::UI::Xaml::FrameworkElement element() const {
		return m_root;
	}
	bool focus();
	bool playfieldFocused(
	    const winrt::Windows::Foundation::IInspectable& focused) const;
	void key();
	void steer(int direction);
	void pause();

  private:
#ifdef SABLE_SURFACE_TEST
	friend struct winrt::Sable::implementation::TabsTest;
#endif
	using Clock = std::chrono::steady_clock;
	Pane(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
	     std::filesystem::path best);
	void start(bool restart);
	void tick();
	void render();
	void updateControls();
	void sound(Cue cue);
	void finish();

	Run m_run{static_cast<unsigned>(Clock::now().time_since_epoch().count())};
	Audio m_audio;
	bool m_muted = false;
	std::filesystem::path m_bestFile;
	std::uint64_t m_best = 0;
	bool m_newBest = false;
	double m_wormX = 0;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{nullptr};
	Clock::time_point m_next;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_root;
	winrt::Microsoft::UI::Xaml::Controls::ContentControl m_field;
	winrt::Microsoft::UI::Xaml::Controls::Button m_play, m_restart, m_mute;
	winrt::Microsoft::UI::Xaml::Controls::TextBlock m_score, m_status;
	winrt::Microsoft::UI::Xaml::Shapes::Rectangle m_runner;
	winrt::Microsoft::UI::Xaml::Shapes::Ellipse m_head, m_shield;
	winrt::Microsoft::UI::Xaml::Media::SolidColorBrush m_board{nullptr},
	    m_inked{nullptr};
	std::vector<winrt::Microsoft::UI::Xaml::Shapes::Rectangle> m_waves;
	// one shape per lane of every row that can be on the track at once
	std::vector<winrt::Microsoft::UI::Xaml::Shapes::Rectangle> m_cells;
	std::vector<Cell> m_cellKinds;
	std::vector<winrt::Microsoft::UI::Xaml::Shapes::Ellipse> m_worm;
};
} // namespace night_run
