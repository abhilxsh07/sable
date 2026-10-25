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
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "pdf/engine.h"
#include "pdf/session.h"
#include "pdf/printing.h"
#include "pdf_test_files.h"

namespace {
namespace fs = std::filesystem;

// A4 pages labelled like pages.pdf (i, ii, then 1, 2, ...), written out by
// hand the way make_fixtures.py does it, just with many more pages
std::string manyPages(int count) {
	std::string pdf = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	const auto add = [&](const std::string& body) {
		offsets.push_back(pdf.size());
		pdf +=
		    std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
	};
	add("<< /Type /Catalog /Pages 2 0 R /PageLabels << /Nums [0 << /S /r >> "
	    "2 << /S /D >>] >> >>");
	std::string kids;
	for (int i = 0; i < count; ++i) {
		kids += std::to_string(3 + i) + " 0 R ";
	}
	add("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(count) +
	    " >>");
	for (int i = 0; i < count; ++i) {
		add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] >>");
	}
	const auto xref = pdf.size();
	pdf += "xref\n0 " + std::to_string(offsets.size() + 1) +
	       "\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		pdf += line;
	}
	pdf += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) +
	       " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
	return pdf;
}

// Keeps the PDFium thread busy until it's opened, so whatever is posted
// after it waits in the queue. Opens itself when it goes away.
class Gate {
  public:
	explicit Gate(pdf::Engine& engine) {
		engine.post([wait = m_open.get_future().share()] { wait.wait(); });
	}
	~Gate() {
		open();
	}
	Gate(const Gate&) = delete;
	Gate& operator=(const Gate&) = delete;
	void open() {
		if (!m_opened) {
			m_opened = true;
			m_open.set_value();
		}
	}

  private:
	std::promise<void> m_open;
	bool m_opened = false;
};

// A callback's answer. Callbacks run on the PDFium thread, so they only hand
// the result over and the checks happen back here. The callback shares the
// promise, so it stays safe if the test stops waiting, and one that never
// runs fails the test instead of hanging it.
template <typename T> class Reply {
  public:
	std::function<void(T)> callback() const {
		return [promise = m_promise](T value) {
			promise->set_value(std::move(value));
		};
	}
	std::optional<T> wait() {
		if (m_future.wait_for(std::chrono::seconds(10)) !=
		    std::future_status::ready) {
			ADD_FAILURE() << "the callback never ran";
			return std::nullopt;
		}
		return m_future.get();
	}

  private:
	std::shared_ptr<std::promise<T>> m_promise =
	    std::make_shared<std::promise<T>>();
	std::future<T> m_future = m_promise->get_future();
};

class SessionTest : public ::testing::Test {
  protected:
	void TearDown() override {
		std::error_code ec;
		for (const fs::path& dir : m_dirs) {
			fs::remove_all(dir, ec);
		}
	}

	fs::path writeTemp(std::string_view name, const std::string& bytes) {
		const auto path = tempDir() / std::string(name);
		std::ofstream out(path, std::ios::binary);
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		return path;
	}
	std::vector<std::uint8_t> readFile(const fs::path& path) {
		std::ifstream in(path, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), {}};
	}

	fs::path tempDir() {
		m_dirs.push_back(makeTempDir("session"));
		return m_dirs.back();
	}

	// opens and waits for the answer
	pdf::SessionInfo open(pdf::Session& session, const fs::path& path,
	                      const std::string& password = {}) {
		Reply<pdf::SessionInfo> reply;
		if (!session.open(path, password, reply.callback())) {
			ADD_FAILURE() << "open wasn't accepted";
			return {};
		}
		return reply.wait().value_or(pdf::SessionInfo{});
	}

	// renders and waits for the answer
	pdf::RenderResult render(pdf::Session& session, pdf::PageKey key) {
		Reply<pdf::RenderResult> reply;
		if (!session.render(key, reply.callback())) {
			ADD_FAILURE() << "render wasn't accepted";
			return {key, std::nullopt};
		}
		return reply.wait().value_or(pdf::RenderResult{key, std::nullopt});
	}

	pdf::Engine engine;

  private:
	std::vector<fs::path> m_dirs;
};

TEST_F(SessionTest, LinksReturnOnTheEngineThreadAndCloseCancelsQueuedClicks) {
	pdf::Session session(engine);
	Reply<pdf::LinkResult> unused;
	EXPECT_FALSE(session.linkAt(0, 0, 0, unused.callback()));
	ASSERT_EQ(open(session, fixturePath("links.pdf")).error, pdf::Error::None);
	Reply<pdf::LinkResult> reply;
	auto onThread = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.linkAt(
	    0, 80.0 / 360, 50.0 / 540,
	    [onThread, callback = reply.callback()](pdf::LinkResult result) {
		    *onThread = pdf::Engine::onThread();
		    callback(std::move(result));
	    }));
	const auto result = reply.wait();
	ASSERT_TRUE(result && result->link);
	EXPECT_TRUE(*onThread);
	EXPECT_FALSE(result->canceled);
	EXPECT_EQ(result->link->uri, "https://example.com/sable?x=1#part");
	Gate gate(engine);
	Reply<pdf::LinkResult> canceled;
	ASSERT_TRUE(
	    session.linkAt(0, 80.0 / 360, 130.0 / 540, canceled.callback()));
	session.close();
	gate.open();
	const auto late = canceled.wait();
	ASSERT_TRUE(late);
	EXPECT_TRUE(late->canceled);
	EXPECT_FALSE(late->link);
	EXPECT_FALSE(session.linkAt(0, 0, 0, unused.callback()));
}

TEST_F(SessionTest, PrintingHasOneSlotAndCloseCancelsQueuedJobs) {
	pdf::Session session(engine);
	pdf::PrintJob job;
	// An invalid page fails before GDI sees this sentinel device.
	job.device =
	    std::shared_ptr<void>(reinterpret_cast<void*>(1), [](void*) {});
	job.pages = {-1};
	Reply<pdf::PrintResult> unused;
	EXPECT_FALSE(session.print(job, unused.callback()));
	ASSERT_EQ(open(session, fixturePath("pages.pdf")).error, pdf::Error::None);
	Reply<pdf::PrintResult> failed;
	auto onThread = std::make_shared<std::atomic<bool>>(false);
	{
		Gate gate(engine);
		ASSERT_TRUE(session.print(job, [onThread, callback = failed.callback()](
		                                   pdf::PrintResult result) {
			*onThread = pdf::Engine::onThread();
			callback(result);
		}));
		EXPECT_FALSE(session.print(job, unused.callback()));
	}
	EXPECT_EQ(failed.wait(), pdf::PrintResult::Failed);
	EXPECT_TRUE(*onThread);
	Reply<pdf::PrintResult> retry;
	ASSERT_TRUE(session.print(job, retry.callback()));
	EXPECT_EQ(retry.wait(), pdf::PrintResult::Failed);
	Reply<pdf::PrintResult> canceled;
	{
		Gate gate(engine);
		job.pages = {0};
		ASSERT_TRUE(session.print(job, canceled.callback()));
		session.close();
	}
	EXPECT_EQ(canceled.wait(), pdf::PrintResult::Canceled);
	EXPECT_FALSE(session.print(job, unused.callback()));
}

TEST_F(SessionTest, OpensAFileAndListsItsPages) {
	pdf::Session session(engine);
	const auto info = open(session, fixturePath("pages.pdf"));
	EXPECT_EQ(info.error, pdf::Error::None);
	EXPECT_FALSE(info.canceled);
	ASSERT_EQ(info.pages.size(), 3u);
	EXPECT_FLOAT_EQ(info.pages[0].width, 595.0f);
	EXPECT_FLOAT_EQ(info.pages[0].height, 842.0f);
	EXPECT_FLOAT_EQ(info.pages[1].width, 792.0f);
	EXPECT_FLOAT_EQ(info.pages[1].height, 612.0f);
	// stored 300 x 200 with /Rotate 90
	EXPECT_FLOAT_EQ(info.pages[2].width, 200.0f);
	EXPECT_FLOAT_EQ(info.pages[2].height, 300.0f);
	EXPECT_EQ(info.labels, (std::vector<std::string>{"i", "ii", "1"}));
}

TEST_F(SessionTest, LabelsAreEmptyWhenTheFileHasNone) {
	pdf::Session session(engine);
	const auto info = open(session, fixturePath("restricted.pdf"));
	EXPECT_EQ(info.error, pdf::Error::None);
	EXPECT_EQ(info.labels, (std::vector<std::string>{""}));
}

TEST_F(SessionTest, ListsAThousandPages) {
	pdf::Session session(engine);
	const auto info = open(session, writeTemp("many.pdf", manyPages(1000)));
	EXPECT_EQ(info.error, pdf::Error::None);
	ASSERT_EQ(info.pages.size(), 1000u);
	ASSERT_EQ(info.labels.size(), 1000u);
	EXPECT_FLOAT_EQ(info.pages[999].width, 595.0f);
	EXPECT_FLOAT_EQ(info.pages[999].height, 842.0f);
	EXPECT_EQ(info.labels[0], "i");
	EXPECT_EQ(info.labels[1], "ii");
	EXPECT_EQ(info.labels[2], "1");
	EXPECT_EQ(info.labels[999], "998");
}

TEST_F(SessionTest, SaysWhyAFileWontOpen) {
	pdf::Session missing(engine);
	const auto info = open(missing, fixturePath("not-there.pdf"));
	EXPECT_EQ(info.error, pdf::Error::File);
	EXPECT_FALSE(info.canceled);
	EXPECT_TRUE(info.pages.empty());
	EXPECT_TRUE(info.labels.empty());
	EXPECT_FALSE(missing.render({0, 10, 10}, [](pdf::RenderResult) {}));

	pdf::Session broken(engine);
	EXPECT_EQ(open(broken, fixturePath("broken.pdf")).error,
	          pdf::Error::Format);
}

TEST_F(SessionTest, AsksForAPassword) {
	pdf::Session without(engine);
	EXPECT_EQ(open(without, fixturePath("password.pdf")).error,
	          pdf::Error::PasswordRequired);
	pdf::Session wrong(engine);
	EXPECT_EQ(open(wrong, fixturePath("password.pdf"), "nope").error,
	          pdf::Error::WrongPassword);
	pdf::Session right(engine);
	const auto info = open(right, fixturePath("password.pdf"), "sable");
	EXPECT_EQ(info.error, pdf::Error::None);
	EXPECT_EQ(info.pages.size(), 1u);
}

TEST_F(SessionTest, EmptyAndNonPdfFilesAreFormatErrors) {
	for (const auto name : {"empty.pdf", "not_pdf.pdf"}) {
		pdf::Session session(engine);
		const auto info = open(session, fixturePath(name));
		EXPECT_EQ(info.error, pdf::Error::Format);
		EXPECT_FALSE(info.canceled);
		EXPECT_TRUE(info.pages.empty());
		EXPECT_TRUE(info.labels.empty());
		EXPECT_FALSE(session.render({0, 10, 10}, [](pdf::RenderResult) {}));
	}
}

TEST_F(SessionTest, EmptyPageTreeHasNoPagesToRender) {
	pdf::Session session(engine);
	const auto info = open(session, fixturePath("no_pages.pdf"));
	ASSERT_EQ(info.error, pdf::Error::None);
	EXPECT_FALSE(info.canceled);
	EXPECT_TRUE(info.pages.empty());
	EXPECT_TRUE(info.labels.empty());
	EXPECT_FALSE(render(session, {0, 10, 10}).bitmap);
}

TEST_F(SessionTest, OpensOnlyOnce) {
	pdf::Session session(engine);
	open(session, fixturePath("pages.pdf"));
	EXPECT_FALSE(
	    session.open(fixturePath("pages.pdf"), {}, [](pdf::SessionInfo) {}));
}

TEST_F(SessionTest, OpensAndRendersAUnicodePathWithSpaces) {
	const auto folder = tempDir() / L"Zo\u00eb \u65e5\u672c\u8a9e \U0001f4d6";
	ASSERT_TRUE(fs::create_directories(folder));
	const auto path = folder / L"-reading notes \u00fc.pdf";
	ASSERT_TRUE(fs::copy_file(fixturePath("pages.pdf"), path));
	pdf::Session session(engine);
	const auto info = open(session, path);
	ASSERT_EQ(info.error, pdf::Error::None);
	ASSERT_EQ(info.pages.size(), 3u);
	EXPECT_EQ(info.labels, (std::vector<std::string>{"i", "ii", "1"}));
	const auto result = render(session, {2, 80, 120});
	EXPECT_FALSE(result.canceled);
	ASSERT_TRUE(result.bitmap);
	EXPECT_EQ(result.bitmap->width, 80);
	EXPECT_EQ(result.bitmap->height, 120);
}

TEST_F(SessionTest, SourceCanBeRenamedReplacedAndDeletedWhileOpen) {
	const auto path = writeTemp("read while open.pdf", manyPages(3));
	pdf::Session session(engine);
	ASSERT_EQ(open(session, path).pages.size(), 3u);
	const auto before = render(session, {2, 60, 85});
	ASSERT_TRUE(before.bitmap);

	std::error_code ec;
	const auto renamed = path.parent_path() / "renamed.pdf";
	fs::rename(path, renamed, ec);
	ASSERT_FALSE(ec) << ec.message();
	// the open session must keep its own bytes, not read the new file
	{
		std::ofstream out(renamed, std::ios::binary | std::ios::trunc);
		out << manyPages(1);
		out.close();
		ASSERT_FALSE(out.fail());
	}
	pdf::Session replacement(engine);
	ASSERT_EQ(open(replacement, renamed).pages.size(), 1u);
	ASSERT_TRUE(fs::remove(renamed, ec)) << ec.message();
	ASSERT_FALSE(ec) << ec.message();

	const auto after = render(session, {2, 60, 85});
	EXPECT_FALSE(after.canceled);
	ASSERT_TRUE(after.bitmap);
	EXPECT_EQ(after.bitmap->pixels, before.bitmap->pixels);
	EXPECT_TRUE(render(replacement, {0, 60, 85}).bitmap);
}

TEST_F(SessionTest, NeedsACallback) {
	pdf::Session session(engine);
	EXPECT_FALSE(session.open(fixturePath("pages.pdf"), {}, {}));
	// that didn't count as the one open
	open(session, fixturePath("pages.pdf"));
	EXPECT_FALSE(session.render({0, 10, 10}, {}));
}

TEST_F(SessionTest, RendersNothingBeforeOpening) {
	pdf::Session session(engine);
	EXPECT_FALSE(session.render({0, 10, 10}, [](pdf::RenderResult) {}));
}

TEST_F(SessionTest, RendersAPage) {
	pdf::Session session(engine);
	open(session, fixturePath("pages.pdf"));
	const pdf::PageKey key{1, 264, 204};
	const auto result = render(session, key);
	EXPECT_EQ(result.key, key);
	EXPECT_FALSE(result.canceled);
	ASSERT_TRUE(result.bitmap);
	EXPECT_EQ(result.bitmap->width, 264);
	EXPECT_EQ(result.bitmap->height, 204);
}

TEST_F(SessionTest, APageThatIsntThereRendersNothing) {
	pdf::Session session(engine);
	open(session, fixturePath("pages.pdf"));
	const auto result = render(session, {3, 10, 10});
	EXPECT_FALSE(result.canceled);
	EXPECT_FALSE(result.bitmap);
}

TEST_F(SessionTest, RendersOneAtATime) {
	pdf::Session session(engine);
	open(session, fixturePath("pages.pdf"));
	Gate gate(engine);
	Reply<pdf::RenderResult> first;
	ASSERT_TRUE(session.render({0, 60, 85}, first.callback()));
	EXPECT_FALSE(session.render({1, 60, 46}, [](pdf::RenderResult) {}));
	gate.open();
	const auto done = first.wait();
	ASSERT_TRUE(done);
	EXPECT_TRUE(done->bitmap);
	// the slot is free again once the callback has run
	EXPECT_TRUE(render(session, {1, 60, 46}).bitmap);
}

TEST_F(SessionTest, CancelDropsTheResult) {
	pdf::Session session(engine);
	open(session, fixturePath("pages.pdf"));
	Gate gate(engine);
	Reply<pdf::RenderResult> reply;
	ASSERT_TRUE(session.render({0, 60, 85}, reply.callback()));
	session.cancelRender();
	// still busy until the canceled job calls back
	EXPECT_FALSE(session.render({0, 60, 85}, [](pdf::RenderResult) {}));
	gate.open();
	const auto canceled = reply.wait();
	ASSERT_TRUE(canceled);
	EXPECT_TRUE(canceled->canceled);
	EXPECT_FALSE(canceled->bitmap);
	const auto next = render(session, {0, 60, 85});
	EXPECT_FALSE(next.canceled);
	EXPECT_TRUE(next.bitmap);
}

TEST_F(SessionTest, ACallbackCanAskForTheNextPage) {
	pdf::Session session(engine);
	open(session, fixturePath("pages.pdf"));
	std::atomic<bool> queued{false};
	Reply<pdf::RenderResult> second;
	ASSERT_TRUE(session.render(
	    {0, 60, 85},
	    [&session, &queued, next = second.callback()](pdf::RenderResult) {
		    queued = session.render({1, 60, 46}, next);
		    if (!queued) {
			    next({{}, std::nullopt});
		    }
	    }));
	const auto result = second.wait();
	ASSERT_TRUE(result);
	EXPECT_TRUE(queued);
	EXPECT_EQ(result->key.page, 1);
	EXPECT_TRUE(result->bitmap);
}

TEST_F(SessionTest, CloseCancelsAnOpenStillQueued) {
	pdf::Session session(engine);
	Gate gate(engine);
	Reply<pdf::SessionInfo> reply;
	ASSERT_TRUE(session.open(fixturePath("pages.pdf"), {}, reply.callback()));
	session.close();
	gate.open();
	const auto info = reply.wait();
	ASSERT_TRUE(info);
	EXPECT_TRUE(info->canceled);
	EXPECT_TRUE(info->pages.empty());
	EXPECT_TRUE(info->labels.empty());
	EXPECT_FALSE(session.render({0, 10, 10}, [](pdf::RenderResult) {}));
	// closing twice is fine
	session.close();
}

TEST_F(SessionTest, CloseCancelsARenderStillQueued) {
	pdf::Session session(engine);
	open(session, fixturePath("pages.pdf"));
	Gate gate(engine);
	Reply<pdf::RenderResult> reply;
	ASSERT_TRUE(session.render({0, 60, 85}, reply.callback()));
	session.close();
	gate.open();
	const auto result = reply.wait();
	ASSERT_TRUE(result);
	EXPECT_TRUE(result->canceled);
	EXPECT_FALSE(result->bitmap);
}

TEST_F(SessionTest, DroppingTheSessionCancelsItsWork) {
	Gate gate(engine);
	Reply<pdf::SessionInfo> reply;
	{
		pdf::Session session(engine);
		ASSERT_TRUE(
		    session.open(fixturePath("pages.pdf"), {}, reply.callback()));
	}
	gate.open();
	const auto info = reply.wait();
	ASSERT_TRUE(info);
	EXPECT_TRUE(info->canceled);
}
} // namespace

TEST_F(SessionTest, FindReturnsDocumentRangesAndCanBeCanceled) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, fixturePath("text.pdf")).error, pdf::Error::None);
	Reply<pdf::FindUpdate> reply;
	ASSERT_TRUE(session.search(u"alpha", {}, reply.callback()));
	const auto found = reply.wait();
	ASSERT_TRUE(found);
	EXPECT_TRUE(found->done);
	EXPECT_FALSE(found->failed);
	EXPECT_EQ(found->matches.size(), 4u);
	EXPECT_EQ(found->pagesScanned, 5);
	ASSERT_FALSE(found->matches.empty());
	EXPECT_EQ(found->matches.front(), (pdf::FindMatch{0, 0, 5}));
	Gate gate(engine);
	Reply<pdf::FindUpdate> canceled;
	ASSERT_TRUE(session.search(u"alpha", {}, canceled.callback()));
	session.cancelSearch();
	gate.open();
	const auto result = canceled.wait();
	ASSERT_TRUE(result);
	EXPECT_TRUE(result->canceled);
	EXPECT_TRUE(result->done);
	EXPECT_TRUE(result->matches.empty());
}
TEST_F(SessionTest, FindYieldsBetweenPagesAndCloseStopsTheRemainingScan) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, writeTemp("search.pdf", manyPages(1000))).error,
	          pdf::Error::None);
	Reply<pdf::FindUpdate> reply;
	const auto marker = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.search(
	    u"missing", {},
	    [&, marker, callback = reply.callback()](pdf::FindUpdate update) {
		    if (update.done) {
			    callback(std::move(update));
			    return;
		    }
		    engine.post([marker] { *marker = true; });
		    session.close();
	    }));
	const auto result = reply.wait();
	ASSERT_TRUE(result);
	EXPECT_TRUE(result->canceled);
	EXPECT_TRUE(*marker);
	EXPECT_EQ(result->pagesScanned, 16);
	EXPECT_FALSE(session.search(u"a", {}, [](auto) {}));
}

TEST_F(SessionTest, FindSupersedesQueuedQueryAndRejectsInvalidInput) {
	pdf::Session session(engine);
	Reply<pdf::FindUpdate> unopened;
	EXPECT_FALSE(session.search(u"a", {}, unopened.callback()));
	ASSERT_EQ(open(session, fixturePath("text.pdf")).error, pdf::Error::None);
	Gate gate(engine);
	Reply<pdf::FindUpdate> stale, current;
	ASSERT_TRUE(session.search(u"alpha", {}, stale.callback()));
	ASSERT_TRUE(session.search(u"Second", {}, current.callback()));
	gate.open();
	const auto old = stale.wait(), now = current.wait();
	ASSERT_TRUE(old && now);
	EXPECT_TRUE(old->canceled);
	EXPECT_FALSE(now->canceled);
	EXPECT_EQ(now->matches.size(), 4u);
	Reply<pdf::FindUpdate> invalid;
	ASSERT_TRUE(
	    session.search(std::u16string(4097, u'a'), {}, invalid.callback()));
	const auto rejected = invalid.wait();
	ASSERT_TRUE(rejected);
	EXPECT_TRUE(rejected->failed);
}

TEST_F(SessionTest, AttachmentsStayOnTheEngineThreadAndCloseDiscardsData) {
	pdf::Session session(engine);
	Reply<pdf::AttachmentsResult> unopened;
	EXPECT_FALSE(session.attachments(unopened.callback()));
	EXPECT_FALSE(session.attachment(0, {}));
	ASSERT_EQ(open(session, fixturePath("attachments.pdf")).error,
	          pdf::Error::None);
	Reply<pdf::AttachmentsResult> list;
	ASSERT_TRUE(session.attachments(list.callback()));
	const auto found = list.wait();
	ASSERT_TRUE(found && found->attachments);
	EXPECT_EQ(found->attachments->entries.size(), 4u);
	Reply<pdf::AttachmentResult> data;
	const auto onThread = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.attachment(0, [onThread, cb = data.callback()](auto r) {
		*onThread = pdf::Engine::onThread();
		cb(std::move(r));
	}));
	const auto extracted = data.wait();
	ASSERT_TRUE(extracted && extracted->bytes);
	EXPECT_EQ(extracted->bytes->size(), 8u);
	EXPECT_TRUE(*onThread);
	Gate gate(engine);
	Reply<pdf::AttachmentResult> canceled;
	Reply<pdf::AttachmentsResult> canceledList;
	ASSERT_TRUE(session.attachment(0, canceled.callback()));
	ASSERT_TRUE(session.attachments(canceledList.callback()));
	session.close();
	gate.open();
	const auto closed = canceled.wait();
	const auto closedList = canceledList.wait();
	ASSERT_TRUE(closed && closedList);
	EXPECT_TRUE(closed->canceled && closedList->canceled);
	EXPECT_FALSE(closed->bytes);
	EXPECT_FALSE(closedList->attachments);
	EXPECT_FALSE(session.attachment(0, data.callback()));
}

TEST_F(SessionTest, OutlinesReturnOnTheEngineThreadAndCloseDiscardsQueuedData) {
	pdf::Session session(engine);
	Reply<pdf::OutlineResult> unopened;
	EXPECT_FALSE(session.outline(unopened.callback()));
	ASSERT_EQ(open(session, fixturePath("outline.pdf")).error,
	          pdf::Error::None);
	Reply<pdf::OutlineResult> reply;
	const auto onThread = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.outline(
	    [onThread, callback = reply.callback()](pdf::OutlineResult result) {
		    *onThread = pdf::Engine::onThread();
		    callback(std::move(result));
	    }));
	const auto found = reply.wait();
	ASSERT_TRUE(found && found->outline);
	EXPECT_TRUE(*onThread);
	EXPECT_FALSE(found->canceled);
	EXPECT_EQ(found->outline->entries.size(), 8u);
	{
		Gate queued(engine);
		Reply<pdf::LinkResult> stale, latest;
		ASSERT_TRUE(session.outlineLink(1, stale.callback()));
		ASSERT_TRUE(session.outlineLink(2, latest.callback()));
		queued.open();
		const auto old = stale.wait(), next = latest.wait();
		ASSERT_TRUE(old && next);
		EXPECT_TRUE(old->canceled);
		ASSERT_TRUE(next->link && next->link->destination);
		EXPECT_EQ(next->link->destination->page, 2);
	}

	Gate gate(engine);
	Reply<pdf::OutlineResult> canceled;
	ASSERT_TRUE(session.outline(canceled.callback()));
	session.close();
	gate.open();
	const auto closed = canceled.wait();
	ASSERT_TRUE(closed);
	EXPECT_TRUE(closed->canceled);
	EXPECT_FALSE(closed->outline);
	EXPECT_FALSE(session.outline([](auto) {}));
}

TEST_F(SessionTest, OpenReportsAnnotationPermissionIndependentlyOfCopying) {
	for (const auto name : {"annotations.pdf", "notes_allowed.pdf",
	                        "notes_denied.pdf", "restricted.pdf"}) {
		pdf::Session session(engine);
		const auto info = open(session, fixturePath(name));
		ASSERT_EQ(info.error, pdf::Error::None);
		EXPECT_EQ(info.canAnnotate,
		          std::string_view(name) == "annotations.pdf" ||
		              std::string_view(name) == "notes_allowed.pdf");
	}
	pdf::Session owner(engine), broken(engine), canceled(engine);
	EXPECT_TRUE(
	    open(owner, fixturePath("restricted.pdf"), "nightpdf").canAnnotate);
	const auto failed = open(broken, fixturePath("broken.pdf"));
	EXPECT_EQ(failed.error, pdf::Error::Format);
	EXPECT_FALSE(failed.canAnnotate);
	Gate gate(engine);
	Reply<pdf::SessionInfo> reply;
	ASSERT_TRUE(
	    canceled.open(fixturePath("annotations.pdf"), {}, reply.callback()));
	canceled.close();
	gate.open();
	const auto stopped = reply.wait();
	ASSERT_TRUE(stopped);
	EXPECT_TRUE(stopped->canceled);
	EXPECT_FALSE(stopped->canAnnotate);
}

TEST_F(SessionTest, NoteRequestsOwnTheirTextAndKeepQueueOrder) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, fixturePath("annotations.pdf")).error,
	          pdf::Error::None);
	Gate gate(engine);
	Reply<pdf::NoteResult> first, second;
	const auto onThread = std::make_shared<std::atomic<bool>>(false);
	std::u16string text = u"Notes \u03a9\U0001f600\nsecond line",
	               author = u"Abhilash \u03a9";
	ASSERT_TRUE(
	    session.addTextNote(1, 40, 50, text, author,
	                        [onThread, cb = first.callback()](auto result) {
		                        *onThread = pdf::Engine::onThread();
		                        cb(std::move(result));
	                        }));
	text.assign(u"Changed");
	author.clear();
	ASSERT_TRUE(
	    session.addTextNote(1, 80, 100, u"Second", {}, second.callback()));
	session.cancelAnnotations();
	session.cancelRender();
	session.cancelSearch();
	Reply<pdf::AnnotationsResult> inventory;
	Reply<pdf::RenderResult> raster;
	Reply<pdf::FindUpdate> find;
	ASSERT_TRUE(session.annotations(1, inventory.callback()));
	ASSERT_TRUE(session.render({1, 180, 140}, raster.callback()));
	ASSERT_TRUE(session.search(u"note", {}, find.callback()));
	gate.open();
	const auto a = first.wait(), b = second.wait();
	ASSERT_TRUE(a && b);
	EXPECT_TRUE(*onThread);
	EXPECT_EQ(a->page, 1);
	EXPECT_EQ(b->page, 1);
	EXPECT_EQ(a->index, 0);
	EXPECT_EQ(b->index, 1);
	EXPECT_FALSE(a->canceled || b->canceled);
	const auto notes = inventory.wait();
	ASSERT_TRUE(notes && notes->annotations);
	ASSERT_EQ(notes->annotations->entries.size(), 2u);
	EXPECT_EQ(notes->annotations->entries[0].contents,
	          "Notes \u03a9\U0001f600\nsecond line");
	EXPECT_EQ(notes->annotations->entries[0].author, "Abhilash \u03a9");
	EXPECT_EQ(notes->annotations->entries[1].contents, "Second");
	const auto rendered = raster.wait();
	ASSERT_TRUE(rendered);
	EXPECT_FALSE(rendered->canceled);
	EXPECT_TRUE(rendered->bitmap);
	const auto searched = find.wait();
	ASSERT_TRUE(searched);
	EXPECT_TRUE(searched->done);
	EXPECT_FALSE(searched->failed || searched->canceled);
}

TEST_F(SessionTest, NoteRequestsRejectUnavailableSessionsAndReportFailures) {
	pdf::Session session(engine), denied(engine);
	Reply<pdf::NoteResult> unused;
	EXPECT_FALSE(
	    session.addTextNote(1, 40, 50, u"Note", {}, unused.callback()));
	ASSERT_EQ(open(session, fixturePath("annotations.pdf")).error,
	          pdf::Error::None);
	EXPECT_FALSE(session.addTextNote(1, 40, 50, u"Note", {}, {}));
	const std::u16string large(32768, u'a');
	EXPECT_FALSE(session.addTextNote(1, 40, 50, large, {}, unused.callback()));
	EXPECT_FALSE(
	    session.addTextNote(1, 40, 50, u"Note", large, unused.callback()));
	for (const int page : {-1, 1, 5}) {
		Reply<pdf::NoteResult> reply;
		ASSERT_TRUE(
		    session.addTextNote(page, 0, 0, u"Outside", {}, reply.callback()));
		const auto result = reply.wait();
		ASSERT_TRUE(result);
		EXPECT_EQ(result->page, page);
		EXPECT_FALSE(result->index);
		EXPECT_FALSE(result->canceled);
	}
	ASSERT_EQ(open(denied, fixturePath("restricted.pdf")).error,
	          pdf::Error::None);
	Reply<pdf::NoteResult> failed;
	ASSERT_TRUE(
	    denied.addTextNote(0, 40, 50, u"Denied", {}, failed.callback()));
	const auto result = failed.wait();
	ASSERT_TRUE(result);
	EXPECT_EQ(result->page, 0);
	EXPECT_FALSE(result->index);
	EXPECT_FALSE(result->canceled);
}

TEST_F(SessionTest, CloseAndDestructionCancelQueuedNotesOnlyInTheirSession) {
	for (const bool destroy : {false, true}) {
		auto session = std::make_unique<pdf::Session>(engine);
		pdf::Session other(engine);
		ASSERT_EQ(open(*session, fixturePath("annotations.pdf")).error,
		          pdf::Error::None);
		ASSERT_EQ(open(other, fixturePath("annotations.pdf")).error,
		          pdf::Error::None);
		Gate gate(engine);
		Reply<pdf::NoteResult> pending, intact;
		const auto calls = std::make_shared<std::atomic<int>>(0);
		ASSERT_TRUE(
		    session->addTextNote(1, 40, 50, u"Closed", {},
		                         [calls, cb = pending.callback()](auto result) {
			                         ++*calls;
			                         cb(std::move(result));
		                         }));
		ASSERT_TRUE(
		    other.addTextNote(1, 40, 50, u"Open", {}, intact.callback()));
		if (destroy) {
			session.reset();
		} else {
			session->close();
			EXPECT_FALSE(session->addTextNote(1, 40, 50, u"Late", {},
			                                  pending.callback()));
		}
		gate.open();
		const auto closed = pending.wait(), live = intact.wait();
		ASSERT_TRUE(closed && live);
		EXPECT_EQ(closed->page, 1);
		EXPECT_TRUE(closed->canceled);
		EXPECT_FALSE(closed->index);
		EXPECT_FALSE(live->canceled);
		EXPECT_EQ(live->index, 0);
		engine.run([] {}).get();
		EXPECT_EQ(*calls, 1);
	}
}

TEST_F(SessionTest, NoteCallbacksCanQueueEditsAndCloseWithoutLosingTheResult) {
	for (const bool destroy : {false, true}) {
		auto session = std::make_unique<pdf::Session>(engine);
		ASSERT_EQ(open(*session, fixturePath("annotations.pdf")).error,
		          pdf::Error::None);
		Reply<pdf::NoteResult> reply;
		const auto queued = std::make_shared<std::atomic<bool>>(false);
		ASSERT_TRUE(session->addTextNote(
		    1, 40, 50, u"First", {},
		    [&session, destroy, queued, cb = reply.callback()](auto first) {
			    if (first.index != 0 || first.canceled) {
				    cb(std::move(first));
				    return;
			    }
			    *queued =
			        session->addTextNote(1, 80, 100, u"Next", {},
			                             [&session, destroy, cb](auto result) {
				                             if (destroy) {
					                             session.reset();
				                             } else {
					                             session->close();
				                             }
				                             cb(std::move(result));
			                             });
		    }));
		const auto result = reply.wait();
		ASSERT_TRUE(result);
		EXPECT_TRUE(*queued);
		EXPECT_EQ(result->page, 1);
		EXPECT_EQ(result->index, 1);
		EXPECT_FALSE(result->canceled);
		engine.run([] {}).get();
	}
}

TEST_F(SessionTest, ThrowingNoteCallbacksDoNotUndoOrRepeatTheEdit) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, fixturePath("annotations.pdf")).error,
	          pdf::Error::None);
	const auto calls = std::make_shared<std::atomic<int>>(0);
	Reply<pdf::NoteResult> first;
	ASSERT_TRUE(session.addTextNote(
	    1, 40, 50, u"First", {}, [calls, cb = first.callback()](auto result) {
		    ++*calls;
		    cb(std::move(result));
		    throw 1;
	    }));
	const auto added = first.wait();
	ASSERT_TRUE(added);
	EXPECT_EQ(added->index, 0);
	engine.run([] {}).get();
	EXPECT_EQ(*calls, 1);
	Reply<pdf::NoteResult> next;
	ASSERT_TRUE(session.addTextNote(1, 80, 100, u"Next", {}, next.callback()));
	const auto survived = next.wait();
	ASSERT_TRUE(survived);
	EXPECT_FALSE(survived->canceled);
	EXPECT_EQ(survived->index, 1);
}

TEST_F(SessionTest, EditedSavesWriteNotesAndProtectSourcesAndFailedTargets) {
	pdf::Session session(engine);
	const auto source = fixturePath("pages.pdf");
	const auto original = readFile(source);
	const auto target = writeTemp("edited.pdf", "previous");
	Reply<pdf::SaveResult> unused;
	EXPECT_FALSE(
	    session.saveEdits(target, source, 1024 * 1024, {}, unused.callback()));
	ASSERT_EQ(open(session, source).error, pdf::Error::None);
	Reply<pdf::NoteResult> note;
	ASSERT_TRUE(session.addTextNote(1, 40, 50, u"Saved \u03a9\U0001f600", {},
	                                note.callback()));
	const auto added = note.wait();
	ASSERT_TRUE(added && added->index);
	EXPECT_FALSE(session.saveEdits(target, source, 1024 * 1024, {}, {}));
	Reply<pdf::SaveResult> limited;
	const auto previous = readFile(target);
	ASSERT_TRUE(session.saveEdits(target, source, 0, {}, limited.callback()));
	EXPECT_EQ(limited.wait(), pdf::SaveResult::Failed);
	EXPECT_EQ(readFile(target), previous);
	Reply<pdf::SaveResult> protectedSource;
	ASSERT_TRUE(session.saveEdits(source, source, 1024 * 1024, {},
	                              protectedSource.callback()));
	EXPECT_EQ(protectedSource.wait(), pdf::SaveResult::Source);
	Reply<pdf::SaveResult> saved;
	const auto onThread = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(
	    session.saveEdits(target, source, 1024 * 1024, {},
	                      [onThread, cb = saved.callback()](auto result) {
		                      *onThread = pdf::Engine::onThread();
		                      cb(result);
	                      }));
	EXPECT_EQ(saved.wait(), pdf::SaveResult::Saved);
	EXPECT_TRUE(*onThread);
	EXPECT_EQ(readFile(source), original);
	engine
	    .run([&] {
		    const auto reopened = pdf::openFile(target);
		    ASSERT_TRUE(reopened.document);
		    const auto notes = reopened.document->annotations(1);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 1u);
		    EXPECT_EQ(notes->entries[0].contents, "Saved \u03a9\U0001f600");
	    })
	    .get();
	pdf::Session attachment(engine);
	const auto attachedSource = fixturePath("attachments.pdf");
	ASSERT_EQ(open(attachment, attachedSource).error, pdf::Error::None);
	Reply<pdf::NoteResult> attached;
	ASSERT_TRUE(
	    attachment.addTextNote(0, 40, 50, u"Retain", {}, attached.callback()));
	ASSERT_TRUE(attached.wait());
	const auto beforeRefusal = readFile(target);
	Reply<pdf::SaveResult> refused;
	ASSERT_TRUE(attachment.saveEdits(target, attachedSource, 1024 * 1024, {},
	                                 refused.callback()));
	EXPECT_EQ(refused.wait(), pdf::SaveResult::Failed);
	EXPECT_EQ(readFile(target), beforeRefusal);
}

TEST_F(SessionTest, SavingEditsKeepsTheOriginalDocumentModified) {
	const auto source = fixturePath("pages.pdf"),
	           target = tempDir() / "edited.pdf";
	const auto original = readFile(source);
	engine
	    .run([&] {
		    const auto opened = pdf::openFile(source);
		    ASSERT_TRUE(opened.document);
		    ASSERT_TRUE(
		        opened.document->addTextNote(1, 40, 50, u"Still modified"));
		    EXPECT_EQ(opened.document->saveEdits(target, source, 1024 * 1024),
		              pdf::SaveResult::Saved);
		    EXPECT_TRUE(opened.document->modified());
	    })
	    .get();
	EXPECT_EQ(readFile(source), original);
}

TEST_F(SessionTest, EditedSavesUseOneSlotAndKeepCancellationSticky) {
	pdf::Session session(engine);
	const auto source = fixturePath("pages.pdf");
	const auto target = writeTemp("edited.pdf", "previous");
	const auto previous = readFile(target);
	ASSERT_EQ(open(session, source).error, pdf::Error::None);
	Gate gate(engine);
	Reply<pdf::SaveResult> first, busy;
	const auto checks = std::make_shared<std::atomic<int>>(0);
	ASSERT_TRUE(session.saveEdits(
	    target, source, 1024 * 1024, [checks] { return ++*checks == 2; },
	    first.callback()));
	EXPECT_FALSE(
	    session.saveEdits(target, source, 1024 * 1024, {}, busy.callback()));
	session.cancelRender();
	session.cancelAnnotations();
	session.cancelSearch();
	gate.open();
	EXPECT_EQ(first.wait(), pdf::SaveResult::Canceled);
	EXPECT_EQ(readFile(target), previous);
	Reply<pdf::SaveResult> retry;
	ASSERT_TRUE(
	    session.saveEdits(target, source, 1024 * 1024, {}, retry.callback()));
	EXPECT_EQ(retry.wait(), pdf::SaveResult::Saved);
	EXPECT_EQ(readFile(target), readFile(source));
}

TEST_F(SessionTest, CloseAndDestructionCancelQueuedEditSavesOnce) {
	for (const bool destroy : {false, true}) {
		auto session = std::make_unique<pdf::Session>(engine);
		const auto source = fixturePath("pages.pdf");
		const auto target = writeTemp("edited.pdf", "previous");
		const auto previous = readFile(target);
		ASSERT_EQ(open(*session, source).error, pdf::Error::None);
		Gate gate(engine);
		Reply<pdf::SaveResult> pending;
		const auto calls = std::make_shared<std::atomic<int>>(0);
		ASSERT_TRUE(
		    session->saveEdits(target, source, 1024 * 1024, {},
		                       [calls, cb = pending.callback()](auto result) {
			                       ++*calls;
			                       cb(result);
		                       }));
		if (destroy) {
			session.reset();
		} else {
			session->close();
			EXPECT_FALSE(session->saveEdits(target, source, 1024 * 1024, {},
			                                pending.callback()));
		}
		gate.open();
		EXPECT_EQ(pending.wait(), pdf::SaveResult::Canceled);
		EXPECT_EQ(readFile(target), previous);
		engine.run([] {}).get();
		EXPECT_EQ(*calls, 1);
	}
}

TEST_F(SessionTest, EditSaveCallbacksCanQueueSavesThrowAndClose) {
	pdf::Session session(engine);
	const auto source = fixturePath("pages.pdf");
	const auto target = tempDir() / "first.pdf", next = tempDir() / "next.pdf";
	ASSERT_EQ(open(session, source).error, pdf::Error::None);
	Reply<pdf::SaveResult> reply;
	const auto queued = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.saveEdits(
	    target, source, 1024 * 1024, {},
	    [&session, source, next, queued, cb = reply.callback()](auto first) {
		    if (first != pdf::SaveResult::Saved) {
			    cb(first);
			    return;
		    }
		    *queued = session.saveEdits(next, source, 1024 * 1024, {},
		                                [&session, cb](auto result) {
			                                session.close();
			                                cb(result);
		                                });
		    throw 1;
	    }));
	EXPECT_EQ(reply.wait(), pdf::SaveResult::Saved);
	EXPECT_TRUE(*queued);
	EXPECT_EQ(readFile(target), readFile(source));
	EXPECT_EQ(readFile(next), readFile(source));
	engine.run([] {}).get();
}

TEST_F(SessionTest, CenteredNotesReportModificationAndProtectAttachments) {
	for (const auto name :
	     {"comment_locations.pdf", "attachments.pdf", "notes_denied.pdf"}) {
		pdf::Session session(engine);
		ASSERT_EQ(open(session, fixturePath(name)).error, pdf::Error::None);
		Reply<pdf::NoteResult> reply;
		ASSERT_TRUE(
		    session.addCenteredTextNote(0, u"Center", {}, reply.callback()));
		const auto result = reply.wait();
		ASSERT_TRUE(result);
		const bool allowed = std::string_view(name) == "comment_locations.pdf";
		EXPECT_EQ(result->index.has_value(), allowed);
		EXPECT_EQ(result->changed, allowed);
		EXPECT_FALSE(result->canceled);
		if (allowed) {
			Reply<pdf::NoteResult> refused;
			ASSERT_TRUE(session.addCenteredTextNote(-1, u"Rejected", {},
			                                        refused.callback()));
			const auto unchanged = refused.wait();
			ASSERT_TRUE(unchanged);
			EXPECT_FALSE(unchanged->changed);
			EXPECT_FALSE(unchanged->index);
		}
	}
}

TEST_F(SessionTest, PlacedNotesOwnTheirTargetAndKeepTheCloseBoundary) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, fixturePath("comment_locations.pdf")).error,
	          pdf::Error::None);
	Reply<pdf::NoteResult> first, refused, canceled;
	std::u16string draft = u"Placed \u03a9";
	ASSERT_TRUE(
	    session.addTextNoteAt(1, .25, .7, 1, draft, {}, first.callback()));
	draft.assign(u"Different");
	const auto added = first.wait();
	ASSERT_TRUE(added && added->index && added->changed);
	Reply<pdf::AnnotationsResult> inventory;
	ASSERT_TRUE(session.annotations(1, inventory.callback()));
	const auto notes = inventory.wait();
	ASSERT_TRUE(notes && notes->annotations &&
	            !notes->annotations->entries.empty());
	const auto& note = notes->annotations->entries.back();
	EXPECT_EQ(note.contents, "Placed \u03a9");
	ASSERT_TRUE(note.position);
	EXPECT_NEAR(note.position->x, .7, 2e-6);
	EXPECT_NEAR(note.position->y, .75, 2e-6);
	ASSERT_TRUE(
	    session.addTextNoteAt(1, .5, .5, 4, u"No", {}, refused.callback()));
	const auto invalid = refused.wait();
	ASSERT_TRUE(invalid);
	EXPECT_FALSE(invalid->changed);
	{
		Gate gate(engine);
		ASSERT_TRUE(session.addTextNoteAt(1, .3, .2, 0, u"Closed", {},
		                                  canceled.callback()));
		session.close();
	}
	const auto closed = canceled.wait();
	ASSERT_TRUE(closed);
	EXPECT_TRUE(closed->canceled);
	EXPECT_FALSE(closed->changed);
}

TEST_F(SessionTest, AnnotationsReturnPageSnapshotsOnTheEngineThread) {
	pdf::Session session(engine);
	Reply<pdf::AnnotationsResult> unopened;
	EXPECT_FALSE(session.annotations(0, unopened.callback()));
	ASSERT_EQ(open(session, fixturePath("annotations.pdf")).error,
	          pdf::Error::None);
	EXPECT_FALSE(session.annotations(0, {}));
	Reply<pdf::AnnotationsResult> reply;
	const auto onThread = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(
	    session.annotations(0, [onThread, cb = reply.callback()](auto result) {
		    *onThread = pdf::Engine::onThread();
		    cb(std::move(result));
	    }));
	const auto found = reply.wait();
	ASSERT_TRUE(found && found->annotations);
	EXPECT_TRUE(*onThread);
	EXPECT_EQ(found->page, 0);
	EXPECT_FALSE(found->canceled);
	ASSERT_EQ(found->annotations->entries.size(), 6u);
	EXPECT_EQ(found->annotations->entries[0].contents,
	          "Notes \u03a9\U0001f600\nsecond line");
	for (const int page : {1, 2, -1, 5}) {
		Reply<pdf::AnnotationsResult> next;
		ASSERT_TRUE(session.annotations(page, next.callback()));
		const auto result = next.wait();
		ASSERT_TRUE(result);
		EXPECT_EQ(result->page, page);
		EXPECT_FALSE(result->canceled);
		if (page < 0 || page == 5) {
			EXPECT_FALSE(result->annotations);
		} else {
			ASSERT_TRUE(result->annotations);
			EXPECT_EQ(result->annotations->page, page);
			EXPECT_EQ(result->annotations->limited, page == 2);
			EXPECT_EQ(result->annotations->entries.size(), page == 1 ? 0u : 1u);
		}
	}
}

TEST_F(SessionTest, AnnotationRequestsSupersedeOnlyTheirOwnWork) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, fixturePath("annotations.pdf")).error,
	          pdf::Error::None);
	{
		Gate gate(engine);
		Reply<pdf::AnnotationsResult> old, latest;
		ASSERT_TRUE(session.annotations(0, old.callback()));
		ASSERT_TRUE(session.annotations(1, latest.callback()));
		session.cancelRender();
		session.cancelSearch();
		gate.open();
		const auto stale = old.wait(), current = latest.wait();
		ASSERT_TRUE(stale && current && current->annotations);
		EXPECT_EQ(stale->page, 0);
		EXPECT_TRUE(stale->canceled);
		EXPECT_FALSE(stale->annotations);
		EXPECT_FALSE(current->canceled);
		EXPECT_EQ(current->annotations->page, 1);
	}
	{
		Gate gate(engine);
		Reply<pdf::AnnotationsResult> annotations;
		Reply<pdf::RenderResult> raster;
		Reply<pdf::FindUpdate> find;
		ASSERT_TRUE(session.annotations(0, annotations.callback()));
		ASSERT_TRUE(session.render({0, 60, 85}, raster.callback()));
		ASSERT_TRUE(session.search(u"note", {}, find.callback()));
		session.cancelAnnotations();
		gate.open();
		const auto canceled = annotations.wait();
		const auto rendered = raster.wait();
		const auto searched = find.wait();
		ASSERT_TRUE(canceled && rendered && searched);
		EXPECT_TRUE(canceled->canceled);
		EXPECT_FALSE(canceled->annotations);
		EXPECT_FALSE(rendered->canceled);
		EXPECT_TRUE(rendered->bitmap);
		EXPECT_TRUE(searched->done);
		EXPECT_FALSE(searched->canceled || searched->failed);
	}
}

TEST_F(SessionTest, AnnotationCancellationIsIsolatedBetweenSessions) {
	pdf::Session first(engine), second(engine);
	ASSERT_EQ(open(first, fixturePath("annotations.pdf")).error,
	          pdf::Error::None);
	ASSERT_EQ(open(second, fixturePath("annotations.pdf")).error,
	          pdf::Error::None);
	Gate gate(engine);
	Reply<pdf::AnnotationsResult> canceled, intact;
	ASSERT_TRUE(first.annotations(0, canceled.callback()));
	ASSERT_TRUE(second.annotations(0, intact.callback()));
	first.cancelAnnotations();
	gate.open();
	const auto old = canceled.wait(), current = intact.wait();
	ASSERT_TRUE(old && current && current->annotations);
	EXPECT_TRUE(old->canceled);
	EXPECT_FALSE(current->canceled);
	EXPECT_EQ(current->annotations->entries.size(), 6u);
}

TEST_F(SessionTest, CloseAndDestructionCancelQueuedAnnotationRepliesOnce) {
	for (const bool destroy : {false, true}) {
		auto session = std::make_unique<pdf::Session>(engine);
		ASSERT_EQ(open(*session, fixturePath("annotations.pdf")).error,
		          pdf::Error::None);
		Gate gate(engine);
		Reply<pdf::AnnotationsResult> pending;
		const auto calls = std::make_shared<std::atomic<int>>(0);
		ASSERT_TRUE(session->annotations(
		    0, [calls, cb = pending.callback()](auto result) {
			    ++*calls;
			    cb(std::move(result));
		    }));
		if (destroy) {
			session.reset();
		} else {
			session->close();
			EXPECT_FALSE(session->annotations(0, pending.callback()));
		}
		gate.open();
		const auto canceled = pending.wait();
		ASSERT_TRUE(canceled);
		EXPECT_TRUE(canceled->canceled);
		EXPECT_FALSE(canceled->annotations);
		engine.run([] {}).get();
		EXPECT_EQ(*calls, 1);
	}
}

TEST_F(SessionTest, AnnotationCallbacksCanQueueWorkAndThrowWithoutBlocking) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, fixturePath("annotations.pdf")).error,
	          pdf::Error::None);
	Reply<pdf::AnnotationsResult> next;
	const auto queued = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.annotations(
	    0, [&session, queued, cb = next.callback()](auto result) {
		    if (result.annotations) {
			    *queued = session.annotations(1, cb);
		    }
	    }));
	const auto second = next.wait();
	ASSERT_TRUE(second && second->annotations);
	EXPECT_TRUE(*queued);
	EXPECT_EQ(second->page, 1);
	const auto calls = std::make_shared<std::atomic<int>>(0);
	ASSERT_TRUE(session.annotations(0, [calls](auto) {
		++*calls;
		throw 1;
	}));
	engine.run([] {}).get();
	EXPECT_EQ(*calls, 1);
	Reply<pdf::AnnotationsResult> after;
	ASSERT_TRUE(session.annotations(0, after.callback()));
	const auto survived = after.wait();
	ASSERT_TRUE(survived && survived->annotations);
	EXPECT_FALSE(survived->canceled);
}

TEST_F(SessionTest, ExistingNoteEditsOwnTextAndKeepNoOpAndStaleOutcomes) {
	pdf::Session session(engine);
	Reply<pdf::TextNoteResult> read;
	Reply<pdf::NoteResult> unused;
	EXPECT_FALSE(session.textNote(0, 0, read.callback()));
	EXPECT_FALSE(
	    session.updateTextNote(0, 0, u"Old", u"New", unused.callback()));
	ASSERT_EQ(open(session, fixturePath("comment_locations.pdf")).error,
	          pdf::Error::None);
	EXPECT_FALSE(session.textNote(0, 0, {}));
	EXPECT_FALSE(session.updateTextNote(0, 0, u"Old", u"New", {}));
	EXPECT_FALSE(session.updateTextNote(0, 0, std::u16string(32768, u'a'),
	                                    u"New", unused.callback()));
	ASSERT_TRUE(session.textNote(0, 0, read.callback()));
	const auto snapshot = read.wait();
	ASSERT_TRUE(snapshot && snapshot->contents);
	EXPECT_EQ(snapshot->page, 0);
	EXPECT_EQ(snapshot->index, 0);
	EXPECT_FALSE(snapshot->canceled);
	EXPECT_EQ(*snapshot->contents, u"Location 1");
	Gate gate(engine);
	Reply<pdf::NoteResult> same, changed, stale;
	ASSERT_TRUE(session.updateTextNote(0, 0, *snapshot->contents,
	                                   *snapshot->contents, same.callback()));
	std::u16string expected = *snapshot->contents,
	               replacement = u"Changed \u03a9";
	ASSERT_TRUE(session.updateTextNote(0, 0, expected, replacement,
	                                   changed.callback()));
	expected.clear();
	replacement.clear();
	ASSERT_TRUE(session.updateTextNote(0, 0, *snapshot->contents, u"Stale",
	                                   stale.callback()));
	session.cancelAnnotations();
	session.cancelSearch();
	session.cancelRender();
	gate.open();
	const auto a = same.wait(), b = changed.wait(), c = stale.wait();
	ASSERT_TRUE(a && b && c);
	EXPECT_EQ(a->index, 0);
	EXPECT_FALSE(a->changed);
	EXPECT_EQ(b->index, 0);
	EXPECT_TRUE(b->changed);
	EXPECT_FALSE(c->index);
	EXPECT_FALSE(c->changed);
	Reply<pdf::TextNoteResult> final;
	ASSERT_TRUE(session.textNote(0, 0, final.callback()));
	const auto result = final.wait();
	ASSERT_TRUE(result);
	EXPECT_EQ(result->contents, u"Changed \u03a9");
}

TEST_F(SessionTest, ExistingNoteCloseCancelsQueuedReadsAndEdits) {
	for (const bool destroy : {false, true}) {
		auto session = std::make_unique<pdf::Session>(engine);
		ASSERT_EQ(open(*session, fixturePath("comment_locations.pdf")).error,
		          pdf::Error::None);
		Gate gate(engine);
		Reply<pdf::TextNoteResult> read;
		Reply<pdf::NoteResult> edit;
		ASSERT_TRUE(session->textNote(0, 0, read.callback()));
		ASSERT_TRUE(session->updateTextNote(0, 0, u"Location 1", u"Closed",
		                                    edit.callback()));
		if (destroy) {
			session.reset();
		} else {
			session->close();
		}
		gate.open();
		const auto a = read.wait();
		const auto b = edit.wait();
		ASSERT_TRUE(a && b);
		EXPECT_TRUE(a->canceled && b->canceled);
		EXPECT_FALSE(a->contents || b->index || b->changed);
	}
}

TEST_F(SessionTest, HighlightsOwnSelectionsAndKeepQueueAndCloseBoundaries) {
	pdf::Session session(engine);
	Reply<pdf::HighlightResult> unused;
	EXPECT_FALSE(session.highlightText({{0, 0}, {1, 4}}, unused.callback()));
	ASSERT_EQ(open(session, fixturePath("text.pdf")).error, pdf::Error::None);
	EXPECT_FALSE(session.highlightText({{0, 0}, {1, 4}}, {}));
	Gate gate(engine);
	pdf::TextSelection selection{{1, 4}, {0, 0}};
	Reply<pdf::HighlightResult> first, second;
	ASSERT_TRUE(session.highlightText(selection, first.callback()));
	selection = {{-1, -1}, {-1, -1}};
	ASSERT_TRUE(
	    session.highlightText({{4, 0}, {4, INT_MAX}}, second.callback()));
	session.cancelAnnotations();
	session.cancelRender();
	session.cancelSearch();
	gate.open();
	const auto a = first.wait(), b = second.wait();
	ASSERT_TRUE(a && b);
	EXPECT_EQ(a->count, 2);
	EXPECT_EQ(b->count, 1);
	EXPECT_TRUE(a->changed && b->changed);
	EXPECT_FALSE(a->canceled || b->canceled);
	Reply<pdf::AnnotationsResult> inventory;
	ASSERT_TRUE(session.annotations(4, inventory.callback()));
	const auto notes = inventory.wait();
	ASSERT_TRUE(notes && notes->annotations &&
	            notes->annotations->entries.size() == 1);
	EXPECT_EQ(notes->annotations->entries[0].subtype, "Highlight");
	for (const bool destroy : {false, true}) {
		auto closing = std::make_unique<pdf::Session>(engine);
		ASSERT_EQ(open(*closing, fixturePath("text.pdf")).error,
		          pdf::Error::None);
		Gate closedGate(engine);
		Reply<pdf::HighlightResult> pending;
		ASSERT_TRUE(
		    closing->highlightText({{0, 0}, {1, 4}}, pending.callback()));
		if (destroy) {
			closing.reset();
		} else {
			closing->close();
		}
		closedGate.open();
		const auto result = pending.wait();
		ASSERT_TRUE(result);
		EXPECT_TRUE(result->canceled);
		EXPECT_EQ(result->count, 0);
		EXPECT_FALSE(result->changed);
	}
}

TEST_F(SessionTest, HighlightsRefuseWideSelectionsAndLeaveTheSessionUsable) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, writeTemp("wide.pdf", manyPages(129))).error,
	          pdf::Error::None);
	Reply<pdf::HighlightResult> reply;
	ASSERT_TRUE(
	    session.highlightText({{0, 0}, {128, INT_MAX}}, reply.callback()));
	const auto result = reply.wait();
	ASSERT_TRUE(result);
	EXPECT_FALSE(result->changed || result->canceled);
	EXPECT_EQ(result->count, 0);
	Reply<pdf::NoteResult> note;
	ASSERT_TRUE(
	    session.addCenteredTextNote(0, u"Still open", {}, note.callback()));
	const auto added = note.wait();
	ASSERT_TRUE(added && added->index == 0);
}

TEST_F(SessionTest, LineMarkupOwnsItsTypeAndKeepsWriteAndCloseOrder) {
	for (const auto type :
	     {pdf::TextMarkup::Underline, pdf::TextMarkup::StrikeOut}) {
		pdf::Session session(engine);
		Reply<pdf::MarkupResult> first, second, unused;
		EXPECT_FALSE(
		    session.markText({{0, 0}, {0, 4}}, type, unused.callback()));
		ASSERT_EQ(open(session, fixturePath("text.pdf")).error,
		          pdf::Error::None);
		EXPECT_FALSE(session.markText({{0, 0}, {0, 4}}, type, {}));
		Gate gate(engine);
		auto markup = type;
		pdf::TextSelection selection{{1, 4}, {0, 0}};
		ASSERT_TRUE(session.markText(selection, markup, first.callback()));
		markup = static_cast<pdf::TextMarkup>(-1);
		selection = {{-1, 0}, {-1, 4}};
		ASSERT_TRUE(session.markText(selection, markup, unused.callback()));
		const auto other = type == pdf::TextMarkup::Underline
		                       ? pdf::TextMarkup::StrikeOut
		                       : pdf::TextMarkup::Underline;
		ASSERT_TRUE(
		    session.markText({{0, 0}, {0, 4}}, other, second.callback()));
		session.cancelRender();
		session.cancelSearch();
		session.cancelAnnotations();
		gate.open();
		const auto a = first.wait(), b = second.wait(), refused = unused.wait();
		ASSERT_TRUE(a && b && refused);
		EXPECT_EQ(a->count, 2);
		EXPECT_EQ(b->count, 1);
		EXPECT_TRUE(a->changed && b->changed);
		EXPECT_FALSE(a->canceled || b->canceled || refused->canceled);
		EXPECT_FALSE(refused->changed);
		Reply<pdf::AnnotationsResult> inventory;
		ASSERT_TRUE(session.annotations(0, inventory.callback()));
		const auto notes = inventory.wait();
		ASSERT_TRUE(notes && notes->annotations &&
		            notes->annotations->entries.size() == 2);
		EXPECT_EQ(notes->annotations->entries[0].subtype,
		          type == pdf::TextMarkup::Underline ? "Underline"
		                                             : "StrikeOut");
		EXPECT_EQ(notes->annotations->entries[1].subtype,
		          type == pdf::TextMarkup::Underline ? "StrikeOut"
		                                             : "Underline");
		Gate closeGate(engine);
		Reply<pdf::MarkupResult> started, canceled;
		ASSERT_TRUE(
		    session.markText({{4, 0}, {4, 2}}, type,
		                     [&session, reply = started.callback()](auto r) {
			                     session.close();
			                     reply(r);
		                     }));
		ASSERT_TRUE(
		    session.markText({{0, 0}, {0, 4}}, type, canceled.callback()));
		closeGate.open();
		const auto finished = started.wait(), closed = canceled.wait();
		ASSERT_TRUE(finished && closed);
		EXPECT_TRUE(finished->changed);
		EXPECT_FALSE(finished->canceled);
		EXPECT_EQ(finished->count, 1);
		EXPECT_TRUE(closed->canceled);
		EXPECT_FALSE(closed->changed);
		EXPECT_EQ(closed->count, 0);
	}
}
