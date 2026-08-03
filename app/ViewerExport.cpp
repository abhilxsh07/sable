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
#include "pdf/session.h"
#include "helpers/attachment_file.h"
#include "helpers/export.h"
#include "helpers/logging.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using Windows::Foundation::IAsyncOperation;

namespace winrt::Sable::implementation {
namespace {
// waits on the engine for one callback without blocking the UI thread.
template <typename Result> struct Wait {
	handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
	Result result;
};

// WIC encodes through Windows.Graphics.Imaging into memory, so the file is
// written by the same replace-after-flush helper as Save a copy.
IAsyncOperation<Windows::Storage::Streams::IBuffer>
encodePage(pdf::Bitmap bitmap, bool jpeg, int dpi) {
	using namespace Windows::Graphics::Imaging;
	std::vector<std::uint8_t> tight(static_cast<std::size_t>(bitmap.width) *
	                                bitmap.height * 4);
	const auto row = static_cast<std::size_t>(bitmap.width) * 4;
	for (int y = 0; y < bitmap.height; ++y) {
		std::memcpy(tight.data() + row * y,
		            bitmap.pixels.data() +
		                static_cast<std::size_t>(bitmap.stride) * y,
		            row);
	}
	Windows::Storage::Streams::InMemoryRandomAccessStream stream;
	BitmapEncoder encoder{nullptr};
	if (jpeg) {
		BitmapPropertySet options;
		options.Insert(
		    L"ImageQuality",
		    BitmapTypedValue(box_value(0.9f),
		                     Windows::Foundation::PropertyType::Single));
		encoder = co_await BitmapEncoder::CreateAsync(
		    BitmapEncoder::JpegEncoderId(), stream, options);
	} else {
		encoder = co_await BitmapEncoder::CreateAsync(
		    BitmapEncoder::PngEncoderId(), stream);
	}
	encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore,
	                     static_cast<uint32_t>(bitmap.width),
	                     static_cast<uint32_t>(bitmap.height), dpi, dpi, tight);
	co_await encoder.FlushAsync();
	Windows::Storage::Streams::Buffer buffer(
	    static_cast<uint32_t>(stream.Size()));
	co_return co_await stream.GetInputStreamAt(0).ReadAsync(
	    buffer, buffer.Capacity(),
	    Windows::Storage::Streams::InputStreamOptions::None);
}
} // namespace

// Export pages: PNG or JPEG images of the unfiltered pages as they print,
// or the document's text. The open file is only read.
fire_and_forget ViewerPage::exportPages() {
	const auto lifetime = get_strong();
	try {
		if (m_closed || !m_layout || !m_session || m_exporting ||
		    m_noteDialog || m_passwordDialog || !XamlRoot()) {
			co_return;
		}
		const int count = static_cast<int>(m_sizes.size());
		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(L"Export pages"));
		dialog.PrimaryButtonText(L"Export…");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::Primary);
		StackPanel fields;
		fields.Spacing(12);
		fields.MinWidth(340);
		RadioButtons format;
		format.Header(box_value(L"Format"));
		for (const auto* name :
		     {L"PNG images", L"JPEG images", L"Text (.txt)"}) {
			format.Items().Append(box_value(name));
		}
		format.SelectedIndex(0);
		fields.Children().Append(format);
		RadioButtons which;
		which.Header(box_value(L"Pages"));
		which.Items().Append(box_value(L"All pages"));
		which.Items().Append(box_value(L"Range"));
		which.SelectedIndex(0);
		fields.Children().Append(which);
		TextBox range;
		range.PlaceholderText(L"1-3, 5");
		range.IsEnabled(false);
		Automation::AutomationProperties::SetName(range, L"Page range");
		fields.Children().Append(range);
		ComboBox resolution;
		resolution.Header(box_value(L"Resolution"));
		for (const auto* name : {L"72 dpi", L"150 dpi", L"300 dpi"}) {
			resolution.Items().Append(box_value(name));
		}
		resolution.SelectedIndex(1);
		fields.Children().Append(resolution);
		TextBlock error;
		error.TextWrapping(TextWrapping::Wrap);
		fields.Children().Append(error);
		dialog.Content(fields);
		which.SelectionChanged([pick = make_weak(which),
		                        field = make_weak(range)](auto&&, auto&&) {
			const auto choice = pick.get();
			if (const auto box = field.get(); choice && box) {
				box.IsEnabled(choice.SelectedIndex() == 1);
			}
		});
		format.SelectionChanged([pick = make_weak(format),
		                         dpi = make_weak(resolution)](auto&&, auto&&) {
			const auto choice = pick.get();
			if (const auto box = dpi.get(); choice && box) {
				box.IsEnabled(choice.SelectedIndex() != 2);
			}
		});
		dialog.PrimaryButtonClick(
		    [which = make_weak(which), field = make_weak(range),
		     message = make_weak(error),
		     count](auto&&, const ContentDialogButtonClickEventArgs& args) {
			    const auto pick = which.get();
			    const auto box = field.get();
			    if (pick && box && pick.SelectedIndex() == 1 &&
			        !helpers::parsePageList(box.Text(), count)) {
				    args.Cancel(true);
				    if (const auto text = message.get()) {
					    text.Text(L"Enter pages like 1-3, 5 between 1 and " +
					              to_hstring(count) + L".");
				    }
			    }
		    });
		m_noteDialog = dialog;
		const auto choice = co_await dialog.ShowAsync();
		if (m_noteDialog == dialog) {
			m_noteDialog = nullptr;
		}
		if (m_closed || choice != ContentDialogResult::Primary) {
			co_return;
		}
		std::vector<int> pages;
		if (which.SelectedIndex() == 1) {
			pages = helpers::parsePageList(range.Text(), count).value_or(pages);
		} else {
			for (int page = 0; page < count; ++page) {
				pages.push_back(page);
			}
		}
		const int kind = format.SelectedIndex();
		const int dpi = std::array{72, 150, 300}[static_cast<std::size_t>(
		    std::clamp(resolution.SelectedIndex(), 0, 2))];
		if (pages.empty() || !m_session) {
			co_return;
		}
		m_exporting = true;
		const auto finish = [this] { m_exporting = false; };
		const auto stem = m_path.stem().wstring();
		const auto owner = XamlRoot().ContentIslandEnvironment().AppWindowId();
		const apartment_context ui;
		if (kind == 2) {
			hstring path;
#ifdef SABLE_SURFACE_TEST
			if (m_exportPicker) {
				path = co_await m_exportPicker(false);
			} else
#endif
			{
				Microsoft::Windows::Storage::Pickers::FileSavePicker picker(
				    owner);
				picker.SuggestedFileName(hstring(stem));
				picker.FileTypeChoices().Insert(
				    L"Text file", single_threaded_vector<hstring>({L".txt"}));
				picker.DefaultFileExtension(L".txt");
				if (const auto chosen = co_await picker.PickSaveFileAsync()) {
					path = chosen.Path();
				}
			}
			if (path.empty() || m_closed || !m_session) {
				finish();
				co_return;
			}
			const auto wait = std::make_shared<Wait<pdf::DocumentTextResult>>();
			const bool queued = m_session->documentText(
			    pages, [wait](pdf::DocumentTextResult result) {
				    wait->result = std::move(result);
				    SetEvent(wait->ready.get());
			    });
			if (queued) {
				co_await resume_on_signal(wait->ready.get());
			}
			co_await ui;
			finish();
			const std::filesystem::path destination(path.c_str());
			if (m_closed || wait->result.canceled) {
				co_return;
			}
			if (!queued || !wait->result.text) {
				showNotice(L"Couldn't export the text. The PDF may not allow "
				           L"copying.");
			} else if (destination == m_path ||
			           !helpers::saveFile(destination, *wait->result.text)) {
				showNotice(L"Couldn't save the text. Choose a writable "
				           L"folder and a new file name.");
			} else {
				showNotice(L"Exported the text to " +
				               hstring(destination.filename().wstring()) + L".",
				           InfoBarSeverity::Success);
			}
			co_return;
		}
		hstring folder;
#ifdef SABLE_SURFACE_TEST
		if (m_exportPicker) {
			folder = co_await m_exportPicker(true);
		} else
#endif
		{
			Microsoft::Windows::Storage::Pickers::FolderPicker picker(owner);
			if (const auto chosen = co_await picker.PickSingleFolderAsync()) {
				folder = chosen.Path();
			}
		}
		if (folder.empty() || m_closed) {
			finish();
			co_return;
		}
		const bool jpeg = kind == 1;
		int done = 0;
		for (const int page : pages) {
			if (m_closed || !m_session) {
				break;
			}
			const auto& size = m_sizes[static_cast<std::size_t>(page)];
			const auto [width, height] =
			    helpers::exportPixels(size.width, size.height, dpi);
			const auto wait = std::make_shared<Wait<pdf::PageImageResult>>();
			const bool queued = m_session->pageImage(
			    page, width, height, [wait](pdf::PageImageResult result) {
				    wait->result = std::move(result);
				    SetEvent(wait->ready.get());
			    });
			if (queued) {
				co_await resume_on_signal(wait->ready.get());
			}
			co_await ui;
			if (m_closed || wait->result.canceled) {
				break;
			}
			std::vector<std::uint8_t> bytes;
			if (queued && wait->result.bitmap) {
				const auto buffer = co_await encodePage(
				    std::move(*wait->result.bitmap), jpeg, dpi);
				bytes.assign(buffer.data(), buffer.data() + buffer.Length());
			}
			const auto destination =
			    std::filesystem::path(folder.c_str()) /
			    helpers::exportName(stem, page + 1, pages.back() + 1,
			                        jpeg ? L".jpg" : L".png");
			if (bytes.empty() ||
			    helpers::saveFile(destination, m_path, bytes) !=
			        helpers::FileSaveResult::Saved) {
				finish();
				showNotice(L"Couldn't export page " + to_hstring(page + 1) +
				           L". Choose a writable folder and try again.");
				co_return;
			}
			++done;
		}
		finish();
		if (!m_closed && done == static_cast<int>(pages.size())) {
			showNotice(L"Exported " + to_hstring(done) +
			               (done == 1 ? L" page to " : L" pages to ") +
			               hstring(std::filesystem::path(folder.c_str())
			                           .filename()
			                           .wstring()) +
			               L".",
			           InfoBarSeverity::Success);
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not export pages");
		m_exporting = false;
		if (!m_closed) {
			showNotice(L"Couldn't export these pages. Try again.");
		}
	}
}
} // namespace winrt::Sable::implementation
