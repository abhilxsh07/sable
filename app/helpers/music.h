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

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace music {
enum class Player { None, Spotify, AppleMusic };
enum class Playback { Inactive, Paused, Playing };
enum class Repeat { Off, All, One };
enum class Command { Previous, PlayPause, Next, Seek, Shuffle, Repeat };

Player identify(std::string_view appId);
std::string_view playerName(Player player);

struct Candidate {
	Player player = Player::None;
	Playback playback = Playback::Inactive;
	bool current = false;
	bool previous = false;
	std::int64_t changed = 0;
};
// Playing wins, then recent media/playback activity. Windows current and
// the previous target only break exact ties.
std::optional<std::size_t> choose(std::span<const Candidate> sessions);

struct Timeline {
	// Windows media-control timestamps use 100 ns ticks.
	std::int64_t start = 0, end = 0, position = 0;
	std::int64_t minSeek = 0, maxSeek = 0;
	double rate = 1;
	bool playing = false;
	std::int64_t updated = 0;
};
constexpr std::int64_t TICKS_PER_SECOND = 10'000'000;
std::int64_t position(const Timeline& timeline, double elapsedSeconds);
std::int64_t positionAt(const Timeline& timeline, std::int64_t now);
Timeline changePlayback(Timeline timeline, double elapsedSeconds, bool playing,
                        double rate);
struct SeekRequest {
	std::int64_t position = 0, at = 0;
	std::uint64_t track = 0;
};
// A successful command is not a timeline acknowledgement.
bool awaitingSeek(const SeekRequest& request, const Timeline& sampled,
                  std::int64_t now, std::uint64_t track, bool failed = false);
std::optional<std::int64_t> seekPosition(const Timeline& timeline,
                                         double secondsFromStart);
std::string timeText(std::int64_t ticks);

struct Controls {
	bool previous = false, play = false, pause = false, toggle = false;
	bool next = false, seek = false, shuffle = false, repeat = false;
};
struct State {
	Player player = Player::None;
	Playback playback = Playback::Inactive;
	std::string title, artist, error;
	Controls controls;
	Timeline timeline;
	std::optional<bool> shuffle;
	std::optional<Repeat> repeat;
	bool busy = false;
	bool visible() const;
};
bool enabled(const State& state, Command command);
Repeat nextRepeat(Repeat repeat);
// what a refresh does with the track already on show
enum class MediaRefresh {
	None,   // nothing changed that needs the metadata again
	Reread, // read it again, keeping what's shown until the reply
	Clear,  // a different track or player: drop it and read the new one
};
MediaRefresh mediaRefresh(bool requested, bool reopened, bool mediaChanged,
                          bool switched);

// Average the small decoded BGRA artwork sample, ignoring transparent
// pixels. Limit channel brightness so white text remains legible.
struct Tint {
	std::uint8_t r = 45, g = 45, b = 45;
	bool operator==(const Tint&) const = default;
};
Tint artworkTint(std::span<const std::uint8_t> bgra);
} // namespace music
