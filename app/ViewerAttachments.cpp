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
#include "helpers/logging.h"
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using PanelVisibility = Microsoft::UI::Xaml::Visibility;
namespace winrt::Sable::implementation {
bool ViewerPage::attachmentsOpen() {
	return AttachmentPanel().Visibility() == PanelVisibility::Visible;
}
bool ViewerPage::attachmentsHasFocus() {
	if (!attachmentsOpen() || !XamlRoot()) {
		return false;
	}
	auto element = Input::FocusManager::GetFocusedElement(XamlRoot())
	                   .try_as<DependencyObject>();
	while (element) {
		if (element == AttachmentPanel()) {
			return true;
		}
		element = Media::VisualTreeHelper::GetParent(element);
	}
	return false;
}
void ViewerPage::setupAttachments() {
	setupAttachmentSaving();
	const auto weak = get_weak();
	AttachmentButton().Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->leavePageBox();
			self->setAttachmentsOpen(unbox_value_or<bool>(
			    self->AttachmentButton().IsChecked(), false));
		}
	});
	AttachmentRetry().Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->requestAttachments();
		}
	});
}
void ViewerPage::setAttachmentsOpen(bool open) {
	if (m_closed || (open && !m_layout)) {
		return;
	}
	if (open) {
		setThumbnailsOpen(false);
		setOutlineOpen(false);
		setCommentsOpen(false);
		m_sidebar = Sidebar::Attachments;
	} else if (attachmentsHasFocus()) {
		Scroller().Focus(FocusState::Programmatic);
	}
	AttachmentButton().IsChecked(open);
	AttachmentPanel().Visibility(open ? PanelVisibility::Visible
	                                  : PanelVisibility::Collapsed);
	if (open) {
		showAttachments();
	}
}
void ViewerPage::closeAttachments() {
	setAttachmentsOpen(false);
	++m_attachmentVersion;
	++m_attachmentSaveVersion;
	m_attachmentSaving = false;
	m_attachmentsPending = false;
	m_attachmentsFailed = false;
	m_attachmentsBuilt = false;
	m_attachments.reset();
	AttachmentList().Items().Clear();
}
void ViewerPage::requestAttachments() {
	if (m_closed || !m_session || m_attachmentsPending || m_attachments) {
		return;
	}
	m_attachmentsPending = true;
	m_attachmentsFailed = false;
	const auto version = ++m_attachmentVersion;
	const auto session = m_session.get();
	showAttachments();
	try {
		if (session->attachments([weak = get_weak(), queue = DispatcherQueue(),
		                          version,
		                          session](pdf::AttachmentsResult result) {
			    const auto reply =
			        std::make_shared<pdf::AttachmentsResult>(std::move(result));
			    queue.TryEnqueue([weak, reply, version, session] {
				    const auto self = weak.get();
				    if (!self || self->m_closed ||
				        self->m_attachmentVersion != version ||
				        self->m_session.get() != session) {
					    return;
				    }
				    self->m_attachmentsPending = false;
				    self->m_attachments = reply->canceled
				                              ? std::nullopt
				                              : std::move(reply->attachments);
				    self->m_attachmentsFailed = !self->m_attachments;
				    self->m_attachmentsBuilt = false;
				    self->showAttachments();
			    });
		    })) {
			return;
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't load the attachments sidebar.");
	}
	m_attachmentsPending = false;
	m_attachmentsFailed = true;
	showAttachments();
}
void ViewerPage::showAttachments() {
	if (m_closed || !attachmentsOpen() ||
	    Visibility() != PanelVisibility::Visible) {
		return;
	}
	const auto visible = PanelVisibility::Visible;
	const auto collapsed = PanelVisibility::Collapsed;
	AttachmentRetry().Visibility(collapsed);
	if (!m_attachments) {
		AttachmentList().Visibility(collapsed);
		AttachmentStatus().Visibility(visible);
		if (m_attachmentsPending) {
			AttachmentStatus().Text(L"Loading attachments\u2026");
		} else if (m_attachmentsFailed) {
			AttachmentStatus().Text(
			    L"Couldn't load this document's attachments.");
			AttachmentRetry().Visibility(visible);
		} else {
			requestAttachments();
		}
		return;
	}
	if (!m_attachmentsBuilt) {
		AttachmentList().Items().Clear();
		for (const auto& entry : m_attachments->entries) {
			// a click reports the row's content rather than the row, so the
			// name carries the PDF index as well (see setupAttachmentSaving)
			TextBlock name;
			name.Text(to_hstring(entry.name));
			name.Tag(box_value(entry.index));
			ListViewItem item;
			item.Content(name);
			item.Tag(box_value(entry.index));
			ToolTipService::SetToolTip(item, box_value(L"Save attachment as"));
			Automation::AutomationProperties::SetName(item, name.Text());
			Automation::AutomationProperties::SetHelpText(
			    item, L"Save attachment as");
			AttachmentList().Items().Append(item);
		}
		m_attachmentsBuilt = true;
	}
	const bool empty = m_attachments->entries.empty();
	AttachmentList().Visibility(empty ? collapsed : visible);
	AttachmentStatus().Visibility(empty || m_attachments->limited ? visible
	                                                              : collapsed);
	AttachmentStatus().Text(empty ? L"No attachments."
	                        : m_attachments->limited
	                            ? L"Some attachment names are unavailable."
	                            : L"");
}
} // namespace winrt::Sable::implementation
