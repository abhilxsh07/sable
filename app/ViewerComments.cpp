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
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Text.h>
#include <string_view>

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
	PlaceNoteButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->beginNotePlacement();
		}
	});
	SaveNotesButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get(); self && self->m_saveNotes) {
			self->m_saveNotes();
		}
	});
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
	if (m_closed ||
	    (open && (!m_layout || Rail().Visibility() != XVisibility::Visible))) {
		return;
	}
	if (open) {
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
void ViewerPage::closeComments() {
	m_notePlacing = false;
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
	if (!commentsOpen() || Visibility() != XVisibility::Visible ||
	    Rail().Visibility() != XVisibility::Visible) {
		m_notePlacing = false;
	}
	PlaceNoteButton().IsEnabled(m_noteAllowed && !m_noteBusy && !m_noteSaving);
	PlaceNoteButton().Text(m_notePlacing ? L"Cancel placement"
	                                     : L"Place note on page");
	AddNoteButton().IsEnabled(m_noteAllowed && !m_noteBusy && !m_noteSaving &&
	                          !m_notePlacing);
	SaveNotesButton().IsEnabled(m_notesDirty && !m_noteBusy && !m_noteSaving &&
	                            !m_notePlacing);
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
	Automation::AutomationProperties::SetName(
	    EditNoteButton(),
	    selectedMarkup ? L"Edit selected comment" : L"Edit selected note");
	EditNoteButton().IsEnabled(
	    m_noteAllowed && !m_noteBusy && !m_noteSaving && !m_noteDialog &&
	    !m_notePlacing &&
	    (selectedMarkup ||
	     (selected >= 0 && static_cast<std::size_t>(selected) < notes.size() &&
	      notes[selected].subtype == "Text")));
	DeleteNoteButton().IsEnabled(canDeleteAnnotation());
	CommentsCount().Text(
	    to_hstring(notes.size()) +
	    (m_commentScan.done() && !m_commentScan.limited() ? L"" : L"+"));
	Automation::AutomationProperties::SetName(
	    CommentsCount(), to_hstring(notes.size()) + L" comments loaded");
	NotesState().Text(m_notePlacing
	                      ? L"Click a page to place a note. Esc cancels."
	                  : m_noteBusy   ? L"Updating comments\u2026"
	                  : m_noteSaving ? L"Saving comments\u2026"
	                  : m_notesDirty ? L"Unsaved changes"
	                                 : L"");
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
		heading.Text(L"Page " + to_hstring(entry.page + 1) + L" \u00b7 " +
		             to_hstring(helpers::commentLabel(entry.subtype)));
		heading.FontSize(12);
		heading.TextWrapping(TextWrapping::Wrap);
		detail.Children().Append(heading);
		for (const auto* text : {&entry.author, &entry.text}) {
			if (!text->empty()) {
				TextBlock block;
				block.Text(to_hstring(*text));
				block.TextWrapping(TextWrapping::Wrap);
				block.MaxLines(text == &entry.author ? 1 : 3);
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
		    L"Loading comments\u2026 " + to_hstring(m_commentScan.nextPage()) +
		    L" of " + to_hstring(m_commentScan.pageCount()) + L" pages");
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
	    !m_commentsPending) {
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
		diagnostics::write(diagnostics::Level::Error,
		                   "Couldn't load the comments sidebar.");
	}
	m_commentsPending = false;
	m_commentScan.fail();
	showComments();
}
} // namespace winrt::Sable::implementation
