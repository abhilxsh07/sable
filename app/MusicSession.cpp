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
#include "MusicSession.h"
#include "helpers/logging.h"

#include <algorithm>
#include <winrt/Windows.Media.h>

namespace music {
namespace {
using namespace winrt;
using namespace Windows::Media::Control;
Playback
playback(GlobalSystemMediaTransportControlsSessionPlaybackStatus status) {
	switch (status) {
	case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
		return Playback::Playing;
	case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
		return Playback::Paused;
	default:
		return Playback::Inactive;
	}
}
template <typename Work>
auto onDispatcher(std::weak_ptr<Session> weak,
                  Microsoft::UI::Dispatching::DispatcherQueue queue,
                  Work work) {
	return [weak, queue, work](auto&&, auto&&) noexcept {
		try {
			queue.TryEnqueue([weak, work] {
				if (const auto self = weak.lock()) {
					work(*self);
				}
			});
		} catch (...) {
		}
	};
}
} // namespace

Session::Session(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
                 Changed changed)
    : m_queue(std::move(queue)), m_changed(std::move(changed)) {}
std::shared_ptr<Session>
Session::create(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
                Changed changed) {
	auto self =
	    std::shared_ptr<Session>(new Session(queue, std::move(changed)));
	start(self);
	return self;
}
Session::~Session() {
	close();
}

winrt::fire_and_forget Session::start(std::weak_ptr<Session> weak) {
	try {
		// see:
		// https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager.requestasync
		const auto manager = co_await Manager::RequestAsync();
		const auto self = weak.lock();
		if (!self || self->m_closed) {
			co_return;
		}
		self->m_manager = manager;
		const auto changed = onDispatcher(
		    weak, self->m_queue, [](Session& owner) { owner.refresh(); });
		self->m_sessionsChanged =
		    manager.SessionsChanged(winrt::auto_revoke, changed);
		self->m_currentChanged =
		    manager.CurrentSessionChanged(winrt::auto_revoke, changed);
		self->refresh();
	} catch (...) {
		diagnostics::write(diagnostics::Level::Warning,
		                   "Windows media sessions are unavailable");
	}
}
void Session::close() noexcept {
	if (m_closed) {
		return;
	}
	m_closed = true;
	++m_generation;
	++m_request;
	m_changed = {};
	m_sessionsChanged.revoke();
	m_currentChanged.revoke();
	m_mediaChanged.revoke();
	m_timelineChanged.revoke();
	m_watches.clear();
	m_remote = nullptr;
	m_manager = nullptr;
}
void Session::notify() noexcept {
	if (!m_closed && m_changed) {
		try {
			m_changed(m_snapshot);
		} catch (...) {
			diagnostics::write(diagnostics::Level::Warning,
			                   "Couldn't update the music pane");
		}
	}
}
void Session::refresh() noexcept {
	if (m_closed || !m_manager) {
		return;
	}
	try {
		const auto current = m_manager.GetCurrentSession();
		std::vector<Candidate> candidates;
		std::vector<Remote> remotes;
		std::vector<Info> infos;
		std::vector<Watch> watches;
		const auto weak = weak_from_this();
		for (const auto& remote : m_manager.GetSessions()) {
			try {
				const auto player =
				    identify(winrt::to_string(remote.SourceAppUserModelId()));
				if (player == Player::None) {
					continue;
				}
				const auto info = remote.GetPlaybackInfo();
				if (!info) {
					continue;
				}
				candidates.push_back({player, playback(info.PlaybackStatus()),
				                      remote == current, remote == m_remote});
				remotes.push_back(remote);
				infos.push_back(info);
				watches.push_back({remote, remote.PlaybackInfoChanged(
				                               winrt::auto_revoke,
				                               onDispatcher(weak, m_queue,
				                                            [](Session& owner) {
					                                            owner.refresh();
				                                            }))});
			} catch (...) {
				// A player may disappear between enumeration and querying it.
			}
		}
		m_watches = std::move(watches);
		const auto selected = choose(candidates);
		const Remote next = selected ? remotes[*selected] : nullptr;
		if (next != m_remote) {
			++m_generation;
			++m_request;
			m_mediaChanged.revoke();
			m_timelineChanged.revoke();
			m_remote = next;
			m_metadataRequested = false;
			const auto version = m_snapshot.artworkVersion + 1;
			m_snapshot = {};
			m_snapshot.artworkVersion = version;
			if (next) {
				m_mediaChanged = next.MediaPropertiesChanged(
				    winrt::auto_revoke,
				    onDispatcher(weak, m_queue,
				                 [](Session& owner) { owner.refreshMedia(); }));
				m_timelineChanged = next.TimelinePropertiesChanged(
				    winrt::auto_revoke,
				    onDispatcher(weak, m_queue, [](Session& owner) {
					    if (owner.m_closed) {
						    return;
					    }
					    try {
						    owner.readTimeline();
						    owner.notify();
					    } catch (...) {
					    }
				    }));
			}
		}
		if (selected) {
			m_snapshot.state.player = candidates[*selected].player;
			readPlayback(infos[*selected]);
			readTimeline();
			if (!m_metadataRequested) {
				refreshMedia();
			}
		}
		notify();
	} catch (...) {
		// Losing the broker must not leave controls targeting stale sessions.
		m_remote = nullptr;
		++m_generation;
		const auto version = m_snapshot.artworkVersion + 1;
		m_snapshot = {};
		m_snapshot.artworkVersion = version;
		notify();
		diagnostics::write(diagnostics::Level::Warning,
		                   "Couldn't refresh Windows media sessions");
	}
}
void Session::readPlayback(const Info& info) {
	auto& state = m_snapshot.state;
	state.playback = playback(info.PlaybackStatus());
	const auto controls = info.Controls();
	state.controls = {};
	if (controls) {
		state.controls = {
		    controls.IsPreviousEnabled(), controls.IsPlayEnabled(),
		    controls.IsPauseEnabled(),    controls.IsPlayPauseToggleEnabled(),
		    controls.IsNextEnabled(),     controls.IsPlaybackPositionEnabled(),
		    controls.IsShuffleEnabled(),  controls.IsRepeatEnabled()};
	}
	const auto shuffle = info.IsShuffleActive();
	state.shuffle = shuffle ? std::optional(shuffle.Value()) : std::nullopt;
	const auto repeat = info.AutoRepeatMode();
	state.repeat.reset();
	if (repeat) {
		using winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
		switch (repeat.Value()) {
		case MediaPlaybackAutoRepeatMode::None:
			state.repeat = Repeat::Off;
			break;
		case MediaPlaybackAutoRepeatMode::List:
			state.repeat = Repeat::All;
			break;
		case MediaPlaybackAutoRepeatMode::Track:
			state.repeat = Repeat::One;
			break;
		}
	}
	const auto rate = info.PlaybackRate();
	state.timeline.rate = rate ? rate.Value() : 1;
	state.timeline.playing = state.playback == Playback::Playing;
}
void Session::readTimeline() {
	if (!m_remote) {
		return;
	}
	// see:
	// https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessiontimelineproperties
	const auto timeline = m_remote.GetTimelineProperties();
	auto& t = m_snapshot.state.timeline;
	m_snapshot.sampledAt = std::chrono::steady_clock::now();
	if (!timeline) {
		const auto rate = t.rate;
		const bool playing = t.playing;
		t = {};
		t.rate = rate;
		t.playing = playing;
		return;
	}
	t.start = timeline.StartTime().count();
	t.end = timeline.EndTime().count();
	t.minSeek = timeline.MinSeekTime().count();
	t.maxSeek = timeline.MaxSeekTime().count();
	t.position = timeline.Position().count();
	const auto updated = timeline.LastUpdatedTime();
	// Missing timestamps must not advance a track from the Windows epoch.
	if (updated.time_since_epoch().count() > 0) {
		const double elapsed =
		    std::chrono::duration<double>(winrt::clock::now() - updated)
		        .count();
		t.position = position(t, elapsed);
	} else {
		t.position = position(t, 0);
	}
}
void Session::refreshMedia() {
	if (m_closed || !m_remote) {
		return;
	}
	m_metadataRequested = true;
	// Clear the previous track before fetching; late metadata cannot restore
	// it.
	m_snapshot.state.title.clear();
	m_snapshot.state.artist.clear();
	m_snapshot.artwork = nullptr;
	++m_snapshot.artworkVersion;
	notify();
	readMedia(weak_from_this(), m_remote, m_generation, ++m_request);
}
winrt::fire_and_forget Session::readMedia(std::weak_ptr<Session> weak,
                                          Remote remote,
                                          std::uint64_t generation,
                                          std::uint64_t request) {
	try {
		const auto media = co_await remote.TryGetMediaPropertiesAsync();
		const auto self = weak.lock();
		if (!self || self->m_closed || self->m_generation != generation ||
		    self->m_request != request) {
			co_return;
		}
		self->m_snapshot.state.title =
		    media ? winrt::to_string(media.Title()) : "";
		self->m_snapshot.state.artist =
		    media ? winrt::to_string(media.Artist()) : "";
		self->m_snapshot.artwork = media ? media.Thumbnail() : nullptr;
		++self->m_snapshot.artworkVersion;
		self->notify();
	} catch (...) {
		diagnostics::write(diagnostics::Level::Debug,
		                   "Couldn't read music metadata");
	}
}
void Session::command(Command command, double seekSeconds) {
	if (m_closed || !m_remote || !enabled(m_snapshot.state, command)) {
		return;
	}
	m_snapshot.state.busy = true;
	m_snapshot.state.error.clear();
	notify();
	send(weak_from_this(), m_remote, m_generation, command, seekSeconds);
}
winrt::fire_and_forget Session::send(std::weak_ptr<Session> weak, Remote remote,
                                     std::uint64_t generation, Command command,
                                     double seekSeconds) {
	bool accepted = false;
	try {
		// Release the owner before the remote operation suspends.
		winrt::Windows::Foundation::IAsyncOperation<bool> operation{nullptr};
		{
			const auto self = weak.lock();
			if (!self || self->m_closed || self->m_generation != generation) {
				co_return;
			}
			const auto& state = self->m_snapshot.state;
			switch (command) {
			case Command::Previous:
				operation = remote.TrySkipPreviousAsync();
				break;
			case Command::Next:
				operation = remote.TrySkipNextAsync();
				break;
			case Command::PlayPause:
				if (state.controls.toggle) {
					operation = remote.TryTogglePlayPauseAsync();
				} else if (state.playback == Playback::Playing) {
					operation = remote.TryPauseAsync();
				} else {
					operation = remote.TryPlayAsync();
				}
				break;
			case Command::Seek:
				if (const auto target =
				        seekPosition(state.timeline, seekSeconds)) {
					operation = remote.TryChangePlaybackPositionAsync(*target);
				}
				break;
			case Command::Shuffle:
				operation = remote.TryChangeShuffleActiveAsync(
				    !state.shuffle.value_or(false));
				break;
			case Command::Repeat:
				using winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
				const auto next =
				    nextRepeat(state.repeat.value_or(Repeat::Off));
				operation = remote.TryChangeAutoRepeatModeAsync(
				    next == Repeat::Off   ? MediaPlaybackAutoRepeatMode::None
				    : next == Repeat::All ? MediaPlaybackAutoRepeatMode::List
				                          : MediaPlaybackAutoRepeatMode::Track);
				break;
			}
		}
		if (operation) {
			accepted = co_await operation;
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Warning,
		                   "Couldn't send a music control request");
	}
	if (const auto self = weak.lock();
	    self && !self->m_closed && self->m_generation == generation) {
		self->m_snapshot.state.busy = false;
		self->m_snapshot.state.error =
		    accepted ? "" : "The player couldn't complete that request.";
		self->refresh();
	}
}
} // namespace music
