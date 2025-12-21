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
#include "pch.h"
#include "MusicPane.h"
#include "helpers/logging.h"

#include <algorithm>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace music {
namespace {
void label(const DependencyObject& object, std::wstring_view name) {
	Automation::AutomationProperties::SetName(object, name);
}
} // namespace
Pane::Pane(Button button, std::function<void()> layoutChanged)
    : m_button(std::move(button)), m_layoutChanged(std::move(layoutChanged)) {}
std::shared_ptr<Pane> Pane::attach(const Button& button,
                                   std::function<void()> layoutChanged,
                                   bool connect) {
	auto self =
	    std::shared_ptr<Pane>(new Pane(button, std::move(layoutChanged)));
	self->initialize(connect);
	return self;
}
Pane::~Pane() {
	close();
}

void Pane::initialize(bool connect) {
	const auto weak = weak_from_this();
	layout();
	m_themeChanged = appearance::observe([weak] {
		if (auto self = weak.lock()) {
			self->tint(self->m_tint);
		}
	});
	tint(m_tint);

	m_previous.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::Previous);
		}
	});
	m_play.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::PlayPause);
		}
	});
	m_next.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::Next);
		}
	});
	m_shuffle.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::Shuffle);
		}
	});
	m_repeat.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::Repeat);
		}
	});
	const auto queue = m_button.DispatcherQueue();
	m_timer = queue.CreateTimer();
	m_timer.Interval(std::chrono::milliseconds(250));
	m_timer.Tick([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->progress();
		}
	});
	m_seekTimer = queue.CreateTimer();
	m_seekTimer.IsRepeating(false);
	m_seekTimer.Interval(std::chrono::milliseconds(350));
	m_seekTimer.Tick([weak](auto&&, auto&&) {
		if (auto self = weak.lock(); self && !self->m_closed) {
			if (const auto target = seekPosition(
			        self->m_snapshot.state.timeline, self->m_seek.Value())) {
				self->m_pendingSeek = SeekRequest{
				    *target, winrt::clock::now().time_since_epoch().count(),
				    self->m_snapshot.trackVersion};
				self->m_seekExpiry.Start();
#ifdef SABLE_SURFACE_TEST
				if (self->m_seekSink) {
					self->m_seekSink(self->m_seek.Value());
				} else
#endif
				    if (self->m_session) {
					self->m_session->command(Command::Seek,
					                         self->m_seek.Value());
				}
			}
		}
	});
	m_seekExpiry = queue.CreateTimer();
	m_seekExpiry.IsRepeating(false);
	m_seekExpiry.Interval(std::chrono::seconds(5));
	m_seekExpiry.Tick([weak](auto&&, auto&&) {
		if (auto self = weak.lock(); self && !self->m_closed) {
			self->m_pendingSeek.reset();
			self->m_seeking = false;
			self->progress();
		}
	});
	m_seek.ValueChanged([weak](auto&&, auto&&) {
		if (auto self = weak.lock();
		    self && !self->m_closed && !self->m_updating &&
		    enabled(self->m_snapshot.state, Command::Seek)) {
			self->m_seeking = true;
			self->m_pendingSeek.reset();
			self->m_seekExpiry.Stop();
			self->m_seekTimer.Stop();
			self->m_seekTimer.Start();
		}
	});
	m_flyout.Opened([weak](auto&&, auto&&) {
		if (auto self = weak.lock(); self && !self->m_closed) {
			self->m_open = true;
			if (self->m_session) {
				self->m_session->reload();
			}
			self->motion(true);
			self->progress();
		}
	});
	m_flyout.Closed([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->m_open = false;
			self->m_timer.Stop();
			self->m_seekTimer.Stop();
			self->m_seekExpiry.Stop();
			self->m_pendingSeek.reset();
			self->m_seeking = false;
		}
	});
	if (connect) {
		m_session = Session::create(queue, [weak](const Snapshot& snapshot) {
			if (const auto self = weak.lock()) {
				self->update(snapshot);
			}
		});
	}
}
void Pane::command(Command command) {
	if (!m_closed && m_session) {
		m_session->command(command);
	}
}
void Pane::update(const Snapshot& snapshot) {
	if (m_closed) {
		return;
	}
	const bool changedSession =
	    snapshot.state.player != m_snapshot.state.player ||
	    snapshot.trackVersion != m_snapshot.trackVersion;
	m_snapshot = snapshot;
	const auto& state = m_snapshot.state;
	const auto visibility =
	    state.visible() ? Visibility::Visible : Visibility::Collapsed;
	if (m_button.Visibility() != visibility) {
		m_button.Visibility(visibility);
		if (m_layoutChanged) {
			m_layoutChanged();
		}
	}
	if (!state.visible()) {
		m_flyout.Hide();
	}
	m_title.Text(
	    to_hstring(state.title.empty() ? "Unknown track" : state.title));
	ToolTipService::SetToolTip(m_title, box_value(m_title.Text()));
	m_artist.Text(to_hstring(state.artist));
	m_artist.Visibility(state.artist.empty() ? Visibility::Collapsed
	                                         : Visibility::Visible);
	m_player.Text(to_hstring(playerName(state.player)));
	m_error.Text(to_hstring(state.error));
	m_error.Visibility(state.error.empty() ? Visibility::Collapsed
	                                       : Visibility::Visible);
	m_previous.IsEnabled(enabled(state, Command::Previous));
	m_play.IsEnabled(enabled(state, Command::PlayPause));
	m_next.IsEnabled(enabled(state, Command::Next));
	m_seek.IsEnabled(enabled(state, Command::Seek));
	m_shuffle.IsEnabled(enabled(state, Command::Shuffle));
	m_shuffle.IsChecked(state.shuffle.value_or(false));
	m_repeat.IsEnabled(enabled(state, Command::Repeat));
	const bool playing = state.playback == Playback::Playing;
	m_playIcon.Glyph(playing ? L"\uE769" : L"\uE768");
	label(m_play, playing ? L"Pause" : L"Play");
	ToolTipService::SetToolTip(m_play, box_value(playing ? L"Pause" : L"Play"));
	const hstring repeat = state.repeat == Repeat::All   ? L"Repeat: all"
	                       : state.repeat == Repeat::One ? L"Repeat: one"
	                                                     : L"Repeat: off";
	m_repeatIcon.Glyph(state.repeat == Repeat::One ? L"\uE8ED" : L"\uE8EE");
	m_repeat.IsChecked(state.repeat && *state.repeat != Repeat::Off);
	m_shuffle.Visibility(state.controls.shuffle && state.shuffle
	                         ? Visibility::Visible
	                         : Visibility::Collapsed);
	m_repeat.Visibility(state.controls.repeat && state.repeat
	                        ? Visibility::Visible
	                        : Visibility::Collapsed);
	ToolTipService::SetToolTip(m_repeat, box_value(repeat));
	ToolTipService::SetToolTip(
	    m_shuffle, box_value(state.shuffle.value_or(false) ? L"Shuffle on"
	                                                       : L"Shuffle off"));
	label(m_shuffle,
	      state.shuffle.value_or(false) ? L"Shuffle on" : L"Shuffle off");
	m_playingDot.Visibility(playing ? Visibility::Visible
	                                : Visibility::Collapsed);
	Automation::AutomationProperties::SetItemStatus(
	    m_button, playing ? L"Playing" : L"Paused");
	label(m_repeat, repeat);
	if (changedSession || !state.controls.seek || !state.error.empty() ||
	    (m_pendingSeek &&
	     !awaitingSeek(*m_pendingSeek, state.timeline,
	                   winrt::clock::now().time_since_epoch().count(),
	                   snapshot.trackVersion))) {
		m_seekTimer.Stop();
		m_seekExpiry.Stop();
		m_pendingSeek.reset();
		m_seeking = false;
	}
	if (snapshot.artworkVersion != m_artworkVersion) {
		m_artworkVersion = snapshot.artworkVersion;
		const auto generation = ++m_artworkGeneration;
		m_artFade.Stop();
		m_art.Opacity(1);
		m_previousArt.Source(nullptr);
		if (snapshot.artwork) {
			artwork(weak_from_this(), snapshot.artwork, generation);
		} else {
			setArtwork(nullptr, {});
		}
	}
	progress();
}
void Pane::progress() {
	if (m_closed) {
		return;
	}
	if (m_open && m_snapshot.state.playback == Playback::Playing) {
		if (!m_timer.IsRunning()) {
			m_timer.Start();
		}
	} else {
		m_timer.Stop();
	}
	const auto& t = m_snapshot.state.timeline;
	const double elapsed =
	    std::chrono::duration<double>(std::chrono::steady_clock::now() -
	                                  m_snapshot.sampledAt)
	        .count();
	const auto current = position(t, elapsed);
	const bool valid = t.start >= 0 && t.end > t.start;
	m_updating = true;
	m_seek.Maximum(
	    valid ? static_cast<double>(t.end - t.start) / TICKS_PER_SECOND : 1);
	if (!m_seeking) {
		m_seek.Value(valid ? static_cast<double>(current - t.start) /
		                         TICKS_PER_SECOND
		                   : 0);
	}
	m_updating = false;
	const auto shown =
	    m_seeking ? seekPosition(t, m_seek.Value()).value_or(current) : current;
	m_elapsed.Text(to_hstring(timeText(valid ? shown - t.start : 0)));
	m_remaining.Text(valid ? L"-" + to_hstring(timeText(t.end - shown))
	                       : L"--:--");
}
winrt::fire_and_forget
Pane::artwork(std::weak_ptr<Pane> weak,
              std::shared_ptr<const std::vector<std::uint8_t>> bytes,
              std::uint64_t generation) {
	try {
		const auto current = [weak, generation] {
			const auto self = weak.lock();
			return self && !self->m_closed &&
			       self->m_artworkGeneration == generation;
		};
		if (!bytes || bytes->size() > 8 * 1024 * 1024) {
			co_return;
		}
		Windows::Storage::Streams::InMemoryRandomAccessStream stream;
		Windows::Storage::Streams::DataWriter writer(stream);
		writer.WriteBytes(*bytes);
		co_await writer.StoreAsync();
		writer.DetachStream();
		stream.Seek(0);
		if (!current()) {
			co_return;
		}
		using namespace Windows::Graphics::Imaging;
		const auto decoder = co_await BitmapDecoder::CreateAsync(stream);
		if (!current()) {
			co_return;
		}
		if (static_cast<std::uint64_t>(decoder.PixelWidth()) *
		        decoder.PixelHeight() >
		    16 * 1024 * 1024) {
			throw hresult_invalid_argument();
		}
		BitmapTransform transform;
		transform.ScaledWidth(16);
		transform.ScaledHeight(16);
		const auto data = co_await decoder.GetPixelDataAsync(
		    BitmapPixelFormat::Bgra8, BitmapAlphaMode::Straight, transform,
		    ExifOrientationMode::IgnoreExifOrientation,
		    ColorManagementMode::DoNotColorManage);
		const auto pixels = data.DetachPixelData();
		const auto color = artworkTint({pixels.data(), pixels.size()});
		if (!current()) {
			co_return;
		}
		stream.Seek(0);
		Media::Imaging::BitmapImage bitmap;
		bitmap.DecodePixelWidth(192);
		co_await bitmap.SetSourceAsync(stream);
		if (const auto self = weak.lock();
		    self && !self->m_closed &&
		    self->m_artworkGeneration == generation) {
			self->setArtwork(bitmap, color);
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Debug,
		                   "Couldn't decode music artwork");
		if (const auto self = weak.lock();
		    self && !self->m_closed &&
		    self->m_artworkGeneration == generation) {
			self->setArtwork(nullptr, {});
		}
	}
}
void Pane::close() noexcept {
	if (m_closed) {
		return;
	}
	m_closed = true;
	++m_artworkGeneration;
	if (m_session) {
		m_session->close();
	}
	m_session.reset();
	m_layoutChanged = {};
	try {
		if (m_timer) {
			m_timer.Stop();
		}
		if (m_seekTimer) {
			m_seekTimer.Stop();
		}
		if (m_seekExpiry) {
			m_seekExpiry.Stop();
		}
		m_pendingSeek.reset();
		m_transition.Stop();
		m_artFade.Stop();
		m_motion.Stop();
		m_flyout.Hide();
		m_button.Flyout(nullptr);
	} catch (...) {
	}
}
} // namespace music
