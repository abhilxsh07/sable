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
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

namespace winrt::Sable::implementation {
namespace {
void invokeCommentTool(const FrameworkElement& element) {
	using namespace Microsoft::UI::Xaml::Automation;
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
} // namespace
void TabsTest::commentLocationsTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || ticks < 3 || v.m_commentsPending) {
		return;
	}
	if (step == 0) {
		owner.AppWindow().Resize({1000, 700});
		v.zoomTo(4, {0, 0});
		v.setCommentsOpen(true);
		++step;
		ticks = 0;
		return;
	}
	if (!v.m_commentScan.done()) {
		return;
	}
	check(v.CommentsList().Items().Size() == 13, "location row count");
	constexpr int rows[] = {0, 1, 10, 11, 12, 3};
	constexpr int pages[] = {0, 0, 1, 2, 3, 0};
	constexpr double points[][2] = {
	    {.2, .75}, {.8, .25}, {.25, .2}, {.8, .25}, {.75, .8}};
	const int round = (step - 1) / 2;
	if (round == 6) {
		if (!commentFactorStarted) {
			commentFactorStarted = true;
			v.Scroller().ZoomMode(ZoomMode::Enabled);
			commentFactorChanged = v.Scroller().ViewChanged(
			    auto_revoke, [this, weak = v.get_weak()](auto&&, auto&&) {
				    const auto self = weak.get();
				    if (!self || commentFactorSeen ||
				        self->Scroller().ZoomFactor() < 1.1f) {
					    return;
				    }
				    commentFactorSeen = true;
				    const auto scroll = self->Scroller();
				    const auto& rect = self->m_layout->pages()[0];
				    const double factor = scroll.ZoomFactor();
				    const double y = std::clamp(
				        (rect.y + .75 * rect.height) * factor -
				            scroll.ViewportHeight() / 2,
				        0.,
				        (std::max)(0., self->Pages().Height() * factor -
				                           scroll.ViewportHeight()));
				    self->followComment(0);
				    const double actual = self->m_landingAt
				                              ? self->m_landingAt->second
				                              : scroll.VerticalOffset();
				    commentFactorCorrect = std::abs(actual - y) < 1.5;
			    });
			v.Scroller().ChangeView(nullptr, nullptr, 1.5f, true);
			ticks = 0;
			return;
		}
		if (!commentFactorSeen) {
			check(ticks < 30, "comment native scale did not apply");
			return;
		}
		commentFactorChanged.revoke();
		check(commentFactorCorrect, "comment ignored native zoom factor");
		const auto scroll = v.Scroller();
		const auto& rect = v.m_layout->pages()[0];
		const double factor = scroll.ZoomFactor();
		const double centered = std::clamp(
		    (rect.y + .75 * rect.height) * factor - scroll.ViewportHeight() / 2,
		    0.,
		    (std::max)(0.,
		               v.Pages().Height() * factor - scroll.ViewportHeight()));
		check(std::abs(scroll.VerticalOffset() - centered) < 1.5,
		      "native zoom completion lost the comment center");
		const auto page = v.m_current;
		const auto offset = v.Scroller().VerticalOffset();
		v.followComment(-1);
		v.followComment(2000);
		check(v.m_current == page && v.Scroller().VerticalOffset() == offset,
		      "invalid comment row moved the view");
		status.Text(L"PASS: comment centers, row identity, crop, "
		            L"rotation, unchanged zoom and missing bounds");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	if (step % 2) {
		using namespace Microsoft::UI::Xaml::Automation;
		const auto item =
		    v.CommentsList().Items().GetAt(rows[round]).as<ListViewItem>();
		check(unbox_value<int>(item.Tag()) == rows[round],
		      "comment tag lost its row identity");
		v.CommentsList().ScrollIntoView(item);
		v.CommentsList().UpdateLayout();
		const auto peer =
		    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item);
		const auto action =
		    peer ? peer.GetPattern(Peers::PatternInterface::Invoke)
		               .try_as<Provider::IInvokeProvider>()
		         : nullptr;
		if (!action) {
			check(ticks < 30, "comment location has no Invoke");
			return;
		}
		action.Invoke();
	} else {
		check(v.m_current == static_cast<std::size_t>(pages[round]),
		      "comment location reached another page");
		check(std::abs(v.m_zoom - 4) < 1e-6, "comment changed zoom");
		if (round < 5) {
			const auto& rect = v.m_layout->pages()[pages[round]];
			const auto scroll = v.Scroller();
			const double x = std::clamp(
			    v.imageLeft() + rect.x + points[round][0] * rect.width -
			        scroll.ViewportWidth() / 2,
			    0., (std::max)(0., v.Pages().Width() - scroll.ViewportWidth()));
			const double y = std::clamp(
			    rect.y + points[round][1] * rect.height -
			        scroll.ViewportHeight() / 2,
			    0.,
			    (std::max)(0., v.Pages().Height() - scroll.ViewportHeight()));
			check(std::abs(scroll.HorizontalOffset() - x) < 1.5 &&
			          std::abs(scroll.VerticalOffset() - y) < 1.5,
			      "comment position was not centered: actual " +
			          std::to_string(scroll.HorizontalOffset()) + "," +
			          std::to_string(scroll.VerticalOffset()) + " expected " +
			          std::to_string(x) + "," + std::to_string(y) + " factor " +
			          std::to_string(scroll.ZoomFactor()) + " size " +
			          std::to_string(rect.width) + "," +
			          std::to_string(rect.height));
		}
	}
	++step;
	ticks = 0;
}

void TabsTest::annotationActionsTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
	    v.m_annotationPending) {
		return;
	}
	const auto invoke = [&](const FrameworkElement& item) {
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto dialogButton = [&](const wchar_t* name) {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& node) -> Button {
			if (const auto button = node.try_as<Button>();
			    button && button.Name() == name) {
				return button;
			}
			for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node);
			     ++i) {
				if (const auto button =
				        find(Media::VisualTreeHelper::GetChild(node, i))) {
					return button;
				}
			}
			return nullptr;
		};
		const auto button = find(v.m_noteDialog);
		check(button && button.IsEnabled(), "annotation dialog action");
		invoke(button);
	};
	const auto annotations = [&] {
		const auto reply =
		    std::make_shared<std::promise<pdf::AnnotationsResult>>();
		auto future = reply->get_future();
		check(
		    v.m_session->annotations(
		        0, [reply](auto value) { reply->set_value(std::move(value)); }),
		    "annotation snapshot not queued");
		check(future.wait_for(std::chrono::seconds(10)) ==
		          std::future_status::ready,
		      "annotation snapshot timed out");
		auto result = future.get();
		check(!result.canceled && result.annotations.has_value(),
		      "annotation snapshot missing");
		return std::move(*result.annotations);
	};
	switch (step) {
	case 0: {
		check(v.m_textHighlight.Items().Size() == 5,
		      "highlight palette is incomplete");
		const wchar_t* labels[]{L"Yellow", L"Green", L"Light blue", L"Pink",
		                        L"Red"};
		const Windows::UI::Color colors[]{{255, 255, 240, 102},
		                                  {255, 125, 240, 102},
		                                  {255, 143, 222, 249},
		                                  {255, 247, 153, 209},
		                                  {255, 235, 73, 73}};
		for (unsigned i = 0; i < 5; ++i) {
			const auto item =
			    v.m_textHighlight.Items().GetAt(i).as<MenuFlyoutItem>();
			// the swatch is a drawn 32 px dot; its centre pixel is the colour.
			const auto icon = item.Icon().try_as<ImageIcon>();
			const auto dot =
			    icon ? icon.Source().try_as<Media::Imaging::WriteableBitmap>()
			         : nullptr;
			bool same = false;
			if (dot && dot.PixelWidth() == 32 && dot.PixelHeight() == 32) {
				const auto* centre =
				    dot.PixelBuffer().data() + (16 * 32 + 16) * 4;
				same = centre[0] == colors[i].B && centre[1] == colors[i].G &&
				       centre[2] == colors[i].R && centre[3] == 255;
			}
			check(item.Text() == labels[i] && same,
			      "highlight color label or swatch differs");
		}
		v.m_textMenu.Opened([this](auto&&, auto&&) { ++annotationMenus; });
		v.requestText(0);
		break;
	}
	case 1: {
		if (!v.m_text.contains(0)) {
			return;
		}
		const auto point = [&](int index) {
			const auto box =
			    v.viewBox(v.m_text.at(0).text->characters[index].box);
			const auto& page = v.m_layout->pages()[0];
			return Windows::Foundation::Point{
			    static_cast<float>(page.x +
			                       (box.left + box.right) / 2 * page.width),
			    static_cast<float>(page.y +
			                       (box.top + box.bottom) / 2 * page.height)};
		};
		v.beginSelection(point(0));
		v.extendSelection(point(4));
		v.endSelection();
		break;
	}
	case 2:
		check(annotationMenus == 1 && v.m_selection &&
		          !v.m_selectionMenuPending,
		      "completed selection did not open its menu once");
		v.m_textMenu.Hide();
		v.Scroller().Focus(FocusState::Programmatic);
		break;
	case 3:
		check(owner.routeKey('A', helpers::SHORTCUT_CTRL, false),
		      "Ctrl+A did not reach the viewer");
		break;
	case 4:
		check(annotationMenus == 2 && v.m_selection &&
		          v.m_selection->anchor.page == 0 &&
		          v.m_selection->focus.page ==
		              static_cast<int>(v.m_sizes.size() - 1),
		      "Select All menu or document range missing");
		v.m_selection = pdf::TextSelection{{0, 0}, {0, 4}};
		++v.m_selectionVersion;
		invoke(v.m_textHighlight.Items().GetAt(3).as<MenuFlyoutItem>());
		v.m_textMenu.Hide();
		v.setCommentsOpen(true);
		break;
	case 5: {
		if (!v.m_commentScan.done()) {
			return;
		}
		const auto snapshot = annotations();
		check(snapshot.entries.size() == 1 &&
		          snapshot.entries[0].subtype == "Highlight",
		      "palette item did not create a highlight");
		annotationSnapshot = snapshot.entries[0];
		check(!annotationSnapshot->displayQuads.empty() &&
		          annotationSnapshot->displayBounds.has_value(),
		      "markup lacks display hit and outline geometry");
		const auto& q = annotationSnapshot->displayQuads[0];
		const auto center =
		    helpers::rotatePoint({(q.x1 + q.x2 + q.x3 + q.x4) / 4.,
		                          (q.y1 + q.y2 + q.y3 + q.y4) / 4.},
		                         v.m_rotation);
		const auto& page = v.m_layout->pages()[0];
		v.requestAnnotationAt(
		    {static_cast<float>(page.x + center.x * page.width),
		     static_cast<float>(page.y + center.y * page.height)});
		break;
	}
	case 6:
		check(v.m_selectedAnnotation &&
		          v.m_selectedAnnotation->target ==
		              annotationSnapshot->target &&
		          v.m_annotationMenu.Items().Size() == 2 &&
		          v.m_device.annotationDashes && v.m_device.annotationOutline,
		      "markup click did not select its menu and dashed outline");
		v.m_annotationMenu.Hide();
		v.PageBox().Focus(FocusState::Programmatic);
		check(!owner.routeKey(VK_DELETE, 0, false) && !v.m_noteBusy &&
		          v.m_selectedAnnotation,
		      "Delete in a text field reached the annotation");
		v.Scroller().Focus(FocusState::Programmatic);
		check(owner.routeKey(VK_DELETE, 0, true) && !v.m_noteBusy,
		      "repeated Delete removed an annotation");
		annotationRow = v.CommentsList().Items().GetAt(0).as<ListViewItem>();
		check(owner.routeKey(VK_DELETE, 0, false) && v.m_noteBusy,
		      "Delete did not queue the selected markup");
		break;
	case 7: {
		if (!v.m_commentScan.done()) {
			return;
		}
		check(annotations().entries.empty() && v.m_notesDirty &&
		          !v.m_selectedAnnotation,
		      "markup deletion kept an annotation or stale selection");
		const auto stale = annotationRow.ContextFlyout().as<MenuFlyout>();
		stale.ShowAt(v.CommentsList());
		check(!stale.Items().GetAt(1).as<MenuFlyoutItem>().IsEnabled(),
		      "stale row context menu can redirect a deletion");
		stale.Hide();
		v.submitNote(0, u"native note", {});
		break;
	}
	case 8:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_commentScan.rows().size() == 1 &&
		          v.m_commentScan.rows()[0].subtype == "Text",
		      "new hidden note missing from Comments");
		v.CommentsList().SelectedIndex(0);
		invoke(v.EditNoteButton());
		break;
	case 9:
		if (!v.m_noteDialog) {
			return;
		}
		v.m_noteDialog.Content()
		    .as<StackPanel>()
		    .Children()
		    .GetAt(1)
		    .as<TextBox>()
		    .Text(L"");
		if (v.m_noteDialog.PrimaryButtonText() != L"Delete" ||
		    !v.m_noteDialog.IsPrimaryButtonEnabled()) {
			check(ticks < 20, "blank existing note did not offer Delete");
			return;
		}
		check(v.m_noteDialog.PrimaryButtonText() == L"Delete" &&
		          v.m_noteDialog.IsPrimaryButtonEnabled(),
		      "blank existing note did not offer Delete");
		dialogButton(L"PrimaryButton");
		break;
	case 10:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			return;
		}
		check(annotations().entries.empty(), "blank note was not deleted");
		v.m_selection = pdf::TextSelection{{0, 0}, {0, 4}};
		++v.m_selectionVersion;
		v.markSelection(pdf::TextMarkup::Underline);
		break;
	case 11: {
		if (!v.m_commentScan.done()) {
			return;
		}
		const auto snapshot = annotations();
		check(snapshot.entries.size() == 1 &&
		          snapshot.entries[0].subtype == "Underline",
		      "comment-removal markup setup failed");
		annotationSnapshot = snapshot.entries[0];
		const auto target = annotationSnapshot->target;
		const auto reply = std::make_shared<std::promise<pdf::NoteResult>>();
		auto future = reply->get_future();
		check(v.m_session->updateMarkupComment(
		          0, target.index, pdf::TextMarkup::Underline, {u"", false},
		          u"native comment", [reply](auto r) { reply->set_value(r); },
		          target.revision),
		      "comment setup not queued");
		check(future.wait_for(std::chrono::seconds(10)) ==
		              std::future_status::ready &&
		          future.get().changed,
		      "comment setup failed");
		v.refreshNotes();
		v.showComments();
		break;
	}
	case 12:
		if (!v.m_commentScan.done()) {
			return;
		}
		v.CommentsList().SelectedIndex(0);
		invoke(v.EditNoteButton());
		break;
	case 13:
		if (!v.m_noteDialog) {
			return;
		}
		check(v.m_noteDialog.SecondaryButtonText() == L"Remove comment" &&
		          v.m_noteDialog.IsSecondaryButtonEnabled(),
		      "markup editor lacks Remove comment");
		dialogButton(L"SecondaryButton");
		break;
	case 14: {
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			return;
		}
		const auto snapshot = annotations();
		check(snapshot.entries.size() == 1 &&
		          snapshot.entries[0].contents.empty() &&
		          !snapshot.entries[0].quads.empty() &&
		          snapshot.entries[0].quads.size() ==
		              annotationSnapshot->quads.size() &&
		          snapshot.entries[0].quads[0].x1 ==
		              annotationSnapshot->quads[0].x1 &&
		          snapshot.entries[0].quads[0].y4 ==
		              annotationSnapshot->quads[0].y4,
		      "removing comment deleted or moved its markup");
		const auto row = v.CommentsList().Items().GetAt(0).as<ListViewItem>();
		const auto menu = row.ContextFlyout().as<MenuFlyout>();
		menu.ShowAt(v.CommentsList());
		check(v.annotationTarget() == snapshot.entries[0].target &&
		          menu.Items().GetAt(1).as<MenuFlyoutItem>().IsEnabled(),
		      "row context menu did not capture its exact target");
		invoke(menu.Items().GetAt(1).as<MenuFlyoutItem>());
		menu.Hide();
		break;
	}
	case 15:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(annotations().entries.empty() &&
		          !v.DeleteNoteButton().IsEnabled(),
		      "row context deletion kept its target or enabled Delete");
		status.Text(L"PASS: selection menus, palette, markup hit/outline, "
		            L"Delete focus/repeat guards, stale row, blank-note "
		            L"deletion and comment removal preserving markup");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::highlightTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || (v.m_noteBusy && step != 2) || v.m_commentsPending ||
	    (notesSave &&
	     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
		return;
	}
	const auto invoke = [&](const FrameworkElement& item) {
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto item =
	    selectionMarkup == pdf::TextMarkup::Underline ? v.m_textUnderline
	    : selectionMarkup == pdf::TextMarkup::StrikeOut
	        ? v.m_textStrikeOut
	        : v.m_textHighlight.Items().GetAt(0).as<MenuFlyoutItem>();
	switch (step) {
	case 0:
		check(!v.canMarkSelection(), "empty selection could highlight");
		v.m_selection = pdf::TextSelection{{1, 4}, {0, 0}};
		++v.m_selectionVersion;
		v.m_copyAllowed = false;
		v.m_textMenu.ShowAt(v.Scroller());
		break;
	case 1:
		check(v.m_textHighlight.IsEnabled() && v.m_textUnderline.IsEnabled() &&
		          v.m_textStrikeOut.IsEnabled() && !v.m_textCopy.IsEnabled(),
		      "highlight depends on copy permission");
		notesGate = std::make_shared<std::promise<void>>();
		engine.post([wait = notesGate->get_future().share()] { wait.wait(); });
		invoke(item);
		v.m_textMenu.Hide();
		check(v.m_noteBusy && !v.canMarkSelection(),
		      "highlight did not hold writes busy");
		v.m_selection = pdf::TextSelection{{4, 1}, {4, 2}};
		++v.m_selectionVersion;
		v.Visibility(Visibility::Collapsed);
		break;
	case 2: {
		owner.closeTab(owner.m_tabs.front().tab);
		check(owner.m_tabs.size() == 1, "close discarded pending highlight");
		HWND handle{};
		check_hresult(
		    owner.try_as<::IWindowNative>()->get_WindowHandle(&handle));
		SendMessageW(handle, WM_CLOSE, 0, 0);
		check(!owner.m_closing, "window closed during highlight");
		const auto blocked = owner.saveNotes(current);
		check(blocked.Status() == Windows::Foundation::AsyncStatus::Completed &&
		          !blocked.GetResults(),
		      "save ran before highlight completed");
		notesGate->set_value();
		notesGate.reset();
		break;
	}
	case 3:
		check(v.m_notesDirty &&
		          v.m_selection == pdf::TextSelection{{4, 1}, {4, 2}},
		      "completed hidden edit lost dirty state or newer selection");
		v.Visibility(Visibility::Visible);
		v.setCommentsOpen(true);
		break;
	case 4:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_commentScan.rows().size() == 2 &&
		          v.m_commentScan.rows()[0].subtype == markupSubtype &&
		          v.m_commentScan.rows()[1].page == 1,
		      "highlight comments not refreshed");
		notesPath = std::filesystem::temp_directory_path() /
		            (L"sable_highlight_" +
		             std::to_wstring(GetCurrentProcessId()) + L".pdf");
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		break;
	case 5:
		check(notesSave.GetResults() && !v.m_notesDirty,
		      "highlight save failed");
		check(engine
		          .run([path = notesPath, subtype = markupSubtype] {
			          const auto saved = pdf::openFile(path);
			          if (!saved.document) {
				          return false;
			          }
			          for (int page = 0; page < 2; ++page) {
				          const auto notes = saved.document->annotations(page);
				          if (!notes || notes->entries.size() != 1 ||
				              notes->entries[0].subtype != subtype) {
					          return false;
				          }
			          }
			          const auto other = saved.document->annotations(4);
			          return other && other->entries.empty();
		          })
		          .get(),
		      "saved highlight geometry/pages missing");
		v.m_textMenu.ShowAt(v.Scroller());
		break;
	case 6:
		invoke(item);
		v.m_textMenu.Hide();
		break;
	case 7:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && !v.m_selection &&
		          v.m_commentScan.rows().size() == 3,
		      "successful highlight kept old selection or lost dirty state");
		v.CommentsList().SelectedIndex(2);
		check(v.EditNoteButton().IsEnabled(),
		      "Text markup comment editor unavailable");
		v.followComment(2);
		notesSave = owner.saveNotes(current);
		break;
	case 8:
		check(notesSave.GetResults() && !v.m_notesDirty && v.m_current == 4,
		      "second save or highlight navigation failed");
		v.m_selection = pdf::TextSelection{{-1, 0}, {-1, 4}};
		++v.m_selectionVersion;
		v.markSelection(selectionMarkup);
		break;
	case 9:
		check(!v.m_notesDirty &&
		          v.m_selection == pdf::TextSelection{{-1, 0}, {-1, 4}} &&
		          v.Message().IsOpen(),
		      "refused highlight lost selection or savepoint");
		v.clearTextSelection();
		v.m_textMenu.ShowAt(v.Scroller());
		check(!v.m_textHighlight.IsEnabled() &&
		          !v.m_textUnderline.IsEnabled() &&
		          !v.m_textStrikeOut.IsEnabled(),
		      "empty context highlight still enabled");
		v.m_textMenu.Hide();
		std::filesystem::remove(notesPath);
		status.Text(L"PASS: text markup menu, independent copy permission, "
		            L"pending close/save, hidden completion, newer selection, "
		            L"comments, saved copy and retained refused selection");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::noteEditTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || (v.m_noteBusy && step != 4 && step != 5) ||
	    v.m_commentsPending ||
	    (notesSave &&
	     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
		return;
	}
	const auto invoke = [&](const FrameworkElement& element) {
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto button = [&](const wchar_t* name) {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& element) -> Button {
			if (const auto item = element.try_as<Button>();
			    item && item.Name() == name) {
				return item;
			}
			for (int i = 0;
			     i < Media::VisualTreeHelper::GetChildrenCount(element); ++i) {
				if (const auto item =
				        find(Media::VisualTreeHelper::GetChild(element, i))) {
					return item;
				}
			}
			return nullptr;
		};
		const auto item = find(v.m_noteDialog);
		check(item && item.IsEnabled(), "note edit button unavailable");
		invoke(item);
	};
	const auto change = [&](std::u16string oldText, std::u16string newText) {
		const auto reply = std::make_shared<std::promise<pdf::NoteResult>>();
		auto future = reply->get_future();
		const auto callback = [reply](auto result) {
			reply->set_value(result);
		};
		bool present = true;
		if (markupCommentOnly && oldText == u"Location 1") {
			oldText.clear();
			present = false;
		}
		check(
		    markupCommentOnly
		        ? v.m_session->updateMarkupComment(0, 0, selectionMarkup,
		                                           {oldText, present}, newText,
		                                           callback)
		        : v.m_session->updateTextNote(0, 0, oldText, newText, callback),
		    "external edit not queued");
		check(future.wait_for(std::chrono::seconds(10)) ==
		          std::future_status::ready,
		      "external edit reply missing");
		check(future.get().index == 0, "external edit refused");
	};
	const std::u16string raw = u"Raw\r\nline\tend";
	switch (step) {
	case 0:
		v.setCommentsOpen(true);
		if (!v.m_commentScan.done()) {
			return;
		}
		check(!v.EditNoteButton().IsEnabled(), "unselected note editable");
		if (markupCommentOnly && !commentFactorStarted) {
			commentFactorStarted = true;
			const auto reply =
			    std::make_shared<std::promise<pdf::MarkupResult>>();
			auto future = reply->get_future();
			check(
			    v.m_session->markText({{0, 0}, {0, 4}}, selectionMarkup,
			                          [reply](auto r) { reply->set_value(r); }),
			    "markup setup not queued");
			check(future.wait_for(std::chrono::seconds(10)) ==
			              std::future_status::ready &&
			          future.get().changed,
			      "markup setup failed");
			v.refreshNotes();
		}
		if (!markupCommentOnly || !commentFactorSeen) {
			change(u"Location 1", raw);
			commentFactorSeen = true;
			if (markupCommentOnly) {
				v.refreshNotes();
				v.setCommentsOpen(true);
				return;
			}
		}
		v.CommentsList().SelectedIndex(0);
		check(v.EditNoteButton().IsEnabled(), "selected Text note disabled");
		invoke(v.EditNoteButton());
		break;
	case 1:
		if (!v.m_noteDialog) {
			return;
		}
		check(unbox_value<hstring>(v.m_noteDialog.Title()) ==
		          (markupCommentOnly ? L"Edit comment" : L"Edit note"),
		      "wrong editor");
		owner.closeTab(owner.m_tabs.front().tab);
		check(owner.m_tabs.size() == 1, "close dismissed existing-note editor");
		button(L"PrimaryButton");
		break;
	case 2: {
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty, "unchanged normalized text dirtied savepoint");
		const auto reply =
		    std::make_shared<std::promise<pdf::TextNoteResult>>();
		auto future = reply->get_future();
		const auto callback = [reply](auto result) {
			reply->set_value(result);
		};
		check(markupCommentOnly
		          ? v.m_session->markupComment(0, 0, selectionMarkup, callback)
		          : v.m_session->textNote(0, 0, callback),
		      "raw note read not queued");
		check(future.wait_for(std::chrono::seconds(10)) ==
		          std::future_status::ready,
		      "raw note read missing");
		check(future.get().contents == raw,
		      "unchanged editor normalized PDF contents");
		invoke(v.EditNoteButton());
		break;
	}
	case 3:
		if (!v.m_noteDialog) {
			return;
		}
		v.m_noteDialog.Content()
		    .as<StackPanel>()
		    .Children()
		    .GetAt(1)
		    .as<TextBox>()
		    .Text(L"Edited native \u03a9\U0001f600");
		notesGate = std::make_shared<std::promise<void>>();
		engine.post([wait = notesGate->get_future().share()] { wait.wait(); });
		button(L"PrimaryButton");
		check(v.m_noteBusy, "Apply did not hold edit busy");
		break;
	case 4:
		// the queued write deliberately holds the engine until the next
		// tick.
		break;
	case 5:
		check(v.m_noteBusy && v.m_noteDialog &&
		          !v.m_noteDialog.IsPrimaryButtonEnabled(),
		      "pending edit lost its dialog");
		v.m_noteDialog.Hide();
		owner.closeTab(owner.m_tabs.front().tab);
		check(owner.m_tabs.size() == 1 && v.m_noteDialog,
		      "close lost pending edit");
		notesGate->set_value();
		notesGate.reset();
		break;
	case 6:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && v.m_commentScan.rows()[0].text ==
		                            "Edited native \u03a9\U0001f600",
		      "edited contents did not refresh");
		notesPath = std::filesystem::temp_directory_path() /
		            (L"sable_note_edit_" +
		             std::to_wstring(GetCurrentProcessId()) + L".pdf");
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		break;
	case 7:
		check(notesSave.GetResults() && !v.m_notesDirty,
		      "edited note save failed");
		check(engine
		          .run([path = notesPath, markup = markupCommentOnly,
		                type = selectionMarkup] {
			          const auto saved = pdf::openFile(path);
			          if (!saved.document) {
				          return false;
			          }
			          if (markup) {
				          const auto value =
				              saved.document->markupComment(0, 0, type);
				          return value && value->present &&
				                 value->text ==
				                     u"Edited native \u03a9\U0001f600";
			          }
			          return saved.document->textNote(0, 0) ==
			                 u"Edited native \u03a9\U0001f600";
		          })
		          .get(),
		      "reopened note edit missing");
		v.CommentsList().SelectedIndex(0);
		invoke(v.EditNoteButton());
		break;
	case 8:
		if (!v.m_noteDialog) {
			return;
		}
		v.m_noteDialog.Content()
		    .as<StackPanel>()
		    .Children()
		    .GetAt(1)
		    .as<TextBox>()
		    .Text(L"Keep this draft");
		change(u"Edited native \u03a9\U0001f600", u"Changed elsewhere");
		button(L"PrimaryButton");
		break;
	case 9: {
		check(v.m_noteDialog && !v.m_notesDirty,
		      "refused edit lost dialog or savepoint");
		const auto fields = v.m_noteDialog.Content().as<StackPanel>();
		check(fields.Children().GetAt(1).as<TextBox>().Text() ==
		              L"Keep this draft" &&
		          !fields.Children().GetAt(1).as<TextBox>().IsReadOnly() &&
		          !fields.Children().GetAt(2).as<TextBlock>().Text().empty(),
		      "refused edit lost draft or error");
		button(L"CloseButton");
		break;
	}
	case 10:
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty, "Cancel dirtied savepoint");
		std::filesystem::remove(notesPath);
		status.Text(
		    L"PASS: selected note editor, exact no-op, Unicode Apply, "
		    L"pending close, refreshed comments, saved copy and retained "
		    L"stale draft");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::fillMarksTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	using Windows::Foundation::AsyncStatus;
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
	    (notesSave && notesSave.Status() == AsyncStatus::Started)) {
		return;
	}
	const auto invoke = [](const FrameworkElement& element) {
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto place = [&](int page) {
		const auto& rect = v.m_layout->pages()[page];
		check(v.placeNote({static_cast<float>(rect.x + .4 * rect.width),
		                   static_cast<float>(rect.y + .6 * rect.height)}),
		      "armed mark did not consume the page click");
	};
	const auto& rows = v.m_commentScan.rows();
	const auto inkOn = [&](int page) {
		return std::find_if(rows.begin(), rows.end(), [page](const auto& r) {
			return r.subtype == "Ink" && r.page == page;
		});
	};
	switch (step) {
	case 0:
		v.setCommentsOpen(true);
		v.requestTool(L"fillsign");
		check(v.FillSignActions().IsEnabled(), "fill marks are disabled");
		invokeCommentTool(v.PlaceCheckButton());
		check(v.notePlacing() && v.placeNote({-1, -1}) && v.notePlacing() &&
		          v.cancelNotePlacement() && !v.m_notesDirty,
		      "page gap or Escape changed the PDF");
		invokeCommentTool(v.PlaceCrossButton());
		check(v.notePlacing() &&
		          v.m_activeTool == ViewerPage::PlacementTool::FillCross,
		      "cross command did not arm placement");
		v.setCommentsOpen(false);
		check(v.notePlacing() && !v.m_notesDirty,
		      "closing Comments dropped the mark");
		v.cancelNotePlacement();
		v.setCommentsOpen(true);
		invokeCommentTool(v.PlaceCheckButton());
		place(0);
		check(v.m_noteBusy &&
		          v.m_activeTool == ViewerPage::ActiveTool::FillCheck,
		      "placing a check disarmed it while saving");
		place(0);
		check(v.m_noteBusy &&
		          v.m_activeTool == ViewerPage::ActiveTool::FillCheck,
		      "a busy click dropped the check tool or its write guard");
		break;
	case 1:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && v.notePlacing() && inkOn(0) != rows.end(),
		      "check mark did not refresh Comments");
		{
			const auto checks =
			    std::count_if(rows.begin(), rows.end(), [](const auto& row) {
				    return row.page == 0 && row.subtype == "Ink";
			    });
			if (checks == 1) {
				place(0);
				return;
			}
			check(checks == 2, "the repeated check was lost or duplicated");
		}
		check(v.cancelNotePlacement() && !v.notePlacing(),
		      "Esc kept repeated check placement armed");
		v.rotateView(1);
		v.goToPage(1);
		break;
	case 2:
		check(v.m_rotation == 1, "cross placement did not rotate");
		invokeCommentTool(v.PlaceCrossButton());
		place(1);
		break;
	case 3:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_activeTool == ViewerPage::ActiveTool::FillCross &&
		          inkOn(0) != rows.end() && inkOn(1) != rows.end(),
		      "cross did not land on the next page");
		v.setCursorTool(ViewerPage::CursorTool::Hand);
		check(!v.notePlacing(), "Hand kept the cross armed");
		v.beginNotePlacement(ViewerPage::PlacementTool::FillCross);
		notesPath = makeTempDir("fill_marks") / L"marked copy.pdf";
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		check(!v.notePlacing(), "saving kept the cross armed");
		break;
	case 4:
		check(notesSave.GetResults() && !v.m_notesDirty,
		      "saving the two marks failed");
		check(engine
		          .run([path = notesPath] {
			          const auto saved = pdf::openFile(path);
			          if (!saved.document) {
				          return false;
			          }
			          const auto tick = saved.document->printRaster(
			              0, 200, 300, 0, 0, 200, 300);
			          const auto cross = saved.document->printRaster(
			              1, 300, 200, 0, 0, 300, 200);
			          const auto blue = [](const pdf::Bitmap& image, int x,
			                               int y) {
				          return image.pixels[y * image.stride + x * 4];
			          };
			          return tick && cross && blue(*tick, 83, 177) < 128 &&
			                 blue(*tick, 76, 176) == 255 &&
			                 blue(*cross, 175, 124) < 128;
		          })
		          .get(),
		      "the saved check mark and cross did not print as placed");
		owner.openDocument(engine, notesPath, 1);
		break;
	case 5:
		if (v.m_path != notesPath) {
			return;
		}
		v.setCommentsOpen(true);
		break;
	case 6:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(inkOn(0) != rows.end() && inkOn(1) != rows.end() &&
		          !v.m_notesDirty && !v.m_noteSaving,
		      "the reopened copy lost a mark or was dirty");
		v.CommentsList().SelectedIndex(
		    static_cast<int>(inkOn(1) - rows.begin()));
		v.showComments();
		check(v.DeleteNoteButton().IsEnabled(), "cross cannot be removed");
		invoke(v.DeleteNoteButton());
		break;
	case 7:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && inkOn(0) != rows.end() &&
		          inkOn(1) == rows.end(),
		      "removing the cross lost the check mark or kept the cross");
		invokeCommentTool(v.PlaceFillTextButton());
		check(v.notePlacing() &&
		          v.m_activeTool == ViewerPage::PlacementTool::FillText &&
		          v.cancelNotePlacement(),
		      "Place text did not arm and cancel text placement");
		invoke(v.PlaceTypedSignatureButton());
		check(v.m_noteDialog && unbox_value<hstring>(v.m_noteDialog.Title()) ==
		                            L"Typed signature",
		      "the typed signature dialog did not open");
		v.m_noteDialog.Hide();
		break;
	case 8:
		if (v.m_noteDialog) {
			return;
		}
		v.m_signatureImage = helpers::typedSignature(u"Probe Signer");
		check(v.m_signatureImage.has_value(), "typed signature image failed");
		v.beginNotePlacement(ViewerPage::PlacementTool::TypedSignature);
		check(v.notePlacing(), "typed signature did not arm placement");
		place(0);
		break;
	case 9:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(std::any_of(rows.begin(), rows.end(),
		                  [](const auto& r) {
			                  return r.subtype == "Stamp" && r.page == 0;
		                  }),
		      "the typed signature did not reach Comments");
		invoke(v.PlaceDrawnSignatureButton());
		check(v.m_noteDialog && unbox_value<hstring>(v.m_noteDialog.Title()) ==
		                            L"Drawn signature",
		      "the drawn signature dialog did not open");
		v.m_noteDialog.Hide();
		break;
	case 10:
		if (v.m_noteDialog) {
			return;
		}
		ViewerPage::s_signatureStrokes = {{{20, 20}, {120, 80}, {220, 30}},
		                                  {{60, 100}, {300, 90}}};
		v.beginNotePlacement(ViewerPage::PlacementTool::DrawnSignature);
		check(v.notePlacing(), "drawn signature did not arm placement");
		place(0);
		break;
	case 11:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(std::count_if(rows.begin(), rows.end(),
		                    [](const auto& r) {
			                    return r.subtype == "Ink" && r.page == 0;
		                    }) == 3,
		      "the drawn signature did not reach Comments");
		v.close();
		status.Text(L"PASS: fill marks, text and signatures placement, save "
		            L"and Delete");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected fill marks step");
	}
	++step;
	ticks = 0;
}

void TabsTest::stampTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	using Windows::Foundation::AsyncStatus;
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending || v.m_stampPicking ||
	    (stampFixtures && stampFixtures.Status() == AsyncStatus::Started) ||
	    (notesSave && notesSave.Status() == AsyncStatus::Started)) {
		return;
	}
	const auto invoke = [](const FrameworkElement& element) {
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto at = [&](int page, double x, double y) {
		const auto& rect = v.m_layout->pages()[page];
		return Windows::Foundation::Point{
		    static_cast<float>(rect.x + x * rect.width),
		    static_cast<float>(rect.y + y * rect.height)};
	};
	const auto& rows = v.m_commentScan.rows();
	const auto stamp =
	    std::find_if(rows.begin(), rows.end(),
	                 [](const auto& row) { return row.subtype == "Stamp"; });
	switch (step) {
	case 0: {
		v.setCommentsOpen(true);
		const auto dir = makeTempDir("stamp_ui");
		stampPath = dir / L"selected stamp.PNG";
		notesPath = dir / L"saved stamp.pdf";
		stampFixtures = makeStampFixtures(stampPath);
		break;
	}
	case 1:
		stampFixtures.GetResults();
		v.m_stampPicker = [] { return attachmentChoice({}, {}); };
		invoke(v.PlaceStampButton());
		break;
	case 2:
		check(!v.notePlacing() && !v.m_notesDirty,
		      "canceling the picker changed the PDF");
		v.m_stampPicker =
		    [bad =
		         hstring((stampPath.parent_path() / L"wrong.jpg").wstring())] {
			    return attachmentChoice(bad, {});
		    };
		invoke(v.PlaceStampButton());
		break;
	case 3:
		check(!v.notePlacing() && !v.m_notesDirty && v.Message().IsOpen(),
		      "renamed PNG was accepted as JPEG");
		attachmentGate = std::make_shared<winrt::handle>(
		    CreateEventW(nullptr, TRUE, FALSE, nullptr));
		v.m_stampPicker = [path = hstring(stampPath.wstring()),
		                   gate = attachmentGate] {
			return attachmentChoice(path, gate);
		};
		invoke(v.PlaceStampButton());
		check(v.m_stampPicking && v.cancelNotePlacement(),
		      "Escape did not cancel a pending picker");
		SetEvent(attachmentGate->get());
		break;
	case 4:
		check(!v.notePlacing() && !v.m_stampImage && !v.m_notesDirty,
		      "a canceled picker result returned to the page");
		v.m_stampPicker = [path = hstring(stampPath.wstring())] {
			return attachmentChoice(path, {});
		};
		invoke(v.PlaceStampButton());
		break;
	case 5:
		check(v.notePlacing() && v.m_stampImage && v.m_stampImage->valid() &&
		          unbox_value<hstring>(v.PlaceStampButton().Content()) ==
		              L"Cancel stamp",
		      "a valid PNG did not arm placement");
		v.placeNote(at(1, .25, .25));
		v.placeNote(at(0, .6, .6));
		check(v.m_shapeStart && v.m_shapeStart->corner.page == 1 &&
		          v.notePlacing() && !v.m_notesDirty,
		      "a corner on another page consumed the stamp");
		v.placeNote(at(1, .65, .60));
		break;
	case 6:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && stamp != rows.end() && stamp->page == 1 &&
		          !v.notePlacing() && !v.m_stampImage,
		      "the placed stamp did not appear in Comments");
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		break;
	case 7:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(notesSave.GetResults() && !v.m_notesDirty,
		      "saving the stamp failed");
		check(engine
		          .run([path = notesPath] {
			          const auto saved = pdf::openFile(path);
			          if (!saved.document) {
				          return false;
			          }
			          const auto notes = saved.document->annotations(1);
			          return notes &&
			                 std::any_of(
			                     notes->entries.begin(), notes->entries.end(),
			                     [](const auto& note) {
				                     return note.subtype == "Stamp" &&
				                            note.bounds &&
				                            std::abs((note.bounds->right -
				                                      note.bounds->left) -
				                                     (note.bounds->top -
				                                      note.bounds->bottom)) <
				                                .002f;
			                     });
		          })
		          .get(),
		      "the reopened stamp lost its aspect ratio");
		check(stamp != rows.end(), "stamp row disappeared after save");
		v.CommentsList().SelectedIndex(static_cast<int>(stamp - rows.begin()));
		v.showComments();
		check(v.DeleteNoteButton().IsEnabled() &&
		          v.EditNoteButton().IsEnabled() &&
		          Automation::AutomationProperties::GetName(
		              v.EditNoteButton()) == L"Resize selected stamp",
		      "stamp did not offer Resize and guarded Delete");
		invoke(v.DeleteNoteButton());
		break;
	case 8:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && stamp == rows.end(),
		      "deleting the stamp did not refresh Comments");
		v.close();
		status.Text(L"PASS: image stamp picker, placement, save and Delete");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected image stamp step");
	}
	++step;
	ticks = 0;
}

void TabsTest::inkDrawingTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
	    (notesSave &&
	     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
		return;
	}
	const auto invoke = [&](const FrameworkElement& element) {
		using namespace Microsoft::UI::Xaml::Automation;
		const auto action =
		    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)
		        .GetPattern(Peers::PatternInterface::Invoke)
		        .as<Provider::IInvokeProvider>();
		action.Invoke();
	};
	// displayed fractions of the second page, in Pages coordinates
	const auto at = [&](double x, double y) {
		const auto& rect = v.m_layout->pages()[1];
		return Windows::Foundation::Point{
		    static_cast<float>(rect.x + x * rect.width),
		    static_cast<float>(rect.y + y * rect.height)};
	};
	const auto inkRows = [&] {
		const auto& rows = v.m_commentScan.rows();
		return std::count_if(rows.begin(), rows.end(), [](const auto& row) {
			return row.page == 1 && row.subtype == "Ink";
		});
	};
	const auto previewShown = [&] {
		return v.InkPreview().Visibility() == Visibility::Visible;
	};
	switch (step) {
	case 0:
		v.setCommentsOpen(true);
		v.zoomTo(1.5, {0, 0});
		v.goToPage(1);
		invokeCommentTool(v.DrawInkButton());
		check(v.notePlacing() &&
		          v.m_activeTool == ViewerPage::PlacementTool::Ink &&
		          unbox_value_or<bool>(v.DrawInkButton().IsChecked(), false) &&
		          !v.m_notesDirty,
		      "drawing did not arm");
		invokeCommentTool(v.DrawInkButton());
		check(!v.notePlacing() &&
		          !unbox_value_or<bool>(v.DrawInkButton().IsChecked(), false),
		      "Stop drawing kept the tool armed");
		break;
	case 1:
		invokeCommentTool(v.DrawInkButton());
		check(!v.beginInkStroke({-1, -1}) && !v.m_inkStroke,
		      "a page gap started a stroke");
		check(v.placeNote(at(.3, .3)) && !v.m_inkStroke && !v.m_noteDialog &&
		          v.notePlacing(),
		      "a click opened an editor while drawing");
		check(v.beginInkStroke(at(.2, .3)) && v.m_inkStroke && previewShown(),
		      "stroke did not start on the page");
		v.extendInkStroke(at(.3, .4));
		check(v.cancelNotePlacement() && !v.m_inkStroke && !previewShown() &&
		          !v.m_notesDirty,
		      "Esc kept the stroke or wrote it");
		invokeCommentTool(v.DrawInkButton());
		check(v.beginInkStroke(at(.2, .3)), "second stroke did not start");
		invoke(v.PlaceRectangleButton());
		check(!v.m_inkStroke && !previewShown() &&
		          v.m_activeTool == ViewerPage::PlacementTool::Rectangle,
		      "tool switch kept the stroke");
		invokeCommentTool(v.DrawInkButton());
		check(v.beginInkStroke(at(.2, .3)), "third stroke did not start");
		v.setCommentsOpen(false);
		check(v.m_inkStroke && v.notePlacing() && previewShown(),
		      "closing Comments dropped the drawing tool");
		v.cancelNotePlacement();
		v.setCommentsOpen(true);
		invokeCommentTool(v.DrawInkButton());
		check(v.beginInkStroke(at(.2, .3)), "fourth stroke did not start");
		v.rotateView(1);
		check(!v.m_inkStroke && v.notePlacing() && !previewShown(),
		      "rotation kept the stroke");
		v.rotateView(-1);
		check(!v.m_notesDirty, "canceled strokes changed the PDF");
		break;
	case 2:
		check(v.notePlacing() && v.beginInkStroke(at(.2, .3)),
		      "stroke did not start after rotation");
		// the last point runs off the page and has to stay on it
		for (const auto& [x, y] : {std::pair{.3, .4}, std::pair{.4, .35},
		                           std::pair{.5, .45}, std::pair{1.3, .5}}) {
			v.extendInkStroke(at(x, y));
		}
		check(v.m_inkStroke && v.m_inkStroke->points.size() == 5 &&
		          v.InkPreviewLine().Points().Size() == 6,
		      "stroke lost points or its preview");
		check(v.m_inkStroke->points.back().x > .99 &&
		          v.m_inkStroke->points.back().x < 1,
		      "off-page point was not kept inside the page");
		v.finishInkStroke();
		v.chooseInkStyle(pdf::AnnotationColor{37, 99, 235, 255}, 1.f);
		check(v.m_noteBusy && !v.m_inkStroke && previewShown() &&
		          v.beginInkStroke(at(.6, .6)) && previewShown(),
		      "the preceding write dropped a second stroke");
		v.finishInkStroke();
		v.chooseInkStyle(pdf::AnnotationColor{0, 0, 0, 255}, 2.f);
		check(v.beginInkStroke(at(.65, .65)),
		      "queued strokes blocked the next pointer draft");
		break;
	case 3:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && v.notePlacing() && v.m_inkStroke &&
		          previewShown() && inkRows() == 2,
		      "a completed stroke was lost or cleared the next pointer draft");
		v.dropInkStroke();
		{
			// five pen colors, a separator and three widths
			const auto items = v.InkStyleMenu().Items();
			check(items.Size() == 9 &&
			          items.GetAt(2).as<RadioMenuFlyoutItem>().Text() ==
			              L"Red" &&
			          items.GetAt(2)
			              .as<RadioMenuFlyoutItem>()
			              .Icon()
			              .try_as<ImageIcon>() &&
			          items.GetAt(8).as<RadioMenuFlyoutItem>().Text() ==
			              L"Thick (4 pt)",
			      "drawing style menu is incomplete");
			const pdf::InkStyle red{{235, 73, 73, 255}, 4};
			v.chooseInkStyle(red.color, red.width);
			check(v.notePlacing() && v.s_inkStyle == red &&
			          v.NotesState().Text() ==
			              L"Drag on a page to draw (Red, 4 pt). Esc "
			              L"stops drawing.",
			      "choosing a style lost drawing or its hint");
			check(items.GetAt(2).as<RadioMenuFlyoutItem>().IsChecked() &&
			          items.GetAt(8).as<RadioMenuFlyoutItem>().IsChecked() &&
			          !items.GetAt(0).as<RadioMenuFlyoutItem>().IsChecked() &&
			          !items.GetAt(7).as<RadioMenuFlyoutItem>().IsChecked(),
			      "the style menu did not follow the shared style");
			check(v.beginInkStroke(at(.7, .8)) && v.m_inkStroke &&
			          v.m_inkStroke->style == red,
			      "dot did not start with the chosen style");
			const auto shown = helpers::applyFilter(v.m_values, {235, 73, 73});
			const auto brush = v.InkPreviewLine()
			                       .Stroke()
			                       .as<Media::SolidColorBrush>()
			                       .Color();
			check(brush.R == shown.r && brush.G == shown.g &&
			          brush.B == shown.b,
			      "preview color skipped the reading filter");
		}
		v.finishInkStroke();
		check(v.m_noteBusy, "dot was not queued");
		check(v.cancelNotePlacement() && v.m_noteBusy,
		      "Esc lost a completed drawing or its write guard");
		owner.m_notesPicker = [](hstring) { return attachmentChoice({}, {}); };
		owner.onSaveCopy({}, {});
		{
			const auto closeReview = owner.confirmNotes(current);
			check(v.m_noteBusy && !v.m_noteSaving &&
			          closeReview.Status() ==
			              Windows::Foundation::AsyncStatus::Completed &&
			          !closeReview.GetResults(),
			      "save or close passed a queued drawing");
		}
		break;
	case 4:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(inkRows() == 3, "single tap did not add a drawing");
		check(!v.notePlacing(), "Stop drawing kept the tool armed");
		v.chooseInkStyle(std::nullopt, 1.f);
		check(!v.notePlacing() && v.s_inkStyle.width == 1 &&
		          v.s_inkStyle.color.red == 235,
		      "an idle style choice armed drawing or kept the old width");
		check(!v.notePlacing() && v.BarSaveButton().IsEnabled() &&
		          v.m_notesDirty,
		      "Stop drawing did not allow saving");
		notesPath =
		    std::filesystem::temp_directory_path() /
		    (L"sable_ink_" + std::to_wstring(GetCurrentProcessId()) + L".pdf");
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		break;
	case 5: {
		check(notesSave.GetResults() && !v.m_notesDirty, "drawing save failed");
		check(engine
		          .run([path = notesPath] {
			          const auto saved = pdf::openFile(path);
			          if (!saved.document) {
				          return false;
			          }
			          const auto notes = saved.document->annotations(1);
			          if (!notes) {
				          return false;
			          }
			          std::vector<pdf::AnnotationBounds> ink;
			          for (const auto& item : notes->entries) {
				          if (item.subtype == "Ink" && item.bounds) {
					          ink.push_back(*item.bounds);
				          }
			          }
			          // the page turns 90 degrees, so the long stroke can
			          // run along either PDF axis. the red dot is exactly
			          // its 4 pt line width across.
			          return ink.size() == 3 &&
			                 (std::max)(ink[0].right - ink[0].left,
			                            ink[0].top - ink[0].bottom) > 50 &&
			                 std::abs(ink[1].right - ink[1].left - 1) < 1e-3 &&
			                 std::abs(ink[1].top - ink[1].bottom - 1) < 1e-3 &&
			                 std::abs(ink[2].right - ink[2].left - 4) < 1e-3 &&
			                 std::abs(ink[2].top - ink[2].bottom - 4) < 1e-3;
		          })
		          .get(),
		      "saved drawings lost their strokes");
		std::filesystem::remove(notesPath);
		const auto& rows = v.m_commentScan.rows();
		const auto dot =
		    std::find_if(rows.rbegin(), rows.rend(),
		                 [](const auto& row) { return row.subtype == "Ink"; });
		check(dot != rows.rend(), "drawing row missing after save");
		v.CommentsList().SelectedIndex(static_cast<int>(rows.rend() - dot - 1));
		v.showComments();
		check(v.DeleteNoteButton().IsEnabled() &&
		          v.EditNoteButton().IsEnabled() &&
		          Automation::AutomationProperties::GetName(
		              v.EditNoteButton()) == L"Edit selected drawing",
		      "drawing row did not offer Edit drawing and Delete");
		invoke(v.DeleteNoteButton());
		break;
	}
	case 6:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && inkRows() == 2,
		      "Delete kept the drawing or a clean savepoint");
		invokeCommentTool(v.DrawInkButton());
		check(v.beginInkStroke(at(.2, .6)),
		      "stroke before close did not start");
		v.finishInkStroke();
		check(v.beginInkStroke(at(.3, .6)), "close test lost a second stroke");
		v.finishInkStroke();
		check(v.beginInkStroke(at(.4, .6)),
		      "close test lost its pointer draft");
		v.close();
		check(!v.m_inkStroke && !v.notePlacing() && v.m_inkQueue.empty() &&
		          !v.m_inkWriting,
		      "forced close kept drawing input or its queue");
		status.Text(L"PASS: drawing arms and stops, cancels on Esc, tool "
		            L"switch, hidden Comments and rotation, keeps strokes "
		            L"on the page, styles the preview and the saved dot, "
		            L"saves and deletes drawings");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::notePlacementTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
	    (notesSave &&
	     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
		return;
	}
	const auto invoke = [&](const FrameworkElement& element) {
		using namespace Microsoft::UI::Xaml::Automation;
		const auto action =
		    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)
		        .GetPattern(Peers::PatternInterface::Invoke)
		        .as<Provider::IInvokeProvider>();
		action.Invoke();
	};
	const auto dialogButton = [&](const wchar_t* name) {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& element) -> Button {
			if (const auto button = element.try_as<Button>();
			    button && button.Name() == name) {
				return button;
			}
			for (int i = 0;
			     i < Media::VisualTreeHelper::GetChildrenCount(element); ++i) {
				if (const auto button =
				        find(Media::VisualTreeHelper::GetChild(element, i))) {
					return button;
				}
			}
			return nullptr;
		};
		const auto button = find(v.m_noteDialog);
		check(button != nullptr && button.IsEnabled(),
		      "placement editor button unavailable");
		invoke(button);
	};
	const auto choosePoint = [&] {
		const auto& rect = v.m_layout->pages()[1];
		return Windows::Foundation::Point{
		    static_cast<float>(rect.x + .25 * rect.width),
		    static_cast<float>(rect.y + .7 * rect.height)};
	};
	const auto shapeType = ellipsePlacementOnly ? "Circle" : "Square";
	const auto placeShape = [&] {
		if (ellipsePlacementOnly) {
			invoke(v.PlaceEllipseButton());
		} else {
			invoke(v.PlaceRectangleButton());
		}
	};
	switch (step) {
	case 0:
		v.setCommentsOpen(true);
		invokeCommentTool(v.PlaceNoteButton());
		check(v.notePlacing() && !v.m_notesDirty &&
		          !v.AddNoteButton().IsEnabled(),
		      "placement did not arm independently of centered Add");
		break;
	case 1: {
		check(v.placeNote({-1, -1}) && v.notePlacing() && !v.m_noteDialog,
		      "page gap opened editor or activated a link");
		const auto review = owner.confirmNotes(current);
		check(review.Status() == Windows::Foundation::AsyncStatus::Completed &&
		          review.GetResults() && !v.notePlacing(),
		      "an armed tool refused a clean close");
		v.beginNotePlacement();
		v.m_noteBusy = true;
		owner.closeTab(owner.m_tabs.front().tab);
		check(owner.m_tabs.size() == 1 && !v.notePlacing() &&
		          owner.PickerError().IsOpen(),
		      "tab close kept placement or lost the write guard");
		v.m_noteBusy = false;
		v.beginNotePlacement();
		v.m_noteBusy = true;
		HWND handle{};
		check_hresult(
		    owner.try_as<::IWindowNative>()->get_WindowHandle(&handle));
		SendMessageW(handle, WM_CLOSE, 0, 0);
		check(!owner.m_closing && !v.notePlacing(),
		      "window close kept placement or lost the write guard");
		v.m_noteBusy = false;
		v.beginNotePlacement();
		invokeCommentTool(v.PlaceNoteButton());
		check(!v.notePlacing() && !v.m_notesDirty,
		      "Cancel placement changed PDF");
		v.openFind();
		v.beginNotePlacement();
		check(v.cancelNotePlacement() && v.findOpen(),
		      "placement cancel closed Find");
		v.closeFind();
		v.beginNotePlacement();
		v.Visibility(Visibility::Collapsed);
		check(v.notePlacing(), "switching tabs dropped placement");
		v.Visibility(Visibility::Visible);
		v.setCommentsOpen(false);
		check(v.notePlacing(), "closing Comments dropped placement");
		v.cancelNotePlacement();
		v.setCommentsOpen(true);
		break;
	}
	case 2:
		v.zoomTo(1.5, {0, 0});
		v.goToPage(1);
		v.setCommentsOpen(false);
		invokeCommentTool(v.PlaceNoteButton());
		check(v.placeNote(choosePoint()) && v.m_noteDialog &&
		          !v.notePlacing() && !v.m_notesDirty,
		      "chosen location mutated before Add");
		v.setCommentsOpen(true);
		break;
	case 3: {
		const auto fields = v.m_noteDialog.Content().as<StackPanel>();
		check(fields.Children().GetAt(0).as<TextBlock>().Text() == L"Page 2",
		      "wrong placement target in editor");
		fields.Children().GetAt(1).as<TextBox>().Text(L"Author");
		fields.Children().GetAt(2).as<TextBox>().Text(L"Placed native \u03a9");
		dialogButton(L"CloseButton");
		break;
	}
	case 4:
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty && v.m_noteDraft == L"Placed native \u03a9",
		      "editor cancellation wrote or lost draft");
		v.beginNotePlacement();
		check(v.placeNote(choosePoint()) && v.m_noteDialog,
		      "retry target did not open");
		// i change the reading view after choosing the original location.
		v.zoomTo(.8, {0, 0});
		v.goToPage(3);
		break;
	case 5:
		dialogButton(L"PrimaryButton");
		break;
	case 6:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty &&
		          std::any_of(v.m_commentScan.rows().begin(),
		                      v.m_commentScan.rows().end(),
		                      [](const auto& row) {
			                      return row.page == 1 &&
			                             row.text == "Placed native \u03a9" &&
			                             row.position &&
			                             std::abs(row.position->x - .25) <
			                                 2e-6 &&
			                             std::abs(row.position->y - .7) < 2e-6;
		                      }),
		      "saved target followed later navigation or zoom");
		notesPath = std::filesystem::temp_directory_path() /
		            (L"sable_placed_" + std::to_wstring(GetCurrentProcessId()) +
		             L".pdf");
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		break;
	case 7:
		check(notesSave.GetResults() && !v.m_notesDirty,
		      "placed note save failed");
		check(engine
		          .run([path = notesPath, shapeType] {
			          const auto saved = pdf::openFile(path);
			          if (!saved.document) {
				          return false;
			          }
			          const auto notes = saved.document->annotations(1);
			          if (!notes || notes->entries.empty()) {
				          return false;
			          }
			          const auto& note = notes->entries.back();
			          return note.contents == "Placed native \u03a9" &&
			                 note.position &&
			                 std::abs(note.position->x - .25) < 2e-6 &&
			                 std::abs(note.position->y - .7) < 2e-6;
		          })
		          .get(),
		      "reopened note moved from chosen location");
		std::filesystem::remove(notesPath);
		placeShape();
		if (ellipsePlacementOnly) {
			v.placeNote(choosePoint());
			invoke(v.PlaceRectangleButton());
			check(v.notePlacing() && !v.m_shapeStart &&
			          v.m_activeTool == ViewerPage::PlacementTool::Rectangle,
			      "tool switch retained the ellipse corner");
			v.placeNote(choosePoint());
			invoke(v.PlaceEllipseButton());
			check(v.notePlacing() && !v.m_shapeStart &&
			          v.m_activeTool == ViewerPage::PlacementTool::Ellipse,
			      "tool switch retained the rectangle corner");
		}
		check(v.placeNote(choosePoint()) && v.m_shapeStart && !v.m_notesDirty &&
		          !v.m_noteBusy,
		      "first shape corner changed the PDF");
		break;
	case 8: {
		const auto& other = v.m_layout->pages()[0];
		check(v.placeNote({static_cast<float>(other.x + other.width / 2),
		                   static_cast<float>(other.y + other.height / 2)}) &&
		          v.m_shapeStart && v.m_shapeStart->corner.page == 1,
		      "another page replaced the shape corner");
		check(v.placeNote({-1, -1}) && v.m_shapeStart,
		      "gap lost the shape corner");
		check(v.cancelNotePlacement() && !v.m_shapeStart,
		      "Cancel retained the shape corner");
		placeShape();
		v.placeNote(choosePoint());
		v.setCommentsOpen(false);
		check(v.notePlacing() && v.m_shapeStart,
		      "closing Comments dropped shape placement");
		v.cancelNotePlacement();
		v.setCommentsOpen(true);
		placeShape();
		v.placeNote(choosePoint());
		const int previousRotation = v.m_rotation;
		v.rotateView(1);
		check(v.m_rotation != previousRotation && !v.m_shapeStart &&
		          v.notePlacing(),
		      "rotation retained the old shape corner");
		v.rotateView(-1);
		v.placeNote(choosePoint());
		v.placeNote(choosePoint());
		break;
	}
	case 9: {
		check(!v.m_notesDirty && !v.notePlacing() && !v.m_shapeStart &&
		          v.Message().IsOpen(),
		      "empty shape changed the PDF or kept placement");
		placeShape();
		v.placeNote(choosePoint());
		const auto& page = v.m_layout->pages()[1];
		v.placeNote({static_cast<float>(page.x + .65 * page.width),
		             static_cast<float>(page.y + .4 * page.height)});
		break;
	}
	case 10: {
		if (!v.m_commentScan.done()) {
			return;
		}
		const auto& rows = v.m_commentScan.rows();
		const auto shape = std::find_if(
		    rows.begin(), rows.end(), [shapeType](const auto& row) {
			    return row.page == 1 && row.subtype == shapeType;
		    });
		check(shape != rows.end() && v.m_notesDirty && !v.Message().IsOpen(),
		      "shape missing from refreshed Comments");
		v.CommentsList().SelectedIndex(static_cast<int>(shape - rows.begin()));
		v.showComments();
		check(v.DeleteNoteButton().IsEnabled() &&
		          v.EditNoteButton().IsEnabled(),
		      "shape actions do not match their support");
		notesPath = std::filesystem::temp_directory_path() /
		            (L"sable_shape_" + std::to_wstring(GetCurrentProcessId()) +
		             L".pdf");
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		notesSave = owner.saveNotes(current);
		break;
	}
	case 11:
		check(notesSave.GetResults() && !v.m_notesDirty,
		      "shape save did not finish");
		check(engine
		          .run([path = notesPath, shapeType] {
			          const auto saved = pdf::openFile(path);
			          if (!saved.document) {
				          return false;
			          }
			          const auto notes = saved.document->annotations(1);
			          if (!notes) {
				          return false;
			          }
			          bool note = false, shape = false;
			          for (const auto& item : notes->entries) {
				          note =
				              note || item.contents == "Placed native \u03a9";
				          if (item.subtype == shapeType && item.displayBounds) {
					          const auto& box = *item.displayBounds;
					          shape = std::abs(box.left - .25) < 2e-6 &&
					                  std::abs(box.top - .4) < 2e-6 &&
					                  std::abs(box.right - .65) < 2e-6 &&
					                  std::abs(box.bottom - .7) < 2e-6;
				          }
			          }
			          return note && shape;
		          })
		          .get(),
		      "reopened shape moved or lost its neighboring note");
		// i open the editor while later Comments pages are still loading.
		v.m_commentScan.reset(static_cast<int>(v.m_sizes.size()));
		for (int p = 0; p <= 1; ++p) {
			auto notes =
			    engine
			        .run([path = notesPath, p] {
				        return pdf::openFile(path).document->annotations(p);
			        })
			        .get();
			check(notes.has_value(), "couldn't read the staged Comments page");
			notes->revision = v.m_session->annotationRevision();
			for (auto& note : notes->entries) {
				note.target.revision = notes->revision;
			}
			check(notes && v.m_commentScan.append(std::move(*notes)),
			      "couldn't stage the unfinished Comments scan");
		}
		{
			const auto& rows = v.m_commentScan.rows();
			const auto shape = std::find_if(rows.begin(), rows.end(),
			                                [shapeType](const auto& row) {
				                                return row.subtype == shapeType;
			                                });
			check(shape != rows.end(), "missing shape before scan overlap");
			v.CommentsList().SelectedIndex(
			    static_cast<int>(shape - rows.begin()));
			v.showComments();
		}
		invoke(v.EditNoteButton());
		break;
	case 12:
		if (!v.m_noteDialog) {
			return;
		}
		dialogButton(L"CloseButton");
		break;
	case 13:
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty && !v.m_commentScan.failed(),
		      "Cancel resized the shape or failed the Comments scan");
		invoke(v.EditNoteButton());
		break;
	case 14:
		if (!v.m_noteDialog) {
			return;
		}
		dialogButton(L"PrimaryButton");
		break;
	case 15:
		if (v.m_noteDialog) {
			return;
		}
		check(!v.m_notesDirty, "unchanged shape Apply dirtied the PDF");
		invoke(v.EditNoteButton());
		break;
	case 16: {
		if (!v.m_noteDialog) {
			return;
		}
		const auto fields = v.m_noteDialog.Content()
		                        .as<ScrollViewer>()
		                        .Content()
		                        .as<StackPanel>();
		const auto width = fields.Children().GetAt(1).as<TextBox>();
		width.Text(L"-1");
		dialogButton(L"PrimaryButton");
		check(!v.m_noteBusy && !v.m_notesDirty &&
		          !fields.Children().GetAt(3).as<TextBlock>().Text().empty(),
		      "invalid dimension lost its draft or changed the PDF");
		width.Text(L"1");
		dialogButton(L"PrimaryButton");
		check(v.m_noteBusy && width.IsReadOnly() &&
		          !v.m_noteDialog.IsPrimaryButtonEnabled(),
		      "queued shape resize left controls enabled");
		owner.closeTab(owner.m_tabs.front().tab);
		check(owner.m_tabs.size() == 1 && v.m_noteDialog,
		      "tab close lost the shape draft");
		break;
	}
	case 17: {
		check(v.m_noteDialog && !v.m_notesDirty,
		      "refused shape size changed the PDF or closed its editor");
		const auto fields = v.m_noteDialog.Content()
		                        .as<ScrollViewer>()
		                        .Content()
		                        .as<StackPanel>();
		const auto width = fields.Children().GetAt(1).as<TextBox>();
		const auto height = fields.Children().GetAt(2).as<TextBox>();
		check(width.Text() == L"1" && !width.IsReadOnly() &&
		          v.m_noteDialog.IsPrimaryButtonEnabled() &&
		          !fields.Children().GetAt(3).as<TextBlock>().Text().empty(),
		      "refused resize lost its editable dimensions or error");
		width.Text(L"120");
		height.Text(L"80");
		dialogButton(L"PrimaryButton");
		break;
	}
	case 18:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty, "changed shape Apply kept a clean savepoint");
		notesSave = owner.saveNotes(current);
		break;
	case 19:
		check(notesSave.GetResults() && !v.m_notesDirty,
		      "resized shape save failed");
		check(engine
		          .run([path = notesPath, shapeType] {
			          const auto saved = pdf::openFile(path);
			          if (!saved.document) {
				          return false;
			          }
			          const auto notes = saved.document->annotations(1);
			          if (!notes) {
				          return false;
			          }
			          bool note = false, shape = false;
			          for (const auto& item : notes->entries) {
				          note =
				              note || item.contents == "Placed native \u03a9";
				          if (item.subtype == shapeType && item.bounds) {
					          const auto& b = *item.bounds;
					          // the placed corner is fractional, so the
					          // float edges can miss by an ulp.
					          shape = std::abs(b.right - b.left - 120) < 1e-3 &&
					                  std::abs(b.top - b.bottom - 80) < 1e-3;
				          }
			          }
			          return note && shape;
		          })
		          .get(),
		      "saved resize lost dimensions or its neighboring note");
		{
			const auto& rows = v.m_commentScan.rows();
			const auto shape = std::find_if(rows.begin(), rows.end(),
			                                [shapeType](const auto& row) {
				                                return row.subtype == shapeType;
			                                });
			check(shape != rows.end(), "resized shape missing from Comments");
			v.CommentsList().SelectedIndex(
			    static_cast<int>(shape - rows.begin()));
			v.showComments();
		}
		invoke(v.DeleteNoteButton());
		break;
	case 20:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && std::none_of(v.m_commentScan.rows().begin(),
		                                     v.m_commentScan.rows().end(),
		                                     [shapeType](const auto& row) {
			                                     return row.subtype ==
			                                            shapeType;
		                                     }),
		      "shape Delete kept the shape or a clean savepoint");
		std::filesystem::remove(notesPath);
		placeShape();
		v.placeNote(choosePoint());
		v.close();
		check(!v.notePlacing() && !v.m_shapeStart && !v.m_noteDialog,
		      "forced close kept placement state");
		status.Text(
		    L"PASS: note and shape placement, tool changes, cancel, "
		    L"rotation, "
		    L"cross-page refusal, resize drafts, saved positions and shape "
		    L"Delete");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::notesTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_commentsPending ||
	    (notesSave &&
	     notesSave.Status() == Windows::Foundation::AsyncStatus::Started)) {
		return;
	}
	const auto choose = [&](ContentDialogResult choice) {
		owner.m_notesChoice = [choice] { return notesChoice(choice); };
	};
	const auto pick = [&](const std::filesystem::path& path) {
		owner.m_notesPicker = [value = hstring(path.wstring())](hstring) {
			return attachmentChoice(value, {});
		};
	};
	switch (step) {
	case 0: {
		const auto raster = v.m_device.bitmaps.find(0);
		if (raster == v.m_device.bitmaps.end() ||
		    raster->second.version != v.m_renderVersion) {
			return;
		}
		if (!notesOldRaster) {
			notesOldRaster = raster->second.bitmap;
			const auto version = v.m_renderVersion;
			v.refreshNotes();
			const auto retained = v.m_device.bitmaps.find(0);
			check(retained != v.m_device.bitmaps.end() &&
			          retained->second.bitmap.get() == notesOldRaster.get() &&
			          retained->second.version == version &&
			          v.m_renderVersion == version + 1 &&
			          !v.m_cache.find(v.key(0)),
			      "refresh dropped the displayed raster or kept stale input");
			return;
		}
		check(raster->second.bitmap.get() != notesOldRaster.get(),
		      "refresh did not upload a raster for the new revision");
		if (!notesFailedReplacement) {
			v.refreshNotes();
			const auto maximum = v.m_device.context->GetMaximumBitmapSize();
			const pdf::Bitmap oversized{
			    static_cast<int>(maximum) + 1, 1, 0, {}};
			check(!v.showRaster(0, &oversized),
			      "oversized replacement unexpectedly uploaded");
			check(!v.m_device.bitmaps.contains(0),
			      "failed upload kept pre-edit pixels on the page");
			notesFailedReplacement = true;
			return;
		}
		notesOldRaster = nullptr;
		v.setCommentsOpen(true);
		check(v.AddNoteButton().IsEnabled(), "note creation disabled");
		v.editNote();
		break;
	}
	case 1: {
		check(v.m_noteDialog != nullptr, "note editor did not open");
		const auto fields = v.m_noteDialog.Content().as<StackPanel>();
		if (fields.Children().GetAt(2).as<TextBox>().Text().empty()) {
			check(!v.m_noteDialog.IsPrimaryButtonEnabled(),
			      "empty note could be submitted");
			fields.Children().GetAt(1).as<TextBox>().Text(L"Abhilash");
			fields.Children().GetAt(2).as<TextBox>().Text(
			    L"Native \u03a9\U0001f600");
			return;
		}
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& element) -> Button {
			if (const auto button = element.try_as<Button>();
			    button && button.Name() == L"PrimaryButton") {
				return button;
			}
			for (int i = 0;
			     i < Media::VisualTreeHelper::GetChildrenCount(element); ++i) {
				if (const auto button =
				        find(Media::VisualTreeHelper::GetChild(element, i))) {
					return button;
				}
			}
			return nullptr;
		};
		const auto button = find(v.m_noteDialog);
		check(button != nullptr, "editor Add button missing");
		check(button.IsEnabled(), "editor Add button still disabled");
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
		break;
	}
	case 2:
		if (v.m_noteDialog || !v.m_commentScan.done()) {
			return;
		}
		check(v.m_notesDirty && v.m_renderVersion > 0 &&
		          v.BarSaveButton().IsEnabled() &&
		          v.NotesState().Text() == L"Unsaved changes",
		      "completed note did not mark unsaved state");
		check(std::any_of(v.m_commentScan.rows().begin(),
		                  v.m_commentScan.rows().end(),
		                  [](const auto& row) {
			                  return row.text == "Native \u03a9\U0001f600" &&
			                         row.author == "Abhilash";
		                  }),
		      "new note missing from refreshed comments");
		choose(ContentDialogResult::None);
		owner.closeTab(owner.m_tabs.front().tab);
		break;
	case 3:
		owner.m_notesChoice =
		    []() -> Windows::Foundation::IAsyncOperation<ContentDialogResult> {
			throw hresult_error(E_ILLEGAL_METHOD_CALL);
		};
		owner.m_allowNoteClose = true;
		owner.closeWithNotes();
		break;
	case 4: {
		if (owner.m_closePending) {
			return;
		}
		check(owner.m_tabs.size() == 1 && v.m_notesDirty,
		      "Cancel lost the edited tab");
		check(!owner.m_closing && !owner.m_allowNoteClose && !v.m_noteDialog &&
		          v.Message().IsOpen() &&
		          std::wstring_view(v.Message().Message())
		                  .find(L"Couldn't review unsaved notes") == 0,
		      "close review exception lost its notice or left close allowed");
		v.Message().IsOpen(false);
		choose(ContentDialogResult::None);
		HWND handle{};
		check_hresult(
		    owner.try_as<::IWindowNative>()->get_WindowHandle(&handle));
		SendMessageW(handle, WM_CLOSE, 0, 0);
		check(!owner.m_closing, "native close bypassed unsaved prompt");
		pick({});
		notesSave = owner.saveNotes(current);
		break;
	}
	case 5:
		check(!notesSave.GetResults() && v.m_notesDirty,
		      "canceled picker cleared notes");
		pick(v.m_path);
		notesSave = owner.saveNotes(current);
		break;
	case 6:
		check(!notesSave.GetResults() && v.m_notesDirty,
		      "source destination accepted or cleared notes");
		notesPath = std::filesystem::temp_directory_path() /
		            (L"sable_notes_" + std::to_wstring(GetCurrentProcessId()) +
		             L".pdf");
		pick(notesPath);
		notesSave = owner.saveNotes(current);
		break;
	case 7:
		check(notesSave.GetResults() && !v.m_notesDirty,
		      "successful save did not clear the UI savepoint");
		if (!notesRefusalTried) {
			notesRefusalTried = true;
			v.submitNote(-1, u"Rejected after save", {});
			return;
		}
		check(engine
		          .run([path = notesPath] {
			          const auto saved = pdf::openFile(path);
			          return saved.document &&
			                 saved.document->annotations(0)
			                         ->entries.back()
			                         .contents == "Native \u03a9\U0001f600";
		          })
		          .get(),
		      "saved copy lost Unicode note");
		notesGate = std::make_shared<std::promise<void>>();
		engine.post([wait = notesGate->get_future().share()] { wait.wait(); });
		v.submitNote(0, u"Second", {});
		owner.closeTab(owner.m_tabs.front().tab);
		check(owner.m_tabs.size() == 1 && v.m_noteBusy,
		      "pending edit was closed");
		notesGate->set_value();
		notesGate.reset();
		break;
	case 8:
		check(v.m_notesDirty, "later note did not mark unsaved state");
		pick(notesPath / L"missing" / L"notes.pdf");
		choose(ContentDialogResult::Primary);
		owner.closeTab(owner.m_tabs.front().tab);
		break;
	case 9:
		if (owner.m_closePending) {
			return;
		}
		check(owner.m_tabs.size() == 1 && v.m_notesDirty,
		      "failed save closed the edited tab");
		choose(ContentDialogResult::Secondary);
		owner.closeTab(owner.m_tabs.front().tab);
		check(owner.m_tabs.empty(), "Discard did not close tab");
		owner.openDocument(engine, L"tests/fixtures/attachments.pdf", 1);
		break;
	case 10:
		v.submitNote(0, u"Must not lose attachments", {});
		break;
	case 11:
		check(!v.m_notesDirty, "attachment refusal changed the document");
		owner.closeTab(owner.m_tabs.front().tab);
		owner.openDocument(engine, L"tests/fixtures/notes_denied.pdf", 1);
		break;
	case 12:
		v.setCommentsOpen(true);
		check(!v.AddNoteButton().IsEnabled(), "denied PDF enabled notes");
		v.editNote();
		check(!v.m_noteDialog, "denied PDF opened editor");
		peerWindow = make_self<MainWindow>(owner.m_settings);
		peerWindow->setEngine(engine);
		peerWindow->openDocument(engine,
		                         L"tests/fixtures/comment_locations.pdf", 1);
		peerWindow->Activate();
		break;
	case 13: {
		const auto peer = peerWindow->Viewer();
		if (!peer || !ready(*get_self<ViewerPage>(peer))) {
			return;
		}
		get_self<ViewerPage>(peer)->submitNote(0, u"Window close", {});
		break;
	}
	case 14: {
		const auto pdf = get_self<ViewerPage>(peerWindow->Viewer());
		if (pdf->m_noteBusy) {
			return;
		}
		check(pdf->m_notesDirty, "peer window note missing");
		peerWindow->m_notesChoice = [] {
			return notesChoice(ContentDialogResult::None);
		};
		HWND handle{};
		check_hresult(
		    peerWindow->try_as<::IWindowNative>()->get_WindowHandle(&handle));
		SendMessageW(handle, WM_SYSCOMMAND, SC_CLOSE, 0);
		check(!peerWindow->m_closing, "system close ignored Cancel");
		peerWindow->m_notesChoice = [] {
			return notesChoice(ContentDialogResult::Primary);
		};
		peerWindow->m_notesPicker =
		    [path = hstring(notesPath.wstring())](hstring) {
			    return attachmentChoice(path, {});
		    };
		SendMessageW(handle, WM_CLOSE, 0, 0);
		break;
	}
	case 15:
		if (peerWindow->m_closePending) {
			return;
		}
		check(peerWindow->m_closing, "saved window did not close");
		peerWindow = nullptr;
		std::filesystem::remove(notesPath);
		status.Text(
		    L"PASS: note editor, refresh, Unicode save/reopen, "
		    L"source protection, unsaved close, pending edit and refusals");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::commentsTick(MainWindow& owner) {
	if (step == 11) {
		check(first && first->m_closed && !first->m_commentsPending &&
		          first->CommentsList().Items().Size() == 0 &&
		          first->m_commentScan.rows().empty(),
		      "closed comments accepted a late result");
		status.Text(L"PASS: comments ordering, Unicode, navigation, sidebar "
		            L"memory, pause/resume, empty/retry/limited and close");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_commentsPending) {
		return;
	}
	const auto invoke = [](const FrameworkElement& element) {
		using namespace Microsoft::UI::Xaml::Automation;
		const auto peer =
		    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
		        element);
		const auto action =
		    peer ? peer.GetPattern(Peers::PatternInterface::Invoke)
		               .try_as<Provider::IInvokeProvider>()
		         : nullptr;
		if (action) {
			action.Invoke();
		}
		return static_cast<bool>(action);
	};
	const auto pause = [&](bool rail) {
		auto release = std::make_shared<std::promise<void>>();
		auto wait = release->get_future().share();
		engine.post([wait] { wait.wait(); });
		v.m_commentScan.reset(3);
		v.CommentsList().Items().Clear();
		v.showComments();
		const bool pending = v.m_commentsPending;
		commentVersion = v.m_commentVersion;
		if (rail) {
			release->set_value();
			auto complete = std::make_shared<std::promise<void>>();
			auto finished = complete->get_future();
			engine.post([complete] { complete->set_value(); });
			// i hold this ui turn until the old reply is queued.
			check(finished.wait_for(std::chrono::seconds(2)) ==
			          std::future_status::ready,
			      "comments reply did not reach the UI queue");
			v.Rail().Visibility(Visibility::Collapsed);
		} else {
			v.Visibility(Visibility::Collapsed);
			release->set_value();
		}
		check(pending && !v.m_commentsPending &&
		          v.m_commentVersion > commentVersion &&
		          v.CommentsPanel().Visibility() == Visibility::Collapsed,
		      "hidden comments did not cancel their pending request");
		commentVersion = v.m_commentVersion;
	};
	switch (step) {
	case 0:
		check(v.m_commentScan.nextPage() == 0 &&
		          v.CommentsList().Items().Size() == 0 && !v.commentsOpen(),
		      "comments loaded before opening the panel");
		v.setCommentsOpen(true);
		break;
	case 1: {
		if (!v.m_commentScan.done()) {
			return;
		}
		const auto& rows = v.m_commentScan.rows();
		check(rows.size() == 3 && rows[0].page == 0 && rows[1].page == 1 &&
		          rows[2].page == 2 && rows[1].subtype == "Highlight" &&
		          v.CommentsCount().Text() == L"3" &&
		          v.CommentsStatus().Text().empty(),
		      "document-wide comment count or order");
		const auto row = v.CommentsList().Items().GetAt(0).as<ListViewItem>();
		check(std::wstring_view(Automation::AutomationProperties::GetName(row))
		              .find(L"Abhilash \u03a9 Notes "
		                    L"\u03a9\U0001f600\nsecond line") !=
		          std::wstring_view::npos,
		      "comments lost Unicode author or text");
		check(v.CommentsList().IsItemClickEnabled(),
		      "comment rows do not invoke navigation");
		if (!invoke(v.CommentsList().Items().GetAt(2).as<ListViewItem>())) {
			check(ticks < 30, "comment row has no Invoke");
			return;
		}
		break;
	}
	case 2:
		check(v.m_current == 2, "comment row did not navigate to its page");
		v.CommentsList().Focus(FocusState::Programmatic);
		if (!v.commentsHasFocus()) {
			return;
		}
		v.toggleSidebar();
		check(!v.commentsOpen() && !v.commentsHasFocus(),
		      "closed comments kept focus");
		v.toggleSidebar();
		check(v.commentsOpen(), "F4 forgot comments");
		v.setThumbnailsOpen(true);
		check(!v.commentsOpen(), "thumbnails left comments open");
		v.setCommentsOpen(true);
		v.setOutlineOpen(true);
		check(!v.commentsOpen(), "outline left comments open");
		v.setCommentsOpen(true);
		v.setAttachmentsOpen(true);
		check(!v.commentsOpen(), "attachments left comments open");
		v.setCommentsOpen(true);
		check(!v.thumbnailsOpen() && !v.outlineOpen() && !v.attachmentsOpen(),
		      "comments sidebar exclusivity");
		pause(false);
		break;
	case 3:
	case 5:
		check(v.m_commentScan.nextPage() == 0 &&
		          v.m_commentVersion == commentVersion,
		      "hidden comments accepted stale progress");
		if (step == 3) {
			v.Visibility(Visibility::Visible);
		} else {
			v.Rail().Visibility(Visibility::Visible);
		}
		break;
	case 4:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.CommentsList().Items().Size() == 3,
		      "viewer visibility did not resume comments");
		pause(true);
		break;
	case 6:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(v.CommentsList().Items().Size() == 3,
		      "rail visibility did not resume comments");
		v.m_commentScan.reset(3);
		v.CommentsList().Items().Clear();
		v.m_commentScan.fail();
		v.showComments();
		check(v.CommentsRetry().Visibility() == Visibility::Visible &&
		          v.CommentsStatus().Text() ==
		              L"Couldn't load comments on page 1.",
		      "comments failure has no visible retry");
		check(invoke(v.CommentsRetry()), "comments Retry has no Invoke");
		break;
	case 7:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(!v.m_commentScan.failed() &&
		          v.CommentsRetry().Visibility() == Visibility::Collapsed,
		      "comments retry failed");
		v.m_commentScan.reset(1);
		v.CommentsList().Items().Clear();
		check(v.m_commentScan.append({0, {}, true}));
		v.showComments();
		check(v.CommentsStatus().Text() ==
		          L"Some comments could not be listed.",
		      "comments limitation has no visible status");
		owner.openDocument(engine, L"tests/fixtures/pages.pdf", 1);
		break;
	case 8:
		check(!v.commentsOpen() && v.m_commentScan.nextPage() == 0,
		      "comments state leaked between tabs");
		v.setCommentsOpen(true);
		break;
	case 9:
		if (!v.m_commentScan.done()) {
			return;
		}
		check(std::wstring_view(v.CommentsStatus().Text())
		              .starts_with(L"No comments yet.") &&
		          v.CommentsList().Items().Size() == 0,
		      "empty document comments state");
		break;
	case 10: {
		auto release = std::make_shared<std::promise<void>>();
		auto wait = release->get_future().share();
		engine.post([wait] { wait.wait(); });
		v.m_commentScan.reset(3);
		v.showComments();
		const bool pending = v.m_commentsPending;
		first.copy_from(&v);
		owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
		release->set_value();
		check(pending, "closing comments had no queued request");
		break;
	}
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
