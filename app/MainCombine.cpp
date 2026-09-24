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
Windows::Foundation::IAsyncOperation<int>
writeCombinedPdf(std::filesystem::path destination,
                 std::filesystem::path source, std::vector<std::uint8_t> bytes,
                 std::shared_ptr<std::atomic_bool> cancel) {
	co_await resume_background();
	try {
		co_return static_cast<int>(helpers::saveFile(
		    destination, source, bytes, [cancel] { return cancel->load(); }));
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not write the combined PDF");
		co_return static_cast<int>(helpers::FileSaveResult::Failed);
	}
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
} // namespace

// Combine files: the PDFs in a list I can reorder, joined into one new
// PDF saved through the Save a copy picker. The inputs are only read.
fire_and_forget MainWindow::combinePdfs() {
	const auto lifetime = get_strong();
	try {
		if (m_closing || !m_engine || !RootGrid().XamlRoot()) {
			co_return;
		}
		if (const auto viewer = Viewer()) {
			get_self<ViewerPage>(viewer)->openCombine();
			co_return;
		}
		m_toolNext = L"combine";
		pickDocuments();
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not open Combine files");
		m_toolNext.clear();
	}
}

fire_and_forget
MainWindow::saveCombined(std::vector<std::filesystem::path> files,
                         hstring name) {
	const auto lifetime = get_strong();
	if (m_closing || !m_engine || m_combining || files.size() < 2) {
		co_return;
	}
	com_ptr<ViewerPage> source;
	try {
		m_combining = true;
		if (const auto viewer = Viewer()) {
			source = get_self<ViewerPage>(viewer)->get_strong();
		}
		const auto cancel = std::make_shared<std::atomic_bool>(false);
		if (source) {
			source->m_combineCancel = cancel;
			source->CombineProgress().IsIndeterminate(false);
			source->CombineProgress().Maximum(
			    static_cast<double>(files.size()));
			source->CombineProgress().Value(0);
			source->CombineProgressText().Text(
			    L"Preparing to combine files...");
			source->refreshCombine();
		}
		const auto finish =
		    std::shared_ptr<void>(nullptr, [lifetime, source](void*) {
			    lifetime->m_combining = false;
			    if (source) {
				    source->m_combineCancel.reset();
				    source->refreshCombine();
			    }
		    });
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
		if (path.empty() || m_closing || cancel->load()) {
			co_return;
		}
		const std::filesystem::path destination(path.c_str());
		const auto report = [this](const hstring& message) {
			PickerError().Severity(InfoBarSeverity::Error);
			PickerError().Title(L"Couldn't combine the files");
			PickerError().Message(message);
			PickerError().IsOpen(true);
		};
		// the new file never replaces one of its own inputs.
		std::error_code error;
		for (const auto& file : files) {
			if (std::filesystem::equivalent(file, destination, error)) {
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
		const auto progress = [weak = source ? source->get_weak()
		                                     : weak_ref<ViewerPage>{},
		                       queue = DispatcherQueue(),
		                       cancel](std::size_t done, std::size_t total) {
			queue.TryEnqueue([weak, cancel, done, total] {
				if (const auto self = weak.get();
				    self && !self->m_closed &&
				    self->m_combineCancel == cancel && !cancel->load() &&
				    !self->CombineProgress().IsIndeterminate()) {
					self->CombineProgress().Value(static_cast<double>(done));
					self->CombineProgressText().Text(
					    to_hstring(done) + L" of " + to_hstring(total) +
					    L" files");
				}
			});
		};
		const bool queued = m_engine->post([reply, files, cancel, progress] {
			try {
				reply->result = pdf::combineFiles(
				    files, 128 * 1024 * 1024,
				    [cancel] { return cancel->load(); }, progress);
			} catch (...) {
				reply->result.error = pdf::CombineError::Failed;
			}
			SetEvent(reply->ready.get());
		});
		if (queued) {
			co_await resume_on_signal(reply->ready.get());
		}
		co_await ui;
		if (m_closing) {
			co_return;
		}
		const auto stopped = [&] {
			if (source && !source->m_closed) {
				source->showNotice(L"Stopped combining. No file was saved.");
			}
		};
		if (reply->result.error == pdf::CombineError::Canceled) {
			stopped();
			co_return;
		}
		if (!queued || reply->result.error != pdf::CombineError::None) {
			report(combineError(reply->result, files));
			co_return;
		}
		// the writer wants a source to protect; every input was checked
		// above, so the first stands in for them.
		if (source && !source->m_closed) {
			source->CombineProgress().IsIndeterminate(true);
			source->CombineProgressText().Text(L"Saving the combined PDF...");
		}
		const auto saved = static_cast<helpers::FileSaveResult>(
		    co_await writeCombinedPdf(destination, files.front(),
		                              std::move(reply->result.bytes), cancel));
		if (m_closing) {
			co_return;
		}
		if (saved != helpers::FileSaveResult::Saved) {
			if (saved != helpers::FileSaveResult::Canceled) {
				report(L"Couldn't save the combined PDF. Choose a writable "
				       L"folder.");
			} else {
				stopped();
			}
			co_return;
		}
		openDocument(*m_engine, destination, std::nullopt);
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not combine PDFs");
		m_combining = false;
		if (source && !source->m_closed) {
			source->m_combineCancel.reset();
			source->refreshCombine();
			source->showNotice(L"Couldn't combine these files. Try again.");
		}
	}
}

// the draft belongs to the tab; the window only saves the result.
void ViewerPage::openCombine() {
	if (m_closed || !m_layout || !m_session || presenting() || m_noteDialog ||
	    m_passwordDialog || !XamlRoot()) {
		return;
	}
	if (m_combineCancel) {
		openPane(ToolPane::Combine);
		focusToolPane();
		return;
	}
	if (organizing() || pageOperationBusy() || m_noteBusy || m_noteSaving) {
		showNotice(L"Finish the page or note operation before combining.");
		return;
	}
	if (!m_combineSetup) {
		m_combineSetup = true;
		const auto weak = get_weak();
		CombineCancel().Click([weak](auto&&, auto&&) {
			if (const auto self = weak.get(); self && self->m_combineCancel) {
				self->m_combineCancel->store(true);
				self->CombineCancel().IsEnabled(false);
				self->CombineProgressText().Text(L"Stopping combine...");
			}
		});
		CombineName().Text(hstring(m_path.stem().wstring()) +
		                   L" (combined).pdf");
		CombineList().DragOver([weak](auto&&, const DragEventArgs& args) {
			using namespace Windows::ApplicationModel::DataTransfer;
			if (const auto self = weak.get();
			    self && !self->m_closed && !self->m_combineLoading &&
			    !self->m_combineCancel &&
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
			if (const auto self = weak.get(); self && self->m_combineRequest &&
			                                  !self->m_combineLoading &&
			                                  !self->m_combineCancel) {
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
		if (m_combineSeed.empty()) {
			addCombineFiles({m_path});
		}
	}
	if (!m_combineSeed.empty()) {
		m_combineInputs.clear();
		addCombineFiles(std::exchange(m_combineSeed, {}));
	}
	openPane(ToolPane::Combine);
	focusToolPane();
}

void ViewerPage::refreshCombine() {
	if (m_closed) {
		return;
	}
	const int selected = CombineList().SelectedIndex();
	const bool busy = m_combineLoading || m_combineCancel;
	CombineList().Items().Clear();
	std::uint64_t total = 0;
	bool ready = m_combineInputs.size() >= 2 && !busy;
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
		remove.IsEnabled(!busy);
		Automation::AutomationProperties::SetName(
		    remove, L"Remove " + hstring(input.path.filename().wstring()));
		Grid::SetColumn(remove, 1);
		remove.Click([weak = get_weak(), i](auto&&, auto&&) {
			if (const auto self = weak.get();
			    self && !self->m_combineLoading && !self->m_combineCancel &&
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
	CombineAdd().IsEnabled(!busy);
	CombineList().IsEnabled(!busy);
	CombineName().IsEnabled(!busy);
	CombineProgressRow().Visibility(m_combineCancel ? Visibility::Visible
	                                                : Visibility::Collapsed);
	CombineCancel().IsEnabled(m_combineCancel && !m_combineCancel->load());
	CombineButton().IsEnabled(ready);
}

// the rows carry their old indices, including when a file appears twice.
void ViewerPage::reorderCombine() {
	if (m_closed || m_combineLoading || m_combineCancel) {
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
	if (m_combineLoading || m_combineCancel || !modifiers.alt ||
	    modifiers.ctrl || modifiers.shift || modifiers.windows ||
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
		if (m_closed || m_combineLoading || m_combineCancel) {
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
	if (m_closed || !m_engine || m_combineLoading || m_combineCancel ||
	    files.empty()) {
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
	if (m_closed || m_combineLoading || m_combineCancel || !XamlRoot()) {
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
