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

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using XVisibility = Microsoft::UI::Xaml::Visibility;
namespace winrt::Sable::implementation {
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
			self->beginNoteEdit();
		}
	});
	CommentsList().SelectionChanged([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
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
	PlaceNoteButton().Content(
	    box_value(m_notePlacing ? L"Cancel placement" : L"Place note on page"));
	AddNoteButton().IsEnabled(m_noteAllowed && !m_noteBusy && !m_noteSaving &&
	                          !m_notePlacing);
	SaveNotesButton().IsEnabled(m_notesDirty && !m_noteBusy && !m_noteSaving &&
	                            !m_notePlacing);
	const auto selected = CommentsList().SelectedIndex();
	const auto& notes = m_commentScan.rows();
	EditNoteButton().IsEnabled(
	    m_noteAllowed && !m_noteBusy && !m_noteSaving && !m_noteDialog &&
	    !m_notePlacing && selected >= 0 &&
	    static_cast<std::size_t>(selected) < notes.size() &&
	    notes[selected].subtype == "Text");
	NotesState().Text(m_notePlacing
	                      ? L"Click a page to place a note. Esc cancels."
	                  : m_noteBusy   ? L"Updating note\u2026"
	                  : m_noteSaving ? L"Saving notes\u2026"
	                  : m_notesDirty ? L"Unsaved notes"
	                                 : L"");
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
		StackPanel content;
		content.Tag(box_value(static_cast<int>(i)));
		content.Spacing(4);
		TextBlock heading;
		heading.Text(L"Page " + to_hstring(entry.page + 1) + L" \u00b7 " +
		             to_hstring(entry.subtype));
		heading.FontSize(12);
		heading.TextWrapping(TextWrapping::Wrap);
		content.Children().Append(heading);
		for (const auto* text : {&entry.author, &entry.text}) {
			if (!text->empty()) {
				TextBlock block;
				block.Text(to_hstring(*text));
				block.TextWrapping(TextWrapping::Wrap);
				content.Children().Append(block);
			}
		}
		ListViewItem item;
		item.Content(content);
		item.Tag(box_value(static_cast<int>(i)));
		item.HorizontalContentAlignment(
		    Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
		Automation::AutomationProperties::SetName(
		    item, heading.Text() + L" " + to_hstring(entry.author) + L" " +
		              to_hstring(entry.text));
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
		    : rows.empty()          ? L"No comments."
		                            : to_hstring(rows.size()) + L" comments.");
	}
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
