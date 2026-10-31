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
void TabsTest::printTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& viewer = *get_self<ViewerPage>(current);
	if ((step != 6 && !ready(viewer)) ||
	    (owner.m_printing && step != 3 && step != 6)) {
		return;
	}
	switch (step) {
	case 0: {
		check(owner.PrintItem().IsEnabled(), "print disabled with document");
		owner.PickerError().IsOpen(false);
		owner.m_shellFault = [](std::wstring_view) {
			throw hresult_error(E_FAIL, L"forced shell failure");
		};
		bool escaped = false;
		try {
			owner.onPrint({}, {});
		} catch (...) {
			escaped = true;
		}
		owner.m_shellFault = {};
		check(!escaped && owner.PickerError().IsOpen() && !owner.m_printing &&
		          owner.PrintItem().IsEnabled(),
		      "window-handle failure escaped without a print notice");
		const auto selected = owner.Tabs().SelectedItem();
		const auto position = owner.Tabs().SelectedIndex();
		const auto count = owner.Tabs().TabItems().Size();
		owner.m_shellFault = [](std::wstring_view) {
			throw std::runtime_error("forced tab failure");
		};
		owner.PickerError().IsOpen(false);
		owner.moveTab(position == 0 ? 1 : 0);
		check(owner.PickerError().IsOpen() && !owner.m_movingTab &&
		          owner.Tabs().SelectedItem() == selected &&
		          owner.Tabs().SelectedIndex() == position &&
		          owner.Tabs().TabItems().Size() == count,
		      "failed move lost the tab, selection or notice");
		owner.PickerError().IsOpen(false);
		owner.beginTabDrag();
		owner.endTabDrag();
		check(owner.PickerError().IsOpen() && !owner.m_dragSelection &&
		          owner.Tabs().SelectedItem() == selected,
		      "failed drag retained its state or escaped without notice");
		owner.m_shellFault = {};
		owner.moveTab(position == 0 ? 1 : 0);
		owner.moveTab(position);
		check(owner.Tabs().SelectedItem() == selected &&
		          owner.Tabs().SelectedIndex() == position,
		      "tab move did not recover after failure");
		owner.PickerError().IsOpen(false);
		app.reportUiException(L"forced fallback failure");
		check(owner.PickerError().IsOpen() &&
		          owner.PickerError().Title() == L"Couldn't finish that action",
		      "UI exception fallback did not show a notice");
		owner.PickerError().IsOpen(false);
		int calls = 0;
		bool busyNoticeSeen = false;
		viewer.m_printDialog = [&](void* handle, int count, int page,
		                           const std::wstring& title) {
			HWND expected{};
			check_hresult(
			    owner.get_strong().as<::IWindowNative>()->get_WindowHandle(
			        &expected));
			check(handle == expected && count == 1 && page == 0 &&
			          title == L"print.pdf",
			      "print dialog active-document snapshot");
			++calls;
			owner.onPrint({}, {});
			busyNoticeSeen =
			    owner.PickerError().IsOpen() &&
			    owner.PickerError().Title() == L"Print already in progress" &&
			    owner.m_printing;
			return pdf::PrintDialogResult{};
		};
		check(owner.routeKey('P', helpers::SHORTCUT_CTRL),
		      "fixed print shortcut did not dispatch");
		viewer.m_printDialog = {};
		check(calls == 1 && busyNoticeSeen && !owner.m_printing &&
		          owner.PrintItem().IsEnabled(),
		      "repeated Print had no notice or changed its busy state");
		owner.PickerError().IsOpen(false);
		owner.selectTabPosition(0);
		break;
	}
	case 1:
		check(viewer.m_current == 1, "print current page setup");
		viewer.m_printDialog = [](void*, int count, int page,
		                          const std::wstring& title) {
			if (count != 3 || page != 1 || title != L"pages.pdf") {
				throw std::runtime_error("print switched tab");
			}
			return pdf::PrintDialogResult{{}, true};
		};
		owner.onPrint({}, {});
		check(!owner.m_printing && viewer.Message().IsOpen(),
		      "print dialog failure");
		check(viewer.Scroller().Visibility() == Visibility::Visible &&
		          viewer.Rail().Visibility() == Visibility::Visible &&
		          viewer.Message().IsClosable(),
		      "print failure hid the document");
		viewer.Message().IsOpen(false);
		viewer.m_printDialog = [](void*, int, int, const std::wstring&) {
			pdf::PrintJob job;
			job.device =
			    std::shared_ptr<void>(reinterpret_cast<void*>(1), [](void*) {});
			job.pages = {
			    -1}; // fails before any GDI call; no physical printer job
			return pdf::PrintDialogResult{std::move(job), false};
		};
		owner.onPrint({}, {});
		check(owner.m_printing && !owner.PrintItem().IsEnabled(),
		      "print job busy state");
		break;
	case 2:
		check(viewer.Message().IsOpen() && owner.PrintItem().IsEnabled(),
		      "print job failure/reset");
		check(viewer.Scroller().Visibility() == Visibility::Visible,
		      "spool failure hid the document");
		viewer.Message().IsOpen(false);
		viewer.openLink("");
		check(viewer.Message().IsOpen() && viewer.Message().IsClosable() &&
		          viewer.Scroller().Visibility() == Visibility::Visible,
		      "invalid browser link hid the document");
		viewer.Message().IsOpen(false);
		viewer.m_printDialog = {};
		viewer.m_printUi = [](auto) {};
		owner.onPrint({}, {});
		check(owner.m_printing && viewer.m_printController,
		      "modern print did not retain its document");
		{
			const auto controller = viewer.m_printController;
			owner.PickerError().IsOpen(false);
			owner.onPrint({}, {});
			check(controller == viewer.m_printController &&
			          owner.PickerError().IsOpen() &&
			          owner.PickerError().Title() ==
			              L"Print already in progress",
			      "modern print accepted a second dialog");
			owner.PickerError().IsOpen(false);
			controller->shown(true);
			check(!controller->m_finished && owner.m_printing &&
			          !viewer.Message().IsOpen(),
			      "opened print UI was treated as a failure");
			controller->shown(false);
		}
		check(!owner.m_printing && viewer.Message().IsOpen() &&
		          viewer.Scroller().Visibility() == Visibility::Visible,
		      "modern print failure hid or disabled the document");
		viewer.Message().IsOpen(false);
		owner.onPrint({}, {});
		viewer.m_printController->finish(pdf::PrintResult::Completed);
		check(owner.m_printing && viewer.m_printController,
		      "submitted print discarded the unfinished writer");
		break;
	case 3:
		check(owner.m_printing && viewer.m_printController,
		      "writer guard ended before completion");
		viewer.m_printController->m_source->jobFinished();
		break;
	case 4: {
		check(!viewer.m_printController && !viewer.Message().IsOpen(),
		      "finished writer kept the reader busy");
		int fallbacks = 0;
		viewer.m_printDialog = [&fallbacks](void*, int, int,
		                                    const std::wstring&) {
			++fallbacks;
			return pdf::PrintDialogResult{};
		};
		owner.onPrint({}, {});
		{
			const auto controller = viewer.m_printController;
			controller->finish(std::nullopt);
		}
		check(fallbacks == 1 && !owner.m_printing,
		      "unavailable modern UI did not fall back once");
		viewer.m_printDialog = {};
		owner.onPrint({}, {});
		const auto controller = viewer.m_printController;
		owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
		check(!owner.m_printing && controller->m_finished &&
		          controller->m_source->MakePage(1, 300, 400) ==
		              HRESULT_FROM_WIN32(ERROR_CANCELLED),
		      "closed tab retained its print job");
		break;
	}
	case 5:
		try {
			using Windows::Graphics::Printing::PrintManager;
			HWND handle{};
			check_hresult(
			    owner.get_strong().as<::IWindowNative>()->get_WindowHandle(
			        &handle));
			auto interop =
			    get_activation_factory<PrintManager, IPrintManagerInterop>();
			PrintManager manager{nullptr};
			check_hresult(interop->GetForWindow(handle, guid_of<PrintManager>(),
			                                    put_abi(manager)));
			diagnostics::write(diagnostics::Level::Info,
			                   "Modern print window interop is available");
		} catch (const hresult_error& error) {
			diagnostics::write(diagnostics::Level::Warning,
			                   "Modern print window interop unavailable: " +
			                       to_string(error.message()));
		}
		if (!diagnostics::environmentEnabled(L"SablePrintUiTest")) {
			step = 7;
			return;
		}
		viewer.m_printDialog = {};
		viewer.m_printUi = {};
		owner.onPrint({}, {});
		printPreviewController = viewer.m_printController;
		check(static_cast<bool>(printPreviewController),
		      "modern print UI rejected the live preview request");
		break;
	case 6:
		if (printPreviewController->m_source->m_previewFrames == 0) {
			bool target = false;
			{
				std::lock_guard lock(printPreviewController->m_source->m_mutex);
				target = static_cast<bool>(
				    printPreviewController->m_source->m_preview);
			}
			check(ticks < 60 && !printPreviewController->m_finished,
			      "system preview unavailable: task " +
			          std::to_string(printPreviewController->m_created) +
			          ", target " + std::to_string(target));
			return;
		}
		diagnostics::write(diagnostics::Level::Info,
		                   "System print preview received a DXGI page");
		printPreviewController->cancel();
		break;
	case 7:
		while (!owner.m_tabs.empty()) {
			owner.closeTab(owner.m_tabs.back().tab);
		}
		check(!owner.PrintItem().IsEnabled(), "print enabled on home");
		check(owner.Tabs().Visibility() == Visibility::Collapsed &&
		          owner.Nav().Visibility() == Visibility::Visible &&
		          owner.Documents().Visibility() == Visibility::Collapsed,
		      "last-tab home state");
		owner.onPrint({}, {});
		check(!owner.m_printing, "print without document");
		status.Text(L"PASS: print menu/accelerator, active tab/current "
		            L"page, modern lifecycle/fallback, cancel and home");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}

void TabsTest::saveCopyTick(MainWindow& owner) {
	if (ticks < 3 || owner.m_saveCopyCancel || owner.m_notesSaving) {
		return;
	}
	if (step == 12) {
		check(first && first->m_closed, "closed save retained its viewer");
		check(!std::filesystem::exists(attachmentTarget),
		      "closed tab accepted a late picker result");
		status.Text(L"PASS: PDF copy, cancel, source protection, errors "
		            L"and late picker guards");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_noteBusy || v.m_noteSaving) {
		return;
	}
	const auto choose = [&](const std::wstring& path) {
		owner.m_copyPicker = [this, path](hstring name) {
			attachmentName = name;
			++attachmentPicks;
			return attachmentChoice(hstring(path), attachmentGate);
		};
	};
	const auto chooseNotes = [&](const std::wstring& path) {
		owner.m_notesPicker = [this, path](hstring name) {
			attachmentName = name;
			++attachmentPicks;
			return attachmentChoice(hstring(path), {});
		};
	};
	const auto bytes = [](const std::filesystem::path& path) {
		std::ifstream in(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), {});
	};
	switch (step) {
	case 0:
		attachmentTarget = settingsPath;
		attachmentTarget.replace_extension(L".pdf");
		choose(L"");
		v.beginNotePlacement();
		check(v.notePlacing(), "save test didn't arm a note");
		owner.onSaveCopy(nullptr, nullptr);
		break;
	case 1:
		check(attachmentPicks == 1 && attachmentName == L"pages copy.pdf" &&
		          !std::filesystem::exists(attachmentTarget) &&
		          !v.Message().IsOpen() && !v.notePlacing() &&
		          owner.SaveCopyItem().IsEnabled(),
		      "cancel changed document state or suggested name");
		v.rotateView(1);
		v.zoomIn();
		choose(attachmentTarget.wstring());
		owner.onSaveCopy(nullptr, nullptr);
		owner.PickerError().IsOpen(false);
		owner.onSaveCopy(nullptr, nullptr);
		check(attachmentPicks == 2 && owner.m_saveCopyCancel &&
		          !owner.m_saveCopyCancel->load() &&
		          owner.PickerError().IsOpen() &&
		          owner.PickerError().Title() == L"Save already in progress",
		      "duplicate save opened a picker, canceled the save or had no "
		      "notice");
		owner.PickerError().IsOpen(false);
		break;
	case 2:
		check(bytes(attachmentTarget) == bytes(v.m_path),
		      "saving applied view changes to the PDF");
		owner.PickerError().IsOpen(false);
		owner.m_notesSaving = true;
		owner.onSaveCopy(nullptr, nullptr);
		check(owner.PickerError().IsOpen() && attachmentPicks == 2 &&
		          !owner.m_saveCopyCancel && owner.m_notesSaving &&
		          owner.PickerError().Title() == L"Save already in progress",
		      "a save in flight was refused without a notice");
		owner.m_notesSaving = false;
		for (auto* busy : {&owner.m_notesSaving, &v.m_noteSaving}) {
			*busy = true;
			owner.PickerError().IsOpen(false);
			const auto refused = owner.saveNotes(current);
			check(refused.Status() ==
			              Windows::Foundation::AsyncStatus::Completed &&
			          !refused.GetResults() && owner.PickerError().IsOpen() &&
			          owner.PickerError().Title() ==
			              L"Save already in progress",
			      "repeated note save was refused without a notice");
			*busy = false;
		}
		owner.PickerError().IsOpen(false);
		choose(std::filesystem::absolute(v.m_path).wstring());
		owner.onSaveCopy(nullptr, nullptr);
		break;
	case 3:
		check(v.Message().IsOpen() &&
		          std::wstring_view(v.Message().Message())
		                  .find(L"Choose a different") == 0 &&
		          bytes(attachmentTarget) == bytes(v.m_path),
		      "source protection missing or previous copy changed");
		v.Message().IsOpen(false);
		choose((attachmentTarget / L"missing" / L"copy.pdf").wstring());
		owner.onSaveCopy(nullptr, nullptr);
		break;
	case 4:
		check(v.Message().IsOpen() &&
		          std::wstring_view(v.Message().Message())
		                  .find(L"Couldn't save") == 0 &&
		          bytes(attachmentTarget) == bytes(v.m_path),
		      "failed copy has no notice or changed existing output");
		v.Message().IsOpen(false);
		owner.m_copyPicker =
		    [](hstring) -> Windows::Foundation::IAsyncOperation<hstring> {
			throw hresult_access_denied();
		};
		owner.onSaveCopy(nullptr, nullptr);
		break;
	case 5:
		check(v.Message().IsOpen(), "picker failure has no notice");
		v.Message().IsOpen(false);
		v.submitNote(0, u"Copy retains notes", {});
		owner.onSaveCopy(nullptr, nullptr);
		check(!owner.m_saveCopyCancel && v.m_noteBusy && v.Message().IsOpen(),
		      "copy ran before the pending note finished");
		v.Message().IsOpen(false);
		break;
	case 6:
		check(v.m_notesDirty, "copy test note did not become dirty");
		chooseNotes(L"");
		v.beginNotePlacement(ViewerPage::PlacementTool::Arrow);
		check(v.notePlacing(), "edited-copy save didn't arm an arrow");
		v.Scroller().Focus(FocusState::Programmatic);
		check(owner.routeKey('S', helpers::SHORTCUT_CTRL),
		      "Ctrl-S did not reach edited-copy saving");
		break;
	case 7:
		check(v.m_notesDirty && !v.notePlacing() && !v.m_noteSaving &&
		          !owner.m_saveCopyCancel && attachmentPicks == 5 &&
		          attachmentName == L"pages notes.pdf" &&
		          bytes(attachmentTarget) == bytes(v.m_path),
		      "dirty copy cancel lost notes or opened another picker");
		chooseNotes((attachmentTarget / L"missing" / L"notes.pdf").wstring());
		owner.onSaveCopy(nullptr, nullptr);
		break;
	case 8:
		check(v.m_notesDirty && !v.m_noteSaving && attachmentPicks == 6 &&
		          v.Message().IsOpen() &&
		          v.Message().Severity() == InfoBarSeverity::Error &&
		          bytes(attachmentTarget) == bytes(v.m_path),
		      "dirty copy failure lost notes or changed prior output");
		v.Message().IsOpen(false);
		chooseNotes(attachmentTarget.wstring());
		owner.onSaveCopy(nullptr, nullptr);
		break;
	case 9:
		check(!v.m_notesDirty && !v.m_noteSaving && attachmentPicks == 7 &&
		          !owner.m_saveCopyCancel && v.Message().IsOpen() &&
		          v.Message().Severity() == InfoBarSeverity::Success,
		      "dirty copy success kept the wrong save state");
		v.showNotice(L"Couldn't save this test copy.");
		check(v.Message().Severity() == InfoBarSeverity::Error,
		      "an error kept the previous save success style");
		v.Message().IsOpen(false);
		check(engine
		          .run([path = attachmentTarget] {
			          const auto saved = pdf::openFile(path);
			          const auto notes = saved.document
			                                 ? saved.document->annotations(0)
			                                 : std::nullopt;
			          return notes && notes->entries.size() == 1 &&
			                 notes->entries[0].contents == "Copy retains notes";
		          })
		          .get(),
		      "Ctrl-S saved a copy without the unsaved note");
		first.copy_from(&v);
		std::filesystem::remove(attachmentTarget);
		attachmentGate = std::make_shared<winrt::handle>(
		    CreateEventW(nullptr, TRUE, FALSE, nullptr));
		check(static_cast<bool>(*attachmentGate));
		choose(attachmentTarget.wstring());
		owner.PickerError().IsOpen(false);
		owner.onSaveCopy(nullptr, nullptr);
		owner.openDocument(engine, L"tests/fixtures/links.pdf", 1);
		SetEvent(attachmentGate->get());
		break;
	case 10:
		check(!std::filesystem::exists(attachmentTarget),
		      "tab switch accepted a late picker result");
		check(owner.PickerError().IsOpen() &&
		          owner.PickerError().Title() == L"Copy not saved",
		      "a tab switch dropped the chosen copy without a notice");
		owner.PickerError().IsOpen(false);
		owner.Tabs().SelectedIndex(0);
		break;
	case 11:
		ResetEvent(attachmentGate->get());
		choose(attachmentTarget.wstring());
		owner.onSaveCopy(nullptr, nullptr);
		owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
		SetEvent(attachmentGate->get());
		break;
	}
	++step;
	ticks = 0;
}

void TabsTest::attachmentSaveTick(MainWindow& owner) {
	if (step == 8) {
		check(first && first->m_closed && !first->m_attachmentSaving,
		      "closed attachment save stayed active");
		check(!std::filesystem::exists(attachmentTarget),
		      "closed tab saved a late picker result");
		status.Text(L"PASS: attachment save, cancel, read/write errors "
		            L"and closed picker guards");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_attachmentsPending ||
	    (v.m_attachmentSaving && step != 7)) {
		return;
	}
	const auto choose = [&](const std::wstring& path) {
		v.m_attachmentPicker = [this, path](hstring name) {
			attachmentName = name;
			++attachmentPicks;
			return attachmentChoice(hstring(path), attachmentGate);
		};
	};
	const auto save = v.FindName(L"AttachmentSaveButton").try_as<Button>();
	const auto saveSelected = [&] {
		check(save && save.IsEnabled(), "attachment Save disabled");
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(save)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	switch (step) {
	case 0:
		attachmentTarget = settingsPath;
		attachmentTarget.replace_extension(L".bin");
		v.setAttachmentsOpen(true);
		break;
	case 1:
		if (!v.m_attachmentsBuilt) {
			return;
		}
		check(!v.AttachmentList().IsItemClickEnabled(),
		      "selecting an attachment still invokes Save As");
		check(save && !save.IsEnabled(), "Save enabled without a selection");
		{
			const auto row =
			    v.AttachmentList().Items().GetAt(0).as<ListViewItem>();
			choose(L"");
			v.AttachmentList().SelectedItem(row);
			check(attachmentPicks == 0 &&
			          v.AttachmentList().SelectedItem() == row,
			      "selecting an attachment opened its picker");
			check(save.IsEnabled(), "selection did not enable Save");
			saveSelected();
		}
		break;
	case 2: {
		check(attachmentPicks == 1,
		      "explicit attachment Save did not open its picker");
		check(!std::filesystem::exists(attachmentTarget) &&
		          !v.Message().IsOpen(),
		      "cancel changed attachment state");
		check(attachmentName == L"notes \u03a9\U0001f600.txt",
		      "picker lost Unicode filename");
		v.AttachmentList().SelectedIndex(-1);
		check(!save.IsEnabled(), "Save enabled after clearing selection");
		const auto row = v.AttachmentList().Items().GetAt(0);
		v.AttachmentList().Items().RemoveAt(0);
		v.AttachmentList().Items().Append(row);
		v.AttachmentList().SelectedItem(row);
		choose(attachmentTarget.wstring());
		saveSelected();
		check(!save.IsEnabled(), "Save enabled during attachment extraction");
		v.saveAttachment(1);
		check(attachmentPicks == 2, "opened duplicate attachment picker");
		break;
	}
	case 3: {
		std::ifstream in(attachmentTarget, std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(in)), {});
		check(bytes == std::string("hello\0\xff\n", 8),
		      "attachment bytes changed while saving");
		choose(attachmentTarget.wstring());
		v.AttachmentList().SelectedIndex(0);
		saveSelected();
		check(attachmentName == L"empty.bin", "picker kept a PDF path");
		break;
	}
	case 4:
		check(std::filesystem::file_size(attachmentTarget) == 0,
		      "empty attachment was not saved");
		v.AttachmentList().SelectedIndex(1);
		saveSelected();
		break;
	case 5:
		check(v.Message().IsOpen() &&
		          std::wstring_view(v.Message().Message())
		                  .find(L"Couldn't read") == 0 &&
		          std::filesystem::file_size(attachmentTarget) == 0,
		      "damaged attachment replaced a saved file");
		v.Message().IsOpen(false);
		choose(std::filesystem::absolute(v.m_path).wstring());
		v.AttachmentList().SelectedIndex(3);
		saveSelected();
		break;
	case 6:
		check(v.Message().IsOpen() && std::wstring_view(v.Message().Message())
		                                      .find(L"Couldn't save") == 0,
		      "source PDF overwrite has no error");
		v.Message().IsOpen(false);
		std::filesystem::remove(attachmentTarget);
		attachmentGate = std::make_shared<winrt::handle>(
		    CreateEventW(nullptr, TRUE, FALSE, nullptr));
		check(static_cast<bool>(*attachmentGate));
		choose(attachmentTarget.wstring());
		saveSelected();
		check(v.m_attachmentSaving && !save.IsEnabled(),
		      "picker didn't suspend saving");
		v.AttachmentList().SelectedIndex(0);
		check(!save.IsEnabled() &&
		          attachmentName == L"notes \u03a9\U0001f600.txt",
		      "busy selection changed the chosen attachment or enabled Save");
		break;
	case 7:
		first.copy_from(&v);
		owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
		SetEvent(attachmentGate->get());
		break;
	}
	++step;
	ticks = 0;
}

void TabsTest::attachmentsTick(MainWindow& owner) {
	if (step == 6) {
		check(first && first->m_closed && !first->m_attachments &&
		          !first->m_attachmentsPending &&
		          first->AttachmentList().Items().Size() == 0,
		      "closed attachment panel accepted a late result");
		// closing the focused tab hands the keys to the next document
		const auto next = owner.Viewer();
		const auto focus =
		    Input::FocusManager::GetFocusedElement(owner.RootGrid().XamlRoot());
		if (!next || focus != get_self<ViewerPage>(next)->Scroller()) {
			check(ticks < 30, "closing a tab left the keys nowhere");
			return;
		}
		status.Text(L"PASS: attachment names, cache, sidebar switching, "
		            L"empty/retry, close guards and Recent");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_attachmentsPending) {
		return;
	}
	switch (step) {
	case 0:
		check(!v.m_attachments, "attachments loaded before sidebar open");
		check(v.AttachmentButton().IsEnabled(),
		      "Attachments is disabled for a PDF that has some");
		v.setAttachmentsOpen(true);
		check(v.attachmentsOpen() && !v.thumbnailsOpen() && !v.outlineOpen(),
		      "attachment sidebar exclusivity");
		break;
	case 1:
		if (!v.m_attachmentsBuilt) {
			return;
		}
		check(v.AttachmentList().Items().Size() == 4, "attachment list count");
		attachmentItem = v.AttachmentList().Items().GetAt(0).as<ListViewItem>();
		check(attachmentItem.Content().as<TextBlock>().Text() ==
		              L"notes \u03a9\U0001f600.txt" &&
		          Automation::AutomationProperties::GetName(attachmentItem) ==
		              L"notes \u03a9\U0001f600.txt",
		      "attachment Unicode name");
		v.AttachmentList().Focus(FocusState::Programmatic);
		if (!v.attachmentsHasFocus()) {
			return;
		}
		v.toggleSidebar();
		check(!v.attachmentsHasFocus(), "closed attachment panel kept focus");
		v.toggleSidebar();
		check(v.attachmentsOpen(), "F4 forgot attachments");
		v.zoomIn();
		break;
	case 2:
		check(v.AttachmentList().Items().GetAt(0) == attachmentItem,
		      "zoom rebuilt cached attachments");
		v.setOutlineOpen(true);
		check(!v.attachmentsOpen() && v.outlineOpen(),
		      "outline did not close attachments");
		v.setAttachmentsOpen(true);
		v.setThumbnailsOpen(true);
		check(v.thumbnailsOpen() && !v.attachmentsOpen(),
		      "thumbnails did not close attachments");
		owner.openDocument(engine, L"tests/fixtures/pages.pdf", 1);
		break;
	case 3: {
		check(!v.m_attachments && !v.attachmentsOpen(),
		      "attachment state leaked between tabs");
		check(!v.AttachmentButton().IsEnabled(),
		      "Attachments is offered for a PDF without any");
		// both files opened, so both are in Recent, newest first
		const auto& recent =
		    get_self<HomePage>(owner.Home())->m_recents.items();
		check(recent.size() == 2 && recent[0].path.filename() == L"pages.pdf" &&
		          recent[1].path.filename() == L"attachments.pdf",
		      "opened files missing from Recent");
		v.setAttachmentsOpen(true);
		break;
	}
	case 4:
		if (!v.m_attachmentsBuilt) {
			return;
		}
		check(v.AttachmentList().Items().Size() == 0 &&
		          v.AttachmentStatus().Text() == L"No attachments.",
		      "empty attachment state");
		check(v.AttachmentButton().IsEnabled(),
		      "an open empty attachment panel can't be closed");
		v.m_attachments.reset();
		v.m_attachmentsFailed = true;
		v.showAttachments();
		check(v.AttachmentRetry().Visibility() == Visibility::Visible,
		      "attachment load error has no retry");
		v.requestAttachments();
		break;
	case 5:
		if (!v.m_attachments) {
			return;
		}
		check(!v.m_attachmentsFailed &&
		          v.AttachmentRetry().Visibility() == Visibility::Collapsed,
		      "attachment retry did not recover");
		v.m_attachments.reset();
		v.requestAttachments();
		first.copy_from(&v);
		owner.closeTab(owner.Tabs().SelectedItem().as<TabViewItem>());
		break;
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
