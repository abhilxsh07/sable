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
#include <random>

namespace night_run {
inline constexpr double FIELD_WIDTH = 640;
inline constexpr double FIELD_HEIGHT = 260;
inline constexpr double GROUND = 210;
inline constexpr double RUNNER_X = 92;
inline constexpr double RUNNER_WIDTH = 24;
inline constexpr double RUNNER_HEIGHT = 34;
inline constexpr double OBSTACLE_WIDTH = 26;
inline constexpr int TICKS_PER_SECOND = 60;

enum class Phase { Ready, Running, Paused, Crashed };

class Run {
  public:
	explicit Run(unsigned seed) : m_random(seed) {}
	void start();
	void pause();
	void resume();
	bool jump();
	bool tick();

	Phase phase() const {
		return m_phase;
	}
	double lift() const {
		return m_lift;
	}
	double obstacleX() const {
		return m_obstacleX;
	}
	double obstacleHeight() const {
		return m_obstacleHeight;
	}
	std::uint64_t score() const {
		return static_cast<std::uint64_t>(m_distance / 10);
	}

  private:
	std::minstd_rand m_random;
	Phase m_phase = Phase::Ready;
	double m_lift = 0;
	double m_velocity = 0;
	double m_distance = 0;
	double m_obstacleX = 500;
	double m_obstacleHeight = 32;
};
} // namespace night_run
