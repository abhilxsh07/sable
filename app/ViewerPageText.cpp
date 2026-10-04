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
#include "PageText.h"
#include "helpers/logging.h"
#include "pdf/session.h"

using namespace winrt;

namespace winrt::Sable::implementation {
// Screen readers get the current page as a UI Automation document. Its
// name and text arrive together, fetched only after a UI
// Automation client has asked for the peer, so nobody else pays for it.
void ViewerPage::updatePageText() {
	if (m_closed || !m_layout) {
		return;
	}
	const auto host = PageReader();
	auto& impl = *get_self<implementation::PageTextHost>(host);
	if (!impl.onWanted) {
		impl.onWanted = [weak = get_weak()] {
			if (const auto self = weak.get()) {
				self->m_pageTextPage.reset();
				self->DispatcherQueue().TryEnqueue([weak] {
					if (const auto page = weak.get()) {
						page->updatePageText();
					}
				});
			}
		};
	}
	const auto count = m_layout->pages().size();
	const auto page = m_current;
	const auto name =
	    L"Page " + to_hstring(page + 1) + L" of " + to_hstring(count);
	if (!impl.wanted()) {
		impl.publish(name, L"");
		return;
	}
	if (!m_session || m_pageTextPage == page) {
		return;
	}
	m_pageTextPage = page;
	const auto version = ++m_pageTextVersion;
	const auto session = m_session.get();
	try {
		if (session->documentText(
		        {static_cast<int>(page)},
		        [weak = get_weak(), queue = DispatcherQueue(), version, session,
		         page](pdf::DocumentTextResult result) {
			        queue.TryEnqueue([weak, version, session, page,
			                          result = std::move(result)]() mutable {
				        if (const auto self = weak.get()) {
					        self->applyPageText(version, session, page,
					                            std::move(result));
				        }
			        });
		        })) {
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Warning,
		                    "Could not queue the page text for UI Automation");
	}
	m_pageTextPage.reset();
	impl.publish(name, L"");
}
void ViewerPage::applyPageText(std::uint64_t version, pdf::Session* session,
                               std::size_t page,
                               pdf::DocumentTextResult result) {
	if (m_closed || !m_layout || m_session.get() != session ||
	    m_pageTextVersion != version || m_pageTextPage != page ||
	    m_current != page) {
		return;
	}
	if (result.canceled) {
		m_pageTextPage.reset();
		return;
	}
	const auto name = L"Page " + to_hstring(page + 1) + L" of " +
	                  to_hstring(m_layout->pages().size());
	// a failed read or a PDF that forbids copying exposes no page text
	get_self<implementation::PageTextHost>(PageReader())
	    ->publish(name, result.text ? to_hstring(*result.text) : hstring());
	if (!result.text) {
		m_pageTextPage.reset();
	}
}
} // namespace winrt::Sable::implementation
