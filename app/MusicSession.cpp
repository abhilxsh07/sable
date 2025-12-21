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
#include <format>
#include <winrt/Windows.Media.h>
namespace music {
namespace {
using namespace winrt;
using namespace Windows::Media::Control;
Playback playback(GlobalSystemMediaTransportControlsSessionPlaybackStatus s) {
	switch (s) {
	case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
		return Playback::Playing;
	case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
		return Playback::Paused;
	default:
		return Playback::Inactive;
	}
}
#ifdef _DEBUG
void trace(const std::string& message) {
	diagnostics::write(diagnostics::Level::Info, "Music: " + message);
}
std::string describe(const GlobalSystemMediaTransportControlsSession& remote) {
	if (!remote) {
		return "none";
	}
	try {
		return std::format(
		    "{} [{:x}] status={}", to_string(remote.SourceAppUserModelId()),
		    reinterpret_cast<std::uintptr_t>(get_abi(remote)),
		    static_cast<int>(remote.GetPlaybackInfo().PlaybackStatus()));
	} catch (...) {
		return "disconnected";
	}
}
#endif
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
		co_await winrt::resume_background();
		const auto manager = co_await Manager::RequestAsync();
		const auto self = weak.lock();
		if (!self || self->m_closed) {
			co_return;
		}
		std::lock_guard lock(self->m_mutex);
		if (self->m_closed) {
			co_return;
		}
		self->m_manager = manager;
		self->m_sessionsChanged =
		    manager.SessionsChanged(winrt::auto_revoke, [weak](auto&&, auto&&) {
			    request(weak, Event::Sessions);
		    });
		self->m_currentChanged = manager.CurrentSessionChanged(
		    winrt::auto_revoke,
		    [weak](auto&&, auto&&) { request(weak, Event::Current); });
		self->refresh(Event::Open, nullptr, 0);
	} catch (...) {
		diagnostics::write(diagnostics::Level::Warning,
		                   "Windows media sessions are unavailable: " +
		                       winrt::to_string(winrt::to_message()));
	}
}
void Session::close() noexcept {
	if (!m_closed.exchange(true)) {
		m_changed = {};
	}
	// The last background reader releases the broker and its auto revokers.
}
void Session::reload() {
	request(weak_from_this(), Event::Open);
}
winrt::fire_and_forget Session::request(std::weak_ptr<Session> weak,
                                        Event event, Remote source,
                                        std::int64_t when) {
	try {
		if (event == Event::Media) {
			if (const auto self = weak.lock()) {
				++self->m_mediaEpoch;
			}
		}
		if (!when) {
			when = winrt::clock::now().time_since_epoch().count();
		}
		co_await winrt::resume_background();
		if (const auto self = weak.lock(); self && !self->m_closed) {
			std::lock_guard lock(self->m_mutex);
			if (!self->m_closed && self->m_manager) {
				try {
					self->refresh(event, source, when);
				} catch (...) {
					self->m_remote = nullptr;
					++self->m_generation;
					++self->m_request;
					const auto art = self->m_work.artworkVersion + 1;
					self->m_work = {};
					self->m_work.artworkVersion = art;
					self->notify();
					throw;
				}
			}
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Warning,
		                   "Couldn't refresh Windows media sessions: " +
		                       winrt::to_string(winrt::to_message()));
	}
}
void Session::notify() noexcept {
	try {
		if (m_closed) {
			return;
		}
		const auto reply = std::make_shared<Snapshot>(m_work);
		m_queue.TryEnqueue(
		    [weak = weak_from_this(), reply, serial = ++m_published] {
			    const auto self = weak.lock();
			    if (!self || self->m_closed || serial <= self->m_received) {
				    return;
			    }
			    self->m_received = serial;
			    self->m_snapshot = *reply;
			    try {
				    if (self->m_changed) {
					    self->m_changed(self->m_snapshot);
				    }
			    } catch (...) {
				    diagnostics::write(diagnostics::Level::Warning,
				                       "Couldn't update the music pane");
			    }
		    });
	} catch (...) {
		diagnostics::write(diagnostics::Level::Warning,
		                   "Couldn't update the music pane");
	}
}
void Session::refresh(Event event, const Remote& source, std::int64_t when) {
	// Candidate watches observe other sessions; the followed handlers read
	// all three kinds of changes. Ignore the duplicate candidate callback.
	if ((event == Event::ObserveMedia || event == Event::ObservePlayback) &&
	    source == m_remote) {
		return;
	}
	const auto current = m_manager.GetCurrentSession();
#ifdef _DEBUG
	trace(std::format("refresh={} current={}", static_cast<int>(event),
	                  describe(current)));
#endif
	std::vector<Candidate> candidates;
	std::vector<Remote> remotes;
	std::vector<Info> infos;
	for (const auto& remote : m_manager.GetSessions()) {
#ifdef _DEBUG
		trace("session=" + describe(remote));
#endif
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
			auto watch =
			    std::find_if(m_watches.begin(), m_watches.end(),
			                 [&](const auto& w) { return w.remote == remote; });
			if (watch == m_watches.end()) {
				Watch added;
				added.remote = remote;
				const auto weak = weak_from_this();
				added.playback = remote.PlaybackInfoChanged(
				    winrt::auto_revoke, [weak](const Remote& sender, auto&&) {
					    request(weak, Event::ObservePlayback, sender);
				    });
				added.media = remote.MediaPropertiesChanged(
				    winrt::auto_revoke, [weak](const Remote& sender, auto&&) {
					    request(weak, Event::ObserveMedia, sender);
				    });
				const auto timeline = remote.GetTimelineProperties();
				if (timeline) {
					added.changed = (std::min)(
					    timeline.LastUpdatedTime().time_since_epoch().count(),
					    winrt::clock::now().time_since_epoch().count());
				}
				m_watches.push_back(std::move(added));
				watch = m_watches.end() - 1;
			}
			if (source == remote && event != Event::Timeline) {
				watch->changed = (std::max)(watch->changed, when);
			}
			candidates.push_back({player, playback(info.PlaybackStatus()),
			                      remote == current, remote == m_remote,
			                      watch->changed});
			remotes.push_back(remote);
			infos.push_back(info);
		} catch (...) {
			// A player may disconnect during enumeration.
		}
	}
	std::erase_if(m_watches, [&](const auto& w) {
		return std::find(remotes.begin(), remotes.end(), w.remote) ==
		       remotes.end();
	});
	const auto selected = choose(candidates);
	const Remote next = selected ? remotes[*selected] : nullptr;
	const bool switched = next != m_remote;
	if (switched) {
		m_playbackChanged.revoke();
		m_mediaChanged.revoke();
		m_timelineChanged.revoke();
		++m_generation;
		++m_request;
		const auto art = m_work.artworkVersion + 1;
		const auto track = m_work.trackVersion + 1;
		m_work = {};
		m_work.artworkVersion = art;
		m_work.trackVersion = track;
		m_remote = next;
		m_timelineFloor = m_mediaFloor = 0;
		m_title.clear();
		m_artist.clear();
		m_metadataRequested = false;
		if (next) {
			const auto weak = weak_from_this();
			m_playbackChanged = next.PlaybackInfoChanged(
			    winrt::auto_revoke, [weak](const Remote& sender, auto&&) {
				    request(weak, Event::Playback, sender);
			    });
			m_mediaChanged = next.MediaPropertiesChanged(
			    winrt::auto_revoke, [weak](const Remote& sender, auto&&) {
				    request(weak, Event::Media, sender);
			    });
			m_timelineChanged = next.TimelinePropertiesChanged(
			    winrt::auto_revoke, [weak](const Remote& sender, auto&&) {
				    request(weak, Event::Timeline, sender);
			    });
		}
	}
#ifdef _DEBUG
	trace("selected=" + describe(next));
#endif
	if (selected) {
		m_work.state.player = candidates[*selected].player;
		readPlayback(infos[*selected]);
		const bool media =
		    event == Event::Media || event == Event::ObserveMedia;
		if (media && source == next && !switched) {
			m_mediaFloor = m_work.state.timeline.updated;
		} else if (event == Event::Open) {
			m_mediaFloor = m_timelineFloor = 0;
		}
		readTimeline();
		if (!m_metadataRequested || event == Event::Open || media) {
			refreshMedia(media || switched);
		}
	}
	notify();
}
} // namespace music
