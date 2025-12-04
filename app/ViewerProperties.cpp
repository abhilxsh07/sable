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
#include "helpers/properties.h"
#include "helpers/logging.h"
#include <algorithm>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
void ViewerPage::dismissProperties() {
	if (auto dialog = std::exchange(m_propertiesDialog, nullptr)) {
		dialog.Hide();
	}
}
fire_and_forget ViewerPage::showProperties() {
	weak_ref<ViewerPage> weak;
	ContentDialog dialog{nullptr};
	try {
		weak = get_weak();
		if (m_closed || !m_layout || !m_session || !XamlRoot() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    m_propertiesDialog || m_passwordDialog) {
			co_return;
		}
		stopPan();
		leavePageBox();
		m_propertiesPage = m_current;
		m_propertiesSize = helpers::rotatedSize(m_sizes[m_current], m_rotation);
		dialog = ContentDialog();
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(L"Document properties"));
		dialog.CloseButtonText(L"Close");
		dialog.DefaultButton(ContentDialogButton::Close);
		dialog.SizeChanged([weak](auto&&, auto&&) {
			const auto self = weak.get();
			if (!self || !self->m_propertiesDialog || !self->XamlRoot()) {
				return;
			}
			if (const auto scroll =
			        self->m_propertiesDialog.Content().try_as<ScrollViewer>()) {
				scroll.MaxHeight((std::max)(
				    80.,
				    (std::min)(460., self->XamlRoot().Size().Height - 210.)));
			}
		});
		dialog.PrimaryButtonClick(
		    [weak](auto&&, const ContentDialogButtonClickEventArgs& e) {
			    e.Cancel(true);
			    if (auto self = weak.get()) {
				    self->requestProperties();
			    }
		    });
		m_propertiesDialog = dialog;
		if (!m_properties) {
			requestProperties();
		}
		updatePropertiesDialog();
		co_await dialog.ShowAsync();
		if (auto self = weak.get();
		    self && self->m_propertiesDialog == dialog) {
			self->m_propertiesDialog = nullptr;
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't show document properties.");
		if (auto self = weak.get()) {
			if (self->m_propertiesDialog == dialog) {
				self->m_propertiesDialog = nullptr;
			}
			if (dialog) {
				try {
					dialog.Hide();
				} catch (...) {
				}
			}
			if (!self->m_closed) {
				self->showNotice(
				    L"Couldn't open document properties. Try again.");
			}
		}
	}
}
void ViewerPage::requestProperties() {
	if (m_closed || !m_session || m_propertiesPending || m_properties) {
		return;
	}
	m_propertiesPending = true;
	m_propertiesFailed = false;
	updatePropertiesDialog();
	const auto version = ++m_propertiesVersion;
	const auto session = m_session.get();
	try {
		if (session->properties([weak = get_weak(), queue = DispatcherQueue(),
		                         version,
		                         session](pdf::PropertiesResult result) {
			    const auto reply =
			        std::make_shared<pdf::PropertiesResult>(std::move(result));
			    queue.TryEnqueue([weak, reply, version, session] {
				    const auto self = weak.get();
				    if (!self || self->m_closed ||
				        self->m_propertiesVersion != version ||
				        self->m_session.get() != session) {
					    return;
				    }
				    self->m_propertiesPending = false;
				    self->m_properties = reply->canceled
				                             ? std::nullopt
				                             : std::move(reply->properties);
				    self->m_propertiesFailed = !self->m_properties;
				    self->updatePropertiesDialog();
			    });
		    })) {
			return;
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't request document properties.");
	}
	m_propertiesPending = false;
	m_propertiesFailed = true;
	updatePropertiesDialog();
}
void ViewerPage::updatePropertiesDialog() {
	if (!m_propertiesDialog || m_closed) {
		return;
	}
	auto dialog = m_propertiesDialog;
	dialog.PrimaryButtonText(L"");
	if (!m_properties) {
		TextBlock status;
		status.Text(
		    m_propertiesFailed
		        ? L"Couldn't load this document's details. You can try again."
		        : L"Loading document details...");
		status.TextWrapping(TextWrapping::Wrap);
		Automation::AutomationProperties::SetLiveSetting(
		    status, Automation::Peers::AutomationLiveSetting::Polite);
		if (m_propertiesFailed) {
			dialog.PrimaryButtonText(L"Retry");
		}
		dialog.Content(status);
		return;
	}
	const auto& p = *m_properties;
	const auto date = [](const std::string& value) {
		const auto formatted = helpers::propertyDate(value);
		return formatted.empty() && !value.empty() ? std::string("Unavailable")
		                                           : formatted;
	};
	const std::pair<const wchar_t*, hstring> fields[] = {
	    {L"File name", hstring(m_path.filename().wstring())},
	    {L"File size", to_hstring(helpers::propertyFileSize(p.bytes))},
	    {L"Title", to_hstring(helpers::propertyText(p.title))},
	    {L"Author", to_hstring(helpers::propertyText(p.author))},
	    {L"Subject", to_hstring(helpers::propertyText(p.subject))},
	    {L"Keywords", to_hstring(helpers::propertyText(p.keywords))},
	    {L"Created", to_hstring(date(p.created))},
	    {L"Modified", to_hstring(date(p.modified))},
	    {L"Application", to_hstring(helpers::propertyText(p.creator))},
	    {L"PDF producer", to_hstring(helpers::propertyText(p.producer))},
	    {L"PDF version", p.version ? to_hstring(*p.version / 10) + L"." +
	                                     to_hstring(*p.version % 10)
	                               : L""},
	    {L"Page count", to_hstring(p.pages)},
	    {L"Page size", to_hstring(helpers::propertyPageSize(
	                       m_propertiesSize.width, m_propertiesSize.height))},
	    {L"Current page", to_hstring(m_propertiesPage + 1)},
	    {L"Fast web view",
	     p.linearized ? (*p.linearized ? L"Yes" : L"No") : L"Unavailable"},
	    {L"Encrypted", p.encrypted ? L"Yes" : L"No"}};
	Grid rows;
	rows.ColumnSpacing(18);
	rows.RowSpacing(13);
	ColumnDefinition labelColumn;
	labelColumn.Width({116, GridUnitType::Pixel});
	rows.ColumnDefinitions().Append(labelColumn);
	rows.ColumnDefinitions().Append(ColumnDefinition());
	for (const auto& [name, value] : fields) {
		const auto index = static_cast<int32_t>(rows.RowDefinitions().Size());
		RowDefinition row;
		row.Height({1, GridUnitType::Auto});
		rows.RowDefinitions().Append(row);
		TextBlock label;
		label.Text(name);
		label.FontSize(12);
		label.TextWrapping(TextWrapping::Wrap);
		label.Foreground(Media::SolidColorBrush({255, 166, 166, 166}));
		Grid::SetRow(label, index);
		rows.Children().Append(label);
		TextBlock text;
		text.Text(value.empty() ? L"\u2014" : value);
		text.FontSize(13);
		text.TextWrapping(TextWrapping::Wrap);
		text.IsTextSelectionEnabled(true);
		Automation::AutomationProperties::SetName(text, name);
		Grid::SetRow(text, index);
		Grid::SetColumn(text, 1);
		rows.Children().Append(text);
	}
	StackPanel content;
	content.Spacing(18);
	content.Children().Append(rows);
	if (p.limited) {
		TextBlock note;
		note.Text(L"Some document details are too large to show.");
		note.TextWrapping(TextWrapping::Wrap);
		content.Children().Append(note);
	}
	ScrollViewer scroll;
	scroll.MaxWidth(480);
	scroll.MaxHeight(
	    (std::max)(80., (std::min)(460., XamlRoot().Size().Height - 210.)));
	scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
	scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
	scroll.Padding({0, 0, 12, 0});
	scroll.Content(content);
	dialog.Content(scroll);
}
} // namespace winrt::Sable::implementation
