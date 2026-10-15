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
