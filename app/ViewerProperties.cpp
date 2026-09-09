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
	if (propertiesOpen()) {
		closePane();
	}
}
// Document properties live in the tool pane, so they sit beside the pages
// and follow the reader instead of freezing at the page that was current.
void ViewerPage::showProperties() {
	try {
		if (m_closed || !m_layout || !m_session || !XamlRoot() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    presenting() || m_passwordDialog) {
			return;
		}
		if (organizing()) {
			showNotice(
			    L"Finish organizing pages before opening document properties.");
			return;
		}
		stopPan();
		leavePageBox();
		const bool opened = propertiesOpen();
		openPane(ToolPane::Properties);
		if (!opened) {
			focusToolPane();
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Couldn't show document properties.");
		if (!m_closed) {
			showNotice(L"Couldn't open document properties. Try again.");
		}
	}
}
void ViewerPage::requestProperties() {
	if (m_closed || !m_session || m_propertiesPending || m_properties) {
		return;
	}
	m_propertiesPending = true;
	m_propertiesFailed = false;
	updatePropertiesPane();
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
				    self->updatePropertiesPane();
			    });
		    })) {
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Couldn't request document properties.");
	}
	m_propertiesPending = false;
	m_propertiesFailed = true;
	updatePropertiesPane();
}
void ViewerPage::followPropertiesPage() {
	if (!m_layout || m_current >= m_sizes.size()) {
		return;
	}
	m_propertiesPage = m_current;
	m_propertiesSize = helpers::rotatedSize(m_sizes[m_current], m_rotation);
	// the same text the rows were built with, named like they were
	const auto set = [](const TextBlock& cell, const wchar_t* label,
	                    const hstring& value) {
		if (cell) {
			cell.Text(value);
			Automation::AutomationProperties::SetName(cell, hstring(label) +
			                                                    L": " + value);
		}
	};
	set(m_propertiesCurrent, L"Current page", to_hstring(m_propertiesPage + 1));
	set(m_propertiesPageSize, L"Page size",
	    to_hstring(helpers::propertyPageSize(m_propertiesSize.width,
	                                         m_propertiesSize.height)));
}
void ViewerPage::updatePropertiesPane() {
	if (!propertiesOpen() || m_closed) {
		return;
	}
	const auto children = PropertiesHost().Children();
	children.Clear();
	m_propertiesCurrent = nullptr;
	m_propertiesPageSize = nullptr;
	if (!m_properties) {
		TextBlock status;
		status.Text(
		    m_propertiesFailed
		        ? L"Couldn't load this document's details. You can try again."
		        : L"Loading document details...");
		status.TextWrapping(TextWrapping::Wrap);
		Automation::AutomationProperties::SetLiveSetting(
		    status, Automation::Peers::AutomationLiveSetting::Polite);
		children.Append(status);
		if (m_propertiesFailed) {
			Button retry;
			retry.Content(box_value(L"Retry"));
			retry.Click([weak = get_weak()](auto&&, auto&&) {
				if (const auto self = weak.get()) {
					self->requestProperties();
				}
			});
			children.Append(retry);
		}
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
	// the label colour comes from the theme, so it follows light and dark
	const auto labelStyle = PropertiesHost()
	                            .Resources()
	                            .Lookup(box_value(L"PropertyLabelStyle"))
	                            .as<winrt::Microsoft::UI::Xaml::Style>();
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
		label.Style(labelStyle);
		// the value's name carries the label, so it isn't read twice
		Automation::AutomationProperties::SetAccessibilityView(
		    label, Automation::Peers::AccessibilityView::Raw);
		Grid::SetRow(label, index);
		rows.Children().Append(label);
		TextBlock text;
		text.FontSize(13);
		text.TextWrapping(TextWrapping::Wrap);
		text.IsTextSelectionEnabled(true);
		const hstring shown = value.empty() ? hstring(L"\u2014") : value;
		text.Text(shown);
		Automation::AutomationProperties::SetName(text, hstring(name) + L": " +
		                                                    shown);
		Grid::SetRow(text, index);
		Grid::SetColumn(text, 1);
		rows.Children().Append(text);
		if (std::wstring_view(name) == L"Current page") {
			m_propertiesCurrent = text;
		} else if (std::wstring_view(name) == L"Page size") {
			m_propertiesPageSize = text;
		}
	}
	children.Append(rows);
	if (p.limited) {
		TextBlock note;
		note.Text(L"Some document details are too large to show.");
		note.TextWrapping(TextWrapping::Wrap);
		children.Append(note);
	}
}
} // namespace winrt::Sable::implementation
