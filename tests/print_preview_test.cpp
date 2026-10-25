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
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <string_view>
#include <utility>

#include "pdf/engine.h"
#include "pdf/print_layout.h"
#include "pdf/session.h"
#include "pdf_test_files.h"

namespace {
std::array<std::uint8_t, 4> pixel(const pdf::Bitmap& bitmap, int x, int y) {
	const auto offset = static_cast<std::size_t>(y) * bitmap.stride + x * 4;
	return {bitmap.pixels[offset], bitmap.pixels[offset + 1],
	        bitmap.pixels[offset + 2], bitmap.pixels[offset + 3]};
}
template <typename T> class Answer {
  public:
	auto callback() {
		return [promise = m_promise](T result) {
			promise->set_value(std::move(result));
		};
	}
	T get() {
		if (m_future.wait_for(std::chrono::seconds(10)) !=
		    std::future_status::ready) {
			ADD_FAILURE() << "the callback never ran";
			return {};
		}
		return m_future.get();
	}

  private:
	std::shared_ptr<std::promise<T>> m_promise =
	    std::make_shared<std::promise<T>>();
	std::future<T> m_future = m_promise->get_future();
};
class Gate {
  public:
	explicit Gate(pdf::Engine& engine) {
		engine.post([wait = m_promise.get_future().share()] { wait.wait(); });
	}
	~Gate() {
		open();
	}
	void open() {
		if (!m_open) {
			m_open = true;
			m_promise.set_value();
		}
	}

  private:
	std::promise<void> m_promise;
	bool m_open = false;
};
} // namespace

TEST(PrintPreview, FitsAndCentersPortraitAndLandscapeInsidePaperMargins) {
	const pdf::PrintPaper paper{816, 1056, 24, 48, 768, 960};
	const auto portrait = pdf::layoutPrint({100, 200}, paper, 300, 300);
	ASSERT_TRUE(portrait);
	EXPECT_NEAR(portrait->x, 168, 1e-3);
	EXPECT_NEAR(portrait->y, 48, 1e-3);
	EXPECT_NEAR(portrait->width, 480, 1e-3);
	EXPECT_NEAR(portrait->height, 960, 1e-3);
	const auto landscape = pdf::layoutPrint({200, 100}, paper, 600, 300);
	ASSERT_TRUE(landscape);
	EXPECT_NEAR(landscape->x, 24, 1e-3);
	EXPECT_NEAR(landscape->y, 336, 1e-3);
	EXPECT_NEAR(landscape->width, 768, 1e-3);
	EXPECT_NEAR(landscape->height, 384, 1e-3);
	EXPECT_EQ(landscape->rasterWidth, 4800);
	EXPECT_EQ(landscape->rasterHeight, 1200);
	const auto clipped =
	    pdf::layoutPrint({100, 100}, {600, 800, -20, 60, 700, 900}, 96, 96);
	ASSERT_TRUE(clipped);
	EXPECT_NEAR(clipped->x, 0, 1e-3);
	EXPECT_NEAR(clipped->y, 130, 1e-3);
}

TEST(PrintPreview, RejectsInvalidPaperDpiAndUnboundedRasters) {
	const pdf::PrintPaper paper{816, 1056, 24, 48, 768, 960};
	const double nan = std::numeric_limits<double>::quiet_NaN();
	for (const auto dpi :
	     {0., -1., nan, std::numeric_limits<double>::infinity()}) {
		EXPECT_FALSE(pdf::layoutPrint({100, 200}, paper, dpi, 300));
		EXPECT_FALSE(pdf::layoutPrint({100, 200}, paper, 300, dpi));
	}
	EXPECT_FALSE(pdf::layoutPrint({0, 200}, paper, 300, 300));
	EXPECT_FALSE(pdf::layoutPrint({100, 200}, {}, 300, 300));
	EXPECT_FALSE(
	    pdf::layoutPrint({100, 200}, {816, 1056, 900, 0, 100, 100}, 300, 300));
	EXPECT_FALSE(pdf::layoutPrint({100, 200}, paper, 1e10, 300));
}

TEST(PrintPreview, RasterKeepsOriginalColorsAndPrintOnlyAnnotations) {
	pdf::Engine engine;
	const auto bitmap =
	    engine
	        .run([] {
		        auto opened = pdf::openFile(fixturePath("print.pdf"));
		        if (!opened.document) {
			        return std::optional<pdf::Bitmap>{};
		        }
		        return opened.document->printRaster(0, 100, 100, 0, 0, 100,
		                                            100);
	        })
	        .get();
	ASSERT_TRUE(bitmap);
	EXPECT_EQ(pixel(*bitmap, 20, 80),
	          (std::array<std::uint8_t, 4>{0, 0, 255, 255}));
	EXPECT_EQ(pixel(*bitmap, 60, 80),
	          (std::array<std::uint8_t, 4>{0, 255, 0, 255}));
	EXPECT_EQ(pixel(*bitmap, 20, 30),
	          (std::array<std::uint8_t, 4>{255, 255, 255, 255}));
}

TEST(PrintPreview, TilesMatchFullRasterAndRespectIntrinsicRotation) {
	pdf::Engine engine;
	engine
	    .run([] {
		    auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    const auto full =
		        opened.document->printRaster(2, 200, 300, 0, 0, 200, 300);
		    const auto tile =
		        opened.document->printRaster(2, 200, 300, 30, 20, 130, 270);
		    ASSERT_TRUE(full && tile);
		    EXPECT_EQ(pixel(*full, 100, 40),
		              (std::array<std::uint8_t, 4>{0, 0, 0, 255}));
		    EXPECT_EQ(pixel(*full, 100, 250),
		              (std::array<std::uint8_t, 4>{255, 255, 255, 255}));
		    for (int y = 0; y < tile->height; ++y) {
			    for (int x = 0; x < tile->width; ++x) {
				    ASSERT_EQ(pixel(*tile, x, y), pixel(*full, x + 30, y + 20));
			    }
		    }
	    })
	    .get();
}

TEST(PrintPreview, PrinterDpiUsesSmallTilesBeyondScreenRasterLimits) {
	const auto layout =
	    pdf::layoutPrint({595, 842}, {816, 1056, 24, 24, 768, 1008}, 600, 600);
	ASSERT_TRUE(layout);
	ASSERT_GT(layout->rasterWidth, 4096);
	ASSERT_GT(layout->rasterHeight, 4096);
	pdf::Engine engine;
	engine
	    .run([layout] {
		    auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    EXPECT_TRUE(opened.document->printRaster(0, layout->rasterWidth,
		                                             layout->rasterHeight, 4200,
		                                             5000, 32, 32));
		    EXPECT_FALSE(
		        opened.document->printRaster(0, 5000, 6000, 0, 0, 4097, 1));
		    EXPECT_FALSE(
		        opened.document->printRaster(0, 5000, 6000, 4990, 0, 32, 32));
		    EXPECT_FALSE(
		        opened.document->printRaster(-1, 100, 100, 0, 0, 32, 32));
	    })
	    .get();
}

TEST(PrintPreview, PrintTilesDoNotConsumeTheScreenSlotAndCancelIndependently) {
	pdf::Engine engine;
	pdf::Session session(engine);
	Answer<pdf::SessionInfo> opened;
	ASSERT_TRUE(session.open(fixturePath("pages.pdf"), {}, opened.callback()));
	ASSERT_EQ(opened.get().error, pdf::Error::None);
	Gate gate(engine);
	Answer<pdf::RenderResult> screen;
	ASSERT_TRUE(session.render({0, 100, 100}, screen.callback()));
	const auto renderer = session.printRenderer();
	Answer<std::optional<pdf::Bitmap>> printed, canceled;
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	auto onThread = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(renderer(
	    {2, 200, 300, {0, 0, 32, 32}}, [] { return false; },
	    [onThread, callback = printed.callback()](auto value) {
		    *onThread = pdf::Engine::onThread();
		    callback(std::move(value));
	    }));
	ASSERT_TRUE(renderer(
	    {0, 100, 100, {0, 0, 32, 32}}, [cancel] { return cancel->load(); },
	    canceled.callback()));
	*cancel = true;
	session.cancelRender();
	gate.open();
	EXPECT_TRUE(screen.get().canceled);
	EXPECT_TRUE(printed.get());
	EXPECT_TRUE(*onThread);
	EXPECT_FALSE(canceled.get());
}

TEST(PrintPreview, CapturedRendererRejectsClosedSessionAndCancelsQueuedTiles) {
	pdf::Engine engine;
	pdf::Session session(engine);
	Answer<pdf::SessionInfo> opened;
	ASSERT_TRUE(session.open(fixturePath("pages.pdf"), {}, opened.callback()));
	ASSERT_EQ(opened.get().error, pdf::Error::None);
	const auto renderer = session.printRenderer();
	Gate gate(engine);
	Answer<std::optional<pdf::Bitmap>> answer;
	ASSERT_TRUE(renderer(
	    {0, 100, 100, {0, 0, 32, 32}}, [] { return false; },
	    answer.callback()));
	session.close();
	gate.open();
	EXPECT_FALSE(answer.get());
	EXPECT_FALSE(renderer(
	    {0, 100, 100, {0, 0, 32, 32}}, [] { return false; },
	    answer.callback()));
}

TEST(PrintPreview, CapturedRendererCanOutliveSessionAndEngine) {
	pdf::PrintRenderer renderer;
	{
		auto engine = std::make_unique<pdf::Engine>();
		{
			pdf::Session session(*engine);
			Answer<pdf::SessionInfo> opened;
			ASSERT_TRUE(
			    session.open(fixturePath("pages.pdf"), {}, opened.callback()));
			ASSERT_EQ(opened.get().error, pdf::Error::None);
			renderer = session.printRenderer();
		}
		engine->run([] {}).get();
	}
	bool called = false;
	EXPECT_FALSE(renderer(
	    {0, 100, 100, {0, 0, 32, 32}}, [] { return false; },
	    [&](auto) { called = true; }));
	EXPECT_FALSE(called);
	// The print source can release its renderer after the reader is gone.
	renderer = {};
}

TEST(PrintPreview, AuthenticatedDocumentAndLastPageRenderOnDemand) {
	pdf::Engine engine;
	for (const auto& fixture : {"password.pdf", "long.pdf"}) {
		pdf::Session session(engine);
		Answer<pdf::SessionInfo> opened;
		ASSERT_TRUE(
		    session.open(fixturePath(fixture), "sable", opened.callback()));
		const auto info = opened.get();
		ASSERT_EQ(info.error, pdf::Error::None);
		ASSERT_FALSE(info.pages.empty());
		const int page = static_cast<int>(info.pages.size()) - 1;
		if (std::string_view(fixture) == "long.pdf") {
			EXPECT_EQ(page, 999);
		}
		Answer<std::optional<pdf::Bitmap>> answer;
		ASSERT_TRUE(session.printRenderer()(
		    {page, 100, 100, {0, 0, 32, 32}}, [] { return false; },
		    answer.callback()));
		const auto bitmap = answer.get();
		ASSERT_TRUE(bitmap);
		EXPECT_EQ(bitmap->width, 32);
		EXPECT_EQ(bitmap->pixels.size(), 32u * 32u * 4u);
	}
}
