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
#include "pch.h"
#include "ViewerPage.xaml.h"
#if __has_include("ViewerPage.g.cpp")
#include "ViewerPage.g.cpp"
#endif

#include <d3d11.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "helpers/filter.h"
#include "pdf/page_filter.h"
#include "pdf/session.h"
#include "pdf/surface.h"
#include "pdf/surface_util.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

// Visibility is also a property of the page
using XVisibility = Microsoft::UI::Xaml::Visibility;

namespace {
// pdf.js gives every page a 9px transparent border and a "1px auto -8px"
// margin, so the pages sit 11px apart and 10px in from the top
// see:
// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/pdf_viewer.css
constexpr double PAGE_GAP = 11;
constexpr double PAGE_MARGIN = 10;
// pdf.js's dark --body-bg-color, around and between the pages
// see: https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/viewer.css
constexpr D2D1_COLOR_F BACKGROUND{42 / 255.0f, 42 / 255.0f, 46 / 255.0f, 1};
// unfiltered rasters kept for scrolling back. An A4 page at 100% is about
// 3.5 MB, so this is a few dozen pages.
constexpr std::size_t CACHE_BYTES = std::size_t{256} << 20;
// pdf.js's maxCanvasPixels, and the largest side Document::render takes.
// Bigger pages are rendered smaller and stretched.
constexpr long long MAX_PAGE_PIXELS = 1LL << 25;
constexpr int MAX_PAGE_SIDE = 16384;
// pdf.js's defaultZoomDelay
// see:
// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/app_options.js
constexpr std::chrono::milliseconds ZOOM_DELAY{400};
// how long a jump gets to land before the current page goes by where the
// view actually is
constexpr std::chrono::milliseconds LANDING_TIME{300};

bool sameSize(ID2D1Bitmap1& bitmap, const pdf::PageKey& key) {
	const auto size = bitmap.GetPixelSize();
	return static_cast<int>(size.width) == key.width &&
	       static_cast<int>(size.height) == key.height;
}
} // namespace

namespace winrt::Sable::implementation {
ViewerPage::ViewerPage() : m_cache(CACHE_BYTES) {}

// out of line, where Session and Surface are complete types
ViewerPage::~ViewerPage() = default;

void ViewerPage::open(pdf::Engine& engine, const std::filesystem::path& path,
                      std::optional<int> page) {
	if (m_closed || m_session) {
		return;
	}
	m_startPage = page;
	m_session = std::make_unique<pdf::Session>(engine);
	m_session->open(
	    path, {},
	    [weak = get_weak(), queue = DispatcherQueue()](pdf::SessionInfo info) {
		    auto result = std::make_shared<pdf::SessionInfo>(std::move(info));
		    queue.TryEnqueue([weak, result] {
			    if (auto self = weak.get()) {
				    self->opened(std::move(*result));
			    }
		    });
	    });
}

void ViewerPage::close() {
	m_closed = true;
	if (m_settle) {
		m_settle.Stop();
	}
	if (m_land) {
		m_land.Stop();
	}
	m_viewChanged.revoke();
	m_sizeChanged.revoke();
	m_layoutUpdated.revoke();
	m_rootChanged.revoke();
	m_loaded.revoke();
	m_session.reset();
	Pages().Source(nullptr);
	m_surface.reset();
	m_layout.reset();
	m_device = {};
	m_cache.clear();
}

// port of app/helpers/private.ts updateDarkSettings() (L91-116)
void ViewerPage::setValues(const helpers::SliderValues& values) {
	if (m_closed) {
		return;
	}
	m_values = values;
	if (!m_surface || !m_layout) {
		return;
	}
	try {
		m_device.filter->setValues(values);
		const auto blank = helpers::applyFilter(values, {255, 255, 255});
		m_device.placeholder->SetColor(
		    D2D1::ColorF(blank.r / 255.0f, blank.g / 255.0f, blank.b / 255.0f));
		// offscreen tiles also contain the old filter, so invalidate them too
		m_surface->invalidate({0, 0, static_cast<LONG>(pdf::MAX_SURFACE_SIDE),
		                       static_cast<LONG>(pdf::MAX_SURFACE_SIDE)});
	} catch (...) {
		// most likely a lost device; m_values is already kept, and the rebuild
		// that follows draws with it
	}
}

void ViewerPage::zoomIn(int steps) {
	if (m_layout) {
		zoomTo(helpers::zoomIn(m_zoom, steps), {0, 0});
	}
}

void ViewerPage::zoomOut(int steps) {
	if (m_layout) {
		zoomTo(helpers::zoomOut(m_zoom, steps), {0, 0});
	}
}

// pdf.js's zoomReset goes back to "auto", not to 100%
void ViewerPage::zoomReset() {
	setPreset(helpers::ZoomPreset::Auto);
}

// a number, as zoomIn and zoomOut set it, so the zoom no longer follows the
// window. pdf.js keeps it even when the scale doesn't change, but not when
// it couldn't be applied (see relayout).
void ViewerPage::zoomTo(double zoom, Windows::Foundation::Point anchor) {
	if (std::abs(zoom - m_zoom) < 1e-9 || setZoom(zoom, anchor)) {
		m_preset = helpers::ZoomPreset::None;
		showPosition();
	}
}

// pdf_viewer.js currentScaleValue = "auto", "page-fit" and so on
void ViewerPage::setPreset(helpers::ZoomPreset preset) {
	if (!m_layout) {
		return;
	}
	m_preset = preset;
	applyPreset();
	showPosition();
}

// the preset worked out again for pdf.js's current page
void ViewerPage::applyPreset() {
	if (!m_layout || m_preset == helpers::ZoomPreset::None ||
	    m_current >= m_sizes.size()) {
		return;
	}
	setZoom(helpers::presetZoom(m_preset, m_sizes[m_current],
	                            Scroller().ActualWidth(),
	                            Scroller().ActualHeight()),
	        {0, 0});
}

// Lays the pages out at zoom and scrolls so the spot that was under anchor
// (viewport DIPs) is under it again. pdf.js keeps the top left of the view
// for keys and the menu, and the pointer for the wheel.
bool ViewerPage::setZoom(double zoom, Windows::Foundation::Point anchor) {
	if (!m_layout || !m_surface || std::abs(zoom - m_zoom) < 1e-9) {
		return false;
	}
	try {
		const double x = Scroller().HorizontalOffset() + anchor.X - imageLeft();
		const double y = Scroller().VerticalOffset() + anchor.Y;
		const auto page = m_layout->pageAt(y);
		const auto before = m_layout->pages()[page];
		const double across = (x - before.x) / before.width;
		const double down = (y - before.y) / before.height;

		// the current page waits for the new offsets
		m_holdCurrent = true;
		if (!relayout(zoom)) {
			m_holdCurrent = false;
			return false;
		}
		// before the layout pass, so nothing it triggers starts a render
		settle();
		Scroller().UpdateLayout();
		m_holdCurrent = false;
		const auto& after = m_layout->pages()[page];
		jumpTo((std::max)(0.0, after.x + across * after.width + imageLeft() -
		                           anchor.X),
		       (std::max)(0.0, after.y + down * after.height - anchor.Y));
		return true;
	} catch (...) {
		m_holdCurrent = false;
		// relayout resizes the surface before it changes anything else, so
		// a failure there leaves the old zoom in place; not worth losing the
		// document over
		return false;
	}
}

// the pages at zoom, the surface and Image resized to match. False, and
// nothing changed, if that's more than one surface can hold; for now the
// zoom just stays, see "Still undecided" in DECISIONS.md.
bool ViewerPage::relayout(double zoom) {
	helpers::PageLayout next(m_sizes, zoom, 0.0, PAGE_GAP, PAGE_MARGIN);
	const auto size =
	    pdf::scaledSurfaceSize(next.width(), next.height(), m_scale);
	if (!size) {
		return false;
	}
	m_surface->resize(size->cx, size->cy);
	m_layout = std::move(next);
	m_zoom = zoom;
	Pages().Width(size->cx / m_scale);
	Pages().Height(size->cy / m_scale);
	showPosition();
	return true;
}

// a root change also covers moving to another monitor
// see:
// https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.xamlroot.changed
void ViewerPage::watchScale() {
	if (m_closed) {
		return;
	}
	if (auto root = XamlRoot()) {
		m_rootChanged =
		    root.Changed(auto_revoke, [weak = get_weak()](auto&&, auto&&) {
			    if (auto self = weak.get()) {
				    self->queueScale();
			    }
		    });
		queueScale();
	}
}

void ViewerPage::queueScale() {
	if (m_closed || m_scaleQueued || !XamlRoot() ||
	    XamlRoot().RasterizationScale() == m_scale) {
		return;
	}
	m_scaleQueued = DispatcherQueue().TryEnqueue([weak = get_weak()] {
		if (auto self = weak.get()) {
			self->m_scaleQueued = false;
			if (!self->m_closed && self->XamlRoot()) {
				self->setScale(self->XamlRoot().RasterizationScale());
			}
		}
	});
}

bool ViewerPage::setScale(double scale) {
	if (m_closed || !m_layout || !m_surface || scale == m_scale) {
		return false;
	}
	const auto size =
	    pdf::scaledSurfaceSize(m_layout->width(), m_layout->height(), scale);
	if (!size) {
		// keep the readable surface until larger documents are supported
		return false;
	}
	try {
		const auto x = Scroller().HorizontalOffset();
		const auto y = Scroller().VerticalOffset();
		m_surface->resize(size->cx, size->cy);
		m_scale = scale;
		settle();
		m_holdCurrent = true;
		Pages().Width(size->cx / scale);
		Pages().Height(size->cy / scale);
		Scroller().UpdateLayout();
		m_holdCurrent = false;
		jumpTo(x, y);
		updateVisible();
		return true;
	} catch (...) {
		m_holdCurrent = false;
		return false;
	}
}

// the Image is centred in the scroller while it's narrower than it
double ViewerPage::imageLeft() {
	return (std::max)(0.0, (Scroller().ViewportWidth() - Pages().Width()) / 2);
}

// the old rasters stay up, stretched, until the zoom has been still for
// pdf.js's 400 ms; only then are sharp pages rendered
void ViewerPage::settle() {
	m_settling = true;
	if (m_session) {
		m_session->cancelRender();
	}
	if (!m_settle) {
		m_settle = DispatcherQueue().CreateTimer();
		m_settle.Interval(ZOOM_DELAY);
		m_settle.IsRepeating(false);
		m_settle.Tick([weak = get_weak()](auto&&, auto&&) {
			if (auto self = weak.get()) {
				self->m_settling = false;
				self->updateVisible();
			}
		});
	}
	m_settle.Stop();
	m_settle.Start();
}

// pdf_viewer.js currentPageNumber: the page becomes the current one, then it
// is scrolled into view, even when it already was the current one
void ViewerPage::goToPage(std::size_t page) {
	if (!m_layout || page >= m_layout->pages().size()) {
		return;
	}
	m_current = page;
	showPosition();
	scrollToPage(page);
}

// pdf_viewer.js #scrollIntoView and ui_utils.js scrollIntoView: the top of
// the page (inside pdf.js's border) at the top of the view, and its left
// edge at the left only when it doesn't already fit across
void ViewerPage::scrollToPage(std::size_t page) {
	const auto& rect = m_layout->pages()[page];
	const double left = imageLeft() + rect.x;
	const double scrollLeft = Scroller().HorizontalOffset();
	std::optional<double> across;
	if (left < scrollLeft ||
	    left + rect.width > scrollLeft + Scroller().ViewportWidth()) {
		across = left;
	}
	jumpTo(across, rect.y);
}

// ChangeView, with the current page worked out for where the view is going
// until it gets there
void ViewerPage::jumpTo(std::optional<double> x, double y) {
	auto scroller = Scroller();
	const double down =
	    std::clamp(y, 0.0, (std::max)(0.0, scroller.ScrollableHeight()));
	const double across =
	    x ? std::clamp(*x, 0.0, (std::max)(0.0, scroller.ScrollableWidth()))
	      : scroller.HorizontalOffset();
	if (std::abs(down - scroller.VerticalOffset()) >= 0.5 ||
	    std::abs(across - scroller.HorizontalOffset()) >= 0.5) {
		m_landingAt = std::pair{across, down};
		if (!m_land) {
			m_land = DispatcherQueue().CreateTimer();
			m_land.Interval(LANDING_TIME);
			m_land.IsRepeating(false);
			m_land.Tick([weak = get_weak()](auto&&, auto&&) {
				if (auto self = weak.get()) {
					self->m_landingAt.reset();
					self->updateCurrentPage();
				}
			});
		}
		m_land.Stop();
		m_land.Start();
	} else {
		m_landingAt.reset();
	}
	if (!scroller.ChangeView(x ? Windows::Foundation::IReference<double>(*x)
	                           : nullptr,
	                         y, nullptr, true)) {
		m_landingAt.reset();
	}
}

// pdf_viewer.js update(), run whenever the view moves
void ViewerPage::updateCurrentPage() {
	if (!m_layout || m_startPage || m_holdCurrent) {
		return;
	}
	double left = Scroller().HorizontalOffset();
	double top = Scroller().VerticalOffset();
	if (m_landingAt) {
		if (std::abs(left - m_landingAt->first) < 1 &&
		    std::abs(top - m_landingAt->second) < 1) {
			m_landingAt.reset();
			m_land.Stop();
		} else {
			left = m_landingAt->first;
			top = m_landingAt->second;
		}
	}
	const helpers::Viewport view{left - imageLeft(), top,
	                             Scroller().ViewportWidth(),
	                             Scroller().ViewportHeight()};
	const auto page = helpers::currentPage(*m_layout, view, m_current);
	if (page != m_current) {
		m_current = page;
		showPosition();
	}
}

// toolbar.js #updateUIState: the page box, "of N", and which buttons work
void ViewerPage::showPosition() {
	if (!m_layout) {
		return;
	}
	const auto count = m_layout->pages().size();
	const auto text = helpers::pageText(m_current, count, m_labels);
	m_pageBoxText = to_hstring(text.box);
	PageBox().Text(m_pageBoxText);
	PageTotal().Text(to_hstring(text.total));
	PreviousButton().IsEnabled(m_current > 0);
	NextButton().IsEnabled(m_current + 1 < count);
	ZoomOutButton().IsEnabled(m_zoom > helpers::MIN_ZOOM);
	ZoomInButton().IsEnabled(m_zoom < helpers::MAX_ZOOM);
	// the rail is too narrow for pdf.js's "Automatic Zoom", so the button
	// always shows the percentage and the menu ticks the preset
	ZoomText().Text(to_hstring(helpers::zoomPercent(m_zoom)));
}

// app.js webViewerPageNumberChanged, for an edited page box: go there if
// it names a page, and show the current page in the box either way
void ViewerPage::commitPageBox() {
	if (!m_layout) {
		return;
	}
	const auto text = PageBox().Text();
	// an input's change event only fires for an edit
	if (text == m_pageBoxText) {
		return;
	}
	if (!text.empty()) {
		if (const auto page = helpers::pageFromText(to_string(text), m_labels,
		                                            m_layout->pages().size())) {
			goToPage(*page);
		}
	}
	showPosition();
}

// pdf.js's buttons took the focus, so the box's change event went first.
// These don't (the keys stay on the pages), so do it for them.
void ViewerPage::leavePageBox() {
	if (PageBox().FocusState() != FocusState::Unfocused) {
		commitPageBox();
		Scroller().Focus(FocusState::Programmatic);
	}
}

void ViewerPage::onPreviewKeyDown(const Windows::Foundation::IInspectable&,
                                  const Input::KeyRoutedEventArgs& args) {
	using helpers::NavAction;
	using helpers::NavFocus;
	using helpers::NavKey;
	using Windows::System::VirtualKey;
	// a menu opened from the rail keeps its own keys
	if (!m_layout || args.Handled() ||
	    args.OriginalSource().try_as<MenuFlyoutItemBase>()) {
		return;
	}
	NavKey pressed = NavKey::Other;
	switch (args.Key()) {
	case VirtualKey::Up:
		pressed = NavKey::Up;
		break;
	case VirtualKey::Down:
		pressed = NavKey::Down;
		break;
	case VirtualKey::Left:
		pressed = NavKey::Left;
		break;
	case VirtualKey::Right:
		pressed = NavKey::Right;
		break;
	case VirtualKey::PageUp:
		pressed = NavKey::PageUp;
		break;
	case VirtualKey::PageDown:
		pressed = NavKey::PageDown;
		break;
	case VirtualKey::Home:
		pressed = NavKey::Home;
		break;
	case VirtualKey::End:
		pressed = NavKey::End;
		break;
	case VirtualKey::Space:
		pressed = NavKey::Space;
		break;
	case VirtualKey::Enter:
		pressed = NavKey::Enter;
		break;
	case VirtualKey::Back:
		pressed = NavKey::Backspace;
		break;
	case VirtualKey::G:
		pressed = NavKey::G;
		break;
	case VirtualKey::J:
		pressed = NavKey::J;
		break;
	case VirtualKey::K:
		pressed = NavKey::K;
		break;
	case VirtualKey::N:
		pressed = NavKey::N;
		break;
	case VirtualKey::P:
		pressed = NavKey::P;
		break;
	default:
		return;
	}
	const auto held = [](int virtualKey) {
		return (GetKeyState(virtualKey) & 0x8000) != 0;
	};
	const helpers::NavModifiers modifiers{held(VK_CONTROL), held(VK_MENU),
	                                      held(VK_SHIFT)};
	const auto source = args.OriginalSource();
	auto focus = NavFocus::Viewer;
	if (source.try_as<TextBox>()) {
		focus = NavFocus::TextBox;
	} else if (source.try_as<Primitives::ButtonBase>()) {
		focus = NavFocus::Button;
	}
	const auto count = m_layout->pages().size();
	// in "page-fit" the scrolling keys turn pages, otherwise only when
	// there's nothing to scroll that way
	const helpers::NavState state{
	    m_current, count,
	    Scroller().ComputedVerticalScrollBarVisibility() ==
	        XVisibility::Visible,
	    Scroller().ComputedHorizontalScrollBarVisibility() ==
	        XVisibility::Visible,
	    m_preset == helpers::ZoomPreset::PageFit};
	const auto action = helpers::navAction(pressed, modifiers, focus, state);
	switch (action) {
	case NavAction::None:
		break;
	case NavAction::Previous:
		if (m_current > 0) {
			goToPage(m_current - 1);
		}
		break;
	case NavAction::Next:
		if (m_current + 1 < count) {
			goToPage(m_current + 1);
		}
		break;
	case NavAction::First:
		goToPage(0);
		break;
	case NavAction::Last:
		goToPage(count - 1);
		break;
	case NavAction::FocusPageBox:
		PageBox().Focus(FocusState::Keyboard);
		PageBox().SelectAll();
		break;
	}
	if (action != NavAction::None) {
		args.Handled(true);
	}
	// what the browser did with the keys pdf.js left alone. Space scrolled
	// a page, which WinUI's ScrollViewer doesn't; a page is the viewport,
	// as for its PageDown.
	// see:
	// https://raw.githubusercontent.com/microsoft/microsoft-ui-xaml/fe9d01273af3382ea570e203ab700772528d98df/dxaml/xcp/dxaml/lib/ScrollContentPresenter_Partial.cpp
	if (action == NavAction::None && pressed == NavKey::Space &&
	    focus == NavFocus::Viewer && !modifiers.ctrl && !modifiers.alt) {
		const double page = Scroller().ViewportHeight();
		Scroller().ChangeView(nullptr,
		                      Scroller().VerticalOffset() +
		                          (modifiers.shift ? -page : page),
		                      nullptr, false);
		args.Handled(true);
	}
	// app.js: the navigation keys bring the focus back to the pages
	// (ensureViewerFocused), except from a text field
	bool focusPages = action == NavAction::First || action == NavAction::Last;
	if (action == NavAction::None && focus != NavFocus::TextBox) {
		switch (pressed) {
		case NavKey::PageUp:
		case NavKey::PageDown:
		case NavKey::Home:
		case NavKey::End:
		case NavKey::Up:
		case NavKey::Down:
		case NavKey::Left:
		case NavKey::Right:
			focusPages = true;
			break;
		case NavKey::Space:
			focusPages = focus != NavFocus::Button;
			break;
		default:
			break;
		}
	}
	if (focusPages && focus != NavFocus::Viewer) {
		Scroller().Focus(FocusState::Programmatic);
	}
}

void ViewerPage::onPageBoxKeyDown(const Windows::Foundation::IInspectable&,
                                  const Input::KeyRoutedEventArgs& args) {
	if (args.Key() == Windows::System::VirtualKey::Enter) {
		commitPageBox();
		args.Handled(true);
	}
}

// tabbing into a text field selects it in a browser, and Ctrl+Alt+G does
void ViewerPage::onPageBoxGotFocus(const Windows::Foundation::IInspectable&,
                                   const RoutedEventArgs&) {
	PageBox().SelectAll();
}

// LosingFocus rather than LostFocus, which comes later: by then a click on
// the rail could have put the current page back in the box
void ViewerPage::onPageBoxLosingFocus(const Windows::Foundation::IInspectable&,
                                      const Input::LosingFocusEventArgs&) {
	commitPageBox();
}

// the rail's buttons don't take the focus from the pages when clicked; if
// one had it from the keyboard and just got disabled, it goes to the pages
void ViewerPage::onPreviousPage(const Windows::Foundation::IInspectable&,
                                const RoutedEventArgs&) {
	leavePageBox();
	if (m_current > 0) {
		goToPage(m_current - 1);
	}
	if (!PreviousButton().IsEnabled()) {
		Scroller().Focus(FocusState::Programmatic);
	}
}

void ViewerPage::onNextPage(const Windows::Foundation::IInspectable&,
                            const RoutedEventArgs&) {
	leavePageBox();
	if (m_layout && m_current + 1 < m_layout->pages().size()) {
		goToPage(m_current + 1);
	}
	if (!NextButton().IsEnabled()) {
		Scroller().Focus(FocusState::Programmatic);
	}
}

void ViewerPage::onZoomIn(const Windows::Foundation::IInspectable&,
                          const RoutedEventArgs&) {
	leavePageBox();
	zoomIn();
	if (!ZoomInButton().IsEnabled()) {
		Scroller().Focus(FocusState::Programmatic);
	}
}

void ViewerPage::onZoomOut(const Windows::Foundation::IInspectable&,
                           const RoutedEventArgs&) {
	leavePageBox();
	zoomOut();
	if (!ZoomOutButton().IsEnabled()) {
		Scroller().Focus(FocusState::Programmatic);
	}
}

// toolbar.js #updateUIState: the entry for the preset, or for the number if
// it's one of the menu's, is the selected one; anything else is "custom"
// and nothing is ticked (the button shows the percentage either way)
void ViewerPage::onZoomMenuOpening(const Windows::Foundation::IInspectable&,
                                   const Windows::Foundation::IInspectable&) {
	for (const auto& entry : ZoomMenu().Items()) {
		const auto item = entry.try_as<RadioMenuFlyoutItem>();
		if (!item) {
			continue;
		}
		const auto value = helpers::zoomValue(
		    to_string(unbox_value_or<hstring>(item.Tag(), L"")));
		item.IsChecked(value &&
		               helpers::zoomChoiceSelected(*value, m_preset, m_zoom));
	}
}

// app.js webViewerScaleChanged: currentScaleValue = the entry's value
void ViewerPage::onZoomChoice(const Windows::Foundation::IInspectable& sender,
                              const RoutedEventArgs&) {
	const auto item = sender.try_as<MenuFlyoutItem>();
	if (!item || !m_layout) {
		return;
	}
	const auto value =
	    helpers::zoomValue(to_string(unbox_value_or<hstring>(item.Tag(), L"")));
	if (!value) {
		return;
	}
	if (value->preset == helpers::ZoomPreset::None) {
		zoomTo(value->zoom, {0, 0});
	} else {
		setPreset(value->preset);
	}
}

// Ctrl+wheel, around the pointer (app.js webViewerWheel)
void ViewerPage::onWheel(const Input::PointerRoutedEventArgs& args) {
	using Windows::System::VirtualKeyModifiers;
	if (!m_layout || (args.KeyModifiers() & VirtualKeyModifiers::Control) !=
	                     VirtualKeyModifiers::Control) {
		return;
	}
	const auto point = args.GetCurrentPoint(Scroller());
	if (point.Properties().IsHorizontalMouseWheel()) {
		return;
	}
	args.Handled(true);
	UINT lines = 3;
	SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
	const int ticks = m_wheelTicks.add(helpers::wheelTicks(
	    point.Properties().MouseWheelDelta(),
	    lines == WHEEL_PAGESCROLL ? 0 : static_cast<int>(lines)));
	if (ticks != 0) {
		zoomTo(ticks > 0 ? helpers::zoomIn(m_zoom, ticks)
		                 : helpers::zoomOut(m_zoom, -ticks),
		       point.Position());
	}
}

void ViewerPage::opened(pdf::SessionInfo info) {
	if (m_closed || info.canceled) {
		return;
	}
	if (info.error != pdf::Error::None) {
		showMessage(to_hstring(pdf::errorMessage(info.error)));
		return;
	}
	if (info.pages.empty()) {
		showMessage(L"This PDF has no pages.");
		return;
	}
	try {
		std::vector<helpers::PageSize> sizes;
		sizes.reserve(info.pages.size());
		for (const auto& size : info.pages) {
			sizes.push_back({size.width, size.height});
		}
		m_sizes = std::move(sizes);
		m_labels = helpers::pageLabels(std::move(info.labels), m_sizes.size());
		m_scale = XamlRoot() ? XamlRoot().RasterizationScale() : 1.0;
		// the page -p asks for, or the first; pdf.js ignores a page that
		// isn't in the document
		m_current =
		    m_startPage && *m_startPage >= 1 &&
		            static_cast<std::size_t>(*m_startPage) <= m_sizes.size()
		        ? static_cast<std::size_t>(*m_startPage - 1)
		        : 0;
		if (m_preset != helpers::ZoomPreset::None) {
			m_zoom = helpers::presetZoom(m_preset, m_sizes[m_current],
			                             Scroller().ActualWidth(),
			                             Scroller().ActualHeight());
		}
		m_layout.emplace(m_sizes, m_zoom, 0.0, PAGE_GAP, PAGE_MARGIN);
		m_fitPending = helpers::followsViewport(m_preset);
		const auto size = pdf::scaledSurfaceSize(m_layout->width(),
		                                         m_layout->height(), m_scale);
		if (!size) {
			m_layout.reset();
			showMessage(L"This PDF is too long to show in one piece yet.");
			return;
		}
		m_device = makeDevice();
		m_surface = pdf::Surface::create(
		    *m_device.device, size->cx, size->cy,
		    [weak = get_weak()](ID2D1DeviceContext& context, const RECT& rect) {
			    if (auto self = weak.get()) {
				    self->draw(context, rect);
			    }
		    },
		    [weak = get_weak()] {
			    if (auto self = weak.get()) {
				    self->rebuildDevice();
			    }
		    });
		Pages().Source(m_surface->source());
		Pages().Width(size->cx / m_scale);
		Pages().Height(size->cy / m_scale);
		m_loaded = Loaded(auto_revoke, [weak = get_weak()](auto&&, auto&&) {
			if (auto self = weak.get()) {
				self->watchScale();
			}
		});
		watchScale();
		// the "auto" fit that's still to come takes the rail into account
		Rail().Visibility(XVisibility::Visible);
		showPosition();
		m_viewChanged = Scroller().ViewChanged(
		    auto_revoke, [this](auto&&, auto&&) { updateVisible(); });
		// app.js webViewerResize: "auto", "page-fit" and "page-width" are
		// worked out again for the new size. The scroller's size, so the rail
		// and later the side panels count.
		m_sizeChanged =
		    Scroller().SizeChanged(auto_revoke, [this](auto&&, auto&&) {
			    if (helpers::followsViewport(m_preset)) {
				    // after this layout pass, not inside it
				    DispatcherQueue().TryEnqueue([weak = get_weak()] {
					    if (auto self = weak.get();
					        self && helpers::followsViewport(self->m_preset)) {
						    self->applyPreset();
					    }
				    });
			    }
			    updateVisible();
		    });
		// handled events too: the ScrollViewer sees the wheel first
		Scroller().AddHandler(
		    UIElement::PointerWheelChangedEvent(),
		    box_value(Input::PointerEventHandler(
		        [weak = get_weak()](auto&&, const auto& args) {
			        if (auto self = weak.get()) {
				        self->onWheel(args);
			        }
		        })),
		    true);
		// toolbar.js selects the page box's text on a click, after the
		// TextBox has put the caret where it was clicked
		PageBox().AddHandler(UIElement::PointerReleasedEvent(),
		                     box_value(Input::PointerEventHandler(
		                         [weak = get_weak()](auto&&, auto&&) {
			                         if (auto self = weak.get()) {
				                         self->PageBox().SelectAll();
			                         }
		                         })),
		                     true);
		// opening can finish before the window's first layout. Wait for the
		// full extent before applying -p, rather than clamping it to zero.
		m_layoutUpdated = LayoutUpdated(
		    auto_revoke, [this](auto&&, auto&&) { updateVisible(); });
		// app.js: the keys work as soon as the document is in
		Scroller().Focus(FocusState::Programmatic);
		updateVisible();
	} catch (...) {
		m_layout.reset();
		m_surface.reset();
		showMessage(L"Couldn't show this PDF: " + to_message());
	}
}

void ViewerPage::showMessage(const hstring& text) {
	Message().Text(text);
	Message().Visibility(XVisibility::Visible);
	Scroller().Visibility(XVisibility::Collapsed);
	Rail().Visibility(XVisibility::Collapsed);
}

ViewerPage::Device ViewerPage::makeDevice() {
	com_ptr<ID3D11Device> d3d;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
	                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
	                             D3D11_SDK_VERSION, d3d.put(), nullptr,
	                             nullptr))) {
		// no usable GPU: the software rasterizer still shows the pages
		check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		                                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		                                nullptr, 0, D3D11_SDK_VERSION,
		                                d3d.put(), nullptr, nullptr));
	}
	com_ptr<ID2D1Factory1> factory;
	check_hresult(
	    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()));
	Device device;
	check_hresult(factory->CreateDevice(d3d.as<IDXGIDevice>().get(),
	                                    device.device.put()));
	check_hresult(device.device->CreateDeviceContext(
	    D2D1_DEVICE_CONTEXT_OPTIONS_NONE, device.context.put()));
	device.filter = std::make_unique<pdf::PageFilter>(*device.context);
	device.filter->setValues(m_values);
	// what pdf.js shows while a page loads: a white page, filtered
	const auto blank = helpers::applyFilter(m_values, {255, 255, 255});
	check_hresult(device.context->CreateSolidColorBrush(
	    D2D1::ColorF(blank.r / 255.0f, blank.g / 255.0f, blank.b / 255.0f),
	    device.placeholder.put()));
	return device;
}

// the surface lost its device: new devices, new GPU copies of the pages near
// the viewport, and only then hand the device over
void ViewerPage::rebuildDevice() {
	if (!m_surface) {
		return;
	}
	try {
		auto device = makeDevice();
		for (auto page = m_wanted.first; page < m_wanted.end; ++page) {
			if (const auto* bitmap = m_cache.find(key(page))) {
				upload(device, page, *bitmap);
			}
		}
		// kept first: if SetDevice fails the surface reports the loss again
		// and this runs again, instead of drawing old bitmaps on a new device
		m_device = std::move(device);
		m_surface->setDevice(*m_device.device);
		requestNext();
	} catch (...) {
		showMessage(L"The graphics device stopped working: " + to_message());
	}
}

void ViewerPage::updateVisible() {
	if (!m_layout || !m_surface) {
		return;
	}
	if (Scroller().ViewportHeight() > 0 &&
	    Scroller().ExtentHeight() >= Pages().Height() - 1) {
		if (std::exchange(m_fitPending, false) &&
		    helpers::followsViewport(m_preset)) {
			// for the page -p asks for, or the first
			const double zoom = helpers::presetZoom(
			    m_preset, m_sizes[m_current], Scroller().ActualWidth(),
			    Scroller().ActualHeight());
			try {
				// -p waits for the new extent; asked for again in case the
				// size didn't change enough to start another layout pass
				if (std::abs(zoom - m_zoom) > 1e-9 && relayout(zoom)) {
					DispatcherQueue().TryEnqueue([weak = get_weak()] {
						if (auto self = weak.get()) {
							self->updateVisible();
						}
					});
					return;
				}
			} catch (...) {
				showMessage(L"Couldn't show this PDF: " + to_message());
				return;
			}
		}
		m_layoutUpdated.revoke();
		// pdf_link_service.js setHash: NightPDF passed -p as #page=, which
		// goes to that page like the page box does, page 1 included
		const auto start = std::exchange(m_startPage, std::nullopt);
		if (start && *start >= 1 &&
		    static_cast<std::size_t>(*start) <= m_layout->pages().size()) {
			goToPage(static_cast<std::size_t>(*start - 1));
		}
	}
	updateCurrentPage();
	const double top = Scroller().VerticalOffset();
	double height = Scroller().ViewportHeight();
	if (height <= 0) {
		height = ActualHeight();
	}
	m_visible = m_layout->visiblePages(top, height, 0);
	m_wanted = m_layout->visiblePages(top, height, 1);
	// GPU copies only for the pages near the viewport; the rest stay cached
	std::erase_if(m_device.bitmaps, [this](const auto& entry) {
		return entry.first < m_wanted.first || entry.first >= m_wanted.end;
	});
	// this runs from XAML events, where an exception would end the app
	try {
		requestNext();
	} catch (...) {
		showMessage(L"Couldn't show this PDF: " + to_message());
	}
}

void ViewerPage::requestNext() {
	if (!m_session || !m_layout) {
		return;
	}
	for (const auto page : helpers::renderOrder(m_visible, m_wanted)) {
		const auto wanted = key(page);
		// one from before a zoom is only stretched, it still needs a render
		if (const auto found = m_device.bitmaps.find(page);
		    found != m_device.bitmaps.end() &&
		    sameSize(*found->second, wanted)) {
			continue;
		}
		if (const auto* bitmap = m_cache.find(wanted)) {
			if (upload(m_device, page, *bitmap)) {
				invalidate(page);
			}
			continue;
		}
		if (m_settling || m_rendering || m_failed.contains(wanted)) {
			continue;
		}
		m_rendering = m_session->render(
		    wanted, [weak = get_weak(),
		             queue = DispatcherQueue()](pdf::RenderResult result) {
			    auto rendered =
			        std::make_shared<pdf::RenderResult>(std::move(result));
			    queue.TryEnqueue([weak, rendered] {
				    if (auto self = weak.get()) {
					    self->rendered(std::move(*rendered));
				    }
			    });
		    });
	}
}

void ViewerPage::rendered(pdf::RenderResult result) {
	if (m_closed) {
		return;
	}
	m_rendering = false;
	try {
		if (m_layout && !result.canceled && result.key.page >= 0) {
			const auto page = static_cast<std::size_t>(result.key.page);
			// only if it's still the size the page is drawn at
			if (page < m_layout->pages().size() && result.key == key(page)) {
				if (!result.bitmap) {
					m_failed.insert(result.key);
				} else {
					const bool shown = page >= m_wanted.first &&
					                   page < m_wanted.end &&
					                   upload(m_device, page, *result.bitmap);
					if (shown) {
						invalidate(page);
					}
					// kept nowhere means rendering it again would go the
					// same way, round and round
					if (!m_cache.put(result.key, std::move(*result.bitmap)) &&
					    !shown) {
						m_failed.insert(result.key);
					}
				}
			}
		}
		requestNext();
	} catch (...) {
		showMessage(L"Couldn't show this PDF: " + to_message());
	}
}

bool ViewerPage::upload(Device& device, std::size_t page,
                        const pdf::Bitmap& bitmap) {
	const auto props = D2D1::BitmapProperties1(
	    D2D1_BITMAP_OPTIONS_NONE,
	    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
	                      D2D1_ALPHA_MODE_PREMULTIPLIED));
	com_ptr<ID2D1Bitmap1> copy;
	// a lost device shows up here too; the surface notices it on its next
	// draw, until then the page stays a placeholder
	if (FAILED(device.context->CreateBitmap(
	        D2D1::SizeU(static_cast<UINT32>(bitmap.width),
	                    static_cast<UINT32>(bitmap.height)),
	        bitmap.pixels.data(), static_cast<UINT32>(bitmap.stride), props,
	        copy.put()))) {
		return false;
	}
	device.bitmaps[page] = std::move(copy);
	return true;
}

void ViewerPage::invalidate(std::size_t page) {
	const auto rect = pixels(page);
	m_surface->invalidate(
	    {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height});
}

// runs inside the surface's update callback, in pixels
void ViewerPage::draw(ID2D1DeviceContext& context, const RECT& rect) {
	context.Clear(BACKGROUND);
	if (!m_layout) {
		return;
	}
	const auto pages = m_layout->visiblePages(
	    rect.top / m_scale, (rect.bottom - rect.top) / m_scale, 1);
	for (auto page = pages.first; page < pages.end; ++page) {
		const auto at = pixels(page);
		const auto found = m_device.bitmaps.find(page);
		if (found != m_device.bitmaps.end()) {
			const auto size = found->second->GetPixelSize();
			m_device.filter->setInput(found->second.get());
			if (static_cast<int>(size.width) == at.width &&
			    static_cast<int>(size.height) == at.height) {
				context.DrawImage(m_device.filter->output(),
				                  D2D1::Point2F(static_cast<float>(at.x),
				                                static_cast<float>(at.y)));
			} else {
				// rendered before a zoom, or capped: stretched to fit
				D2D1::Matrix3x2F base;
				context.GetTransform(&base);
				context.SetTransform(
				    D2D1::Matrix3x2F::Scale(
				        static_cast<float>(at.width) / size.width,
				        static_cast<float>(at.height) / size.height) *
				    D2D1::Matrix3x2F::Translation(static_cast<float>(at.x),
				                                  static_cast<float>(at.y)) *
				    base);
				context.DrawImage(m_device.filter->output());
				context.SetTransform(base);
			}
			continue;
		}
		context.FillRectangle(D2D1::RectF(static_cast<float>(at.x),
		                                  static_cast<float>(at.y),
		                                  static_cast<float>(at.x + at.width),
		                                  static_cast<float>(at.y + at.height)),
		                      m_device.placeholder.get());
	}
	// the effect mustn't keep the last page alive after the GPU cache drops it
	m_device.filter->setInput(nullptr);
}

helpers::PixelRect ViewerPage::pixels(std::size_t page) const {
	return helpers::toPixels(m_layout->pages()[page], m_scale);
}

pdf::PageKey ViewerPage::key(std::size_t page) const {
	const auto rect = pixels(page);
	const auto size = helpers::renderSize(rect.width, rect.height,
	                                      MAX_PAGE_PIXELS, MAX_PAGE_SIDE);
	return {static_cast<int>(page), size.width, size.height, 0};
}
} // namespace winrt::Sable::implementation
