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
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

namespace night_run {
namespace {
constexpr double STEP = 1.0 / TICKS_PER_SECOND;
// rows are dropped once they are well past the bottom of the track
constexpr double GONE_X = -80;

// the worm creeps in on its own, faster as the run speeds up. Pages push it
// back; ink and a broken shield let it close in.
constexpr double WORM_CREEP = 2;
constexpr double WORM_CREEP_FAST = 7;
constexpr double WORM_PAGE = 7;
constexpr double WORM_INK = 15;
constexpr double WORM_SHIELD = 20;
constexpr int SLOW_TICKS = TICKS_PER_SECOND;

enum class Pattern {
	Rock,     // one rock, a page in another lane
	Staples,  // staples right across, a page above one of them
	RockInk,  // a rock, ink and a page, one each
	InkField, // ink in two lanes, a page in the third
	Wall,     // rocks in two lanes, a page in the gap
	ZigZag,   // two walls close together with neighbouring gaps
	RockHop,  // a rock and two staple lanes, a page over one of them
};

struct Choice {
	Pattern pattern;
	double from; // distance it starts showing up at
	unsigned weight;
};

constexpr Choice PATTERNS[] = {
    {Pattern::Rock, 0, 4},       {Pattern::Staples, 1500, 2},
    {Pattern::RockInk, 1500, 3}, {Pattern::InkField, 2500, 2},
    {Pattern::Wall, 3500, 3},    {Pattern::ZigZag, 4500, 2},
    {Pattern::RockHop, 6000, 2},
};
} // namespace

void Run::start() {
	m_phase = Phase::Running;
	m_ending = Ending::None;
	m_events = 0;
	m_lane = 1;
	m_pages = 0;
	m_shield = false;
	m_slowTicks = 0;
	m_worm = WORM_START;
	m_lift = 0;
	m_velocity = 0;
	m_distance = 0;
	m_rows.clear();
	spawn(520);
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

void Run::steer(int direction) {
	if (m_phase == Phase::Running) {
		m_lane = std::clamp(m_lane + std::clamp(direction, -1, 1), 0, 2);
	}
}

bool Run::jump() {
	if (m_phase != Phase::Running || !grounded()) {
		return false;
	}
	m_velocity = 480;
	return true;
}

double Run::difficulty() const {
	return (std::min)(1.0, m_distance / 9000);
}

double Run::speed() const {
	const double pace = (std::min)(520.0, 260 + m_distance / 48);
	return m_slowTicks > 0 ? pace * 0.6 : pace;
}

void Run::layRows(std::deque<Row> rows) {
	m_rows = std::move(rows);
	// leave at least a second before the generator adds its own rows
	const double last = m_rows.empty() ? 0 : m_rows.back().x;
	m_until = (std::max)(0.0, last - SPAWN_X) + speed();
}

void Run::spawn(double x) {
	const double hurry = 1.15 - 0.5 * difficulty();
	const double pace = (std::min)(520.0, 260 + m_distance / 48);
	unsigned total = 0;
	for (const auto& choice : PATTERNS) {
		if (m_distance >= choice.from) {
			total += choice.weight;
		}
	}
	auto pick = static_cast<unsigned>(m_random() % total);
	Pattern pattern = Pattern::Rock;
	for (const auto& choice : PATTERNS) {
		if (m_distance < choice.from) {
			continue;
		}
		if (pick < choice.weight) {
			pattern = choice.pattern;
			break;
		}
		pick -= choice.weight;
	}

	const int a = static_cast<int>(m_random() % 3);
	const int b = (a + 1 + static_cast<int>(m_random() % 2)) % 3;
	const int c = 3 - a - b;
	Row row{x};
	double last = x;
	// a hop takes about 0.66 s, so a staple row leaves room to land first
	double gap = pace * hurry + static_cast<double>(m_random() % 50);
	switch (pattern) {
	case Pattern::Rock:
		row.cells[a] = Cell::Rock;
		row.cells[b] = Cell::Page;
		break;
	case Pattern::Staples:
		row.cells = {Cell::Staple, Cell::Staple, Cell::Staple};
		row.cells[a] = Cell::Page;
		gap = (std::max)(gap, pace * 0.8);
		break;
	case Pattern::RockInk:
		row.cells[a] = Cell::Rock;
		row.cells[b] = Cell::Ink;
		row.cells[c] = Cell::Page;
		break;
	case Pattern::InkField:
		row.cells[a] = Cell::Ink;
		row.cells[b] = Cell::Ink;
		row.cells[c] = Cell::Page;
		break;
	case Pattern::Wall:
		row.cells[a] = Cell::Rock;
		row.cells[b] = Cell::Rock;
		row.cells[c] = Cell::Page;
		break;
	case Pattern::ZigZag: {
		// the second gap is always one lane over, one press away
		const int first = a;
		const int second = first == 1 ? (m_random() % 2 ? 0 : 2) : 1;
		row.cells = {Cell::Rock, Cell::Rock, Cell::Rock};
		row.cells[first] = Cell::Page;
		Row next{x + (std::max)(140.0, pace * 0.35)};
		next.cells = {Cell::Rock, Cell::Rock, Cell::Rock};
		next.cells[second] = Cell::Empty;
		m_rows.push_back(row);
		row = next;
		last = next.x;
		break;
	}
	case Pattern::RockHop:
		row.cells[a] = Cell::Rock;
		row.cells[b] = Cell::Page;
		row.cells[c] = Cell::Staple;
		gap = (std::max)(gap, pace * 0.8);
		break;
	}
	m_rows.push_back(row);
	m_until = last + gap - SPAWN_X;
}

void Run::end(Ending ending) {
	m_phase = Phase::Crashed;
	m_ending = ending;
}

void Run::resolve(Row& row) {
	const Cell cell = row.cells[static_cast<std::size_t>(m_lane)];
	if (cell == Cell::Page) {
		if (!row.taken) {
			row.taken = true;
			++m_pages;
			m_events |= EVENT_PAGE;
			m_worm = (std::min)(WORM_START, m_worm + WORM_PAGE);
			if (m_pages % 3 == 0 && !m_shield) {
				m_shield = true;
				m_events |= EVENT_SHIELD_GAINED;
			}
		}
		return;
	}
	if (row.hit) {
		return;
	}
	if (cell == Cell::Ink && m_lift < INK_HEIGHT) {
		row.hit = true;
		m_slowTicks = SLOW_TICKS;
		m_worm -= WORM_INK;
		m_events |= EVENT_INK;
		return;
	}
	const bool rock = cell == Cell::Rock;
	if (rock || (cell == Cell::Staple && m_lift < STAPLE_HEIGHT)) {
		row.hit = true;
		if (m_shield) {
			m_shield = false;
			m_worm -= WORM_SHIELD;
			m_events |= EVENT_SHIELD_LOST;
		} else {
			end(rock ? Ending::Rock : Ending::Staple);
		}
	}
}

bool Run::tick() {
	m_events = 0;
	if (m_phase != Phase::Running) {
		return false;
	}
	m_lift += m_velocity * STEP;
	if (m_lift > 0) {
		m_velocity -= 1450 * STEP;
	} else {
		m_lift = 0;
		m_velocity = 0;
	}

	const double travel = speed() * STEP;
	const bool slowed = m_slowTicks > 0;
	if (slowed) {
		--m_slowTicks;
	}
	m_distance += travel;
	m_until -= travel;
	for (auto& row : m_rows) {
		row.x -= travel;
	}
	while (m_until <= 0) {
		spawn(SPAWN_X + m_until);
	}
	for (auto& row : m_rows) {
		if (row.x < RUNNER_X + RUNNER_WIDTH &&
		    row.x + OBSTACLE_WIDTH > RUNNER_X) {
			resolve(row);
			if (m_phase != Phase::Running) {
				return true;
			}
		}
	}
	while (!m_rows.empty() && m_rows.front().x < GONE_X) {
		m_rows.pop_front();
	}

	const double creep =
	    WORM_CREEP + (WORM_CREEP_FAST - WORM_CREEP) * difficulty();
	m_worm -= creep * (slowed ? 2 : 1) * STEP;
	if (m_worm <= 0) {
		m_worm = 0;
		end(Ending::Worm);
		return true;
	}
	return false;
}

std::uint64_t loadBest(const std::filesystem::path& file) noexcept {
	if (file.empty()) {
		return 0;
	}
	try {
		std::ifstream in(file, std::ios::binary);
		if (!in) {
			return 0;
		}
		const auto json = nlohmann::json::parse(in, nullptr, false);
		const auto best = json.is_object() ? json.find("best") : json.end();
		if (best == json.end() || !best->is_number_unsigned()) {
			return 0;
		}
		return best->get<std::uint64_t>();
	} catch (...) {
		return 0;
	}
}

bool saveBest(const std::filesystem::path& file, std::uint64_t best) noexcept {
	if (file.empty()) {
		return false;
	}
	namespace fs = std::filesystem;
	std::error_code ec;
	try {
		if (file.has_parent_path()) {
			fs::create_directories(file.parent_path(), ec);
		}
		fs::path tmp = file;
		tmp += ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) {
				return false;
			}
			out << nlohmann::json{{"best", best}}.dump(1, '\t') << '\n';
			out.flush();
			if (!out) {
				out.close();
				fs::remove(tmp, ec);
				return false;
			}
		}
		fs::rename(tmp, file, ec);
		if (ec) {
			std::error_code ignored;
			fs::remove(tmp, ignored);
			return false;
		}
		return true;
	} catch (...) {
		return false;
	}
}
} // namespace night_run
