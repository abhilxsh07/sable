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
#include "InkPalette.h"
#include "pdf/session.h"
#include "helpers/logging.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Text.h>
#include <string_view>
#include <algorithm>
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using XVisibility = Microsoft::UI::Xaml::Visibility;
namespace winrt::Sable::implementation {
namespace {
Symbol commentSymbol(std::string_view subtype) {
	if (subtype == "Text") {
		return Symbol::Comment;
	}
	if (subtype == "Highlight") {
		return Symbol::Highlight;
	}
	if (subtype == "Underline") {
		return Symbol::Underline;
	}
	if (subtype == "FreeText") {
		return Symbol::Font;
	}
	if (subtype == "FileAttachment") {
		return Symbol::Attach;
	}
	return Symbol::Document;
}
} // namespace
bool ViewerPage::commentsOpen() {
	return unbox_value_or<bool>(CommentsButton().IsChecked(), false);
}
const wchar_t* ViewerPage::annotationRefusal() const {
	if (m_closed || !m_layout || !m_session) {
		return L"This PDF isn't ready for this tool yet.";
	}
	if (m_editRefusal != pdf::WriteRefusal::None) {
		return writeNotice(m_editRefusal,
		                   L"This PDF can't be safely rewritten for editing.");
	}
	if (!m_noteAllowed) {
		return L"This PDF doesn't allow comments or Fill & Sign marks.";
	}
	if (m_mode != ViewerMode::Read || pageOperationBusy() || m_noteBusy ||
	    m_noteSaving || m_noteDialog || m_passwordDialog || m_linkPending ||
	    (m_notesAvailable && !m_notesAvailable())) {
		return L"Finish the open dialog or document operation before using "
		       L"this tool.";
	}
	return nullptr;
}
bool ViewerPage::commentsHasFocus() {
	if (CommentsPanel().Visibility() != XVisibility::Visible || !XamlRoot()) {
		return false;
	}
	auto element = Input::FocusManager::GetFocusedElement(XamlRoot())
	                   .try_as<DependencyObject>();
	while (element) {
		if (element == CommentsPanel()) {
			return true;
		}
		element = Media::VisualTreeHelper::GetParent(element);
	}
	return false;
}
void ViewerPage::followComment(int index) {
	const auto& rows = m_commentScan.rows();
	if (m_closed || index < 0 ||
	    static_cast<std::size_t>(index) >= rows.size()) {
		return;
	}
	const auto& row = rows[index];
	if (row.position && row.position->x >= 0 && row.position->x <= 1 &&
	    row.position->y >= 0 && row.position->y <= 1) {
		pdf::LinkDestination target;
		target.page = row.page;
		target.x = row.position->x;
		target.y = row.position->y;
		followLink(target, true);
	} else {
		goToPage(static_cast<std::size_t>(row.page));
	}
}
void ViewerPage::setupComments() {
	m_commentScan.reset(static_cast<int>(m_sizes.size()));
	const auto weak = get_weak();
	CommentsButton().Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->leavePageBox();
			self->setCommentsOpen(self->commentsOpen());
		}
	});
	AddNoteButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->editNote();
		}
	});
	EditNoteButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			if (self->m_selectedAnnotation) {
				self->editAnnotation(self->m_selectedAnnotation->target);
			} else {
				self->beginNoteEdit();
			}
		}
	});
	DeleteNoteButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->deleteAnnotation();
		}
	});
	CommentsList().SelectionChanged([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->clearAnnotationSelection();
			self->showComments();
		}
	});
	for (const auto& [button, tool] :
	     {std::pair{PlaceNoteButton(), ActiveTool::Note},
	      std::pair{PlaceTextBoxButton(), ActiveTool::TextBox},
	      std::pair{DrawInkButton(), ActiveTool::Ink}}) {
		const auto pressed = [weak, tool](auto&&, auto&&) {
			if (const auto self = weak.get();
			    self && !self->m_syncingQuickTools) {
				self->beginNotePlacement(tool);
			}
		};
		button.Checked(pressed);
		button.Unchecked(pressed);
	}
	for (const auto& [button, markup] :
	     {std::pair{CommentHighlightButton(), pdf::TextMarkup::Highlight},
	      std::pair{CommentUnderlineButton(), pdf::TextMarkup::Underline},
	      std::pair{CommentStrikeButton(), pdf::TextMarkup::StrikeOut}}) {
		button.Click([weak, markup](auto&&, auto&&) {
			if (const auto self = weak.get()) {
				self->setCursorTool(CursorTool::Select);
				self->markSelection(markup);
			}
		});
	}
	PlaceRectangleButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->beginNotePlacement(PlacementTool::Rectangle);
		}
	});
	PlaceEllipseButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->beginNotePlacement(PlacementTool::Ellipse);
		}
	});
	PlaceLineButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->beginNotePlacement(PlacementTool::Line);
		}
	});
	PlaceArrowButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->beginNotePlacement(PlacementTool::Arrow);
		}
	});
	PlaceStampButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->pickStamp();
		}
	});
	PlaceCheckButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->beginNotePlacement(PlacementTool::FillCheck);
		}
	});
	PlaceCrossButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->beginNotePlacement(PlacementTool::FillCross);
		}
	});
	PlaceFillTextButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->beginNotePlacement(PlacementTool::FillText);
		}
	});
	PlaceTypedSignatureButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->createSignature(PlacementTool::TypedSignature);
		}
	});
	PlaceDrawnSignatureButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->createSignature(PlacementTool::DrawnSignature);
		}
	});
	for (const auto& [label, color] : INK_COLORS) {
		RadioMenuFlyoutItem item;
		item.Text(label);
		item.GroupName(L"InkColor");
		item.IsChecked(color == s_inkStyle.color);
		item.Icon(colorDot({255, static_cast<std::uint8_t>(color.red),
		                    static_cast<std::uint8_t>(color.green),
		                    static_cast<std::uint8_t>(color.blue)}));
		item.Click([weak, color = color](auto&&, auto&&) {
			if (const auto self = weak.get()) {
				self->chooseInkStyle(color, std::nullopt);
			}
		});
		InkStyleMenu().Items().Append(item);
	}
	InkStyleMenu().Items().Append(MenuFlyoutSeparator());
	for (const auto& [label, width] : INK_WIDTHS) {
		RadioMenuFlyoutItem item;
		item.Text(label);
		item.GroupName(L"InkWidth");
		item.IsChecked(width == s_inkStyle.width);
		item.Click([weak, width = width](auto&&, auto&&) {
			if (const auto self = weak.get()) {
				self->chooseInkStyle(std::nullopt, width);
			}
		});
		InkStyleMenu().Items().Append(item);
	}
	CommentsRetry().Click([weak](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->m_commentScan.retry();
			self->showComments();
		}
	});
	CommentsList().ItemClick([weak](auto&&, const ItemClickEventArgs& args) {
		if (auto self = weak.get()) {
			const auto row = args.ClickedItem().try_as<FrameworkElement>();
			if (row && !self->m_closed && self->commentsOpen() &&
			    self->Visibility() == XVisibility::Visible &&
			    self->Rail().Visibility() == XVisibility::Visible) {
				self->followComment(unbox_value_or<int>(row.Tag(), -1));
			}
		}
	});
	const DependencyPropertyChangedCallback visibility = [weak](auto&&,
	                                                            auto&&) {
		if (auto self = weak.get()) {
			self->showComments();
		}
	};
	m_commentVisibility = RegisterPropertyChangedCallback(
	    UIElement::VisibilityProperty(), visibility);
	m_commentRailVisibility = Rail().RegisterPropertyChangedCallback(
	    UIElement::VisibilityProperty(), visibility);
}
void ViewerPage::setCommentsOpen(bool open) {
	if (m_closed || (open && (!m_layout || organizing() ||
	                          Rail().Visibility() != XVisibility::Visible))) {
		return;
	}
	if (open) {
		makeRoomForPanel(CommentsPanel().Width());
		setThumbnailsOpen(false);
		setOutlineOpen(false);
		setAttachmentsOpen(false);
		m_sidebar = Sidebar::Comments;
	} else if (commentsHasFocus()) {
		Scroller().Focus(FocusState::Programmatic);
	}
	CommentsButton().IsChecked(open);
	showComments();
}
void ViewerPage::pauseComments() {
	++m_commentVersion;
	m_commentsPending = false;
	if (m_session) {
		m_session->cancelAnnotations();
	}
}
// a filled dot inside a grey ring, drawn at 32 px for a 16 DIP icon. the
// ring keeps black visible on dark menus and yellow on light ones.
IconElement ViewerPage::colorDot(Windows::UI::Color color) {
	constexpr int size = 32;
	constexpr double centre = size / 2., fill = 7.5, outer = 9.5;
	constexpr double grey = 0x8a;
	Media::Imaging::WriteableBitmap bitmap(size, size);
	auto* pixel = bitmap.PixelBuffer().data();
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x, pixel += 4) {
			const double distance =
			    std::hypot(x + .5 - centre, y + .5 - centre);
			const double inside = std::clamp(fill + .5 - distance, 0., 1.);
			const double alpha = std::clamp(outer + .5 - distance, 0., 1.);
			// premultiplied BGRA
			const auto channel = [&](std::uint8_t value) {
				return static_cast<std::uint8_t>(std::lround(
				    (value * inside + grey * (1 - inside)) * alpha));
			};
			pixel[0] = channel(color.B);
			pixel[1] = channel(color.G);
			pixel[2] = channel(color.R);
			pixel[3] = static_cast<std::uint8_t>(std::lround(255 * alpha));
		}
	}
	bitmap.Invalidate();
	ImageIcon icon;
	icon.Source(bitmap);
	return icon;
}

// the color's menu name and the rounded width, for the drawing hint
static hstring inkStyleName(const pdf::InkStyle& style) {
	const wchar_t* name = L"Custom color";
	for (const auto& [label, color] : INK_COLORS) {
		if (color == style.color) {
			name = label;
		}
	}
	return hstring(name) + L", " +
	       to_hstring(static_cast<int>(std::lround(style.width))) + L" pt";
}

// a style choice leaves the tool and its draft alone
void ViewerPage::chooseInkStyle(std::optional<pdf::AnnotationColor> color,
                                std::optional<float> width) {
	if (color) {
		s_inkStyle.color = *color;
	}
	if (width) {
		s_inkStyle.width = *width;
	}
	showComments();
}
void ViewerPage::closeComments() {
	m_inkQueue.clear();
	m_inkWriting = false;
	m_shapeStart.reset();
	m_stampImage.reset();
	m_stampPicking = false;
	dropInkStroke();
	m_activeTool = ActiveTool::Select;
	setCommentsOpen(false);
	if (m_commentVisibility) {
		UnregisterPropertyChangedCallback(UIElement::VisibilityProperty(),
		                                  *m_commentVisibility);
		m_commentVisibility.reset();
	}
	if (m_commentRailVisibility) {
		Rail().UnregisterPropertyChangedCallback(
		    UIElement::VisibilityProperty(), *m_commentRailVisibility);
		m_commentRailVisibility.reset();
	}
	m_commentScan.reset(0);
	CommentsList().Items().Clear();
}
void ViewerPage::showComments() {
	if (m_closed) {
		return;
	}
	updateQuickTools();
	if (Visibility() != XVisibility::Visible) {
		m_shapeStart.reset();
		dropInkStroke();
	}
	const bool canUseTools = !annotationRefusal();
	PlaceNoteButton().IsEnabled(canUseTools ||
	                            m_activeTool == ActiveTool::Note);
	PlaceTextBoxButton().IsEnabled(canUseTools ||
	                               m_activeTool == ActiveTool::TextBox);
	PlaceRectangleButton().IsEnabled(canUseTools ||
	                                 m_activeTool == ActiveTool::Rectangle);
	PlaceRectangleButton().Text(notePlacing() &&
	                                    m_activeTool == PlacementTool::Rectangle
	                                ? L"Cancel rectangle placement"
	                                : L"Place rectangle on page");
	PlaceEllipseButton().IsEnabled(canUseTools ||
	                               m_activeTool == ActiveTool::Ellipse);
	PlaceEllipseButton().Text(notePlacing() &&
	                                  m_activeTool == PlacementTool::Ellipse
	                              ? L"Cancel ellipse placement"
	                              : L"Place ellipse on page");
	for (const auto& [button, tool, text] :
	     {std::tuple{PlaceLineButton(), PlacementTool::Line, L"line"},
	      std::tuple{PlaceArrowButton(), PlacementTool::Arrow, L"arrow"}}) {
		button.IsEnabled(canUseTools || m_activeTool == tool);
		button.Text(notePlacing() && m_activeTool == tool
		                ? L"Cancel " + hstring(text) + L" placement"
		                : L"Place " + hstring(text) + L" on page");
	}
	PlaceStampButton().IsEnabled(canPickStamp() && !m_stampPicking);
	FillMarksMenu().IsEnabled(canUseTools);
	PlaceStampButton().Content(box_value(
	    m_activeTool == ActiveTool::Stamp ? L"Cancel stamp" : L"Stamp\u2026"));
	DrawInkButton().IsEnabled(canUseTools || m_activeTool == ActiveTool::Ink);
	// another tab or window may have changed the shared style
	const auto styles = InkStyleMenu().Items();
	constexpr auto colors = static_cast<std::uint32_t>(std::size(INK_COLORS));
	constexpr auto widths = static_cast<std::uint32_t>(std::size(INK_WIDTHS));
	if (styles.Size() == colors + 1 + widths) {
		for (std::uint32_t i = 0; i < colors; ++i) {
			styles.GetAt(i).as<RadioMenuFlyoutItem>().IsChecked(
			    INK_COLORS[i].color == s_inkStyle.color);
		}
		for (std::uint32_t i = 0; i < widths; ++i) {
			styles.GetAt(colors + 1 + i)
			    .as<RadioMenuFlyoutItem>()
			    .IsChecked(INK_WIDTHS[i].width == s_inkStyle.width);
		}
	}
	AddNoteButton().IsEnabled(canUseTools && !notePlacing());
	const auto selected = CommentsList().SelectedIndex();
	const auto& notes = m_commentScan.rows();
	const bool selectedMarkup =
	    m_selectedAnnotation.has_value() ||
	    (selected >= 0 && static_cast<std::size_t>(selected) < notes.size() &&
	     (notes[selected].subtype == "Highlight" ||
	      notes[selected].subtype == "Underline" ||
	      notes[selected].subtype == "StrikeOut"));
	const bool selectedRow =
	    m_selectedAnnotation.has_value() ||
	    (selected >= 0 && static_cast<std::size_t>(selected) < notes.size());
	CommentActions().Visibility(selectedRow ? XVisibility::Visible
	                                        : XVisibility::Collapsed);
	const bool selectedBox =
	    selected >= 0 && static_cast<std::size_t>(selected) < notes.size() &&
	    notes[selected].subtype == "FreeText";
	const bool selectedShape =
	    selected >= 0 && static_cast<std::size_t>(selected) < notes.size() &&
	    (notes[selected].subtype == "Square" ||
	     notes[selected].subtype == "Circle");
	const bool selectedInk =
	    selected >= 0 && static_cast<std::size_t>(selected) < notes.size() &&
	    notes[selected].subtype == "Ink";
	const bool selectedStamp =
	    selected >= 0 && static_cast<std::size_t>(selected) < notes.size() &&
	    notes[selected].subtype == "Stamp";
	Automation::AutomationProperties::SetName(
	    EditNoteButton(), selectedInk      ? L"Edit selected drawing"
	                      : selectedStamp  ? L"Resize selected stamp"
	                      : selectedShape  ? L"Resize selected shape"
	                      : selectedBox    ? L"Edit selected text box"
	                      : selectedMarkup ? L"Edit selected comment"
	                                       : L"Edit selected note");
	EditNoteButton().IsEnabled(
	    m_noteAllowed && !m_noteBusy && !m_noteSaving && !m_noteDialog &&
	    !notePlacing() &&
	    (selectedShape || selectedInk || selectedStamp || selectedBox ||
	     selectedMarkup ||
	     (selected >= 0 && static_cast<std::size_t>(selected) < notes.size() &&
	      notes[selected].subtype == "Text")));
	DeleteNoteButton().IsEnabled(canDeleteAnnotation());
	for (const auto& control : {PlaceNoteButton().as<FrameworkElement>(),
	                            PlaceTextBoxButton().as<FrameworkElement>(),
	                            PlaceRectangleButton().as<FrameworkElement>(),
	                            PlaceEllipseButton().as<FrameworkElement>(),
	                            PlaceLineButton().as<FrameworkElement>(),
	                            PlaceArrowButton().as<FrameworkElement>(),
	                            PlaceStampButton().as<FrameworkElement>(),
	                            DrawInkButton().as<FrameworkElement>(),
	                            FillMarksMenu().as<FrameworkElement>(),
	                            AddNoteButton().as<FrameworkElement>()}) {
		const auto reason = annotationRefusal();
		ToolTipService::SetToolTip(control,
		                           reason ? box_value(reason) : nullptr);
	}
	CommentsCount().Text(
	    to_hstring(notes.size()) +
	    (m_commentScan.done() && !m_commentScan.limited() ? L"" : L"+"));
	Automation::AutomationProperties::SetName(
	    CommentsCount(), to_hstring(notes.size()) + L" comments loaded");
	NotesState().Text(
	    notePlacing()  ? (m_activeTool == PlacementTool::TextBox
	                          ? L"Click a page to place a text box. "
                               L"Esc cancels."
	                      : m_activeTool == PlacementTool::Ink
	                          ? L"Drag on a page to draw. Esc stops drawing."
	                          : L"Click a page to place a note. Esc cancels.")
	    : m_noteBusy   ? L"Updating comments\u2026"
	    : m_noteSaving ? L"Saving comments\u2026"
	    : m_notesDirty ? L"Unsaved changes"
	                   : L"");
	if (!m_noteAllowed) {
		NotesState().Text(annotationRefusal());
	}
	if (notePlacing() && (m_activeTool == PlacementTool::Rectangle ||
	                      m_activeTool == PlacementTool::Ellipse ||
	                      m_activeTool == PlacementTool::Stamp)) {
		NotesState().Text(m_shapeStart
		                      ? L"Click the opposite corner on the same page. "
		                        L"Esc cancels."
		                  : m_activeTool == PlacementTool::Ellipse
		                      ? L"Click the first corner of the ellipse. "
		                        L"Esc cancels."
		                  : m_activeTool == PlacementTool::Stamp
		                      ? L"Click the first corner of the image stamp. "
		                        L"Esc cancels."
		                      : L"Click the first corner of the rectangle. "
		                        L"Esc cancels.");
	}
	if (notePlacing() && (m_activeTool == PlacementTool::Line ||
	                      m_activeTool == PlacementTool::Arrow)) {
		const bool arrow = m_activeTool == PlacementTool::Arrow;
		NotesState().Text(
		    (m_shapeStart ? (arrow ? L"Click where the arrow points"
		                           : L"Click where the line ends")
		                  : (arrow ? L"Click where the arrow starts"
		                           : L"Click where the line starts")) +
		    hstring(L" (") + inkStyleName(s_inkStyle) + L"). Esc cancels.");
	}
	if (notePlacing() && m_activeTool == PlacementTool::Ink) {
		NotesState().Text(L"Drag on a page to draw (" +
		                  inkStyleName(s_inkStyle) + L"). Esc stops drawing.");
	}
	if (notePlacing() && (m_activeTool == PlacementTool::FillCheck ||
	                      m_activeTool == PlacementTool::FillCross)) {
		NotesState().Text(
		    m_activeTool == PlacementTool::FillCheck
		        ? L"Click a page to place a check mark. Esc cancels."
		        : L"Click a page to place a cross. Esc cancels.");
	}
	if (notePlacing() && m_activeTool == PlacementTool::FillText) {
		NotesState().Text(L"Click a page to place text. Esc cancels.");
	}
	if (notePlacing() && (m_activeTool == PlacementTool::TypedSignature ||
	                      m_activeTool == PlacementTool::DrawnSignature)) {
		NotesState().Text(L"Click a page to place the signature. Esc cancels.");
	}
	NotesState().Visibility(NotesState().Text().empty() ? XVisibility::Collapsed
	                                                    : XVisibility::Visible);
	const bool visible = commentsOpen() &&
	                     Visibility() == XVisibility::Visible &&
	                     Rail().Visibility() == XVisibility::Visible;
	CommentsPanel().Visibility(visible ? XVisibility::Visible
	                                   : XVisibility::Collapsed);
	if (!visible) {
		pauseComments();
		return;
	}
	const auto& rows = m_commentScan.rows();
	for (auto i = CommentsList().Items().Size(); i < rows.size(); ++i) {
		const auto& entry = rows[i];
		Grid content;
		content.Tag(box_value(static_cast<int>(i)));
		content.ColumnSpacing(8);
		content.Margin(Thickness{0, 6, 0, 6});
		ColumnDefinition marker;
		marker.Width(GridLength{22, GridUnitType::Pixel});
		content.ColumnDefinitions().Append(marker);
		ColumnDefinition body;
		body.Width(GridLength{1, GridUnitType::Star});
		content.ColumnDefinitions().Append(body);
		StackPanel marks;
		marks.Spacing(6);
		if (entry.subtype == "StrikeOut") {
			TextBlock icon;
			icon.Text(L"S");
			icon.FontSize(16);
			icon.TextDecorations(
			    Windows::UI::Text::TextDecorations::Strikethrough);
			icon.HorizontalAlignment(HorizontalAlignment::Center);
			marks.Children().Append(icon);
		} else {
			SymbolIcon icon{commentSymbol(entry.subtype)};
			icon.Width(20);
			icon.Height(20);
			marks.Children().Append(icon);
		}
		if (entry.color) {
			Border swatch;
			swatch.Width(12);
			swatch.Height(4);
			swatch.CornerRadius(Microsoft::UI::Xaml::CornerRadius{2});
			swatch.Background(Media::SolidColorBrush(Windows::UI::Color{
			    static_cast<std::uint8_t>(entry.color->alpha),
			    static_cast<std::uint8_t>(entry.color->red),
			    static_cast<std::uint8_t>(entry.color->green),
			    static_cast<std::uint8_t>(entry.color->blue)}));
			marks.Children().Append(swatch);
		}
		content.Children().Append(marks);
		StackPanel detail;
		detail.Spacing(4);
		Grid::SetColumn(detail, 1);
		TextBlock heading;
		heading.Text(
		    L"Page " + to_hstring(entry.page + 1) + L" \u00b7 " +
		    to_hstring(helpers::commentLabel(entry.subtype, entry.line)));
		heading.FontSize(12);
		heading.TextWrapping(TextWrapping::Wrap);
		detail.Children().Append(heading);
		const bool multilineBox = entry.subtype == "FreeText" &&
		                          entry.text.find('\n') != std::string::npos;
		const int bodyLines = multilineBox ? pdf::textBoxMaxLines : 3;
		for (const auto* text : {&entry.author, &entry.text}) {
			if (!text->empty()) {
				TextBlock block;
				block.Text(to_hstring(*text));
				block.TextWrapping(TextWrapping::Wrap);
				block.MaxLines(text == &entry.author ? 1 : bodyLines);
				block.TextTrimming(TextTrimming::CharacterEllipsis);
				if (text == &entry.author) {
					block.FontSize(12);
					block.Opacity(.7);
				}
				detail.Children().Append(block);
			}
		}
		content.Children().Append(detail);
		ListViewItem item;
		item.Content(content);
		item.Tag(box_value(static_cast<int>(i)));
		item.HorizontalContentAlignment(
		    Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
		Automation::AutomationProperties::SetName(
		    item, heading.Text() + L" " + to_hstring(entry.author) + L" " +
		              to_hstring(entry.text));
		MenuFlyout menu;
		const auto choose = [weak = get_weak(), row = static_cast<int>(i),
		                     target = entry.target]() {
			const auto self = weak.get();
			if (!self || self->m_closed) {
				return false;
			}
			const auto& rows = self->m_commentScan.rows();
			if (static_cast<std::size_t>(row) >= rows.size() ||
			    rows[row].target.page != target.page ||
			    rows[row].target.index != target.index ||
			    rows[row].target.subtype != target.subtype ||
			    rows[row].target.revision != target.revision) {
				return false;
			}
			self->clearAnnotationSelection();
			self->CommentsList().SelectedIndex(row);
			self->showComments();
			return true;
		};
		MenuFlyoutItem edit;
		edit.Text(L"Edit");
		edit.Icon(SymbolIcon{Symbol::Edit});
		edit.Click([weak = get_weak(), choose](auto&&, auto&&) {
			if (const auto self = weak.get(); self && choose()) {
				self->beginNoteEdit();
			}
		});
		MenuFlyoutItem remove;
		remove.Text(L"Delete annotation");
		remove.Icon(SymbolIcon{Symbol::Delete});
		remove.Click([weak = get_weak(), choose](auto&&, auto&&) {
			if (const auto self = weak.get(); self && choose()) {
				self->deleteAnnotation();
			}
		});
		menu.Items().Append(edit);
		menu.Items().Append(remove);
		menu.Opening([weak = get_weak(), choose, edit = make_weak(edit),
		              remove = make_weak(remove)](auto&&, auto&&) {
			const auto self = weak.get();
			const bool current = self && choose();
			if (self) {
				if (const auto action = edit.get()) {
					action.IsEnabled(current &&
					                 self->EditNoteButton().IsEnabled());
				}
				if (const auto action = remove.get()) {
					action.IsEnabled(current && self->canDeleteAnnotation());
				}
			}
		});
		item.ContextFlyout(menu);
		CommentsList().Items().Append(item);
	}
	CommentsRetry().Visibility(m_commentScan.failed() ? XVisibility::Visible
	                                                  : XVisibility::Collapsed);
	if (m_commentScan.failed()) {
		CommentsStatus().Text(L"Couldn't load comments on page " +
		                      to_hstring(m_commentScan.nextPage() + 1) + L".");
	} else if (!m_commentScan.done()) {
		CommentsStatus().Text(
		    L"Loading comments\u2026 " +
		    to_hstring(helpers::grouped(m_commentScan.nextPage())) + L" of " +
		    to_hstring(helpers::grouped(m_commentScan.pageCount())) +
		    L" pages");
	} else {
		CommentsStatus().Text(
		    m_commentScan.limited() ? L"Some comments could not be listed."
		    : rows.empty() ? L"No comments yet. Add a note or select text "
		                     L"on a page to highlight it."
		                   : L"");
	}
	CommentsStatus().Visibility(CommentsStatus().Text().empty()
	                                ? XVisibility::Collapsed
	                                : XVisibility::Visible);
	if (!m_commentScan.done() && !m_commentScan.failed() &&
	    !m_commentsPending && !m_noteBusy) {
		requestComments();
	}
}
void ViewerPage::requestComments() {
	if (m_closed || !m_session || m_commentsPending || !commentsOpen() ||
	    m_commentScan.done() || m_commentScan.failed()) {
		return;
	}
	m_commentsPending = true;
	const auto version = ++m_commentVersion;
	const int page = m_commentScan.nextPage();
	const auto session = m_session.get();
	try {
		if (session->annotations(
		        page, [weak = get_weak(), queue = DispatcherQueue(), version,
		               session, page](pdf::AnnotationsResult result) {
			        const auto reply = std::make_shared<pdf::AnnotationsResult>(
			            std::move(result));
			        queue.TryEnqueue([weak, reply, version, session, page] {
				        const auto self = weak.get();
				        if (!self || self->m_closed ||
				            self->m_commentVersion != version ||
				            self->m_session.get() != session) {
					        return;
				        }
				        self->m_commentsPending = false;
				        if (self->Visibility() != XVisibility::Visible ||
				            !self->commentsOpen() ||
				            self->Rail().Visibility() != XVisibility::Visible) {
					        self->pauseComments();
					        return;
				        }
				        if (reply->canceled || reply->page != page ||
				            !reply->annotations ||
				            !self->m_commentScan.append(
				                std::move(*reply->annotations))) {
					        self->m_commentScan.fail();
				        }
				        self->showComments();
			        });
		        })) {
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Couldn't load the comments sidebar.");
	}
	m_commentsPending = false;
	m_commentScan.fail();
	showComments();
}
} // namespace winrt::Sable::implementation
