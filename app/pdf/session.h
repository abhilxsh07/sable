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
#pragma once

#include <functional>
#include <memory>

#include "page_cache.h"

namespace pdf {
class Engine;
struct PrintJob;
enum class PrintResult;

struct SessionInfo {
	Error error = Error::None;
	std::vector<PageSize> pages;
	std::vector<std::string> labels;
	bool canceled = false;
	bool canCopy = false;
};

struct RenderResult {
	PageKey key;
	std::optional<Bitmap> bitmap;
	bool canceled = false;
};
struct LinkResult {
	std::optional<Link> link;
	bool canceled = false;
};
struct TextResult {
	int page = 0;
	std::optional<TextPage> text;
	bool canceled = false;
};
struct CopyResult {
	std::string text;
	bool failed = false;
	bool canceled = false;
};

struct OutlineResult {
	std::optional<Outline> outline;
	bool canceled = false;
};
struct AnnotationsResult {
	int page = 0;
	std::optional<Annotations> annotations;
	bool canceled = false;
};
struct AttachmentsResult {
	std::optional<Attachments> attachments;
	bool canceled = false;
};
struct AttachmentResult {
	std::optional<std::vector<std::uint8_t>> bytes;
	bool canceled = false;
};
struct FindUpdate {
	std::vector<FindMatch> matches;
	int pagesScanned = 0, pageCount = 0;
	bool done = false, canceled = false, failed = false, limited = false;
};

// The UI owns this, but the document stays on the engine thread. Engine must
// outlive the session and its callbacks; work posted while it shuts down is
// dropped. No method waits for PDFium work.
class Session {
  public:
	explicit Session(Engine& engine);
	~Session();
	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;

	// one open attempt per session. False means no job or callback was queued.
	bool open(const std::filesystem::path& path, const std::string& password,
	          std::function<void(SessionInfo)> callback);
	// accepted only after opening, with at most one render outstanding.
	// Callbacks run once on the engine thread, also for errors/cancellation.
	// A callback may request the next render; its exceptions stop only itself.
	bool render(PageKey key, std::function<void(RenderResult)> callback);
	bool linkAt(int page, double x, double y,
	            std::function<void(LinkResult)> callback);
	bool textPage(int page, std::function<void(TextResult)> callback);
	bool outline(std::function<void(OutlineResult)> callback);
	// New requests supersede older annotation reads, independently of Find
	// and rendering. Completed replies can still arrive after cancellation.
	bool annotations(int page, std::function<void(AnnotationsResult)> callback);
	void cancelAnnotations();
	bool attachments(std::function<void(AttachmentsResult)> callback);
	bool attachment(int index, std::function<void(AttachmentResult)> callback);
	// A newer outline navigation cancels older queued navigation.
	bool outlineLink(std::size_t index,
	                 std::function<void(LinkResult)> callback);
	bool copyText(TextSelection selection,
	              std::function<void(CopyResult)> callback);
	// Batches run on the engine thread, yielding between pages. A new query
	// cancels its predecessor independently of screen rendering.
	bool search(std::u16string query, FindOptions options,
	            std::function<void(FindUpdate)> callback);
	void cancelSearch();
	// One print job per session, independent of the screen render slot.
	bool print(PrintJob job, std::function<void(PrintResult)> callback);
	// doesn't interrupt PDFium; it discards that job's result when it finishes.
	// The slot stays busy until the canceled job calls back. A render that had
	// already finished can still arrive uncanceled, so check its key.
	void cancelRender();
	// permanent and idempotent. Pending callbacks still report cancellation.
	void close();

  private:
	struct State;
	std::shared_ptr<State> m_state;
};
} // namespace pdf
