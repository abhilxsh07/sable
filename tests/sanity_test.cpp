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
#include <gtest/gtest.h>
#include "probe_result.h"

// nothing to test yet, this just proves the test project builds and runs.
// the real ones come with helpers/ (settings, keybinds, filter math)
TEST(Sanity, GoogleTestRuns) {
	EXPECT_EQ(2 + 2, 4);
}

TEST(ProbeResult, PendingAndFailedChecksCannotBecomeSuccessful) {
	ProbeResult result;
	EXPECT_NE(result.exitCode(), 0);
	result.pass();
	EXPECT_EQ(result.exitCode(), 0);
	result.fail();
	EXPECT_NE(result.exitCode(), 0);
	result.pass();
	EXPECT_NE(result.exitCode(), 0);
	ProbeResult failed;
	failed.fail();
	failed.pass();
	EXPECT_NE(failed.exitCode(), 0);
}
