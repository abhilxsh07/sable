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
#include "pdf/session.h"
#include "pdf/surface.h"
#include "pdf/surface_util.h"
#include <algorithm>
#include <chrono>
#include <winrt/Windows.System.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::Sable::implementation {
void ViewerPage::invalidateFind() {
	if (m_surface) {
		m_surface->invalidate({0, 0, static_cast<LONG>(pdf::MAX_SURFACE_SIDE),
		                       static_cast<LONG>(pdf::MAX_SURFACE_SIDE)});
	}
}
bool ViewerPage::findOpen() {
	return FindBar().Visibility() == Microsoft::UI::Xaml::Visibility::Visible;
}
void ViewerPage::setupFind() {
	const auto weak = get_weak();
	m_findTimer = DispatcherQueue().CreateTimer();
	m_findTimer.IsRepeating(false);
	m_findTimer.Interval(std::chrono::milliseconds(150));
	m_findTimer.Tick([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->startFind();
		}
	});
	FindBox().TextChanged([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->changedFind();
		}
	});
	const auto changed = [weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->changedFind();
		}
	};
	for (const auto& option : {FindCase(), FindWords(), FindDiacritics()}) {
		option.Checked(changed);
		option.Unchecked(changed);
	}
	const auto highlight = [weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->m_textPrefetched.clear();
			self->prefetchSelectedText();
			self->invalidateFind();
		}
	};
	FindHighlight().Checked(highlight);
	FindHighlight().Unchecked(highlight);
	FindPrevious().Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->navigateFind(-1);
		}
	});
	FindNext().Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->navigateFind(1);
		}
	});
	FindClose().Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->closeFind();
		}
	});
	FindBox().KeyDown([weak](auto&&, const Input::KeyRoutedEventArgs& args) {
		if (auto self = weak.get();
		    self && args.Key() == Windows::System::VirtualKey::Enter) {
			self->navigateFind((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
			args.Handled(true);
		}
	});
	showFindCount();
}
void ViewerPage::openFind() {
	if (m_closed || m_presentation || !m_layout || !m_findTimer) {
		return;
	}
	stopPan();
	const bool wasOpen = findOpen();
	FindBar().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
	FindBox().Focus(FocusState::Programmatic);
	FindBox().SelectAll();
	if (!wasOpen) {
		changedFind();
	}
}
void ViewerPage::closeFind() {
	if (m_findTimer) {
		m_findTimer.Stop();
	}
	if (m_session) {
		m_session->cancelSearch();
	}
	++m_findVersion;
	m_finding = false;
	m_findActive.reset();
	m_findNeedsReveal = false;
	m_findMatches.clear();
	m_findPages.clear();
	if (findOpen()) {
		Scroller().Focus(FocusState::Programmatic);
		FindBar().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
	}
	invalidateFind();
}
void ViewerPage::changedFind() {
	if (m_closed || !findOpen()) {
		return;
	}
	m_session->cancelSearch();
	++m_findVersion;
	m_findTimer.Stop();
	m_findActive.reset();
	m_findNeedsReveal = false;
	m_findMatches.clear();
	m_findPages.clear();
	m_findFailed = false;
	m_findLimited = false;
	m_finding = !FindBox().Text().empty();
	m_findReverse = false;
	m_findStartPage = m_current;
	m_textPrefetched.clear();
	invalidateFind();
	showFindCount();
	if (m_finding) {
		m_findTimer.Start();
	}
}
void ViewerPage::startFind() {
	if (m_closed || !findOpen() || !m_session) {
		return;
	}
	m_findTimer.Stop();
	const auto text = FindBox().Text();
	if (text.empty()) {
		return;
	}
	std::u16string query;
	query.reserve(text.size());
	for (const auto c : text) {
		query += static_cast<char16_t>(c);
	}
	const pdf::FindOptions options{
	    unbox_value_or<bool>(FindCase().IsChecked(), false),
	    unbox_value_or<bool>(FindWords().IsChecked(), false),
	    unbox_value_or<bool>(FindDiacritics().IsChecked(), false)};
	const auto version = m_findVersion;
	const auto session = m_session.get();
	try {
		if (session->search(
		        std::move(query), options,
		        [weak = get_weak(), queue = DispatcherQueue(), version,
		         session](pdf::FindUpdate update) {
			        const auto reply =
			            std::make_shared<pdf::FindUpdate>(std::move(update));
			        queue.TryEnqueue([weak, reply, version, session] {
				        if (auto self = weak.get();
				            self && !self->m_closed &&
				            self->m_findVersion == version &&
				            self->m_session.get() == session) {
					        self->applyFind(std::move(*reply));
				        }
			        });
		        })) {
			return;
		}
	} catch (...) {
	}
	m_finding = false;
	m_findFailed = true;
	showFindCount();
}
void ViewerPage::applyFind(pdf::FindUpdate update) {
	if (update.canceled) {
		return;
	}
	m_finding = !update.done;
	m_findFailed = update.failed;
	m_findLimited = update.limited;
	if (update.failed) {
		m_findMatches.clear();
		m_findPages.clear();
		m_findActive.reset();
	}
	for (const auto& match : update.matches) {
		m_findPages[match.page].push_back(m_findMatches.size());
		m_findMatches.push_back(match);
	}
	if (!m_findActive && !m_findMatches.empty()) {
		auto chosen = m_findMatches.end();
		if (m_findReverse) {
			if (update.done ||
			    update.pagesScanned > static_cast<int>(m_findStartPage)) {
				const auto after = std::find_if(
				    m_findMatches.begin(), m_findMatches.end(),
				    [this](const auto& match) {
					    return match.page > static_cast<int>(m_findStartPage);
				    });
				if (after != m_findMatches.begin()) {
					chosen = std::prev(after);
				} else if (update.done) {
					chosen = std::prev(m_findMatches.end());
				}
			}
		} else {
			chosen = std::find_if(m_findMatches.begin(), m_findMatches.end(),
			                      [this](const auto& match) {
				                      return match.page >=
				                             static_cast<int>(m_findStartPage);
			                      });
			if (chosen == m_findMatches.end() && update.done) {
				chosen = m_findMatches.begin();
			}
		}
		if (chosen != m_findMatches.end()) {
			m_findActive =
			    static_cast<std::size_t>(chosen - m_findMatches.begin());
			m_findNeedsReveal = true;
		}
	}
	showFindCount();
	revealFindMatch();
	prefetchSelectedText();
	invalidateFind();
}
void ViewerPage::navigateFind(int direction) {
	if (m_closed || !m_layout) {
		return;
	}
	if (!findOpen()) {
		openFind();
		m_findReverse = direction < 0;
		return;
	}
	if (m_findTimer.IsRunning()) {
		m_findReverse = direction < 0;
		startFind();
		return;
	}
	if (!m_findActive || m_findMatches.empty()) {
		m_findReverse = direction < 0;
		return;
	}
	const auto count = m_findMatches.size();
	// The unscanned pages may still contain the next/previous match.
	if (m_finding && ((direction > 0 && *m_findActive + 1 == count) ||
	                  (direction < 0 && *m_findActive == 0))) {
		return;
	}
	m_findActive = direction < 0 ? (*m_findActive + count - 1) % count
	                             : (*m_findActive + 1) % count;
	m_findNeedsReveal = true;
	showFindCount();
	revealFindMatch();
	prefetchSelectedText();
	invalidateFind();
}
void ViewerPage::showFindCount() {
	const auto count = m_findMatches.size();
	if (m_findFailed) {
		FindCount().Text(L"Search failed");
	} else if (FindBox().Text().empty()) {
		FindCount().Text(L"");
	} else if (!count) {
		FindCount().Text(m_finding ? L"Searching…" : L"Phrase not found");
	} else {
		FindCount().Text(
		    (m_findActive ? to_hstring(*m_findActive + 1) + L" of " : L"") +
		    to_hstring(count) +
		    (m_findLimited ? L"+"
		     : m_finding   ? L"…"
		                   : L""));
	}
	FindPrevious().IsEnabled(count > 0 &&
	                         (!m_finding || m_findActive.value_or(0) > 0));
	FindNext().IsEnabled(
	    count > 0 &&
	    (!m_finding || (m_findActive && *m_findActive + 1 < count)));
}
bool ViewerPage::hasFindOnPage(int page) {
	if (!findOpen()) {
		return false;
	}
	if (unbox_value_or<bool>(FindHighlight().IsChecked(), true)) {
		return m_findPages.contains(page);
	}
	return m_findActive && m_findMatches[*m_findActive].page == page;
}
void ViewerPage::revealFindMatch() {
	if (!m_findNeedsReveal || !m_findActive || !m_layout || m_closed ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
		return;
	}
	const auto match = m_findMatches[*m_findActive];
	const auto found = m_text.find(match.page);
	if (found == m_text.end() && !m_textFailed.contains(match.page)) {
		requestText(match.page);
		return;
	}
	if (!ensurePageLayout(static_cast<std::size_t>(match.page))) {
		return;
	}
	m_current = static_cast<std::size_t>(match.page);
	showPosition();
	const auto& page = m_layout->pages()[match.page];
	double x = page.x, y = page.y;
	if (found != m_text.end()) {
		const auto rects = pdf::textRects(*found->second.text, match.first,
		                                  match.first + match.count);
		if (!rects.empty()) {
			const auto box = viewBox(rects.front());
			x += box.left * page.width;
			y += box.top * page.height;
		}
	}
	m_findNeedsReveal = false;
	jumpTo((std::max)(0., imageLeft() + x - 32), (std::max)(0., y - 32));
}
void ViewerPage::drawFind(ID2D1DeviceContext& context,
                          helpers::PageRange pages) {
	if (!findOpen()) {
		return;
	}
	for (auto page = pages.first; page < pages.end; ++page) {
		const auto matches = m_findPages.find(static_cast<int>(page));
		const auto text = m_text.find(static_cast<int>(page));
		if (matches == m_findPages.end() || text == m_text.end()) {
			continue;
		}
		const auto at = pixels(page);
		for (const auto index : matches->second) {
			const bool active = m_findActive == index;
			if (!active &&
			    !unbox_value_or<bool>(FindHighlight().IsChecked(), true)) {
				continue;
			}
			const auto& match = m_findMatches[index];
			for (const auto& original :
			     pdf::textRects(*text->second.text, match.first,
			                    match.first + match.count)) {
				const auto box = viewBox(original);
				context.FillRectangle(
				    D2D1::RectF(
				        static_cast<float>(at.x + box.left * at.width),
				        static_cast<float>(at.y + box.top * at.height),
				        static_cast<float>(at.x + box.right * at.width),
				        static_cast<float>(at.y + box.bottom * at.height)),
				    active ? m_device.findActive.get()
				           : m_device.findMatch.get());
			}
		}
	}
}
} // namespace winrt::Sable::implementation
