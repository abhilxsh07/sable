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
#include "../app/helpers/motion.h"
#include <gtest/gtest.h>

namespace {
using namespace std::chrono_literals;
TEST(Motion, TokensAreWinUIsDurations) {
	EXPECT_EQ(motion::duration(motion::Token::Faster, true), 83ms);
	EXPECT_EQ(motion::duration(motion::Token::Fast, true), 167ms);
	EXPECT_EQ(motion::duration(motion::Token::Normal, true), 250ms);
	EXPECT_EQ(motion::LONGEST, 250ms);
	EXPECT_EQ(motion::SLIDE, 16.f);
}
TEST(Motion, NothingRunsWithAnimationsOff) {
	constexpr motion::Token tokens[]{motion::Token::Faster, motion::Token::Fast,
	                                 motion::Token::Normal};
	for (const auto token : tokens) {
		EXPECT_EQ(motion::duration(token, false), 0ms);
		EXPECT_LE(motion::duration(token, true), motion::LONGEST);
	}
}
TEST(Motion, EnterDeceleratesAndExitAccelerates) {
	EXPECT_EQ(motion::ENTER.x1, 0.f);
	EXPECT_EQ(motion::ENTER.y2, 1.f);
	EXPECT_EQ(motion::ENTER.x2, 0.f);
	EXPECT_EQ(motion::EXIT.x1, 1.f);
	EXPECT_EQ(motion::EXIT.y1, 0.f);
	EXPECT_EQ(motion::EXIT.x2, 1.f);
	EXPECT_EQ(motion::EXIT.y2, 1.f);
}
} // namespace
