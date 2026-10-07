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
#include "helpers/comments.h"
#include "helpers/logging.h"
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
namespace {
// a press has to travel this far, in DIPs, before it moves anything, and a
// press this close outside the outline still picks it up.
constexpr double DRAG_SLOP = 3, GRAB_MARGIN = 4;
} // namespace

bool ViewerPage::movableAnnotation() const {
	return m_selectedAnnotation && m_selectedAnnotation->displayBounds &&
	       m_selectedAnnotation->bounds &&
	       (m_selectedAnnotation->target.subtype == "Ink" ||
	        m_selectedAnnotation->target.subtype == "Stamp");
}

// a press inside the selected drawing's outline starts moving it.
bool ViewerPage::beginAnnotationDrag(Windows::Foundation::Point point) {
	if (m_annotationDrag || m_closed || !m_layout || !movableAnnotation() ||
	    !canDeleteAnnotation()) {
		return false;
	}
	const auto page =
	    static_cast<std::size_t>(m_selectedAnnotation->target.page);
	const auto& pages = m_layout->pages();
	if (page >= pages.size()) {
		return false;
	}
	const auto& rect = pages[page];
	const auto& bounds = *m_selectedAnnotation->displayBounds;
	const auto box =
	    viewBox({bounds.left, bounds.top, bounds.right, bounds.bottom});
	if (point.X < rect.x + box.left * rect.width - GRAB_MARGIN ||
	    point.X > rect.x + box.right * rect.width + GRAB_MARGIN ||
	    point.Y < rect.y + box.top * rect.height - GRAB_MARGIN ||
	    point.Y > rect.y + box.bottom * rect.height + GRAB_MARGIN) {
		return false;
	}
	if (m_annotationMenu) {
		m_annotationMenu.Hide();
	}
	m_annotationDrag = AnnotationDrag{*m_selectedAnnotation, point, point};
	using namespace Microsoft::UI::Input;
	ProtectedCursor(InputSystemCursor::Create(InputSystemCursorShape::SizeAll));
	return true;
}

void ViewerPage::extendAnnotationDrag(Windows::Foundation::Point point) {
	if (!m_annotationDrag || m_annotationDrag->queued ||
	    !std::isfinite(point.X) || !std::isfinite(point.Y)) {
		return;
	}
	m_annotationDrag->current = point;
	invalidateSelection();
}

void ViewerPage::dropAnnotationDrag() {
	if (m_annotationDrag) {
		m_annotationDrag.reset();
		ProtectedCursor(nullptr);
		invalidateSelection();
	}
}

// the release queues the move. the outline stays where it was dropped
// until the write answers.
void ViewerPage::finishAnnotationDrag(Windows::Foundation::Point point) {
	if (!m_annotationDrag || m_annotationDrag->queued) {
		return;
	}
	extendAnnotationDrag(point);
	auto& drag = *m_annotationDrag;
	const auto session = m_session.get();
	const auto page = static_cast<std::size_t>(drag.annotation.target.page);
	if (std::hypot(drag.current.X - drag.start.X,
	               drag.current.Y - drag.start.Y) < DRAG_SLOP ||
	    !session || !m_layout || page >= m_layout->pages().size() ||
	    !canDeleteAnnotation() ||
	    annotationTarget() != drag.annotation.target) {
		dropAnnotationDrag();
		return;
	}
	const auto& rect = m_layout->pages()[page];
	const auto fraction = [&](Windows::Foundation::Point at) {
		return pdf::AnnotationPoint{
		    std::clamp((at.X - rect.x) / rect.width, 0., 1.),
		    std::clamp((at.Y - rect.y) / rect.height, 0., 1.)};
	};
	const auto from = fraction(drag.start), to = fraction(drag.current);
	drag.queued = true;
	m_noteBusy = true;
	const auto reply = [weak = get_weak(), queue = DispatcherQueue(),
	                    session](pdf::NoteResult result) {
		queue.TryEnqueue([weak, session, result] {
			const auto self = weak.get();
			if (!self || self->m_closed || self->m_session.get() != session) {
				return;
			}
			self->m_noteBusy = false;
			self->dropAnnotationDrag();
			self->m_notesDirty = self->m_notesDirty || result.changed;
			if (result.changed) {
				self->clearAnnotationSelection();
				self->refreshNotes(result.page);
			} else if (!result.canceled) {
				self->showNotice(
				    writeNotice(result.refusal,
				                L"Couldn't move this. It may have changed, be "
				                L"protected, or reach past the page."));
			}
			self->showComments();
		});
	};
	try {
		if (drag.annotation.target.subtype == "Stamp"
		        ? session->editStampAt(drag.annotation, from, to,
		                               key(page).rotation, {}, reply)
		        : session->editInkAt(drag.annotation, from, to,
		                             key(page).rotation, {}, {}, reply)) {
			showComments();
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue the move");
	}
	m_noteBusy = false;
	dropAnnotationDrag();
	showNotice(L"Couldn't queue the move. Try again.");
}
// Edit opens this for a drawing: a new color or width, or Keep current.
// moving is a drag on the page.
fire_and_forget ViewerPage::editDrawing(pdf::Annotation existing) {
	const auto lifetime = get_strong();
	try {
		if (!existing.bounds || m_closed || !m_session || !m_noteAllowed ||
		    m_noteBusy || m_noteSaving || m_noteDialog || notePlacing() ||
		    m_passwordDialog || !XamlRoot() || !commentsOpen() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
			co_return;
		}
		leavePageBox();
		const auto session = m_session.get();
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(L"Edit drawing"));
		dialog.PrimaryButtonText(L"Apply");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::Primary);
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(L"Page " + to_hstring(existing.target.page + 1) +
		              L" \u00b7 Pick a new color or width. Drag the drawing "
		              L"on the page to move it.");
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		ComboBox colors, widths;
		colors.Header(box_value(L"Color"));
		widths.Header(box_value(L"Width"));
		colors.Items().Append(box_value(L"Keep current"));
		for (const auto& [label, color] : INK_COLORS) {
			colors.Items().Append(box_value(label));
		}
		widths.Items().Append(box_value(L"Keep current"));
		for (const auto& [label, width] : INK_WIDTHS) {
			widths.Items().Append(box_value(label));
		}
		colors.SelectedIndex(0);
		widths.SelectedIndex(0);
		colors.HorizontalAlignment(HorizontalAlignment::Stretch);
		widths.HorizontalAlignment(HorizontalAlignment::Stretch);
		Automation::AutomationProperties::SetName(colors, L"Drawing color");
		Automation::AutomationProperties::SetName(widths, L"Drawing width");
		fields.Children().Append(colors);
		fields.Children().Append(widths);
		TextBlock error;
		error.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(error);
		dialog.Content(fields);
		dialog.Closing(
		    [weak = get_weak()](const ContentDialog& sender,
		                        const ContentDialogClosingEventArgs& args) {
			    if (const auto self = weak.get();
			        self && self->m_noteBusy && self->m_noteDialog == sender) {
				    args.Cancel(true);
			    }
		    });
		dialog.PrimaryButtonClick(
		    [weak = get_weak(), session, existing, prompt = make_weak(dialog),
		     pickColor = make_weak(colors), pickWidth = make_weak(widths),
		     failure = make_weak(error)](
		        auto&&, const ContentDialogButtonClickEventArgs& args) {
			    args.Cancel(true);
			    const auto self = weak.get();
			    const auto view = prompt.get();
			    const auto c = pickColor.get();
			    const auto w = pickWidth.get();
			    if (!self || self->m_closed || self->m_noteBusy || !view ||
			        !c || !w || self->m_session.get() != session ||
			        self->m_noteDialog != view) {
				    return;
			    }
			    std::optional<pdf::AnnotationColor> color;
			    std::optional<float> width;
			    if (c.SelectedIndex() > 0) {
				    color = INK_COLORS[c.SelectedIndex() - 1].color;
			    }
			    if (w.SelectedIndex() > 0) {
				    width = INK_WIDTHS[w.SelectedIndex() - 1].width;
			    }
			    if (!color && !width) {
				    view.Hide();
				    return;
			    }
			    const auto recover = [weak, session, prompt, pickColor,
			                          pickWidth, failure](hstring message) {
				    const auto owner = weak.get();
				    const auto dialog = prompt.get();
				    if (!owner || owner->m_closed ||
				        owner->m_session.get() != session || !dialog ||
				        owner->m_noteDialog != dialog) {
					    return;
				    }
				    owner->m_noteBusy = false;
				    dialog.IsPrimaryButtonEnabled(true);
				    for (const auto& box : {pickColor.get(), pickWidth.get()}) {
					    if (box) {
						    box.IsEnabled(true);
					    }
				    }
				    if (const auto label = failure.get()) {
					    label.Text(message);
				    }
				    owner->showComments();
			    };
			    self->m_noteBusy = true;
			    view.IsPrimaryButtonEnabled(false);
			    c.IsEnabled(false);
			    w.IsEnabled(false);
			    if (const auto label = failure.get()) {
				    label.Text(L"Changing the drawing\u2026");
			    }
			    try {
				    if (session->editInkAt(
				            existing, {}, {}, 0, color, width,
				            [weak, session, prompt, recover,
				             queue = self->DispatcherQueue()](auto result) {
					            queue.TryEnqueue([weak, session, prompt,
					                              recover, result] {
						            const auto owner = weak.get();
						            if (!owner || owner->m_closed ||
						                owner->m_session.get() != session) {
							            return;
						            }
						            if (!result.index) {
							            recover(writeNotice(
							                result.refusal,
							                L"Couldn't change this drawing. It "
							                L"may not be one Sable drew, be "
							                L"protected, or reach past the "
							                L"page at that width."));
							            return;
						            }
						            owner->m_noteBusy = false;
						            owner->m_notesDirty =
						                owner->m_notesDirty || result.changed;
						            if (const auto shown = prompt.get()) {
							            shown.Hide();
						            }
						            if (result.changed) {
							            owner->clearAnnotationSelection();
							            owner->refreshNotes(result.page);
						            }
						            owner->showComments();
					            });
				            })) {
					    self->showComments();
					    return;
				    }
			    } catch (...) {
				    diagnostics::caught(diagnostics::Level::Error,
				                        "Could not queue the drawing edit");
			    }
			    recover(L"Couldn't queue this change. Try again.");
		    });
		m_noteDialog = dialog;
		co_await dialog.ShowAsync();
		if (m_noteDialog == dialog) {
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showComments();
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open the drawing editor");
		m_noteBusy = false;
		if (m_noteDialog) {
			m_noteDialog.Hide();
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showNotice(L"Couldn't open the drawing editor. Try again.");
			showComments();
		}
	}
}

// Edit opens this for a stamp. The height follows the width, so the image
// keeps its shape, and its top-left corner stays put; moving is a drag.
fire_and_forget ViewerPage::editStampSize(pdf::Annotation existing) {
	const auto lifetime = get_strong();
	try {
		if (!existing.bounds || m_closed || !m_session || !m_noteAllowed ||
		    m_noteBusy || m_noteSaving || m_noteDialog || notePlacing() ||
		    m_passwordDialog || !XamlRoot() || !commentsOpen() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
			co_return;
		}
		leavePageBox();
		const auto session = m_session.get();
		const auto& bounds = *existing.bounds;
		const float oldWidth = bounds.right - bounds.left;
		const float oldHeight = bounds.top - bounds.bottom;
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(L"Resize stamp"));
		dialog.PrimaryButtonText(L"Apply");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(L"Page " + to_hstring(existing.target.page + 1) +
		              L" \u00b7 72 pt = 1 inch. The height follows the width "
		              L"to keep the image's shape, and its top-left corner "
		              L"stays put. Drag the stamp on the page to move it.");
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		TextBox width;
		width.Header(box_value(L"Width (pt)"));
		width.MaxLength(64);
		width.Text(to_hstring(helpers::textBoxDimension(oldWidth)));
		Automation::AutomationProperties::SetName(width,
		                                          L"Stamp width in PDF points");
		fields.Children().Append(width);
		TextBlock height;
		const auto showHeight = [height, oldWidth, oldHeight](float value) {
			height.Text(L"Height: " +
			            to_hstring(helpers::textBoxDimension(value * oldHeight /
			                                                 oldWidth)) +
			            L" pt");
		};
		showHeight(oldWidth);
		fields.Children().Append(height);
		width.TextChanged([showHeight, oldWidth](const auto& sender, auto&&) {
			if (const auto parsed = helpers::parseTextBoxDimension(
			        to_string(sender.template as<TextBox>().Text()),
			        oldWidth)) {
				showHeight(*parsed);
			}
		});
		TextBlock error;
		error.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(error);
		dialog.Content(fields);
		dialog.Opened([width](auto&&, auto&&) {
			width.Focus(FocusState::Programmatic);
			width.SelectAll();
		});
		dialog.Closing(
		    [weak = get_weak()](const ContentDialog& sender,
		                        const ContentDialogClosingEventArgs& args) {
			    if (const auto self = weak.get();
			        self && self->m_noteBusy && self->m_noteDialog == sender) {
				    args.Cancel(true);
			    }
		    });
		const auto recover = [weak = get_weak(), session,
		                      prompt = make_weak(dialog),
		                      wide = make_weak(width),
		                      failure = make_weak(error)](hstring message) {
			const auto self = weak.get();
			const auto view = prompt.get();
			if (!self || self->m_closed || self->m_session.get() != session ||
			    !view || self->m_noteDialog != view) {
				return;
			}
			self->m_noteBusy = false;
			if (const auto w = wide.get()) {
				w.IsReadOnly(false);
			}
			view.IsPrimaryButtonEnabled(true);
			if (const auto label = failure.get()) {
				label.Text(message);
			}
			self->showComments();
		};
		dialog.PrimaryButtonClick([weak = get_weak(), session, existing,
		                           oldWidth, prompt = make_weak(dialog),
		                           wide = make_weak(width),
		                           failure = make_weak(error), recover](
		                              auto&&,
		                              const ContentDialogButtonClickEventArgs&
		                                  args) {
			args.Cancel(true);
			const auto self = weak.get();
			const auto view = prompt.get();
			const auto w = wide.get();
			if (!self || self->m_closed || self->m_noteBusy || !view || !w ||
			    self->m_session.get() != session ||
			    self->m_noteDialog != view) {
				return;
			}
			const auto value =
			    helpers::parseTextBoxDimension(to_string(w.Text()), oldWidth);
			if (!value || *value < 8) {
				recover(L"Enter a width of at least 8 PDF points. Use a "
				        L"decimal point for fractional sizes.");
				return;
			}
			self->m_noteBusy = true;
			view.IsPrimaryButtonEnabled(false);
			w.IsReadOnly(true);
			if (const auto label = failure.get()) {
				label.Text(L"Resizing stamp\u2026");
			}
			try {
				if (session->editStampAt(
				        existing, {}, {}, 0, *value,
				        [weak, session, prompt, recover,
				         queue = self->DispatcherQueue()](auto result) {
					        queue.TryEnqueue([weak, session, prompt, recover,
					                          result] {
						        const auto owner = weak.get();
						        if (!owner || owner->m_closed ||
						            owner->m_session.get() != session) {
							        return;
						        }
						        if (!result.index) {
							        recover(writeNotice(
							            result.refusal,
							            L"Couldn't resize this stamp. It may "
							            L"not be one Sable placed, be "
							            L"protected, or reach past the page "
							            L"at that size."));
							        return;
						        }
						        owner->m_noteBusy = false;
						        owner->m_notesDirty =
						            owner->m_notesDirty || result.changed;
						        if (const auto shown = prompt.get()) {
							        shown.Hide();
						        }
						        if (result.changed) {
							        owner->clearAnnotationSelection();
							        owner->refreshNotes(result.page);
						        }
						        owner->showComments();
					        });
				        })) {
					self->showComments();
					return;
				}
			} catch (...) {
				diagnostics::caught(diagnostics::Level::Error,
				                    "Could not queue the stamp resize");
			}
			recover(L"Couldn't queue this resize. Try again.");
		});
		m_noteDialog = dialog;
		co_await dialog.ShowAsync();
		if (m_noteDialog == dialog) {
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showComments();
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open the stamp editor");
		m_noteBusy = false;
		if (m_noteDialog) {
			m_noteDialog.Hide();
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showNotice(L"Couldn't open the stamp editor. Try again.");
			showComments();
		}
	}
}
// the line keeps its start, direction and pen; the arrowhead is drawn
// again for the new length.
fire_and_forget ViewerPage::editLineLength(pdf::Annotation existing) {
	const auto lifetime = get_strong();
	try {
		if (existing.line == pdf::LineKind::None || m_closed || !m_session ||
		    !m_noteAllowed || m_noteBusy || m_noteSaving || m_noteDialog ||
		    notePlacing() || m_passwordDialog || !XamlRoot() ||
		    !commentsOpen() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
			co_return;
		}
		leavePageBox();
		const auto session = m_session.get();
		const bool arrow = existing.line == pdf::LineKind::Arrow;
		const float oldLength = existing.lineLength;
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(arrow ? L"Resize arrow" : L"Resize line"));
		dialog.PrimaryButtonText(L"Apply");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(L"Page " + to_hstring(existing.target.page + 1) +
		              L" \u00b7 72 pt = 1 inch. The start and the direction "
		              L"stay put" +
		              (arrow ? L" and the head is drawn again." : L".") +
		              L" Drag it on the page to move it.");
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		TextBox length;
		length.Header(box_value(L"Length (pt)"));
		length.MaxLength(64);
		length.Text(to_hstring(helpers::textBoxDimension(oldLength)));
		Automation::AutomationProperties::SetName(
		    length, arrow ? L"Arrow length in PDF points"
		                  : L"Line length in PDF points");
		fields.Children().Append(length);
		TextBlock error;
		error.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(error);
		dialog.Content(fields);
		dialog.Opened([length](auto&&, auto&&) {
			length.Focus(FocusState::Programmatic);
			length.SelectAll();
		});
		dialog.Closing(
		    [weak = get_weak()](const ContentDialog& sender,
		                        const ContentDialogClosingEventArgs& args) {
			    if (const auto self = weak.get();
			        self && self->m_noteBusy && self->m_noteDialog == sender) {
				    args.Cancel(true);
			    }
		    });
		const auto recover = [weak = get_weak(), session,
		                      prompt = make_weak(dialog),
		                      field = make_weak(length),
		                      failure = make_weak(error)](hstring message) {
			const auto self = weak.get();
			const auto view = prompt.get();
			if (!self || self->m_closed || self->m_session.get() != session ||
			    !view || self->m_noteDialog != view) {
				return;
			}
			self->m_noteBusy = false;
			if (const auto f = field.get()) {
				f.IsReadOnly(false);
			}
			view.IsPrimaryButtonEnabled(true);
			if (const auto label = failure.get()) {
				label.Text(message);
			}
			self->showComments();
		};
		dialog.PrimaryButtonClick([weak = get_weak(), session, existing,
		                           oldLength, arrow, prompt = make_weak(dialog),
		                           field = make_weak(length),
		                           failure = make_weak(error), recover](
		                              auto&&,
		                              const ContentDialogButtonClickEventArgs&
		                                  args) {
			args.Cancel(true);
			const auto self = weak.get();
			const auto view = prompt.get();
			const auto f = field.get();
			if (!self || self->m_closed || self->m_noteBusy || !view || !f ||
			    self->m_session.get() != session ||
			    self->m_noteDialog != view) {
				return;
			}
			const auto value =
			    helpers::parseTextBoxDimension(to_string(f.Text()), oldLength);
			if (!value || *value < 4) {
				recover(L"Enter a length of at least 4 PDF points. Use a "
				        L"decimal point for fractional sizes.");
				return;
			}
			self->m_noteBusy = true;
			view.IsPrimaryButtonEnabled(false);
			f.IsReadOnly(true);
			if (const auto label = failure.get()) {
				label.Text(arrow ? L"Resizing arrow\u2026"
				                 : L"Resizing line\u2026");
			}
			try {
				if (session->resizeLine(
				        existing, *value,
				        [weak, session, prompt, recover, arrow,
				         queue = self->DispatcherQueue()](auto result) {
					        queue.TryEnqueue([weak, session, prompt, recover,
					                          arrow, result] {
						        const auto owner = weak.get();
						        if (!owner || owner->m_closed ||
						            owner->m_session.get() != session) {
							        return;
						        }
						        if (!result.index) {
							        recover(writeNotice(
							            result.refusal,
							            arrow ? L"Couldn't resize this arrow. "
							                    L"It may not be one Sable "
							                    L"drew, be protected, or reach "
							                    L"past the page at that length."
							                  : L"Couldn't resize this line. "
							                    L"It may not be one Sable "
							                    L"drew, be protected, or reach "
							                    L"past the page at that "
							                    L"length."));
							        return;
						        }
						        owner->m_noteBusy = false;
						        owner->m_notesDirty =
						            owner->m_notesDirty || result.changed;
						        if (const auto shown = prompt.get()) {
							        shown.Hide();
						        }
						        if (result.changed) {
							        owner->clearAnnotationSelection();
							        owner->refreshNotes(result.page);
						        }
						        owner->showComments();
					        });
				        })) {
					self->showComments();
					return;
				}
			} catch (...) {
				diagnostics::caught(diagnostics::Level::Error,
				                    "Could not queue the line resize");
			}
			recover(L"Couldn't queue this resize. Try again.");
		});
		m_noteDialog = dialog;
		co_await dialog.ShowAsync();
		if (m_noteDialog == dialog) {
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showComments();
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open the line editor");
		m_noteBusy = false;
		if (m_noteDialog) {
			m_noteDialog.Hide();
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showNotice(L"Couldn't open the line editor. Try again.");
			showComments();
		}
	}
}
} // namespace winrt::Sable::implementation
