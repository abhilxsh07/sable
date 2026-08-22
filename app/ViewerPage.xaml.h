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
#include "Appearance.h"

#include "ViewerPage.g.h"
// the pages' screen-reader host, which XamlTypeInfo needs through here
#include "PageText.h"

#include <d2d1_1.h>

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "helpers/layout.h"
#include "helpers/navigation.h"
#include "helpers/sliders.h"
#include "helpers/thumbnails.h"
#include "helpers/comments.h"
#include "helpers/signature.h"
#include "helpers/zoom.h"
#include "pdf/write_guard.h"
#include "helpers/rotation.h"
#include "helpers/pan.h"
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Media.SpeechSynthesis.h>
#include "pdf/page_cache.h"
#include "pdf/text.h"
#include "pdf/search.h"

namespace pdf {
class Engine;
class PageFilter;
class Session;
enum class TextMarkup;
class Surface;
struct RenderResult;
struct NoteResult;
struct SessionInfo;
struct PageEdit;
struct PageEditResult;
struct FindUpdate;
struct Link;
struct LinkDestination;
struct PrintDialogResult;
struct FormField;
} // namespace pdf

namespace printing {
class Controller;
}

namespace winrt::Sable::implementation {
// a refused write names its reason when the document gave one.
inline const wchar_t* writeNotice(pdf::WriteRefusal refusal,
                                  const wchar_t* fallback) {
	if (refusal == pdf::WriteRefusal::Signed) {
		return L"This PDF is signed; edits would break the signature.";
	}
	if (refusal == pdf::WriteRefusal::TooLarge) {
		return L"This PDF is over 128 MiB, which is too large to edit.";
	}
	return fallback;
}
struct MainWindow;
// UI thread only. The session does the PDFium work on the engine thread and
// its callbacks come back here through the dispatcher queue.
struct ViewerPage : ViewerPageT<ViewerPage> {
	ViewerPage();
	~ViewerPage();

	// opens path and, once the pages are laid out, scrolls to page (1-based,
	// like -p). thumbnails opens the thumbnail panel with it, NightPDF's
	// DisplayThumbs. One viewer per document tab.
	void open(pdf::Engine& engine, const std::filesystem::path& path,
	          std::optional<int> page, bool thumbnails);
	// lets go of the document while the engine is still around
	void close();
	// a pending password prompt waits until this tab is selected
	void activate();
	// the keys go where they would after opening: Find while it's open,
	// otherwise the pages. False when there's nothing to focus, like a tab
	// that only shows an error.
	bool takeFocus();
	// called once each time the document opens, after the pages are in
	void onOpened(std::function<void()> handler) {
		m_documentOpened = std::move(handler);
	}
	// changes only the GPU filter; page rasters stay in the cache
	void setValues(const helpers::SliderValues& values);
	// pdf.js's zoom: about 10% a step, or back to "auto"
	void zoomIn(int steps = 1);
	void zoomOut(int steps = 1);
	void zoomReset();
	void rotateView(int turns);
	// Organize, or another tool asked for from Home, the nav pane or a
	// file just picked for it.
	void requestTool(std::wstring_view tool);
	void openFillSign();
	void setLayout(helpers::ScrollMode scroll, helpers::SpreadMode spread);
	helpers::ScrollMode scrollMode() const {
		return m_scrollMode;
	}
	helpers::SpreadMode spreadMode() const {
		return m_spreadMode;
	}
	enum class CursorTool { Select, Hand };
	void setCursorTool(CursorTool tool);
	CursorTool cursorTool() const {
		return m_cursorTool;
	}
	void stopPan(bool leaveViewport = false);
	void print(void* owner, std::function<void()> done);
	winrt::fire_and_forget showProperties();
	void dismissProperties();
	// A menu temporarily takes focus from an unfinished page-box edit.
	void holdPageEdit(bool hold);
	bool hasTextSelection() const;
	bool canCopySelection() const;
	void copySelection();
	void selectAllText();
	void clearTextSelection();
	void openFind();
	void closeFind();
	void navigateFind(int direction);
	bool findOpen();
	void onPageTapped(
	    const winrt::Windows::Foundation::IInspectable&,
	    const winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs&);

	// the right rail and the keys, wired up in ViewerPage.xaml
	bool navigationKey(unsigned key, helpers::NavModifiers modifiers);
	void onPageBoxKeyDown(
	    const winrt::Windows::Foundation::IInspectable&,
	    const winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs& args);
	void onPageBoxGotFocus(const winrt::Windows::Foundation::IInspectable&,
	                       const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onPageBoxLosingFocus(
	    const winrt::Windows::Foundation::IInspectable&,
	    const winrt::Microsoft::UI::Xaml::Input::LosingFocusEventArgs&);
	void onPreviousPage(const winrt::Windows::Foundation::IInspectable&,
	                    const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onNextPage(const winrt::Windows::Foundation::IInspectable&,
	                const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onZoomIn(const winrt::Windows::Foundation::IInspectable&,
	              const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onZoomOut(const winrt::Windows::Foundation::IInspectable&,
	               const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onZoomMenuOpening(const winrt::Windows::Foundation::IInspectable&,
	                       const winrt::Windows::Foundation::IInspectable&);
	void onZoomChoice(const winrt::Windows::Foundation::IInspectable& sender,
	                  const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);
	void onThumbnailsClick(const winrt::Windows::Foundation::IInspectable&,
	                       const winrt::Microsoft::UI::Xaml::RoutedEventArgs&);

  private:
	friend struct MainWindow;
	struct PresentationView {
		helpers::ScrollMode scroll;
		helpers::SpreadMode spread;
		helpers::ZoomPreset preset;
		double zoom;
		winrt::Microsoft::UI::Xaml::Visibility thumbnails, outline, attachments;
		winrt::Microsoft::UI::Xaml::Controls::ScrollMode horizontal, vertical;
	};
	std::optional<PresentationView> m_presentation;
	std::function<void()> m_exitPresentation;
	std::function<void()> m_documentOpened;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_presentationTimer{
	    nullptr};
	std::chrono::steady_clock::time_point m_presentationWheelTime{};
	int m_presentationDelta = 0;
	std::optional<std::uint32_t> m_presentationTouch;
	winrt::Windows::Foundation::Point m_presentationStart{};
	bool m_presentationSwiped = false;
	void setupPresentation();
	bool beginPresentation(std::function<void()> exit);
	void endPresentation();
	void showPresentationControls();
	void presentationKey(unsigned key, bool shift);
	void presentationWheel(int delta);
	// pdf.js's transparent page border and margins, in DIPs
	// see:
	// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/pdf_viewer.css
	static constexpr double PAGE_GAP = 11;
	static constexpr double PAGE_MARGIN = 10;
	void printLegacy(void* owner, std::function<void()> done);
#ifdef SABLE_SURFACE_TEST
	friend struct TabsTest;
	friend struct ScaleTest;
	friend struct ViewerErrorTest;
#endif
	struct Device {
		struct Raster {
			winrt::com_ptr<ID2D1Bitmap1> bitmap;
			std::uint64_t version;
			int rotation = 0;
		};
		winrt::com_ptr<ID2D1Device> device;
		winrt::com_ptr<ID2D1DeviceContext> context;
		std::unique_ptr<pdf::PageFilter> filter;
		winrt::com_ptr<ID2D1SolidColorBrush> placeholder;
		winrt::com_ptr<ID2D1SolidColorBrush> selection;
		winrt::com_ptr<ID2D1SolidColorBrush> annotationOutline,
		    annotationBackground;
		winrt::com_ptr<ID2D1StrokeStyle> annotationDashes;
		winrt::com_ptr<ID2D1SolidColorBrush> findMatch, findActive;
		std::map<std::size_t, Raster> bitmaps;
	};

	void startOpen(const std::string& password);
	void requestProperties();
	void updatePropertiesDialog();
	std::optional<pdf::Properties> m_properties;
	winrt::Microsoft::UI::Xaml::Controls::ContentDialog m_propertiesDialog{
	    nullptr};
	std::uint64_t m_propertiesVersion = 0;
	bool m_propertiesPending = false, m_propertiesFailed = false;
	std::size_t m_propertiesPage = 0;
	helpers::PageSize m_propertiesSize{};
	void setupSelection();
	void setupPan();
	bool beginPan(winrt::Windows::Foundation::Point point);
	void movePan(winrt::Windows::Foundation::Point point);
	void finishPan(winrt::Windows::Foundation::Point point);
	void updatePanCursor();
	CursorTool m_cursorTool = CursorTool::Select;
	helpers::PanGesture m_pan;
	std::optional<std::uint32_t> m_panPointer;
	bool m_panHover = false, m_ignorePanTap = false;
	winrt::Microsoft::UI::Input::InputSystemCursor m_handCursor{nullptr},
	    m_dragCursor{nullptr};
	void setupOutline();
	void setupComments();
	struct NoteTarget {
		int page;
		double x, y;
		int rotation;
	};
	enum class PlacementTool {
		Note,
		TextBox,
		Rectangle,
		Ellipse,
		// two clicks, drawn as ink in the current Drawing style
		Line,
		Arrow,
		Ink,
		Stamp,
		FillCheck,
		FillCross,
		// the text-box writer, reached from Fill & Sign.
		FillText,
		// visual signatures: an image stamp of a typed name, or ink.
		TypedSignature,
		DrawnSignature
	};
	void beginNotePlacement(PlacementTool tool = PlacementTool::Note);
	void placeLine(NoteTarget first, NoteTarget target, std::uint64_t revision);
	bool canPickStamp();
	winrt::fire_and_forget pickStamp();
	bool cancelNotePlacement();
	bool placeNote(winrt::Windows::Foundation::Point point);
	void placeShape(NoteTarget target);
	void placeFillMark(NoteTarget target);
	winrt::fire_and_forget createSignature(PlacementTool tool);
	winrt::Windows::Foundation::IAsyncAction drawSignature();
	void placeSignature(NoteTarget target);
	// signatures last until the app closes; nothing is saved to settings.
	static inline std::u16string s_signatureName;
	static inline std::vector<helpers::PadStroke> s_signatureStrokes;
	std::optional<pdf::StampImage> m_signatureImage;
	std::function<void(pdf::NoteResult)> placementReply(pdf::Session* session,
	                                                    const wchar_t* failure);
	struct ShapeStart {
		NoteTarget corner;
		std::uint64_t revision;
	};
	std::optional<ShapeStart> m_shapeStart;
	std::optional<pdf::StampImage> m_stampImage;
	std::uint64_t m_stampPickVersion = 0;
	bool m_stampPicking = false;
#ifdef SABLE_SURFACE_TEST
	std::function<winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>()>
	    m_stampPicker;
#endif
	bool m_notePlacing = false;
	PlacementTool m_placementTool = PlacementTool::Note;
	// the stroke under the pointer, in displayed page fractions
	struct InkDrawing {
		int page;
		int rotation;
		std::uint64_t revision;
		pdf::DisplayedStroke points;
		pdf::InkStyle style;
	};
	// shared by every tab and window while Sable runs, and copied by each
	// stroke when it starts. it isn't saved, so the settings stay NightPDF's.
	static inline pdf::InkStyle s_inkStyle;
	void chooseInkStyle(std::optional<pdf::AnnotationColor> color,
	                    std::optional<float> width);
	// a menu color swatch that stays visible on light and dark menus
	static winrt::Microsoft::UI::Xaml::Controls::IconElement
	colorDot(winrt::Windows::UI::Color color);
	std::optional<InkDrawing> m_inkStroke;
	bool beginInkStroke(winrt::Windows::Foundation::Point point);
	void extendInkStroke(winrt::Windows::Foundation::Point point);
	void finishInkStroke();
	void dropInkStroke();
	winrt::fire_and_forget
	editTextBox(NoteTarget target,
	            std::optional<pdf::Annotation> existing = {});
	winrt::hstring m_textBoxDraft, m_textBoxAuthor;
	winrt::fire_and_forget editShape(pdf::Annotation existing);
	winrt::fire_and_forget editDrawing(pdf::Annotation existing);
	winrt::fire_and_forget editStampSize(pdf::Annotation existing);
	winrt::fire_and_forget editLineLength(pdf::Annotation existing);
	// a selected drawing or stamp dragged on the page, in Pages coordinates
	struct AnnotationDrag {
		pdf::Annotation annotation;
		winrt::Windows::Foundation::Point start, current;
		bool queued = false;
	};
	std::optional<AnnotationDrag> m_annotationDrag;
	bool movableAnnotation() const;
	bool beginAnnotationDrag(winrt::Windows::Foundation::Point point);
	void extendAnnotationDrag(winrt::Windows::Foundation::Point point);
	void finishAnnotationDrag(winrt::Windows::Foundation::Point point);
	void dropAnnotationDrag();
	winrt::fire_and_forget editNote(std::optional<NoteTarget> target = {});
	void submitNote(int page, std::u16string text, std::u16string author,
	                std::optional<NoteTarget> target = {});
	bool canMarkSelection() const;
	void markSelection(pdf::TextMarkup markup,
	                   pdf::HighlightColor color = pdf::HighlightColor::Yellow);
	bool canDeleteAnnotation();
	void deleteAnnotation();
	std::optional<pdf::AnnotationTarget> annotationTarget();
	void clearAnnotationSelection();
	void requestAnnotationAt(winrt::Windows::Foundation::Point point,
	                         bool context = false);
	void requestFormAt(winrt::Windows::Foundation::Point point);
	void cancelFormRequest();
	winrt::fire_and_forget editFormField(int page, std::uint64_t revision,
	                                     pdf::FormField field);
	std::uint64_t m_formVersion = 0;
	void showAnnotationMenu(winrt::Windows::Foundation::Point point);
	void editAnnotation(pdf::AnnotationTarget target);
	void drawSelectedAnnotation(ID2D1DeviceContext& context,
	                            helpers::PageRange pages);
	std::optional<pdf::Annotation> m_selectedAnnotation;
	std::uint64_t m_annotationVersion = 0;
	bool m_annotationPending = false;
	winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_annotationMenu{nullptr};
	void showSelectionMenu();
	bool m_selectionMenuPending = false;
	void beginNoteEdit();
	winrt::fire_and_forget
	editExistingNote(int page, int index, std::u16string expected,
	                 std::optional<pdf::TextMarkup> markup = {},
	                 bool contentsPresent = true,
	                 std::optional<pdf::AnnotationTarget> target = {});
	void refreshNotes(std::optional<int> edited = {});
	bool layOut(int rotation, std::size_t page);
	bool organizing();
	void setupOrganize();
	void openOrganize();
	void closeOrganize();
	void fillOrganize();
	void renderOrganizeTile();
	void showOrganizeTile(std::size_t page, const pdf::Bitmap& bitmap);
	std::vector<int> organizeSelection();
	void showOrganizeState();
	void reorderPages(std::vector<int> dragged);
	void organizeEdit(pdf::PageEdit edit);
	void finishPageEdit(pdf::PageEditResult result);
	void pagesChanged(pdf::SessionInfo info);
	winrt::fire_and_forget insertPagesFromFile();
	winrt::fire_and_forget extractSelectedPages();
	winrt::fire_and_forget exportPages();
	bool m_exporting = false;
#ifdef SABLE_SURFACE_TEST
	// a folder when asked for one, else a .txt path
	std::function<winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>(
	    bool)>
	    m_exportPicker;
#endif
	std::wstring m_pendingTool;
	// the command bar, the All tools panel and the quick-tools strip
	void setupToolSurfaces();
	void setAllToolsOpen(bool open);
	void runTool(std::wstring_view tool);
	void updateQuickTools();
	// save, print, properties, presentation and combine belong to the window
	std::function<void(std::wstring_view)> m_commandRequested;
	std::vector<std::pair<
	    winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton,
	    std::wstring>>
	    m_quickToolButtons;
	bool m_toolSurfacesSetup = false, m_syncingQuickTools = false;
	// shown or hidden for every tab until the app closes
	static inline bool s_quickTools = true;
	// Read Aloud: one page at a time through Windows' voices
	void startReadAloud();
	void readAloudPage(std::size_t page);
	void speakReadAloud(std::uint64_t generation, std::size_t page,
	                    std::optional<std::string> text);
	winrt::fire_and_forget synthesizeReadAloud(std::uint64_t generation,
	                                           winrt::hstring text);
	void readAloudEnded();
	void pauseReadAloud();
	void resumeReadAloud();
	void stopReadAloud();
	void stepReadAloud(int delta);
	void chooseReadAloud();
	void updateReadAloudBar();
	void updateReadAloudFrame();
	bool readingAloud() const;
	void setupReadAloud();
	winrt::Windows::Media::SpeechSynthesis::SpeechSynthesizer m_speech{nullptr};
	winrt::Windows::Media::Playback::MediaPlayer m_speechPlayer{nullptr};
	winrt::Windows::Media::Playback::MediaPlayer::MediaEnded_revoker
	    m_speechEnded;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::ViewChanged_revoker
	    m_readAloudViewChanged;
	std::vector<winrt::Windows::Media::SpeechSynthesis::VoiceInformation>
	    m_readAloudVoices;
	std::optional<std::size_t> m_readAloudPage;
	std::uint64_t m_readAloudGeneration = 0;
	std::int64_t m_readAloudVisibility = 0;
	bool m_readAloudPaused = false, m_readAloudPending = false;
	bool m_readAloudSetup = false, m_readAloudFilling = false;
	// the probes read the words a page gave instead of hearing them
	bool m_readAloudSilent = false;
	std::string m_readAloudWords;
	// read-aloud.json next to config.json; MainWindow sets it
	static inline std::filesystem::path s_readAloudFile;
	bool m_organizeSetup = false, m_organizeBusy = false;
	bool m_organizeRendering = false;
	std::size_t m_organizeTile = 0;
	std::uint64_t m_organizeVersion = 0;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_organizeTimer{
	    nullptr};
	bool m_noteAllowed = false, m_fillAllowed = false, m_noteBusy = false,
	     m_notesDirty = false, m_formsDirty = false, m_noteSaving = false;
	std::uint64_t m_renderVersion = 0;
	winrt::hstring m_noteDraft, m_noteAuthor;
	winrt::Microsoft::UI::Xaml::Controls::ContentDialog m_noteDialog{nullptr};
	std::function<void()> m_saveNotes;
	std::function<bool()> m_notesAvailable;
	void followComment(int index);
	void closeComments();
	void pauseComments();
	void showComments();
	void requestComments();
	void setCommentsOpen(bool open);
	bool commentsOpen();
	bool commentsHasFocus();
	helpers::CommentScan m_commentScan;
	std::uint64_t m_commentVersion = 0;
	bool m_commentsPending = false;
	std::optional<std::int64_t> m_commentVisibility, m_commentRailVisibility;
	void setupAttachments();
	void setupAttachmentSaving();
	winrt::fire_and_forget saveAttachment(int index);
	std::uint64_t m_attachmentSaveVersion = 0;
	bool m_attachmentSaving = false;
	void closeAttachments();
	void requestAttachments();
	void showAttachments();
	bool attachmentsOpen();
	bool attachmentsHasFocus();
	void setAttachmentsOpen(bool open);
	std::optional<pdf::Attachments> m_attachments;
	std::uint64_t m_attachmentVersion = 0;
	bool m_attachmentsPending = false, m_attachmentsFailed = false,
	     m_attachmentsBuilt = false;
	void closeOutline();
	void requestOutline();
	void showOutline();
	void followOutline(std::size_t index);
	bool outlineOpen();
	bool outlineHasFocus();
	void setOutlineOpen(bool open);
	void toggleSidebar();
	enum class Sidebar { Thumbnails, Outline, Attachments, Comments };
	Sidebar m_sidebar = Sidebar::Thumbnails;
	std::optional<pdf::Outline> m_outline;
	std::vector<winrt::Microsoft::UI::Xaml::Controls::TreeViewNode>
	    m_outlineNodes;
	std::uint64_t m_outlineVersion = 0;
	std::uint64_t m_outlineLinkVersion = 0;
	bool m_outlineLinkPending = false;
	bool m_outlinePending = false, m_outlineFailed = false,
	     m_outlineBuilt = false;
	void setupFind();
	void invalidateFind();
	void changedFind();
	void startFind();
	void applyFind(pdf::FindUpdate update);
	void revealFindMatch();
	void showFindCount();
	bool hasFindOnPage(int page);
	void drawFind(ID2D1DeviceContext& context, helpers::PageRange pages);
	std::vector<pdf::FindMatch> m_findMatches;
	std::map<int, std::vector<std::size_t>> m_findPages;
	std::optional<std::size_t> m_findActive;
	std::uint64_t m_findVersion = 0;
	std::size_t m_findStartPage = 0;
	bool m_finding = false, m_findReverse = false, m_findNeedsReveal = false,
	     m_findLimited = false, m_findFailed = false;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_findTimer{
	    nullptr};
	void beginSelection(winrt::Windows::Foundation::Point point);
	void extendSelection(winrt::Windows::Foundation::Point point);
	void endSelection();
	void cancelSelectionDrag();
	void updateSelection();
	void requestText(int page);
	void prefetchSelectedText();
	void invalidateSelection();
	struct CachedText {
		std::shared_ptr<const pdf::TextPage> text;
		std::uint64_t used = 0;
	};
	std::map<int, CachedText> m_text;
	std::set<int> m_textFailed, m_textPrefetched;
	helpers::PageRange m_textView;
	std::optional<int> m_textPending;
	std::uint64_t m_textClock = 0, m_selectionVersion = 0;
	std::optional<pdf::TextSelection> m_selection;
	std::optional<pdf::TextPosition> m_textAnchor;
	std::optional<winrt::Windows::Foundation::Point> m_textStart, m_textPoint;
	winrt::Windows::Foundation::Point m_textViewport{};
	bool m_textPointerDown = false, m_textMoved = false,
	     m_ignoreTextTap = false;
	bool m_copyAllowed = false, m_copying = false;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_textScroll{
	    nullptr};
	winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_textMenu{nullptr};
	winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem m_textCopy{nullptr};
	winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem m_textHighlight{
	    nullptr};
	winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem m_textUnderline{
	    nullptr};
	winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem m_textStrikeOut{
	    nullptr};
	winrt::fire_and_forget askPassword(bool incorrect);
	void opened(pdf::SessionInfo info);
	bool relayout(double zoom);
	helpers::PageLayout makeLayout(std::span<const helpers::PageSize> sizes,
	                               double zoom);
	double fittedZoom(std::span<const helpers::PageSize> sizes);
	helpers::PageRect viewport();
	void reflowViewport();
	bool ensurePageLayout(std::size_t page);
	void advancePage(int direction);
	bool spreadClicksEnabled() const;
	int spreadClickDirection(winrt::Windows::Foundation::Point point);
	void turnSpread(int direction);
	bool m_clickSpreads = false;
	std::vector<helpers::PageSize> displaySizes() const;
	pdf::TextRect viewBox(const pdf::TextRect& box) const;
	std::optional<int> hitText(int page,
	                           winrt::Windows::Foundation::Point point,
	                           bool nearest = false) const;
	pdf::LinkDestination viewDestination(pdf::LinkDestination target) const;
	void watchScale();
	void queueScale();
	bool setScale(double scale);
	bool setZoom(double zoom, winrt::Windows::Foundation::Point anchor);
	void zoomTo(double zoom, winrt::Windows::Foundation::Point anchor);
	void setPreset(helpers::ZoomPreset preset);
	void applyPreset();
	void settle();
	void nativeViewChanged(bool intermediate);
	void updateGestureLimits();
	void queueNativeZoom();
	void finishNativeZoom();
	void applyNativeZoom();
	struct NativeZoom {
		double zoom, across, down;
		std::size_t page;
	};
	std::optional<NativeZoom> m_nativeZoom;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer
	    m_nativeResetTimeout{nullptr};
	bool m_directManipulation = false, m_nativeZoomQueued = false;
	bool m_resettingGesture = false;
	void resetNativeZoom();
	void dropNativeZoom();
	void nativeGestureStarted();
	// The scroller's zoom resets a frame or more before the pages are laid out
	// again, and that frame showed them at the old zoom in the wrong place.
	// The gesture's last view is drawn over them until the new layout lands.
	enum class FreezeStage { None, Showing, Resetting, Landing };
	struct Freeze {
		// the document pixel at the viewport's top left, and the scale
		double left, top, factor;
		bool drawn = false;
	};
	bool showFreeze();
	void hideFreeze();
	void dropFreeze();
	void freezeFrame();
	void drawFreeze(ID2D1DeviceContext& context, const RECT& rect);
	std::optional<Freeze> m_freeze;
	FreezeStage m_freezeStage = FreezeStage::None;
	std::chrono::steady_clock::time_point m_freezeSince;
	int m_freezeFrames = 0;
	std::shared_ptr<pdf::Surface> m_freezeSurface;
	winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering_revoker
	    m_freezeRendering;
#ifdef SABLE_SURFACE_TEST
	int m_freezeShows = 0;
#endif
	void goToPage(std::size_t page);
	void scrollToPage(std::size_t page);
	void requestLink(winrt::Windows::Foundation::Point point,
	                 int presentationAdvance = 0);
	void followLink(const pdf::LinkDestination& target, bool center = false);
	winrt::fire_and_forget openLink(std::string uri);
	void jumpTo(std::optional<double> x, double y);
	void updateCurrentPage();
	void showPosition();
	void commitPageBox();
	void leavePageBox();
	bool thumbnailsOpen();
	void setThumbnailsOpen(bool open);
	void scrollThumbnailIntoView();
	void landThumbnailScroll();
	void updateThumbnails();
	winrt::Microsoft::UI::Xaml::Controls::Button makeThumbnail();
	void
	prepareThumbnail(const winrt::Microsoft::UI::Xaml::Controls::Button& button,
	                 std::size_t page);
	void
	styleThumbnail(const winrt::Microsoft::UI::Xaml::Controls::Button& button,
	               std::size_t page);
	void showThumbnail(std::size_t page, const pdf::Bitmap& bitmap);
	pdf::PageKey thumbKey(std::size_t page) const;
	void queueRequest();
	bool retryFailed(const pdf::PageKey& key);
	void startRender(const pdf::PageKey& key);
	double imageLeft();
	void onWheel(
	    const winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs& args);
	void rendered(pdf::RenderResult result);
	void showMessage(const winrt::hstring& text);
	void showNotice(const winrt::hstring& text);
	void
	showNotice(const winrt::hstring& text,
	           winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
	Device makeDevice();
	void rebuildDevice();
	void updateVisible();
	void requestNext();
	bool showRaster(std::size_t page, const pdf::Bitmap* bitmap);
	bool upload(Device& device, std::size_t page, const pdf::Bitmap& bitmap);
	void invalidate(std::size_t page);
	void draw(ID2D1DeviceContext& context, const RECT& rect);
	helpers::PixelRect pixels(std::size_t page) const;
	pdf::PageKey key(std::size_t page) const;

	std::unique_ptr<pdf::Session> m_session;
	std::shared_ptr<printing::Controller> m_printController;
	// what open() was given, to open again with a password
	pdf::Engine* m_engine = nullptr;
	std::filesystem::path m_path;
	winrt::Microsoft::UI::Xaml::Controls::ContentDialog m_passwordDialog{
	    nullptr};
	// a prompt waiting for the page to be in the window (true: "invalid")
	std::optional<bool> m_passwordPending;
	winrt::Microsoft::UI::Xaml::FrameworkElement::Loaded_revoker m_promptLoaded;
	// page sizes in points, to lay them out again at another zoom
	std::vector<helpers::PageSize> m_sizes;
	helpers::ScrollMode m_scrollMode = helpers::ScrollMode::Vertical;
	helpers::SpreadMode m_spreadMode = helpers::SpreadMode::None;
	bool m_reflowQueued = false;
	double m_layoutViewWidth = 0;
	std::chrono::steady_clock::time_point m_pageWheelTime{};
	int m_rotation = 0;
	std::uint64_t m_rotationVersion = 0;
	std::optional<helpers::PageLayout> m_layout;
	double m_zoom = 1;
	// pdf.js's currentScaleValue when it's a name: it opens at "auto", and
	// a zoom key, the wheel or a percentage from the menu makes it a number
	helpers::ZoomPreset m_preset = helpers::ZoomPreset::Auto;
	// opening can finish before the viewport has a size, so a fitting
	// preset is worked out again once it has one
	bool m_fitPending = false;
	// right after a zoom the old rasters are stretched; sharp ones are
	// rendered once it has been still for a moment
	bool m_settling = false;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_settle{nullptr};
	std::optional<int> m_startPage;
	// pdf.js's current page (0-based) and the labels it would have shown
	std::size_t m_current = 0;
	// ViewerPageText.cpp: the page whose text the screen-reader host has
	std::optional<std::size_t> m_pageTextPage;
	void updatePageText();
	std::vector<std::string> m_labels;
	// what the page box was last set to, so only an edit goes anywhere
	winrt::hstring m_pageBoxText;
	bool m_pageEditHeld = false;
	// a jump through ChangeView lands a moment later, where a browser's
	// scrollTop changes at once. Until it has, the current page is worked
	// out for the offsets it's going to (x, y), with a timer in case it
	// never quite gets there.
	std::optional<std::pair<double, double>> m_landingAt;
	winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_land{nullptr};
	// while setZoom has the new layout but the old offsets
	bool m_holdCurrent = false;
	double m_scale = 1;
	bool m_scaleQueued = false;
	winrt::Microsoft::UI::Xaml::XamlRoot::Changed_revoker m_rootChanged;
	winrt::Microsoft::UI::Xaml::FrameworkElement::Loaded_revoker m_loaded;
	helpers::SliderValues m_values;
	helpers::PageRange m_visible;
	helpers::PageRange m_wanted;
	pdf::PageCache m_cache;
	// failed rasters get one retry before an edit or rotation resets them.
	std::set<pdf::PageKey> m_failed;
	std::set<pdf::PageKey> m_retriedRenders, m_retriedUploads;
	bool m_deviceRecoveryPending = false;
	std::set<pdf::PageKey> m_loggedPlaceholders;
	std::set<std::size_t> m_uploadFailed;
	bool m_rendering = false;
	bool m_closed = false;
	bool m_linkPending = false;
	std::uint64_t m_linkVersion = 0;
#ifdef SABLE_SURFACE_TEST
	std::function<winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>(
	    winrt::hstring)>
	    m_attachmentPicker;
	std::string m_launchedUri;
	std::function<void(const std::string&)> m_copySink;
	std::function<pdf::PrintDialogResult(void*, int, int, const std::wstring&)>
	    m_printDialog;
	std::function<void(std::shared_ptr<printing::Controller>)> m_printUi;
#endif
	Device m_device;
	std::shared_ptr<pdf::Surface> m_surface;
	appearance::Subscription m_themeChanged;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::ViewChanged_revoker
	    m_viewChanged;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::ViewChanging_revoker
	    m_viewChanging;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::
	    DirectManipulationStarted_revoker m_manipulationStarted;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::
	    DirectManipulationCompleted_revoker m_manipulationCompleted;
	winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker
	    m_sizeChanged;
	winrt::Microsoft::UI::Xaml::FrameworkElement::LayoutUpdated_revoker
	    m_layoutUpdated;
	bool m_requestQueued = false;

	// the thumbnail panel: pdf.js's sidebar in its thumbnails view, from the
	// right rail. Thumbnails aren't filtered, like NightPDF's.
	std::optional<helpers::ThumbnailStrip> m_strip;
	// open asked for before the document was in (DisplayThumbs)
	bool m_thumbsWanted = false;
	// the thumbnails made for pages near the view, which of them show a
	// render at the current display scale, and the ones put aside
	std::map<std::size_t, winrt::Microsoft::UI::Xaml::Controls::Button>
	    m_thumbs;
	std::vector<winrt::Microsoft::UI::Xaml::Controls::Button> m_spareThumbs;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::ViewChanged_revoker
	    m_thumbScrolled;
	winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker
	    m_thumbResized;
	std::set<std::size_t> m_thumbLoaded;
	std::optional<std::size_t> m_hoverThumb;
	// the page the thumbnails last highlighted
	std::optional<std::size_t> m_thumbCurrent;
	// a scroll to the current thumbnail asked for before the strip had a
	// size
	bool m_thumbScrollPending = false;
	bool m_thumbScrollQueued = false;
	winrt::Microsoft::UI::Xaml::Media::SolidColorBrush m_thumbSelected{nullptr};
	winrt::Microsoft::UI::Xaml::Media::SolidColorBrush m_thumbHover{nullptr};
	winrt::Microsoft::UI::Xaml::Media::SolidColorBrush m_thumbPlain{nullptr};
};
} // namespace winrt::Sable::implementation

namespace winrt::Sable::factory_implementation {
struct ViewerPage : ViewerPageT<ViewerPage, implementation::ViewerPage> {};
} // namespace winrt::Sable::factory_implementation
