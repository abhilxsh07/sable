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
#include <limits>

#include "pdf/surface_util.h"

namespace {
using pdf::DrawLoss;

bool same(const RECT& a, const RECT& b) {
	return a.left == b.left && a.top == b.top && a.right == b.right &&
	       a.bottom == b.bottom;
}

TEST(DrawLoss, DeviceErrorsNeedNewDevices) {
	EXPECT_EQ(pdf::drawLoss(DXGI_ERROR_DEVICE_REMOVED), DrawLoss::Device);
	EXPECT_EQ(pdf::drawLoss(DXGI_ERROR_DEVICE_RESET), DrawLoss::Device);
	EXPECT_EQ(pdf::drawLoss(D2DERR_RECREATE_TARGET), DrawLoss::Device);
}

TEST(DrawLoss, ContentsLostKeepsTheDevice) {
	EXPECT_EQ(pdf::drawLoss(pdf::CONTENTS_LOST), DrawLoss::Contents);
}

TEST(DrawLoss, OtherResultsAreNotLosses) {
	EXPECT_EQ(pdf::drawLoss(S_OK), DrawLoss::None);
	EXPECT_EQ(pdf::drawLoss(E_FAIL), DrawLoss::None);
	EXPECT_EQ(pdf::drawLoss(E_INVALIDARG), DrawLoss::None);
	EXPECT_EQ(pdf::drawLoss(E_OUTOFMEMORY), DrawLoss::None);
}

TEST(ClipToSurface, InsideIsUnchanged) {
	const auto clipped = pdf::clipToSurface({10, 20, 300, 400}, 640, 880);
	ASSERT_TRUE(clipped);
	EXPECT_TRUE(same(*clipped, {10, 20, 300, 400}));
	const auto whole = pdf::clipToSurface({0, 0, 640, 880}, 640, 880);
	ASSERT_TRUE(whole);
	EXPECT_TRUE(same(*whole, {0, 0, 640, 880}));
}

TEST(ClipToSurface, OverhangIsCut) {
	const auto clipped = pdf::clipToSurface({-50, -10, 700, 1000}, 640, 880);
	ASSERT_TRUE(clipped);
	EXPECT_TRUE(same(*clipped, {0, 0, 640, 880}));
	const auto bottom = pdf::clipToSurface({100, 800, 200, 900}, 640, 880);
	ASSERT_TRUE(bottom);
	EXPECT_TRUE(same(*bottom, {100, 800, 200, 880}));
}

TEST(ClipToSurface, OutsideLeavesNothing) {
	EXPECT_FALSE(pdf::clipToSurface({640, 0, 700, 100}, 640, 880));
	EXPECT_FALSE(pdf::clipToSurface({0, 880, 100, 900}, 640, 880));
	EXPECT_FALSE(pdf::clipToSurface({-100, -100, 0, 50}, 640, 880));
	EXPECT_FALSE(pdf::clipToSurface({0, 0, 100, -1}, 640, 880));
}

TEST(ClipToSurface, EmptyOrInvertedLeavesNothing) {
	EXPECT_FALSE(pdf::clipToSurface({10, 10, 10, 20}, 640, 880));
	EXPECT_FALSE(pdf::clipToSurface({10, 10, 20, 10}, 640, 880));
	EXPECT_FALSE(pdf::clipToSurface({300, 10, 200, 20}, 640, 880));
}

TEST(ClipToSurface, EmptySurfaceTakesNothing) {
	EXPECT_FALSE(pdf::clipToSurface({0, 0, 10, 10}, 0, 0));
	EXPECT_FALSE(pdf::clipToSurface({0, 0, 10, 10}, 640, 0));
}

TEST(ClipToSurface, TallSurface) {
	// 1,000 A4 pages at 400% on a 150% display
	const auto clipped =
	    pdf::clipToSurface({0, 6'749'000, 4'792, 6'760'000}, 4'792, 6'750'133);
	ASSERT_TRUE(clipped);
	EXPECT_TRUE(same(*clipped, {0, 6'749'000, 4'792, 6'750'133}));
}

TEST(FitsOneSurface, UpToTwoToTheTwentyFour) {
	EXPECT_TRUE(pdf::fitsOneSurface(4'792, 6'750'133));
	EXPECT_TRUE(pdf::fitsOneSurface(1, 16'777'216));
	EXPECT_FALSE(pdf::fitsOneSurface(1, 16'777'217));
	EXPECT_FALSE(pdf::fitsOneSurface(16'777'217, 1));
	EXPECT_FALSE(pdf::fitsOneSurface(0, 100));
	EXPECT_FALSE(pdf::fitsOneSurface(100, -1));
}
TEST(ScaledSurfaceSize, RoundsUpAtEachDisplayScale) {
	for (double scale : {1.0, 1.25, 1.5, 2.0}) {
		const auto size = pdf::scaledSurfaceSize(601.1, 842.1, scale);
		ASSERT_TRUE(size);
		EXPECT_EQ(size->cx, static_cast<LONG>(std::ceil(601.1 * scale)));
		EXPECT_EQ(size->cy, static_cast<LONG>(std::ceil(842.1 * scale)));
	}
}

TEST(ScaledSurfaceSize, LimitIsInPhysicalPixels) {
	const auto size = pdf::scaledSurfaceSize(100, 8'388'608, 2);
	ASSERT_TRUE(size);
	EXPECT_EQ(size->cy, pdf::MAX_SURFACE_SIDE);
	EXPECT_FALSE(pdf::scaledSurfaceSize(100, 8'388'608.1, 2));
	EXPECT_FALSE(pdf::scaledSurfaceSize(8'388'608.1, 100, 2));
	EXPECT_TRUE(pdf::scaledSurfaceSize(100, 8'388'609, 1));
}

TEST(ScaledSurfaceSize, InvalidInputsAndOverflowHaveNoSize) {
	for (double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(),
	                   std::numeric_limits<double>::quiet_NaN()}) {
		EXPECT_FALSE(pdf::scaledSurfaceSize(bad, 100, 1));
		EXPECT_FALSE(pdf::scaledSurfaceSize(100, bad, 1));
		EXPECT_FALSE(pdf::scaledSurfaceSize(100, 100, bad));
	}
	EXPECT_FALSE(pdf::scaledSurfaceSize(1e308, 100, 2));
	EXPECT_FALSE(pdf::scaledSurfaceSize(100, 1e308, 2));
}
} // namespace
