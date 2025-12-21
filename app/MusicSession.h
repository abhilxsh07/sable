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

#include "helpers/music.h"
#include <chrono>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

namespace music {
struct Snapshot {
	State state;
	std::shared_ptr<const std::vector<std::uint8_t>> artwork;
	std::uint64_t artworkVersion = 0, trackVersion = 0;
	std::chrono::steady_clock::time_point sampledAt =
	    std::chrono::steady_clock::now();
};
// Public methods run on the dispatcher. Broker reads and subscriptions run
// in the background under m_mutex; only plain snapshots reach the UI.
class Session : public std::enable_shared_from_this<Session> {
  public:
	using Changed = std::function<void(const Snapshot&)>;
	static std::shared_ptr<Session>
	create(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
	       Changed changed);
	~Session();
	void close() noexcept;
	void command(Command command, double seekSeconds = 0);
	void reload();
	const Snapshot& snapshot() const {
		return m_snapshot;
	}

  private:
	using Manager = winrt::Windows::Media::Control::
	    GlobalSystemMediaTransportControlsSessionManager;
	using Remote = winrt::Windows::Media::Control::
	    GlobalSystemMediaTransportControlsSession;
	using Info = winrt::Windows::Media::Control::
	    GlobalSystemMediaTransportControlsSessionPlaybackInfo;
	Session(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
	        Changed changed);
	static winrt::fire_and_forget start(std::weak_ptr<Session> weak);
	enum class Event {
		Open,
		Sessions,
		Current,
		Playback,
		Media,
		Timeline,
		ObservePlayback,
		ObserveMedia
	};
	static winrt::fire_and_forget request(std::weak_ptr<Session> weak,
	                                      Event event, Remote source = nullptr,
	                                      std::int64_t when = 0);
	static winrt::fire_and_forget readMedia(std::weak_ptr<Session> weak,
	                                        Remote remote,
	                                        std::uint64_t generation,
	                                        std::uint64_t request,
	                                        std::uint64_t epoch);
	static winrt::fire_and_forget send(std::weak_ptr<Session> weak,
	                                   Command command, double seekSeconds);
	void refresh(Event event, const Remote& source, std::int64_t when);
	void refreshMedia(bool trackChanged);
	void readPlayback(const Info& info);
	void readTimeline();
	void notify() noexcept;
	struct Watch {
		Remote remote{nullptr};
		Remote::PlaybackInfoChanged_revoker playback;
		Remote::MediaPropertiesChanged_revoker media;
		std::int64_t changed = 0;
	};
	winrt::Microsoft::UI::Dispatching::DispatcherQueue m_queue{nullptr};
	Changed m_changed;
	Manager m_manager{nullptr};
	Remote m_remote{nullptr};
	Manager::SessionsChanged_revoker m_sessionsChanged;
	Manager::CurrentSessionChanged_revoker m_currentChanged;
	Remote::PlaybackInfoChanged_revoker m_playbackChanged;
	Remote::MediaPropertiesChanged_revoker m_mediaChanged;
	Remote::TimelinePropertiesChanged_revoker m_timelineChanged;
	std::vector<Watch> m_watches;
	Snapshot m_snapshot, m_work;
	std::mutex m_mutex;
	std::uint64_t m_published = 0, m_received = 0;
	std::int64_t m_timelineFloor = 0, m_mediaFloor = 0;
	std::string m_title, m_artist;
	std::uint64_t m_generation = 0, m_request = 0;
	std::atomic<bool> m_closed{false};
	std::atomic<std::uint64_t> m_mediaEpoch{0};
	bool m_metadataRequested = false;
};
} // namespace music
