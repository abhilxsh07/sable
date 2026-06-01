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

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <random>

namespace night_run {
inline constexpr double FIELD_WIDTH = 640;
inline constexpr double FIELD_HEIGHT = 400;
inline constexpr double GROUND = 310;
inline constexpr double RUNNER_X = 92;
inline constexpr double RUNNER_WIDTH = 24;
inline constexpr double RUNNER_HEIGHT = 34;
inline constexpr double OBSTACLE_WIDTH = 26;
// a hop has to be this high to clear a staple or skim over ink
inline constexpr double STAPLE_HEIGHT = 18;
inline constexpr double INK_HEIGHT = 8;
// rows appear here, off the top of the track
inline constexpr double SPAWN_X = FIELD_WIDTH + 40;
// how far the bookworm is behind; it catches the runner at zero
inline constexpr double WORM_START = 100;
inline constexpr int TICKS_PER_SECOND = 60;

enum class Phase { Ready, Running, Paused, Crashed };
// what ended the run
enum class Ending { None, Rock, Staple, Worm };
// what sits in one lane of a row. Rocks can't be hopped, staples must be,
// ink only slows the board and pages float above the water.
enum class Cell : std::uint8_t { Empty, Rock, Staple, Ink, Page };

struct Row {
	double x = 0;
	std::array<Cell, 3> cells{};
	// the runner already touched this row's hazard or took its page
	bool hit = false;
	bool taken = false;
};

// what happened during the last tick, for sounds and the status line
enum Event : unsigned {
	EVENT_PAGE = 1,
	EVENT_INK = 2,
	EVENT_SHIELD_GAINED = 4,
	EVENT_SHIELD_LOST = 8,
};

class Run {
  public:
	explicit Run(unsigned seed) : m_random(seed) {}
	void start();
	void pause();
	void resume();
	bool jump();
	void steer(int direction);
	// true when the run ended during this tick
	bool tick();

	Phase phase() const {
		return m_phase;
	}
	Ending ending() const {
		return m_ending;
	}
	unsigned events() const {
		return m_events;
	}
	int lane() const {
		return m_lane;
	}
	double lift() const {
		return m_lift;
	}
	bool grounded() const {
		return m_lift == 0 && m_velocity == 0;
	}
	unsigned pages() const {
		return m_pages;
	}
	bool shield() const {
		return m_shield;
	}
	bool slowed() const {
		return m_slowTicks > 0;
	}
	double worm() const {
		return m_worm;
	}
	// 0 at the start, 1 once every pattern is in play at full pace
	double difficulty() const;
	// track units per second, after any ink slowdown
	double speed() const;
	const std::deque<Row>& rows() const {
		return m_rows;
	}
	// replaces the rows ahead; the generator carries on after the last one
	void layRows(std::deque<Row> rows);
	std::uint64_t score() const {
		return static_cast<std::uint64_t>(m_distance / 10);
	}

  private:
	void spawn(double x);
	void resolve(Row& row);
	void end(Ending ending);

	std::minstd_rand m_random;
	Phase m_phase = Phase::Ready;
	Ending m_ending = Ending::None;
	unsigned m_events = 0;
	int m_lane = 1;
	unsigned m_pages = 0;
	bool m_shield = false;
	int m_slowTicks = 0;
	double m_worm = WORM_START;
	double m_lift = 0;
	double m_velocity = 0;
	double m_distance = 0;
	// distance left before the next row appears at SPAWN_X
	double m_until = 0;
	std::deque<Row> m_rows;
};

// the best distance lives in its own small file next to config.json, so the
// settings file keeps the shape NightPDF wrote. A missing or broken file reads
// as 0; an empty path never touches the disk.
std::uint64_t loadBest(const std::filesystem::path& file) noexcept;
bool saveBest(const std::filesystem::path& file, std::uint64_t best) noexcept;
} // namespace night_run
