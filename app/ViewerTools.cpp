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
#include "helpers/logging.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using Primitives::ToggleButton;

// The command bar above the document, the All tools panel it opens and the
// quick-tools strip beside the pages. They only start tools that already
// exist: the same calls Home, the Menu and the Comments panel make.
namespace winrt::Sable::implementation {
namespace {
struct ToolEntry {
	const wchar_t* group;
	const wchar_t* label;
	const wchar_t* glyph;
	const wchar_t* tool;
	const wchar_t* tip;
};
// in the order the All tools panel lists them
constexpr ToolEntry ALL_TOOLS[] = {
    {L"Pages", L"Organize pages", L"\uE8A9", L"organize",
     L"Turn, delete, insert, extract and reorder pages"},
    {L"Pages", L"Combine files", L"\uE8C8", L"combine",
     L"Join several PDFs into one"},
    {L"Comment & sign", L"Comment", L"\uE8BD", L"comment",
     L"Add notes, highlights and drawings"},
    {L"Comment & sign", L"Fill & Sign", L"\uE70F", L"fillsign",
     L"Add text, checks and a signature"},
    {L"Export", L"Export", L"\uEDE1", L"export",
     L"Save pages as images or the text as a file"},
    {L"Read", L"Read aloud", L"\uE767", L"readaloud",
     L"Listen to the PDF with a Windows voice"},
    {L"Read", L"Find", L"\uE721", L"find", L"Find in document (Ctrl+F)"},
};
// the strip: the tools you reach for constantly
constexpr ToolEntry QUICK_TOOLS[] = {
    {L"", L"Text selection", L"\uE8B3", L"select", L"Text selection (S)"},
    {L"", L"Hand tool", L"\uE7C9", L"hand", L"Hand tool (H)"},
    {L"", L"Sticky note", L"\uE70B", L"note", L"Place a note on the page"},
    {L"", L"Highlight", L"\uE7E6", L"highlight",
     L"Highlight the selected text"},
    {L"", L"Draw", L"\uE76D", L"draw", L"Draw on page"},
    {L"", L"Text box", L"\uE8D2", L"textbox", L"Place a text box on the page"},
    {L"", L"Fill & Sign", L"\uE70F", L"fillsign",
     L"Add text, checks and a signature"},
    {L"", L"Read aloud", L"\uE767", L"readaloud",
     L"Listen to the PDF with a Windows voice"},
};

FontIcon icon(const wchar_t* glyph) {
	FontIcon icon;
	icon.Glyph(glyph);
	icon.FontSize(14);
	return icon;
}
} // namespace

void ViewerPage::setupToolSurfaces() {
	if (m_toolSurfacesSetup) {
		return;
	}
	m_toolSurfacesSetup = true;
	const auto weak = get_weak();
	const auto command = [weak](const wchar_t* name) {
		return [weak, name](auto&&, auto&&) {
			if (const auto self = weak.get();
			    self && !self->m_closed && self->m_commandRequested) {
				self->m_commandRequested(name);
			}
		};
	};
	BarFindButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->runTool(L"find");
		}
	});
	BarSaveButton().Click(command(L"save"));
	BarPrintButton().Click(command(L"print"));
	BarPropertiesItem().Click(command(L"properties"));
	BarPresentationItem().Click(command(L"presentation"));
	BarRotateClockwiseItem().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->rotateView(1);
		}
	});
	BarRotateCounterItem().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->rotateView(-1);
		}
	});
	// Checked and Unchecked, not Click, so keyboard and automation toggles
	// count too
	AllToolsButton().Checked([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->setAllToolsOpen(true);
		}
	});
	AllToolsButton().Unchecked([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->setAllToolsOpen(false);
		}
	});
	BarQuickToolsItem().IsChecked(s_quickTools);
	// the item's own check, however it changed: click, keyboard or a toggle
	BarQuickToolsItem().RegisterPropertyChangedCallback(
	    ToggleMenuFlyoutItem::IsCheckedProperty(), [weak](auto&&, auto&&) {
		    if (const auto self = weak.get();
		        self && !self->m_syncingQuickTools) {
			    s_quickTools = self->BarQuickToolsItem().IsChecked();
			    self->updateQuickTools();
		    }
	    });
	const auto list = AllToolsList().Children();
	const wchar_t* group = nullptr;
	for (const auto& entry : ALL_TOOLS) {
		if (!group || std::wstring_view(group) != entry.group) {
			group = entry.group;
			TextBlock heading;
			heading.Text(group);
			heading.FontSize(12);
			heading.Opacity(.7);
			heading.Margin({4, list.Size() ? 12. : 0., 0, 4});
			list.Append(heading);
		}
		Button button;
		button.HorizontalAlignment(HorizontalAlignment::Stretch);
		button.HorizontalContentAlignment(HorizontalAlignment::Left);
		button.Background(nullptr);
		button.BorderThickness({0, 0, 0, 0});
		StackPanel content;
		content.Orientation(Orientation::Horizontal);
		content.Spacing(10);
		content.Children().Append(icon(entry.glyph));
		TextBlock label;
		label.Text(entry.label);
		content.Children().Append(label);
		button.Content(content);
		ToolTipService::SetToolTip(button, box_value(entry.tip));
		Automation::AutomationProperties::SetName(button, entry.label);
		button.Click([weak, tool = entry.tool](auto&&, auto&&) {
			if (const auto self = weak.get()) {
				self->runTool(tool);
			}
		});
		list.Append(button);
	}
	for (const auto& entry : QUICK_TOOLS) {
		ToggleButton button;
		button.Content(icon(entry.glyph));
		button.Width(36);
		button.Height(36);
		button.Padding({0, 0, 0, 0});
		ToolTipService::SetToolTip(button, box_value(entry.tip));
		Automation::AutomationProperties::SetName(button, entry.label);
		const auto pressed = [weak, tool = entry.tool](auto&&, auto&&) {
			if (const auto self = weak.get();
			    self && !self->m_syncingQuickTools) {
				self->runTool(tool);
			}
		};
		button.Checked(pressed);
		button.Unchecked(pressed);
		QuickToolsList().Children().Append(button);
		m_quickToolButtons.emplace_back(button, entry.tool);
	}
	updateQuickTools();
}

void ViewerPage::setAllToolsOpen(bool open) {
	if (m_closed) {
		return;
	}
	const auto checked = AllToolsButton().IsChecked();
	if ((checked && checked.Value()) != open) {
		AllToolsButton().IsChecked(open);
	}
	AllToolsPanel().Visibility(open && !m_presentation ? Visibility::Visible
	                                                   : Visibility::Collapsed);
	updateQuickTools();
}

// one tool from the panel or the strip; Combine starts from its own list of
// files, so the window opens it.
void ViewerPage::runTool(std::wstring_view tool) {
	if (m_closed || m_presentation) {
		return;
	}
	const auto placing = [&](PlacementTool placement) {
		return m_notePlacing && m_placementTool == placement;
	};
	try {
		if (tool == L"combine") {
			if (m_commandRequested) {
				m_commandRequested(L"combine");
			}
		} else if (tool == L"find") {
			openFind();
		} else if (tool == L"select") {
			setCursorTool(CursorTool::Select);
		} else if (tool == L"hand") {
			setCursorTool(CursorTool::Hand);
		} else if (tool == L"highlight") {
			if (hasTextSelection() && canMarkSelection()) {
				markSelection(pdf::TextMarkup::Highlight);
			} else {
				showNotice(L"Select some text first, then Highlight marks it.");
			}
		} else if (tool == L"note" || tool == L"draw" || tool == L"textbox") {
			const auto placement = tool == L"note"   ? PlacementTool::Note
			                       : tool == L"draw" ? PlacementTool::Ink
			                                         : PlacementTool::TextBox;
			if (placing(placement)) {
				cancelNotePlacement();
			} else {
				setCommentsOpen(true);
				if (commentsOpen()) {
					beginNotePlacement(placement);
				}
			}
		} else {
			requestTool(tool);
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not start a tool");
	}
	updateQuickTools();
}

void ViewerPage::updateQuickTools() {
	if (m_closed || !m_toolSurfacesSetup) {
		return;
	}
	QuickTools().Visibility(s_quickTools && !m_presentation
	                            ? Visibility::Visible
	                            : Visibility::Collapsed);
	m_syncingQuickTools = true;
	BarQuickToolsItem().IsChecked(s_quickTools);
	m_syncingQuickTools = false;
	// the strip moves over when the panel is open
	QuickTools().Margin(
	    {AllToolsPanel().Visibility() == Visibility::Visible ? 242. : 12., 0, 0,
	     0});
	const bool canNote =
	    m_noteAllowed && !m_noteBusy && !m_noteSaving && !m_noteDialog;
	// setting IsChecked here mustn't start the tools again
	m_syncingQuickTools = true;
	for (const auto& [button, tool] : m_quickToolButtons) {
		bool on = false, enabled = true;
		if (tool == L"select") {
			on = m_cursorTool == CursorTool::Select;
		} else if (tool == L"hand") {
			on = m_cursorTool == CursorTool::Hand;
		} else if (tool == L"note") {
			on = m_notePlacing && m_placementTool == PlacementTool::Note;
			enabled = canNote;
		} else if (tool == L"draw") {
			on = m_notePlacing && m_placementTool == PlacementTool::Ink;
			enabled = canNote;
		} else if (tool == L"textbox") {
			on = m_notePlacing && m_placementTool == PlacementTool::TextBox;
			enabled = canNote;
		} else if (tool == L"readaloud") {
			on = readingAloud();
		} else if (tool == L"fillsign") {
			enabled = m_noteAllowed || m_fillAllowed;
		}
		try {
			button.IsChecked(on);
			button.IsEnabled(enabled);
		} catch (...) {
			m_syncingQuickTools = false;
			throw;
		}
	}
	m_syncingQuickTools = false;
}
} // namespace winrt::Sable::implementation
