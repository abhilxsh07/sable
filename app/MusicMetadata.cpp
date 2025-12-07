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
#include <winrt/Windows.Media.h>
namespace music {
void Session::readPlayback(const Info& info) {
	auto& state = m_work.state;
	using Status = winrt::Windows::Media::Control::
	    GlobalSystemMediaTransportControlsSessionPlaybackStatus;
	state.playback =
	    info.PlaybackStatus() == Status::Playing  ? Playback::Playing
	    : info.PlaybackStatus() == Status::Paused ? Playback::Paused
	                                              : Playback::Inactive;
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
	const double nextRate = rate ? rate.Value() : 1;
	const bool playing = state.playback == Playback::Playing;
	if (state.timeline.rate != nextRate || state.timeline.playing != playing) {
		const auto now = std::chrono::steady_clock::now();
		state.timeline = changePlayback(
		    state.timeline,
		    std::chrono::duration<double>(now - m_work.sampledAt).count(),
		    playing, nextRate);
		m_work.sampledAt = now;
	}
}

void Session::readTimeline() {
	if (!m_remote) {
		return;
	}
	const auto sample = m_remote.GetTimelineProperties();
	auto& t = m_work.state.timeline;
	if (!sample) {
		return;
	}
	const auto updated = sample.LastUpdatedTime().time_since_epoch().count();
	if (m_timelineFloor && updated <= m_timelineFloor) {
		return;
	}
	// Playback/rate changes rebase the clock. Reading the same broker sample
	// again would undo a pause or count time spent paused when resuming.
	if (updated > 0 && updated == t.updated) {
		return;
	}
	m_timelineFloor = 0;
	t.start = sample.StartTime().count();
	t.end = sample.EndTime().count();
	t.minSeek = sample.MinSeekTime().count();
	t.maxSeek = sample.MaxSeekTime().count();
	t.position = sample.Position().count();
	t.updated = updated;
	// see:
	// https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessiontimelineproperties.lastupdatedtime?view=winrt-28000
	t.position = positionAt(t, winrt::clock::now().time_since_epoch().count());
	m_work.sampledAt = std::chrono::steady_clock::now();
}
void Session::refreshMedia(bool trackChanged) {
	m_metadataRequested = true;
	m_work.state.title.clear();
	m_work.state.artist.clear();
	m_work.artwork.reset();
	++m_work.artworkVersion;
	if (trackChanged) {
		m_work.state.busy = false;
	}
	notify();
	readMedia(weak_from_this(), m_remote, m_generation, ++m_request,
	          m_mediaEpoch.load());
}
winrt::fire_and_forget Session::readMedia(std::weak_ptr<Session> weak,
                                          Remote remote,
                                          std::uint64_t generation,
                                          std::uint64_t request,
                                          std::uint64_t epoch) {
	try {
		co_await winrt::resume_background();
		const auto media = co_await remote.TryGetMediaPropertiesAsync();
		const auto title = media ? winrt::to_string(media.Title()) : "";
		const auto artist = media ? winrt::to_string(media.Artist()) : "";
		const auto art = media ? media.Thumbnail() : nullptr;
		{
			const auto self = weak.lock();
			if (!self || self->m_closed) {
				co_return;
			}
			std::lock_guard lock(self->m_mutex);
			if (self->m_closed || self->m_generation != generation ||
			    self->m_request != request || self->m_mediaEpoch != epoch) {
				co_return;
			}
			if (title != self->m_title || artist != self->m_artist) {
				++self->m_work.trackVersion;
				self->m_title = title;
				self->m_artist = artist;
				if (self->m_mediaFloor &&
				    self->m_work.state.timeline.updated <= self->m_mediaFloor) {
					self->m_timelineFloor = self->m_mediaFloor;
					auto& timeline = self->m_work.state.timeline;
					const auto rate = timeline.rate;
					const auto playing = timeline.playing;
					timeline = {};
					timeline.rate = rate;
					timeline.playing = playing;
					self->m_work.sampledAt = std::chrono::steady_clock::now();
				}
			}
			self->m_work.state.title = title;
			self->m_work.state.artist = artist;
#ifdef _DEBUG
			diagnostics::write(
			    diagnostics::Level::Info,
			    "Music: source=" +
			        winrt::to_string(remote.SourceAppUserModelId()) +
			        " track=" + title + " artist=" + artist +
			        " generation=" + std::to_string(generation) +
			        " request=" + std::to_string(request));
#endif
			self->notify();
		}
		if (!art) {
			co_return;
		}
		const auto stream = co_await art.OpenReadAsync();
		if (!stream || stream.Size() > 8 * 1024 * 1024) {
			co_return;
		}
		winrt::Windows::Storage::Streams::DataReader reader(stream);
		const auto size = static_cast<std::uint32_t>(stream.Size());
		if (co_await reader.LoadAsync(size) != size) {
			co_return;
		}
		auto bytes = std::make_shared<std::vector<std::uint8_t>>(size);
		reader.ReadBytes(*bytes);
		const auto self = weak.lock();
		if (!self || self->m_closed) {
			co_return;
		}
		std::lock_guard lock(self->m_mutex);
		if (self->m_closed || self->m_generation != generation ||
		    self->m_request != request || self->m_mediaEpoch != epoch) {
			co_return;
		}
		self->m_work.artwork = std::move(bytes);
		++self->m_work.artworkVersion;
		self->notify();
	} catch (...) {
		diagnostics::write(diagnostics::Level::Debug,
		                   "Couldn't read music metadata or artwork");
	}
}
} // namespace music
