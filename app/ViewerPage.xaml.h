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

#include "ViewerPage.g.h"

#include <d2d1_1.h>

#include <cstddef>
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
#include "helpers/zoom.h"
#include "pdf/page_cache.h"
#include "pdf/text.h"
#include "pdf/search.h"

namespace pdf {
class Engine;
class PageFilter;
class Session;
class Surface;
struct RenderResult;
struct SessionInfo;
struct FindUpdate;
struct Link;
struct LinkDestination;
struct PrintDialogResult;
} // namespace pdf

namespace winrt::Sable::implementation {
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
	// changes only the GPU filter; page rasters stay in the cache
	void setValues(const helpers::SliderValues& values);
	// pdf.js's zoom: about 10% a step, or back to "auto"
	void zoomIn(int steps = 1);
	void zoomOut(int steps = 1);
	void zoomReset();
	void print(void* owner, std::function<void()> done);
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
	void onPreviewKeyDown(
	    const winrt::Windows::Foundation::IInspectable&,
	    const winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs& args);
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
#ifdef SABLE_SURFACE_TEST
	friend struct TabsTest;
	friend struct ScaleTest;
	friend struct ViewerErrorTest;
#endif
	struct Device {
		winrt::com_ptr<ID2D1Device> device;
		winrt::com_ptr<ID2D1DeviceContext> context;
		std::unique_ptr<pdf::PageFilter> filter;
		winrt::com_ptr<ID2D1SolidColorBrush> placeholder;
		winrt::com_ptr<ID2D1SolidColorBrush> selection;
		winrt::com_ptr<ID2D1SolidColorBrush> findMatch, findActive;
		std::map<std::size_t, winrt::com_ptr<ID2D1Bitmap1>> bitmaps;
	};

	void startOpen(const std::string& password);
	void setupSelection();
	void setupOutline();
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
	enum class Sidebar { Thumbnails, Outline, Attachments };
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
	winrt::fire_and_forget askPassword(bool incorrect);
	void opened(pdf::SessionInfo info);
	bool relayout(double zoom);
	void watchScale();
	void queueScale();
	bool setScale(double scale);
	bool setZoom(double zoom, winrt::Windows::Foundation::Point anchor);
	void zoomTo(double zoom, winrt::Windows::Foundation::Point anchor);
	void setPreset(helpers::ZoomPreset preset);
	void applyPreset();
	void settle();
	void goToPage(std::size_t page);
	void scrollToPage(std::size_t page);
	void requestLink(winrt::Windows::Foundation::Point point);
	void followLink(const pdf::LinkDestination& target);
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
	void startRender(const pdf::PageKey& key);
	double imageLeft();
	void onWheel(
	    const winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs& args);
	void rendered(pdf::RenderResult result);
	void showMessage(const winrt::hstring& text);
	void showNotice(const winrt::hstring& text);
	Device makeDevice();
	void rebuildDevice();
	void updateVisible();
	void requestNext();
	bool upload(Device& device, std::size_t page, const pdf::Bitmap& bitmap);
	void invalidate(std::size_t page);
	void draw(ID2D1DeviceContext& context, const RECT& rect);
	helpers::PixelRect pixels(std::size_t page) const;
	pdf::PageKey key(std::size_t page) const;

	std::unique_ptr<pdf::Session> m_session;
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
	helpers::WheelTicks m_wheelTicks;
	std::optional<int> m_startPage;
	// pdf.js's current page (0-based) and the labels it would have shown
	std::size_t m_current = 0;
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
	// renders that came back empty, so they aren't asked for again
	std::set<pdf::PageKey> m_failed;
	bool m_rendering = false;
	bool m_closed = false;
	bool m_linkPending = false;
#ifdef SABLE_SURFACE_TEST
	std::function<winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>(
	    winrt::hstring)>
	    m_attachmentPicker;
	std::string m_launchedUri;
	std::function<void(const std::string&)> m_copySink;
	std::function<pdf::PrintDialogResult(void*, int, int, const std::wstring&)>
	    m_printDialog;
#endif
	Device m_device;
	std::shared_ptr<pdf::Surface> m_surface;
	winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::ViewChanged_revoker
	    m_viewChanged;
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
