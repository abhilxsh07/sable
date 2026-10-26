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

#include "MusicSession.h"
#include "Appearance.h"
#include <chrono>
#include <functional>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace winrt::Sable::implementation {
struct TabsTest;
}

namespace music {
class Pane : public std::enable_shared_from_this<Pane> {
  public:
	static std::shared_ptr<Pane>
	attach(const winrt::Microsoft::UI::Xaml::Controls::Button& button,
	       std::function<void()> layoutChanged, bool connect = true);
	~Pane();
	void close() noexcept;

  private:
#ifdef SABLE_SURFACE_TEST
	friend struct winrt::Sable::implementation::TabsTest;
#endif
	Pane(winrt::Microsoft::UI::Xaml::Controls::Button button,
	     std::function<void()> layoutChanged);
	void initialize(bool connect);
	void layout();
	void show();
	void motion(bool opening);
	void
	setArtwork(const winrt::Microsoft::UI::Xaml::Media::ImageSource& source,
	           Tint color);
	void update(const Snapshot& snapshot);
	void progress();
	void command(Command command);
	void tint(Tint tint);
	appearance::Subscription m_themeChanged;
	Tint m_tint{45, 45, 45};
	static winrt::fire_and_forget
	artwork(std::weak_ptr<Pane> weak,
	        std::shared_ptr<const std::vector<std::uint8_t>> bytes,
	        std::uint64_t generation);
	winrt::Microsoft::UI::Xaml::Controls::Button m_button{nullptr};
	winrt::Microsoft::UI::Xaml::Controls::Flyout m_flyout;
	winrt::Microsoft::UI::Xaml::Controls::TextBlock m_title, m_artist, m_player;
	winrt::Microsoft::UI::Xaml::Controls::TextBlock m_elapsed, m_remaining,
	    m_error;
	winrt::Microsoft::UI::Xaml::Controls::Image m_art, m_previousArt;
	winrt::Microsoft::UI::Xaml::Controls::FontIcon m_placeholder, m_playIcon,
	    m_repeatIcon;
	winrt::Microsoft::UI::Xaml::Controls::Button m_previous, m_play, m_next;
	winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton m_shuffle,
	    m_repeat;
	winrt::Microsoft::UI::Xaml::Controls::Border m_surface;
	winrt::Microsoft::UI::Xaml::Controls::Grid m_playingDot;
	winrt::Microsoft::UI::Xaml::Media::GradientStop m_gradientTop,
	    m_gradientBottom;
	winrt::Microsoft::UI::Xaml::Media::SolidColorBrush m_seekFill;
	winrt::Microsoft::UI::Xaml::Media::TranslateTransform m_slide;
	winrt::Microsoft::UI::Xaml::Controls::Slider m_seek;
	winrt::Microsoft::UI::Xaml::Media::AcrylicBrush m_background;
	winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard m_transition,
	    m_artFade, m_motion;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{nullptr},
	    m_seekTimer{nullptr}, m_seekExpiry{nullptr};
	std::function<void()> m_layoutChanged;
	std::shared_ptr<Session> m_session;
	Snapshot m_snapshot;
	std::optional<SeekRequest> m_pendingSeek;
#ifdef SABLE_SURFACE_TEST
	std::function<void(double)> m_seekSink;
#endif
	std::uint64_t m_artworkVersion = 0, m_artworkGeneration = 0;
	bool m_closed = false, m_updating = false, m_seeking = false,
	     m_open = false, m_closing = false, m_allowClose = false;
	// how long the last open, close, tint and artwork change ran for
	std::chrono::milliseconds m_enterLength{0}, m_exitLength{0},
	    m_tintLength{0}, m_artLength{0};
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_closeFallback{
	    nullptr};
	bool m_fadingOut = false, m_artFading = false;
	// each close and each artwork fade gets a number, so a stale one's
	// Completed can't act on a later one
	std::uint64_t m_closeToken = 0, m_artFadeToken = 0;
	void finishClosing(std::uint64_t token);
};
} // namespace music
