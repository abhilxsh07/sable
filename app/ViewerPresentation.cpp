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
#include <algorithm>
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using XVisibility = Microsoft::UI::Xaml::Visibility;
namespace winrt::Sable::implementation {
bool ViewerPage::beginPresentation(std::function<void()> exit) {
	if (m_closed || !m_layout || !m_surface || m_startPage || m_presentation ||
	    m_directManipulation || m_resettingGesture ||
	    std::abs(Scroller().ZoomFactor() - 1.f) > 1e-5f) {
		return false;
	}
	leavePageBox();
	const auto page = m_current;
	stopPan(true);
	dismissProperties();
	closeFind();
	clearTextSelection();
	if (m_textMenu) {
		m_textMenu.Hide();
	}
	ZoomMenu().Hide();
	m_presentation = PresentationView{m_scrollMode,
	                                  m_spreadMode,
	                                  m_preset,
	                                  m_zoom,
	                                  ThumbnailPanel().Visibility(),
	                                  OutlinePanel().Visibility(),
	                                  AttachmentPanel().Visibility(),
	                                  Scroller().HorizontalScrollMode(),
	                                  Scroller().VerticalScrollMode()};
	m_exitPresentation = std::move(exit);
	m_presentationSwiped = false;
	m_presentationDelta = 0;
	m_presentationWheelTime = {};
	m_scrollMode = helpers::ScrollMode::Page;
	const auto sizes = displaySizes();
	if (!std::all_of(sizes.begin(), sizes.end(), [&](const auto& size) {
		    return std::abs(size.width - sizes.front().width) < .001 &&
		           std::abs(size.height - sizes.front().height) < .001;
	    })) {
		m_spreadMode = helpers::SpreadMode::None;
	}
	m_preset = helpers::ZoomPreset::PageFit;
	ThumbnailPanel().Visibility(XVisibility::Collapsed);
	OutlinePanel().Visibility(XVisibility::Collapsed);
	AttachmentPanel().Visibility(XVisibility::Collapsed);
	Rail().Visibility(XVisibility::Collapsed);
	PresentationBackdrop().Visibility(XVisibility::Visible);
	Pages().VerticalAlignment(VerticalAlignment::Center);
	Scroller().HorizontalScrollMode(ScrollMode::Disabled);
	Scroller().VerticalScrollMode(ScrollMode::Disabled);
	Scroller().ZoomMode(ZoomMode::Disabled);
	Scroller().HorizontalScrollBarVisibility(ScrollBarVisibility::Hidden);
	Scroller().VerticalScrollBarVisibility(ScrollBarVisibility::Hidden);
	Pages().ContextFlyout(nullptr);
	++m_linkVersion;
	m_linkPending = false;
	++m_outlineLinkVersion;
	m_outlineLinkPending = false;
	try {
		UpdateLayout();
		if (!relayout(fittedZoom(sizes))) {
			endPresentation();
			return false;
		}
		settle();
		Scroller().UpdateLayout();
		goToPage(page);
		showPresentationControls();
		return true;
	} catch (...) {
		endPresentation();
		return false;
	}
}

void ViewerPage::endPresentation() {
	const auto saved = std::exchange(m_presentation, std::nullopt);
	if (!saved) {
		return;
	}
	m_exitPresentation = {};
	m_presentationTouch.reset();
	if (m_presentationTimer) {
		m_presentationTimer.Stop();
	}
	++m_linkVersion;
	m_linkPending = false;
	if (m_closed) {
		return;
	}
	const auto page = m_current;
	m_scrollMode = saved->scroll;
	m_spreadMode = saved->spread;
	m_preset = saved->preset;
	ThumbnailPanel().Visibility(saved->thumbnails);
	OutlinePanel().Visibility(saved->outline);
	AttachmentPanel().Visibility(saved->attachments);
	Rail().Visibility(XVisibility::Visible);
	PresentationBackdrop().Visibility(XVisibility::Collapsed);
	PresentationControls().Visibility(XVisibility::Collapsed);
	Pages().VerticalAlignment(VerticalAlignment::Top);
	Pages().ContextFlyout(m_textMenu);
	Scroller().HorizontalScrollMode(saved->horizontal);
	Scroller().VerticalScrollMode(saved->vertical);
	Scroller().ZoomMode(ZoomMode::Enabled);
	Scroller().HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
	Scroller().VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
	try {
		UpdateLayout();
		const double zoom = m_preset == helpers::ZoomPreset::None
		                        ? saved->zoom
		                        : fittedZoom(displaySizes());
		if (!relayout(zoom)) {
			// The saved continuous layout may exceed the surface at a new DPI.
			m_scrollMode = helpers::ScrollMode::Page;
			m_spreadMode = helpers::SpreadMode::None;
			m_preset = helpers::ZoomPreset::PageFit;
			relayout(fittedZoom(displaySizes()));
			showNotice(
			    L"The previous layout is too large at this display scale.");
		}
		settle();
		Scroller().UpdateLayout();
		goToPage(page);
		Scroller().Focus(FocusState::Programmatic);
		updateVisible();
	} catch (...) {
		showNotice(L"Couldn't restore the reading layout.");
	}
}

void ViewerPage::presentationKey(unsigned key, bool shift) {
	if (!m_presentation || m_closed) {
		return;
	}
	m_presentationWheelTime = {};
	m_presentationDelta = 0;
	++m_linkVersion;
	m_linkPending = false;
	int direction = 0;
	switch (key) {
	case VK_SPACE:
	case VK_RETURN:
		direction = shift ? -1 : 1;
		break;
	case VK_DOWN:
	case VK_RIGHT:
	case VK_NEXT:
	case 'J':
	case 'N':
		direction = 1;
		break;
	case VK_UP:
	case VK_LEFT:
	case VK_PRIOR:
	case VK_BACK:
	case 'K':
	case 'P':
		direction = -1;
		break;
	case VK_HOME:
		goToPage(0);
		return;
	case VK_END:
		goToPage(m_sizes.size() - 1);
		return;
	default:
		return;
	}
	advancePage(direction);
}

void ViewerPage::presentationWheel(int delta) {
	if (!m_presentation || !delta || m_closed) {
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	if (now - m_presentationWheelTime < std::chrono::milliseconds(50)) {
		return;
	}
	if ((delta > 0) != (m_presentationDelta > 0)) {
		m_presentationDelta = 0;
	}
	m_presentationDelta += std::clamp(delta, -120, 120);
	if (std::abs(m_presentationDelta) >= 12) {
		const auto page = m_current;
		const int direction = m_presentationDelta < 0 ? 1 : -1;
		m_presentationDelta = 0;
		++m_linkVersion;
		m_linkPending = false;
		advancePage(direction);
		if (m_current != page) {
			m_presentationWheelTime = now;
		}
	}
}

void ViewerPage::showPresentationControls() {
	if (!m_presentation || m_closed) {
		return;
	}
	PresentationPosition().Text(to_hstring(m_current + 1) + L" / " +
	                            to_hstring(m_sizes.size()));
	PresentationPrevious().IsEnabled(m_current > 0);
	PresentationNext().IsEnabled(m_current + 1 < m_sizes.size());
	PresentationControls().Visibility(XVisibility::Visible);
	if (!m_presentationTimer) {
		m_presentationTimer = DispatcherQueue().CreateTimer();
		m_presentationTimer.IsRepeating(false);
		m_presentationTimer.Interval(std::chrono::seconds(3));
		m_presentationTimer.Tick([weak = get_weak()](auto&&, auto&&) {
			if (const auto self = weak.get()) {
				const auto focus =
				    Input::FocusManager::GetFocusedElement(self->XamlRoot());
				if (focus == self->PresentationPrevious() ||
				    focus == self->PresentationNext() ||
				    focus == self->PresentationExit()) {
					return;
				}
				self->PresentationControls().Visibility(XVisibility::Collapsed);
			}
		});
	}
	m_presentationTimer.Stop();
	m_presentationTimer.Start();
}

void ViewerPage::setupPresentation() {
	const auto weak = get_weak();
	PresentationPrevious().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->presentationKey(VK_PRIOR, false);
		}
	});
	PresentationNext().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->presentationKey(VK_NEXT, false);
		}
	});
	PresentationExit().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			if (const auto exit = self->m_exitPresentation) {
				exit();
			}
		}
	});
	Scroller().PointerMoved(
	    [weak](auto&&, const Input::PointerRoutedEventArgs&) {
		    if (const auto self = weak.get(); self && self->m_presentation) {
			    self->showPresentationControls();
		    }
	    });
	Scroller().Tapped([weak](auto&&, const Input::TappedRoutedEventArgs& e) {
		if (const auto self = weak.get(); self && self->m_presentation) {
			if (!std::exchange(self->m_presentationSwiped, false)) {
				self->requestLink(e.GetPosition(self->Pages()),
				                  (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
			}
			e.Handled(true);
		}
	});
	Scroller().PointerPressed(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& e) {
		    if (const auto self = weak.get();
		        self && self->m_presentation &&
		        e.Pointer().PointerDeviceType() !=
		            Microsoft::UI::Input::PointerDeviceType::Touch) {
			    self->m_presentationSwiped = false;
		    }
		    if (const auto self = weak.get();
		        self && self->m_presentation &&
		        e.Pointer().PointerDeviceType() ==
		            Microsoft::UI::Input::PointerDeviceType::Touch) {
			    if (self->m_presentationTouch) {
				    self->m_presentationTouch.reset();
				    self->m_presentationSwiped = true;
				    return;
			    }
			    self->m_presentationSwiped = false;
			    self->m_presentationTouch = e.Pointer().PointerId();
			    self->m_presentationStart =
			        e.GetCurrentPoint(self->Scroller()).Position();
		    }
	    });
	Scroller().PointerReleased(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& e) {
		    if (const auto self = weak.get();
		        self && self->m_presentation &&
		        self->m_presentationTouch == e.Pointer().PointerId()) {
			    self->m_presentationTouch.reset();
			    const auto end = e.GetCurrentPoint(self->Scroller()).Position();
			    const double x = end.X - self->m_presentationStart.X;
			    const double y = end.Y - self->m_presentationStart.Y;
			    const bool horizontal =
			        std::abs(x) > 50 && std::abs(y) <= std::abs(x) * .57735;
			    const bool vertical =
			        std::abs(y) > 50 && std::abs(x) <= std::abs(y) * .57735;
			    if (horizontal || vertical) {
				    self->m_presentationSwiped = true;
				    self->presentationKey(
				        (horizontal ? x : y) < 0 ? VK_NEXT : VK_PRIOR, false);
			    }
		    }
	    });
	const auto cancel = [weak](auto&&, auto&&) {
		if (const auto self = weak.get(); self && self->m_presentationTouch) {
			self->m_presentationTouch.reset();
			self->m_presentationSwiped = true;
		}
	};
	Scroller().PointerCanceled(cancel);
	Scroller().PointerCaptureLost(cancel);
}
} // namespace winrt::Sable::implementation
