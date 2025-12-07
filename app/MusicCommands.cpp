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
void Session::command(Command command, double seekSeconds) {
	if (m_closed || !enabled(m_snapshot.state, command)) {
		return;
	}

	send(weak_from_this(), command, seekSeconds);
}
winrt::fire_and_forget Session::send(std::weak_ptr<Session> weak,
                                     Command command, double seekSeconds) {
	bool accepted = false;
	Remote remote{nullptr};
	std::uint64_t generation = 0, track = 0;
	try {
		co_await winrt::resume_background();
		// Release the owner before the remote operation suspends.
		winrt::Windows::Foundation::IAsyncOperation<bool> operation{nullptr};
		{
			const auto self = weak.lock();
			if (!self || self->m_closed) {
				co_return;
			}
			std::lock_guard lock(self->m_mutex);
			if (self->m_closed || !self->m_remote ||
			    !enabled(self->m_work.state, command)) {
				co_return;
			}
			remote = self->m_remote;
			generation = self->m_generation;
			track = self->m_work.trackVersion;
			self->m_work.state.busy = true;
			self->m_work.state.error.clear();
			self->notify();
			const auto& state = self->m_work.state;
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
	if (const auto self = weak.lock(); self && !self->m_closed) {
		std::lock_guard lock(self->m_mutex);
		if (self->m_closed || self->m_generation != generation ||
		    self->m_work.trackVersion != track) {
			co_return;
		}
		self->m_work.state.busy = false;
		self->m_work.state.error =
		    accepted ? "" : "The player couldn't complete that request.";
		try {
			self->readPlayback(remote.GetPlaybackInfo());
			self->readTimeline();
		} catch (...) {
		}
		self->notify();
	}
}
} // namespace music
