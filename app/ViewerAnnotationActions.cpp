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
#include "helpers/logging.h"
#include "ViewerPage.xaml.h"
#include "pdf/session.h"
#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
std::optional<pdf::AnnotationTarget> ViewerPage::annotationTarget() {
	if (m_selectedAnnotation) {
		return m_selectedAnnotation->target;
	}
	const auto index = CommentsList().SelectedIndex();
	const auto& rows = m_commentScan.rows();
	if (index >= 0 && static_cast<std::size_t>(index) < rows.size()) {
		return rows[index].target;
	}
	return {};
}
bool ViewerPage::canDeleteAnnotation() {
	const auto target = annotationTarget();
	return target && target->revision && m_session && !m_closed &&
	       m_noteAllowed && !m_noteBusy && !m_noteSaving && !m_noteDialog &&
	       !m_notePlacing && !m_presentation &&
	       Visibility() == Microsoft::UI::Xaml::Visibility::Visible &&
	       (!m_notesAvailable || m_notesAvailable()) &&
	       (target->subtype == "Text" || target->subtype == "Highlight" ||
	        target->subtype == "Underline" || target->subtype == "StrikeOut" ||
	        target->subtype == "FreeText" || target->subtype == "Square" ||
	        target->subtype == "Circle" || target->subtype == "Ink" ||
	        target->subtype == "Stamp");
}
void ViewerPage::clearAnnotationSelection() {
	cancelFormRequest();
	++m_annotationVersion;
	m_annotationPending = false;
	m_selectedAnnotation.reset();
	if (m_annotationMenu) {
		m_annotationMenu.Hide();
	}
	invalidateSelection();
}
void ViewerPage::requestAnnotationAt(Windows::Foundation::Point point,
                                     bool context) {
	if (m_closed || m_presentation || !m_session || !m_layout || m_noteBusy ||
	    m_noteSaving || m_noteDialog || m_notePlacing ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
		return;
	}
	clearAnnotationSelection();
	const auto page = m_layout->pageAt(point.X, point.Y);
	const auto& bounds = m_layout->pages()[page];
	const auto source =
	    helpers::rotatePoint({(point.X - bounds.x) / bounds.width,
	                          (point.Y - bounds.y) / bounds.height},
	                         -m_rotation);
	if (source.x < 0 || source.x > 1 || source.y < 0 || source.y > 1) {
		return;
	}
	const auto version = m_annotationVersion;
	const auto rotation = m_rotationVersion;
	const auto zoom = m_zoom;
	const auto at = pixels(page);
	const auto session = m_session.get();
	m_annotationPending = true;
	try {
		if (session->annotationAt(
		        static_cast<int>(page), source.x, source.y,
		        [weak = get_weak(), queue = DispatcherQueue(), session, version,
		         rotation, zoom, at, page, point,
		         context](pdf::AnnotationHitResult result) {
			        queue.TryEnqueue([weak, session, version, rotation, zoom,
			                          at, page, point, context,
			                          result = std::move(result)] {
				        const auto self = weak.get();
				        if (!self || self->m_closed ||
				            self->m_session.get() != session ||
				            self->m_annotationVersion != version) {
					        return;
				        }
				        self->m_annotationPending = false;
				        if (result.canceled || !self->m_layout ||
				            self->m_presentation || self->pixels(page) != at ||
				            self->m_noteBusy || self->m_noteSaving ||
				            self->m_noteDialog || self->m_notePlacing ||
				            self->m_rotationVersion != rotation ||
				            self->m_zoom != zoom ||
				            self->Visibility() !=
				                Microsoft::UI::Xaml::Visibility::Visible) {
					        return;
				        }
				        if (!result.annotation) {
					        if (context) {
						        Primitives::FlyoutShowOptions options;
						        options.Position(point);
						        self->m_textMenu.ShowAt(self->Pages(), options);
					        } else if (self->m_cursorTool != CursorTool::Hand) {
						        self->requestLink(
						            point,
						            self->m_clickSpreads
						                ? self->spreadClickDirection(point)
						                : 0);
					        }
					        return;
				        }
				        self->clearTextSelection();
				        self->CommentsList().SelectedIndex(-1);
				        self->m_selectedAnnotation =
				            std::move(result.annotation);
				        self->invalidateSelection();
				        self->showComments();
				        self->showAnnotationMenu(point);
			        });
		        })) {
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not select a comment");
	}
	m_annotationPending = false;
	showNotice(L"Couldn't select this comment. Try again.");
}
void ViewerPage::showAnnotationMenu(Windows::Foundation::Point point) {
	if (!m_selectedAnnotation) {
		return;
	}
	const auto target = m_selectedAnnotation->target;
	const auto weak = get_weak();
	m_annotationMenu = MenuFlyout();
	MenuFlyoutItem edit;
	edit.Text(L"Edit comment");
	edit.IsEnabled(canDeleteAnnotation());
	edit.Click([weak, target](auto&&, auto&&) {
		if (const auto self = weak.get();
		    self && self->annotationTarget() == target) {
			self->editAnnotation(target);
		}
	});
	m_annotationMenu.Items().Append(edit);
	MenuFlyoutItem remove;
	remove.Text(L"Delete");
	remove.KeyboardAcceleratorTextOverride(L"Del");
	remove.IsEnabled(canDeleteAnnotation());
	remove.Click([weak, target](auto&&, auto&&) {
		if (const auto self = weak.get();
		    self && self->annotationTarget() == target) {
			self->deleteAnnotation();
		}
	});
	m_annotationMenu.Items().Append(remove);
	Primitives::FlyoutShowOptions options;
	options.Position(point);
	m_annotationMenu.ShowAt(Pages(), options);
}
void ViewerPage::editAnnotation(pdf::AnnotationTarget target) {
	if (!canDeleteAnnotation() || annotationTarget() != target) {
		return;
	}
	setCommentsOpen(true);
	std::optional<pdf::TextMarkup> markup;
	if (target.subtype == "Highlight") {
		markup = pdf::TextMarkup::Highlight;
	} else if (target.subtype == "Underline") {
		markup = pdf::TextMarkup::Underline;
	} else if (target.subtype == "StrikeOut") {
		markup = pdf::TextMarkup::StrikeOut;
	}
	const auto session = m_session.get();
	m_noteBusy = true;
	const auto reply = [weak = get_weak(), queue = DispatcherQueue(), session,
	                    target, markup](pdf::TextNoteResult result) {
		queue.TryEnqueue([weak, session, target, markup,
		                  result = std::move(result)] {
			const auto self = weak.get();
			if (!self || self->m_closed || self->m_session.get() != session) {
				return;
			}
			self->m_noteBusy = false;
			self->showComments();
			if (result.contents && !result.canceled &&
			    result.target == target) {
				self->editExistingNote(result.page, result.index,
				                       std::move(*result.contents), markup,
				                       result.contentsPresent, target);
			} else {
				self->showNotice(L"Couldn't edit this comment. It may have "
				                 L"changed or be protected.");
			}
		});
	};
	try {
		if (markup ? session->markupComment(target.page, target.index, *markup,
		                                    reply, target.revision)
		           : session->textNote(target.page, target.index, reply,
		                               target.revision)) {
			showComments();
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue the comment editor");
	}
	m_noteBusy = false;
	showComments();
	showNotice(L"Couldn't queue the comment editor. Try again.");
}
void ViewerPage::deleteAnnotation() {
	if (!canDeleteAnnotation()) {
		return;
	}
	const auto target = *annotationTarget();
	const auto session = m_session.get();
	m_noteBusy = true;
	try {
		if (session->removeAnnotation(
		        target, [weak = get_weak(), queue = DispatcherQueue(),
		                 session](pdf::NoteResult result) {
			        queue.TryEnqueue([weak, session, result] {
				        const auto self = weak.get();
				        if (!self || self->m_closed ||
				            self->m_session.get() != session) {
					        return;
				        }
				        self->m_noteBusy = false;
				        self->m_notesDirty =
				            self->m_notesDirty || result.changed;
				        if (result.changed) {
					        self->clearAnnotationSelection();
					        self->refreshNotes();
				        } else if (!result.canceled) {
					        self->showNotice(writeNotice(
					            result.refusal,
					            L"Couldn't delete this comment. It may have "
					            L"changed or be protected."));
				        }
				        self->showComments();
			        });
		        })) {
			showComments();
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue annotation deletion");
	}
	m_noteBusy = false;
	showComments();
	showNotice(L"Couldn't queue this deletion. Try again.");
}
void ViewerPage::drawSelectedAnnotation(ID2D1DeviceContext& context,
                                        helpers::PageRange pages) {
	if (!m_selectedAnnotation || !m_selectedAnnotation->displayBounds ||
	    m_presentation) {
		return;
	}
	const auto page = m_selectedAnnotation->target.page;
	if (page < 0 || static_cast<std::size_t>(page) < pages.first ||
	    static_cast<std::size_t>(page) >= pages.end) {
		return;
	}
	const auto& bounds = *m_selectedAnnotation->displayBounds;
	const auto box =
	    viewBox({bounds.left, bounds.top, bounds.right, bounds.bottom});
	const auto at = pixels(static_cast<std::size_t>(page));
	const auto rect =
	    D2D1::RectF(static_cast<float>(at.x + box.left * at.width),
	                static_cast<float>(at.y + box.top * at.height),
	                static_cast<float>(at.x + box.right * at.width),
	                static_cast<float>(at.y + box.bottom * at.height));
	context.DrawRectangle(rect, m_device.annotationBackground.get(),
	                      static_cast<float>(3 * m_scale));
	context.DrawRectangle(rect, m_device.annotationOutline.get(),
	                      static_cast<float>(1.5 * m_scale),
	                      m_device.annotationDashes.get());
}
} // namespace winrt::Sable::implementation
