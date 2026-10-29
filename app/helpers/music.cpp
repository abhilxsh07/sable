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
#include "music.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <tuple>

namespace music {
Player identify(std::string_view appId) {
	std::string id(appId);
	for (auto& c : id) {
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c + ('a' - 'A'));
		}
	}
	if (id == "spotify.exe" || id == "spotify" ||
	    id == "spotifyab.spotifymusic_zpdnekdrzrea0!spotify") {
		return Player::Spotify;
	}
	if (id == "applemusic.exe" ||
	    id == "appleinc.applemusicwin_nzyj5cx40ttqa!app") {
		return Player::AppleMusic;
	}
	return Player::None;
}
std::string_view playerName(Player player) {
	switch (player) {
	case Player::Spotify:
		return "Spotify";
	case Player::AppleMusic:
		return "Apple Music";
	default:
		return "";
	}
}
std::optional<std::size_t> choose(std::span<const Candidate> sessions) {
	std::optional<std::size_t> best;
	std::tuple<bool, std::int64_t, bool, bool> score{};
	for (std::size_t i = 0; i < sessions.size(); ++i) {
		const auto& session = sessions[i];
		if (session.player == Player::None ||
		    session.playback == Playback::Inactive) {
			continue;
		}
		const auto rank =
		    std::tuple{session.playback == Playback::Playing, session.changed,
		               session.current, session.previous};
		if (!best || rank > score) {
			score = rank;
			best = i;
		}
	}
	return best;
}
namespace {
bool valid(const Timeline& t) {
	return t.start >= 0 && t.end > t.start;
}
std::int64_t clampTick(long double value, std::int64_t low, std::int64_t high) {
	if (value <= low) {
		return low;
	}
	if (value >= high) {
		return high;
	}
	return static_cast<std::int64_t>(value);
}
} // namespace
std::int64_t position(const Timeline& t, double elapsedSeconds) {
	if (!valid(t)) {
		return 0;
	}
	long double value = static_cast<long double>(t.position);
	if (t.playing && std::isfinite(t.rate) && t.rate >= 0 &&
	    std::isfinite(elapsedSeconds) && elapsedSeconds > 0) {
		value += static_cast<long double>(elapsedSeconds) * t.rate *
		         TICKS_PER_SECOND;
	}
	return clampTick(value, t.start, t.end);
}
std::int64_t positionAt(const Timeline& t, std::int64_t now) {
	const double elapsed =
	    t.updated > 0 && now > t.updated
	        ? static_cast<double>(now - t.updated) / TICKS_PER_SECOND
	        : 0;
	return position(t, elapsed);
}
Timeline changePlayback(Timeline timeline, double elapsedSeconds, bool playing,
                        double rate) {
	timeline.position = position(timeline, elapsedSeconds);
	timeline.playing = playing;
	timeline.rate = rate;
	return timeline;
}
bool awaitingSeek(const SeekRequest& request, const Timeline& sampled,
                  std::int64_t now, std::uint64_t track, bool failed) {
	if (failed || track != request.track || now < request.at ||
	    static_cast<long double>(now) - request.at >= 5 * TICKS_PER_SECOND) {
		return false;
	}
	if (sampled.updated < request.at) {
		return true;
	}
	auto expected = sampled;
	expected.position = request.position;
	const double elapsed =
	    static_cast<double>(now - request.at) / TICKS_PER_SECOND;
	return std::abs(static_cast<long double>(sampled.position) -
	                position(expected, elapsed)) > 2 * TICKS_PER_SECOND;
}
std::optional<std::int64_t> seekPosition(const Timeline& t, double seconds) {
	if (!valid(t) || !std::isfinite(seconds)) {
		return {};
	}
	const auto low = std::max(t.start, t.minSeek);
	const auto high = std::min(t.end, t.maxSeek);
	if (high <= low) {
		return {};
	}
	return clampTick(static_cast<long double>(t.start) +
	                     static_cast<long double>(seconds) * TICKS_PER_SECOND,
	                 low, high);
}
std::string timeText(std::int64_t ticks) {
	const auto seconds = std::max<std::int64_t>(0, ticks) / TICKS_PER_SECOND;
	if (seconds >= 3600) {
		return std::format("{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60,
		                   seconds % 60);
	}
	return std::format("{}:{:02}", seconds / 60, seconds % 60);
}
bool State::visible() const {
	return player != Player::None && playback != Playback::Inactive;
}
bool enabled(const State& state, Command command) {
	if (!state.visible() || state.busy) {
		return false;
	}
	const auto& c = state.controls;
	switch (command) {
	case Command::Previous:
		return c.previous;
	case Command::PlayPause:
		return c.toggle ||
		       (state.playback == Playback::Playing ? c.pause : c.play);
	case Command::Next:
		return c.next;
	case Command::Seek:
		return c.seek && seekPosition(state.timeline, 0).has_value();
	case Command::Shuffle:
		return c.shuffle && state.shuffle.has_value();
	case Command::Repeat:
		return c.repeat && state.repeat.has_value();
	}
	return false;
}
MediaRefresh mediaRefresh(bool requested, bool reopened, bool mediaChanged,
                          bool switched) {
	if (!requested || mediaChanged || switched) {
		return MediaRefresh::Clear;
	}
	// the same track as before: what's on show stays until the reply
	return reopened ? MediaRefresh::Reread : MediaRefresh::None;
}
Repeat nextRepeat(Repeat repeat) {
	switch (repeat) {
	case Repeat::Off:
		return Repeat::All;
	case Repeat::All:
		return Repeat::One;
	default:
		return Repeat::Off;
	}
}
Tint artworkTint(std::span<const std::uint8_t> pixels) {
	if (pixels.empty() || pixels.size() % 4) {
		return {};
	}
	std::uint64_t r = 0, g = 0, b = 0, weight = 0;
	for (std::size_t i = 0; i < pixels.size(); i += 4) {
		const auto a = pixels[i + 3];
		r += static_cast<std::uint64_t>(pixels[i + 2]) * a;
		g += static_cast<std::uint64_t>(pixels[i + 1]) * a;
		b += static_cast<std::uint64_t>(pixels[i]) * a;
		weight += a;
	}
	if (!weight) {
		return {};
	}
	const double red = static_cast<double>(r) / weight;
	const double green = static_cast<double>(g) / weight;
	const double blue = static_cast<double>(b) / weight;
	const double scale = 80.0 / std::max({80.0, red, green, blue});
	return {static_cast<std::uint8_t>(red * scale),
	        static_cast<std::uint8_t>(green * scale),
	        static_cast<std::uint8_t>(blue * scale)};
}
} // namespace music
