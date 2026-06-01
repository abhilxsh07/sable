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
#include "../app/helpers/night_run.h"
#include "pdf_test_files.h"
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>

using night_run::Cell;
using night_run::Ending;
using night_run::Phase;
using night_run::Row;

namespace {
Row row(double x, Cell left, Cell middle, Cell right) {
	Row made;
	made.x = x;
	made.cells = {left, middle, right};
	return made;
}

Row across(double x, Cell cell) {
	return row(x, cell, cell, cell);
}

// ticks until the run ends or something in mask happens
unsigned until(night_run::Run& run, unsigned mask, int ticks = 300) {
	for (int i = 0; i < ticks; ++i) {
		run.tick();
		if ((run.events() & mask) || run.phase() != Phase::Running) {
			return run.events();
		}
	}
	return 0;
}

int rank(const Row& next, int lane) {
	switch (next.cells[static_cast<std::size_t>(lane)]) {
	case Cell::Page:
	case Cell::StaplePage:
		return next.taken ? 1 : 0;
	case Cell::Empty:
		return 1;
	case Cell::Staple:
		return 2;
	case Cell::Ink:
		return 3;
	case Cell::Rock:
		break;
	}
	return 9;
}

// a careful player: heads for the best lane of the next row and hops staples
void drive(night_run::Run& run) {
	const Row* next = nullptr;
	for (const auto& ahead : run.rows()) {
		if (ahead.x + night_run::OBSTACLE_WIDTH > night_run::RUNNER_X) {
			next = &ahead;
			break;
		}
	}
	if (!next) {
		return;
	}
	int best = run.lane();
	for (int lane = 0; lane < 3; ++lane) {
		const int score = rank(*next, lane);
		const int held = rank(*next, best);
		if (score < held ||
		    (score == held &&
		     std::abs(lane - run.lane()) < std::abs(best - run.lane()))) {
			best = lane;
		}
	}
	// one press per tick, like a player tapping the keys
	run.steer((best > run.lane()) - (best < run.lane()));
	const double lead =
	    next->x - (night_run::RUNNER_X + night_run::RUNNER_WIDTH);
	if (night_run::hasStaple(next->cells[static_cast<std::size_t>(best)]) &&
	    lead < run.speed() * 0.1) {
		run.jump();
	}
}
} // namespace

TEST(NightRun, StartsAtRestAndScoresDistance) {
	night_run::Run run(7);
	EXPECT_EQ(run.phase(), Phase::Ready);
	EXPECT_FALSE(run.jump());
	EXPECT_FALSE(run.tick());
	EXPECT_EQ(run.score(), 0u);
	run.start();
	ASSERT_FALSE(run.rows().empty());
	const double first = run.rows().front().x;
	for (int i = 0; i < night_run::TICKS_PER_SECOND; ++i) {
		EXPECT_FALSE(run.tick());
	}
	EXPECT_GT(run.score(), 25u);
	EXPECT_LT(run.rows().front().x, first - 250);
	EXPECT_LT(run.worm(), night_run::WORM_START);
}

TEST(NightRun, JumpsOnceUntilLanding) {
	night_run::Run run(7);
	run.start();
	ASSERT_TRUE(run.jump());
	EXPECT_FALSE(run.jump());
	run.tick();
	EXPECT_GT(run.lift(), 0);
	EXPECT_FALSE(run.jump());
	for (int i = 0; i < 50; ++i) {
		run.tick();
	}
	EXPECT_EQ(run.lift(), 0);
	EXPECT_TRUE(run.jump());
}

TEST(NightRun, RocksCannotBeHopped) {
	night_run::Run run(7);
	run.start();
	run.layRows({across(200, Cell::Rock)});
	ASSERT_TRUE(run.jump());
	until(run, 0);
	EXPECT_EQ(run.phase(), Phase::Crashed);
	EXPECT_EQ(run.ending(), Ending::Rock);
	const auto score = run.score();
	EXPECT_FALSE(run.tick());
	EXPECT_FALSE(run.jump());
	EXPECT_EQ(run.score(), score);
}

TEST(NightRun, StaplesNeedAHop) {
	night_run::Run sitting(7);
	sitting.start();
	sitting.layRows({across(160, Cell::Staple)});
	until(sitting, 0);
	EXPECT_EQ(sitting.ending(), Ending::Staple);

	night_run::Run hopping(7);
	hopping.start();
	hopping.layRows({across(160, Cell::Staple)});
	ASSERT_TRUE(hopping.jump());
	for (int i = 0; i < 45; ++i) {
		ASSERT_FALSE(hopping.tick());
	}
	EXPECT_EQ(hopping.phase(), Phase::Running);
	EXPECT_LT(hopping.rows().front().x + night_run::OBSTACLE_WIDTH,
	          night_run::RUNNER_X);
}

TEST(NightRun, APageAboveAStapleStillNeedsAHop) {
	const auto sheet = row(160, Cell::Staple, Cell::StaplePage, Cell::Staple);
	night_run::Run sitting(7);
	sitting.start();
	sitting.layRows({sheet});
	until(sitting, night_run::EVENT_PAGE);
	EXPECT_EQ(sitting.ending(), Ending::Staple);
	EXPECT_EQ(sitting.pages(), 0u);

	night_run::Run hopping(7);
	hopping.start();
	hopping.layRows({sheet});
	ASSERT_TRUE(hopping.jump());
	EXPECT_TRUE(until(hopping, night_run::EVENT_PAGE) & night_run::EVENT_PAGE);
	for (int i = 0; i < 40; ++i) {
		ASSERT_FALSE(hopping.tick());
	}
	EXPECT_EQ(hopping.pages(), 1u);
}

TEST(NightRun, InkSlowsTheBoardAndFeedsTheWorm) {
	night_run::Run run(7);
	run.start();
	const double pace = run.speed();
	run.layRows({across(150, Cell::Ink)});
	ASSERT_TRUE(until(run, night_run::EVENT_INK) & night_run::EVENT_INK);
	EXPECT_EQ(run.phase(), Phase::Running);
	EXPECT_TRUE(run.slowed());
	EXPECT_LT(run.speed(), pace);
	EXPECT_LT(run.worm(), night_run::WORM_START - 15);
	// one blot only counts once while the board crosses it
	for (int i = 0; i < 5; ++i) {
		run.tick();
		EXPECT_FALSE(run.events() & night_run::EVENT_INK);
	}
	for (int i = 0; i < night_run::TICKS_PER_SECOND; ++i) {
		run.tick();
	}
	EXPECT_FALSE(run.slowed());

	night_run::Run skimming(7);
	skimming.start();
	skimming.layRows({across(150, Cell::Ink)});
	ASSERT_TRUE(skimming.jump());
	EXPECT_EQ(until(skimming, night_run::EVENT_INK, 40), 0u);
	EXPECT_FALSE(skimming.slowed());
}

TEST(NightRun, PagesHoldOffTheWormAndThreeGiveAShield) {
	night_run::Run run(7);
	run.start();
	run.layRows({row(150, Cell::Rock, Cell::Page, Cell::Rock),
	             row(300, Cell::Rock, Cell::Page, Cell::Rock),
	             row(450, Cell::Rock, Cell::Page, Cell::Rock)});
	unsigned seen = 0;
	for (int i = 0; i < 100; ++i) {
		ASSERT_FALSE(run.tick());
		seen |= run.events();
	}
	EXPECT_EQ(run.pages(), 3u);
	EXPECT_TRUE(run.shield());
	EXPECT_TRUE(seen & night_run::EVENT_SHIELD_GAINED);
	EXPECT_GT(run.worm(), night_run::WORM_START - 10);

	run.layRows({across(150, Cell::Rock)});
	const double worm = run.worm();
	ASSERT_TRUE(until(run, night_run::EVENT_SHIELD_LOST) &
	            night_run::EVENT_SHIELD_LOST);
	EXPECT_EQ(run.phase(), Phase::Running);
	EXPECT_FALSE(run.shield());
	EXPECT_LT(run.worm(), worm - 19);
	// staying on the same rock must not spend the shield twice
	for (int i = 0; i < 5; ++i) {
		EXPECT_FALSE(run.tick());
	}
}

TEST(NightRun, TheWormCatchesASlowBoard) {
	night_run::Run run(7);
	run.start();
	std::deque<Row> ink;
	for (int i = 0; i < 9; ++i) {
		ink.push_back(across(150 + i * 90.0, Cell::Ink));
	}
	run.layRows(ink);
	until(run, 0, 600);
	EXPECT_EQ(run.phase(), Phase::Crashed);
	EXPECT_EQ(run.ending(), Ending::Worm);
	EXPECT_EQ(run.worm(), 0);
}

TEST(NightRun, RestartClearsTheRun) {
	night_run::Run run(7);
	run.start();
	run.layRows({across(150, Cell::Ink), across(250, Cell::Page),
	             across(400, Cell::Rock)});
	until(run, 0);
	ASSERT_EQ(run.ending(), Ending::Rock);
	run.start();
	EXPECT_EQ(run.phase(), Phase::Running);
	EXPECT_EQ(run.ending(), Ending::None);
	EXPECT_EQ(run.score(), 0u);
	EXPECT_EQ(run.pages(), 0u);
	EXPECT_EQ(run.worm(), night_run::WORM_START);
	EXPECT_FALSE(run.shield());
	EXPECT_FALSE(run.slowed());
	EXPECT_EQ(run.lane(), 1);
	EXPECT_EQ(run.lift(), 0);
	ASSERT_EQ(run.rows().size(), 1u);
	EXPECT_EQ(run.rows().front().x, 520);
}

TEST(NightRun, PausesInMidairAndResumesFromTheSamePoint) {
	night_run::Run run(7);
	run.start();
	ASSERT_TRUE(run.jump());
	run.tick();
	const auto lift = run.lift();
	const auto ahead = run.rows().front().x;
	const auto worm = run.worm();
	run.pause();
	EXPECT_EQ(run.phase(), Phase::Paused);
	EXPECT_FALSE(run.jump());
	EXPECT_FALSE(run.tick());
	EXPECT_EQ(run.lift(), lift);
	EXPECT_EQ(run.rows().front().x, ahead);
	EXPECT_EQ(run.worm(), worm);
	run.resume();
	EXPECT_EQ(run.phase(), Phase::Running);
	EXPECT_FALSE(run.tick());
	EXPECT_GT(run.lift(), lift);
	EXPECT_LT(run.rows().front().x, ahead);
}

TEST(NightRun, SteeringStaysInTheChannelAndPauses) {
	night_run::Run run(7);
	run.steer(-1);
	EXPECT_EQ(run.lane(), 1);
	run.start();
	run.steer(-1);
	run.steer(-1);
	EXPECT_EQ(run.lane(), 0);
	run.steer(1);
	run.steer(1);
	run.steer(1);
	EXPECT_EQ(run.lane(), 2);
	run.pause();
	run.steer(-1);
	EXPECT_EQ(run.lane(), 2);
}

// every row leaves a way through, the harder patterns only turn up later,
// and a careful player can keep going without the worm catching up
TEST(NightRun, ACarefulRunnerGetsThroughEveryPattern) {
	for (unsigned seed = 1; seed <= 12; ++seed) {
		night_run::Run run(seed);
		run.start();
		bool staples = false, ink = false, wall = false;
		for (int i = 0; i < 90 * night_run::TICKS_PER_SECOND; ++i) {
			for (const auto& ahead : run.rows()) {
				int rocks = 0;
				for (const auto cell : ahead.cells) {
					rocks += cell == Cell::Rock;
					staples |= night_run::hasStaple(cell);
					ink |= cell == Cell::Ink;
				}
				ASSERT_LT(rocks, 3) << "seed " << seed << " a row had no gap";
				wall |= rocks == 2;
				if (run.score() < 140) {
					ASSERT_LE(rocks, 1) << "seed " << seed;
					for (const auto cell : ahead.cells) {
						ASSERT_TRUE(!night_run::hasStaple(cell) &&
						            cell != Cell::Ink)
						    << "seed " << seed;
					}
				}
			}
			drive(run);
			ASSERT_FALSE(run.tick())
			    << "seed " << seed << " ended at " << run.score() << " by "
			    << static_cast<int>(run.ending());
		}
		EXPECT_TRUE(staples && ink && wall) << "seed " << seed;
		EXPECT_GT(run.pages(), 50u) << "seed " << seed;
		EXPECT_DOUBLE_EQ(run.difficulty(), 1.0);
	}
}

TEST(NightRun, BestDistanceSurvivesARestart) {
	const auto dir = makeTempDir("night_run");
	const auto file = dir / "night-run.json";
	EXPECT_EQ(night_run::loadBest(file), 0u);
	EXPECT_TRUE(night_run::saveBest(file, 1234));
	EXPECT_EQ(night_run::loadBest(file), 1234u);
	EXPECT_TRUE(night_run::saveBest(file, 99));
	EXPECT_EQ(night_run::loadBest(file), 99u);
	EXPECT_FALSE(std::filesystem::exists(dir / "night-run.json.tmp"));

	for (const char* broken :
	     {"", "not json", "[]", "{\"best\":-4}", "{\"best\":\"12\"}",
	      "{\"best\":2.5}", "{\"score\":12}"}) {
		std::ofstream(file, std::ios::binary | std::ios::trunc) << broken;
		EXPECT_EQ(night_run::loadBest(file), 0u) << broken;
	}
	EXPECT_EQ(night_run::loadBest({}), 0u);
	EXPECT_FALSE(night_run::saveBest({}, 5));
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}
