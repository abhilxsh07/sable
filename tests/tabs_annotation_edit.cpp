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
#include "tabs_test.h"

namespace winrt::Sable::implementation {
namespace {
using namespace Microsoft::UI::Xaml::Automation;
void invokeElement(const FrameworkElement& element) {
	const auto peer =
	    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element);
	if (const auto toggle = peer.GetPattern(Peers::PatternInterface::Toggle)) {
		toggle.as<Provider::IToggleProvider>().Toggle();
	} else {
		peer.GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	}
}
Button primaryButton(const DependencyObject& node,
                     const wchar_t* wanted = L"PrimaryButton") {
	if (const auto button = node.try_as<Button>();
	    button && button.Name() == wanted) {
		return button;
	}
	for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node); ++i) {
		if (const auto button = primaryButton(
		        Media::VisualTreeHelper::GetChild(node, i), wanted)) {
			return button;
		}
	}
	return nullptr;
}
} // namespace

// InkEdit: draw, pick the drawing on the page, drag it, restyle it through
// Edit drawing, then Delete it.
void TabsTest::inkEditTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
	    v.m_annotationPending) {
		return;
	}
	const auto at = [&](double x, double y) {
		const auto& rect = v.m_layout->pages()[0];
		return Windows::Foundation::Point{
		    static_cast<float>(rect.x + x * rect.width),
		    static_cast<float>(rect.y + y * rect.height)};
	};
	const auto inkRows = [&] {
		const auto& rows = v.m_commentScan.rows();
		return std::count_if(rows.begin(), rows.end(), [](const auto& row) {
			return row.page == 0 && row.subtype == "Ink";
		});
	};
	const auto selectedInk = [&] {
		return v.m_selectedAnnotation &&
		       v.m_selectedAnnotation->target.subtype == "Ink";
	};
	switch (step) {
	case 0:
		v.setCommentsOpen(true);
		v.goToPage(0);
		invokeElement(v.DrawInkButton());
		check(v.beginInkStroke(at(.30, .30)), "the stroke did not start");
		v.extendInkStroke(at(.40, .34));
		v.extendInkStroke(at(.45, .30));
		v.finishInkStroke();
		break;
	case 1:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(inkRows() == 1 && v.m_notesDirty, "the drawing wasn't written");
		check(v.cancelNotePlacement() && !v.notePlacing(),
		      "drawing stayed armed");
		v.requestAnnotationAt(at(.40, .34));
		break;
	case 2:
		check(selectedInk() && v.movableAnnotation(),
		      "the drawing wasn't selected on the page");
		if (v.m_annotationMenu) {
			v.m_annotationMenu.Hide();
		}
		// a press outside it doesn't grab it; a short press moves nothing
		check(!v.beginAnnotationDrag(at(.8, .8)) && !v.m_annotationDrag,
		      "a press away from the drawing grabbed it");
		check(v.beginAnnotationDrag(at(.40, .34)) && v.m_annotationDrag,
		      "a press on the drawing didn't grab it");
		v.finishAnnotationDrag(at(.401f, .34));
		check(!v.m_annotationDrag && !v.m_noteBusy,
		      "a click without moving queued a move");
		check(v.beginAnnotationDrag(at(.40, .34)), "the drag didn't restart");
		v.extendAnnotationDrag(at(.50, .50));
		check(v.cancelNotePlacement() && !v.m_annotationDrag,
		      "Esc didn't drop the drag");
		check(v.beginAnnotationDrag(at(.40, .34)), "the drag didn't start");
		v.extendAnnotationDrag(at(.55, .50));
		v.finishAnnotationDrag(at(.60, .60));
		check(v.m_noteBusy && v.m_annotationDrag && v.m_annotationDrag->queued,
		      "the move wasn't queued");
		break;
	case 3:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(!v.m_annotationDrag && !v.m_selectedAnnotation && inkRows() == 1,
		      "the move lost the drawing or kept the drag");
		v.requestAnnotationAt(at(.60, .60));
		break;
	case 4:
		check(selectedInk(), "the drawing isn't where it was dropped");
		if (v.m_annotationMenu) {
			v.m_annotationMenu.Hide();
		}
		v.editAnnotation(v.m_selectedAnnotation->target);
		break;
	case 5: {
		if (!v.m_noteDialog) {
			check(ticks < 40, "Edit drawing didn't open");
			return;
		}
		check(unbox_value<hstring>(v.m_noteDialog.Title()) == L"Edit drawing",
		      "the wrong editor opened");
		const auto fields = v.m_noteDialog.Content().as<StackPanel>();
		const auto colors = fields.Children().GetAt(1).as<ComboBox>();
		const auto widths = fields.Children().GetAt(2).as<ComboBox>();
		check(colors.Items().Size() == 6 && widths.Items().Size() == 4 &&
		          colors.SelectedIndex() == 0 && widths.SelectedIndex() == 0,
		      "Edit drawing doesn't offer the pens");
		colors.SelectedIndex(3);
		widths.SelectedIndex(3);
		const auto button = primaryButton(v.m_noteDialog);
		check(button && button.IsEnabled(), "Apply is disabled");
		invokeElement(button);
		break;
	}
	case 6:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			check(ticks < 80, "Edit drawing didn't finish");
			return;
		}
		check(inkRows() == 1 && !v.m_selectedAnnotation,
		      "restyling lost the drawing");
		v.requestAnnotationAt(at(.60, .60));
		break;
	case 7:
		check(selectedInk() && v.canDeleteAnnotation(),
		      "the restyled drawing can't be selected");
		if (v.m_annotationMenu) {
			v.m_annotationMenu.Hide();
		}
		v.deleteAnnotation();
		break;
	case 8:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(inkRows() == 0, "Delete kept the edited drawing");
		v.close();
		status.Text(L"PASS: drawing select, drag, Esc, restyle and Delete");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected drawing edit step");
	}
	++step;
	ticks = 0;
}

// StampEdit: place a stamp, pick it on the page, drag it, resize it
// through Resize stamp, then Delete it.
void TabsTest::stampEditTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
	    v.m_annotationPending) {
		return;
	}
	const auto at = [&](double x, double y) {
		const auto& rect = v.m_layout->pages()[0];
		return Windows::Foundation::Point{
		    static_cast<float>(rect.x + x * rect.width),
		    static_cast<float>(rect.y + y * rect.height)};
	};
	const auto stampRows = [&] {
		const auto& rows = v.m_commentScan.rows();
		return std::count_if(rows.begin(), rows.end(), [](const auto& row) {
			return row.page == 0 && row.subtype == "Stamp";
		});
	};
	const auto selectedStamp = [&] {
		return v.m_selectedAnnotation &&
		       v.m_selectedAnnotation->target.subtype == "Stamp";
	};
	switch (step) {
	case 0:
		v.setCommentsOpen(true);
		v.goToPage(0);
		v.beginNotePlacement(ViewerPage::PlacementTool::Stamp);
		// red on the left, blue on the right
		v.m_stampImage =
		    pdf::StampImage{2, 1, {0, 0, 255, 255, 255, 0, 0, 255}};
		check(v.notePlacing(), "stamp placement didn't arm");
		v.placeNote(at(.2, .2));
		v.placeNote(at(.4, .3));
		break;
	case 1:
		if (!v.m_commentScan.done() || stampRows() != 1) {
			check(ticks < 60, "the stamp didn't reach Comments");
			return;
		}
		check(!v.notePlacing(), "placing the stamp kept the tool armed");
		v.requestAnnotationAt(at(.3, .25));
		break;
	case 2:
		check(selectedStamp() && v.movableAnnotation(),
		      "the stamp wasn't selected on the page");
		if (v.m_annotationMenu) {
			v.m_annotationMenu.Hide();
		}
		check(v.beginAnnotationDrag(at(.3, .25)), "the stamp wasn't grabbed");
		v.extendAnnotationDrag(at(.4, .4));
		v.finishAnnotationDrag(at(.5, .55));
		check(v.m_noteBusy && v.m_annotationDrag && v.m_annotationDrag->queued,
		      "the stamp move wasn't queued");
		break;
	case 3:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(stampRows() == 1 && !v.m_selectedAnnotation,
		      "the move lost the stamp");
		v.requestAnnotationAt(at(.5, .55));
		break;
	case 4:
		check(selectedStamp(), "the stamp isn't where it was dropped");
		if (v.m_annotationMenu) {
			v.m_annotationMenu.Hide();
		}
		stampEditWidth = v.m_selectedAnnotation->bounds->right -
		                 v.m_selectedAnnotation->bounds->left;
		v.editAnnotation(v.m_selectedAnnotation->target);
		break;
	case 5: {
		if (!v.m_noteDialog) {
			check(ticks < 40, "Resize stamp didn't open");
			return;
		}
		check(unbox_value<hstring>(v.m_noteDialog.Title()) == L"Resize stamp",
		      "the wrong editor opened");
		const auto fields = v.m_noteDialog.Content().as<StackPanel>();
		const auto width = fields.Children().GetAt(1).as<TextBox>();
		const auto height = fields.Children().GetAt(2).as<TextBlock>();
		width.Text(to_hstring(stampEditWidth / 2));
		check(std::wstring_view(height.Text()).starts_with(L"Height: "),
		      "Resize stamp doesn't show the height");
		const auto button = primaryButton(v.m_noteDialog);
		check(button && button.IsEnabled(), "Apply is disabled");
		invokeElement(button);
		break;
	}
	case 6:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			check(ticks < 80, "Resize stamp didn't finish");
			return;
		}
		check(stampRows() == 1, "resizing lost the stamp");
		// the top-left corner stays, so the stamp is still under it
		v.requestAnnotationAt(at(.45, .52));
		break;
	case 7:
		check(selectedStamp() &&
		          std::abs(v.m_selectedAnnotation->bounds->right -
		                   v.m_selectedAnnotation->bounds->left -
		                   stampEditWidth / 2) < .1,
		      "the stamp didn't take the new width");
		if (v.m_annotationMenu) {
			v.m_annotationMenu.Hide();
		}
		v.deleteAnnotation();
		break;
	case 8:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(stampRows() == 0, "Delete kept the edited stamp");
		v.close();
		status.Text(L"PASS: stamp select, drag, resize and Delete");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected stamp edit step");
	}
	++step;
	ticks = 0;
}

// LineArrow: two clicks for a line and an arrow, a too-short click that
// keeps the start, then the arrow picked on the page and deleted.
void TabsTest::lineArrowTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
	    v.m_annotationPending) {
		return;
	}
	const auto at = [&](double x, double y) {
		const auto& rect = v.m_layout->pages()[0];
		return Windows::Foundation::Point{
		    static_cast<float>(rect.x + x * rect.width),
		    static_cast<float>(rect.y + y * rect.height)};
	};
	const auto inkRows = [&] {
		const auto& rows = v.m_commentScan.rows();
		return std::count_if(rows.begin(), rows.end(), [](const auto& row) {
			return row.page == 0 && row.subtype == "Ink";
		});
	};
	switch (step) {
	case 0:
		v.setCommentsOpen(true);
		v.goToPage(0);
		invokeElement(v.PlaceLineButton());
		check(v.notePlacing() &&
		          v.m_activeTool == ViewerPage::PlacementTool::Line &&
		          v.PlaceLineButton().Text() == L"Cancel line placement",
		      "Place line didn't arm");
		check(v.cancelNotePlacement() && !v.notePlacing(),
		      "Esc kept the line tool");
		invokeElement(v.PlaceLineButton());
		v.placeNote(at(.2, .2));
		check(v.m_shapeStart.has_value(), "the line start wasn't kept");
		{
			const auto style = v.s_inkStyle;
			const auto corner = v.m_shapeStart->corner;
			v.chooseInkStyle(pdf::AnnotationColor{235, 73, 73, 255}, 4.f);
			check(v.m_activeTool == ViewerPage::ActiveTool::Line &&
			          v.m_shapeStart && v.m_shapeStart->corner.x == corner.x &&
			          v.m_shapeStart->corner.y == corner.y &&
			          v.s_inkStyle.width == 4 && v.s_inkStyle.color.red == 235,
			      "a style choice replaced the line tool or its first point");
			v.chooseInkStyle(style.color, style.width);
		}
		v.placeNote(at(.5, .3));
		break;
	case 1:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(inkRows() == 1 && v.m_notesDirty && !v.notePlacing(),
		      "the line wasn't written");
		check(std::any_of(v.m_commentScan.rows().begin(),
		                  v.m_commentScan.rows().end(),
		                  [](const auto& row) {
			                  return row.line == pdf::LineKind::Line;
		                  }),
		      "the line isn't labelled Line in Comments");
		invokeElement(v.PlaceArrowButton());
		v.placeNote(at(.3, .6));
		// a click right next to the start is too short and keeps it
		v.placeNote(at(.3005, .6));
		check(v.notePlacing() && v.m_shapeStart && !v.m_noteBusy,
		      "a too-short arrow didn't keep its start");
		v.chooseInkStyle(std::nullopt, v.s_inkStyle.width);
		check(v.m_activeTool == ViewerPage::ActiveTool::Arrow && v.m_shapeStart,
		      "a style choice replaced the arrow tool or its first point");
		v.placeNote(at(.7, .6));
		break;
	case 2:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(inkRows() == 2 && !v.notePlacing(), "the arrow wasn't written");
		// the arrow's box takes in its head, around its tip
		v.requestAnnotationAt(at(.69, .6));
		break;
	case 3: {
		check(v.m_selectedAnnotation &&
		          v.m_selectedAnnotation->target.subtype == "Ink" &&
		          v.m_selectedAnnotation->line == pdf::LineKind::Arrow &&
		          v.canDeleteAnnotation(),
		      "the arrow couldn't be selected");
		bool offered = false;
		if (v.m_annotationMenu) {
			for (const auto& item : v.m_annotationMenu.Items()) {
				if (const auto entry = item.try_as<MenuFlyoutItem>()) {
					offered = offered || entry.Text() == L"Resize arrow";
				}
			}
			v.m_annotationMenu.Hide();
		}
		check(offered, "the page menu doesn't offer Resize arrow");
		const auto& rows = v.m_commentScan.rows();
		const auto arrowRow =
		    std::find_if(rows.begin(), rows.end(), [](const auto& row) {
			    return row.line == pdf::LineKind::Arrow;
		    });
		check(arrowRow != rows.end(), "the arrow has no Comments row");
		v.clearAnnotationSelection();
		v.CommentsList().SelectedIndex(
		    static_cast<int>(arrowRow - rows.begin()));
		v.showComments();
		invokeElement(v.EditNoteButton());
		break;
	}
	case 4: {
		if (!v.m_noteDialog) {
			check(ticks < 40, "the list's arrow editor didn't open");
			return;
		}
		check(unbox_value<hstring>(v.m_noteDialog.Title()) == L"Edit arrow" &&
		          v.m_noteDialog.SecondaryButtonText() == L"Resize arrow\u2026",
		      "list Edit did not offer arrow styling and Resize");
		const auto fields = v.m_noteDialog.Content().as<StackPanel>();
		check(unbox_value<hstring>(
		          fields.Children().GetAt(1).as<ComboBox>().Header()) ==
		              L"Color" &&
		          unbox_value<hstring>(
		              fields.Children().GetAt(2).as<ComboBox>().Header()) ==
		              L"Width",
		      "the arrow editor lost Color or Width");
		const auto resize = primaryButton(v.m_noteDialog, L"SecondaryButton");
		if (!resize || !resize.IsEnabled()) {
			check(ticks < 40, "Resize arrow action is unavailable");
			return;
		}
		invokeElement(resize);
		break;
	}
	case 5: {
		if (!v.m_noteDialog ||
		    unbox_value<hstring>(v.m_noteDialog.Title()) != L"Resize arrow") {
			check(ticks < 40, "Resize arrow didn't open");
			return;
		}
		check(unbox_value<hstring>(v.m_noteDialog.Title()) == L"Resize arrow",
		      "the resize dialog has the wrong title");
		v.m_noteDialog.Content()
		    .as<StackPanel>()
		    .Children()
		    .GetAt(1)
		    .as<TextBox>()
		    .Text(L"60");
		const auto button = primaryButton(v.m_noteDialog);
		if (!button || !button.IsEnabled()) {
			check(ticks < 20, "Apply is disabled");
			return;
		}
		invokeElement(button);
		break;
	}
	case 6:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			check(ticks < 80, "Resize arrow didn't finish");
			return;
		}
		check(inkRows() == 2 && std::any_of(v.m_commentScan.rows().begin(),
		                                    v.m_commentScan.rows().end(),
		                                    [](const auto& row) {
			                                    return row.line ==
			                                           pdf::LineKind::Arrow;
		                                    }),
		      "the resized arrow isn't labelled Arrow");
		// its start stayed put, so it's still under the first click
		v.requestAnnotationAt(at(.31, .6));
		break;
	case 7:
		check(v.m_selectedAnnotation &&
		          v.m_selectedAnnotation->line == pdf::LineKind::Arrow &&
		          std::abs(v.m_selectedAnnotation->lineLength - 60) < .01,
		      "the arrow isn't 60 pt long after Resize arrow");
		if (v.m_annotationMenu) {
			v.m_annotationMenu.Hide();
		}
		v.deleteAnnotation();
		break;
	case 8: {
		if (!v.m_commentScan.done()) {
			return;
		}
		check(inkRows() == 1, "Delete didn't remove the arrow");
		const auto& rows = v.m_commentScan.rows();
		const auto lineRow =
		    std::find_if(rows.begin(), rows.end(), [](const auto& row) {
			    return row.line == pdf::LineKind::Line;
		    });
		check(lineRow != rows.end(), "the line has no Comments row");
		v.clearAnnotationSelection();
		v.CommentsList().SelectedIndex(
		    static_cast<int>(lineRow - rows.begin()));
		v.showComments();
		invokeElement(v.EditNoteButton());
		break;
	}
	case 9:
		if (!v.m_noteDialog) {
			check(ticks < 40, "the line editor didn't open");
			return;
		}
		check(unbox_value<hstring>(v.m_noteDialog.Title()) == L"Edit line" &&
		          v.m_noteDialog.SecondaryButtonText() == L"Resize line\u2026",
		      "list Edit did not offer line styling and Resize: " +
		          to_string(unbox_value<hstring>(v.m_noteDialog.Title())) +
		          " / " + to_string(v.m_noteDialog.SecondaryButtonText()));
		v.m_noteDialog.Hide();
		break;
	case 10:
		if (v.m_noteDialog) {
			check(ticks < 40, "line Cancel didn't close");
			return;
		}
		check(inkRows() == 1 &&
		          std::any_of(v.m_commentScan.rows().begin(),
		                      v.m_commentScan.rows().end(),
		                      [&v](const auto& row) {
			                      return row.line == pdf::LineKind::Line &&
			                             row.target.revision ==
			                                 v.m_session->annotationRevision();
		                      }),
		      "canceling line Edit changed the PDF");
		v.close();
		status.Text(L"PASS: line and arrow placement, labels, Resize arrow, "
		            L"short click and Delete");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected line step");
	}
	++step;
	ticks = 0;
}

// TextBoxUnicode: the text-box dialog takes Greek, CJK and an accent typed
// as a separate mark, turns away a right-to-left script, and the box
// reaches Comments with its text.
void TabsTest::textBoxUnicodeTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending) {
		return;
	}
	const auto at = [&](double x, double y) {
		const auto& rect = v.m_layout->pages()[0];
		return Windows::Foundation::Point{
		    static_cast<float>(rect.x + x * rect.width),
		    static_cast<float>(rect.y + y * rect.height)};
	};
	// the fields sit in a scroller: location, author, text, then the error
	const auto fields = [&] {
		return v.m_noteDialog.Content()
		    .as<ScrollViewer>()
		    .Content()
		    .as<StackPanel>()
		    .Children();
	};
	const auto contents = [&] { return fields().GetAt(2).as<TextBox>(); };
	const auto error = [&] {
		const auto children = fields();
		return children.GetAt(children.Size() - 1).as<TextBlock>();
	};
	switch (step) {
	case 0:
		v.setCommentsOpen(true);
		v.goToPage(0);
		invokeElement(v.PlaceTextBoxButton());
		check(v.placeNote(at(.15, .15)), "the text box click wasn't taken");
		break;
	case 1:
		if (!v.m_noteDialog) {
			check(ticks < 40, "the text box dialog closed");
			return;
		}
		contents().Text(L"\u05e9\u05dc\u05d5\u05dd");
		break;
	case 2:
	case 4:
	case 7: {
		// Add follows the text a tick later
		const auto button = primaryButton(v.m_noteDialog);
		if (!button || !button.IsEnabled()) {
			check(ticks < 20, "Add is disabled");
			return;
		}
		invokeElement(button);
		break;
	}
	case 3:
		check(v.m_noteDialog && !v.m_noteBusy &&
		          std::wstring_view(error().Text())
		              .starts_with(L"Sable can't draw"),
		      "a right-to-left script wasn't turned away");
		// "e" and a combining acute compose to one drawable character
		contents().Text(L"Cafe\u0301 \u03b1\u03b2\u03b3\n\u65e5\u672c\u8a9e");
		break;
	case 5:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			check(ticks < 80, "the Unicode text box wasn't written");
			return;
		}
		check(v.m_notesDirty &&
		          std::any_of(v.m_commentScan.rows().begin(),
		                      v.m_commentScan.rows().end(),
		                      [](const auto& row) {
			                      return row.subtype == "FreeText" &&
			                             row.text ==
			                                 reinterpret_cast<const char*>(
			                                     u8"Café αβγ\n日本語");
		                      }),
		      "the Unicode text box didn't reach Comments");
		invokeElement(v.PlaceTextBoxButton());
		check(v.placeNote(at(.15, .55)),
		      "the second text box click wasn't taken");
		break;
	case 6:
		if (!v.m_noteDialog) {
			check(ticks < 40, "the second text box dialog closed");
			return;
		}
		// Hindi and Bengali are shaped, conjuncts and all
		contents().Text(L"\u0915\u094d\u0937\u0924\u094d\u0930\u093f\u092f\n"
		                L"\u09b8\u09cd\u09a4\u09cd\u09b0\u09c0");
		break;
	case 8:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			check(ticks < 80, "the Hindi and Bengali box wasn't written");
			return;
		}
		check(std::any_of(
		          v.m_commentScan.rows().begin(), v.m_commentScan.rows().end(),
		          [](const auto& row) {
			          return row.subtype == "FreeText" &&
			                 row.text ==
			                     reinterpret_cast<const char*>(
			                         u8"\u0915\u094d\u0937\u0924\u094d"
			                         u8"\u0930\u093f\u092f\n\u09b8\u09cd"
			                         u8"\u09a4\u09cd\u09b0\u09c0");
		          }),
		      "the Hindi and Bengali box didn't reach Comments");
		v.close();
		status.Text(L"PASS: Unicode text box, NFC, script refusal, Hindi and "
		            L"Bengali");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected Unicode text box step");
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
