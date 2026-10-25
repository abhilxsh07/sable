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
#include "pdf/surface.h"
#include "pdf/surface_util.h"
#include <algorithm>
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::Sable::implementation {
helpers::PageLayout
ViewerPage::makeLayout(std::span<const helpers::PageSize> sizes, double zoom) {
	const double width = Scroller().ActualWidth();
	return helpers::PageLayout(
	    sizes, zoom, m_scrollMode == helpers::ScrollMode::Wrapped ? width : 0.,
	    PAGE_GAP, PAGE_MARGIN, {m_scrollMode, m_spreadMode, m_current, width});
}
double ViewerPage::fittedZoom(std::span<const helpers::PageSize> sizes) {
	const auto spread =
	    helpers::spreadPages(sizes.size(), m_current, m_spreadMode);
	if (m_presentation) {
		const auto size = helpers::spreadSize(sizes, m_current, m_spreadMode);
		const double gap = spread.end - spread.first > 1 ? PAGE_GAP : 0.;
		return (std::min)({helpers::MAX_ZOOM,
		                   (std::max)(1., Scroller().ActualWidth() - 20 - gap) /
		                       (size.width * (96. / 72.)),
		                   (std::max)(1., Scroller().ActualHeight() - 20) /
		                       (size.height * (96. / 72.))});
	}
	return helpers::presetZoom(
	    m_preset, helpers::spreadSize(sizes, m_current, m_spreadMode),
	    Scroller().ActualWidth() -
	        (spread.end - spread.first > 1 ? PAGE_GAP : 0.),
	    Scroller().ActualHeight());
}
helpers::PageRect ViewerPage::viewport() {
	const double factor = Scroller().ZoomFactor();
	const auto [x, y] = m_landingAt.value_or(
	    std::pair{Scroller().HorizontalOffset(), Scroller().VerticalOffset()});
	return {x / factor - imageLeft(), y / factor,
	        Scroller().ViewportWidth() / factor,
	        Scroller().ViewportHeight() / factor};
}
void ViewerPage::setLayout(helpers::ScrollMode scroll,
                           helpers::SpreadMode spread) {
	if (m_presentation || m_directManipulation || m_resettingGesture ||
	    std::abs(Scroller().ZoomFactor() - 1.f) > 1e-5f) {
		return;
	}
	if (m_closed || !m_layout || !m_surface ||
	    (scroll == m_scrollMode && spread == m_spreadMode)) {
		return;
	}
	if (scroll < helpers::ScrollMode::Vertical ||
	    scroll > helpers::ScrollMode::Page ||
	    spread < helpers::SpreadMode::None ||
	    spread > helpers::SpreadMode::Even) {
		return;
	}
	const auto page = m_current;
	const auto oldHold = m_holdCurrent;
	const auto oldScroll = m_scrollMode;
	const auto oldSpread = m_spreadMode;
	m_holdCurrent = true;
	m_scrollMode = scroll;
	m_spreadMode = spread;
	try {
		const auto sizes = displaySizes();
		const double zoom =
		    m_preset == helpers::ZoomPreset::None ? m_zoom : fittedZoom(sizes);
		if (!relayout(zoom)) {
			m_holdCurrent = oldHold;
			m_scrollMode = oldScroll;
			m_spreadMode = oldSpread;
			showNotice(L"This layout is too large at the current zoom.");
			return;
		}
		++m_linkVersion;
		m_linkPending = false;
		++m_outlineLinkVersion;
		m_outlineLinkPending = false;
		settle();
		Scroller().UpdateLayout();
		m_holdCurrent = oldHold;
		goToPage(page);
		updateVisible();
	} catch (...) {
		m_holdCurrent = oldHold;
		m_scrollMode = oldScroll;
		m_spreadMode = oldSpread;
		showNotice(L"Couldn't change the page layout.");
	}
}
bool ViewerPage::ensurePageLayout(std::size_t page) {
	if (!m_layout || page >= m_sizes.size()) {
		return false;
	}
	if (m_scrollMode != helpers::ScrollMode::Page || m_layout->contains(page)) {
		return true;
	}
	const auto previous = m_current;
	const auto oldHold = std::exchange(m_holdCurrent, true);
	m_current = page;
	try {
		const double zoom = m_preset == helpers::ZoomPreset::None
		                        ? m_zoom
		                        : fittedZoom(displaySizes());
		if (!relayout(zoom)) {
			m_holdCurrent = oldHold;
			m_current = previous;
			return false;
		}
		settle();
		Scroller().UpdateLayout();
		m_holdCurrent = oldHold;
		return true;
	} catch (...) {
		m_holdCurrent = oldHold;
		m_current = previous;
		return false;
	}
}
void ViewerPage::reflowViewport() {
	if (m_closed || !m_layout || m_directManipulation || m_resettingGesture ||
	    std::abs(Scroller().ZoomFactor() - 1.f) > 1e-5f) {
		return;
	}
	if (helpers::followsViewport(m_preset)) {
		applyPreset();
	}
	const double width = Scroller().ActualWidth();
	const bool wrapped = m_scrollMode == helpers::ScrollMode::Wrapped;
	// once the widest row doesn't fit, rows centre in the visible width
	// rather than in the pages, so a resize moves them too
	const bool centred =
	    m_scrollMode != helpers::ScrollMode::Horizontal &&
	    m_layout->width() > (std::min)(m_layoutViewWidth, width) + .5;
	if ((wrapped || centred) && std::abs(m_layoutViewWidth - width) > .5) {
		const auto page = m_current;
		try {
			if (relayout(m_zoom)) {
				settle();
				Scroller().UpdateLayout();
				// only wrapping moves pages to other rows; the rest keep
				// their height, so the reading position stays put
				if (wrapped) {
					goToPage(page);
				}
			}
		} catch (...) {
			showNotice(L"Couldn't resize this page layout.");
		}
	}
	updateVisible();
}
void ViewerPage::advancePage(int direction) {
	if (!m_layout || m_sizes.empty()) {
		return;
	}
	auto group = helpers::spreadPages(m_sizes.size(), m_current, m_spreadMode);
	// In horizontal scrolling the next button advances one page, as upstream.
	if (m_scrollMode == helpers::ScrollMode::Horizontal) {
		group = {m_current, m_current + 1};
	}
	if (m_scrollMode == helpers::ScrollMode::Wrapped) {
		group = m_layout->rowPages(m_current);
	}
	const auto view = viewport();
	for (auto i = group.first; i < group.end; ++i) {
		const auto& rect = m_layout->pages()[i];
		if (rect.x < view.x - 1 ||
		    rect.x + rect.width > view.x + view.width + 1 ||
		    rect.y + rect.height <= view.y || rect.y >= view.y + view.height) {
			group = {m_current, m_current + 1};
			break;
		}
	}

	if (direction < 0 && m_current > 0) {
		goToPage(group.first > 0 ? group.first - 1 : 0);
	}
	if (direction > 0 && m_current + 1 < m_sizes.size()) {
		goToPage((std::min)(group.end, m_sizes.size() - 1));
	}
}
bool ViewerPage::spreadClicksEnabled() const {
	return !m_closed && m_layout && !m_presentation &&
	       m_scrollMode == helpers::ScrollMode::Page &&
	       m_spreadMode != helpers::SpreadMode::None;
}
int ViewerPage::spreadClickDirection(Windows::Foundation::Point point) {
	if (!spreadClicksEnabled() || m_directManipulation || m_resettingGesture) {
		return 0;
	}
	const auto page = m_layout->pageAt(point.X, point.Y);
	const auto& rect = m_layout->pages()[page];
	if (point.X < rect.x || point.X > rect.x + rect.width || point.Y < rect.y ||
	    point.Y > rect.y + rect.height) {
		return 0;
	}
	const auto group =
	    helpers::spreadPages(m_sizes.size(), m_current, m_spreadMode);
	if (group.end - group.first == 2) {
		return page == group.first ? -1 : 1;
	}
	// A lone cover or final page still has a back half and a forward half.
	return point.X < rect.x + rect.width / 2 ? -1 : 1;
}
void ViewerPage::turnSpread(int direction) {
	if (!spreadClicksEnabled()) {
		return;
	}
	const auto group =
	    helpers::spreadPages(m_sizes.size(), m_current, m_spreadMode);
	if (direction < 0 && group.first > 0) {
		goToPage(group.first - 1);
	} else if (direction > 0 && group.end < m_sizes.size()) {
		goToPage(group.end);
	}
}
} // namespace winrt::Sable::implementation
