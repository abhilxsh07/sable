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

#include <atomic>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

#include <fpdf_edit.h>
#include <fpdfview.h>

#include "pdf/engine.h"

TEST(PdfEngine, RunsWorkOnItsOwnThread) {
	pdf::Engine engine;
	EXPECT_FALSE(pdf::Engine::onThread());
	const auto there =
	    engine.run([] { return std::this_thread::get_id(); }).get();
	EXPECT_NE(there, std::this_thread::get_id());
	EXPECT_TRUE(engine.run([] { return pdf::Engine::onThread(); }).get());
}

TEST(PdfEngine, KeepsTheOrderWorkWasPostedIn) {
	pdf::Engine engine;
	// only ever touched on the PDFium thread, read after the run() below
	std::vector<int> seen;
	for (int i = 0; i < 100; ++i) {
		engine.post([&seen, i] { seen.push_back(i); });
	}
	engine.run([] {}).get();
	std::vector<int> expected(100);
	std::iota(expected.begin(), expected.end(), 0);
	EXPECT_EQ(seen, expected);
}

TEST(PdfEngine, HandsExceptionsBackThroughTheFuture) {
	pdf::Engine engine;
	auto failed = engine.run([]() -> int { throw std::runtime_error("nope"); });
	EXPECT_THROW(failed.get(), std::runtime_error);
	// and keeps going
	EXPECT_EQ(engine.run([] { return 42; }).get(), 42);
}

TEST(PdfEngine, SurvivesPostedWorkThatThrows) {
	pdf::Engine engine;
	engine.post([] { throw std::runtime_error("nobody's listening"); });
	EXPECT_EQ(engine.run([] { return 7; }).get(), 7);
}

TEST(PdfEngine, TakesWorkThatCanOnlyBeMoved) {
	pdf::Engine engine;
	auto value = std::make_unique<int>(21);
	EXPECT_EQ(engine.run([v = std::move(value)] { return *v * 2; }).get(), 42);
}

TEST(PdfEngine, FinishesWhatsQueuedBeforeStopping) {
	std::atomic<int> done{0};
	{
		pdf::Engine engine;
		for (int i = 0; i < 20; ++i) {
			engine.post([&done] { ++done; });
		}
	}
	EXPECT_EQ(done.load(), 20);
}

// a couple of engines have come and gone by now, so this also checks that
// PDFium comes back up properly after being shut down
TEST(PdfEngine, PdfiumWorksOnItsThread) {
	pdf::Engine engine;
	auto pages = engine.run([] {
		FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
		if (!doc) {
			return -1;
		}
		FPDF_ClosePage(FPDFPage_New(doc, 0, 595, 842));
		const int count = FPDF_GetPageCount(doc);
		FPDF_CloseDocument(doc);
		return count;
	});
	EXPECT_EQ(pages.get(), 1);
}
