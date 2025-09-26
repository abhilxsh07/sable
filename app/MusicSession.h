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
#include <functional>
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

namespace music {
struct Snapshot {
	State state;
	winrt::Windows::Storage::Streams::IRandomAccessStreamReference artwork{
	    nullptr};
	std::uint64_t artworkVersion = 0;
	std::chrono::steady_clock::time_point sampledAt =
	    std::chrono::steady_clock::now();
};
// Created, read and closed on the reader's dispatcher. System event callbacks
// only enqueue work; coroutines keep weak owners across remote calls.
class Session : public std::enable_shared_from_this<Session> {
  public:
	using Changed = std::function<void(const Snapshot&)>;
	static std::shared_ptr<Session>
	create(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
	       Changed changed);
	~Session();
	void close() noexcept;
	void command(Command command, double seekSeconds = 0);
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
	static winrt::fire_and_forget readMedia(std::weak_ptr<Session> weak,
	                                        Remote remote,
	                                        std::uint64_t generation,
	                                        std::uint64_t request);
	static winrt::fire_and_forget send(std::weak_ptr<Session> weak,
	                                   Remote remote, std::uint64_t generation,
	                                   Command command, double seekSeconds);
	void refresh() noexcept;
	void refreshMedia();
	void readPlayback(const Info& info);
	void readTimeline();
	void notify() noexcept;
	struct Watch {
		Remote remote{nullptr};
		Remote::PlaybackInfoChanged_revoker playback;
	};
	winrt::Microsoft::UI::Dispatching::DispatcherQueue m_queue{nullptr};
	Changed m_changed;
	Manager m_manager{nullptr};
	Remote m_remote{nullptr};
	Manager::SessionsChanged_revoker m_sessionsChanged;
	Manager::CurrentSessionChanged_revoker m_currentChanged;
	Remote::MediaPropertiesChanged_revoker m_mediaChanged;
	Remote::TimelinePropertiesChanged_revoker m_timelineChanged;
	std::vector<Watch> m_watches;
	Snapshot m_snapshot;
	std::uint64_t m_generation = 0, m_request = 0;
	bool m_closed = false;
	bool m_metadataRequested = false;
};
} // namespace music
