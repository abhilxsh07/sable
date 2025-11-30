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
#include "pdf/page_filter.h"
#include "pdf/session.h"
#include "pdf/surface.h"
#include "pdf/surface_util.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
std::vector<helpers::PageSize> ViewerPage::displaySizes() const {
	auto sizes = m_sizes;
	for (auto& size : sizes) {
		size = helpers::rotatedSize(size, m_rotation);
	}
	return sizes;
}

// see: https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/app.js
// see:
// https://raw.githubusercontent.com/mozilla/pdf.js/v4.2.67/web/pdf_viewer.js
void ViewerPage::rotateView(int turns) {
	if (m_presentation || m_directManipulation || m_resettingGesture ||
	    std::abs(Scroller().ZoomFactor() - 1.f) > 1e-5f) {
		return;
	}
	if (m_closed || !m_layout || !m_surface || !m_session ||
	    helpers::quarterTurns(turns) == 0) {
		return;
	}
	stopPan();
	const auto page = m_current;
	try {
		const auto rotation =
		    helpers::quarterTurns(m_rotation + helpers::quarterTurns(turns));
		auto sizes = m_sizes;
		for (auto& size : sizes) {
			size = helpers::rotatedSize(size, rotation);
		}
		helpers::ThumbnailStrip strip(sizes);
		const double zoom =
		    m_preset == helpers::ZoomPreset::None ? m_zoom : fittedZoom(sizes);
		auto next = makeLayout(sizes, zoom);
		const auto extent =
		    pdf::scaledSurfaceSize(next.width(), next.height(), m_scale);
		if (!extent) {
			showNotice(L"This document is too large to rotate at this zoom.");
			return;
		}
		m_holdCurrent = true;
		m_surface->resize(extent->cx, extent->cy);
		// commit the geometry and its pixels before calling back into XAML
		m_layout = std::move(next);
		m_strip = std::move(strip);
		m_zoom = zoom;
		m_rotation = rotation;
		++m_rotationVersion;
		++m_outlineLinkVersion;
		m_outlineLinkPending = false;
		m_device.filter->setInput(nullptr);
		m_device.bitmaps.clear();
		m_failed.clear();
		settle();
		cancelSelectionDrag();
		// a square or a half turn needs new pixels too
		Scroller().Focus(FocusState::Programmatic);
		Thumbnails().Children().Clear();
		m_thumbs.clear();
		m_spareThumbs.clear();
		m_thumbLoaded.clear();
		m_hoverThumb.reset();
		m_thumbCurrent.reset();
		Thumbnails().Height(m_strip->height());
		Pages().Width(extent->cx / m_scale);
		Pages().Height(extent->cy / m_scale);
		Scroller().UpdateLayout();
		m_holdCurrent = false;
		goToPage(page);
		updateThumbnails();
		updateVisible();
	} catch (...) {
		m_holdCurrent = false;
		showNotice(L"Couldn't rotate this view.");
	}
}

pdf::TextRect ViewerPage::viewBox(const pdf::TextRect& box) const {
	const auto rotated = helpers::rotateRect(
	    {box.left, box.top, box.right - box.left, box.bottom - box.top},
	    m_rotation);
	return {rotated.x, rotated.y, rotated.x + rotated.width,
	        rotated.y + rotated.height};
}

std::optional<int> ViewerPage::hitText(int page,
                                       Windows::Foundation::Point point,
                                       bool nearest) const {
	const auto found = m_text.find(page);
	if (found == m_text.end() || !m_layout) {
		return std::nullopt;
	}
	const auto& rect = m_layout->pages()[page];
	const auto source = helpers::rotatePoint(
	    {(point.X - rect.x) / rect.width, (point.Y - rect.y) / rect.height},
	    -m_rotation);
	const auto size =
	    helpers::rotatedSize({rect.width, rect.height}, m_rotation);
	return pdf::hitText(*found->second.text, source.x, source.y, size.width,
	                    size.height, nearest);
}

pdf::LinkDestination
ViewerPage::viewDestination(pdf::LinkDestination target) const {
	if (target.fit == pdf::LinkFit::Rectangle) {
		const auto rect =
		    helpers::rotateRect({target.x.value_or(0), target.y.value_or(0),
		                         target.width, target.height},
		                        m_rotation);
		target.x = rect.x;
		target.y = rect.y;
		target.width = rect.width;
		target.height = rect.height;
	} else {
		const auto invert = [](std::optional<double> value) {
			return value ? std::optional<double>(1 - *value) : std::nullopt;
		};
		const auto x = target.x, y = target.y;
		switch (m_rotation) {
		case 1:
			target.x = invert(y);
			target.y = x;
			break;
		case 2:
			target.x = invert(x);
			target.y = invert(y);
			break;
		case 3:
			target.x = y;
			target.y = invert(x);
			break;
		default:
			break;
		}
		if (m_rotation % 2) {
			if (target.fit == pdf::LinkFit::Width) {
				target.fit = pdf::LinkFit::Height;
			} else if (target.fit == pdf::LinkFit::Height) {
				target.fit = pdf::LinkFit::Width;
			}
		}
	}
	return target;
}
} // namespace winrt::Sable::implementation
