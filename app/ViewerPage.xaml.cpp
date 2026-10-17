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
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <utility>

#include "helpers/filter.h"
#include "helpers/logging.h"
#include "pdf/page_filter.h"
#include "pdf/session.h"
#include "pdf/printing.h"
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
                      std::optional<int> page, bool thumbnails) {
	if (m_closed || m_session) {
		return;
	}
	m_engine = &engine;
	m_path = path;
	m_startPage = page;
	setThumbnailsOpen(thumbnails);
	startOpen({});
}

// a session opens once, so every password tried gets a new one
void ViewerPage::startOpen(const std::string& password) {
	try {
		m_session = std::make_unique<pdf::Session>(*m_engine);
		const bool queued = m_session->open(
		    m_path, password,
		    [weak = get_weak(),
		     queue = DispatcherQueue()](pdf::SessionInfo info) {
			    auto result =
			        std::make_shared<pdf::SessionInfo>(std::move(info));
			    queue.TryEnqueue([weak, result] {
				    if (auto self = weak.get()) {
					    self->opened(std::move(*result));
				    }
			    });
		    });
		if (!queued) {
			showMessage(to_hstring(pdf::errorMessage(pdf::Error::Unknown)));
		}
	} catch (...) {
		showMessage(L"Couldn't open this PDF: " + to_message());
	}
}

void ViewerPage::activate() {
	if (m_closed || Visibility() != XVisibility::Visible) {
		return;
	}
	if (const auto pending = std::exchange(m_passwordPending, std::nullopt)) {
		askPassword(*pending);
	} else if (m_layout && !m_passwordDialog) {
		showOutline();
		showAttachments();
		if (findOpen()) {
			FindBox().Focus(FocusState::Programmatic);
			revealFindMatch();
		} else {
			Scroller().Focus(FocusState::Programmatic);
		}
	}
}

// pdf.js's PasswordPrompt (password_prompt.js): the label, a masked box,
// Cancel and OK. OK does nothing until something's typed, Enter is OK and
// Esc is Cancel. The password goes to the open and isn't kept.
fire_and_forget ViewerPage::askPassword(bool incorrect) {
	weak_ref<ViewerPage> weak;
	PasswordBox box{nullptr};
	const auto report = [&weak, &box](const wchar_t* message) noexcept {
		OutputDebugStringW(L"PDF password prompt: ");
		OutputDebugStringW(message);
		OutputDebugStringW(L"\n");
		try {
			if (box) {
				box.Password(L"");
			}
		} catch (...) {
			OutputDebugStringW(L"Couldn't clear the password box.\n");
		}
		try {
			if (auto self = weak.get()) {
				self->m_passwordPending.reset();
				if (auto dialog =
				        std::exchange(self->m_passwordDialog, nullptr)) {
					try {
						dialog.Hide();
					} catch (...) {
						OutputDebugStringW(
						    L"Couldn't hide the password dialog.\n");
					}
				}
				if (!self->m_closed) {
					self->showMessage(message);
				}
			}
		} catch (...) {
			OutputDebugStringW(L"Couldn't show the password prompt error.\n");
		}
	};
	try {
		weak = get_weak();
		if (m_closed || m_passwordDialog) {
			co_return;
		}
		if (Visibility() != XVisibility::Visible) {
			m_passwordPending = incorrect;
			co_return;
		}
		if (!XamlRoot()) {
			// opening can beat the window's first layout, and a dialog needs a
			// root to show in
			m_passwordPending = incorrect;
			m_promptLoaded = Loaded(auto_revoke, [weak](auto&&, auto&&) {
				if (auto self = weak.get()) {
					self->m_promptLoaded.revoke();
					if (const auto pending = std::exchange(
					        self->m_passwordPending, std::nullopt)) {
						self->askPassword(*pending);
					}
				}
			});
			co_return;
		}
		// pdfjs-password-label and pdfjs-password-invalid
		TextBlock label;
		label.Text(incorrect ? L"Invalid password. Please try again."
		                     : L"Enter the password to open this PDF file.");
		label.TextWrapping(TextWrapping::Wrap);
		box = PasswordBox();
		Automation::AutomationProperties::SetName(box, label.Text());
		StackPanel content;
		content.Spacing(12);
		content.Children().Append(label);
		content.Children().Append(box);
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Content(content);
		dialog.PrimaryButtonText(L"OK");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::Primary);
		// #verify: an empty password isn't tried
		dialog.PrimaryButtonClick(
		    [box](auto&&, const ContentDialogButtonClickEventArgs& args) {
			    if (box.Password().empty()) {
				    args.Cancel(true);
			    }
		    });
		dialog.Opened(
		    [box](auto&&, auto&&) { box.Focus(FocusState::Programmatic); });
		m_passwordDialog = dialog;
		const auto result = co_await dialog.ShowAsync();
		// don't keep the page alive while the dialog waits
		auto self = weak.get();
		if (box) {
			const auto password = box.Password();
			box.Password(L"");
			if (!self || self->m_closed) {
				co_return;
			}
			self->m_passwordDialog = nullptr;
			if (result == ContentDialogResult::Primary) {
				self->startOpen(to_string(password));
			} else {
				// pdf.js left the viewer empty; I show why it's empty
				self->showMessage(to_hstring(
				    pdf::errorMessage(pdf::Error::PasswordRequired)));
			}
		}
	} catch (const hresult_error& error) {
		try {
			report(error.message().c_str());
		} catch (...) {
			report(L"Couldn't open this PDF: password prompt failed.");
		}
	} catch (const std::exception& error) {
		OutputDebugStringA(error.what());
		report(L"Couldn't open this PDF: password prompt failed.");
	} catch (...) {
		report(L"Couldn't open this PDF: unknown password prompt error.");
	}
}

void ViewerPage::close() {
	closeFind();
	closeOutline();
	closeAttachments();
	m_closed = true;
	cancelSelectionDrag();
	m_selection.reset();
	m_text.clear();
	if (m_textMenu) {
		m_textMenu.Hide();
	}
	Message().IsOpen(false);
	m_promptLoaded.revoke();
	m_passwordPending.reset();
	if (m_passwordDialog) {
		m_passwordDialog.Hide();
		m_passwordDialog = nullptr;
	}
	if (m_settle) {
		m_settle.Stop();
	}
	if (m_land) {
		m_land.Stop();
	}
	m_viewChanged.revoke();
	m_sizeChanged.revoke();
	m_layoutUpdated.revoke();
	m_thumbScrolled.revoke();
	m_thumbResized.revoke();
	// closing the window with the page box focused and the thumbnail panel
	// open crashed inside Microsoft.UI.Xaml.dll (0xc0000005) after this page
	// was gone; with the focus on the pages first it closes cleanly
	if (PageBox().FocusState() != FocusState::Unfocused) {
		Scroller().Focus(FocusState::Programmatic);
	}
	ThumbnailPanel().Visibility(XVisibility::Collapsed);
	Thumbnails().Children().Clear();
	m_thumbs.clear();
	m_spareThumbs.clear();
	m_thumbLoaded.clear();
	m_strip.reset();
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
	cancelSelectionDrag();
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
		m_thumbLoaded.clear();
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

void ViewerPage::print(void* owner, std::function<void()> done) {
	try {
		if (m_closed || !m_session || m_sizes.empty()) {
			if (!m_closed) {
				showNotice(L"This PDF isn't ready to print yet.");
			}
			done();
			return;
		}
		const auto session = m_session.get();
		const auto title = m_path.filename().wstring();
		auto selection =
#ifdef SABLE_SURFACE_TEST
		    m_printDialog
		        ? m_printDialog(owner, static_cast<int>(m_sizes.size()),
		                        static_cast<int>(m_current), title)
		        :
#endif
		        pdf::choosePrinter(owner, static_cast<int>(m_sizes.size()),
		                           static_cast<int>(m_current), title);
		if (selection.failed) {
			showNotice(L"Couldn't open the print dialog. Check that a printer "
			           L"is installed.");
		}
		if (!m_closed && m_session.get() == session && selection.job) {
			if (session->print(
			        std::move(*selection.job),
			        [weak = get_weak(), queue = DispatcherQueue(),
			         done](pdf::PrintResult result) {
				        queue.TryEnqueue([weak, done, result] {
					        if (auto self = weak.get();
					            self && !self->m_closed &&
					            result == pdf::PrintResult::Failed) {
						        self->showNotice(
						            L"Couldn't print this PDF. Check the "
						            L"printer and try again.");
					        }
					        done();
				        });
			        })) {
				return;
			}
			showNotice(L"This PDF isn't ready to print yet.");
		}
	} catch (...) {
		if (!m_closed) {
			showNotice(
			    L"Couldn't print this PDF. Check the printer and try again.");
		}
	}
	done();
}

void ViewerPage::onPageTapped(const Windows::Foundation::IInspectable&,
                              const Input::TappedRoutedEventArgs& args) {
	if (m_ignoreTextTap) {
		return;
	}
	requestLink(args.GetPosition(Pages()));
}

// Image-local DIPs already account for scrolling and display scale. Only the
// fractions within the displayed page cross over to the PDFium thread.
void ViewerPage::requestLink(Windows::Foundation::Point point) {
	if (m_closed || m_linkPending || !m_layout || !m_session ||
	    Visibility() != XVisibility::Visible) {
		return;
	}
	const auto page = m_layout->pageAt(point.Y);
	const auto& rect = m_layout->pages()[page];
	const double x = (point.X - rect.x) / rect.width;
	const double y = (point.Y - rect.y) / rect.height;
	if (x < 0 || x > 1 || y < 0 || y > 1) {
		return;
	}
	m_linkPending = true;
	try {
		const auto session = m_session.get();
		if (session->linkAt(
		        static_cast<int>(page), x, y,
		        [weak = get_weak(), queue = DispatcherQueue(),
		         session](pdf::LinkResult result) {
			        auto reply =
			            std::make_shared<pdf::LinkResult>(std::move(result));
			        queue.TryEnqueue([weak, session, reply] {
				        if (auto self = weak.get()) {
					        self->m_linkPending = false;
					        if (self->m_closed ||
					            self->m_session.get() != session ||
					            self->Visibility() != XVisibility::Visible ||
					            reply->canceled || !reply->link) {
						        return;
					        }
					        if (reply->link->destination) {
						        self->followLink(*reply->link->destination);
					        } else {
						        self->openLink(std::move(reply->link->uri));
					        }
				        }
			        });
		        })) {
			return;
		}
	} catch (...) {
		// A failed lookup leaves the document where it was.
	}
	m_linkPending = false;
}

void ViewerPage::followLink(const pdf::LinkDestination& target) {
	if (!m_layout || target.page < 0 ||
	    static_cast<std::size_t>(target.page) >= m_sizes.size()) {
		return;
	}
	m_current = static_cast<std::size_t>(target.page);
	if (target.fit == pdf::LinkFit::Page) {
		setPreset(helpers::ZoomPreset::PageFit);
	} else if (target.fit == pdf::LinkFit::Width) {
		setPreset(helpers::ZoomPreset::PageWidth);
	} else if (target.fit == pdf::LinkFit::Height ||
	           target.fit == pdf::LinkFit::Rectangle) {
		const auto& rect = m_layout->pages()[m_current];
		const double height =
		    target.fit == pdf::LinkFit::Rectangle ? target.height : 1;
		double zoom = m_zoom *
		              (std::max)(1.0, Scroller().ViewportHeight() - 5) /
		              (rect.height * height);
		if (target.fit == pdf::LinkFit::Rectangle) {
			zoom = (std::min)(
			    zoom, m_zoom *
			              (std::max)(1.0, Scroller().ViewportWidth() - 40) /
			              (rect.width * target.width));
		}
		zoomTo(std::clamp(zoom, helpers::MIN_ZOOM, helpers::MAX_ZOOM), {0, 0});
	} else if (target.zoom) {
		zoomTo(std::clamp(*target.zoom, helpers::MIN_ZOOM, helpers::MAX_ZOOM),
		       {0, 0});
	}
	goToPage(static_cast<std::size_t>(target.page));
	const auto& rect = m_layout->pages()[target.page];
	std::optional<double> x;
	if (target.x) {
		x = imageLeft() + rect.x + *target.x * rect.width;
	}
	jumpTo(x, rect.y + target.y.value_or(0) * rect.height);
}

fire_and_forget ViewerPage::openLink(std::string uri) {
	const auto weak = get_weak();
	try {
		Windows::Foundation::Uri address(to_hstring(uri));
		if ((address.SchemeName() != L"http" &&
		     address.SchemeName() != L"https") ||
		    address.Host().empty()) {
			co_return;
		}
#ifdef SABLE_SURFACE_TEST
		m_launchedUri = std::move(uri);
		co_return;
#else
		if (co_await Windows::System::Launcher::LaunchUriAsync(address)) {
			co_return;
		}
#endif
	} catch (...) {
	}
	if (auto self = weak.get(); self && !self->m_closed) {
		self->showNotice(L"Couldn't open this link in your browser.");
	}
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
	// even if this was already current, the initial thumbnail jump may
	// have been requested before the main view finished fitting
	scrollThumbnailIntoView();
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
	if (!m_pageEditHeld) {
		PageBox().Text(m_pageBoxText);
	}
	PageTotal().Text(to_hstring(text.total));
	PreviousButton().IsEnabled(m_current > 0);
	NextButton().IsEnabled(m_current + 1 < count);
	ZoomOutButton().IsEnabled(m_zoom > helpers::MIN_ZOOM);
	ZoomInButton().IsEnabled(m_zoom < helpers::MAX_ZOOM);
	// the rail is too narrow for pdf.js's "Automatic Zoom", so the button
	// always shows the percentage and the menu ticks the preset
	ZoomText().Text(to_hstring(helpers::zoomPercent(m_zoom)));
	// pagechanging: the thumbnails move their highlight and keep it in view
	if (thumbnailsOpen() && m_thumbCurrent != m_current) {
		const auto previous = std::exchange(m_thumbCurrent, m_current);
		for (const auto page : {previous, m_thumbCurrent}) {
			if (const auto found = page ? m_thumbs.find(*page) : m_thumbs.end();
			    found != m_thumbs.end()) {
				styleThumbnail(found->second, found->first);
			}
		}
		scrollThumbnailIntoView();
	}
}

// app.js webViewerPageNumberChanged, for an edited page box: go there if
// it names a page, and show the current page in the box either way
void ViewerPage::commitPageBox() {
	if (m_closed || !m_layout) {
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

// the thumbnail panel (pdf_sidebar.js and pdf_thumbnail_viewer.js)

bool ViewerPage::thumbnailsOpen() {
	return !m_closed && m_strip &&
	       ThumbnailPanel().Visibility() == XVisibility::Visible;
}

// pdf_sidebar.js open/close: opening highlights the current page's
// thumbnail and scrolls it into view. The pages get the room back, and
// "auto" and the fitting presets refit through the scroller's SizeChanged.
void ViewerPage::setThumbnailsOpen(bool open) {
	if (open) {
		setOutlineOpen(false);
		setAttachmentsOpen(false);
		m_sidebar = Sidebar::Thumbnails;
	}
	ThumbnailsButton().IsChecked(open);
	if (!m_strip) {
		m_thumbsWanted = open;
		return;
	}
	ThumbnailPanel().Visibility(open ? XVisibility::Visible
	                                 : XVisibility::Collapsed);
	if (!open) {
		m_hoverThumb.reset();
		return;
	}
	// the strip's viewport, to know what's in view
	ThumbnailScroller().UpdateLayout();
	m_thumbCurrent = m_current;
	for (const auto& [page, button] : m_thumbs) {
		styleThumbnail(button, page);
	}
	scrollThumbnailIntoView();
	updateThumbnails();
}

void ViewerPage::scrollThumbnailIntoView() {
	if (!thumbnailsOpen() || m_thumbScrollQueued) {
		return;
	}
	m_thumbScrollPending = true;
	// let panel layout and the initial page fit finish before scrolling
	m_thumbScrollQueued = DispatcherQueue().TryEnqueue([weak = get_weak()] {
		if (auto self = weak.get()) {
			self->m_thumbScrollQueued = false;
			self->landThumbnailScroll();
		}
	});
}

void ViewerPage::landThumbnailScroll() {
	if (!thumbnailsOpen()) {
		return;
	}
	auto scroller = ThumbnailScroller();
	// opening comes before the panel's first layout when DisplayThumbs is
	// on; then this waits until the strip has its size
	m_thumbScrollPending = scroller.ViewportHeight() <= 0 ||
	                       scroller.ExtentHeight() + 1 < m_strip->height();
	if (m_thumbScrollPending) {
		return;
	}
	if (const auto y = m_strip->scrollTo(m_current, scroller.VerticalOffset(),
	                                     scroller.ViewportHeight())) {
		scroller.ChangeView(nullptr, *y, nullptr, true);
	}
}

// one thumbnail: an <a> around a 7 px border, 1 px of padding and the
// image, with a dashed outline until the render is in (viewer.css)
Button ViewerPage::makeThumbnail() {
	if (!m_thumbPlain) {
		// --thumbnail-selected-color and --thumbnail-hover-color, dark
		m_thumbSelected = Media::SolidColorBrush({0x33, 0xff, 0xff, 0xff});
		m_thumbHover = Media::SolidColorBrush({0x1a, 0xff, 0xff, 0xff});
		m_thumbPlain = Media::SolidColorBrush({0, 0, 0, 0});
	}
	Button button;
	button.Style(Application::Current()
	                 .Resources()
	                 .Lookup(box_value(L"SableThumbnailButtonStyle"))
	                 .as<winrt::Microsoft::UI::Xaml::Style>());
	Border frame;
	frame.BorderThickness({helpers::THUMBNAIL_BORDER, helpers::THUMBNAIL_BORDER,
	                       helpers::THUMBNAIL_BORDER,
	                       helpers::THUMBNAIL_BORDER});
	frame.Padding({helpers::THUMBNAIL_PADDING, helpers::THUMBNAIL_PADDING,
	               helpers::THUMBNAIL_PADDING, helpers::THUMBNAIL_PADDING});
	frame.CornerRadius({2, 2, 2, 2});
	frame.BorderBrush(m_thumbPlain);
	Grid box;
	box.Width(helpers::THUMBNAIL_WIDTH);
	Shapes::Rectangle outline;
	outline.Stroke(Media::SolidColorBrush({0xff, 132, 132, 132}));
	outline.StrokeThickness(1);
	Media::DoubleCollection dashes;
	dashes.Append(3);
	dashes.Append(3);
	outline.StrokeDashArray(dashes);
	Image image;
	image.Stretch(Media::Stretch::Fill);
	box.Children().Append(outline);
	box.Children().Append(image);
	frame.Child(box);
	button.Content(frame);
	// linkService.goToPage(id)
	button.Click([weak = get_weak()](const auto& sender, auto&&) {
		auto self = weak.get();
		const auto tag = sender.template as<Button>().Tag();
		if (self && tag) {
			self->leavePageBox();
			self->goToPage(
			    static_cast<std::size_t>(unbox_value_or<int32_t>(tag, 0)));
		}
	});
	const auto hover = [weak = get_weak()](const IInspectable& sender,
	                                       bool entered) {
		auto self = weak.get();
		const auto button = sender.as<Button>();
		const auto tag = button.Tag();
		if (!self || !tag) {
			return;
		}
		const auto page =
		    static_cast<std::size_t>(unbox_value_or<int32_t>(tag, 0));
		if (entered) {
			self->m_hoverThumb = page;
		} else if (self->m_hoverThumb == page) {
			self->m_hoverThumb.reset();
		}
		self->styleThumbnail(button, page);
	};
	button.PointerEntered(
	    [hover](const auto& sender, auto&&) { hover(sender, true); });
	button.PointerExited(
	    [hover](const auto& sender, auto&&) { hover(sender, false); });
	return button;
}

// .thumbnail.selected and :hover: the border and the image's opacity
void ViewerPage::styleThumbnail(const Button& button, std::size_t page) {
	const auto frame = button.Content().as<Border>();
	const auto image = frame.Child().as<Grid>().Children().GetAt(1).as<Image>();
	const bool selected = page == m_current;
	const bool hovered = m_hoverThumb == page;
	frame.BorderBrush(selected  ? m_thumbSelected
	                  : hovered ? m_thumbHover
	                            : m_thumbPlain);
	image.Opacity(selected ? 1 : hovered ? 0.95 : 0.9);
}

void ViewerPage::showThumbnail(std::size_t page, const pdf::Bitmap& bitmap) {
	const auto found = m_thumbs.find(page);
	if (found == m_thumbs.end() || bitmap.width <= 0 || bitmap.height <= 0) {
		return;
	}
	Media::Imaging::WriteableBitmap pixels(bitmap.width, bitmap.height);
	auto* to = pixels.PixelBuffer().data();
	const auto row = static_cast<std::size_t>(bitmap.width) * 4;
	for (int y = 0; y < bitmap.height; ++y) {
		std::memcpy(to + row * y,
		            bitmap.pixels.data() +
		                static_cast<std::size_t>(bitmap.stride) * y,
		            row);
	}
	pixels.Invalidate();
	const auto box = found->second.Content().as<Border>().Child().as<Grid>();
	box.Children().GetAt(1).as<Image>().Source(pixels);
	box.Children().GetAt(0).Visibility(XVisibility::Collapsed);
	m_thumbLoaded.insert(page);
}

pdf::PageKey ViewerPage::thumbKey(std::size_t page) const {
	const auto pixels = m_strip->renderSize(page, m_scale);
	const auto size = helpers::renderSize(pixels.width, pixels.height,
	                                      MAX_PAGE_PIXELS, MAX_PAGE_SIDE);
	return {static_cast<int>(page), size.width, size.height, 0};
}

void ViewerPage::onThumbnailsClick(const IInspectable&,
                                   const RoutedEventArgs&) {
	leavePageBox();
	setThumbnailsOpen(
	    unbox_value_or<bool>(ThumbnailsButton().IsChecked(), false));
}

// the thumbnails from a screen above the view to a screen below it are
// made, the rest go back to be reused, like ItemsRepeater does but at
// pdf.js's exact positions, so scrolling to a thumbnail lands on it
void ViewerPage::updateThumbnails() {
	if (!thumbnailsOpen()) {
		return;
	}
	if (m_thumbScrollPending) {
		scrollThumbnailIntoView();
	}
	auto scroller = ThumbnailScroller();
	const double view = (std::max)(scroller.ViewportHeight(), 1.0);
	const auto range =
	    m_strip->pagesIn(scroller.VerticalOffset() - view, 3 * view);
	for (auto it = m_thumbs.begin(); it != m_thumbs.end();) {
		if (it->first >= range.first && it->first < range.end) {
			++it;
			continue;
		}
		const auto button = it->second;
		button.Visibility(XVisibility::Collapsed);
		button.Tag(nullptr);
		button.Content()
		    .as<Border>()
		    .Child()
		    .as<Grid>()
		    .Children()
		    .GetAt(1)
		    .as<Image>()
		    .Source(nullptr);
		if (m_hoverThumb == it->first) {
			m_hoverThumb.reset();
		}
		m_thumbLoaded.erase(it->first);
		m_spareThumbs.push_back(button);
		it = m_thumbs.erase(it);
	}
	bool missing = false;
	for (auto page = range.first; page < range.end; ++page) {
		if (m_thumbs.contains(page)) {
			continue;
		}
		Button button{nullptr};
		if (m_spareThumbs.empty()) {
			button = makeThumbnail();
			Thumbnails().Children().Append(button);
		} else {
			button = m_spareThumbs.back();
			m_spareThumbs.pop_back();
		}
		prepareThumbnail(button, page);
		missing = missing || !m_thumbLoaded.contains(page);
	}
	if (missing) {
		queueRequest();
	}
}

void ViewerPage::prepareThumbnail(const Button& button, std::size_t page) {
	button.Tag(box_value(static_cast<int32_t>(page)));
	// Tab goes through the thumbnails in page order, not the order they
	// were made in
	button.TabIndex(static_cast<int32_t>(page));
	const double outer =
	    helpers::THUMBNAIL_WIDTH +
	    2 * (helpers::THUMBNAIL_BORDER + helpers::THUMBNAIL_PADDING);
	const double width =
	    ThumbnailPanel().Width() - ThumbnailPanel().BorderThickness().Left;
	Canvas::SetLeft(button, (std::max)(0.0, std::floor((width - outer) / 2)));
	Canvas::SetTop(button, m_strip->top(page));
	const auto box = button.Content().as<Border>().Child().as<Grid>();
	box.Height(m_strip->imageHeight(page));
	box.Children().GetAt(0).Visibility(XVisibility::Visible);
	box.Children().GetAt(1).as<Image>().Source(nullptr);
	// pdfjs-thumb-page-title and pdfjs-thumb-page-canvas
	const auto name = to_hstring(helpers::pageName(page, m_labels));
	Controls::ToolTipService::SetToolTip(button, box_value(L"Page " + name));
	Automation::AutomationProperties::SetName(button,
	                                          L"Thumbnail of Page " + name);
	button.Visibility(XVisibility::Visible);
	m_thumbs.insert_or_assign(page, button);
	m_thumbLoaded.erase(page);
	styleThumbnail(button, page);
	if (const auto* bitmap = m_cache.find(thumbKey(page))) {
		showThumbnail(page, *bitmap);
	}
}

// many thumbnails are made in one layout pass; look for work once after it
void ViewerPage::queueRequest() {
	if (m_requestQueued || m_closed) {
		return;
	}
	m_requestQueued = DispatcherQueue().TryEnqueue([weak = get_weak()] {
		if (auto self = weak.get()) {
			self->m_requestQueued = false;
			try {
				self->requestNext();
			} catch (...) {
				self->showMessage(L"Couldn't show this PDF: " + to_message());
			}
		}
	});
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
	// Sidebar lists keep their native arrows, type-ahead and selection keys.
	if ((outlineHasFocus() || attachmentsHasFocus()) &&
	    args.Key() != VirtualKey::F4 &&
	    !(args.Key() == VirtualKey::G && (GetKeyState(VK_CONTROL) & 0x8000) &&
	      (GetKeyState(VK_MENU) & 0x8000))) {
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
	case VirtualKey::F4:
		pressed = NavKey::F4;
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
	case NavAction::ToggleSidebar:
		toggleSidebar();
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
	if (!m_pageEditHeld && PageBox().FocusState() == FocusState::Keyboard) {
		PageBox().SelectAll();
	}
}

// LosingFocus rather than LostFocus, which comes later: by then a click on
// the rail could have put the current page back in the box
void ViewerPage::onPageBoxLosingFocus(const Windows::Foundation::IInspectable&,
                                      const Input::LosingFocusEventArgs&) {
	if (!m_pageEditHeld) {
		commitPageBox();
	}
}

void ViewerPage::holdPageEdit(bool hold) {
	if (m_closed) {
		return;
	}
	m_pageEditHeld = hold;
	if (!hold && PageBox().FocusState() == FocusState::Unfocused) {
		commitPageBox();
	}
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
	// app.js onPassword: the prompt, saying so if the last one was wrong
	if (info.error == pdf::Error::PasswordRequired ||
	    info.error == pdf::Error::WrongPassword) {
		askPassword(info.error == pdf::Error::WrongPassword);
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
		m_copyAllowed = info.canCopy;
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
		setupSelection();
		setupFind();
		setupOutline();
		setupAttachments();
		m_loaded = Loaded(auto_revoke, [weak = get_weak()](auto&&, auto&&) {
			if (auto self = weak.get()) {
				self->watchScale();
			}
		});
		watchScale();
		m_strip.emplace(m_sizes);
		Thumbnails().Height(m_strip->height());
		m_thumbScrolled = ThumbnailScroller().ViewChanged(
		    auto_revoke, [this](auto&&, auto&&) { updateThumbnails(); });
		m_thumbResized = ThumbnailScroller().SizeChanged(
		    auto_revoke, [this](auto&&, auto&&) { updateThumbnails(); });
		// the "auto" fit that's still to come takes the rail into account
		Rail().Visibility(XVisibility::Visible);
		showPosition();
		if (m_thumbsWanted) {
			setThumbnailsOpen(true);
		}
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

void ViewerPage::showNotice(const hstring& text) {
	if (m_closed) {
		return;
	}
	diagnostics::write(diagnostics::Level::Warning, to_string(text));
	Message().Message(text);
	Message().IsClosable(true);
	Message().IsOpen(true);
	Message().Visibility(XVisibility::Visible);
}

void ViewerPage::showMessage(const hstring& text) {
	if (m_closed) {
		return;
	}
	showNotice(text);
	Message().IsClosable(false);
	Scroller().Visibility(XVisibility::Collapsed);
	Rail().Visibility(XVisibility::Collapsed);
	ThumbnailPanel().Visibility(XVisibility::Collapsed);
	m_thumbScrollPending = false;
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
	check_hresult(device.context->CreateSolidColorBrush(
	    D2D1::ColorF(0.20f, 0.50f, 1.0f, 0.40f), device.selection.put()));
	check_hresult(device.context->CreateSolidColorBrush(
	    D2D1::ColorF(1.f, .85f, .05f, .45f), device.findMatch.put()));
	check_hresult(device.context->CreateSolidColorBrush(
	    D2D1::ColorF(1.f, .40f, .05f, .65f), device.findActive.put()));
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
	if (m_thumbScrollPending) {
		scrollThumbnailIntoView();
	}
	const double top = Scroller().VerticalOffset();
	double height = Scroller().ViewportHeight();
	if (height <= 0) {
		height = ActualHeight();
	}
	m_visible = m_layout->visiblePages(top, height, 0);
	m_wanted = m_layout->visiblePages(top, height, 1);
	prefetchSelectedText();
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
	if (m_closed || !m_session || !m_layout) {
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
		startRender(wanted);
	}
	// pdf_rendering_queue.js: thumbnails once the pages have what they
	// need, the ones in view and below first, then the ones above
	if (!thumbnailsOpen() || m_thumbs.empty()) {
		return;
	}
	const double top = ThumbnailScroller().VerticalOffset();
	const auto below = [this, top](std::size_t page) {
		return m_strip->top(page) + m_strip->outerHeight(page) > top;
	};
	std::vector<std::size_t> order;
	for (const auto& [page, button] : m_thumbs) {
		if (below(page)) {
			order.push_back(page);
		}
	}
	for (auto it = m_thumbs.rbegin(); it != m_thumbs.rend(); ++it) {
		if (!below(it->first)) {
			order.push_back(it->first);
		}
	}
	for (const auto page : order) {
		if (m_thumbLoaded.contains(page)) {
			continue;
		}
		const auto wanted = thumbKey(page);
		if (const auto* bitmap = m_cache.find(wanted)) {
			showThumbnail(page, *bitmap);
			continue;
		}
		if (m_settling || m_rendering || m_failed.contains(wanted)) {
			continue;
		}
		startRender(wanted);
	}
}

void ViewerPage::startRender(const pdf::PageKey& key) {
	m_rendering =
	    m_session->render(key, [weak = get_weak(), queue = DispatcherQueue()](
	                               pdf::RenderResult result) {
		    auto rendered =
		        std::make_shared<pdf::RenderResult>(std::move(result));
		    queue.TryEnqueue([weak, rendered] {
			    if (auto self = weak.get()) {
				    self->rendered(std::move(*rendered));
			    }
		    });
	    });
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
			} else if (m_strip && page < m_strip->size() &&
			           result.key == thumbKey(page)) {
				if (!result.bitmap) {
					m_failed.insert(result.key);
				} else {
					const bool shown = m_thumbs.contains(page);
					if (shown) {
						showThumbnail(page, *result.bitmap);
					}
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
	drawFind(context, pages);
	if (m_selection) {
		for (auto page = pages.first; page < pages.end; ++page) {
			const auto found = m_text.find(static_cast<int>(page));
			if (found == m_text.end()) {
				continue;
			}
			const auto& text = *found->second.text;
			const auto [first, end] =
			    pdf::textSpan(*m_selection, static_cast<int>(page),
			                  static_cast<int>(text.characters.size()));
			const auto at = pixels(page);
			for (const auto& box : pdf::textRects(text, first, end)) {
				context.FillRectangle(
				    D2D1::RectF(
				        static_cast<float>(at.x + box.left * at.width),
				        static_cast<float>(at.y + box.top * at.height),
				        static_cast<float>(at.x + box.right * at.width),
				        static_cast<float>(at.y + box.bottom * at.height)),
				    m_device.selection.get());
			}
		}
	}
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
