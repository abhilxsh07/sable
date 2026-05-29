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
#include <gtest/gtest.h>

using night_run::Phase;

TEST(NightRun, StartsAtRestAndScoresDistance) {
	night_run::Run run(7);
	EXPECT_EQ(run.phase(), Phase::Ready);
	EXPECT_FALSE(run.jump());
	EXPECT_FALSE(run.tick());
	EXPECT_EQ(run.score(), 0u);
	run.start();
	for (int i = 0; i < night_run::TICKS_PER_SECOND; ++i) {
		EXPECT_FALSE(run.tick());
	}
	EXPECT_GT(run.score(), 25u);
	EXPECT_LT(run.obstacleX(), 250);
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

TEST(NightRun, HitsAnObstacleAndRestarts) {
	night_run::Run run(7);
	run.start();
	bool crashed = false;
	for (int i = 0; i < 120 && !crashed; ++i) {
		crashed = run.tick();
	}
	ASSERT_TRUE(crashed);
	EXPECT_EQ(run.phase(), Phase::Crashed);
	const auto score = run.score();
	EXPECT_FALSE(run.tick());
	EXPECT_FALSE(run.jump());
	EXPECT_EQ(run.score(), score);
	run.start();
	EXPECT_EQ(run.phase(), Phase::Running);
	EXPECT_EQ(run.score(), 0u);
	EXPECT_EQ(run.lift(), 0);
	EXPECT_EQ(run.obstacleX(), 500);
}

TEST(NightRun, ClearsAnObstacleAndSpawnsAnother) {
	night_run::Run run(7);
	run.start();
	for (int i = 0; i < 90 && run.obstacleX() > 165; ++i) {
		ASSERT_FALSE(run.tick());
	}
	ASSERT_LE(run.obstacleX(), 165);
	ASSERT_TRUE(run.jump());
	for (int i = 0; i < 65; ++i) {
		ASSERT_FALSE(run.tick());
	}
	EXPECT_GT(run.obstacleX(), 500);
	EXPECT_GE(run.obstacleHeight(), 26);
	EXPECT_LE(run.obstacleHeight(), 42);
}

TEST(NightRun, PausesInMidairAndResumesFromTheSamePoint) {
	night_run::Run run(7);
	run.start();
	ASSERT_TRUE(run.jump());
	run.tick();
	const auto lift = run.lift();
	const auto obstacle = run.obstacleX();
	run.pause();
	EXPECT_EQ(run.phase(), Phase::Paused);
	EXPECT_FALSE(run.jump());
	EXPECT_FALSE(run.tick());
	EXPECT_EQ(run.lift(), lift);
	EXPECT_EQ(run.obstacleX(), obstacle);
	run.resume();
	EXPECT_EQ(run.phase(), Phase::Running);
	EXPECT_FALSE(run.tick());
	EXPECT_GT(run.lift(), lift);
	EXPECT_LT(run.obstacleX(), obstacle);
}
