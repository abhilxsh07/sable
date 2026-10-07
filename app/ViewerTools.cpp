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
#include "helpers/panes.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <limits>
#include <string_view>

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
    {L"Document", L"Document properties", L"\uE946", L"properties",
     L"Title, author, size and other details of this PDF"},
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

// the narrowest the pages may get before the tool pane and a side panel
// stop sharing the window
constexpr double MIN_CANVAS = 360.;

// what panes.json calls each pane; empty for none
std::string_view paneKey(ToolPane pane) {
	switch (pane) {
	case ToolPane::Launcher:
		return "launcher";
	case ToolPane::Properties:
		return "properties";
	case ToolPane::Export:
		return "export";
	case ToolPane::Combine:
		return "combine";
	default:
		return {};
	}
}

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
		if (const auto self = weak.get(); self && !self->m_syncingToolPane) {
			self->openPane(ToolPane::Launcher);
		}
	});
	AllToolsButton().Unchecked([weak](auto&&, auto&&) {
		if (const auto self = weak.get(); self && !self->m_syncingToolPane) {
			self->closePane();
		}
	});
	ToolPaneBack().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->openPane(ToolPane::Launcher);
		}
	});
	ToolPaneClose().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->closePane();
			self->Scroller().Focus(FocusState::Programmatic);
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
	// the strip has a 56 px margin of its own, so the pages (and the frozen
	// view a gesture leaves over them) start after it. Whatever hides the
	// strip, Presentation or a failed tab too, gives them the room back.
	QuickTools().RegisterPropertyChangedCallback(
	    UIElement::VisibilityProperty(), [weak](auto&&, auto&&) {
		    if (const auto self = weak.get()) {
			    const Thickness margin{
			        self->QuickTools().Visibility() == Visibility::Visible ? 56.
			                                                               : 0.,
			        0, 0, 0};
			    self->Scroller().Margin(margin);
			    self->GestureFreeze().Margin(margin);
		    }
	    });
	updateQuickTools();
	setupPaneGrip();
}

void ViewerPage::setAllToolsOpen(bool open) {
	if (open) {
		openPane(ToolPane::Launcher);
	} else {
		closePane();
	}
}

void ViewerPage::openPane(ToolPane pane) {
	if (m_closed || pane == ToolPane::None || organizing()) {
		return;
	}
	if (m_toolPane == pane) {
		if (pane == ToolPane::Export) {
			refreshExportPreview(true);
		}
		focusToolPane();
		return;
	}
	m_toolPane = pane;
	if (pane == ToolPane::Export) {
		refreshExportPreview(true);
	}
	// each pane comes back at the width it was last given
	AllToolsPanel().Width(
	    helpers::loadPaneWidths(s_panesFile).width(paneKey(pane)));
	updateToolPane();
	if (pane == ToolPane::Properties) {
		// the rows come from the cache when it's still good
		followPropertiesPage();
		requestProperties();
		updatePropertiesPane();
	}
}

void ViewerPage::closePane() {
	if (m_closed) {
		return;
	}
	m_toolPane = ToolPane::None;
	updateToolPane();
}

// Lays the pane out from m_toolPane: its column, header and button. Nothing
// else sets them. Presentation keeps the state and only hides the column.
void ViewerPage::updateToolPane() {
	const bool open = m_toolPane != ToolPane::None;
	const auto checked = AllToolsButton().IsChecked();
	if ((checked && checked.Value()) != open) {
		m_syncingToolPane = true;
		AllToolsButton().IsChecked(open);
		m_syncingToolPane = false;
	}
	const bool properties = m_toolPane == ToolPane::Properties;
	const bool exporting = m_toolPane == ToolPane::Export;
	const bool combining = m_toolPane == ToolPane::Combine;
	ToolPaneTitle().Text(m_toolPane == ToolPane::Launcher ? L"All tools"
	                     : properties ? L"Document properties"
	                     : exporting  ? L"Export"
	                     : combining  ? L"Combine files"
	                                  : L"");
	AllToolsScroll().Visibility(properties || exporting || combining
	                                ? Visibility::Collapsed
	                                : Visibility::Visible);
	PropertiesScroll().Visibility(properties ? Visibility::Visible
	                                         : Visibility::Collapsed);
	ExportScroll().Visibility(exporting ? Visibility::Visible
	                                    : Visibility::Collapsed);
	CombineScroll().Visibility(combining ? Visibility::Visible
	                                     : Visibility::Collapsed);
	// a tool's own pane goes back to the list
	ToolPaneBack().Visibility(m_toolPane == ToolPane::None ||
	                                  m_toolPane == ToolPane::Launcher
	                              ? Visibility::Collapsed
	                              : Visibility::Visible);
	if (open && !presenting()) {
		// a side panel that would leave the pages too narrow closes
		for (const auto& panel : {ThumbnailPanel(), OutlinePanel(),
		                          AttachmentPanel(), CommentsPanel()}) {
			if (panel.Visibility() == Visibility::Visible &&
			    crowded(AllToolsPanel().Width(), panel.Width())) {
				setThumbnailsOpen(false);
				setOutlineOpen(false);
				setAttachmentsOpen(false);
				setCommentsOpen(false);
				break;
			}
		}
	}
	AllToolsPanel().Visibility(open && !presenting() ? Visibility::Visible
	                                                 : Visibility::Collapsed);
	if (AllToolsPanel().Visibility() != Visibility::Visible &&
	    (m_paneGripHover || m_paneResizing)) {
		// a hidden grip gets no PointerExited
		m_paneGripHover = m_paneResizing = false;
		updatePaneGripCursor();
	}
	updateQuickTools();
}

void ViewerPage::focusToolPane() {
	if (presenting() || AllToolsPanel().Visibility() != Visibility::Visible) {
		return;
	}
	const UIElement host =
	    propertiesOpen() ? static_cast<UIElement>(PropertiesHost())
	    : m_toolPane == ToolPane::Export ? static_cast<UIElement>(ExportHost())
	    : m_toolPane == ToolPane::Combine
	        ? static_cast<UIElement>(CombineHost())
	        : static_cast<UIElement>(AllToolsList());
	Control target{nullptr};
	if (const auto first =
	        Input::FocusManager::FindFirstFocusableElement(host)) {
		target = first.try_as<Control>();
	}
	// the values are text, so a properties pane starts at its header
	if (!target && propertiesOpen()) {
		target = ToolPaneBack();
	}
	if (target) {
		target.Focus(FocusState::Programmatic);
	}
}

void ViewerPage::resizePane(double width, bool save) {
	if (m_closed) {
		return;
	}
	// not open or not laid out: only the range applies
	double room = std::numeric_limits<double>::quiet_NaN();
	if (ActualWidth() > 0 &&
	    AllToolsPanel().Visibility() == Visibility::Visible) {
		double side = 0;
		for (const auto& panel : {ThumbnailPanel(), OutlinePanel(),
		                          AttachmentPanel(), CommentsPanel()}) {
			if (panel.Visibility() == Visibility::Visible) {
				side += panel.Width();
			}
		}
		const double rail =
		    Rail().Visibility() == Visibility::Visible ? Rail().Width() : 0.;
		room = ActualWidth() - side - rail - MIN_CANVAS;
	}
	const double fitted = helpers::fitPaneWidth(width, room);
	AllToolsPanel().Width(fitted);
	if (save) {
		if (const auto key = paneKey(m_toolPane); !key.empty()) {
			helpers::savePaneWidth(s_panesFile, key, fitted);
		}
	}
}

// the grip on the pane's edge beside the pages. Its arrow keys come through
// navigationKey, as the window sees keys before the grip does.
void ViewerPage::setupPaneGrip() {
	const auto weak = get_weak();
	const auto grip = ToolPaneGrip();
	grip.DragStarted([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->m_paneResizing = true;
			self->m_paneDragWidth = self->AllToolsPanel().Width();
			self->updatePaneGripCursor();
		}
	});
	grip.DragDelta([weak](auto&&, const Primitives::DragDeltaEventArgs& e) {
		if (const auto self = weak.get()) {
			// the unrounded width, so whole-pixel steps don't drift from
			// the pointer at fractional scales
			self->m_paneDragWidth += e.HorizontalChange();
			self->resizePane(self->m_paneDragWidth, false);
		}
	});
	grip.DragCompleted([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->m_paneResizing = false;
			self->resizePane(self->AllToolsPanel().Width(), true);
			self->updatePaneGripCursor();
		}
	});
	grip.PointerEntered([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->m_paneGripHover = true;
			self->updatePaneGripCursor();
		}
	});
	grip.PointerExited([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->m_paneGripHover = false;
			self->updatePaneGripCursor();
		}
	});
}

void ViewerPage::updatePaneGripCursor() {
	if (m_closed) {
		return;
	}
	if (m_paneGripHover || m_paneResizing) {
		using namespace Microsoft::UI::Input;
		ProtectedCursor(
		    InputSystemCursor::Create(InputSystemCursorShape::SizeWestEast));
	} else {
		// whatever the pages want, the hand tool's cursor or none
		updatePanCursor();
	}
}

bool ViewerPage::crowded(double pane, double side) {
	const double rail =
	    Rail().Visibility() == Visibility::Visible ? Rail().Width() : 0.;
	// not laid out yet: nothing to measure against
	const double width = ActualWidth();
	return width > 0 && width - pane - side - rail < MIN_CANVAS;
}

// Esc steps back one thing at a time: Find, then an armed tool (or a
// drawing being dragged), then Organize, then the tool pane, which hands
// focus back to the pages
bool ViewerPage::escape() {
	if (m_closed || presenting()) {
		return false;
	}
	if (findOpen()) {
		closeFind();
		return true;
	}
	if (cancelNotePlacement()) {
		return true;
	}
	if (organizing()) {
		closeOrganize();
		return true;
	}
	if (m_toolPane != ToolPane::None) {
		closePane();
		Scroller().Focus(FocusState::Programmatic);
		return true;
	}
	return false;
}

// a side panel opening closes the tool pane if both won't fit
void ViewerPage::makeRoomForPanel(double width) {
	if (AllToolsPanel().Visibility() == Visibility::Visible &&
	    crowded(AllToolsPanel().Width(), width)) {
		setAllToolsOpen(false);
	}
}

// one tool from the panel or the strip; Combine starts from its own list of
// files, so the window opens it.
void ViewerPage::runTool(std::wstring_view tool) {
	if (m_closed || presenting()) {
		return;
	}
	const auto placing = [&](PlacementTool placement) {
		return notePlacing() && m_activeTool == placement;
	};
	try {
		if (tool == L"combine") {
			if (m_commandRequested) {
				m_commandRequested(L"combine");
			}
		} else if (tool == L"properties") {
			if (m_commandRequested) {
				m_commandRequested(L"properties");
			}
		} else if (tool == L"find") {
			openFind();
		} else if (tool == L"select") {
			setCursorTool(CursorTool::Select);
		} else if (tool == L"hand") {
			setCursorTool(CursorTool::Hand);
		} else if (tool == L"highlight") {
			setCursorTool(CursorTool::Select);
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
				beginNotePlacement(placement);
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
	// no command bar (a tab that failed or is still loading): no strip
	const bool chrome = DocumentBar().Visibility() == Visibility::Visible;
	QuickTools().Visibility(s_quickTools && chrome && !presenting() &&
	                                !organizing()
	                            ? Visibility::Visible
	                            : Visibility::Collapsed);
	m_syncingQuickTools = true;
	BarQuickToolsItem().IsChecked(s_quickTools);
	m_syncingQuickTools = false;
	const bool canNote =
	    m_noteAllowed && !m_noteBusy && !m_noteSaving && !m_noteDialog;
	// setting IsChecked here mustn't start the tools again
	m_syncingQuickTools = true;
	for (const auto& [button, tool] : m_quickToolButtons) {
		bool on = false, enabled = true;
		if (tool == L"select") {
			on = m_activeTool == CursorTool::Select;
		} else if (tool == L"hand") {
			on = m_activeTool == CursorTool::Hand;
		} else if (tool == L"note") {
			on = notePlacing() && m_activeTool == PlacementTool::Note;
			enabled = canNote;
		} else if (tool == L"draw") {
			on = notePlacing() && m_activeTool == PlacementTool::Ink;
			enabled = canNote;
		} else if (tool == L"textbox") {
			on = notePlacing() && m_activeTool == PlacementTool::TextBox;
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
