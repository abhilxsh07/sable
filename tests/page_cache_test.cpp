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
#include <utility>

#include "pdf/page_cache.h"

using pdf::PageCache;
using pdf::PageKey;

namespace {
constexpr PageKey FIRST{0, 1, 1};
constexpr PageKey SECOND{1, 1, 1};
constexpr PageKey THIRD{2, 1, 1};

pdf::Bitmap pixel(std::uint8_t value = 255) {
	return {1, 1, 4, std::vector<std::uint8_t>(4, value)};
}
} // namespace

TEST(PageCache, StartsEmptyAndMissesDoNotInsert) {
	PageCache cache(16);
	EXPECT_EQ(cache.find(FIRST), nullptr);
	EXPECT_EQ(cache.size(), 0u);
	EXPECT_EQ(cache.usedBytes(), 0u);
}

TEST(PageCache, TakesThePixelBufferWithoutCopying) {
	auto bitmap = pixel(42);
	const auto* data = bitmap.pixels.data();
	const auto bytes = bitmap.pixels.capacity();
	PageCache cache(bytes);
	ASSERT_TRUE(cache.put(FIRST, std::move(bitmap)));
	const auto* found = cache.find(FIRST);
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->pixels.data(), data);
	EXPECT_EQ(found->pixels.front(), 42);
	EXPECT_EQ(cache.usedBytes(), bytes);
}

TEST(PageCache, KeysIncludePageSizeAndRotation) {
	PageCache cache(64);
	ASSERT_TRUE(cache.put(FIRST, pixel()));
	ASSERT_TRUE(cache.put(SECOND, pixel(20)));
	ASSERT_TRUE(cache.put({0, 1, 1, 1}, pixel(30)));
	ASSERT_TRUE(cache.put({0, 2, 1}, {2, 1, 8, std::vector<std::uint8_t>(8)}));
	ASSERT_TRUE(cache.put({0, 1, 2}, {1, 2, 4, std::vector<std::uint8_t>(8)}));
	EXPECT_EQ(cache.size(), 5u);
	EXPECT_NE(cache.find(FIRST), nullptr);
	EXPECT_NE(cache.find({0, 2, 1}), nullptr);
	EXPECT_NE(cache.find({0, 1, 2}), nullptr);
	ASSERT_NE(cache.find(SECOND), nullptr);
	EXPECT_EQ(cache.find(SECOND)->pixels.front(), 20);
	ASSERT_NE(cache.find({0, 1, 1, 1}), nullptr);
	EXPECT_EQ(cache.find({0, 1, 1, 1})->pixels.front(), 30);
	EXPECT_EQ(cache.find({0, 1, 1, 2}), nullptr);
}

TEST(PageCache, HitsProtectTheMostRecentlyUsedPage) {
	PageCache cache(8);
	ASSERT_TRUE(cache.put(FIRST, pixel()));
	ASSERT_TRUE(cache.put(SECOND, pixel()));
	ASSERT_NE(cache.find(FIRST), nullptr);
	ASSERT_TRUE(cache.put(THIRD, pixel()));
	EXPECT_EQ(cache.find(SECOND), nullptr);
	EXPECT_NE(cache.find(FIRST), nullptr);
	EXPECT_NE(cache.find(THIRD), nullptr);
	EXPECT_EQ(cache.usedBytes(), 8u);
}

TEST(PageCache, EvictsEnoughOldPagesToFit) {
	PageCache cache(12);
	ASSERT_TRUE(cache.put(FIRST, pixel()));
	ASSERT_TRUE(cache.put(SECOND, pixel()));
	ASSERT_TRUE(cache.put(THIRD, pixel()));
	ASSERT_TRUE(
	    cache.put({3, 3, 1}, {3, 1, 12, std::vector<std::uint8_t>(12)}));
	EXPECT_EQ(cache.size(), 1u);
	EXPECT_EQ(cache.usedBytes(), 12u);
	EXPECT_EQ(cache.find(FIRST), nullptr);
	EXPECT_EQ(cache.find(SECOND), nullptr);
	EXPECT_EQ(cache.find(THIRD), nullptr);
	EXPECT_NE(cache.find({3, 3, 1}), nullptr);
}

TEST(PageCache, ReplacingAnEntryCountsItOnceAndRefreshesRecency) {
	PageCache cache(8);
	ASSERT_TRUE(cache.put(FIRST, pixel()));
	ASSERT_TRUE(cache.put(SECOND, pixel()));
	ASSERT_TRUE(cache.put(FIRST, pixel(60)));
	EXPECT_EQ(cache.size(), 2u);
	EXPECT_EQ(cache.usedBytes(), 8u);
	ASSERT_TRUE(cache.put(THIRD, pixel()));
	EXPECT_EQ(cache.find(SECOND), nullptr);
	ASSERT_NE(cache.find(FIRST), nullptr);
	EXPECT_EQ(cache.find(FIRST)->pixels.front(), 60);
}

TEST(PageCache, ReplacementCreditsTheOldAllocationBeforeEvicting) {
	PageCache cache(12);
	ASSERT_TRUE(cache.put(FIRST, pixel()));
	ASSERT_TRUE(cache.put(SECOND, pixel()));
	ASSERT_TRUE(cache.put(FIRST, {1, 1, 8, std::vector<std::uint8_t>(8)}));
	EXPECT_EQ(cache.usedBytes(), 12u);
	EXPECT_EQ(cache.size(), 2u);
	EXPECT_NE(cache.find(SECOND), nullptr);
	ASSERT_TRUE(cache.put(FIRST, pixel()));
	EXPECT_EQ(cache.usedBytes(), 8u);
}

TEST(PageCache, LargerReplacementCanEvictOtherPages) {
	PageCache cache(8);
	ASSERT_TRUE(cache.put(FIRST, pixel()));
	ASSERT_TRUE(cache.put(SECOND, pixel()));
	ASSERT_TRUE(cache.put(FIRST, {1, 1, 8, std::vector<std::uint8_t>(8)}));
	EXPECT_EQ(cache.usedBytes(), 8u);
	EXPECT_EQ(cache.size(), 1u);
	EXPECT_EQ(cache.find(SECOND), nullptr);
	EXPECT_NE(cache.find(FIRST), nullptr);
}

TEST(PageCache, ChargesSpareCapacityAndAcceptsAnExactFit) {
	auto bitmap = pixel();
	bitmap.pixels.reserve(64);
	const auto bytes = bitmap.pixels.capacity();
	PageCache cache(bytes);
	ASSERT_TRUE(cache.put(FIRST, std::move(bitmap)));
	EXPECT_EQ(cache.usedBytes(), bytes);
	ASSERT_TRUE(cache.put(SECOND, pixel()));
	EXPECT_EQ(cache.find(FIRST), nullptr);
	EXPECT_EQ(cache.usedBytes(), 4u);
}

TEST(PageCache, OversizedReplacementLeavesEntriesAndRecencyAlone) {
	PageCache cache(8);
	ASSERT_TRUE(cache.put(FIRST, pixel()));
	ASSERT_TRUE(cache.put(SECOND, pixel()));
	auto bitmap = pixel();
	bitmap.pixels.reserve(64);
	EXPECT_FALSE(cache.put(FIRST, std::move(bitmap)));
	EXPECT_EQ(cache.usedBytes(), 8u);
	EXPECT_EQ(cache.size(), 2u);
	ASSERT_TRUE(cache.put(THIRD, pixel()));
	EXPECT_EQ(cache.find(FIRST), nullptr);
	EXPECT_NE(cache.find(SECOND), nullptr);
}

TEST(PageCache, ZeroBudgetRetainsNothing) {
	PageCache cache(0);
	EXPECT_FALSE(cache.put(FIRST, pixel()));
	EXPECT_EQ(cache.usedBytes(), 0u);
	EXPECT_EQ(cache.size(), 0u);
}

TEST(PageCache, RejectsMalformedKeysAndBitmapsWithoutDroppingGoodEntries) {
	PageCache cache(64);
	ASSERT_TRUE(cache.put(FIRST, pixel(10)));
	for (const auto key :
	     {PageKey{-1, 1, 1}, PageKey{0, 0, 1}, PageKey{0, 1, -1},
	      PageKey{0, 1, 1, -1}, PageKey{0, 1, 1, 4}, PageKey{0, 2, 1},
	      PageKey{0, 1, 2}}) {
		EXPECT_FALSE(cache.put(key, pixel()));
	}
	for (auto bitmap :
	     {pdf::Bitmap{1, 1, 0, {1, 2, 3, 4}},
	      pdf::Bitmap{1, 1, -4, {1, 2, 3, 4}}, pdf::Bitmap{1, 1, 3, {1, 2, 3}},
	      pdf::Bitmap{1, 1, 4, {}}, pdf::Bitmap{1, 1, 4, {1, 2, 3}},
	      pdf::Bitmap{1, 1, 4, {1, 2, 3, 4, 5}}}) {
		EXPECT_FALSE(cache.put(FIRST, std::move(bitmap)));
	}
	const int huge = std::numeric_limits<int>::max();
	EXPECT_FALSE(cache.put({0, 1, huge}, {1, huge, huge, {1, 2, 3, 4}}));
	EXPECT_EQ(cache.usedBytes(), 4u);
	EXPECT_EQ(cache.size(), 1u);
	ASSERT_NE(cache.find(FIRST), nullptr);
	EXPECT_EQ(cache.find(FIRST)->pixels.front(), 10);
}

TEST(PageCache, ClearReleasesTheDocumentAndAllowsReuse) {
	PageCache cache(8);
	ASSERT_TRUE(cache.put(FIRST, pixel(10)));
	ASSERT_TRUE(cache.put(SECOND, pixel(20)));
	cache.clear();
	EXPECT_EQ(cache.usedBytes(), 0u);
	EXPECT_EQ(cache.size(), 0u);
	EXPECT_EQ(cache.find(FIRST), nullptr);
	cache.clear();
	ASSERT_TRUE(cache.put(FIRST, pixel(30)));
	ASSERT_NE(cache.find(FIRST), nullptr);
	EXPECT_EQ(cache.find(FIRST)->pixels.front(), 30);
}
