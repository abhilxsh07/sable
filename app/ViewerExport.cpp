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
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
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

IAsyncOperation<bool> writePageImage(pdf::Bitmap bitmap,
                                     std::filesystem::path destination,
                                     std::filesystem::path source, bool jpeg,
                                     int dpi) {
	co_await resume_background();
	try {
		const auto buffer = co_await encodePage(std::move(bitmap), jpeg, dpi);
		const std::vector<std::uint8_t> bytes(buffer.data(),
		                                      buffer.data() + buffer.Length());
		co_return helpers::saveFile(destination, source, bytes) ==
		    helpers::FileSaveResult::Saved;
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not encode or write an exported page");
		co_return false;
	}
}

IAsyncOperation<bool> writeExportText(std::filesystem::path destination,
                                      std::filesystem::path source,
                                      std::string text) {
	co_await resume_background();
	try {
		co_return destination != source&& helpers::saveFile(destination, text);
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not write exported text");
		co_return false;
	}
}
} // namespace

// Export pages: PNG or JPEG images of the unfiltered pages as they print,
// or the document's text. The open file is only read. It lives in the tool
// pane beside the pages; asking again moves focus into it.
void ViewerPage::exportPages() {
	if (m_closed || !m_layout || !m_session || m_noteDialog ||
	    m_passwordDialog || presenting() || !XamlRoot()) {
		return;
	}
	if (organizing() || pageOperationBusy() || m_noteBusy || m_noteSaving) {
		showNotice(L"Finish the page or note operation before exporting.");
		return;
	}
	setupExportPane();
	refreshExportSelection();
	const bool opened = m_toolPane == ToolPane::Export;
	openPane(ToolPane::Export);
	if (!opened) {
		focusToolPane();
	}
}

void ViewerPage::setupExportPane() {
	if (m_exportSetup) {
		return;
	}
	m_exportSetup = true;
	const auto weak = get_weak();
	ExportPages().SelectionChanged([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->ExportRange().IsEnabled(self->ExportPages().SelectedIndex() ==
			                              3);
		}
	});
	ExportFormat().SelectionChanged([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->ExportResolution().IsEnabled(
			    self->ExportFormat().SelectedIndex() != 2);
			self->refreshExportPreview();
		}
	});
	ExportButton().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->runExport();
		}
	});
	ExportFolderChange().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->changeExportFolder();
		}
	});
	ExportCancel().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get(); self && self->m_exporting) {
			self->m_exportCanceled = true;
			self->ExportCancel().IsEnabled(false);
		}
	});
}

void ViewerPage::refreshExportSelection() {
	if (!m_exportSetup || m_closed) {
		return;
	}
	ExportSelection().IsEnabled(!m_exportSelection.empty());
	if (m_exportSelection.empty() && ExportPages().SelectedIndex() == 2) {
		ExportPages().SelectedIndex(0);
	}
}

// the small unfiltered preview follows the page, not the output range.
fire_and_forget ViewerPage::refreshExportPreview(bool force) {
	const auto lifetime = get_strong();
	if (m_closed || !m_session || m_toolPane != ToolPane::Export ||
	    m_current >= m_sizes.size()) {
		co_return;
	}
	const auto page = m_current;
	const int kind = ExportFormat().SelectedIndex();
	const auto revision = m_session->annotationRevision();
	const auto key = std::tuple{page, kind, revision};
	if (!force && m_exportPreviewKey == key) {
		co_return;
	}
	m_exportPreviewKey = key;
	const auto version = ++m_exportPreviewVersion;
	const auto current = [this, version, page, kind, revision] {
		return !m_closed && m_session && m_exportPreviewVersion == version &&
		       m_current == page && m_toolPane == ToolPane::Export &&
		       ExportFormat().SelectedIndex() == kind &&
		       m_session->annotationRevision() == revision;
	};
	try {
		const auto label = L"Page " + to_hstring(page + 1) + L": " +
		                   (kind == 2   ? L"Text"
		                    : kind == 1 ? L"JPEG"
		                                : L"PNG") +
		                   L" preview";
		ExportPreviewLabel().Text(label);
		Automation::AutomationProperties::SetName(ExportPreviewImage(), label);
		ExportPreviewImage().Source(nullptr);
		ExportPreviewImage().Visibility(Visibility::Collapsed);
		ExportPreviewText().Visibility(Visibility::Visible);
		ExportPreviewText().Text(L"Loading preview...");
		const apartment_context ui;
		if (kind == 2) {
			const auto wait = std::make_shared<Wait<pdf::DocumentTextResult>>();
			const bool queued =
			    m_session->documentText({static_cast<int>(page)},
			                            [wait](pdf::DocumentTextResult result) {
				                            wait->result = std::move(result);
				                            SetEvent(wait->ready.get());
			                            });
			if (queued) {
				co_await resume_on_signal(wait->ready.get());
			}
			co_await ui;
			if (current()) {
				ExportPreviewText().Text(
				    !queued || !wait->result.text || wait->result.canceled
				        ? L"Couldn't read this page's text."
				    : wait->result.text->empty()
				        ? L"No text on this page."
				        : to_hstring(*wait->result.text));
			}
			co_return;
		}
		const auto& size = m_sizes[page];
		const double scale = (std::min)(240. / size.width, 220. / size.height);
		const int width = (std::max)(1, static_cast<int>(size.width * scale));
		const int height = (std::max)(1, static_cast<int>(size.height * scale));
		const auto wait = std::make_shared<Wait<pdf::PageImageResult>>();
		const bool queued =
		    m_session->pageImage(static_cast<int>(page), width, height,
		                         [wait](pdf::PageImageResult result) {
			                         wait->result = std::move(result);
			                         SetEvent(wait->ready.get());
		                         });
		if (queued) {
			co_await resume_on_signal(wait->ready.get());
		}
		co_await ui;
		if (!current()) {
			co_return;
		}
		if (!queued || !wait->result.bitmap || wait->result.canceled) {
			ExportPreviewText().Text(L"Couldn't load this preview.");
			co_return;
		}
		const auto buffer =
		    co_await encodePage(std::move(*wait->result.bitmap), kind == 1, 72);
		Windows::Storage::Streams::InMemoryRandomAccessStream stream;
		co_await stream.WriteAsync(buffer);
		stream.Seek(0);
		Media::Imaging::BitmapImage image;
		co_await image.SetSourceAsync(stream);
		if (current()) {
			ExportPreviewImage().Source(image);
			ExportPreviewImage().Visibility(Visibility::Visible);
			ExportPreviewText().Visibility(Visibility::Collapsed);
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not load the export preview");
		if (current()) {
			ExportPreviewText().Text(L"Couldn't load this preview.");
		}
	}
}

void ViewerPage::setExportBusy(bool busy) {
	m_exporting = busy;
	if (!m_closed) {
		ExportButton().IsEnabled(!busy);
		ExportFolderChange().IsEnabled(!busy);
		ExportFormat().IsEnabled(!busy);
		ExportPages().IsEnabled(!busy);
		ExportRange().IsEnabled(!busy && ExportPages().SelectedIndex() == 3);
		ExportResolution().IsEnabled(!busy &&
		                             ExportFormat().SelectedIndex() != 2);
		if (!busy) {
			ExportProgressRow().Visibility(Visibility::Collapsed);
		}
	}
}

IAsyncOperation<bool> ViewerPage::chooseExportFolder() {
	const auto lifetime = get_strong();
	hstring folder;
#ifdef SABLE_SURFACE_TEST
	if (m_exportPicker) {
		folder = co_await m_exportPicker(true);
	} else
#endif
	{
		const auto owner = XamlRoot().ContentIslandEnvironment().AppWindowId();
		Microsoft::Windows::Storage::Pickers::FolderPicker picker(owner);
		if (const auto chosen = co_await picker.PickSingleFolderAsync()) {
			folder = chosen.Path();
		}
	}
	if (m_closed || folder.empty()) {
		co_return false;
	}
	m_exportFolder = folder.c_str();
	ExportFolder().Text(L"Save to " + folder);
	co_return true;
}

fire_and_forget ViewerPage::changeExportFolder() {
	const auto lifetime = get_strong();
	if (m_closed || m_exporting || pageOperationBusy()) {
		co_return;
	}
	setExportBusy(true);
	try {
		co_await chooseExportFolder();
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not choose the export folder");
		if (!m_closed) {
			showNotice(L"Couldn't choose that folder. Try again.");
		}
	}
	setExportBusy(false);
}

fire_and_forget ViewerPage::runExport() {
	const auto lifetime = get_strong();
	try {
		if (m_closed || !m_layout || !m_session || m_exporting ||
		    m_noteDialog || m_passwordDialog || !XamlRoot()) {
			co_return;
		}
		if (organizing() || pageOperationBusy() || m_noteBusy || m_noteSaving) {
			showNotice(L"Finish the page or note operation before exporting.");
			co_return;
		}
		const int count = static_cast<int>(m_sizes.size());
		std::vector<int> pages;
		switch (ExportPages().SelectedIndex()) {
		case 1:
			pages.push_back(
			    static_cast<int>((std::min)(m_current, m_sizes.size() - 1)));
			break;
		case 2:
			pages = m_exportSelection;
			break;
		case 3:
			if (const auto parsed =
			        helpers::parsePageList(ExportRange().Text(), count)) {
				pages = *parsed;
			} else {
				ExportError().Text(L"Enter pages like 1-3, 5 between 1 and " +
				                   to_hstring(helpers::grouped(
				                       static_cast<std::size_t>(count))) +
				                   L".");
				ExportRange().Focus(FocusState::Programmatic);
				co_return;
			}
			break;
		default:
			for (int page = 0; page < count; ++page) {
				pages.push_back(page);
			}
		}
		ExportError().Text(L"");
		const int kind = ExportFormat().SelectedIndex();
		const int dpi = std::array{72, 150, 300}[static_cast<std::size_t>(
		    std::clamp(ExportResolution().SelectedIndex(), 0, 2))];
		if (pages.empty() || !m_session) {
			co_return;
		}
		setExportBusy(true);
		const auto finish = [this] { setExportBusy(false); };
		const auto stem = m_path.stem().wstring();
		const apartment_context ui;
		if (m_exportFolder.empty() && !co_await chooseExportFolder()) {
			finish();
			co_return;
		}
		if (m_closed || !m_session) {
			finish();
			co_return;
		}
		const auto folder = m_exportFolder;
		const auto source = m_path;
		const bool jpeg = kind == 1;
		const int total = static_cast<int>(pages.size());
		m_exportCanceled = false;
		ExportCancel().IsEnabled(true);
		ExportProgress().Maximum(total);
		ExportProgress().Value(0);
		ExportProgressRow().Visibility(Visibility::Visible);
		int done = 0;
		std::string text;
		for (const int page : pages) {
			if (m_closed || !m_session || m_exportCanceled) {
				break;
			}
			ExportProgressText().Text(to_hstring(done + 1) + L" of " +
			                          to_hstring(total));
			bool saved = false;
			if (kind == 2) {
				const auto wait =
				    std::make_shared<Wait<pdf::DocumentTextResult>>();
				const bool queued = m_session->documentText(
				    {page}, [wait](pdf::DocumentTextResult result) {
					    wait->result = std::move(result);
					    SetEvent(wait->ready.get());
				    });
				if (queued) {
					co_await resume_on_signal(wait->ready.get());
				}
				co_await ui;
				if (queued && wait->result.text && !wait->result.canceled) {
					text += done ? "\f" : "";
					text += *wait->result.text;
					saved = true;
				}
			} else {
				const auto& size = m_sizes[static_cast<std::size_t>(page)];
				const auto [width, height] =
				    helpers::exportPixels(size.width, size.height, dpi);
				const auto wait =
				    std::make_shared<Wait<pdf::PageImageResult>>();
				const bool queued = m_session->pageImage(
				    page, width, height, [wait](pdf::PageImageResult result) {
					    wait->result = std::move(result);
					    SetEvent(wait->ready.get());
				    });
				if (queued) {
					co_await resume_on_signal(wait->ready.get());
				}
				co_await ui;
				if (!m_closed && queued && wait->result.bitmap &&
				    !wait->result.canceled) {
					const auto destination =
					    folder / helpers::exportName(stem, page + 1,
					                                 pages.back() + 1,
					                                 jpeg ? L".jpg" : L".png");
					saved =
					    co_await writePageImage(std::move(*wait->result.bitmap),
					                            destination, source, jpeg, dpi);
				}
			}
			if (m_closed) {
				break;
			}
			if (!saved) {
				finish();
				showNotice(L"Couldn't export page " + to_hstring(page + 1) +
				           L". " + to_hstring(kind == 2 ? 0 : done) +
				           L" files written to " + hstring(folder.wstring()) +
				           L". Choose a writable folder and try again.");
				co_return;
			}
			++done;
			ExportProgress().Value(done);
		}
		if (kind == 2 && done && !m_closed) {
			if (!co_await writeExportText(folder / (stem + L".txt"), source,
			                              std::move(text))) {
				finish();
				if (!m_closed) {
					showNotice(
					    L"Couldn't save the text to " +
					    hstring(folder.wstring()) +
					    L". Choose a writable folder and a new file name.");
				}
				co_return;
			}
		}
		finish();
		if (!m_closed) {
			const int files = kind == 2 ? (done ? 1 : 0) : done;
			showNotice((m_exportCanceled ? L"Stopped export: " : L"Exported ") +
			               to_hstring(files) +
			               (files == 1 ? L" file" : L" files") +
			               (kind == 2 ? L" (" + to_hstring(done) + L" pages)"
			                          : hstring{}) +
			               L" to " + hstring(folder.wstring()) + L".",
			           m_exportCanceled ? InfoBarSeverity::Informational
			                            : InfoBarSeverity::Success);
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not export pages");
		setExportBusy(false);
		if (!m_closed) {
			showNotice(L"Couldn't export these pages. Try again.");
		}
	}
}
} // namespace winrt::Sable::implementation
