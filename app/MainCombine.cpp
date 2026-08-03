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
#include "pdf/document.h"
#include "pdf/engine.h"
#include "helpers/attachment_file.h"
#include "helpers/logging.h"
#include <algorithm>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
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
		hstring path;
#ifdef SABLE_SURFACE_TEST
		if (m_notesPicker) {
			path = co_await m_notesPicker(L"Combined.pdf");
		} else
#endif
		{
			Microsoft::Windows::Storage::Pickers::FileSavePicker picker(
			    AppWindow().Id());
			picker.SuggestedFileName(L"Combined");
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
} // namespace winrt::Sable::implementation
