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
std::string manyPages(int count, bool form = false, bool checkbox = false,
                      int choiceFlags = -1) {
	std::string pdf = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	const auto add = [&](const std::string& body) {
		offsets.push_back(pdf.size());
		pdf +=
		    std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
	};
	const auto field = std::to_string(count + 3) + " 0 R";
	const auto note = std::to_string(count + 4) + " 0 R";
	add("<< /Type /Catalog /Pages 2 0 R /PageLabels << /Nums [0 << /S /r >> "
	    "2 << /S /D >>] >> " +
	    (form ? "/AcroForm << /Fields [" + field + "] " +
	                (checkbox ? "/NeedAppearances false " : "") +
	                "/DA (/Helv 10 Tf 0 g) /DR << /Font << /Helv "
	                "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> "
	                ">> >> >> "
	          : "") +
	    ">>");
	std::string kids;
	for (int i = 0; i < count; ++i) {
		kids += std::to_string(3 + i) + " 0 R ";
	}
	add("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(count) +
	    " >>");
	for (int i = 0; i < count; ++i) {
		add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] " +
		    (form && i == 0 ? "/Annots [" + note + " " + field + "] " : "") +
		    ">>");
	}
	if (form) {
		add("<< /Type /Annot /Subtype /Widget " +
		    (choiceFlags >= 0
		         ? "/FT /Ch /T (field) /Ff " + std::to_string(choiceFlags) +
		               " /V (old) /I [0] /Opt [[(old) (First)] "
		               "[<FEFF00630061006600E9> (Second)] "
		               "[<FEFF00630061006600E9> (Third)]] "
		     : checkbox ? "/FT /Btn /T (field) /V /Off /AS /Off /F 4 "
		                  "/AP << /N << /Approved#20now " +
		                      std::to_string(count + 5) + " 0 R /Off " +
		                      std::to_string(count + 6) + " 0 R >> >> "
		                : "/FT /Tx /T (field) /V (value) ") +
		    "/Rect [40 50 90 70] /P 3 0 R >>");
		add("<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] "
		    "/Contents (Original) /P 3 0 R >>");
		if (checkbox) {
			const std::string paint = "0 0 0 rg 0 0 50 20 re f";
			add("<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] /Length " +
			    std::to_string(paint.size()) + " >>\nstream\n" + paint +
			    "\nendstream");
			add("<< /Type /XObject /Subtype /Form /BBox [0 0 50 20] "
			    "/Length 0 >>\nstream\nendstream");
		}
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

struct FileGate {
	std::promise<void> entered, release;
	std::shared_future<void> wait = release.get_future().share();
	std::atomic<bool> started = false;
	bool poll() {
		if (!pdf::Engine::onThread()) {
			if (!started.exchange(true)) {
				entered.set_value();
			}
			wait.wait();
		}
		return false;
	}
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

	std::shared_ptr<const pdf::FormFields> readForms(pdf::Session& session,
	                                                 int page = 0) {
		Reply<pdf::FormFieldsResult> reply;
		if (!session.formFields(page, reply.callback())) {
			ADD_FAILURE() << "form read wasn't accepted";
			return {};
		}
		const auto result = reply.wait();
		return result ? result->fields : nullptr;
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
		EXPECT_EQ(info.canFillForms, info.canAnnotate);
	}
	pdf::Session owner(engine), broken(engine), canceled(engine);
	EXPECT_TRUE(
	    open(owner, fixturePath("restricted.pdf"), "nightpdf").canAnnotate);
	const auto failed = open(broken, fixturePath("broken.pdf"));
	EXPECT_EQ(failed.error, pdf::Error::Format);
	EXPECT_FALSE(failed.canAnnotate);
	EXPECT_FALSE(failed.canFillForms);
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
	EXPECT_FALSE(stopped->canFillForms);
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

TEST_F(SessionTest, FormReadsOwnTheirSnapshotsAndCacheOnePage) {
	pdf::Session session(engine);
	Reply<pdf::FormFieldsResult> unopened;
	EXPECT_FALSE(session.formFields(0, unopened.callback()));
	ASSERT_EQ(open(session, writeTemp("forms.pdf", manyPages(2, true))).error,
	          pdf::Error::None);
	EXPECT_FALSE(session.formFields(0, {}));
	Reply<pdf::FormFieldsResult> first;
	auto onThread = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(
	    session.formFields(0, [cb = first.callback(), onThread](auto r) {
		    *onThread = pdf::Engine::onThread();
		    cb(std::move(r));
	    }));
	const auto found = first.wait();
	ASSERT_TRUE(found && found->fields);
	EXPECT_TRUE(*onThread);
	EXPECT_FALSE(found->canceled);
	ASSERT_EQ(found->fields->entries.size(), 1u);
	EXPECT_EQ(found->fields->entries[0].name, "field");
	EXPECT_EQ(found->fields->entries[0].value, "value");
	EXPECT_EQ(found->fields->entries[0].index, 1);
	{
		Gate gate(engine);
		Reply<pdf::FormFieldsResult> cached;
		auto called = std::make_shared<std::atomic<bool>>(false);
		ASSERT_TRUE(
		    session.formFields(0, [cb = cached.callback(), called](auto r) {
			    *called = true;
			    cb(std::move(r));
		    }));
		EXPECT_FALSE(*called);
		gate.open();
		const auto hit = cached.wait();
		ASSERT_TRUE(hit);
		EXPECT_EQ(hit->fields, found->fields);
	}
	EXPECT_FALSE(readForms(session, -1));
	EXPECT_FALSE(readForms(session, 2));
	EXPECT_EQ(readForms(session), found->fields);
	auto empty = readForms(session, 1);
	ASSERT_TRUE(empty);
	EXPECT_TRUE(empty->entries.empty());
	std::weak_ptr<const pdf::FormFields> evicted = empty;
	empty.reset();
	auto current = readForms(session);
	ASSERT_TRUE(current);
	EXPECT_NE(current, found->fields);
	EXPECT_TRUE(evicted.expired());
	std::weak_ptr<const pdf::FormFields> closed = current;
	current.reset();
	session.close();
	engine.run([] {}).get();
	EXPECT_TRUE(closed.expired());
	EXPECT_EQ(found->fields->entries[0].value, "value");
}

TEST_F(SessionTest, FormCancellationIsIndependentOfOtherRequestsAndSessions) {
	pdf::Session session(engine), other(engine);
	const auto path = writeTemp("forms.pdf", manyPages(2, true));
	ASSERT_EQ(open(session, path).error, pdf::Error::None);
	ASSERT_EQ(open(other, path).error, pdf::Error::None);
	{
		Gate gate(engine);
		Reply<pdf::FormFieldsResult> old, latest;
		ASSERT_TRUE(session.formFields(0, old.callback()));
		ASSERT_TRUE(session.formFields(1, latest.callback()));
		session.cancelRender();
		session.cancelSearch();
		session.cancelAnnotations();
		gate.open();
		const auto stale = old.wait(), current = latest.wait();
		ASSERT_TRUE(stale && current && current->fields);
		EXPECT_TRUE(stale->canceled);
		EXPECT_FALSE(stale->fields);
		EXPECT_FALSE(current->canceled);
		EXPECT_EQ(current->fields->page, 1);
	}
	{
		Gate gate(engine);
		Reply<pdf::FormFieldsResult> canceled, intact;
		Reply<pdf::AnnotationsResult> annotations;
		Reply<pdf::RenderResult> raster;
		Reply<pdf::FindUpdate> find;
		ASSERT_TRUE(session.formFields(0, canceled.callback()));
		ASSERT_TRUE(other.formFields(0, intact.callback()));
		ASSERT_TRUE(session.annotations(0, annotations.callback()));
		ASSERT_TRUE(session.render({0, 60, 85}, raster.callback()));
		ASSERT_TRUE(session.search(u"none", {}, find.callback()));
		session.cancelForms();
		gate.open();
		const auto stopped = canceled.wait(), live = intact.wait();
		const auto notes = annotations.wait();
		const auto rendered = raster.wait();
		const auto searched = find.wait();
		ASSERT_TRUE(stopped && live && live->fields && notes && rendered &&
		            searched);
		EXPECT_TRUE(stopped->canceled);
		EXPECT_FALSE(stopped->fields);
		EXPECT_FALSE(live->canceled || notes->canceled || rendered->canceled ||
		             searched->canceled);
		EXPECT_TRUE(notes->annotations && rendered->bitmap && searched->done);
	}
}

TEST_F(SessionTest, FormReadsCloseOnceAndCallbacksCanQueueWorkOrThrow) {
	const auto path = writeTemp("forms.pdf", manyPages(2, true));
	for (const bool destroy : {false, true}) {
		auto session = std::make_unique<pdf::Session>(engine);
		ASSERT_EQ(open(*session, path).error, pdf::Error::None);
		ASSERT_TRUE(readForms(*session));
		Gate gate(engine);
		Reply<pdf::FormFieldsResult> reply;
		auto calls = std::make_shared<std::atomic<int>>(0);
		ASSERT_TRUE(
		    session->formFields(0, [cb = reply.callback(), calls](auto r) {
			    ++*calls;
			    cb(std::move(r));
		    }));
		if (destroy) {
			session.reset();
		} else {
			session->close();
			EXPECT_FALSE(session->formFields(0, reply.callback()));
		}
		gate.open();
		const auto result = reply.wait();
		ASSERT_TRUE(result);
		EXPECT_TRUE(result->canceled);
		EXPECT_FALSE(result->fields);
		engine.run([] {}).get();
		EXPECT_EQ(*calls, 1);
	}
	pdf::Session session(engine);
	ASSERT_EQ(open(session, path).error, pdf::Error::None);
	Reply<pdf::FormFieldsResult> next;
	ASSERT_TRUE(session.formFields(0, [&session, cb = next.callback()](auto r) {
		if (r.fields) {
			session.formFields(1, cb);
		}
	}));
	const auto queued = next.wait();
	ASSERT_TRUE(queued && queued->fields);
	EXPECT_EQ(queued->page, 1);
	ASSERT_TRUE(session.formFields(0, [](auto) { throw 1; }));
	engine.run([] {}).get();
	EXPECT_TRUE(readForms(session));
}

TEST_F(SessionTest, FormWritesOwnQueueOrderRefreshSnapshotsAndSave) {
	pdf::Session fillOnly(engine);
	const auto permission = open(fillOnly, fixturePath("fill_allowed.pdf"));
	ASSERT_EQ(permission.error, pdf::Error::None);
	EXPECT_TRUE(permission.canFillForms);
	EXPECT_FALSE(permission.canAnnotate || permission.canCopy);
	pdf::Session session(engine);
	const auto source = writeTemp("writable form.pdf", manyPages(1, true));
	ASSERT_TRUE(open(session, source).canFillForms);
	const auto before = readForms(session);
	ASSERT_TRUE(before);
	Reply<pdf::FormWriteResult> first, stale;
	std::u16string draft = u"changed";
	{
		Gate gate(engine);
		ASSERT_TRUE(session.updateTextField(
		    0, 1, u"value", draft, before->revision, first.callback()));
		ASSERT_TRUE(session.updateTextField(
		    0, 1, u"changed", u"stale", before->revision, stale.callback()));
		draft.assign(u"different");
		session.cancelForms();
		gate.open();
	}
	const auto written = first.wait();
	const auto refused = stale.wait();
	ASSERT_TRUE(written && refused);
	EXPECT_TRUE(written->accepted && written->changed);
	EXPECT_FALSE(written->canceled);
	EXPECT_EQ(written->index, 1);
	EXPECT_GT(written->revision, before->revision);
	EXPECT_FALSE(refused->accepted || refused->changed || refused->canceled);
	const auto after = readForms(session);
	ASSERT_TRUE(after);
	EXPECT_NE(after, before);
	EXPECT_EQ(after->revision, written->revision);
	EXPECT_EQ(after->entries[0].value, "changed");
	EXPECT_EQ(before->entries[0].value, "value");
	Reply<pdf::FormWriteResult> invalid;
	ASSERT_TRUE(session.updateTextField(0, 1, u"wrong", u"ignored",
	                                    after->revision, invalid.callback()));
	const auto rejected = invalid.wait();
	ASSERT_TRUE(rejected);
	EXPECT_FALSE(rejected->accepted || rejected->changed || rejected->canceled);
	EXPECT_NE(readForms(session), after);
	Reply<pdf::NoteResult> note;
	ASSERT_TRUE(session.updateTextNote(0, 0, u"Original", u"Preserved",
	                                   note.callback()));
	const auto updatedNote = note.wait();
	ASSERT_TRUE(updatedNote);
	EXPECT_TRUE(updatedNote->index.has_value() && updatedNote->changed);
	const auto destination = source.parent_path() / "edited form.pdf";
	Reply<pdf::SaveResult> saved;
	ASSERT_TRUE(session.saveEdits(destination, source, 1024 * 1024, {},
	                              saved.callback()));
	EXPECT_EQ(saved.wait(), pdf::SaveResult::Saved);
	pdf::Session reopened(engine);
	ASSERT_EQ(open(reopened, destination).error, pdf::Error::None);
	const auto fields = readForms(reopened);
	ASSERT_TRUE(fields);
	EXPECT_EQ(fields->entries[0].value, "changed");
}

TEST_F(SessionTest, MultilineWritesOwnParagraphsAndKeepExactSnapshots) {
	pdf::Session session(engine);
	const auto bytes = mixedFormPdf(true, true);
	const std::vector<std::uint8_t> original(bytes.begin(), bytes.end());
	const auto source = writeTemp("multiline form.pdf", bytes);
	ASSERT_TRUE(open(session, source).canFillForms);
	const auto before = readForms(session);
	ASSERT_TRUE(before && before->entries.size() == 6);
	const int index = before->entries[0].index;
	EXPECT_EQ(before->entries[0].value, "first\nsecond");
	Reply<pdf::FormWriteResult> noOp;
	ASSERT_TRUE(session.updateTextField(0, index, u"first\nsecond",
	                                    u"first\nsecond", before->revision,
	                                    noOp.callback()));
	const auto same = noOp.wait();
	ASSERT_TRUE(same && same->accepted);
	EXPECT_FALSE(same->changed || same->canceled);
	EXPECT_EQ(same->revision, before->revision);
	const auto destination = source.parent_path() / "multiline copy.pdf";
	Reply<pdf::SaveResult> unedited;
	ASSERT_TRUE(session.saveEdits(destination, source, 1024 * 1024, {},
	                              unedited.callback()));
	ASSERT_EQ(unedited.wait(), pdf::SaveResult::Saved);
	pdf::Session unchanged(engine);
	ASSERT_EQ(open(unchanged, destination).error, pdf::Error::None);
	const auto raw = readForms(unchanged);
	ASSERT_TRUE(raw && raw->entries.size() == 6);
	EXPECT_EQ(raw->entries[0].value, "first\nsecond");
	EXPECT_EQ(readFile(source), original);
	Reply<pdf::FormWriteResult> first, stale;
	Reply<pdf::FormFieldsResult> canceled;
	std::u16string expected = u"first\nsecond";
	std::u16string draft = u"Caf\u00e9\n\n\U0001F600";
	{
		Gate gate(engine);
		ASSERT_TRUE(session.updateTextField(
		    0, index, expected, draft, before->revision, first.callback()));
		ASSERT_TRUE(session.updateTextField(
		    0, index, u"Caf\u00e9\r\n\r\n\U0001F600", u"stale",
		    before->revision, stale.callback()));
		ASSERT_TRUE(session.formFields(0, canceled.callback()));
		expected.assign(u"wrong");
		draft.assign(u"caller changed this");
		session.cancelForms();
	}
	const auto written = first.wait(), refused = stale.wait();
	const auto stopped = canceled.wait();
	ASSERT_TRUE(written && refused && stopped);
	EXPECT_TRUE(written->accepted && written->changed);
	EXPECT_FALSE(written->canceled);
	EXPECT_EQ(written->page, 0);
	EXPECT_EQ(written->index, index);
	EXPECT_GT(written->revision, before->revision);
	EXPECT_FALSE(refused->accepted || refused->changed || refused->canceled);
	EXPECT_TRUE(stopped->canceled);
	EXPECT_FALSE(stopped->fields);
	const auto after = readForms(session);
	ASSERT_TRUE(after && after->entries.size() == 6);
	EXPECT_NE(after, before);
	EXPECT_EQ(after->revision, written->revision);
	EXPECT_EQ(after->entries[0].value, "Caf\u00e9\r\n\r\n\U0001F600");
	EXPECT_EQ(before->entries[0].value, "first\nsecond");
	for (std::size_t i = 1; i < after->entries.size(); ++i) {
		EXPECT_EQ(after->entries[i].value, before->entries[i].value);
	}
	Reply<pdf::FormWriteResult> wrong, normalized;
	ASSERT_TRUE(session.updateTextField(0, index, u"Caf\u00e9\n\n\U0001F600",
	                                    u"ignored", after->revision,
	                                    wrong.callback()));
	ASSERT_TRUE(session.updateTextField(
	    0, index, u"Caf\u00e9\r\n\r\n\U0001F600", u"Caf\u00e9\n\n\U0001F600",
	    after->revision, normalized.callback()));
	const auto rejected = wrong.wait(), equivalent = normalized.wait();
	ASSERT_TRUE(rejected && equivalent);
	EXPECT_FALSE(rejected->accepted || rejected->changed || rejected->canceled);
	EXPECT_TRUE(equivalent->accepted);
	EXPECT_FALSE(equivalent->changed || equivalent->canceled);
	EXPECT_EQ(equivalent->revision, after->revision);
	Reply<pdf::SaveResult> saved;
	ASSERT_TRUE(session.saveEdits(destination, source, 1024 * 1024, {},
	                              saved.callback()));
	ASSERT_EQ(saved.wait(), pdf::SaveResult::Saved);
	pdf::Session reopened(engine);
	ASSERT_EQ(open(reopened, destination).error, pdf::Error::None);
	const auto fields = readForms(reopened);
	ASSERT_TRUE(fields && fields->entries.size() == 6);
	for (std::size_t i = 0; i < fields->entries.size(); ++i) {
		EXPECT_EQ(fields->entries[i].value, after->entries[i].value);
	}
	EXPECT_EQ(readFile(source), original);
	EXPECT_NE(readFile(destination), original);
}

TEST_F(SessionTest, MultilineExpansionRefusesWithoutChangingSavedValues) {
	pdf::Session session(engine);
	const auto bytes = mixedFormPdf(true, true, 0);
	const std::vector<std::uint8_t> original(bytes.begin(), bytes.end());
	const auto source = writeTemp("unbounded multiline.pdf", bytes);
	ASSERT_TRUE(open(session, source).canFillForms);
	const auto before = readForms(session);
	ASSERT_TRUE(before && before->entries.size() == 6);
	const int index = before->entries[0].index;
	Reply<pdf::FormWriteResult> oversized;
	ASSERT_TRUE(session.updateTextField(
	    0, index, u"first\nsecond", std::u16string(16384, u'\n'),
	    before->revision, oversized.callback()));
	const auto refused = oversized.wait();
	ASSERT_TRUE(refused);
	EXPECT_FALSE(refused->accepted || refused->changed || refused->canceled);
	const auto after = readForms(session);
	ASSERT_TRUE(after && after->entries.size() == 6);
	EXPECT_EQ(after->revision, before->revision);
	EXPECT_EQ(after->entries[0].value, before->entries[0].value);
	const auto destination = source.parent_path() / "unchanged.pdf";
	Reply<pdf::SaveResult> saved;
	ASSERT_TRUE(session.saveEdits(destination, source, 1024 * 1024, {},
	                              saved.callback()));
	ASSERT_EQ(saved.wait(), pdf::SaveResult::Saved);
	EXPECT_EQ(readFile(source), original);
	pdf::Session reopened(engine);
	ASSERT_EQ(open(reopened, destination).error, pdf::Error::None);
	const auto kept = readForms(reopened);
	ASSERT_TRUE(kept && kept->entries.size() == 6);
	for (std::size_t i = 0; i < kept->entries.size(); ++i) {
		EXPECT_EQ(kept->entries[i].value, before->entries[i].value);
	}
	Reply<pdf::FormWriteResult> retry;
	ASSERT_TRUE(session.updateTextField(0, index, u"first\nsecond", u"ok\nnext",
	                                    after->revision, retry.callback()));
	const auto written = retry.wait();
	ASSERT_TRUE(written && written->accepted && written->changed);
	EXPECT_FALSE(written->canceled);
	const auto current = readForms(session);
	ASSERT_TRUE(current && current->entries.size() == 6);
	EXPECT_EQ(current->entries[0].value, "ok\r\nnext");
	EXPECT_EQ(current->revision, written->revision);
}

TEST_F(SessionTest, FormWriteRepliesAllowReentryAndThrowingCallbacks) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, writeTemp("reentrant form.pdf", manyPages(1, true)))
	              .error,
	          pdf::Error::None);
	const auto before = readForms(session);
	ASSERT_TRUE(before);
	Reply<pdf::FormWriteResult> written, unchanged;
	Reply<pdf::FormFieldsResult> refreshed;
	auto worker = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.updateTextField(
	    0, 1, u"value", u"new", before->revision,
	    [&session, worker, writtenCb = written.callback(),
	     unchangedCb = unchanged.callback(),
	     refreshedCb = refreshed.callback()](auto result) {
		    *worker = pdf::Engine::onThread();
		    session.formFields(0, refreshedCb);
		    session.updateTextField(0, 1, u"new", u"new", result.revision,
		                            unchangedCb);
		    writtenCb(result);
		    throw std::runtime_error("field callback");
	    }));
	const auto result = written.wait();
	const auto fresh = refreshed.wait();
	const auto same = unchanged.wait();
	ASSERT_TRUE(result && fresh && fresh->fields && same);
	EXPECT_TRUE(*worker);
	EXPECT_TRUE(result->accepted && result->changed);
	EXPECT_TRUE(same->accepted);
	EXPECT_FALSE(same->changed || same->canceled);
	EXPECT_EQ(same->revision, result->revision);
	const auto after = readForms(session);
	ASSERT_TRUE(after);
	EXPECT_NE(after, fresh->fields);
	EXPECT_EQ(after->entries[0].value, "new");
	engine.run([] {}).get();
}

TEST_F(SessionTest, FormWritesCloseOnceAndKeepCommittedOutcomes) {
	const auto path = writeTemp("closing form.pdf", manyPages(1, true));
	Reply<pdf::FormWriteResult> canceled;
	{
		auto session = std::make_unique<pdf::Session>(engine);
		ASSERT_EQ(open(*session, path).error, pdf::Error::None);
		Gate gate(engine);
		ASSERT_TRUE(session->updateTextField(0, 1, u"value", u"new", 1,
		                                     canceled.callback()));
		session.reset();
		gate.open();
	}
	const auto stopped = canceled.wait();
	ASSERT_TRUE(stopped);
	EXPECT_TRUE(stopped->canceled);
	EXPECT_FALSE(stopped->accepted || stopped->changed);
	pdf::Session session(engine);
	EXPECT_FALSE(session.updateTextField(0, 1, u"value", u"new", 1,
	                                     canceled.callback()));
	ASSERT_EQ(open(session, path).error, pdf::Error::None);
	EXPECT_FALSE(session.updateTextField(0, 1, u"value", u"new", 0,
	                                     canceled.callback()));
	EXPECT_FALSE(session.updateTextField(0, 1, u"value", u"new", 1, {}));
	EXPECT_FALSE(session.updateTextField(
	    0, 1, u"value", std::u16string(32768, u'x'), 1, canceled.callback()));
	Reply<pdf::FormWriteResult> committed;
	ASSERT_TRUE(session.updateTextField(
	    0, 1, u"value", u"new", 1,
	    [&session, cb = committed.callback()](auto result) {
		    session.close();
		    cb(result);
	    }));
	const auto done = committed.wait();
	ASSERT_TRUE(done);
	EXPECT_TRUE(done->accepted && done->changed);
	EXPECT_FALSE(done->canceled);
	EXPECT_FALSE(session.updateTextField(0, 1, u"new", u"later", done->revision,
	                                     canceled.callback()));
}

TEST_F(SessionTest, CheckBoxWritesOwnTargetsRefreshSnapshotsAndSave) {
	pdf::Session session(engine);
	const auto bytes = manyPages(1, true, true);
	const auto source = writeTemp("checkbox form.pdf", bytes);
	ASSERT_TRUE(open(session, source).canFillForms);
	const auto before = readForms(session);
	ASSERT_TRUE(before && before->entries.size() == 1);
	EXPECT_EQ(before->entries[0].checked, false);
	Reply<pdf::FormWriteResult> first, stale;
	std::u16string expected = u"Off";
	{
		Gate gate(engine);
		ASSERT_TRUE(session.updateCheckBox(0, 1, expected, true,
		                                   before->revision, first.callback()));
		ASSERT_TRUE(session.updateCheckBox(0, 1, u"Approved now", false,
		                                   before->revision, stale.callback()));
		expected.assign(u"wrong");
		session.cancelForms();
	}
	const auto written = first.wait(), refused = stale.wait();
	ASSERT_TRUE(written && refused);
	EXPECT_TRUE(written->accepted && written->changed);
	EXPECT_FALSE(written->canceled);
	EXPECT_EQ(written->page, 0);
	EXPECT_EQ(written->index, 1);
	EXPECT_GT(written->revision, before->revision);
	EXPECT_FALSE(refused->accepted || refused->changed || refused->canceled);
	const auto after = readForms(session);
	ASSERT_TRUE(after && after->entries.size() == 1);
	EXPECT_NE(after, before);
	EXPECT_EQ(after->revision, written->revision);
	EXPECT_EQ(after->entries[0].checked, true);
	EXPECT_EQ(after->entries[0].value, "Approved now");
	EXPECT_EQ(before->entries[0].checked, false);
	Reply<pdf::FormWriteResult> unchanged, invalid;
	ASSERT_TRUE(session.updateCheckBox(0, 1, u"Approved now", true,
	                                   after->revision, unchanged.callback()));
	const auto same = unchanged.wait();
	ASSERT_TRUE(same);
	EXPECT_TRUE(same->accepted);
	EXPECT_FALSE(same->changed || same->canceled);
	EXPECT_EQ(same->revision, after->revision);
	const auto refreshed = readForms(session);
	ASSERT_TRUE(refreshed);
	EXPECT_NE(refreshed, after);
	ASSERT_TRUE(session.updateCheckBox(0, 1, u"wrong", false, after->revision,
	                                   invalid.callback()));
	const auto rejected = invalid.wait();
	ASSERT_TRUE(rejected);
	EXPECT_FALSE(rejected->accepted || rejected->changed || rejected->canceled);
	EXPECT_NE(readForms(session), refreshed);
	Reply<pdf::NoteResult> note;
	ASSERT_TRUE(session.updateTextNote(0, 0, u"Original", u"Preserved",
	                                   note.callback()));
	const auto edited = note.wait();
	ASSERT_TRUE(edited && edited->changed && edited->index);
	const auto destination = source.parent_path() / "checked copy.pdf";
	Reply<pdf::SaveResult> saved;
	ASSERT_TRUE(session.saveEdits(destination, source, 1024 * 1024, {},
	                              saved.callback()));
	EXPECT_EQ(saved.wait(), pdf::SaveResult::Saved);
	pdf::Session reopened(engine);
	ASSERT_EQ(open(reopened, destination).error, pdf::Error::None);
	const auto fields = readForms(reopened);
	ASSERT_TRUE(fields && fields->entries.size() == 1);
	EXPECT_EQ(fields->entries[0].checked, true);
	EXPECT_EQ(fields->entries[0].value, "Approved now");
	EXPECT_EQ(readFile(source),
	          (std::vector<std::uint8_t>(bytes.begin(), bytes.end())));
}

TEST_F(SessionTest, CheckBoxRepliesCanQueueUncheckingAndThrow) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, writeTemp("reentrant checkbox.pdf",
	                                  manyPages(1, true, true)))
	              .error,
	          pdf::Error::None);
	Reply<pdf::FormWriteResult> written, cleared;
	auto worker = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.updateCheckBox(
	    0, 1, u"Off", true, 1,
	    [&session, worker, writtenCb = written.callback(),
	     clearedCb = cleared.callback()](auto result) {
		    *worker = pdf::Engine::onThread();
		    session.updateCheckBox(0, 1, u"Approved now", false,
		                           result.revision, clearedCb);
		    writtenCb(result);
		    throw std::runtime_error("checkbox callback");
	    }));
	const auto on = written.wait(), off = cleared.wait();
	ASSERT_TRUE(on && off);
	EXPECT_TRUE(*worker);
	EXPECT_TRUE(on->accepted && on->changed && off->accepted && off->changed);
	EXPECT_FALSE(on->canceled || off->canceled);
	EXPECT_GT(off->revision, on->revision);
	const auto fields = readForms(session);
	ASSERT_TRUE(fields && fields->entries.size() == 1);
	EXPECT_EQ(fields->entries[0].checked, false);
	EXPECT_EQ(fields->entries[0].value, "Off");
}

TEST_F(SessionTest, CheckBoxWritesRespectCloseAndQueueBoundaries) {
	const auto path =
	    writeTemp("closing checkbox.pdf", manyPages(1, true, true));
	Reply<pdf::FormWriteResult> canceled;
	{
		auto session = std::make_unique<pdf::Session>(engine);
		ASSERT_EQ(open(*session, path).error, pdf::Error::None);
		Gate gate(engine);
		ASSERT_TRUE(session->updateCheckBox(0, 1, u"Off", true, 1,
		                                    canceled.callback()));
		session.reset();
	}
	const auto stopped = canceled.wait();
	ASSERT_TRUE(stopped);
	EXPECT_TRUE(stopped->canceled);
	EXPECT_FALSE(stopped->accepted || stopped->changed);
	pdf::Session session(engine);
	EXPECT_FALSE(
	    session.updateCheckBox(0, 1, u"Off", true, 1, canceled.callback()));
	ASSERT_EQ(open(session, path).error, pdf::Error::None);
	EXPECT_FALSE(
	    session.updateCheckBox(0, 1, u"Off", true, 0, canceled.callback()));
	EXPECT_FALSE(session.updateCheckBox(0, 1, u"Off", true, 1, {}));
	EXPECT_FALSE(session.updateCheckBox(0, 1, std::u16string(32768, u'x'), true,
	                                    1, canceled.callback()));
	Reply<pdf::FormWriteResult> committed;
	ASSERT_TRUE(session.updateCheckBox(
	    0, 1, u"Off", true, 1,
	    [&session, cb = committed.callback()](auto result) {
		    session.close();
		    cb(result);
	    }));
	const auto done = committed.wait();
	ASSERT_TRUE(done);
	EXPECT_TRUE(done->accepted && done->changed);
	EXPECT_FALSE(done->canceled);
	EXPECT_FALSE(session.updateCheckBox(0, 1, u"Approved now", false,
	                                    done->revision, canceled.callback()));
}

TEST_F(SessionTest, RadioWritesOwnTargetsRefreshSnapshotsAndSave) {
	pdf::Session session(engine);
	const auto bytes = radioPdf();
	const auto source = writeTemp("radio form.pdf", bytes);
	ASSERT_TRUE(open(session, source).canFillForms);
	const auto before = readForms(session);
	ASSERT_TRUE(before && before->entries.size() == 2);
	ASSERT_TRUE(before->entries[0].control && before->entries[1].control);
	const auto& target = before->entries[1];
	EXPECT_EQ(target.index, 2);
	EXPECT_EQ(target.control->index, 1);
	EXPECT_EQ(target.control->count, 2);
	EXPECT_EQ(target.control->exportValue, "Next choice");
	Reply<pdf::FormWriteResult> first, stale;
	std::u16string expected = u"First";
	auto control = *target.control;
	{
		Gate gate(engine);
		ASSERT_TRUE(session.updateRadioButton(0, target.index, expected,
		                                      control, before->revision,
		                                      first.callback()));
		ASSERT_TRUE(session.updateRadioButton(
		    0, 1, u"Next choice", *before->entries[0].control, before->revision,
		    stale.callback()));
		expected.assign(u"wrong");
		control = {-1, 0, "wrong"};
		session.cancelForms();
	}
	const auto written = first.wait(), refused = stale.wait();
	ASSERT_TRUE(written && refused);
	EXPECT_TRUE(written->accepted && written->changed);
	EXPECT_FALSE(written->canceled);
	EXPECT_EQ(written->page, 0);
	EXPECT_EQ(written->index, target.index);
	EXPECT_GT(written->revision, before->revision);
	EXPECT_FALSE(refused->accepted || refused->changed || refused->canceled);
	auto after = readForms(session);
	ASSERT_TRUE(after && after->entries.size() == 2);
	EXPECT_NE(after, before);
	EXPECT_EQ(after->revision, written->revision);
	EXPECT_EQ(after->entries[0].checked, false);
	EXPECT_EQ(after->entries[1].checked, true);
	EXPECT_EQ(after->entries[1].value, "Next choice");
	EXPECT_EQ(before->entries[0].checked, true);
	EXPECT_EQ(target.value, "First");
	Reply<pdf::FormWriteResult> unchanged;
	ASSERT_TRUE(session.updateRadioButton(0, 2, u"Next choice", *target.control,
	                                      after->revision,
	                                      unchanged.callback()));
	const auto same = unchanged.wait();
	ASSERT_TRUE(same);
	EXPECT_TRUE(same->accepted);
	EXPECT_FALSE(same->changed || same->canceled);
	EXPECT_EQ(same->revision, after->revision);
	const auto refreshed = readForms(session);
	ASSERT_TRUE(refreshed);
	EXPECT_NE(refreshed, after);
	after = refreshed;
	for (int mismatch = 0; mismatch < 4; ++mismatch) {
		auto changed = *target.control;
		if (mismatch == 0) {
			changed.index = 0;
		} else if (mismatch == 1) {
			changed.count = 3;
		} else if (mismatch == 2) {
			changed.exportValue = "wrong";
		}
		Reply<pdf::FormWriteResult> invalid;
		ASSERT_TRUE(session.updateRadioButton(
		    0, 2, mismatch == 3 ? u"wrong" : u"Next choice", changed,
		    after->revision, invalid.callback()));
		const auto rejected = invalid.wait();
		ASSERT_TRUE(rejected);
		EXPECT_FALSE(rejected->accepted || rejected->changed ||
		             rejected->canceled);
		const auto fresh = readForms(session);
		ASSERT_TRUE(fresh);
		EXPECT_NE(fresh, after);
		EXPECT_EQ(fresh->revision, written->revision);
		EXPECT_EQ(fresh->entries[1].checked, true);
		after = fresh;
	}
	Reply<pdf::NoteResult> note;
	ASSERT_TRUE(session.updateTextNote(0, 0, u"Original", u"Preserved",
	                                   note.callback()));
	const auto edited = note.wait();
	ASSERT_TRUE(edited && edited->changed && edited->index);
	const auto destination = source.parent_path() / "selected copy.pdf";
	Reply<pdf::SaveResult> saved;
	ASSERT_TRUE(session.saveEdits(destination, source, 1024 * 1024, {},
	                              saved.callback()));
	EXPECT_EQ(saved.wait(), pdf::SaveResult::Saved);
	pdf::Session reopened(engine);
	ASSERT_EQ(open(reopened, destination).error, pdf::Error::None);
	const auto fields = readForms(reopened);
	ASSERT_TRUE(fields && fields->entries.size() == 2);
	EXPECT_EQ(fields->entries[0].checked, false);
	EXPECT_EQ(fields->entries[1].checked, true);
	EXPECT_EQ(fields->entries[1].value, "Next choice");
	EXPECT_EQ(readFile(source),
	          (std::vector<std::uint8_t>(bytes.begin(), bytes.end())));
}

TEST_F(SessionTest, RadioRepliesCanQueueAnotherSelectionAndThrow) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, writeTemp("reentrant radio.pdf", radioPdf())).error,
	          pdf::Error::None);
	const auto before = readForms(session);
	ASSERT_TRUE(before && before->entries.size() == 2);
	ASSERT_TRUE(before->entries[0].control && before->entries[1].control);
	Reply<pdf::FormWriteResult> written, selected;
	Reply<pdf::FormFieldsResult> refreshed;
	auto worker = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.updateRadioButton(
	    0, 2, u"First", *before->entries[1].control, before->revision,
	    [&session, worker, control = *before->entries[0].control,
	     writtenCb = written.callback(), selectedCb = selected.callback(),
	     refreshedCb = refreshed.callback()](auto result) {
		    *worker = pdf::Engine::onThread();
		    session.formFields(0, refreshedCb);
		    session.updateRadioButton(0, 1, u"Next choice", control,
		                              result.revision, selectedCb);
		    writtenCb(result);
		    throw std::runtime_error("radio callback");
	    }));
	const auto on = written.wait(), back = selected.wait();
	const auto fresh = refreshed.wait();
	ASSERT_TRUE(on && back && fresh && fresh->fields);
	EXPECT_TRUE(*worker);
	EXPECT_TRUE(on->accepted && on->changed && back->accepted && back->changed);
	EXPECT_FALSE(on->canceled || back->canceled || fresh->canceled);
	EXPECT_GT(back->revision, on->revision);
	ASSERT_EQ(fresh->fields->entries.size(), 2u);
	EXPECT_EQ(fresh->fields->entries[1].checked, true);
	const auto after = readForms(session);
	ASSERT_TRUE(after && after->entries.size() == 2);
	EXPECT_NE(after, fresh->fields);
	EXPECT_EQ(after->entries[0].checked, true);
	EXPECT_EQ(after->entries[1].checked, false);
	EXPECT_EQ(after->entries[0].value, "First");
}

TEST_F(SessionTest, RadioWritesRespectCloseAndQueueBoundaries) {
	const auto path = writeTemp("closing radio.pdf", radioPdf());
	const pdf::FormControl control{1, 2, "Next choice"};
	for (const bool destroy : {false, true}) {
		auto session = std::make_unique<pdf::Session>(engine);
		ASSERT_EQ(open(*session, path).error, pdf::Error::None);
		Reply<pdf::FormWriteResult> canceled;
		auto calls = std::make_shared<std::atomic<int>>(0);
		{
			Gate gate(engine);
			ASSERT_TRUE(session->updateRadioButton(
			    0, 2, u"First", control, 1,
			    [calls, cb = canceled.callback()](auto r) {
				    ++*calls;
				    cb(r);
			    }));
			if (destroy) {
				session.reset();
			} else {
				session->close();
			}
		}
		const auto stopped = canceled.wait();
		ASSERT_TRUE(stopped);
		EXPECT_TRUE(stopped->canceled);
		EXPECT_FALSE(stopped->accepted || stopped->changed);
		engine.run([] {}).get();
		EXPECT_EQ(*calls, 1);
	}
	pdf::Session session(engine);
	Reply<pdf::FormWriteResult> unused;
	EXPECT_FALSE(session.updateRadioButton(0, 2, u"First", control, 1,
	                                       unused.callback()));
	ASSERT_EQ(open(session, path).error, pdf::Error::None);
	EXPECT_FALSE(session.updateRadioButton(0, 2, u"First", control, 0,
	                                       unused.callback()));
	EXPECT_FALSE(session.updateRadioButton(0, 2, u"First", control, 1, {}));
	EXPECT_FALSE(session.updateRadioButton(0, 2, std::u16string(32768, u'x'),
	                                       control, 1, unused.callback()));
	Reply<pdf::FormWriteResult> committed;
	ASSERT_TRUE(session.updateRadioButton(
	    0, 2, u"First", control, 1,
	    [&session, cb = committed.callback()](auto result) {
		    session.close();
		    cb(result);
	    }));
	const auto done = committed.wait();
	ASSERT_TRUE(done);
	EXPECT_TRUE(done->accepted && done->changed);
	EXPECT_FALSE(done->canceled);
	EXPECT_FALSE(session.updateRadioButton(0, 2, u"Next choice", control,
	                                       done->revision, unused.callback()));
}

TEST_F(SessionTest, ChoiceWritesOwnSnapshotsRefreshCachesAndSave) {
	for (const int flags : {0, 131072}) {
		SCOPED_TRACE(flags);
		pdf::Session session(engine);
		const auto bytes = manyPages(1, true, false, flags);
		const auto source = writeTemp("choice form.pdf", bytes);
		ASSERT_TRUE(open(session, source).canFillForms);
		const auto before = readForms(session);
		ASSERT_TRUE(before && before->entries.size() == 1);
		ASSERT_TRUE(before->entries[0].options);
		ASSERT_EQ(before->entries[0].options->size(), 3u);
		auto target = before->entries[0];
		Reply<pdf::FormWriteResult> first, stale;
		{
			Gate gate(engine);
			ASSERT_TRUE(session.updateChoiceField(
			    0, target, 1, before->revision, first.callback()));
			ASSERT_TRUE(session.updateChoiceField(
			    0, target, 2, before->revision, stale.callback()));
			target.name = "wrong";
			target.value = "wrong";
			target.options->front().selected = false;
			target.options->back().label = "wrong";
			target.options.reset();
			session.cancelForms();
		}
		const auto written = first.wait(), refused = stale.wait();
		ASSERT_TRUE(written && refused);
		EXPECT_TRUE(written->accepted && written->changed);
		EXPECT_FALSE(written->canceled);
		EXPECT_EQ(written->page, 0);
		EXPECT_EQ(written->index, 1);
		EXPECT_GT(written->revision, before->revision);
		EXPECT_FALSE(refused->accepted || refused->changed ||
		             refused->canceled);
		auto after = readForms(session);
		ASSERT_TRUE(after && after->entries.size() == 1);
		ASSERT_TRUE(after->entries[0].options);
		ASSERT_EQ(after->entries[0].options->size(), 3u);
		EXPECT_NE(after, before);
		EXPECT_EQ(after->revision, written->revision);
		EXPECT_EQ(after->entries[0].value, "caf\u00e9");
		EXPECT_TRUE((*after->entries[0].options)[1].selected);
		EXPECT_FALSE((*after->entries[0].options)[2].selected);
		EXPECT_EQ(before->entries[0].value, "old");
		EXPECT_TRUE(before->entries[0].options->front().selected);
		Reply<pdf::FormWriteResult> unchanged;
		ASSERT_TRUE(session.updateChoiceField(
		    0, after->entries[0], 1, after->revision, unchanged.callback()));
		const auto same = unchanged.wait();
		ASSERT_TRUE(same);
		EXPECT_TRUE(same->accepted);
		EXPECT_FALSE(same->changed || same->canceled);
		EXPECT_EQ(same->revision, after->revision);
		const auto refreshed = readForms(session);
		ASSERT_TRUE(refreshed);
		EXPECT_NE(refreshed, after);
		after = refreshed;
		for (int mismatch = 0; mismatch < 5; ++mismatch) {
			SCOPED_TRACE(mismatch);
			auto bad = after->entries[0];
			if (mismatch == 0) {
				bad.options->back().label = "wrong";
			} else if (mismatch == 1) {
				bad.value = "wrong";
			} else if (mismatch == 2) {
				bad.options.reset();
			} else if (mismatch == 3) {
				bad.type = pdf::FormFieldType::Text;
			}
			Reply<pdf::FormWriteResult> invalid;
			ASSERT_TRUE(session.updateChoiceField(0, bad, mismatch == 4 ? 3 : 2,
			                                      after->revision,
			                                      invalid.callback()));
			const auto rejected = invalid.wait();
			ASSERT_TRUE(rejected);
			EXPECT_FALSE(rejected->accepted || rejected->changed ||
			             rejected->canceled);
			const auto fresh = readForms(session);
			ASSERT_TRUE(fresh);
			EXPECT_NE(fresh, after);
			EXPECT_EQ(fresh->revision, written->revision);
			EXPECT_EQ(fresh->entries[0].value, "caf\u00e9");
			after = fresh;
		}
		Reply<pdf::NoteResult> note;
		ASSERT_TRUE(session.updateTextNote(0, 0, u"Original", u"Preserved",
		                                   note.callback()));
		const auto edited = note.wait();
		ASSERT_TRUE(edited && edited->changed && edited->index);
		const auto withNote = readForms(session);
		ASSERT_TRUE(withNote && withNote->entries.size() == 1);
		EXPECT_NE(withNote, after);
		EXPECT_EQ(withNote->revision, written->revision);
		Reply<pdf::FormWriteResult> duplicate;
		ASSERT_TRUE(session.updateChoiceField(0, withNote->entries[0], 2,
		                                      withNote->revision,
		                                      duplicate.callback()));
		const auto moved = duplicate.wait();
		ASSERT_TRUE(moved);
		EXPECT_TRUE(moved->accepted && moved->changed);
		EXPECT_FALSE(moved->canceled);
		EXPECT_GT(moved->revision, withNote->revision);
		const auto destination = source.parent_path() / "selected copy.pdf";
		Reply<pdf::SaveResult> saved;
		ASSERT_TRUE(session.saveEdits(destination, source, 1024 * 1024, {},
		                              saved.callback()));
		EXPECT_EQ(saved.wait(), pdf::SaveResult::Saved);
		pdf::Session reopened(engine);
		ASSERT_EQ(open(reopened, destination).error, pdf::Error::None);
		const auto fields = readForms(reopened);
		ASSERT_TRUE(fields && fields->entries.size() == 1);
		ASSERT_TRUE(fields->entries[0].options);
		ASSERT_EQ(fields->entries[0].options->size(), 3u);
		EXPECT_EQ(fields->entries[0].value, "caf\u00e9");
		EXPECT_FALSE((*fields->entries[0].options)[1].selected);
		EXPECT_TRUE((*fields->entries[0].options)[2].selected);
		Reply<pdf::TextNoteResult> preserved;
		ASSERT_TRUE(reopened.textNote(0, 0, preserved.callback()));
		const auto contents = preserved.wait();
		ASSERT_TRUE(contents);
		EXPECT_EQ(contents->contents, u"Preserved");
		EXPECT_EQ(readFile(source),
		          (std::vector<std::uint8_t>(bytes.begin(), bytes.end())));
	}
}

TEST_F(SessionTest, MixedFormWritesPreserveEachOtherThroughSaveAndReopen) {
	for (const bool reverse : {false, true}) {
		SCOPED_TRACE(reverse);
		pdf::Session session(engine);
		const auto bytes = mixedFormPdf();
		const auto original =
		    std::vector<std::uint8_t>(bytes.begin(), bytes.end());
		const auto source = writeTemp("mixed fields.pdf", bytes);
		ASSERT_TRUE(open(session, source).canFillForms);
		bool changed[5] = {};
		const auto checkFields = [&](const pdf::FormFields& fields) {
			EXPECT_FALSE(fields.limited);
			EXPECT_EQ(fields.page, 0);
			ASSERT_EQ(fields.entries.size(), 6u);
			const auto& entries = fields.entries;
			for (int i = 0; i < 6; ++i) {
				EXPECT_EQ(entries[i].index, i + 1);
			}
			EXPECT_EQ(entries[0].type, pdf::FormFieldType::Text);
			EXPECT_EQ(entries[0].name, "text");
			EXPECT_EQ(entries[0].value,
			          changed[0] ? "Edited caf\u00e9" : "initial");
			EXPECT_EQ(entries[1].type, pdf::FormFieldType::CheckBox);
			EXPECT_EQ(entries[1].name, "box");
			EXPECT_EQ(entries[1].value, changed[1] ? "Approved now" : "Off");
			EXPECT_EQ(entries[1].checked, changed[1]);
			for (int i = 0; i < 2; ++i) {
				const auto& member = entries[i + 2];
				EXPECT_EQ(member.type, pdf::FormFieldType::RadioButton);
				EXPECT_EQ(member.name, "radio");
				EXPECT_EQ(member.value, changed[2] ? "Next choice" : "First");
				EXPECT_EQ(member.checked, changed[2] == (i == 1));
				ASSERT_TRUE(member.control);
				EXPECT_EQ(member.control->index, i);
				EXPECT_EQ(member.control->count, 2);
				EXPECT_EQ(member.control->exportValue,
				          i == 0 ? "First" : "Next choice");
			}
			for (int i = 3; i < 5; ++i) {
				const auto& field = entries[i + 1];
				EXPECT_EQ(field.type, i == 3 ? pdf::FormFieldType::ComboBox
				                             : pdf::FormFieldType::ListBox);
				EXPECT_EQ(field.name, i == 3 ? "combo" : "list");
				EXPECT_EQ(field.value, changed[i] ? "caf\u00e9" : "old");
				ASSERT_TRUE(field.options);
				ASSERT_EQ(field.options->size(), 3u);
				for (int option = 0; option < 3; ++option) {
					const auto& item = (*field.options)[option];
					EXPECT_EQ(item.index, option);
					EXPECT_EQ(item.label, option == 0 ? "First" : "Same");
					EXPECT_EQ(item.selected,
					          option == (changed[i] ? (i == 3 ? 2 : 1) : 0));
				}
			}
		};
		const auto checkExtras = [&](pdf::Session& current,
		                             const std::u16string& expected) {
			Reply<pdf::TextNoteResult> note;
			ASSERT_TRUE(current.textNote(0, 0, note.callback()));
			const auto contents = note.wait();
			ASSERT_TRUE(contents);
			EXPECT_FALSE(contents->canceled);
			EXPECT_EQ(contents->contents, expected);
			Reply<pdf::OutlineResult> outline;
			ASSERT_TRUE(current.outline(outline.callback()));
			const auto found = outline.wait();
			ASSERT_TRUE(found && found->outline);
			EXPECT_FALSE(found->canceled || found->outline->limited);
			ASSERT_EQ(found->outline->entries.size(), 1u);
			EXPECT_EQ(found->outline->entries[0].title, "Fields");
			EXPECT_TRUE(found->outline->entries[0].canFollow);
			Reply<pdf::LinkResult> link;
			ASSERT_TRUE(current.outlineLink(0, link.callback()));
			const auto target = link.wait();
			ASSERT_TRUE(target && target->link && target->link->destination);
			EXPECT_FALSE(target->canceled);
			EXPECT_EQ(target->link->destination->page, 0);
			EXPECT_EQ(target->link->destination->fit, pdf::LinkFit::Page);
		};
		const auto write =
		    [&](int kind, const pdf::FormFields& snapshot, bool initial,
		        std::function<void(pdf::FormWriteResult)> callback) {
			    if (snapshot.entries.size() != 6 ||
			        !snapshot.entries[3].control) {
				    return false;
			    }
			    const int position = kind < 2 ? kind : kind + 1;
			    const auto& field = snapshot.entries[position];
			    switch (kind) {
			    case 0:
				    return session.updateTextField(
				        0, field.index,
				        initial ? u"initial" : u"Edited caf\u00e9",
				        u"Edited caf\u00e9", snapshot.revision,
				        std::move(callback));
			    case 1:
				    return session.updateCheckBox(
				        0, field.index, initial ? u"Off" : u"Approved now",
				        true, snapshot.revision, std::move(callback));
			    case 2:
				    return session.updateRadioButton(
				        0, field.index, initial ? u"First" : u"Next choice",
				        *field.control, snapshot.revision, std::move(callback));
			    default:
				    return session.updateChoiceField(
				        0, field, kind == 3 ? 2 : 1, snapshot.revision,
				        std::move(callback));
			    }
		    };
		auto snapshot = readForms(session);
		ASSERT_TRUE(snapshot);
		checkFields(*snapshot);
		checkExtras(session, u"Original");
		Reply<pdf::NoteResult> note;
		ASSERT_TRUE(session.updateTextNote(0, 0, u"Original", u"Before fields",
		                                   note.callback()));
		const auto noted = note.wait();
		ASSERT_TRUE(noted && noted->changed && noted->index == 0);
		EXPECT_FALSE(noted->canceled);
		snapshot = readForms(session);
		ASSERT_TRUE(snapshot);
		for (int step = 0; step < 5; ++step) {
			const int kind = reverse ? 4 - step : step;
			SCOPED_TRACE(kind);
			ASSERT_EQ(snapshot->entries.size(), 6u);
			ASSERT_TRUE(snapshot->entries[3].control);
			Reply<pdf::FormWriteResult> edit, stale;
			{
				Gate gate(engine);
				ASSERT_TRUE(write(kind, *snapshot, true, edit.callback()));
				if (step == 0) {
					ASSERT_TRUE(write(reverse ? 0 : 4, *snapshot, true,
					                  stale.callback()));
				}
			}
			const auto done = edit.wait();
			ASSERT_TRUE(done && done->accepted && done->changed);
			EXPECT_FALSE(done->canceled);
			EXPECT_GT(done->revision, snapshot->revision);
			if (step == 0) {
				const auto refused = stale.wait();
				ASSERT_TRUE(refused);
				EXPECT_FALSE(refused->accepted || refused->changed ||
				             refused->canceled);
			}
			changed[kind] = true;
			const auto fresh = readForms(session);
			ASSERT_TRUE(fresh);
			EXPECT_NE(fresh, snapshot);
			EXPECT_EQ(fresh->revision, done->revision);
			checkFields(*fresh);
			snapshot = fresh;
			if (step == 1) {
				Reply<pdf::NoteResult> interleaved;
				ASSERT_TRUE(session.updateTextNote(0, 0, u"Before fields",
				                                   u"Between fields",
				                                   interleaved.callback()));
				const auto updated = interleaved.wait();
				ASSERT_TRUE(updated && updated->changed && updated->index == 0);
				EXPECT_FALSE(updated->canceled);
				snapshot = readForms(session);
				ASSERT_TRUE(snapshot);
				checkFields(*snapshot);
			}
			checkExtras(session,
			            step == 0 ? u"Before fields" : u"Between fields");
			EXPECT_EQ(readFile(source), original);
		}
		for (int kind = 0; kind < 5; ++kind) {
			SCOPED_TRACE(kind);
			Reply<pdf::FormWriteResult> noOp;
			ASSERT_TRUE(write(kind, *snapshot, false, noOp.callback()));
			const auto same = noOp.wait();
			ASSERT_TRUE(same && same->accepted);
			EXPECT_FALSE(same->changed || same->canceled);
			EXPECT_EQ(same->revision, snapshot->revision);
			snapshot = readForms(session);
			ASSERT_TRUE(snapshot);
			checkFields(*snapshot);
		}
		const auto destination = source.parent_path() / "mixed copy.pdf";
		Reply<pdf::SaveResult> saved;
		ASSERT_TRUE(session.saveEdits(destination, source, 1024 * 1024, {},
		                              saved.callback()));
		ASSERT_EQ(saved.wait(), pdf::SaveResult::Saved);
		session.close();
		pdf::Session reopened(engine);
		ASSERT_EQ(open(reopened, destination).error, pdf::Error::None);
		const auto fields = readForms(reopened);
		ASSERT_TRUE(fields);
		checkFields(*fields);
		checkExtras(reopened, u"Between fields");
		EXPECT_EQ(readFile(source), original);
		EXPECT_NE(readFile(destination), original);
	}
}

TEST_F(SessionTest, ChoiceRepliesCanReadThenQueueAnotherSelectionAndThrow) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, writeTemp("reentrant choice.pdf",
	                                  manyPages(1, true, false, 0)))
	              .error,
	          pdf::Error::None);
	const auto before = readForms(session);
	ASSERT_TRUE(before && before->entries.size() == 1);
	Reply<pdf::FormWriteResult> written, selected;
	auto worker = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(session.updateChoiceField(
	    0, before->entries[0], 1, before->revision,
	    [&session, worker, writtenCb = written.callback(),
	     selectedCb = selected.callback()](auto result) {
		    *worker = pdf::Engine::onThread();
		    session.formFields(0, [&session, selectedCb](auto fresh) {
			    if (fresh.fields && fresh.fields->entries.size() == 1) {
				    session.updateChoiceField(0, fresh.fields->entries[0], 0,
				                              fresh.fields->revision,
				                              selectedCb);
			    }
		    });
		    writtenCb(result);
		    throw std::runtime_error("choice callback");
	    }));
	const auto on = written.wait(), back = selected.wait();
	ASSERT_TRUE(on && back);
	EXPECT_TRUE(*worker);
	EXPECT_TRUE(on->accepted && on->changed && back->accepted && back->changed);
	EXPECT_FALSE(on->canceled || back->canceled);
	EXPECT_GT(back->revision, on->revision);
	const auto after = readForms(session);
	ASSERT_TRUE(after && after->entries.size() == 1);
	EXPECT_EQ(after->entries[0].value, "old");
	ASSERT_TRUE(after->entries[0].options);
	EXPECT_TRUE(after->entries[0].options->front().selected);
}

TEST_F(SessionTest, ChoiceWritesRespectCloseAndQueueBoundaries) {
	const auto path =
	    writeTemp("closing choice.pdf", manyPages(1, true, false, 131072));
	for (const bool destroy : {false, true}) {
		auto session = std::make_unique<pdf::Session>(engine);
		ASSERT_EQ(open(*session, path).error, pdf::Error::None);
		const auto before = readForms(*session);
		ASSERT_TRUE(before && before->entries.size() == 1);
		Reply<pdf::FormWriteResult> canceled;
		auto calls = std::make_shared<std::atomic<int>>(0);
		{
			Gate gate(engine);
			ASSERT_TRUE(session->updateChoiceField(
			    0, before->entries[0], 1, before->revision,
			    [calls, cb = canceled.callback()](auto r) {
				    ++*calls;
				    cb(r);
			    }));
			if (destroy) {
				session.reset();
			} else {
				session->close();
			}
		}
		const auto stopped = canceled.wait();
		ASSERT_TRUE(stopped);
		EXPECT_TRUE(stopped->canceled);
		EXPECT_FALSE(stopped->accepted || stopped->changed);
		engine.run([] {}).get();
		EXPECT_EQ(*calls, 1);
	}
	pdf::Session session(engine);
	Reply<pdf::FormWriteResult> unused;
	EXPECT_FALSE(session.updateChoiceField(0, {}, 1, 1, unused.callback()));
	ASSERT_EQ(open(session, path).error, pdf::Error::None);
	const auto before = readForms(session);
	ASSERT_TRUE(before && before->entries.size() == 1);
	const auto& field = before->entries[0];
	EXPECT_FALSE(session.updateChoiceField(0, field, 1, 0, unused.callback()));
	EXPECT_FALSE(session.updateChoiceField(0, field, 1, before->revision, {}));
	Reply<pdf::FormWriteResult> committed;
	ASSERT_TRUE(session.updateChoiceField(
	    0, field, 1, before->revision,
	    [&session, cb = committed.callback()](auto result) {
		    session.close();
		    cb(result);
	    }));
	const auto done = committed.wait();
	ASSERT_TRUE(done);
	EXPECT_TRUE(done->accepted && done->changed);
	EXPECT_FALSE(done->canceled);
	EXPECT_FALSE(session.updateChoiceField(0, field, 1, done->revision,
	                                       unused.callback()));
}

TEST_F(SessionTest, FormCacheClearsBeforeEditsIncludingRefusedAttempts) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, writeTemp("forms.pdf", manyPages(1, true))).error,
	          pdf::Error::None);
	auto previous = readForms(session);
	ASSERT_TRUE(previous);
	ASSERT_EQ(previous->entries.size(), 1u);
	const auto revision = previous->revision;
	const auto refreshed = [&] {
		auto current = readForms(session);
		EXPECT_NE(current, previous);
		previous = current;
	};
	Reply<pdf::NoteResult> added, updated, markup, cleared, removed;
	ASSERT_TRUE(session.addTextNote(0, 100, 100, u"new", {}, added.callback()));
	const auto note = added.wait();
	ASSERT_TRUE(note && note->index && note->changed);
	refreshed();
	ASSERT_TRUE(session.updateTextNote(0, *note->index, u"new", u"updated",
	                                   updated.callback()));
	const auto change = updated.wait();
	ASSERT_TRUE(change && change->changed);
	refreshed();
	ASSERT_TRUE(session.updateMarkupComment(
	    0, *note->index, pdf::TextMarkup::Highlight, {{}, false}, u"refused",
	    markup.callback()));
	const auto refused = markup.wait();
	ASSERT_TRUE(refused);
	EXPECT_FALSE(refused->changed);
	refreshed();
	Reply<pdf::MarkupResult> invalid;
	ASSERT_TRUE(
	    session.markText({}, pdf::TextMarkup::Highlight, invalid.callback()));
	const auto unmarked = invalid.wait();
	ASSERT_TRUE(unmarked);
	EXPECT_FALSE(unmarked->changed);
	refreshed();
	ASSERT_TRUE(session.clearMarkupComment(
	    {0, *note->index, "Highlight", revision}, {}, cleared.callback()));
	const auto unchanged = cleared.wait();
	ASSERT_TRUE(unchanged);
	EXPECT_FALSE(unchanged->changed);
	refreshed();
	ASSERT_TRUE(previous);
	ASSERT_EQ(previous->entries.size(), 1u);
	EXPECT_EQ(previous->revision, revision);
	EXPECT_EQ(previous->entries[0].index, 1);
	ASSERT_TRUE(
	    session.removeAnnotation({0, 0, "Text", revision}, removed.callback()));
	const auto deletion = removed.wait();
	ASSERT_TRUE(deletion && deletion->changed);
	refreshed();
	ASSERT_TRUE(previous);
	ASSERT_EQ(previous->entries.size(), 1u);
	EXPECT_NE(previous->revision, revision);
	EXPECT_EQ(previous->entries[0].index, 0);
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

TEST_F(SessionTest, PropertiesUseLoadedBytesAndCloseCancelsQueuedReplies) {
	pdf::Session session(engine);
	Reply<pdf::PropertiesResult> unopened;
	EXPECT_FALSE(session.properties(unopened.callback()));
	const auto path = tempDir() / "snapshot.pdf";
	fs::copy_file(fixturePath("properties.pdf"), path);
	const auto byteCount = fs::file_size(path);
	ASSERT_EQ(open(session, path).error, pdf::Error::None);
	fs::remove(path);
	Reply<pdf::PropertiesResult> reply;
	auto onThread = std::make_shared<std::atomic<bool>>(false);
	ASSERT_TRUE(
	    session.properties([cb = reply.callback(), onThread](auto result) {
		    *onThread = pdf::Engine::onThread();
		    cb(std::move(result));
	    }));
	const auto result = reply.wait();
	ASSERT_TRUE(result && result->properties);
	EXPECT_TRUE(*onThread);
	EXPECT_FALSE(result->canceled);
	EXPECT_EQ(result->properties->bytes, byteCount);
	EXPECT_EQ(result->properties->pages, 2);
	Gate gate(engine);
	Reply<pdf::PropertiesResult> pending;
	ASSERT_TRUE(session.properties(pending.callback()));
	session.close();
	gate.open();
	const auto canceled = pending.wait();
	ASSERT_TRUE(canceled);
	EXPECT_TRUE(canceled->canceled);
	EXPECT_FALSE(canceled->properties);
	EXPECT_FALSE(session.properties(reply.callback()));
}

TEST_F(SessionTest, SavesOneCopyAtATimeOnTheEngineThreadAndCancelsQueuedWork) {
	pdf::Session session(engine);
	const auto target = tempDir() / "copy.pdf",
	           source = fixturePath("pages.pdf");
	Reply<pdf::SaveResult> before;
	EXPECT_FALSE(session.saveCopy(target, source, {}, before.callback()));
	ASSERT_EQ(open(session, source).error, pdf::Error::None);
	{
		Gate gate(engine);
		Reply<pdf::SaveResult> reply;
		auto onThread = std::make_shared<std::atomic_bool>(false);
		ASSERT_TRUE(session.saveCopy(
		    target, source, {},
		    [callback = reply.callback(), onThread](auto result) {
			    onThread->store(pdf::Engine::onThread());
			    callback(result);
		    }));
		EXPECT_FALSE(session.saveCopy(target, source, {}, before.callback()));
		gate.open();
		EXPECT_EQ(reply.wait(), pdf::SaveResult::Saved);
		EXPECT_TRUE(onThread->load());
	}
	EXPECT_TRUE(fs::exists(target));
	fs::remove(target);
	{
		Gate gate(engine);
		Reply<pdf::SaveResult> reply;
		auto cancel = std::make_shared<std::atomic_bool>(false);
		ASSERT_TRUE(session.saveCopy(
		    target, source, [cancel] { return cancel->load(); },
		    reply.callback()));
		cancel->store(true);
		gate.open();
		EXPECT_EQ(reply.wait(), pdf::SaveResult::Canceled);
		EXPECT_FALSE(fs::exists(target));
	}
	{
		Gate gate(engine);
		Reply<pdf::SaveResult> reply;
		ASSERT_TRUE(session.saveCopy(target, source, {}, reply.callback()));
		session.close();
		gate.open();
		EXPECT_EQ(reply.wait(), pdf::SaveResult::Canceled);
		EXPECT_FALSE(fs::exists(target));
	}
	EXPECT_FALSE(session.saveCopy(target, source, {}, before.callback()));
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

TEST_F(SessionTest, MarkupCommentRequestsOwnSnapshotsAndKeepCloseOrder) {
	for (auto type : {pdf::TextMarkup::Highlight, pdf::TextMarkup::Underline,
	                  pdf::TextMarkup::StrikeOut}) {
		pdf::Session session(engine);
		Reply<pdf::TextNoteResult> read;
		EXPECT_FALSE(session.markupComment(0, 0, type, read.callback()));
		ASSERT_EQ(open(session, fixturePath("text.pdf")).error,
		          pdf::Error::None);
		Reply<pdf::MarkupResult> mark;
		ASSERT_TRUE(session.markText({{0, 0}, {0, 4}}, type, mark.callback()));
		const auto marked = mark.wait();
		ASSERT_TRUE(marked && marked->changed);
		ASSERT_TRUE(session.markupComment(0, 0, type, read.callback()));
		const auto initial = read.wait();
		ASSERT_TRUE(initial && initial->contents);
		EXPECT_FALSE(initial->contentsPresent);
		Gate gate(engine);
		Reply<pdf::NoteResult> changed, stale;
		pdf::CommentContents expected{*initial->contents,
		                              initial->contentsPresent};
		std::u16string text = u"Queued \u03a9\U0001f600\r\ncomment";
		ASSERT_TRUE(session.updateMarkupComment(0, 0, type, expected, text,
		                                        changed.callback()));
		expected = {u"Later", true};
		text.clear();
		ASSERT_TRUE(session.updateMarkupComment(0, 0, type, {{}, false},
		                                        u"Stale", stale.callback()));
		session.cancelAnnotations();
		session.cancelRender();
		session.cancelSearch();
		gate.open();
		const auto a = changed.wait(), b = stale.wait();
		ASSERT_TRUE(a && b);
		EXPECT_TRUE(a->changed && a->index == 0);
		EXPECT_FALSE(b->changed || b->index || b->canceled);
		Reply<pdf::TextNoteResult> current;
		ASSERT_TRUE(session.markupComment(0, 0, type, current.callback()));
		const auto contents = current.wait();
		ASSERT_TRUE(contents && contents->contents);
		EXPECT_TRUE(contents->contentsPresent);
		EXPECT_EQ(*contents->contents, u"Queued \u03a9\U0001f600\r\ncomment");
		Gate closeGate(engine);
		Reply<pdf::NoteResult> canceled;
		Reply<pdf::TextNoteResult> closed;
		ASSERT_TRUE(
		    session.updateMarkupComment(0, 0, type, {*contents->contents, true},
		                                u"Closed", canceled.callback()));
		ASSERT_TRUE(session.markupComment(0, 0, type, closed.callback()));
		session.close();
		closeGate.open();
		const auto c = canceled.wait();
		const auto d = closed.wait();
		ASSERT_TRUE(c && d);
		EXPECT_TRUE(c->canceled && d->canceled);
		EXPECT_FALSE(c->changed || c->index || d->contents);
	}
}

TEST_F(SessionTest, DeletionRejectsQueuedAndRetainedAnnotationIndices) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, fixturePath("pages.pdf")).error, pdf::Error::None);
	for (int i = 0; i < 2; ++i) {
		Reply<pdf::NoteResult> added;
		ASSERT_TRUE(session.addTextNote(0, 40.0f + i * 30.0f, 50, u"Same", {},
		                                added.callback()));
		ASSERT_TRUE(added.wait()->changed);
	}
	Reply<pdf::AnnotationsResult> inventory;
	ASSERT_TRUE(session.annotations(0, inventory.callback()));
	const auto before = inventory.wait();
	ASSERT_TRUE(before && before->annotations);
	ASSERT_EQ(before->annotations->entries.size(), 2u);
	const auto first = before->annotations->entries[0].target;
	const auto second = before->annotations->entries[1].target;
	Gate gate(engine);
	Reply<pdf::NoteResult> removed, staleEdit, staleDelete;
	Reply<pdf::TextNoteResult> staleRead;
	ASSERT_TRUE(session.removeAnnotation(first, removed.callback()));
	ASSERT_TRUE(session.updateTextNote(0, 1, u"Same", u"Wrong note",
	                                   staleEdit.callback()));
	ASSERT_TRUE(session.textNote(0, 0, staleRead.callback()));
	ASSERT_TRUE(session.removeAnnotation(second, staleDelete.callback()));
	gate.open();
	const auto a = removed.wait(), b = staleEdit.wait(), c = staleDelete.wait();
	const auto d = staleRead.wait();
	ASSERT_TRUE(a && b && c && d);
	EXPECT_TRUE(a->changed);
	EXPECT_FALSE(a->index);
	EXPECT_FALSE(b->changed || b->index || c->changed || c->index);
	EXPECT_FALSE(d->contents || d->target);
	Reply<pdf::TextNoteResult> retained;
	ASSERT_TRUE(session.textNote(0, 0, retained.callback(), second.revision));
	const auto old = retained.wait();
	ASSERT_TRUE(old);
	EXPECT_FALSE(old->contents || old->target);
	Reply<pdf::AnnotationsResult> current;
	ASSERT_TRUE(session.annotations(0, current.callback()));
	const auto after = current.wait();
	ASSERT_TRUE(after && after->annotations);
	ASSERT_EQ(after->annotations->entries.size(), 1u);
	const auto fresh = after->annotations->entries[0].target;
	EXPECT_NE(fresh.revision, second.revision);
	Reply<pdf::TextNoteResult> read;
	ASSERT_TRUE(session.textNote(0, 0, read.callback(), fresh.revision));
	const auto value = read.wait();
	ASSERT_TRUE(value && value->contents && value->target);
	EXPECT_EQ(*value->contents, u"Same");
	EXPECT_EQ(*value->target, fresh);
}

TEST_F(SessionTest, MarkupHitAndCommentRemovalKeepTheirConcreteTarget) {
	pdf::Session session(engine);
	ASSERT_EQ(open(session, fixturePath("text.pdf")).error, pdf::Error::None);
	Reply<pdf::MarkupResult> mark;
	ASSERT_TRUE(session.markText({{0, 0}, {0, 4}}, pdf::TextMarkup::Highlight,
	                             mark.callback(), pdf::HighlightColor::Pink));
	ASSERT_TRUE(mark.wait()->changed);
	Reply<pdf::AnnotationsResult> inventory;
	ASSERT_TRUE(session.annotations(0, inventory.callback()));
	const auto notes = inventory.wait();
	ASSERT_TRUE(notes && notes->annotations);
	ASSERT_EQ(notes->annotations->entries.size(), 1u);
	const auto& item = notes->annotations->entries[0];
	ASSERT_FALSE(item.displayQuads.empty());
	const auto& q = item.displayQuads[0];
	Reply<pdf::AnnotationHitResult> hit;
	ASSERT_TRUE(session.annotationAt(0, (q.x1 + q.x4) / 2, (q.y1 + q.y4) / 2,
	                                 hit.callback()));
	session.cancelAnnotations();
	const auto selected = hit.wait();
	ASSERT_TRUE(selected && selected->annotation);
	EXPECT_EQ(selected->annotation->target, item.target);
	Reply<pdf::NoteResult> edit;
	ASSERT_TRUE(session.updateMarkupComment(
	    0, item.index, pdf::TextMarkup::Highlight, {{}, false}, u"Comment",
	    edit.callback(), item.target.revision));
	ASSERT_TRUE(edit.wait()->changed);
	Reply<pdf::NoteResult> clear;
	ASSERT_TRUE(session.clearMarkupComment(item.target, {u"Comment", true},
	                                       clear.callback()));
	const auto cleared = clear.wait();
	ASSERT_TRUE(cleared);
	EXPECT_TRUE(cleared->changed);
	EXPECT_EQ(cleared->index, item.index);
	Reply<pdf::TextNoteResult> contents;
	ASSERT_TRUE(session.markupComment(0, item.index, pdf::TextMarkup::Highlight,
	                                  contents.callback(),
	                                  item.target.revision));
	const auto empty = contents.wait();
	ASSERT_TRUE(empty && empty->contents);
	EXPECT_TRUE(empty->contents->empty());
	EXPECT_EQ(empty->target, std::optional(item.target));
}

TEST_F(SessionTest, FileWritesLeaveTheEngineFreeAndKeepTheirSnapshot) {
	for (const bool copy : {false, true}) {
		pdf::Session session(engine);
		const auto source = fixturePath("pages.pdf");
		const auto target = writeTemp("worker.pdf", "previous");
		ASSERT_EQ(open(session, source).error, pdf::Error::None);
		const auto gate = std::make_shared<FileGate>();
		auto entered = gate->entered.get_future();
		auto unblock = std::shared_ptr<void>(
		    nullptr, [gate](void*) { gate->release.set_value(); });
		Reply<pdf::SaveResult> reply, busy;
		const auto calls = std::make_shared<std::atomic<int>>(0);
		const auto onThread = std::make_shared<std::atomic<bool>>(false);
		const auto callback = [calls, onThread, cb = reply.callback()](auto r) {
			++*calls;
			*onThread = pdf::Engine::onThread();
			cb(r);
		};
		const auto canceled = [gate] { return gate->poll(); };
		ASSERT_TRUE(copy ? session.saveCopy(target, source, canceled, callback)
		                 : session.saveEdits(target, source, 1024 * 1024,
		                                     canceled, callback));
		ASSERT_EQ(entered.wait_for(std::chrono::seconds(10)),
		          std::future_status::ready);
		EXPECT_FALSE(session.saveCopy(target, source, {}, busy.callback()));
		Reply<pdf::NoteResult> note;
		ASSERT_TRUE(session.addTextNote(0, 40, 50, u"After snapshot", {},
		                                note.callback()));
		const auto edited = note.wait();
		ASSERT_TRUE(edited && edited->index);
		unblock.reset();
		EXPECT_EQ(reply.wait(), pdf::SaveResult::Saved);
		EXPECT_TRUE(*onThread);
		EXPECT_EQ(*calls, 1);
		EXPECT_EQ(readFile(target), readFile(source));
	}
}

TEST_F(SessionTest, CloseAndDestructionCancelWorkerWritesOnce) {
	for (const bool copy : {false, true}) {
		for (const bool destroy : {false, true}) {
			auto session = std::make_unique<pdf::Session>(engine);
			const auto source = fixturePath("pages.pdf");
			const auto target = writeTemp("worker-close.pdf", "previous");
			const auto previous = readFile(target);
			ASSERT_EQ(open(*session, source).error, pdf::Error::None);
			const auto gate = std::make_shared<FileGate>();
			auto entered = gate->entered.get_future();
			auto unblock = std::shared_ptr<void>(
			    nullptr, [gate](void*) { gate->release.set_value(); });
			Reply<pdf::SaveResult> reply;
			const auto calls = std::make_shared<std::atomic<int>>(0);
			const auto callback = [calls, cb = reply.callback()](auto result) {
				++*calls;
				EXPECT_TRUE(pdf::Engine::onThread());
				cb(result);
			};
			const auto canceled = [gate] { return gate->poll(); };
			ASSERT_TRUE(
			    copy ? session->saveCopy(target, source, canceled, callback)
			         : session->saveEdits(target, source, 1024 * 1024, canceled,
			                              callback));
			ASSERT_EQ(entered.wait_for(std::chrono::seconds(10)),
			          std::future_status::ready);
			if (destroy) {
				session.reset();
			} else {
				session->close();
			}
			unblock.reset();
			EXPECT_EQ(reply.wait(), pdf::SaveResult::Canceled);
			EXPECT_EQ(readFile(target), previous);
			engine.run([] {}).get();
			EXPECT_EQ(*calls, 1);
		}
	}
}

TEST(EngineBackground, ShutdownDrainsWorkersAndEngineCompletions) {
	auto engine = std::make_unique<pdf::Engine>();
	const auto gate = std::make_shared<FileGate>();
	auto entered = gate->entered.get_future();
	auto unblock = std::shared_ptr<void>(
	    nullptr, [gate](void*) { gate->release.set_value(); });
	const auto calls = std::make_shared<std::atomic<int>>(0);
	const auto onThread = std::make_shared<std::atomic<bool>>(false);
	engine
	    ->run([&] {
		    engine->background([gate] { gate->poll(); },
		                       [calls, onThread] {
			                       *onThread = pdf::Engine::onThread();
			                       ++*calls;
		                       });
	    })
	    .get();
	ASSERT_EQ(entered.wait_for(std::chrono::seconds(10)),
	          std::future_status::ready);
	auto shutdown =
	    std::async(std::launch::async,
	               [engine = std::move(engine)]() mutable { engine.reset(); });
	EXPECT_EQ(shutdown.wait_for(std::chrono::milliseconds(30)),
	          std::future_status::timeout);
	unblock.reset();
	ASSERT_EQ(shutdown.wait_for(std::chrono::seconds(10)),
	          std::future_status::ready);
	shutdown.get();
	EXPECT_EQ(*calls, 1);
	EXPECT_TRUE(*onThread);
	EXPECT_FALSE(pdf::Engine::running());
}

TEST_F(SessionTest, SaveCopyIncludesNotesAndEveryTextMarkupSubtype) {
	pdf::Session session(engine);
	const auto source = fixturePath("text.pdf");
	const auto original = readFile(source);
	const auto target = writeTemp("annotated-copy.pdf", "previous");
	ASSERT_EQ(open(session, source).error, pdf::Error::None);
	Reply<pdf::NoteResult> note;
	ASSERT_TRUE(session.addTextNote(0, 40, 50, u"Copy \u03a9\U0001f600", {},
	                                note.callback()));
	const auto added = note.wait();
	ASSERT_TRUE(added && added->index == 0);
	for (const auto type :
	     {pdf::TextMarkup::Highlight, pdf::TextMarkup::Underline,
	      pdf::TextMarkup::StrikeOut}) {
		Reply<pdf::MarkupResult> marked;
		ASSERT_TRUE(
		    session.markText({{0, 0}, {1, 4}}, type, marked.callback()));
		const auto result = marked.wait();
		ASSERT_TRUE(result && result->count == 2);
	}
	Reply<pdf::SaveResult> saved;
	ASSERT_TRUE(session.saveCopy(target, source, {}, saved.callback()));
	ASSERT_EQ(saved.wait(), pdf::SaveResult::Saved);
	EXPECT_EQ(readFile(source), original);
	engine
	    .run([&] {
		    const auto copy = pdf::openFile(target);
		    ASSERT_TRUE(copy.document);
		    for (int page = 0; page < 2; ++page) {
			    const auto notes = copy.document->annotations(page);
			    ASSERT_TRUE(notes);
			    const std::size_t offset = page == 0 ? 1 : 0;
			    ASSERT_EQ(notes->entries.size(), offset + 3);
			    if (page == 0) {
				    EXPECT_EQ(notes->entries[0].subtype, "Text");
				    EXPECT_EQ(notes->entries[0].contents,
				              "Copy \u03a9\U0001f600");
			    }
			    EXPECT_EQ(notes->entries[offset].subtype, "Highlight");
			    EXPECT_EQ(notes->entries[offset + 1].subtype, "Underline");
			    EXPECT_EQ(notes->entries[offset + 2].subtype, "StrikeOut");
		    }
	    })
	    .get();
	Reply<pdf::SaveResult> protectedSource;
	ASSERT_TRUE(
	    session.saveCopy(source, source, {}, protectedSource.callback()));
	EXPECT_EQ(protectedSource.wait(), pdf::SaveResult::Source);
	EXPECT_EQ(readFile(source), original);
}
