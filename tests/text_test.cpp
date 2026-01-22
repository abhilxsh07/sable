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
#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf/session.h"
#include "pdf_test_files.h"
#include <gtest/gtest.h>
#include <array>
#include <future>
#include <limits>

namespace {
using namespace pdf;
TEST(Text, ExtractsReadingOrderAndNewlines) {
	Engine engine;
	engine
	    .run([] {
		    auto opened = openFile(fixturePath("text.pdf"));
		    ASSERT_TRUE(opened.document);
		    const auto page = opened.document->textPage(0);
		    ASSERT_TRUE(page);
		    EXPECT_GT(page->characters.size(), 20u);
		    EXPECT_EQ(opened.document->textRange(0, 0, 5), "Alpha");
		    const auto all = opened.document->textRange(
		        0, 0, std::numeric_limits<int>::max());
		    ASSERT_TRUE(all);
		    EXPECT_EQ(*all, "Alpha beta\r\nSecond line");
		    EXPECT_FALSE(opened.document->textPage(-1));
		    EXPECT_FALSE(opened.document->textPage(5));
		    EXPECT_FALSE(opened.document->textRange(0, -1, 3));
		    EXPECT_EQ(opened.document->textRange(0, 1000, 2000), "");
	    })
	    .get();
}
TEST(Text, BoxesFollowCropAndAllPageRotations) {
	Engine engine;
	engine
	    .run([] {
		    auto opened = openFile(fixturePath("text.pdf"));
		    ASSERT_TRUE(opened.document);
		    std::array<TextRect, 4> boxes;
		    for (int i = 0; i < 4; ++i) {
			    const auto page = opened.document->textPage(i);
			    ASSERT_TRUE(page);
			    ASSERT_FALSE(page->characters.empty());
			    const auto letter = std::find_if(
			        page->characters.begin(), page->characters.end(),
			        [](const auto& character) {
				        return character.unicode == U'A';
			        });
			    ASSERT_NE(letter, page->characters.end());
			    const auto index =
			        static_cast<int>(letter - page->characters.begin());
			    boxes[i] = letter->box;
			    const auto& b = boxes[i];
			    EXPECT_GE(b.left, 0);
			    EXPECT_GE(b.top, 0);
			    EXPECT_LE(b.right, 1);
			    EXPECT_LE(b.bottom, 1);
			    const auto hit = hitText(*page, (b.left + b.right) / 2,
			                             (b.top + b.bottom) / 2, 600, 400);
			    EXPECT_EQ(hit, index) << i;
		    }
		    const auto& a = boxes[0];
		    EXPECT_NEAR(boxes[1].left, 1 - a.bottom, 0.00001);
		    EXPECT_NEAR(boxes[1].top, a.left, 0.00001);
		    EXPECT_NEAR(boxes[2].left, 1 - a.right, 0.00001);
		    EXPECT_NEAR(boxes[2].top, 1 - a.bottom, 0.00001);
		    EXPECT_NEAR(boxes[3].left, a.top, 0.00001);
		    EXPECT_NEAR(boxes[3].top, 1 - a.right, 0.00001);
	    })
	    .get();
}
TEST(Text, CopiesUnicodeMappingsIncludingSupplementaryCharacters) {
	Engine engine;
	engine
	    .run([] {
		    auto opened = openFile(fixturePath("text.pdf"));
		    ASSERT_TRUE(opened.document);
		    EXPECT_EQ(opened.document->textRange(4, 0, 100),
		              "\xCE\xA9\xF0\x9F\x98\x80\xC3\xA9");
	    })
	    .get();
}
TEST(Text, CopyPermissionAndImageOnlyPages) {
	Engine engine;
	engine
	    .run([] {
		    auto locked = openFile(fixturePath("restricted.pdf"));
		    ASSERT_TRUE(locked.document);
		    EXPECT_FALSE(locked.document->canCopy());
		    EXPECT_FALSE(locked.document->textRange(0, 0, 100));
		    EXPECT_TRUE(locked.document->textPage(0));
		    auto blank = openFile(fixturePath("links.pdf"));
		    ASSERT_TRUE(blank.document);
		    const auto text = blank.document->textPage(0);
		    ASSERT_TRUE(text);
		    EXPECT_TRUE(text->characters.empty());
	    })
	    .get();
}
TEST(Text, NormalizesSelectionAndClipsPageSpans) {
	const TextSelection s{{3, 2}, {1, 4}};
	EXPECT_EQ(textSpan(s, 0, 20), (std::pair{0, 0}));
	EXPECT_EQ(textSpan(s, 1, 20), (std::pair{4, 20}));
	EXPECT_EQ(textSpan(s, 2, 20), (std::pair{0, 20}));
	EXPECT_EQ(textSpan(s, 3, 20), (std::pair{0, 3}));
	EXPECT_EQ(textSpan(s, 3, 0), (std::pair{0, 0}));
	EXPECT_EQ(textSpan({{0, 0}, {0, std::numeric_limits<int>::max()}}, 0, 4),
	          (std::pair{0, 4}));
}
TEST(Text, HitToleranceIsInDisplayDipsAndNearestIgnoresLineBreaks) {
	const TextPage page{{{U'A', {.1, .1, .2, .2}}, {U'\n', {.3, .3, .4, .4}}}};
	EXPECT_EQ(hitText(page, .15, .15, 100, 100), 0);
	EXPECT_EQ(hitText(page, .25, .15, 100, 100), 0);
	EXPECT_FALSE(hitText(page, .25, .15, 200, 200));
	EXPECT_FALSE(hitText(page, .8, .8, 100, 100));
	EXPECT_EQ(hitText(page, .8, .8, 100, 100, true), 0);
	EXPECT_FALSE(
	    hitText(page, std::numeric_limits<double>::quiet_NaN(), 0, 100, 100));
}
TEST(Text, MergesAdjacentBoxesButKeepsSeparateLines) {
	TextPage page{{{U'A', {.1, .1, .2, .2}},
	               {U'B', {.2, .1, .3, .2}},
	               {U'C', {.1, .4, .2, .5}},
	               {U'D', {.1, .5, .2, .6}}}};
	const auto boxes = textRects(page, 0, 4);
	ASSERT_EQ(boxes.size(), 2u);
	EXPECT_EQ(boxes[0], (TextRect{.1, .1, .3, .2}));
	EXPECT_EQ(boxes[1], (TextRect{.1, .4, .2, .6}));
}
TEST(Text, MixedFontsShareTheWholeLineBandForPartialSelections) {
	const TextPage page{{{U'A', {.1, .2, .12, .235}, {.1, .23, 1, 0, 20}},
	                     {U'B', {.12, .19, .15, .24}, {.12, .23, 1, 0, 28}},
	                     {U' ', {}},
	                     {U'f', {.151, .198, .18, .242}, {.15, .23, 1, 0, 20}},
	                     {U'g', {.181, .205, .2, .25}, {.18, .23, 1, 0, 20}}},
	                    600,
	                    400};
	const auto all = textRects(page, 0, 5);
	ASSERT_EQ(all.size(), 1u);
	EXPECT_EQ(all[0], (TextRect{.1, .19, .2, .25}));
	const auto partial = textRects(page, 3, 4);
	ASSERT_EQ(partial.size(), 1u);
	EXPECT_EQ(partial[0], (TextRect{.151, .19, .18, .25}));
	EXPECT_EQ(textRects(page, 0, 1)[0].bottom, all[0].bottom);
}
TEST(Text, BaselinesAndPhysicalGapsKeepColumnsAndCloseLinesSeparate) {
	const TextPage page{{{U'A', {.1, .1, .12, .2}, {.1, .18, 1, 0, 10}},
	                     {U'B', {.14, .1, .16, .2}, {.14, .18, 1, 0, 10}},
	                     {U'C', {.16, .13, .18, .23}, {.16, .21, 1, 0, 10}},
	                     {U'\n', {}},
	                     {U'D', {.18, .13, .2, .23}, {.18, .21, 1, 0, 10}}},
	                    1000,
	                    100};
	const auto boxes = textRects(page, 0, 5);
	ASSERT_EQ(boxes.size(), 4u);
	for (int i = 0; i < 3; ++i) {
		EXPECT_EQ(boxes[i], page.characters[i].box);
	}
	EXPECT_EQ(boxes[3], page.characters[4].box);
}
TEST(Text, ReversedFontRunsKeepOneBandWithoutJoiningColumnsOrNewlines) {
	const TextPage page{{{U'E', {.18, .2, .2, .24}, {.18, .23, -1, 0, 18}},
	                     {U'F', {.2, .2, .22, .24}, {.2, .23, -1, 0, 18}},
	                     {U'C', {.14, .18, .16, .245}, {.14, .23, -1, 0, 24}},
	                     {U'D', {.16, .18, .18, .245}, {.16, .23, -1, 0, 24}},
	                     {U'A', {.1, .19, .12, .26}, {.1, .23, -1, 0, 30}},
	                     {U'B', {.12, .19, .14, .26}, {.12, .23, -1, 0, 30}},
	                     {U'X', {.6, .2, .62, .24}, {.6, .23, -1, 0, 18}},
	                     {U'\n', {}},
	                     {U'Z', {.22, .2, .24, .24}, {.22, .23, -1, 0, 18}}},
	                    600,
	                    400};
	const auto all = textRects(page, 0, 9);
	ASSERT_EQ(all.size(), 3u);
	EXPECT_EQ(all[0], (TextRect{.1, .18, .22, .26}));
	EXPECT_EQ(all[1], page.characters[6].box);
	EXPECT_EQ(all[2], page.characters[8].box);
	EXPECT_EQ(textRects(page, 0, 2),
	          (std::vector<TextRect>{{.18, .18, .22, .26}}));
	EXPECT_EQ(textRects(page, 4, 5),
	          (std::vector<TextRect>{{.1, .18, .12, .26}}));
}
TEST(Text, VerticalBandsAreStableAndDoNotJoinOppositeOrSkewedRuns) {
	TextPage page{{{U'A', {.2, .1, .3, .13}, {.25, .1, 0, 1, 12}},
	               {U'B', {.18, .13, .31, .16}, {.25, .13, 0, 1, 18}}},
	              100,
	              600};
	const auto all = textRects(page, 0, 2);
	ASSERT_EQ(all.size(), 1u);
	EXPECT_EQ(all[0], (TextRect{.18, .1, .31, .16}));
	EXPECT_EQ(textRects(page, 0, 1)[0], (TextRect{.18, .1, .31, .13}));
	page.characters[1].baseline.dy = -1;
	EXPECT_EQ(textRects(page, 0, 2).size(), 2u);
	for (auto& c : page.characters) {
		c.baseline.dx = .6;
		c.baseline.dy = .8;
	}
	EXPECT_EQ(textRects(page, 0, 2).size(), 2u);
}
TEST(Text, CroppedMixedFontBandsFollowEveryPageRotation) {
	Engine engine;
	engine
	    .run([] {
		    auto opened = openFile(fixturePath("text_bands.pdf"));
		    ASSERT_TRUE(opened.document);
		    std::array<TextRect, 4> bands;
		    for (int turn = 0; turn < 4; ++turn) {
			    const auto page = opened.document->textPage(turn);
			    ASSERT_TRUE(page);
			    const auto letter = std::find_if(
			        page->characters.begin(), page->characters.end(),
			        [](const auto& c) { return c.unicode == U'U'; });
			    ASSERT_NE(letter, page->characters.end());
			    EXPECT_GT(letter->baseline.height, 0);
			    const int index =
			        static_cast<int>(letter - page->characters.begin());
			    const auto selected = textRects(*page, index, index + 1);
			    ASSERT_EQ(selected.size(), 1u);
			    const auto all = textRects(
			        *page, 0, static_cast<int>(page->characters.size()));
			    const auto band =
			        std::find_if(all.begin(), all.end(), [&](const auto& b) {
				        const auto& s = selected[0];
				        return b.left <= s.left && b.top <= s.top &&
				               b.right >= s.right && b.bottom >= s.bottom;
			        });
			    ASSERT_NE(band, all.end());
			    bands[turn] = *band;
			    for (const auto glyph : {U'b', U'f', U'q'}) {
				    const auto other = std::find_if(
				        page->characters.begin(), page->characters.end(),
				        [glyph](const auto& c) { return c.unicode == glyph; });
				    ASSERT_NE(other, page->characters.end());
				    const int n =
				        static_cast<int>(other - page->characters.begin());
				    const auto part = textRects(*page, n, n + 1);
				    ASSERT_EQ(part.size(), 1u);
				    if (turn % 2 == 0) {
					    EXPECT_EQ(part[0].top, band->top);
					    EXPECT_EQ(part[0].bottom, band->bottom);
				    } else {
					    EXPECT_EQ(part[0].left, band->left);
					    EXPECT_EQ(part[0].right, band->right);
				    }
			    }
			    if (turn % 2 == 0) {
				    EXPECT_EQ(selected[0].top, band->top);
				    EXPECT_EQ(selected[0].bottom, band->bottom);
				    EXPECT_LT(selected[0].right - selected[0].left,
				              band->right - band->left);
			    } else {
				    EXPECT_EQ(selected[0].left, band->left);
				    EXPECT_EQ(selected[0].right, band->right);
				    EXPECT_LT(selected[0].bottom - selected[0].top,
				              band->bottom - band->top);
			    }
		    }
		    const auto& a = bands[0];
		    EXPECT_NEAR(bands[1].left, 1 - a.bottom, .00001);
		    EXPECT_NEAR(bands[1].top, a.left, .00001);
		    EXPECT_NEAR(bands[2].left, 1 - a.right, .00001);
		    EXPECT_NEAR(bands[2].top, 1 - a.bottom, .00001);
		    EXPECT_NEAR(bands[3].left, a.top, .00001);
		    EXPECT_NEAR(bands[3].top, 1 - a.right, .00001);
	    })
	    .get();
}
TEST(Text, Utf8HandlesGeneratedTextAndInvalidMappings) {
	EXPECT_EQ(textUtf8(U"A\r\n\u03A9\U0001F600"),
	          "A\r\n\xCE\xA9\xF0\x9F\x98\x80");
	const std::u32string pair{0xD83D, 0xDE00, 0};
	EXPECT_EQ(textUtf8(pair), "\xF0\x9F\x98\x80");
	EXPECT_EQ(textUtf8(std::u32string{0xD800, 0x110000}),
	          "\xEF\xBF\xBD\xEF\xBF\xBD");
}
SessionInfo ready(Session& session) {
	auto opened = std::make_shared<std::promise<SessionInfo>>();
	auto result = opened->get_future();
	EXPECT_TRUE(
	    session.open(fixturePath("text.pdf"), "", [opened](SessionInfo info) {
		    opened->set_value(std::move(info));
	    }));
	return result.get();
}
TEST(Text, SessionCopiesBothDirectionsAcrossPagesOnEngineThread) {
	Engine engine;
	Session session(engine);
	ASSERT_TRUE(ready(session).canCopy);
	for (const auto range :
	     {TextSelection{{0, 6}, {1, 4}}, TextSelection{{1, 4}, {0, 6}}}) {
		auto promise = std::make_shared<std::promise<CopyResult>>();
		auto result = promise->get_future();
		ASSERT_TRUE(session.copyText(range, [promise](CopyResult text) {
			EXPECT_TRUE(Engine::onThread());
			promise->set_value(std::move(text));
		}));
		const auto copy = result.get();
		EXPECT_FALSE(copy.failed);
		EXPECT_FALSE(copy.canceled);
		EXPECT_EQ(copy.text, "beta\r\nSecond line\r\nSecon");
	}
}
TEST(Text, PendingTextAndCopyAreCanceledOnClose) {
	Engine engine;
	Session session(engine);
	ready(session);
	std::promise<void> entered, release;
	auto block = release.get_future().share();
	engine.post([&entered, block] {
		entered.set_value();
		block.wait();
	});
	entered.get_future().wait();
	auto text = std::make_shared<std::promise<TextResult>>();
	auto copy = std::make_shared<std::promise<CopyResult>>();
	auto textResult = text->get_future();
	auto copyResult = copy->get_future();
	const bool textQueued = session.textPage(
	    0, [text](TextResult r) { text->set_value(std::move(r)); });
	const bool copyQueued =
	    session.copyText({{0, 0}, {0, 2}}, [copy](CopyResult r) {
		    copy->set_value(std::move(r));
	    });
	session.close();
	release.set_value();
	ASSERT_TRUE(textQueued && copyQueued);
	EXPECT_TRUE(textResult.get().canceled);
	const auto result = copyResult.get();
	EXPECT_TRUE(result.canceled);
	EXPECT_TRUE(result.text.empty());
	EXPECT_FALSE(session.textPage(0, [](auto) {}));
	EXPECT_FALSE(session.copyText({{0, 0}, {0, 2}}, [](auto) {}));
}
} // namespace
