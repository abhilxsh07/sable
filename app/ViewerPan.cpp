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
#include <winrt/Microsoft.UI.Xaml.Input.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
namespace {
bool panContent(DependencyObject source, const ScrollViewer& scroller) {
	while (source && source != scroller) {
		if (source.try_as<Primitives::ScrollBar>() ||
		    source.try_as<Primitives::ButtonBase>()) {
			return false;
		}
		source = Media::VisualTreeHelper::GetParent(source);
	}
	return source == scroller;
}
} // namespace
void ViewerPage::setCursorTool(CursorTool tool) {
	if (m_closed || m_presentation || tool == m_cursorTool) {
		return;
	}
	stopPan();
	cancelSelectionDrag();
	m_cursorTool = tool;
	updatePanCursor();
}
void ViewerPage::updatePanCursor() {
	if (!m_closed && m_cursorTool == CursorTool::Hand &&
	    (m_panHover || m_pan.active())) {
		if (!m_handCursor) {
			using namespace Microsoft::UI::Input;
			m_handCursor =
			    InputSystemCursor::Create(InputSystemCursorShape::Hand);
			m_dragCursor =
			    InputSystemCursor::Create(InputSystemCursorShape::SizeAll);
		}
		ProtectedCursor(m_pan.active() && m_pan.moved() ? m_dragCursor
		                                                : m_handCursor);
	} else {
		ProtectedCursor(nullptr);
	}
}
void ViewerPage::stopPan(bool leaveViewport) {
	if (leaveViewport) {
		m_panHover = false;
	}
	const bool wasActive = m_pan.active();
	if (wasActive) {
		m_ignorePanTap = true;
	}
	m_pan.end();
	m_panPointer.reset();
	if (wasActive) {
		Scroller().ReleasePointerCaptures();
	}
	updatePanCursor();
}
bool ViewerPage::beginPan(Windows::Foundation::Point point) {
	if (m_closed || m_presentation || !m_layout ||
	    m_cursorTool != CursorTool::Hand ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
		return false;
	}
	cancelSelectionDrag();
	++m_linkVersion;
	m_linkPending = false;
	m_ignorePanTap = false;
	return m_pan.begin({point.X, point.Y}, {Scroller().HorizontalOffset(),
	                                        Scroller().VerticalOffset()});
}
void ViewerPage::movePan(Windows::Foundation::Point point) {
	if (!m_pan.active() || m_closed) {
		return;
	}
	if (const auto offset =
	        m_pan.move({point.X, point.Y}, {Scroller().ScrollableWidth(),
	                                        Scroller().ScrollableHeight()})) {
		m_ignorePanTap = true;
		Scroller().ChangeView(offset->x, offset->y, nullptr, true);
		updatePanCursor();
	}
}
void ViewerPage::finishPan(Windows::Foundation::Point point) {
	if (!m_pan.active()) {
		return;
	}
	movePan(point);
	const bool moved = m_pan.moved();
	stopPan();
	// Viewport capture bypasses Image.Tapped. Resolve an unmoved tap once.
	if (!moved && point.X >= 0 && point.Y >= 0 &&
	    point.X <= Scroller().ViewportWidth() &&
	    point.Y <= Scroller().ViewportHeight()) {
		const auto at =
		    Scroller().TransformToVisual(Pages()).TransformPoint(point);
		requestLink(at, m_clickSpreads ? spreadClickDirection(at) : 0);
	}
}
void ViewerPage::setupPan() {
	const auto weak = get_weak();
	Scroller().LostFocus([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->stopPan();
		}
	});
	Scroller().PointerEntered(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& e) {
		    if (const auto self = weak.get();
		        self && e.Pointer().PointerDeviceType() !=
		                    Microsoft::UI::Input::PointerDeviceType::Touch) {
			    self->m_panHover = true;
			    self->updatePanCursor();
		    }
	    });
	Scroller().PointerExited(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& e) {
		    if (const auto self = weak.get();
		        self && e.Pointer().PointerDeviceType() !=
		                    Microsoft::UI::Input::PointerDeviceType::Touch) {
			    self->m_panHover = false;
			    self->updatePanCursor();
		    }
	    });
	Scroller().PointerPressed([weak](auto&&,
	                                 const Input::PointerRoutedEventArgs& e) {
		const auto self = weak.get();
		if (self && e.Pointer().PointerDeviceType() ==
		                Microsoft::UI::Input::PointerDeviceType::Touch) {
			self->m_ignorePanTap = false;
			self->m_ignoreTextTap = false;
			return;
		}
		if (!self || self->m_pan.active() ||
		    self->m_cursorTool != CursorTool::Hand ||
		    e.Pointer().PointerDeviceType() ==
		        Microsoft::UI::Input::PointerDeviceType::Touch ||
		    !e.GetCurrentPoint(self->Scroller())
		         .Properties()
		         .IsLeftButtonPressed() ||
		    !panContent(e.OriginalSource().try_as<DependencyObject>(),
		                self->Scroller())) {
			return;
		}
		self->leavePageBox();
		self->Scroller().Focus(FocusState::Programmatic);
		if (self->beginPan(e.GetCurrentPoint(self->Scroller()).Position())) {
			if (self->Scroller().CapturePointer(e.Pointer())) {
				self->m_panPointer = e.Pointer().PointerId();
				e.Handled(true);
			} else {
				self->stopPan();
			}
		}
	});
	Scroller().PointerMoved(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& e) {
		    const auto self = weak.get();
		    if (!self || self->m_panPointer != e.Pointer().PointerId()) {
			    return;
		    }
		    const auto point = e.GetCurrentPoint(self->Scroller());
		    if (!point.Properties().IsLeftButtonPressed()) {
			    self->stopPan();
			    return;
		    }
		    self->movePan(point.Position());
		    e.Handled(true);
	    });
	Scroller().PointerReleased(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& e) {
		    const auto self = weak.get();
		    if (!self || self->m_panPointer != e.Pointer().PointerId()) {
			    return;
		    }
		    const auto point = e.GetCurrentPoint(self->Scroller()).Position();
		    self->finishPan(point);
		    e.Handled(true);
	    });
	Scroller().PointerCaptureLost(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& e) {
		    if (const auto self = weak.get();
		        self && self->m_panPointer == e.Pointer().PointerId()) {
			    self->stopPan();
		    }
	    });
	Scroller().PointerCanceled(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& e) {
		    if (const auto self = weak.get();
		        self && self->m_panPointer == e.Pointer().PointerId()) {
			    self->stopPan();
		    }
	    });
	Unloaded([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->m_panHover = false;
			self->stopPan();
		}
	});
}
} // namespace winrt::Sable::implementation
