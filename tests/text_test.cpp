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
