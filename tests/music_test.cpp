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
#include "../app/helpers/music.h"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>

namespace {
using namespace music;
constexpr auto second = TICKS_PER_SECOND;
Timeline track() {
	return {10 * second, 130 * second, 40 * second, 20 * second, 120 * second,
	        1,           true};
}
TEST(Music, RecognizesOnlySupportedPlayerIds) {
	EXPECT_EQ(identify("Spotify.exe"), Player::Spotify);
	EXPECT_EQ(identify("SPOTIFY"), Player::Spotify);
	EXPECT_EQ(identify("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"),
	          Player::Spotify);
	EXPECT_EQ(identify("AppleInc.AppleMusicWin_nzyj5cx40ttqa!App"),
	          Player::AppleMusic);
	EXPECT_EQ(identify("AppleMusic.exe"), Player::AppleMusic);
	for (const auto id : {"", "chrome.exe", "VLC.exe", "spotify.com",
	                      "NotSpotify.exe", "Spotify.exe.browser", "iTunes.exe",
	                      "AppleInc.AppleMusicWin_unrelated!App"}) {
		EXPECT_EQ(identify(id), Player::None) << id;
	}
}
TEST(Music, ChoosesPlayingBeforeCurrentWithStableTie) {
	std::array candidates{
	    Candidate{Player::None, Playback::Playing, true},
	    Candidate{Player::Spotify, Playback::Paused, false, true},
	    Candidate{Player::AppleMusic, Playback::Playing}};
	EXPECT_EQ(choose(candidates), 2u);
	candidates[1].current = true;
	EXPECT_EQ(choose(candidates), 2u);
	candidates[1].current = false;
	candidates[1].playback = Playback::Playing;
	EXPECT_EQ(choose(candidates), 1u);
	candidates[1].playback = Playback::Inactive;
	candidates[2].playback = Playback::Inactive;
	EXPECT_FALSE(choose(candidates));
	EXPECT_FALSE(choose({}));
}
TEST(Music, ChoosesRecentMediaActivityOverAStalePlayingSession) {
	std::array candidates{
	    Candidate{Player::Spotify, Playback::Playing, true, true, 10},
	    Candidate{Player::Spotify, Playback::Playing, false, false, 20},
	    Candidate{Player::AppleMusic, Playback::Paused, true, false, 30}};
	EXPECT_EQ(choose(candidates), 1u);
	candidates[2].playback = Playback::Playing;
	EXPECT_EQ(choose(candidates), 2u);
	candidates[2].playback = Playback::Inactive;
	candidates[0].playback = Playback::Paused;
	EXPECT_EQ(choose(candidates), 1u);
	candidates[1].playback = Playback::Paused;
	EXPECT_EQ(choose(candidates), 1u);
}
TEST(Music, TimelineUsesTheReportedUtcSampleWithoutDoubleAdvancing) {
	auto t = track();
	t.updated = 100 * second;
	EXPECT_EQ(positionAt(t, 120 * second), 60 * second);
	EXPECT_EQ(positionAt(t, 120 * second), 60 * second);
	t.rate = 1.5;
	EXPECT_EQ(positionAt(t, 120 * second), 70 * second);
	EXPECT_EQ(positionAt(t, 200 * second), t.end);
	t.playing = false;
	EXPECT_EQ(positionAt(t, 120 * second), t.position);
	t.updated = 0;
	t.playing = true;
	EXPECT_EQ(positionAt(t, 120 * second), t.position);
	t.updated = 130 * second;
	EXPECT_EQ(positionAt(t, 120 * second), t.position);
}
TEST(Music, PauseResumeAndRateChangesKeepTheDisplayedClock) {
	auto t = track();
	t = changePlayback(t, 5, false, 1);
	EXPECT_EQ(position(t, 100), 45 * second);
	t = changePlayback(t, 100, true, 1);
	EXPECT_EQ(position(t, 2), 47 * second);
	t = changePlayback(t, 2, true, 1.5);
	EXPECT_EQ(position(t, 2), 50 * second);
	t = changePlayback(t, 2, false, 1.5);
	EXPECT_EQ(position(t, 100), 50 * second);
}
TEST(Music, SeekWaitsForANewTimelineNearTheTargetOrFailure) {
	auto t = track();
	SeekRequest request{90 * second, 100 * second, 5};
	t.updated = 99 * second;
	EXPECT_TRUE(awaitingSeek(request, t, 101 * second, 5));
	t.updated = 101 * second;
	EXPECT_TRUE(awaitingSeek(request, t, 101 * second, 5));
	t.position = 91 * second;
	EXPECT_FALSE(awaitingSeek(request, t, 101 * second, 5));
	t.position = 40 * second;
	EXPECT_FALSE(awaitingSeek(request, t, 106 * second, 5));
	EXPECT_FALSE(awaitingSeek(request, t, 101 * second, 6));
	EXPECT_FALSE(awaitingSeek(request, t, 101 * second, 5, true));
	t.playing = false;
	t.position = 90 * second;
	EXPECT_FALSE(awaitingSeek(request, t, 101 * second, 5));
}
TEST(Music, ExtrapolatesPlayingAndClampsTimestamps) {
	auto t = track();
	EXPECT_EQ(position(t, 2), 42 * second);
	t.rate = 2;
	EXPECT_EQ(position(t, 2), 44 * second);
	EXPECT_EQ(position(t, 1e300), t.end);
	t.playing = false;
	EXPECT_EQ(position(t, 200), t.position);
	t.position = -1;
	EXPECT_EQ(position(t, 2), t.start);
	t.end = t.start;
	EXPECT_EQ(position(t, 2), 0);
}
TEST(Music, InvalidClockOrRateDoesNotAdvance) {
	auto t = track();
	for (const auto bad : {-1., std::numeric_limits<double>::infinity(),
	                       std::numeric_limits<double>::quiet_NaN()}) {
		EXPECT_EQ(position(t, bad), t.position);
		t.rate = bad;
		EXPECT_EQ(position(t, 20), t.position);
		t.rate = 1;
	}
}
TEST(Music, SeeksRelativeToStartWithinReportedSeekRange) {
	auto t = track();
	EXPECT_EQ(seekPosition(t, 35), 45 * second);
	EXPECT_EQ(seekPosition(t, -100), t.minSeek);
	EXPECT_EQ(seekPosition(t, 1e300), t.maxSeek);
	EXPECT_FALSE(seekPosition(t, std::numeric_limits<double>::quiet_NaN()));
	t.maxSeek = t.minSeek;
	EXPECT_FALSE(seekPosition(t, 20));
	t = track();
	t.minSeek = t.end + 1;
	EXPECT_FALSE(seekPosition(t, 20));
}
TEST(Music, FormatsElapsedAndRemaining) {
	EXPECT_EQ(timeText(-1), "0:00");
	EXPECT_EQ(timeText(65 * second), "1:05");
	EXPECT_EQ(timeText(3661 * second), "1:01:01");
}
TEST(Music, GatesCommandsOnCapabilitiesAndKnownState) {
	State s;
	s.player = Player::Spotify;
	s.playback = Playback::Playing;
	s.controls = {true, true, true, false, true, true, true, true};
	s.timeline = track();
	EXPECT_TRUE(enabled(s, Command::PlayPause));
	EXPECT_TRUE(enabled(s, Command::Previous));
	EXPECT_TRUE(enabled(s, Command::Next));
	EXPECT_TRUE(enabled(s, Command::Seek));
	EXPECT_FALSE(enabled(s, Command::Shuffle));
	EXPECT_FALSE(enabled(s, Command::Repeat));
	s.shuffle = false;
	s.repeat = Repeat::Off;
	EXPECT_TRUE(enabled(s, Command::Shuffle));
	EXPECT_TRUE(enabled(s, Command::Repeat));
	s.controls.pause = false;
	EXPECT_FALSE(enabled(s, Command::PlayPause));
	s.playback = Playback::Paused;
	EXPECT_TRUE(enabled(s, Command::PlayPause));
	s.busy = true;
	for (const auto command :
	     {Command::Previous, Command::PlayPause, Command::Next, Command::Seek,
	      Command::Shuffle, Command::Repeat}) {
		EXPECT_FALSE(enabled(s, command));
	}
	s.busy = false;
	s.playback = Playback::Inactive;
	EXPECT_FALSE(s.visible());
	EXPECT_FALSE(enabled(s, Command::PlayPause));
}
// A-42: opening the pane read the track again and showed "Unknown track"
// until the reply came
TEST(Music, ReopeningKeepsTheTrackOnShow) {
	EXPECT_EQ(mediaRefresh(true, true, false, false), MediaRefresh::Reread);
	EXPECT_EQ(mediaRefresh(true, false, false, false), MediaRefresh::None);
	EXPECT_EQ(mediaRefresh(true, true, true, false), MediaRefresh::Clear);
	EXPECT_EQ(mediaRefresh(true, false, true, false), MediaRefresh::Clear);
	EXPECT_EQ(mediaRefresh(true, true, false, true), MediaRefresh::Clear);
	EXPECT_EQ(mediaRefresh(false, false, false, false), MediaRefresh::Clear);
}
TEST(Music, CyclesRepeat) {
	EXPECT_EQ(nextRepeat(Repeat::Off), Repeat::All);
	EXPECT_EQ(nextRepeat(Repeat::All), Repeat::One);
	EXPECT_EQ(nextRepeat(Repeat::One), Repeat::Off);
}
TEST(Music, ArtworkTintIgnoresTransparencyAndKeepsWhiteReadable) {
	EXPECT_EQ(artworkTint({}), Tint{});
	const std::array<std::uint8_t, 8> pixels{0, 0, 255, 255, 255, 0, 0, 0};
	EXPECT_EQ(artworkTint(pixels), (Tint{80, 0, 0}));
	const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
	EXPECT_EQ(artworkTint(white), (Tint{80, 80, 80}));
	const std::array<std::uint8_t, 4> transparent{0, 255, 0, 0};
	EXPECT_EQ(artworkTint(transparent), Tint{});
	EXPECT_EQ(artworkTint(std::span(white).first(3)), Tint{});
}
TEST(Music, BrightDarkAndGreyscaleArtworkKeepTitleContrast) {
	const auto linear = [](double channel) {
		channel /= 255.;
		return channel <= 0.04045 ? channel / 12.92
		                          : std::pow((channel + 0.055) / 1.055, 2.4);
	};
	for (const auto input : {std::array<std::uint8_t, 4>{250, 252, 255, 255},
	                         std::array<std::uint8_t, 4>{1, 2, 3, 255},
	                         std::array<std::uint8_t, 4>{128, 128, 128, 255},
	                         std::array<std::uint8_t, 4>{255, 255, 255, 255}}) {
		const auto tint = artworkTint(input);
		const auto luminance = 0.2126 * linear(tint.r) +
		                       0.7152 * linear(tint.g) +
		                       0.0722 * linear(tint.b);
		EXPECT_GE(1.05 / (luminance + 0.05), 4.5);
		EXPECT_LE(tint.r, 80);
		EXPECT_LE(tint.g, 80);
		EXPECT_LE(tint.b, 80);
		if (input[0] == input[1] && input[1] == input[2]) {
			EXPECT_EQ(tint.r, tint.g);
			EXPECT_EQ(tint.g, tint.b);
		}
	}
}
} // namespace
