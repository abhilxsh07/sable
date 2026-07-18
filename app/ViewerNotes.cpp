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
#include "StampImage.h"
#include "pdf/session.h"
#include "pdf/page_filter.h"
#include "helpers/logging.h"
#include "helpers/filter.h"
#include <algorithm>
#include <cmath>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::Sable::implementation {
bool ViewerPage::canPickStamp() {
	return !m_closed && m_session && m_layout && m_noteAllowed && !m_noteBusy &&
	       !m_noteSaving && !m_noteDialog && !m_passwordDialog &&
	       !m_linkPending && !m_presentation && commentsOpen() &&
	       Rail().Visibility() == Microsoft::UI::Xaml::Visibility::Visible &&
	       Visibility() == Microsoft::UI::Xaml::Visibility::Visible &&
	       (!m_notesAvailable || m_notesAvailable());
}

fire_and_forget ViewerPage::pickStamp() {
	const auto weak = get_weak();
	std::uint64_t version = 0;
	pdf::Session* session = nullptr;
	try {
		if (m_notePlacing && m_placementTool == PlacementTool::Stamp) {
			cancelNotePlacement();
			co_return;
		}
		if (!canPickStamp() || m_stampPicking) {
			co_return;
		}
		cancelNotePlacement();
		m_stampPicking = true;
		version = ++m_stampPickVersion;
		session = m_session.get();
		showComments();
		hstring path;
#ifdef SABLE_SURFACE_TEST
		if (m_stampPicker) {
			path = co_await m_stampPicker();
		} else
#endif
		{
			path = co_await pickStampFile(
			    XamlRoot().ContentIslandEnvironment().AppWindowId());
		}
		{
			const auto self = weak.get();
			if (!self || !self->m_stampPicking ||
			    self->m_stampPickVersion != version ||
			    self->m_session.get() != session) {
				co_return;
			}
			if (path.empty() || !self->canPickStamp()) {
				self->m_stampPicking = false;
				self->showComments();
				co_return;
			}
		}
		const auto image = std::make_shared<pdf::StampImage>();
		co_await decodeStampImage(path, image);
		if (const auto self = weak.get(); self && self->m_stampPicking &&
		                                  self->m_stampPickVersion == version &&
		                                  self->m_session.get() == session) {
			self->m_stampPicking = false;
			if (self->canPickStamp()) {
				self->beginNotePlacement(PlacementTool::Stamp);
				if (self->m_notePlacing &&
				    self->m_placementTool == PlacementTool::Stamp) {
					self->m_stampImage = std::move(*image);
				}
			}
			self->showComments();
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not pick an image stamp");
		try {
			if (const auto self = weak.get();
			    self && version && self->m_stampPicking &&
			    self->m_stampPickVersion == version &&
			    self->m_session.get() == session) {
				self->m_stampPicking = false;
				self->showComments();
				self->showNotice(
				    L"Couldn't open the stamp image. Choose a PNG or JPEG "
				    L"up to 2048 pixels per side and 32 MiB.");
			}
		} catch (...) {
			diagnostics::caught(diagnostics::Level::Error,
			                    "Couldn't report the stamp picker error.");
		}
	}
}

void ViewerPage::beginNotePlacement(PlacementTool tool) {
	clearAnnotationSelection();
	if (m_notePlacing && m_placementTool == tool) {
		cancelNotePlacement();
		return;
	}
	if (m_closed || !m_layout || !m_noteAllowed || m_noteBusy || m_noteSaving ||
	    m_noteDialog || m_linkPending || !commentsOpen() ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
	    (m_notesAvailable && !m_notesAvailable())) {
		return;
	}
	leavePageBox();
	cancelSelectionDrag();
	m_ignoreTextTap = false;
	m_shapeStart.reset();
	m_stampPicking = false;
	m_stampImage.reset();
	dropInkStroke();
	m_placementTool = tool;
	m_notePlacing = true;
	showComments();
	Scroller().Focus(FocusState::Programmatic);
}

bool ViewerPage::cancelNotePlacement() {
	m_shapeStart.reset();
	m_stampImage.reset();
	dropInkStroke();
	const bool canceled = std::exchange(m_notePlacing, false);
	const bool canceledPicker = std::exchange(m_stampPicking, false);
	if ((canceled || canceledPicker) && !m_closed) {
		showComments();
	}
	return canceled || canceledPicker;
}

bool ViewerPage::placeNote(Windows::Foundation::Point point) {
	if (!m_notePlacing) {
		return false;
	}
	if (m_closed || !m_layout || !commentsOpen() ||
	    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
	    (m_notesAvailable && !m_notesAvailable())) {
		cancelNotePlacement();
		return true;
	}
	if (!std::isfinite(point.X) || !std::isfinite(point.Y) ||
	    m_placementTool == PlacementTool::Ink) {
		// strokes come from the pointer handlers; a click ends its own.
		return true;
	}
	const auto& pages = m_layout->pages();
	for (std::size_t page = 0; page < pages.size(); ++page) {
		const auto& rect = pages[page];
		const double x = (point.X - rect.x) / rect.width;
		const double y = (point.Y - rect.y) / rect.height;
		if (std::isfinite(x) && std::isfinite(y) && x >= 0 && x <= 1 &&
		    y >= 0 && y <= 1) {
			const NoteTarget target{static_cast<int>(page), x, y,
			                        key(page).rotation};
			if (m_placementTool == PlacementTool::Rectangle ||
			    m_placementTool == PlacementTool::Ellipse ||
			    m_placementTool == PlacementTool::Stamp) {
				placeShape(target);
				break;
			}
			if (m_placementTool == PlacementTool::FillCheck ||
			    m_placementTool == PlacementTool::FillCross) {
				placeFillMark(target);
				break;
			}
			m_notePlacing = false;
			showComments();
			if (m_placementTool == PlacementTool::TextBox) {
				editTextBox(target);
			} else {
				editNote(target);
			}
			break;
		}
	}
	return true;
}

std::function<void(pdf::NoteResult)>
ViewerPage::placementReply(pdf::Session* session, const wchar_t* failure) {
	return [weak = get_weak(), queue = DispatcherQueue(), session,
	        failure](pdf::NoteResult result) {
		queue.TryEnqueue([weak, session, result, failure] {
			const auto self = weak.get();
			if (!self || self->m_closed || self->m_session.get() != session) {
				return;
			}
			self->m_noteBusy = false;
			self->m_notesDirty = self->m_notesDirty || result.changed;
			if (result.changed) {
				self->Message().IsOpen(false);
				self->refreshNotes();
			} else if (!result.canceled) {
				self->showNotice(writeNotice(result.refusal, failure));
			}
			self->showComments();
		});
	};
}

void ViewerPage::placeFillMark(NoteTarget target) {
	const auto session = m_session.get();
	const auto revision = session ? session->annotationRevision() : 0;
	if (!revision || !m_noteAllowed || m_noteBusy || m_noteSaving ||
	    m_noteDialog || m_passwordDialog) {
		cancelNotePlacement();
		return;
	}
	const auto mark = m_placementTool == PlacementTool::FillCheck
	                      ? pdf::FillMark::Check
	                      : pdf::FillMark::Cross;
	m_notePlacing = false;
	m_noteBusy = true;
	try {
		if (session->addFillMarkAt(
		        target.page, {target.x, target.y}, target.rotation, mark,
		        revision,
		        placementReply(session,
		                       L"Couldn't add this mark. The page "
		                       L"may be too small or restrict comments."))) {
			showComments();
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue the fill mark");
	}
	m_noteBusy = false;
	showComments();
	showNotice(L"Couldn't queue this mark. Try again.");
}

void ViewerPage::placeShape(NoteTarget target) {
	const bool ellipse = m_placementTool == PlacementTool::Ellipse;
	const bool stamp = m_placementTool == PlacementTool::Stamp;
	const auto session = m_session.get();
	const auto revision = session ? session->annotationRevision() : 0;
	if (!revision || !m_noteAllowed || m_noteBusy || m_noteSaving ||
	    m_noteDialog || m_passwordDialog) {
		cancelNotePlacement();
		return;
	}
	if (!m_shapeStart) {
		m_shapeStart = ShapeStart{target, revision};
		showComments();
		return;
	}
	const auto [first, expected] = *m_shapeStart;
	if (revision != expected) {
		m_shapeStart.reset();
		showComments();
		showNotice(L"The PDF changed. Click the first corner again.");
		return;
	}
	if (first.page != target.page) {
		showNotice(L"Choose the opposite corner on the same page.");
		return;
	}
	m_shapeStart.reset();
	m_notePlacing = false;
	m_noteBusy = true;
	try {
		const auto write = ellipse ? &pdf::Session::addEllipseAt
		                           : &pdf::Session::addRectangleAt;
		const auto failure =
		    stamp ? L"Couldn't add this image stamp. Try a larger area. "
		            L"This PDF may restrict comments."
		    : ellipse ? L"Couldn't add this ellipse. Try a larger area. "
		                L"This PDF may restrict comments."
		              : L"Couldn't add this rectangle. Try a larger area. "
		                L"This PDF may restrict comments.";
		auto callback = placementReply(session, failure);
		const pdf::AnnotationPoint start{first.x, first.y},
		    end{target.x, target.y};
		auto image = std::exchange(m_stampImage, std::nullopt);
		const bool queued =
		    stamp ? image &&
		                session->addStampAt(target.page, start, end,
		                                    first.rotation, std::move(*image),
		                                    expected, std::move(callback))
		          : (session->*write)(target.page, start, end, first.rotation,
		                              expected, std::move(callback));
		if (queued) {
			showComments();
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue the page shape");
	}
	m_noteBusy = false;
	showComments();
	showNotice(stamp     ? L"Couldn't queue this image stamp. Try again."
	           : ellipse ? L"Couldn't queue this ellipse. Try again."
	                     : L"Couldn't queue this rectangle. Try again.");
}

// mouse and pen strokes each become one Ink annotation. touch keeps
// scrolling the pages, as it does for text selection.
bool ViewerPage::beginInkStroke(Windows::Foundation::Point point) {
	// a queued stroke keeps its preview until its write finishes
	if (m_inkStroke) {
		dropInkStroke();
	}
	const auto session = m_session.get();
	const auto revision = session ? session->annotationRevision() : 0;
	if (!m_notePlacing || m_placementTool != PlacementTool::Ink || m_closed ||
	    !m_layout || !revision || !m_noteAllowed || m_noteBusy ||
	    m_noteSaving || m_noteDialog || m_passwordDialog || !commentsOpen() ||
	    !std::isfinite(point.X) || !std::isfinite(point.Y)) {
		return false;
	}
	const auto& pages = m_layout->pages();
	for (std::size_t page = 0; page < pages.size(); ++page) {
		const auto& rect = pages[page];
		if (point.X >= rect.x && point.X <= rect.x + rect.width &&
		    point.Y >= rect.y && point.Y <= rect.y + rect.height) {
			m_inkStroke = InkDrawing{static_cast<int>(page),
			                         key(page).rotation,
			                         revision,
			                         {},
			                         s_inkStyle};
			// the preview shows the color through the reading filter, the
			// way the page will draw the saved line
			const auto& color = s_inkStyle.color;
			const auto shown = helpers::applyFilter(
			    m_values, {static_cast<std::uint8_t>(color.red),
			               static_cast<std::uint8_t>(color.green),
			               static_cast<std::uint8_t>(color.blue)});
			InkPreviewLine().Stroke(Media::SolidColorBrush(
			    Windows::UI::Color{255, shown.r, shown.g, shown.b}));
			extendInkStroke(point);
			InkPreview().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
			return true;
		}
	}
	return false;
}

void ViewerPage::extendInkStroke(Windows::Foundation::Point point) {
	if (!m_inkStroke || !m_layout || !std::isfinite(point.X) ||
	    !std::isfinite(point.Y)) {
		return;
	}
	auto& stroke = *m_inkStroke;
	const auto page = static_cast<std::size_t>(stroke.page);
	const auto& pages = m_layout->pages();
	if (page >= pages.size() || page >= m_sizes.size() ||
	    stroke.points.size() >= 4096) {
		return;
	}
	const auto& rect = pages[page];
	const auto& size = m_sizes[page];
	const double shortest = (std::min)(size.width, size.height);
	if (!(rect.width > 0 && rect.height > 0 && shortest > 4)) {
		return;
	}
	// the saved line has to stay on the page, so its centre keeps half its
	// width and a little more from every edge.
	const double inset = (stroke.style.width / 2 + .5) / shortest;
	const pdf::AnnotationPoint at{
	    std::clamp((point.X - rect.x) / rect.width, inset, 1 - inset),
	    std::clamp((point.Y - rect.y) / rect.height, inset, 1 - inset)};
	if (!stroke.points.empty()) {
		const auto& last = stroke.points.back();
		if (std::abs(at.x - last.x) * rect.width < .5 &&
		    std::abs(at.y - last.y) * rect.height < .5) {
			return;
		}
	}
	stroke.points.push_back(at);
	const double scale =
	    Scroller().ZoomFactor() *
	    std::sqrt(rect.width * rect.height / (size.width * size.height));
	InkPreviewLine().StrokeThickness(stroke.style.width * scale);
	const auto shown =
	    Pages()
	        .TransformToVisual(InkPreview())
	        .TransformPoint({static_cast<float>(rect.x + at.x * rect.width),
	                         static_cast<float>(rect.y + at.y * rect.height)});
	auto points = InkPreviewLine().Points();
	points.Append(shown);
	if (stroke.points.size() == 1) {
		// a polyline needs two points before its round caps draw a dot
		points.Append({shown.X + .01f, shown.Y});
	}
}

void ViewerPage::finishInkStroke() {
	if (!m_inkStroke) {
		return;
	}
	auto stroke = std::move(*m_inkStroke);
	m_inkStroke.reset();
	const auto session = m_session.get();
	if (m_closed || !session || !m_notePlacing ||
	    m_placementTool != PlacementTool::Ink || stroke.points.empty() ||
	    m_noteBusy || m_noteSaving || m_noteDialog || m_passwordDialog) {
		dropInkStroke();
		return;
	}
	if (session->annotationRevision() != stroke.revision) {
		dropInkStroke();
		showNotice(L"The PDF changed. Draw the stroke again.");
		return;
	}
	m_noteBusy = true;
	try {
		if (session->addInkAt(
		        stroke.page, {std::move(stroke.points)}, stroke.rotation,
		        stroke.revision,
		        [weak = get_weak(), queue = DispatcherQueue(),
		         session](pdf::NoteResult result) {
			        queue.TryEnqueue([weak, session, result] {
				        const auto self = weak.get();
				        if (!self || self->m_closed ||
				            self->m_session.get() != session) {
					        return;
				        }
				        self->m_noteBusy = false;
				        self->dropInkStroke();
				        self->m_notesDirty =
				            self->m_notesDirty || result.changed;
				        if (result.changed) {
					        self->Message().IsOpen(false);
					        self->refreshNotes();
				        } else if (!result.canceled) {
					        self->showNotice(writeNotice(
					            result.refusal, L"Couldn't add this drawing. "
					                            L"This PDF may restrict "
					                            L"comments."));
				        }
				        self->showComments();
			        });
		        },
		        stroke.style)) {
			showComments();
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue the drawing");
	}
	m_noteBusy = false;
	dropInkStroke();
	showComments();
	showNotice(L"Couldn't queue this drawing. Try again.");
}

void ViewerPage::dropInkStroke() {
	m_inkStroke.reset();
	InkPreviewLine().Points().Clear();
	InkPreview().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
}

fire_and_forget
ViewerPage::editTextBox(NoteTarget target,
                        std::optional<pdf::Annotation> existing) {
	const auto lifetime = get_strong();
	try {
		if ((existing && !existing->bounds) || m_closed || !m_layout ||
		    !m_session || !m_noteAllowed || m_noteBusy || m_noteSaving ||
		    m_noteDialog || m_passwordDialog || !commentsOpen() ||
		    !XamlRoot() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    (m_notesAvailable && !m_notesAvailable())) {
			co_return;
		}
		leavePageBox();
		const auto session = m_session.get();
		std::uint64_t revision = existing ? existing->target.revision : 0;
		if (!existing) {
			struct Snapshot {
				handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
				pdf::FormFieldsResult result;
			};
			const auto snapshot = std::make_shared<Snapshot>();
			check_bool(static_cast<bool>(snapshot->ready));
			m_noteBusy = true;
			showComments();
			const apartment_context ui;
			check_bool(
			    session->formFields(target.page, [snapshot](auto result) {
				    snapshot->result = std::move(result);
				    SetEvent(snapshot->ready.get());
			    }));
			co_await resume_on_signal(snapshot->ready.get());
			co_await ui;
			m_noteBusy = false;
			if (m_closed || m_session.get() != session || !commentsOpen() ||
			    Visibility() != Microsoft::UI::Xaml::Visibility::Visible) {
				co_return;
			}
			if (!snapshot->result.fields || snapshot->result.canceled) {
				showNotice(
				    L"Couldn't read this page for a text box. Try again.");
				showComments();
				co_return;
			}
			revision = snapshot->result.fields->revision;
		}
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(existing ? L"Edit text box" : L"Add text box"));
		dialog.PrimaryButtonText(existing ? L"Apply" : L"Add");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(
		    L"Page " + to_hstring(target.page + 1) +
		    L" \u00b7 Up to 8 lines and 128 characters. Use English letters, "
		    L"numbers, spaces and standard punctuation. Press Enter for a "
		    L"new line. The text must fit inside the box.");
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		TextBox author;
		author.Header(box_value(existing ? L"Author" : L"Author (optional)"));
		author.MaxLength(32767);
		author.Text(existing ? to_hstring(existing->author) : m_textBoxAuthor);
		author.IsReadOnly(existing.has_value());
		fields.Children().Append(author);
		TextBox contents;
		contents.Header(box_value(L"Text"));
		contents.AcceptsReturn(true);
		contents.TextWrapping(TextWrapping::Wrap);
		contents.MinHeight(100);
		contents.MaxHeight(180);
		contents.MinWidth(280);
		contents.Text(existing ? to_hstring(existing->contents)
		                       : m_textBoxDraft);
		Automation::AutomationProperties::SetName(contents, L"Text box text");
		fields.Children().Append(contents);
		TextBox width, height;
		if (existing) {
			width.Header(box_value(L"Width (pt)"));
			height.Header(box_value(L"Height (pt)"));
			width.MaxLength(64);
			height.MaxLength(64);
			const auto& rect = *existing->bounds;
			width.Text(
			    to_hstring(helpers::textBoxDimension(rect.right - rect.left)));
			height.Text(
			    to_hstring(helpers::textBoxDimension(rect.top - rect.bottom)));
			Automation::AutomationProperties::SetName(
			    width, L"Text box width in PDF points");
			Automation::AutomationProperties::SetName(
			    height, L"Text box height in PDF points");
			Grid sizes;
			sizes.ColumnSpacing(12);
			sizes.ColumnDefinitions().Append(ColumnDefinition{});
			sizes.ColumnDefinitions().Append(ColumnDefinition{});
			Grid::SetColumn(height, 1);
			sizes.Children().Append(width);
			sizes.Children().Append(height);
			fields.Children().Append(sizes);
			TextBlock units;
			units.Text(
			    L"Size uses the PDF's original orientation. 72 pt = 1 inch. "
			    L"The top-left corner stays fixed.");
			units.TextWrapping(TextWrapping::Wrap);
			fields.Children().Append(units);
		}
		TextBlock error;
		error.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(error);
		ScrollViewer scroll;
		scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
		scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
		scroll.Padding({0, 0, 12, 0});
		scroll.Content(fields);
		const auto fit = [body = make_weak(scroll),
		                  root = make_weak(XamlRoot())] {
			if (const auto content = body.get()) {
				if (const auto host = root.get()) {
					content.MaxHeight(
					    (std::max)(80., host.Size().Height - 210.));
				}
			}
		};
		fit();
		const auto sizeChanged =
		    XamlRoot().Changed(auto_revoke, [fit](auto&&, auto&&) { fit(); });
		dialog.Content(scroll);
		dialog.IsPrimaryButtonEnabled(!contents.Text().empty());
		contents.TextChanged(
		    [prompt = make_weak(dialog)](auto&& sender, auto&&) {
			    if (const auto view = prompt.get()) {
				    view.IsPrimaryButtonEnabled(
				        !sender.template as<TextBox>().Text().empty());
			    }
		    });
		dialog.Opened([contents](auto&&, auto&&) {
			contents.Focus(FocusState::Programmatic);
			contents.SelectAll();
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
		                      editing = existing.has_value(),
		                      prompt = make_weak(dialog),
		                      text = make_weak(contents),
		                      writer = make_weak(author),
		                      wide = make_weak(width), tall = make_weak(height),
		                      failure = make_weak(error)](hstring message) {
			const auto self = weak.get();
			const auto view = prompt.get();
			const auto field = text.get();
			const auto name = writer.get();
			if (!self || self->m_closed || self->m_session.get() != session ||
			    !view || !field || !name || self->m_noteDialog != view) {
				return;
			}
			self->m_noteBusy = false;
			field.IsReadOnly(false);
			name.IsReadOnly(editing);
			if (const auto dimension = wide.get()) {
				dimension.IsReadOnly(false);
			}
			if (const auto dimension = tall.get()) {
				dimension.IsReadOnly(false);
			}
			view.IsPrimaryButtonEnabled(!field.Text().empty());
			if (const auto label = failure.get()) {
				label.Text(message);
			}
			self->showComments();
		};
		dialog.PrimaryButtonClick([weak = get_weak(), session, target, revision,
		                           existing, prompt = make_weak(dialog),
		                           text = make_weak(contents),
		                           writer = make_weak(author),
		                           failure = make_weak(error),
		                           wide = make_weak(width),
		                           tall = make_weak(height), recover](
		                              auto&&,
		                              const ContentDialogButtonClickEventArgs&
		                                  args) {
			args.Cancel(true);
			const auto self = weak.get();
			const auto view = prompt.get();
			const auto field = text.get();
			const auto name = writer.get();
			if (!self || self->m_closed || self->m_noteBusy || !view ||
			    !field || !name || field.Text().empty() ||
			    self->m_session.get() != session) {
				return;
			}
			std::optional<pdf::TextBoxSize> size;
			if (existing) {
				const auto w = wide.get(), h = tall.get();
				if (!w || !h) {
					return;
				}
				const auto parsedWidth = helpers::parseTextBoxDimension(
				    to_string(w.Text()),
				    existing->bounds->right - existing->bounds->left);
				const auto parsedHeight = helpers::parseTextBoxDimension(
				    to_string(h.Text()),
				    existing->bounds->top - existing->bounds->bottom);
				if (!parsedWidth || !parsedHeight) {
					if (const auto label = failure.get()) {
						label.Text(
						    L"Enter positive width and height in PDF points. "
						    L"Use a decimal point for fractional sizes. Your "
						    L"draft is still here.");
					}
					return;
				}
				size = pdf::TextBoxSize{*parsedWidth, *parsedHeight};
			}
			const auto value = field.Text();
			std::u16string replacement;
			constexpr auto layout = pdf::TextBoxLayout::ExplicitLines;
			replacement.reserve(
			    (std::min)(value.size(), pdf::textBoxMaxCharacters + 1));
			for (hstring::size_type i = 0;
			     i < value.size() &&
			     replacement.size() <= pdf::textBoxMaxCharacters;
			     ++i) {
				if (value[i] == L'\r') {
					replacement.push_back(u'\n');
					if (i + 1 < value.size() && value[i + 1] == L'\n') {
						++i;
					}
				} else {
					replacement.push_back(static_cast<char16_t>(value[i]));
				}
			}
			const wchar_t* problem = nullptr;
			switch (pdf::validateTextBoxText(replacement, layout)) {
			case pdf::TextBoxTextError::None:
				break;
			case pdf::TextBoxTextError::Blank:
				problem = L"Enter text on at least one line.";
				break;
			case pdf::TextBoxTextError::Length:
				problem = L"Use up to 128 characters, including line breaks.";
				break;
			case pdf::TextBoxTextError::Lines:
				problem = L"Use up to 8 lines, including blank lines.";
				break;
			case pdf::TextBoxTextError::Characters:
				problem = L"Use English letters, numbers, spaces and standard "
				          L"punctuation.";
				break;
			}
			if (problem) {
				if (const auto label = failure.get()) {
					label.Text(hstring(problem) +
					           L" Your draft is still here.");
				}
				return;
			}
			self->m_noteBusy = true;
			view.IsPrimaryButtonEnabled(false);
			field.IsReadOnly(true);
			name.IsReadOnly(true);
			if (const auto dimension = wide.get()) {
				dimension.IsReadOnly(true);
			}
			if (const auto dimension = tall.get()) {
				dimension.IsReadOnly(true);
			}
			if (const auto label = failure.get()) {
				label.Text(existing ? L"Applying text box\u2026"
				                    : L"Adding text box\u2026");
			}
			try {
				const auto by = name.Text();
				const auto completed =
				    [weak, session, prompt, text, recover,
				     editing = existing.has_value(),
				     queue = self->DispatcherQueue()](auto result) {
					    queue.TryEnqueue([weak, session, prompt, text, recover,
					                      editing, result] {
						    const auto owner = weak.get();
						    if (!owner || owner->m_closed ||
						        owner->m_session.get() != session) {
							    return;
						    }
						    try {
							    if (result.index) {
								    owner->m_noteBusy = false;
								    owner->m_notesDirty =
								        owner->m_notesDirty || result.changed;
								    if (const auto input = text.get();
								        input && !editing) {
									    input.Text(L"");
								    }
								    if (const auto dialog = prompt.get()) {
									    dialog.Hide();
								    }
								    if (result.changed) {
									    owner->refreshNotes();
								    }
								    owner->showComments();
							    } else {
								    recover(writeNotice(
								        result.refusal,
								        L"Couldn't apply this text box. "
								        L"The PDF may restrict "
								        L"this change, or the text may "
								        L"not fit inside the page or box. "
								        L"Check the text and box size. "
								        L"Your draft is still here."));
							    }
						    } catch (...) {
							    diagnostics::caught(
							        diagnostics::Level::Error,
							        "Couldn't refresh the text box.");
						    }
					    });
				    };
				const bool queued =
				    existing ? session->updateTextBox(*existing,
				                                      std::move(replacement),
				                                      *size, completed, layout)
				             : session->addTextBoxAt(
				                   target.page, target.x, target.y,
				                   target.rotation, std::move(replacement),
				                   std::u16string(by.begin(), by.end()),
				                   revision, completed, layout);
				if (queued) {
					self->showComments();
					return;
				}
			} catch (...) {
				diagnostics::caught(diagnostics::Level::Error,
				                    "Could not queue the text box");
			}
			recover(L"Couldn't queue this text box. Your draft is still here.");
		});
		m_noteDialog = dialog;
		co_await dialog.ShowAsync();
		if (m_noteDialog == dialog) {
			m_noteDialog = nullptr;
		}
		if (!existing) {
			m_textBoxDraft = contents.Text();
			m_textBoxAuthor = author.Text();
		}
		if (!m_closed) {
			showComments();
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open the text box editor");
		m_noteBusy = false;
		if (m_noteDialog) {
			m_noteDialog.Hide();
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showNotice(L"Couldn't open the text box editor. Try again.");
			showComments();
		}
	}
}

fire_and_forget ViewerPage::editNote(std::optional<NoteTarget> target) {
	const auto lifetime = get_strong();
	try {
		if (m_closed || !m_layout || !m_session || !m_noteAllowed ||
		    m_noteBusy || m_noteSaving || m_noteDialog || m_notePlacing ||
		    m_passwordDialog || !XamlRoot() ||
		    Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
		    (m_notesAvailable && !m_notesAvailable())) {
			co_return;
		}
		leavePageBox();
		const int page = target ? target->page : static_cast<int>(m_current);
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(L"Add note"));
		dialog.PrimaryButtonText(L"Add");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::None);
		dialog.IsPrimaryButtonEnabled(!m_noteDraft.empty());
		StackPanel fields;
		fields.Spacing(12);
		TextBlock location;
		location.Text(L"Page " + to_hstring(page + 1));
		location.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(location);
		TextBox author;
		author.Header(box_value(L"Author (optional)"));
		author.MaxLength(32767);
		author.Text(m_noteAuthor);
		fields.Children().Append(author);
		TextBox contents;
		contents.Header(box_value(L"Note"));
		contents.AcceptsReturn(true);
		contents.TextWrapping(TextWrapping::Wrap);
		contents.MinHeight(100);
		contents.MaxHeight(180);
		contents.MaxLength(32767);
		contents.Text(m_noteDraft);
		contents.TextChanged(
		    [weak = make_weak(dialog)](
		        const Windows::Foundation::IInspectable& sender, auto&&) {
			    if (const auto prompt = weak.get()) {
				    prompt.IsPrimaryButtonEnabled(
				        !sender.as<TextBox>().Text().empty());
			    }
		    });
		fields.Children().Append(contents);
		dialog.Content(fields);
		m_noteDialog = dialog;
		const auto choice = co_await dialog.ShowAsync();
		if (m_noteDialog == dialog) {
			m_noteDialog = nullptr;
		}
		if (m_closed) {
			co_return;
		}
		m_noteDraft = contents.Text();
		m_noteAuthor = author.Text();
		if (choice != ContentDialogResult::Primary) {
			co_return;
		}
		submitNote(page, std::u16string(m_noteDraft.begin(), m_noteDraft.end()),
		           std::u16string(m_noteAuthor.begin(), m_noteAuthor.end()),
		           target);
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open the note editor");
		if (m_noteDialog) {
			m_noteDialog.Hide();
			m_noteDialog = nullptr;
		}
		if (!m_closed) {
			showNotice(L"Couldn't open the note editor. Try again.");
		}
	}
}

void ViewerPage::submitNote(int page, std::u16string text,
                            std::u16string author,
                            std::optional<NoteTarget> target) {
	if (m_closed || !m_session || !m_noteAllowed || m_noteBusy ||
	    m_noteSaving || m_notePlacing ||
	    (m_notesAvailable && !m_notesAvailable())) {
		return;
	}
	m_noteBusy = true;
	try {
		const auto reply = [weak = get_weak(),
		                    queue = DispatcherQueue()](auto result) {
			queue.TryEnqueue([weak, result] {
				if (const auto self = weak.get(); self && !self->m_closed) {
					self->m_noteBusy = false;
					self->m_notesDirty = self->m_notesDirty || result.changed;
					try {
						if (result.changed) {
							self->refreshNotes();
						}
						if (result.index) {
							self->m_noteDraft = {};
						} else {
							self->showNotice(writeNotice(
							    result.refusal,
							    L"Couldn't add this note. This PDF may "
							    L"restrict "
							    L"comments, contain attachments, or be "
							    L"too "
							    L"small "
							    L"for a note. Your text is still in "
							    L"Add "
							    L"note."));
						}
						self->showComments();
					} catch (...) {
						diagnostics::caught(
						    diagnostics::Level::Error,
						    "Couldn't refresh the edited PDF view.");
					}
				}
			});
		};
		const bool queued =
		    target ? m_session->addTextNoteAt(page, target->x, target->y,
		                                      target->rotation, std::move(text),
		                                      std::move(author), reply)
		           : m_session->addCenteredTextNote(page, std::move(text),
		                                            std::move(author), reply);
		if (!queued) {
			m_noteBusy = false;
			showNotice(L"Couldn't queue this note. Try again.");
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue the note");
		m_noteBusy = false;
		showNotice(L"Couldn't queue this note. Try again.");
	}
	showComments();
}

void ViewerPage::refreshNotes() {
	clearAnnotationSelection();
	// i discard old replies even when their page dimensions still match.
	++m_renderVersion;
	m_session->cancelRender();
	m_cache.clear();
	m_failed.clear();
	m_retriedRenders.clear();
	m_retriedUploads.clear();
	m_loggedPlaceholders.clear();
	m_uploadFailed.clear();
	// keep the pages on screen until the new revision has pixels to show.
	m_thumbLoaded.clear();
	for (const auto& [page, button] : m_thumbs) {
		prepareThumbnail(button, page);
	}
	pauseComments();
	m_commentScan.reset(static_cast<int>(m_sizes.size()));
	CommentsList().Items().Clear();
	updateVisible();
}
} // namespace winrt::Sable::implementation
