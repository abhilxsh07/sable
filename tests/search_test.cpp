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
#include "pdf/search.h"
TEST(Find, CaseWordsAndNonOverlappingMatches) {
	const auto all = pdf::foldedFind(
	    U"Alpha alphabet ALPHA alpha_ alpha2 alpha", u"alpha", {}, 3, 100);
	EXPECT_EQ(all.matches.size(), 6u);
	pdf::FindOptions options;
	options.wholeWords = true;
	const auto words = pdf::foldedFind(
	    U"Alpha alphabet ALPHA alpha_ alpha2 alpha", u"alpha", options, 3, 100);
	ASSERT_EQ(words.matches.size(), 3u);
	EXPECT_EQ(words.matches.front(), (pdf::FindMatch{3, 0, 5}));
	options.matchCase = true;
	EXPECT_EQ(pdf::foldedFind(U"Alpha ALPHA alpha", u"alpha", options, 0, 100)
	              .matches.size(),
	          1u);
	EXPECT_EQ(pdf::foldedFind(U"aaaaa", u"aa", {}, 0, 100).matches.size(), 2u);
}
TEST(Find, AccentAndSurrogateMappingsRetainSourceRanges) {
	const auto result = pdf::foldedFind(U"caf\u00e9 cafe\u0301 \U0001f600",
	                                    u"cafe", {}, 4, 100);
	ASSERT_EQ(result.matches.size(), 2u);
	EXPECT_EQ(result.matches[0], (pdf::FindMatch{4, 0, 4}));
	EXPECT_EQ(result.matches[1], (pdf::FindMatch{4, 5, 5}));
	pdf::FindOptions strict;
	strict.matchDiacritics = true;
	EXPECT_EQ(pdf::foldedFind(U"caf\u00e9 cafe\u0301 cafe", u"caf\u00e9",
	                          strict, 0, 100)
	              .matches.size(),
	          2u);
	const auto unaccented =
	    pdf::foldedFind(U"caf\u00e9 cafe\u0301 cafe", u"cafe", strict, 0, 100);
	ASSERT_EQ(unaccented.matches.size(), 1u);
	EXPECT_EQ(unaccented.matches[0], (pdf::FindMatch{0, 11, 4}));
	EXPECT_TRUE(
	    pdf::foldedFind(U"\u00e9", u"e", strict, 0, 100).matches.empty());
	const auto emoji =
	    pdf::foldedFind(U"a\U0001f600b", u"\U0001f600", {}, 0, 100);
	ASSERT_EQ(emoji.matches.size(), 1u);
	EXPECT_EQ(emoji.matches[0], (pdf::FindMatch{0, 1, 1}));
}
TEST(Find, WhitespaceLimitsAndCancellation) {
	const auto result =
	    pdf::foldedFind(U"alpha\r\n  beta", u"alpha beta", {}, 0, 100);
	ASSERT_EQ(result.matches.size(), 1u);
	EXPECT_EQ(result.matches[0].count, 13);
	auto limited = pdf::foldedFind(U"a a a", u"a", {}, 0, 2);
	EXPECT_EQ(limited.matches.size(), 2u);
	EXPECT_TRUE(limited.limited);
	EXPECT_FALSE(pdf::foldedFind(U"a a", u"a", {}, 0, 2).limited);
	EXPECT_TRUE(pdf::foldedFind(U"alpha", u"alpha", {}, 0, 100, [] {
		            return true;
	            }).matches.empty());
	EXPECT_TRUE(pdf::foldedFind(U"alpha", u"", {}, 0, 100).matches.empty());
	EXPECT_TRUE(
	    pdf::foldedFind(U"alpha", u"\u0301", {}, 0, 100).matches.empty());
}
