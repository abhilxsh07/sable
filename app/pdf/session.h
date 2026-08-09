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
#include <utility>
#include <memory>

#include "page_cache.h"

namespace pdf {
class Engine;
struct PrintJob;
struct PrintRasterRequest;
enum class PrintResult;

struct SessionInfo {
	Error error = Error::None;
	std::vector<PageSize> pages;
	std::vector<std::string> labels;
	bool canceled = false;
	bool canCopy = false;
	bool canAnnotate = false;
	bool canFillForms = false;
};

struct RenderResult {
	PageKey key;
	std::optional<Bitmap> bitmap;
	bool canceled = false;
};
using PrintRenderer =
    std::function<bool(PrintRasterRequest, std::function<bool()>,
                       std::function<void(std::optional<Bitmap>)>)>;
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
struct FormFieldsResult {
	int page = 0;
	std::shared_ptr<const FormFields> fields;
	bool canceled = false;
};
struct AnnotationHitResult {
	std::optional<Annotation> annotation;
	bool canceled = false;
};
struct MarkupResult {
	int count = 0;
	bool changed = false, canceled = false;
	WriteRefusal refusal = WriteRefusal::None;
};
using HighlightResult = MarkupResult;
struct TextBoxResult {
	std::optional<Annotation> box;
	bool canceled = false;
};
struct TextNoteResult {
	int page = 0, index = 0;
	std::optional<std::u16string> contents;
	std::optional<AnnotationTarget> target;
	bool canceled = false;
	bool contentsPresent = true;
};
enum class PageEditKind { Rotate, Delete, Move };
struct PageEdit {
	PageEditKind kind = PageEditKind::Rotate;
	std::vector<int> pages;
	// quarter turns clockwise for Rotate; the first page's new index for Move.
	int turns = 1, destination = 0;
	std::uint64_t revision = 0;
};
struct PageEditResult {
	bool changed = false, canceled = false;
	WriteRefusal refusal = WriteRefusal::None;
	// the page sizes and labels after a change.
	SessionInfo info;
	std::uint64_t revision = 0;
};
struct PageImageResult {
	std::optional<Bitmap> bitmap;
	bool canceled = false;
};
struct DocumentTextResult {
	std::optional<std::string> text;
	bool canceled = false;
};
struct ExtractResult {
	std::optional<std::vector<std::uint8_t>> bytes;
	bool canceled = false;
};
struct NoteResult {
	int page = 0;
	std::optional<int> index;
	bool canceled = false;
	bool changed = false;
	// why the document refused, when it gave a reason.
	WriteRefusal refusal = WriteRefusal::None;
};
struct FormWriteResult {
	int page = 0, index = 0;
	std::uint64_t revision = 0;
	bool accepted = false, changed = false, canceled = false;
	WriteRefusal refusal = WriteRefusal::None;
};
struct PropertiesResult {
	std::optional<Properties> properties;
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
// outlive the session and its callbacks. A request rejected during shutdown
// returns false without a callback. No method waits for PDFium work.
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
	bool annotationAt(int page, double x, double y,
	                  std::function<void(AnnotationHitResult)> callback);
	void cancelAnnotations();
	// One immutable page is cached; replies still run on the engine thread.
	// New reads supersede older form reads independently of other requests.
	// Completed replies may arrive after cancellation; keep caller guards.
	bool formFields(int page, std::function<void(FormFieldsResult)> callback);
	void cancelForms();
	// Explicit snapshot revisions are required. Reads/cancelForms do not
	// cancel writes; close can cancel only before the document is replaced.
	bool updateTextField(int page, int index, std::u16string expected,
	                     std::u16string contents, std::uint64_t revision,
	                     std::function<void(FormWriteResult)> callback);
	bool updateCheckBox(int page, int index, std::u16string expected,
	                    bool checked, std::uint64_t revision,
	                    std::function<void(FormWriteResult)> callback);
	// i own the control metadata from the same snapshot as expected.
	bool updateRadioButton(int page, int index, std::u16string expected,
	                       FormControl control, std::uint64_t revision,
	                       std::function<void(FormWriteResult)> callback);
	// i keep the value and every option from the same immutable snapshot.
	// i use -1 to request a bounded single-list clear.
	bool updateChoiceField(int page, FormField field, int option,
	                       std::uint64_t revision,
	                       std::function<void(FormWriteResult)> callback);
	// Writes keep queue order, independently of annotation reads. Close only
	// cancels jobs that have not started; started edits keep their outcome.
	// An absent index means failure, possibly an incomplete edit if rollback
	// failed. Callbacks run once on the engine thread, outside session locks.
	bool addTextNote(int page, float x, float y, std::u16string contents,
	                 std::u16string author,
	                 std::function<void(NoteResult)> callback);
	std::uint64_t annotationRevision() const;
	bool addRectangleAt(int page, AnnotationPoint first, AnnotationPoint second,
	                    int rotation, std::uint64_t revision,
	                    std::function<void(NoteResult)> callback);
	bool addEllipseAt(int page, AnnotationPoint first, AnnotationPoint second,
	                  int rotation, std::uint64_t revision,
	                  std::function<void(NoteResult)> callback);
	bool addInkAt(int page, std::vector<DisplayedStroke> strokes, int rotation,
	              std::uint64_t revision,
	              std::function<void(NoteResult)> callback,
	              InkStyle style = {});
	bool addFillMarkAt(int page, AnnotationPoint point, int rotation,
	                   FillMark mark, std::uint64_t revision,
	                   std::function<void(NoteResult)> callback);
	bool addStampAt(int page, AnnotationPoint first, AnnotationPoint second,
	                int rotation, StampImage image, std::uint64_t revision,
	                std::function<void(NoteResult)> callback);
	bool resizeShape(Annotation expected, ShapeSize size,
	                 std::function<void(NoteResult)> callback);
	// moves a drawing between two displayed points and sets its color or
	// width where one is given; result.changed is false for no change.
	bool editInkAt(Annotation expected, AnnotationPoint from,
	               AnnotationPoint to, int rotation,
	               std::optional<AnnotationColor> color,
	               std::optional<float> width,
	               std::function<void(NoteResult)> callback);
	// moves a stamp between two displayed points and, with a width for its
	// rect, scales it around its upright top-left corner
	bool editStampAt(Annotation expected, AnnotationPoint from,
	                 AnnotationPoint to, int rotation,
	                 std::optional<float> width,
	                 std::function<void(NoteResult)> callback);
	bool addTextBox(int page, AnnotationBounds bounds, std::u16string contents,
	                std::u16string author, std::uint64_t revision,
	                std::function<void(NoteResult)> callback,
	                TextBoxLayout layout = TextBoxLayout::SingleLine);
	bool addTextBoxAt(int page, double x, double y, int rotation,
	                  std::u16string contents, std::u16string author,
	                  std::uint64_t revision,
	                  std::function<void(NoteResult)> callback,
	                  TextBoxLayout layout = TextBoxLayout::SingleLine);
	bool addCenteredTextNote(int page, std::u16string contents,
	                         std::u16string author,
	                         std::function<void(NoteResult)> callback);
	bool addTextNoteAt(int page, double x, double y, int rotation,
	                   std::u16string contents, std::u16string author,
	                   std::function<void(NoteResult)> callback);
	bool highlightText(TextSelection selection,
	                   std::function<void(HighlightResult)> callback);
	bool markText(TextSelection selection, TextMarkup markup,
	              std::function<void(MarkupResult)> callback,
	              HighlightColor color = HighlightColor::Yellow);
	// Box reads have their own generation; cancelAnnotations also cancels them.
	// Writes own the proved snapshot and only close can cancel a pending edit.
	bool textBox(AnnotationTarget target,
	             std::function<void(TextBoxResult)> callback,
	             TextBoxLayout layout = TextBoxLayout::SingleLine);
	bool updateTextBox(Annotation expected, std::u16string contents,
	                   std::function<void(NoteResult)> callback,
	                   TextBoxLayout layout = TextBoxLayout::SingleLine);
	bool updateTextBox(Annotation expected, std::u16string contents,
	                   TextBoxSize size,
	                   std::function<void(NoteResult)> callback,
	                   TextBoxLayout layout = TextBoxLayout::SingleLine);
	bool textNote(int page, int index,
	              std::function<void(TextNoteResult)> callback,
	              std::uint64_t revision = 0);
	bool updateTextNote(int page, int index, std::u16string expected,
	                    std::u16string contents,
	                    std::function<void(NoteResult)> callback,
	                    std::uint64_t revision = 0);
	bool markupComment(int page, int index, TextMarkup markup,
	                   std::function<void(TextNoteResult)> callback,
	                   std::uint64_t revision = 0);
	bool updateMarkupComment(int page, int index, TextMarkup markup,
	                         CommentContents expected, std::u16string contents,
	                         std::function<void(NoteResult)> callback,
	                         std::uint64_t revision = 0);
	bool removeAnnotation(AnnotationTarget target,
	                      std::function<void(NoteResult)> callback);
	// Rotates, deletes or moves pages of the open document in memory.
	bool editPages(PageEdit edit, std::function<void(PageEditResult)> callback);
	// Inserts pages (all when pages is empty) from a PDF file at index.
	bool insertPages(std::filesystem::path file, std::vector<int> pages,
	                 int index, std::uint64_t revision,
	                 std::function<void(PageEditResult)> callback);
	// Copies pages into new PDF bytes; the open document doesn't change.
	bool extractPages(std::vector<int> pages,
	                  std::function<void(ExtractResult)> callback);
	// An unfiltered page image as it prints, for export.
	bool pageImage(int page, int width, int height,
	               std::function<void(PageImageResult)> callback);
	// The pages' text joined by form feeds; nullopt when copying isn't
	// allowed.
	bool documentText(std::vector<int> pages,
	                  std::function<void(DocumentTextResult)> callback);
	bool clearMarkupComment(AnnotationTarget target, CommentContents expected,
	                        std::function<void(NoteResult)> callback);
	// One save at a time; successful replacement is never relabeled canceled.
	// Serialization keeps engine queue order; file writes run on its worker.
	// Cancellation can be polled by either thread.
	// An accepted save completes on the engine thread, even during shutdown.
	bool saveEdits(const std::filesystem::path& destination,
	               const std::filesystem::path& source, std::size_t limit,
	               std::function<bool()> canceled,
	               std::function<void(SaveResult)> callback);
	bool attachments(std::function<void(AttachmentsResult)> callback);
	bool properties(std::function<void(PropertiesResult)> callback);
	bool attachment(int index, std::function<void(AttachmentResult)> callback);
	bool saveCopy(const std::filesystem::path& destination,
	              const std::filesystem::path& source,
	              std::function<bool()> canceled,
	              std::function<void(SaveResult)> callback);
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
	// Independent of screen rendering; each tile yields back to the engine.
	PrintRenderer printRenderer() const;
	// doesn't interrupt PDFium; it discards that job's result when it finishes.
	// The slot stays busy until the canceled job calls back. A render that had
	// already finished can still arrive uncanceled, so check its key.
	void cancelRender();
	// permanent and idempotent. Pending callbacks still report cancellation.
	void close();

  private:
	using InkWrite = std::function<std::optional<int>(
	    Document&, const std::function<bool()>&)>;
	bool writeInk(int page, std::uint64_t revision, InkWrite write,
	              std::function<void(NoteResult)> callback);
	bool writeOutlineShape(int page, AnnotationPoint first,
	                       AnnotationPoint second, int rotation,
	                       OutlineShape shape, std::uint64_t revision,
	                       std::function<void(NoteResult)> callback);
	bool changeTextBox(Annotation expected, std::u16string contents,
	                   std::optional<TextBoxSize> size,
	                   std::function<void(NoteResult)> callback,
	                   TextBoxLayout layout);
	bool
	writeTextBox(int page, AnnotationBounds bounds, std::u16string contents,
	             std::u16string author, std::uint64_t revision,
	             std::function<void(NoteResult)> callback, TextBoxLayout layout,
	             std::optional<AnnotationPoint> point = {}, int rotation = 0);
	bool writeFormField(int page, int index, std::u16string expected,
	                    std::u16string contents, std::uint64_t revision,
	                    std::optional<bool> checked,
	                    std::optional<FormControl> control,
	                    std::optional<FormField> choice, int option,
	                    std::function<void(FormWriteResult)> callback);
	bool changeAnnotation(AnnotationTarget target,
	                      std::optional<CommentContents> expected,
	                      std::function<void(NoteResult)> callback);
	bool writeTextNote(int page, std::optional<std::pair<double, double>> point,
	                   std::u16string contents, std::u16string author,
	                   std::function<void(NoteResult)> callback,
	                   std::optional<int> rotation = {});
	struct State;
	static void postWrite(State& state, bool changed);
	static void finishPageEdit(State& state, PageEditResult& result,
	                           const std::function<bool()>& canceled);
	std::shared_ptr<State> m_state;
};
} // namespace pdf
