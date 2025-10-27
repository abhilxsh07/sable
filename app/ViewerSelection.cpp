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
#include <algorithm>
#include <cmath>
#include <limits>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
void ViewerPage::setupSelection() {
	const auto weak = get_weak();
	Pages().PointerPressed([weak](auto&&,
	                              const Input::PointerRoutedEventArgs& args) {
		if (auto self = weak.get()) {
			if (self->m_notePlacing) {
				args.Handled(args.Pointer().PointerDeviceType() !=
				             Microsoft::UI::Input::PointerDeviceType::Touch);
				return;
			}
			const auto point = args.GetCurrentPoint(self->Pages());
			if (args.Pointer().PointerDeviceType() ==
			        Microsoft::UI::Input::PointerDeviceType::Touch ||
			    !point.Properties().IsLeftButtonPressed()) {
				return;
			}
			self->Scroller().Focus(FocusState::Programmatic);
			self->beginSelection(point.Position());
			if (self->m_textStart) {
				self->Pages().CapturePointer(args.Pointer());
			}
		}
	});
	Pages().PointerMoved(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& args) {
		    if (auto self = weak.get(); self && self->m_textPointerDown) {
			    self->extendSelection(
			        args.GetCurrentPoint(self->Pages()).Position());
			    args.Handled(self->m_textMoved);
		    }
	    });
	Pages().PointerReleased(
	    [weak](auto&&, const Input::PointerRoutedEventArgs& args) {
		    if (auto self = weak.get(); self && self->m_textPointerDown) {
			    self->extendSelection(
			        args.GetCurrentPoint(self->Pages()).Position());
			    self->endSelection();
			    self->Pages().ReleasePointerCaptures();
			    args.Handled(self->m_ignoreTextTap);
		    }
	    });
	Pages().PointerCaptureLost([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->endSelection();
		}
	});
	Pages().PointerCanceled([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->cancelSelectionDrag();
		}
	});
	m_textScroll = DispatcherQueue().CreateTimer();
	m_textScroll.Interval(std::chrono::milliseconds(30));
	m_textScroll.Tick([weak](auto&&, auto&&) {
		const auto self = weak.get();
		if (!self || self->m_closed || !self->m_textPointerDown ||
		    !self->m_textMoved ||
		    self->Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
			if (self) {
				self->cancelSelectionDrag();
			}
			return;
		}
		const auto delta = [](double point, double extent) {
			if (point < 24) {
				return (std::max)(-40., (point - 24) * .5);
			}
			if (point > extent - 24) {
				return (std::min)(40., (point - extent + 24) * .5);
			}
			return 0.;
		};
		const double x =
		    delta(self->m_textViewport.X, self->Scroller().ViewportWidth());
		const double y =
		    delta(self->m_textViewport.Y, self->Scroller().ViewportHeight());
		if (x || y) {
			self->Scroller().ChangeView(
			    (std::max)(0., self->Scroller().HorizontalOffset() + x),
			    (std::max)(0., self->Scroller().VerticalOffset() + y), nullptr,
			    true);
			self->m_textPoint = self->Scroller()
			                        .TransformToVisual(self->Pages())
			                        .TransformPoint(self->m_textViewport);
			self->updateSelection();
		}
	});
	m_textMenu = MenuFlyout();
	m_textCopy = MenuFlyoutItem();
	m_textCopy.Text(L"Copy");
	m_textCopy.KeyboardAcceleratorTextOverride(L"Ctrl+C");
	m_textCopy.Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->copySelection();
		}
	});
	MenuFlyoutItem all;
	all.Text(L"Select All");
	all.KeyboardAcceleratorTextOverride(L"Ctrl+A");
	all.Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->selectAllText();
		}
	});
	m_textMenu.Items().Append(m_textCopy);
	m_textMenu.Items().Append(all);
	m_textMenu.Opening([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->m_textCopy.IsEnabled(self->canCopySelection());
		}
	});
	Pages().ContextFlyout(m_textMenu);
}
bool ViewerPage::hasTextSelection() const {
	return !m_closed && m_selection.has_value();
}
bool ViewerPage::canCopySelection() const {
	return hasTextSelection() && m_copyAllowed && !m_copying;
}
void ViewerPage::invalidateSelection() {
	if (!m_layout || !m_surface) {
		return;
	}
	const auto visible = m_layout->visiblePages(Scroller().VerticalOffset(),
	                                            Scroller().ViewportHeight(), 0);
	for (auto i = visible.first; i < visible.end; ++i) {
		invalidate(i);
	}
}
void ViewerPage::clearTextSelection() {
	cancelSelectionDrag();
	m_selection.reset();
	++m_selectionVersion;
	invalidateSelection();
}
void ViewerPage::cancelSelectionDrag() {
	m_textPointerDown = false;
	m_textStart.reset();
	m_textPoint.reset();
	m_textAnchor.reset();
	if (m_textScroll) {
		m_textScroll.Stop();
	}
	Pages().ReleasePointerCaptures();
}
void ViewerPage::beginSelection(Windows::Foundation::Point point) {
	if (m_closed || !m_layout || !m_session ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
		return;
	}
	clearTextSelection();
	m_ignoreTextTap = false;
	const auto page = m_layout->pageAt(point.Y);
	const auto& bounds = m_layout->pages()[page];
	if (point.X < bounds.x || point.X > bounds.x + bounds.width ||
	    point.Y < bounds.y || point.Y > bounds.y + bounds.height) {
		return;
	}
	m_textStart = m_textPoint = point;
	m_textMoved = false;
	m_textPointerDown = true;
	m_textViewport =
	    Pages().TransformToVisual(Scroller()).TransformPoint(point);
	updateSelection();
}
void ViewerPage::extendSelection(Windows::Foundation::Point point) {
	if (!m_textStart || !m_textPointerDown || m_closed) {
		return;
	}
	m_textPoint = point;
	m_textViewport =
	    Pages().TransformToVisual(Scroller()).TransformPoint(point);
	if (std::hypot(point.X - m_textStart->X, point.Y - m_textStart->Y) > 3) {
		m_textMoved = true;
		m_ignoreTextTap = true;
		m_textScroll.Start();
	}
	updateSelection();
}
void ViewerPage::endSelection() {
	if (!m_textPointerDown) {
		return;
	}
	m_textPointerDown = false;
	if (m_textScroll) {
		m_textScroll.Stop();
	}
	if (!m_textMoved) {
		m_textStart.reset();
		m_textPoint.reset();
		m_textAnchor.reset();
		return;
	}
	updateSelection();
}
void ViewerPage::updateSelection() {
	if (!m_textStart || !m_textPoint || !m_layout || m_closed) {
		return;
	}
	if (Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
		cancelSelectionDrag();
		return;
	}
	if (!m_textAnchor) {
		const auto page = static_cast<int>(m_layout->pageAt(m_textStart->Y));
		const auto found = m_text.find(page);
		if (found == m_text.end()) {
			if (m_textFailed.contains(page)) {
				cancelSelectionDrag();
			} else {
				requestText(page);
			}
			return;
		}
		found->second.used = ++m_textClock;
		const auto& rect = m_layout->pages()[page];
		const auto hit = pdf::hitText(
		    *found->second.text, (m_textStart->X - rect.x) / rect.width,
		    (m_textStart->Y - rect.y) / rect.height, rect.width, rect.height);
		if (!hit) {
			cancelSelectionDrag();
			return;
		}
		m_textAnchor = pdf::TextPosition{page, *hit};
	}
	if (!m_textMoved) {
		return;
	}
	const auto page = static_cast<int>(m_layout->pageAt(m_textPoint->Y));
	const auto found = m_text.find(page);
	if (found == m_text.end()) {
		if (m_textFailed.contains(page)) {
			cancelSelectionDrag();
		} else {
			requestText(page);
		}
		return;
	}
	found->second.used = ++m_textClock;
	const auto& rect = m_layout->pages()[page];
	const auto hit = pdf::hitText(
	    *found->second.text, (m_textPoint->X - rect.x) / rect.width,
	    (m_textPoint->Y - rect.y) / rect.height, rect.width, rect.height, true);
	const pdf::TextSelection next{*m_textAnchor, {page, hit.value_or(0)}};
	if (m_selection != next) {
		m_selection = next;
		++m_selectionVersion;
		invalidateSelection();
	}
	if (!m_textPointerDown) {
		m_textStart.reset();
		m_textPoint.reset();
		m_textAnchor.reset();
	}
	prefetchSelectedText();
}
void ViewerPage::requestText(int page) {
	if (m_closed || !m_session || m_textPending || m_text.contains(page) ||
	    m_textFailed.contains(page)) {
		return;
	}
	m_textPending = page;
	const auto session = m_session.get();
	try {
		if (session->textPage(page, [weak = get_weak(),
		                             queue = DispatcherQueue(),
		                             session](pdf::TextResult result) {
			    const auto reply =
			        std::make_shared<pdf::TextResult>(std::move(result));
			    queue.TryEnqueue([weak, session, reply] {
				    const auto self = weak.get();
				    if (!self || self->m_closed ||
				        self->m_session.get() != session) {
					    return;
				    }
				    self->m_textPending.reset();
				    if (reply->text && !reply->canceled) {
					    self->m_text[reply->page] = {
					        std::make_shared<pdf::TextPage>(
					            std::move(*reply->text)),
					        ++self->m_textClock};
					    for (;;) {
						    std::size_t bytes = 0;
						    for (const auto& [index, entry] : self->m_text) {
							    bytes += entry.text->characters.size() *
							             sizeof(pdf::TextChar);
						    }
						    if (bytes <= 24 * 1024 * 1024 &&
						        self->m_text.size() <= 64) {
							    break;
						    }
						    auto oldest = self->m_text.end();
						    for (auto it = self->m_text.begin();
						         it != self->m_text.end(); ++it) {
							    if (it->first == reply->page ||
							        (self->m_textAnchor &&
							         it->first == self->m_textAnchor->page) ||
							        (self->m_selection &&
							         (it->first ==
							              self->m_selection->anchor.page ||
							          it->first ==
							              self->m_selection->focus.page))) {
								    continue;
							    }
							    if (oldest == self->m_text.end() ||
							        it->second.used < oldest->second.used) {
								    oldest = it;
							    }
						    }
						    if (oldest == self->m_text.end()) {
							    break;
						    }
						    self->m_text.erase(oldest);
					    }
					    self->invalidate(reply->page);
				    } else {
					    self->m_textFailed.insert(reply->page);
					    if (self->m_textStart && !reply->canceled) {
						    self->showNotice(
						        L"Couldn't read text on this page.");
					    }
				    }
				    self->updateSelection();
				    self->revealFindMatch();
				    self->prefetchSelectedText();
			    });
		    })) {
			return;
		}
	} catch (...) {
	}
	m_textPending.reset();
	m_textFailed.insert(page);
}
void ViewerPage::prefetchSelectedText() {
	if (m_closed || !m_layout || (!m_selection && m_findMatches.empty()) ||
	    m_textPending ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
		return;
	}
	const auto visible = m_layout->visiblePages(Scroller().VerticalOffset(),
	                                            Scroller().ViewportHeight(), 0);
	if (visible != m_textView) {
		m_textView = visible;
		m_textPrefetched.clear();
	}
	const auto [first, last] = pdf::ordered(
	    m_selection.value_or(pdf::TextSelection{{-1, 0}, {-1, 0}}));
	for (auto page = visible.first; page < visible.end; ++page) {
		const int index = static_cast<int>(page);
		if (((index >= first.page && index <= last.page) ||
		     hasFindOnPage(index)) &&
		    !m_text.contains(index) && !m_textFailed.contains(index) &&
		    m_textPrefetched.insert(index).second) {
			requestText(index);
			return;
		}
	}
}
void ViewerPage::selectAllText() {
	if (m_closed || !m_layout || m_sizes.empty()) {
		return;
	}
	cancelSelectionDrag();
	m_selection = pdf::TextSelection{{0, 0},
	                                 {static_cast<int>(m_sizes.size() - 1),
	                                  (std::numeric_limits<int>::max)()}};
	++m_selectionVersion;
	invalidateSelection();
	prefetchSelectedText();
}
void ViewerPage::copySelection() {
	if (!canCopySelection() || !m_session ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
		return;
	}
	const auto version = m_selectionVersion;
	const auto session = m_session.get();
	m_copying = true;
	try {
		if (session->copyText(*m_selection, [weak = get_weak(),
		                                     queue = DispatcherQueue(), session,
		                                     version](pdf::CopyResult result) {
			    const auto reply =
			        std::make_shared<pdf::CopyResult>(std::move(result));
			    queue.TryEnqueue([weak, session, version, reply] {
				    const auto self = weak.get();
				    if (!self || self->m_closed ||
				        self->m_session.get() != session) {
					    return;
				    }
				    self->m_copying = false;
				    if (version != self->m_selectionVersion ||
				        reply->canceled ||
				        self->Visibility() !=
				            Microsoft::UI::Xaml::Visibility::Visible) {
					    return;
				    }
				    try {
					    if (reply->failed) {
						    self->showNotice(
						        L"Couldn't copy the selected text.");
						    return;
					    }
					    if (reply->text.empty()) {
						    return;
					    }
#ifdef SABLE_SURFACE_TEST
					    if (self->m_copySink) {
						    self->m_copySink(reply->text);
						    return;
					    }
#endif
					    using namespace Windows::ApplicationModel::DataTransfer;
					    DataPackage data;
					    data.RequestedOperation(DataPackageOperation::Copy);
					    data.SetText(to_hstring(reply->text));
					    Clipboard::SetContent(data);
					    Clipboard::Flush();
				    } catch (...) {
					    self->showNotice(
					        L"Couldn't copy text to the clipboard. Try again.");
				    }
			    });
		    }))
			return;
	} catch (...) {
	}
	m_copying = false;
	showNotice(L"Couldn't copy the selected text.");
}
} // namespace winrt::Sable::implementation
