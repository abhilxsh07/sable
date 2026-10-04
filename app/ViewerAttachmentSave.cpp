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
#include "helpers/attachment_file.h"
#include "helpers/logging.h"
#include <algorithm>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
void ViewerPage::setupAttachmentSaving() {
	AttachmentList().IsItemClickEnabled(false);
	const auto weak = get_weak();
	AttachmentList().SelectionChanged([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->updateAttachmentSave();
		}
	});
	AttachmentSaveButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->updateAttachmentSave();
			const auto row = self->AttachmentList()
			                     .SelectedItem()
			                     .try_as<FrameworkElement>();
			if (self->AttachmentSaveButton().IsEnabled() && row) {
				self->saveAttachment(unbox_value_or<int>(row.Tag(), -1));
			}
		}
	});
}
void ViewerPage::updateAttachmentSave() {
	const auto row = AttachmentList().SelectedItem().try_as<FrameworkElement>();
	const auto index = row ? unbox_value_or<int>(row.Tag(), -1) : -1;
	const bool valid =
	    m_attachments &&
	    std::any_of(
	        m_attachments->entries.begin(), m_attachments->entries.end(),
	        [index](const auto& entry) { return entry.index == index; });
	AttachmentSaveButton().IsEnabled(
	    valid && !m_closed && m_session && !m_attachmentSaving &&
	    !m_attachmentsPending && !organizing() && attachmentsOpen() &&
	    Visibility() == Microsoft::UI::Xaml::Visibility::Visible);
}
fire_and_forget ViewerPage::saveAttachment(int index) {
	weak_ref<ViewerPage> weak;
	Microsoft::UI::Dispatching::DispatcherQueue queue{nullptr};
	std::uint64_t version = 0;
	try {
		weak = get_weak();
		queue = DispatcherQueue();
		if (m_closed || m_attachmentSaving || organizing() || !m_session ||
		    !m_attachments || !attachmentsOpen() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
			co_return;
		}
		const auto entry = std::find_if(
		    m_attachments->entries.begin(), m_attachments->entries.end(),
		    [index](const auto& e) { return e.index == index; });
		if (entry == m_attachments->entries.end()) {
			co_return;
		}
		const auto name = to_hstring(helpers::attachmentFilename(entry->name));
		const auto session = m_session.get();
		version = ++m_attachmentSaveVersion;
		m_attachmentSaving = true;
		updateAttachmentSave();
		const apartment_context ui;
		hstring path;
#ifdef SABLE_SURFACE_TEST
		if (m_attachmentPicker) {
			path = co_await m_attachmentPicker(name);
		} else
#endif
		{
			// see:
			// https://learn.microsoft.com/en-us/windows/apps/develop/files/pickers-save-file
			Microsoft::Windows::Storage::Pickers::FileSavePicker picker(
			    XamlRoot().ContentIslandEnvironment().AppWindowId());
			picker.SuggestedFileName(name);
			const auto selected = co_await picker.PickSaveFileAsync();
			if (selected) {
				path = selected.Path();
			}
		}
		auto self = weak.get();
		if (!self || self->m_closed ||
		    self->m_attachmentSaveVersion != version) {
			co_return;
		}
		if (path.empty() || self->m_session.get() != session ||
		    self->Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
			self->m_attachmentSaving = false;
			self->updateAttachmentSave();
			co_return;
		}
		struct Extraction {
			handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
			pdf::AttachmentResult result;
		};
		const auto job = std::make_shared<Extraction>();
		check_bool(static_cast<bool>(job->ready));
		check_bool(session->attachment(index, [job](pdf::AttachmentResult r) {
			job->result = std::move(r);
			SetEvent(job->ready.get());
		}));
		self = nullptr;
		co_await resume_on_signal(job->ready.get());
		co_await ui;
		self = weak.get();
		if (!self || self->m_closed ||
		    self->m_attachmentSaveVersion != version) {
			co_return;
		}
		if (job->result.canceled || !job->result.bytes ||
		    self->Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
			self->m_attachmentSaving = false;
			self->updateAttachmentSave();
			if (!job->result.canceled && !job->result.bytes) {
				self->showNotice(L"Couldn't read this attachment. It may be "
				                 L"damaged or larger than 128 MiB.");
			}
			co_return;
		}
		const auto source = self->m_path;
		self = nullptr;
		co_await resume_background();
		bool saved = false;
		try {
			saved = helpers::saveAttachment(path.c_str(), source,
			                                *job->result.bytes);
		} catch (...) {
			diagnostics::caught(diagnostics::Level::Error,
			                    "Attachment file write failed.");
		}
		co_await ui;
		if (self = weak.get(); self && !self->m_closed &&
		                       self->m_attachmentSaveVersion == version) {
			self->m_attachmentSaving = false;
			self->updateAttachmentSave();
			if (!saved) {
				self->showNotice(L"Couldn't save this attachment. Choose a "
				                 L"writable folder and a file other than "
				                 L"the source PDF.");
			}
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Couldn't save a PDF attachment.");
		try {
			if (queue) {
				queue.TryEnqueue([weak, version] {
					if (auto self = weak.get();
					    self && !self->m_closed &&
					    self->m_attachmentSaveVersion == version) {
						self->m_attachmentSaving = false;
						self->updateAttachmentSave();
						self->showNotice(L"Couldn't save this attachment. Try "
						                 L"again or choose another folder.");
					}
				});
			}
		} catch (...) {
		}
	}
}
} // namespace winrt::Sable::implementation
