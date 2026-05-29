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
#include "night_run.h"

#include <algorithm>

namespace night_run {
void Run::start() {
	m_phase = Phase::Running;
	m_lift = 0;
	m_velocity = 0;
	m_distance = 0;
	m_obstacleX = 500;
	m_obstacleHeight = 32;
}

void Run::pause() {
	if (m_phase == Phase::Running) {
		m_phase = Phase::Paused;
	}
}

void Run::resume() {
	if (m_phase == Phase::Paused) {
		m_phase = Phase::Running;
	}
}

bool Run::jump() {
	if (m_phase != Phase::Running || m_lift != 0 || m_velocity != 0) {
		return false;
	}
	m_velocity = 480;
	return true;
}

bool Run::tick() {
	if (m_phase != Phase::Running) {
		return false;
	}
	constexpr double step = 1.0 / TICKS_PER_SECOND;
	m_lift += m_velocity * step;
	if (m_lift > 0) {
		m_velocity -= 1450 * step;
	} else {
		m_lift = 0;
		m_velocity = 0;
	}

	const double travel = std::min(520.0, 260 + m_distance / 48) * step;
	m_distance += travel;
	m_obstacleX -= travel;
	if (m_obstacleX < RUNNER_X + RUNNER_WIDTH &&
	    m_obstacleX + OBSTACLE_WIDTH > RUNNER_X && m_lift < m_obstacleHeight) {
		m_phase = Phase::Crashed;
		return true;
	}
	if (m_obstacleX + OBSTACLE_WIDTH <= 0) {
		m_obstacleX = FIELD_WIDTH + static_cast<double>(m_random() % 80);
		m_obstacleHeight = 26 + static_cast<double>(m_random() % 17);
	}
	return false;
}
} // namespace night_run
