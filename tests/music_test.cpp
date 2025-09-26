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
TEST(Music, ChoosesSupportedCurrentThenPlayingWithStableTie) {
	std::array candidates{
	    Candidate{Player::None, Playback::Playing, true},
	    Candidate{Player::Spotify, Playback::Paused, false, true},
	    Candidate{Player::AppleMusic, Playback::Playing}};
	EXPECT_EQ(choose(candidates), 2u);
	candidates[1].current = true;
	EXPECT_EQ(choose(candidates), 1u);
	candidates[1].current = false;
	candidates[1].playback = Playback::Playing;
	EXPECT_EQ(choose(candidates), 1u);
	candidates[1].playback = Playback::Inactive;
	candidates[2].playback = Playback::Inactive;
	EXPECT_FALSE(choose(candidates));
	EXPECT_FALSE(choose({}));
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
} // namespace
