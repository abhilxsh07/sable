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
	Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)
	    .GetPattern(Peers::PatternInterface::Invoke)
	    .as<Provider::IInvokeProvider>()
	    .Invoke();
}
Button primaryButton(const DependencyObject& node) {
	if (const auto button = node.try_as<Button>();
	    button && button.Name() == L"PrimaryButton") {
		return button;
	}
	for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node); ++i) {
		if (const auto button =
		        primaryButton(Media::VisualTreeHelper::GetChild(node, i))) {
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
		check(v.cancelNotePlacement() && !v.m_notePlacing,
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
} // namespace winrt::Sable::implementation
