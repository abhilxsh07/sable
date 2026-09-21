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
#include "MainWindow.xaml.h"
#include "ViewerPage.xaml.h"
#include "pdf/document.h"
#include "pdf/engine.h"
#include "helpers/attachment_file.h"
#include "helpers/logging.h"
#include <algorithm>
#include <memory>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
namespace {
std::filesystem::path itemPath(const Windows::Foundation::IInspectable& item) {
	const auto row = item.try_as<FrameworkElement>();
	return row ? std::filesystem::path(
	                 unbox_value_or<hstring>(row.Tag(), hstring{}).c_str())
	           : std::filesystem::path{};
}

hstring combineError(const pdf::CombineResult& result,
                     const std::vector<std::filesystem::path>& files) {
	const auto name = result.file < files.size()
	                      ? hstring(files[result.file].filename().wstring())
	                      : hstring(L"A file");
	switch (result.error) {
	case pdf::CombineError::Password:
		return name + L" needs a password, so it can't be combined.";
	case pdf::CombineError::Unreadable:
		return L"Couldn't read " + name + L". It may be damaged or missing.";
	case pdf::CombineError::TooLarge:
		return L"The combined PDF would be over 128 MiB, which is too large.";
	default:
		return L"Couldn't combine these files. Try again.";
	}
}
// a coroutine of its own, so its arguments outlive the click handler.
fire_and_forget addCombineFiles(Microsoft::UI::WindowId owner,
                                weak_ref<ListView> view,
                                std::function<void()> refresh) {
	try {
		Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(owner);
		picker.FileTypeFilter().Append(L".pdf");
		const auto files = co_await picker.PickMultipleFilesAsync();
		const auto box = view.get();
		if (!box) {
			co_return;
		}
		for (const auto& file : files) {
			TextBlock row;
			row.Text(hstring(std::filesystem::path(file.Path().c_str())
			                     .filename()
			                     .wstring()));
			row.Tag(box_value(file.Path()));
			ToolTipService::SetToolTip(row, box_value(file.Path()));
			box.Items().Append(row);
		}
		refresh();
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not add files to combine");
	}
}
} // namespace

// Combine files: the PDFs in a list I can reorder, joined into one new
// PDF saved through the Save a copy picker. The inputs are only read.
fire_and_forget MainWindow::combinePdfs() {
	const auto lifetime = get_strong();
	try {
		if (m_closing || !m_engine || m_combining || !RootGrid().XamlRoot()) {
			co_return;
		}
		if (const auto viewer = Viewer()) {
			get_self<ViewerPage>(viewer)->openCombine();
			co_return;
		}
		m_combining = true;
		ContentDialog dialog;
		dialog.XamlRoot(RootGrid().XamlRoot());
		dialog.Title(box_value(L"Combine files"));
		dialog.PrimaryButtonText(L"Combine and save…");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::Primary);
		dialog.IsPrimaryButtonEnabled(false);
		StackPanel fields;
		fields.Spacing(12);
		fields.MinWidth(380);
		TextBlock hint;
		hint.Text(L"Add PDFs and drag them into the order they should "
		          L"appear. The files themselves don't change.");
		hint.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(hint);
		ListView list;
		list.Height(240);
		list.SelectionMode(ListViewSelectionMode::Single);
		list.CanReorderItems(true);
		list.CanDragItems(true);
		list.AllowDrop(true);
		Automation::AutomationProperties::SetName(list, L"Files to combine");
		fields.Children().Append(list);
		StackPanel buttons;
		buttons.Orientation(Orientation::Horizontal);
		buttons.Spacing(4);
		Button add, remove, up, down;
		add.Content(box_value(L"Add files…"));
		remove.Content(box_value(L"Remove"));
		up.Content(box_value(L"Move up"));
		down.Content(box_value(L"Move down"));
		for (const auto& button : {add, remove, up, down}) {
			buttons.Children().Append(button);
		}
		fields.Children().Append(buttons);
		dialog.Content(fields);
		const auto items = list.Items();
		const std::function<void()> refresh =
		    [prompt = make_weak(dialog), view = make_weak(list),
		     drop = make_weak(remove), raise = make_weak(up),
		     lower = make_weak(down)] {
			    const auto box = view.get();
			    const auto shown = prompt.get();
			    const auto remove = drop.get();
			    const auto up = raise.get();
			    const auto down = lower.get();
			    if (!box || !shown || !remove || !up || !down) {
				    return;
			    }
			    const int index = box.SelectedIndex();
			    const int count = static_cast<int>(box.Items().Size());
			    remove.IsEnabled(index >= 0);
			    up.IsEnabled(index > 0);
			    down.IsEnabled(index >= 0 && index + 1 < count);
			    shown.IsPrimaryButtonEnabled(count >= 2);
		    };
		refresh();
		list.SelectionChanged([refresh](auto&&, auto&&) { refresh(); });
		list.DragItemsCompleted([refresh](auto&&, auto&&) { refresh(); });
		const auto move = [view = make_weak(list), refresh](int step) {
			const auto box = view.get();
			const int index = box ? box.SelectedIndex() : -1;
			const int target = index + step;
			if (index < 0 || target < 0 ||
			    target >= static_cast<int>(box.Items().Size())) {
				return;
			}
			const auto item = box.Items().GetAt(index);
			box.Items().RemoveAt(index);
			box.Items().InsertAt(target, item);
			box.SelectedIndex(target);
			refresh();
		};
		up.Click([move](auto&&, auto&&) { move(-1); });
		down.Click([move](auto&&, auto&&) { move(1); });
		remove.Click([view = make_weak(list), refresh](auto&&, auto&&) {
			if (const auto box = view.get(); box && box.SelectedIndex() >= 0) {
				box.Items().RemoveAt(box.SelectedIndex());
				refresh();
			}
		});
		add.Click([owner = AppWindow().Id(), view = make_weak(list),
		           refresh](auto&&, auto&&) {
			addCombineFiles(owner, view, refresh);
		});
		m_combineDialog = dialog;
		const auto choice = co_await dialog.ShowAsync();
		m_combineDialog = nullptr;
		std::vector<std::filesystem::path> files;
		for (const auto& item : items) {
			if (auto path = itemPath(item); !path.empty()) {
				files.push_back(std::move(path));
			}
		}
		if (m_closing || choice != ContentDialogResult::Primary ||
		    files.size() < 2) {
			m_combining = false;
			co_return;
		}
		m_combining = false;
		saveCombined(std::move(files));
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open Combine files");
		m_combining = false;
	}
}

fire_and_forget
MainWindow::saveCombined(std::vector<std::filesystem::path> files,
                         hstring name) {
	const auto lifetime = get_strong();
	if (m_closing || !m_engine || m_combining || files.size() < 2) {
		co_return;
	}
	m_combining = true;
	try {
		hstring path;
#ifdef SABLE_SURFACE_TEST
		if (m_notesPicker) {
			path = co_await m_notesPicker(name);
		} else
#endif
		{
			Microsoft::Windows::Storage::Pickers::FileSavePicker picker(
			    AppWindow().Id());
			picker.SuggestedFileName(name);
			picker.FileTypeChoices().Insert(
			    L"PDF document", single_threaded_vector<hstring>({L".pdf"}));
			picker.DefaultFileExtension(L".pdf");
			const auto chosen = co_await picker.PickSaveFileAsync();
			if (chosen) {
				path = chosen.Path();
			}
		}
		if (path.empty() || m_closing) {
			m_combining = false;
			co_return;
		}
		const std::filesystem::path destination(path.c_str());
		const auto report = [this](const hstring& message) {
			PickerError().Title(L"Couldn't combine the files");
			PickerError().Message(message);
			PickerError().IsOpen(true);
		};
		// the new file never replaces one of its own inputs.
		std::error_code error;
		for (const auto& file : files) {
			if (std::filesystem::equivalent(file, destination, error)) {
				m_combining = false;
				report(L"Choose a new file name; the combined PDF can't "
				       L"replace one of its inputs.");
				co_return;
			}
		}
		struct Reply {
			handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
			pdf::CombineResult result;
		};
		const auto reply = std::make_shared<Reply>();
		check_bool(static_cast<bool>(reply->ready));
		const apartment_context ui;
		const bool queued = m_engine->post([reply, files] {
			try {
				reply->result = pdf::combineFiles(files);
			} catch (...) {
				reply->result.error = pdf::CombineError::Failed;
			}
			SetEvent(reply->ready.get());
		});
		if (queued) {
			co_await resume_on_signal(reply->ready.get());
		}
		co_await ui;
		m_combining = false;
		if (m_closing) {
			co_return;
		}
		if (!queued || reply->result.error != pdf::CombineError::None) {
			report(combineError(reply->result, files));
			co_return;
		}
		// the writer wants a source to protect; every input was checked
		// above, so the first stands in for them.
		const auto saved =
		    helpers::saveFile(destination, files.front(), reply->result.bytes);
		if (saved != helpers::FileSaveResult::Saved) {
			if (saved != helpers::FileSaveResult::Canceled) {
				report(L"Couldn't save the combined PDF. Choose a writable "
				       L"folder.");
			}
			co_return;
		}
		openDocument(*m_engine, destination, std::nullopt);
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not combine PDFs");
		m_combining = false;
	}
}

// the draft belongs to the tab; the window only saves the result.
void ViewerPage::openCombine() {
	if (m_closed || !m_layout || !m_session || presenting() || m_noteDialog ||
	    m_passwordDialog || !XamlRoot()) {
		return;
	}
	if (organizing() || pageOperationBusy() || m_noteBusy || m_noteSaving) {
		showNotice(L"Finish the page or note operation before combining.");
		return;
	}
	if (!m_combineSetup) {
		m_combineSetup = true;
		const auto weak = get_weak();
		CombineName().Text(hstring(m_path.stem().wstring()) +
		                   L" (combined).pdf");
		CombineList().DragOver([weak](auto&&, const DragEventArgs& args) {
			using namespace Windows::ApplicationModel::DataTransfer;
			if (const auto self = weak.get();
			    self && !self->m_closed && !self->m_combineLoading &&
			    args.DataView().Contains(StandardDataFormats::StorageItems())) {
				args.AcceptedOperation(DataPackageOperation::Copy);
				args.Handled(true);
			}
		});
		CombineList().Drop([weak](auto&&, const DragEventArgs& args) {
			using namespace Windows::ApplicationModel::DataTransfer;
			if (args.DataView().Contains(StandardDataFormats::StorageItems())) {
				args.Handled(true);
				if (const auto self = weak.get()) {
					self->dropCombineFiles(args.DataView());
				}
			}
		});
		CombineList().DragItemsCompleted([weak](auto&&, auto&&) {
			if (const auto self = weak.get()) {
				self->reorderCombine();
			}
		});
		CombineAdd().Click([weak](auto&&, auto&&) {
			if (const auto self = weak.get()) {
				self->pickCombineFiles();
			}
		});
		CombineButton().Click([weak](auto&&, auto&&) {
			if (const auto self = weak.get();
			    self && self->m_combineRequest && !self->m_combineLoading) {
				auto name = self->CombineName().Text();
				const std::wstring value(name);
				if (value.empty() ||
				    value.find_first_of(L"<>:\"/\\|?*\r\n\t") !=
				        std::wstring::npos ||
				    value.back() == L'.' || value.back() == L' ') {
					self->showNotice(
					    L"Enter a file name without folder separators "
					    L"or reserved characters.");
					return;
				}
				if (_wcsicmp(std::filesystem::path(value).extension().c_str(),
				             L".pdf") != 0) {
					name = name + L".pdf";
				}
				std::vector<std::filesystem::path> files;
				for (const auto& input : self->m_combineInputs) {
					files.push_back(input.path);
				}
				self->m_combineRequest(std::move(files), name);
			}
		});
		addCombineFiles({m_path});
	}
	openPane(ToolPane::Combine);
	focusToolPane();
}

void ViewerPage::refreshCombine() {
	if (m_closed) {
		return;
	}
	const int selected = CombineList().SelectedIndex();
	CombineList().Items().Clear();
	std::uint64_t total = 0;
	bool ready = m_combineInputs.size() >= 2 && !m_combineLoading;
	for (std::size_t i = 0; i < m_combineInputs.size(); ++i) {
		const auto& input = m_combineInputs[i];
		total += static_cast<std::uint64_t>(input.pages);
		ready = ready && input.pages > 0;
		Grid row;
		row.Tag(box_value(static_cast<uint32_t>(i)));
		ColumnDefinition nameColumn, removeColumn;
		nameColumn.Width(GridLength{1, GridUnitType::Star});
		removeColumn.Width(GridLength{0, GridUnitType::Auto});
		row.ColumnDefinitions().Append(nameColumn);
		row.ColumnDefinitions().Append(removeColumn);
		TextBlock name;
		name.Text(hstring(input.path.filename().wstring()) + L"\n" +
		          (input.pages ? to_hstring(input.pages) +
		                             (input.pages == 1 ? L" page" : L" pages")
		                       : hstring{}) +
		          (input.note.empty() ? hstring{} : L" " + input.note));
		name.TextWrapping(TextWrapping::Wrap);
		ToolTipService::SetToolTip(name, box_value(input.path.wstring()));
		row.Children().Append(name);
		Button remove;
		remove.Content(box_value(L"Remove"));
		remove.IsEnabled(!m_combineLoading);
		Automation::AutomationProperties::SetName(
		    remove, L"Remove " + hstring(input.path.filename().wstring()));
		Grid::SetColumn(remove, 1);
		remove.Click([weak = get_weak(), i](auto&&, auto&&) {
			if (const auto self = weak.get();
			    self && !self->m_combineLoading &&
			    i < self->m_combineInputs.size()) {
				self->m_combineInputs.erase(self->m_combineInputs.begin() + i);
				self->refreshCombine();
			}
		});
		row.Children().Append(remove);
		CombineList().Items().Append(row);
	}
	if (selected >= 0 && selected < static_cast<int>(m_combineInputs.size())) {
		CombineList().SelectedIndex(selected);
	}
	CombineTotal().Text(to_hstring(total) +
	                    (total == 1 ? L" page total" : L" pages total"));
	CombineAdd().IsEnabled(!m_combineLoading);
	CombineList().IsEnabled(!m_combineLoading);
	CombineButton().IsEnabled(ready);
}

// the rows carry their old indices, including when a file appears twice.
void ViewerPage::reorderCombine() {
	if (m_closed || m_combineLoading) {
		return;
	}
	std::vector<CombineInput> ordered;
	std::vector<bool> seen(m_combineInputs.size());
	for (const auto& item : CombineList().Items()) {
		const auto index = unbox_value<uint32_t>(item.as<Grid>().Tag());
		if (index >= seen.size() || seen[index]) {
			refreshCombine();
			return;
		}
		seen[index] = true;
		ordered.push_back(m_combineInputs[index]);
	}
	if (ordered.size() == m_combineInputs.size()) {
		m_combineInputs = std::move(ordered);
	}
	refreshCombine();
}

bool ViewerPage::combineHasFocus() {
	if (m_closed || m_toolPane != ToolPane::Combine || !XamlRoot()) {
		return false;
	}
	auto source = Input::FocusManager::GetFocusedElement(XamlRoot())
	                  .try_as<DependencyObject>();
	while (source && source != CombineList()) {
		source = Media::VisualTreeHelper::GetParent(source);
	}
	return static_cast<bool>(source);
}

bool ViewerPage::combineKey(unsigned key, helpers::NavModifiers modifiers) {
	if (m_combineLoading || !modifiers.alt || modifiers.ctrl ||
	    modifiers.shift || modifiers.windows ||
	    (key != VK_UP && key != VK_DOWN) || !combineHasFocus()) {
		return false;
	}
	const int index = CombineList().SelectedIndex();
	const int target = index + (key == VK_UP ? -1 : 1);
	if (index >= 0 && target >= 0 &&
	    target < static_cast<int>(m_combineInputs.size())) {
		std::swap(m_combineInputs[index], m_combineInputs[target]);
		refreshCombine();
		CombineList().SelectedIndex(target);
		CombineList().ScrollIntoView(CombineList().SelectedItem());
		CombineList().UpdateLayout();
		if (const auto row =
		        CombineList().ContainerFromIndex(target).try_as<Control>()) {
			row.Focus(FocusState::Keyboard);
		}
	}
	return true;
}

fire_and_forget ViewerPage::dropCombineFiles(
    Windows::ApplicationModel::DataTransfer::DataPackageView data) {
	const auto lifetime = get_strong();
	try {
		if (m_closed || m_combineLoading) {
			co_return;
		}
		m_combineLoading = true;
		refreshCombine();
		const auto items = co_await data.GetStorageItemsAsync();
		std::vector<std::filesystem::path> files;
		for (const auto& item : items) {
			if (item.try_as<Windows::Storage::StorageFile>()) {
				std::filesystem::path path(item.Path().c_str());
				if (_wcsicmp(path.extension().c_str(), L".pdf") == 0) {
					files.push_back(std::move(path));
				}
			}
		}
		m_combineLoading = false;
		co_await addCombineFiles(std::move(files));
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not add dropped Combine inputs");
		if (!m_closed) {
			showNotice(L"Couldn't add those PDFs. Try Add files instead.");
		}
	}
	m_combineLoading = false;
	refreshCombine();
}

Windows::Foundation::IAsyncAction
ViewerPage::addCombineFiles(std::vector<std::filesystem::path> files) {
	const auto lifetime = get_strong();
	if (m_closed || !m_engine || m_combineLoading || files.empty()) {
		co_return;
	}
	m_combineLoading = true;
	refreshCombine();
	struct Reply {
		handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
		std::vector<CombineInput> inputs;
	};
	const auto reply = std::make_shared<Reply>();
	check_bool(static_cast<bool>(reply->ready));
	const apartment_context ui;
	const bool queued = m_engine->post([reply, files = std::move(files)] {
		try {
			for (const auto& path : files) {
				CombineInput input{path};
				const auto opened = pdf::openFile(path);
				if (opened.document) {
					input.pages = opened.document->pageCount();
					if (opened.document->canWrite({}) ==
					    pdf::WriteRefusal::Signed) {
						input.note = L"(signed PDF)";
					}
				} else {
					input.note =
					    opened.error == pdf::Error::PasswordRequired ||
					            opened.error == pdf::Error::WrongPassword
					        ? L"Needs a password"
					        : L"Couldn't read this PDF";
				}
				reply->inputs.push_back(std::move(input));
			}
		} catch (...) {
			diagnostics::caught(diagnostics::Level::Error,
			                    "Could not read Combine inputs");
		}
		SetEvent(reply->ready.get());
	});
	if (queued) {
		co_await resume_on_signal(reply->ready.get());
	}
	co_await ui;
	m_combineLoading = false;
	if (!m_closed) {
		for (auto& input : reply->inputs) {
			m_combineInputs.push_back(std::move(input));
		}
		refreshCombine();
		if (!queued || reply->inputs.empty()) {
			showNotice(L"Couldn't add those PDFs. Try again.");
		}
	}
}

fire_and_forget ViewerPage::pickCombineFiles() {
	const auto lifetime = get_strong();
	if (m_closed || m_combineLoading || !XamlRoot()) {
		co_return;
	}
	m_combineLoading = true;
	refreshCombine();
	try {
		Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(
		    XamlRoot().ContentIslandEnvironment().AppWindowId());
		picker.FileTypeFilter().Append(L".pdf");
		const auto chosen = co_await picker.PickMultipleFilesAsync();
		std::vector<std::filesystem::path> files;
		for (const auto& file : chosen) {
			files.emplace_back(file.Path().c_str());
		}
		m_combineLoading = false;
		co_await addCombineFiles(std::move(files));
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not choose Combine inputs");
		if (!m_closed) {
			showNotice(L"Couldn't choose those PDFs. Try again.");
		}
	}
	m_combineLoading = false;
	refreshCombine();
}
} // namespace winrt::Sable::implementation
