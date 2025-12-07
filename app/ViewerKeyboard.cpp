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

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
namespace {
using XVisibility = Microsoft::UI::Xaml::Visibility;
}
bool ViewerPage::navigationKey(unsigned key, helpers::NavModifiers modifiers) {
	const auto source = Input::FocusManager::GetFocusedElement(XamlRoot());
	using helpers::NavAction;
	using helpers::NavFocus;
	using helpers::NavKey;
	using Windows::System::VirtualKey;
	// a menu opened from the rail keeps its own keys
	if (!m_layout || m_closed || m_presentation || m_propertiesDialog ||
	    m_passwordDialog || source.try_as<MenuFlyoutItemBase>()) {
		return false;
	}
	// Sidebar lists keep their native arrows, type-ahead and selection keys.
	if ((outlineHasFocus() || attachmentsHasFocus() || commentsHasFocus()) &&
	    static_cast<VirtualKey>(key) != VirtualKey::F4 &&
	    !(static_cast<VirtualKey>(key) == VirtualKey::G && modifiers.ctrl &&
	      modifiers.alt)) {
		return false;
	}
	NavKey pressed = NavKey::Other;
	switch (static_cast<VirtualKey>(key)) {
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
	case VirtualKey::H:
		pressed = NavKey::H;
		break;
	case VirtualKey::S:
		pressed = NavKey::S;
		break;
	case VirtualKey::R:
		pressed = NavKey::R;
		break;
	case VirtualKey::F4:
		pressed = NavKey::F4;
		break;
	default:
		return false;
	}
	auto focus = NavFocus::Viewer;
	if (source.try_as<TextBox>() || source.try_as<PasswordBox>() ||
	    source.try_as<RichEditBox>() || source.try_as<ComboBox>() ||
	    source.try_as<Slider>()) {
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
			advancePage(-1);
		}
		break;
	case NavAction::Next:
		if (m_current + 1 < count) {
			advancePage(1);
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
	case NavAction::HandTool:
		setCursorTool(CursorTool::Hand);
		break;
	case NavAction::SelectTool:
		setCursorTool(CursorTool::Select);
		break;
	case NavAction::RotateClockwise:
		rotateView(1);
		break;
	case NavAction::RotateCounterClockwise:
		rotateView(-1);
		break;
	}
	bool handled = action != NavAction::None;
	// what the browser did with the keys pdf.js left alone. Space scrolled
	// a page, which WinUI's ScrollViewer doesn't; a page is the viewport,
	// as for its PageDown.
	// see:
	// https://raw.githubusercontent.com/microsoft/microsoft-ui-xaml/fe9d01273af3382ea570e203ab700772528d98df/dxaml/xcp/dxaml/lib/ScrollContentPresenter_Partial.cpp
	if (action == NavAction::None && focus == NavFocus::Viewer &&
	    !modifiers.ctrl && !modifiers.alt && !modifiers.windows &&
	    (pressed == NavKey::Space ||
	     (m_scrollMode == helpers::ScrollMode::Horizontal &&
	      (pressed == NavKey::PageUp || pressed == NavKey::PageDown)))) {
		const bool horizontal = m_scrollMode == helpers::ScrollMode::Horizontal;
		const bool previous = pressed == NavKey::PageUp ||
		                      (pressed == NavKey::Space && modifiers.shift);
		const double distance =
		    (previous ? -1 : 1) * (horizontal ? Scroller().ViewportWidth()
		                                      : Scroller().ViewportHeight());
		if (horizontal) {
			Scroller().ChangeView(Scroller().HorizontalOffset() + distance,
			                      nullptr, nullptr, false);
		} else {
			Scroller().ChangeView(nullptr,
			                      Scroller().VerticalOffset() + distance,
			                      nullptr, false);
		}
		handled = true;
	}
	// app.js: the navigation keys bring the focus back to the pages
	// (ensureViewerFocused), except from a text field
	bool focusPages = action == NavAction::First || action == NavAction::Last;
	if (action == NavAction::None && focus != NavFocus::TextBox &&
	    !modifiers.ctrl && !modifiers.alt && !modifiers.windows) {
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
	return handled;
}
} // namespace winrt::Sable::implementation
