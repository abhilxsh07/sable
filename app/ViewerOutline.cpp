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
#include <winrt/Microsoft.UI.Xaml.Media.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
bool ViewerPage::outlineOpen() {
	return OutlinePanel().Visibility() ==
	       Microsoft::UI::Xaml::Visibility::Visible;
}
bool ViewerPage::outlineHasFocus() {
	if (!outlineOpen() || !XamlRoot()) {
		return false;
	}
	auto element = Input::FocusManager::GetFocusedElement(XamlRoot())
	                   .try_as<DependencyObject>();
	while (element) {
		if (element == OutlineTree()) {
			return true;
		}
		element = Media::VisualTreeHelper::GetParent(element);
	}
	return false;
}
void ViewerPage::setupOutline() {
	const auto weak = get_weak();
	OutlineButton().Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->leavePageBox();
			self->setOutlineOpen(
			    unbox_value_or<bool>(self->OutlineButton().IsChecked(), false));
		}
	});
	OutlineRetry().Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->requestOutline();
		}
	});
	// RootNodes content mode supplies the invoked TreeViewNode.
	// https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/tree-view
	OutlineTree().ItemInvoked(
	    [weak](auto&&, const TreeViewItemInvokedEventArgs& args) {
		    if (auto self = weak.get()) {
			    const auto node = args.InvokedItem().try_as<TreeViewNode>();
			    const auto found = std::find(self->m_outlineNodes.begin(),
			                                 self->m_outlineNodes.end(), node);
			    if (found != self->m_outlineNodes.end()) {
				    self->followOutline(static_cast<std::size_t>(
				        found - self->m_outlineNodes.begin()));
			    }
		    }
	    });
}
void ViewerPage::setOutlineOpen(bool open) {
	if (m_closed || (open && !m_layout)) {
		return;
	}
	if (open) {
		setThumbnailsOpen(false);
		setAttachmentsOpen(false);
		m_sidebar = Sidebar::Outline;
	} else if (outlineOpen() && outlineHasFocus()) {
		Scroller().Focus(FocusState::Programmatic);
	}
	if (!open) {
		++m_outlineLinkVersion;
		m_outlineLinkPending = false;
	}
	OutlineButton().IsChecked(open);
	OutlinePanel().Visibility(open
	                              ? Microsoft::UI::Xaml::Visibility::Visible
	                              : Microsoft::UI::Xaml::Visibility::Collapsed);
	if (open) {
		showOutline();
	}
}
void ViewerPage::toggleSidebar() {
	if (attachmentsOpen()) {
		setAttachmentsOpen(false);
	} else if (outlineOpen()) {
		setOutlineOpen(false);
	} else if (thumbnailsOpen()) {
		setThumbnailsOpen(false);
	} else if (m_sidebar == Sidebar::Outline) {
		setOutlineOpen(true);
	} else if (m_sidebar == Sidebar::Attachments) {
		setAttachmentsOpen(true);
	} else {
		setThumbnailsOpen(true);
	}
}
void ViewerPage::closeOutline() {
	setOutlineOpen(false);
	++m_outlineVersion;
	m_outlinePending = false;
	m_outlineFailed = false;
	m_outlineBuilt = false;
	m_outline.reset();
	OutlineTree().RootNodes().Clear();
	m_outlineNodes.clear();
}
void ViewerPage::requestOutline() {
	if (m_closed || !m_session || m_outlinePending || m_outline) {
		return;
	}
	++m_outlineLinkVersion;
	m_outlineLinkPending = false;
	m_outlinePending = true;
	m_outlineFailed = false;
	const auto version = ++m_outlineVersion;
	const auto session = m_session.get();
	showOutline();
	try {
		if (session->outline([weak = get_weak(), queue = DispatcherQueue(),
		                      version, session](pdf::OutlineResult result) {
			    const auto reply =
			        std::make_shared<pdf::OutlineResult>(std::move(result));
			    queue.TryEnqueue([weak, reply, version, session] {
				    const auto self = weak.get();
				    if (!self || self->m_closed ||
				        self->m_outlineVersion != version ||
				        self->m_session.get() != session) {
					    return;
				    }
				    self->m_outlinePending = false;
				    if (reply->canceled) {
					    self->m_outline.reset();
				    } else {
					    self->m_outline = std::move(reply->outline);
				    }
				    self->m_outlineFailed = !self->m_outline;
				    self->m_outlineBuilt = false;
				    self->showOutline();
			    });
		    })) {
			return;
		}
	} catch (...) {
	}
	m_outlinePending = false;
	m_outlineFailed = true;
	showOutline();
}
void ViewerPage::showOutline() {
	if (m_closed || !outlineOpen() ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
		return;
	}
	const auto visible = Microsoft::UI::Xaml::Visibility::Visible;
	const auto collapsed = Microsoft::UI::Xaml::Visibility::Collapsed;
	OutlineRetry().Visibility(collapsed);
	if (!m_outline) {
		OutlineTree().Visibility(collapsed);
		OutlineStatus().Visibility(visible);
		if (m_outlinePending) {
			OutlineStatus().Text(L"Loading outline\u2026");
		} else if (m_outlineFailed) {
			OutlineStatus().Text(L"Couldn't load this document's outline.");
			OutlineRetry().Visibility(visible);
		} else {
			requestOutline();
		}
		return;
	}
	if (!m_outlineBuilt) {
		OutlineTree().RootNodes().Clear();
		m_outlineNodes.clear();
		m_outlineNodes.reserve(m_outline->entries.size());
		for (const auto& entry : m_outline->entries) {
			TreeViewNode node;
			node.Content(box_value(to_hstring(entry.title)));
			m_outlineNodes.push_back(node);
		}
		for (std::size_t i = 0; i < m_outlineNodes.size(); ++i) {
			const int parent = m_outline->entries[i].parent;
			if (parent >= 0 && static_cast<std::size_t>(parent) < i) {
				m_outlineNodes[parent].Children().Append(m_outlineNodes[i]);
			} else {
				OutlineTree().RootNodes().Append(m_outlineNodes[i]);
			}
		}
		for (std::size_t i = 0; i < m_outlineNodes.size(); ++i) {
			m_outlineNodes[i].IsExpanded(m_outline->entries[i].expanded);
		}
		m_outlineBuilt = true;
	}
	const bool empty = m_outlineNodes.empty();
	OutlineTree().Visibility(empty ? collapsed : visible);
	OutlineStatus().Visibility(empty || m_outline->limited ? visible
	                                                       : collapsed);
	OutlineStatus().Text(empty ? L"No document outline."
	                     : m_outline->limited
	                         ? L"Some outline entries are unavailable."
	                         : L"");
}
void ViewerPage::followOutline(std::size_t index) {
	if (m_closed || !outlineOpen() || !m_outline ||
	    index >= m_outline->entries.size() ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
		return;
	}
	const auto version = ++m_outlineLinkVersion;
	m_outlineLinkPending = false;
	if (index < m_outlineNodes.size()) {
		OutlineTree().SelectedNode(m_outlineNodes[index]);
	}
	if (!m_outline->entries[index].canFollow || !m_session) {
		return;
	}
	const auto session = m_session.get();
	const auto loaded = m_outlineVersion;
	m_outlineLinkPending = true;
	try {
		if (session->outlineLink(
		        index, [weak = get_weak(), queue = DispatcherQueue(), session,
		                version, loaded](pdf::LinkResult result) {
			        const auto reply =
			            std::make_shared<pdf::LinkResult>(std::move(result));
			        queue.TryEnqueue([weak, reply, session, version, loaded] {
				        const auto self = weak.get();
				        if (!self || self->m_closed ||
				            self->m_session.get() != session ||
				            self->m_outlineLinkVersion != version ||
				            self->m_outlineVersion != loaded) {
					        return;
				        }
				        self->m_outlineLinkPending = false;
				        if (reply->canceled || !reply->link ||
				            !self->outlineOpen() ||
				            self->Visibility() !=
				                Microsoft::UI::Xaml::Visibility::Visible) {
					        return;
				        }
				        if (reply->link->destination) {
					        self->followLink(*reply->link->destination);
				        } else if (!reply->link->uri.empty()) {
					        self->openLink(reply->link->uri);
				        }
			        });
		        })) {
			return;
		}
	} catch (...) {
	}
	m_outlineLinkPending = false;
}
} // namespace winrt::Sable::implementation
