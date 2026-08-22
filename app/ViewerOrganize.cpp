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
#include "helpers/logging.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
namespace {
using XVisibility = Microsoft::UI::Xaml::Visibility;
// tiles are 160 px wide; very tall pages stop at 1.6 times that.
constexpr double TILE = 160, TALLEST = TILE * 1.6;

int tilePage(const Windows::Foundation::IInspectable& item) {
	const auto tile = item.try_as<FrameworkElement>();
	return tile ? unbox_value_or<int>(tile.Tag(), -1) : -1;
}
} // namespace

void ViewerPage::requestTool(std::wstring_view tool) {
	if (m_closed) {
		return;
	}
	// a tool picked before the PDF finished opening waits for it.
	if (!m_layout) {
		m_pendingTool = tool;
		return;
	}
	if (tool == L"readaloud") {
		startReadAloud();
	} else if (tool == L"organize") {
		openOrganize();
	} else if (tool == L"export") {
		exportPages();
	} else if (tool == L"comment" || tool == L"fillsign") {
		setCommentsOpen(true);
		if (tool == L"fillsign" && commentsOpen()) {
			openFillSign();
		}
	}
}

// Home's Fill & Sign opens Comments' More menu with its Fill & Sign entry
// focused. The panel has only just appeared, so the menu waits a turn for
// the button to be laid out.
void ViewerPage::openFillSign() {
	DispatcherQueue().TryEnqueue(
	    Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
	    [weak = get_weak()] {
		    const auto self = weak.get();
		    if (!self || self->m_closed || !self->commentsOpen()) {
			    return;
		    }
		    const auto more = self->CommentsMoreButton();
		    const auto flyout = more.Flyout().as<Controls::MenuFlyout>();
		    // focus only lands once the menu is in its popup; the handler
		    // removes itself on the first open.
		    auto token = std::make_shared<event_token>();
		    *token = flyout.Opened([weak, token](const auto& sender, auto&&) {
			    sender.template as<Controls::MenuFlyout>().Opened(*token);
			    if (const auto page = weak.get()) {
				    page->FillMarksMenu().Focus(FocusState::Keyboard);
			    }
		    });
		    flyout.ShowAt(more);
	    });
}

bool ViewerPage::organizing() {
	return OrganizePanel().Visibility() == XVisibility::Visible;
}

void ViewerPage::setupOrganize() {
	if (m_organizeSetup) {
		return;
	}
	m_organizeSetup = true;
	const auto weak = get_weak();
	const auto rotate = [weak](int turns) {
		if (const auto self = weak.get()) {
			if (auto pages = self->organizeSelection(); !pages.empty()) {
				self->organizeEdit(
				    {pdf::PageEditKind::Rotate, std::move(pages), turns});
			}
		}
	};
	OrganizeRotateLeft().Click([rotate](auto&&, auto&&) { rotate(3); });
	OrganizeRotateRight().Click([rotate](auto&&, auto&&) { rotate(1); });
	OrganizeDelete().Click([weak](auto&&, auto&&) {
		const auto self = weak.get();
		if (!self) {
			return;
		}
		auto pages = self->organizeSelection();
		if (pages.empty()) {
			return;
		}
		if (pages.size() >= self->m_sizes.size()) {
			self->showNotice(L"A PDF needs at least one page.");
			return;
		}
		self->organizeEdit({pdf::PageEditKind::Delete, std::move(pages)});
	});
	OrganizeInsert().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->insertPagesFromFile();
		}
	});
	OrganizeExtract().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->extractSelectedPages();
		}
	});
	OrganizeDone().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->closeOrganize();
		}
	});
	OrganizeGrid().SelectionChanged([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->showOrganizeState();
		}
	});
	OrganizeGrid().DragItemsCompleted(
	    [weak](auto&&, const DragItemsCompletedEventArgs& args) {
		    const auto self = weak.get();
		    if (!self || args.DropResult() ==
		                     Windows::ApplicationModel::DataTransfer::
		                         DataPackageOperation::None) {
			    return;
		    }
		    std::vector<int> dragged;
		    for (const auto& item : args.Items()) {
			    dragged.push_back(tilePage(item));
		    }
		    self->reorderPages(std::move(dragged));
	    });
	m_organizeTimer = DispatcherQueue().CreateTimer();
	m_organizeTimer.Interval(std::chrono::milliseconds(30));
	m_organizeTimer.Tick([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->renderOrganizeTile();
		}
	});
}

void ViewerPage::openOrganize() {
	if (m_closed || !m_layout || !m_session || m_presentation || m_noteDialog ||
	    m_passwordDialog) {
		return;
	}
	setupOrganize();
	cancelNotePlacement();
	clearAnnotationSelection();
	clearTextSelection();
	OrganizePanel().Visibility(XVisibility::Visible);
	fillOrganize();
	OrganizeGrid().Focus(FocusState::Programmatic);
}

void ViewerPage::closeOrganize() {
	if (!m_organizeSetup || !organizing()) {
		return;
	}
	m_organizeTimer.Stop();
	++m_organizeVersion;
	OrganizePanel().Visibility(XVisibility::Collapsed);
	OrganizeGrid().Items().Clear();
	// tiles took the render queue, so pages it turned away get a new try.
	m_failed.clear();
	m_retriedRenders.clear();
	updateVisible();
	Scroller().Focus(FocusState::Programmatic);
}

void ViewerPage::fillOrganize() {
	++m_organizeVersion;
	auto items = OrganizeGrid().Items();
	items.Clear();
	for (std::size_t page = 0; page < m_sizes.size(); ++page) {
		const auto& size = m_sizes[page];
		const double tall =
		    size.width > 0 ? TILE * size.height / size.width : TILE;
		StackPanel tile;
		tile.Spacing(6);
		tile.Padding({4, 4, 4, 4});
		tile.Tag(box_value(static_cast<int>(page)));
		Border frame;
		frame.Width(tall > TALLEST ? TALLEST * size.width / size.height : TILE);
		frame.Height((std::min)(tall, TALLEST));
		frame.Background(Media::SolidColorBrush({0xff, 0xff, 0xff, 0xff}));
		Image image;
		image.Stretch(Media::Stretch::Uniform);
		frame.Child(image);
		TextBlock caption;
		const auto name = L"Page " + to_hstring(page + 1);
		caption.Text(name);
		caption.HorizontalAlignment(HorizontalAlignment::Center);
		tile.Children().Append(frame);
		tile.Children().Append(caption);
		Automation::AutomationProperties::SetName(tile, name);
		items.Append(tile);
	}
	m_organizeTile = 0;
	m_organizeRendering = false;
	m_organizeTimer.Start();
	showOrganizeState();
}

void ViewerPage::renderOrganizeTile() {
	if (m_closed || !m_session || !organizing() || m_organizeRendering) {
		return;
	}
	if (m_organizeTile >= m_sizes.size()) {
		m_organizeTimer.Stop();
		return;
	}
	const auto page = m_organizeTile;
	const auto& size = m_sizes[page];
	const double scale = m_scale > 0 ? m_scale : 1;
	double wide = TILE,
	       tall = size.width > 0 ? TILE * size.height / size.width : TILE;
	if (tall > TALLEST) {
		wide = TALLEST * size.width / size.height;
		tall = TALLEST;
	}
	const pdf::PageKey key{
	    static_cast<int>(page),
	    (std::max)(1, static_cast<int>(std::lround(wide * scale))),
	    (std::max)(1, static_cast<int>(std::lround(tall * scale)))};
	m_organizeRendering = true;
	// the session renders one page at a time; a busy one gets a later tick.
	const bool queued = m_session->render(
	    key, [weak = get_weak(), queue = DispatcherQueue(),
	          version = m_organizeVersion, page](pdf::RenderResult result) {
		    auto rendered =
		        std::make_shared<pdf::RenderResult>(std::move(result));
		    queue.TryEnqueue([weak, rendered, version, page] {
			    const auto self = weak.get();
			    if (!self) {
				    return;
			    }
			    self->m_organizeRendering = false;
			    if (self->m_closed || version != self->m_organizeVersion ||
			        rendered->canceled) {
				    return;
			    }
			    if (rendered->bitmap) {
				    self->showOrganizeTile(page, *rendered->bitmap);
			    }
			    self->m_organizeTile = page + 1;
		    });
	    });
	if (!queued) {
		m_organizeRendering = false;
	}
}

void ViewerPage::showOrganizeTile(std::size_t page, const pdf::Bitmap& bitmap) {
	const auto items = OrganizeGrid().Items();
	if (page >= items.Size() || bitmap.width <= 0 || bitmap.height <= 0) {
		return;
	}
	Media::Imaging::WriteableBitmap pixels(bitmap.width, bitmap.height);
	auto* to = pixels.PixelBuffer().data();
	const auto row = static_cast<std::size_t>(bitmap.width) * 4;
	for (int y = 0; y < bitmap.height; ++y) {
		std::memcpy(to + row * y,
		            bitmap.pixels.data() +
		                static_cast<std::size_t>(bitmap.stride) * y,
		            row);
	}
	pixels.Invalidate();
	items.GetAt(static_cast<uint32_t>(page))
	    .as<StackPanel>()
	    .Children()
	    .GetAt(0)
	    .as<Border>()
	    .Child()
	    .as<Image>()
	    .Source(pixels);
}

std::vector<int> ViewerPage::organizeSelection() {
	std::vector<int> pages;
	if (!m_organizeSetup) {
		return pages;
	}
	for (const auto& item : OrganizeGrid().SelectedItems()) {
		if (const int page = tilePage(item); page >= 0) {
			pages.push_back(page);
		}
	}
	std::sort(pages.begin(), pages.end());
	return pages;
}

void ViewerPage::showOrganizeState() {
	const auto selected = organizeSelection().size();
	const bool idle = !m_organizeBusy;
	OrganizeRotateLeft().IsEnabled(idle && selected);
	OrganizeRotateRight().IsEnabled(idle && selected);
	OrganizeDelete().IsEnabled(idle && selected);
	OrganizeExtract().IsEnabled(idle && selected);
	OrganizeInsert().IsEnabled(idle);
	OrganizeGrid().CanReorderItems(idle);
	OrganizeGrid().CanDragItems(idle);
	auto text = to_hstring(m_sizes.size()) +
	            (m_sizes.size() == 1 ? L" page" : L" pages");
	if (selected) {
		text = text + L" · " + to_hstring(selected) + L" selected";
	}
	OrganizeStatus().Text(m_organizeBusy ? hstring(L"Updating pages…") : text);
}

// the grid has already moved the tiles; the PDF follows only when they
// form one block, which is what FPDF_MovePages can do in one step.
void ViewerPage::reorderPages(std::vector<int> dragged) {
	std::vector<int> order;
	for (const auto& item : OrganizeGrid().Items()) {
		order.push_back(tilePage(item));
	}
	std::vector<int> moved;
	int destination = -1;
	for (int index = 0; index < static_cast<int>(order.size()); ++index) {
		if (std::find(dragged.begin(), dragged.end(), order[index]) !=
		    dragged.end()) {
			destination = destination < 0 ? index : destination;
			moved.push_back(order[index]);
		}
	}
	std::vector<int> expected;
	for (int page = 0; page < static_cast<int>(m_sizes.size()); ++page) {
		if (std::find(moved.begin(), moved.end(), page) == moved.end()) {
			expected.push_back(page);
		}
	}
	if (destination >= 0 &&
	    static_cast<std::size_t>(destination) <= expected.size()) {
		expected.insert(expected.begin() + destination, moved.begin(),
		                moved.end());
	}
	bool same = true;
	for (int index = 0; index < static_cast<int>(order.size()); ++index) {
		same = same && order[index] == index;
	}
	if (same) {
		return;
	}
	if (moved.empty() || expected != order) {
		fillOrganize();
		showNotice(L"Couldn't move those pages. Try moving them together.");
		return;
	}
	organizeEdit({pdf::PageEditKind::Move, std::move(moved), 0, destination});
}

void ViewerPage::organizeEdit(pdf::PageEdit edit) {
	if (m_closed || !m_session || m_organizeBusy) {
		return;
	}
	edit.revision = m_session->annotationRevision();
	m_organizeBusy = true;
	showOrganizeState();
	const auto session = m_session.get();
	bool queued = false;
	try {
		queued = session->editPages(
		    std::move(edit), [weak = get_weak(), queue = DispatcherQueue(),
		                      session](pdf::PageEditResult result) {
			    auto shared =
			        std::make_shared<pdf::PageEditResult>(std::move(result));
			    queue.TryEnqueue([weak, shared, session] {
				    if (const auto self = weak.get();
				        self && self->m_session.get() == session) {
					    self->finishPageEdit(std::move(*shared));
				    }
			    });
		    });
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue a page edit");
	}
	if (!queued) {
		m_organizeBusy = false;
		showOrganizeState();
		showNotice(L"Couldn't change these pages. Try again.");
	}
}

void ViewerPage::finishPageEdit(pdf::PageEditResult result) {
	m_organizeBusy = false;
	if (m_closed || result.canceled) {
		return;
	}
	if (!result.changed || result.info.pages.empty()) {
		if (organizing()) {
			fillOrganize();
		}
		showNotice(writeNotice(result.refusal,
		                       L"Couldn't change these pages. The PDF may "
		                       L"not allow page changes."));
		return;
	}
	m_notesDirty = true;
	pagesChanged(std::move(result.info));
	if (organizing()) {
		fillOrganize();
	}
}

void ViewerPage::pagesChanged(pdf::SessionInfo info) {
	std::vector<helpers::PageSize> sizes;
	sizes.reserve(info.pages.size());
	for (const auto& size : info.pages) {
		sizes.push_back({size.width, size.height});
	}
	const auto page = (std::min)(m_current, sizes.size() - 1);
	m_sizes = std::move(sizes);
	// the same page number can now hold another page's text
	m_pageTextPage.reset();
	m_labels = helpers::pageLabels(std::move(info.labels), m_sizes.size());
	closeFind();
	clearTextSelection();
	m_text.clear();
	try {
		if (!layOut(m_rotation, page)) {
			showMessage(L"This PDF is too long to show in one piece yet.");
			return;
		}
	} catch (...) {
		m_holdCurrent = false;
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not lay out the changed pages");
		showMessage(L"Couldn't show the changed pages.");
		return;
	}
	// every page index may have moved, so the comments are listed again.
	refreshNotes();
	showPosition();
	showComments();
}

fire_and_forget ViewerPage::insertPagesFromFile() {
	const auto lifetime = get_strong();
	try {
		if (m_closed || !m_session || m_organizeBusy || !XamlRoot()) {
			co_return;
		}
		Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(
		    XamlRoot().ContentIslandEnvironment().AppWindowId());
		picker.FileTypeFilter().Append(L".pdf");
		const auto file = co_await picker.PickSingleFileAsync();
		if (!file || m_closed || !m_session || m_organizeBusy) {
			co_return;
		}
		// new pages go after the last selected tile, or at the end.
		const auto selected = organizeSelection();
		const int index = selected.empty() ? static_cast<int>(m_sizes.size())
		                                   : selected.back() + 1;
		m_organizeBusy = true;
		showOrganizeState();
		const auto session = m_session.get();
		const bool queued = session->insertPages(
		    file.Path().c_str(), {}, index, session->annotationRevision(),
		    [weak = get_weak(), queue = DispatcherQueue(),
		     session](pdf::PageEditResult result) {
			    auto shared =
			        std::make_shared<pdf::PageEditResult>(std::move(result));
			    queue.TryEnqueue([weak, shared, session] {
				    const auto self = weak.get();
				    if (!self || self->m_session.get() != session) {
					    return;
				    }
				    if (!shared->changed && !shared->canceled &&
				        shared->refusal == pdf::WriteRefusal::None) {
					    self->m_organizeBusy = false;
					    self->showOrganizeState();
					    self->showNotice(
					        L"Couldn't insert pages from that file. It may "
					        L"need a password or be damaged.");
					    return;
				    }
				    self->finishPageEdit(std::move(*shared));
			    });
		    });
		if (!queued) {
			m_organizeBusy = false;
			showOrganizeState();
			showNotice(L"Couldn't insert pages. Try again.");
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not insert pages from a file");
		m_organizeBusy = false;
		if (!m_closed) {
			showOrganizeState();
			showNotice(L"Couldn't insert pages. Try again.");
		}
	}
}

fire_and_forget ViewerPage::extractSelectedPages() {
	const auto lifetime = get_strong();
	try {
		auto pages = organizeSelection();
		if (m_closed || !m_session || m_organizeBusy || pages.empty() ||
		    !XamlRoot()) {
			co_return;
		}
		m_organizeBusy = true;
		showOrganizeState();
		struct Reply {
			handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
			pdf::ExtractResult result;
		};
		const auto reply = std::make_shared<Reply>();
		check_bool(static_cast<bool>(reply->ready));
		const apartment_context ui;
		const bool queued = m_session->extractPages(
		    std::move(pages), [reply](pdf::ExtractResult result) {
			    reply->result = std::move(result);
			    SetEvent(reply->ready.get());
		    });
		if (queued) {
			co_await resume_on_signal(reply->ready.get());
		}
		co_await ui;
		m_organizeBusy = false;
		showOrganizeState();
		if (m_closed || reply->result.canceled) {
			co_return;
		}
		if (!queued || !reply->result.bytes) {
			showNotice(L"Couldn't extract these pages. The PDF may not "
			           L"allow copying.");
			co_return;
		}
		// the new PDF goes out through the same picker and replace-after-
		// flush writer as Save a copy, and never over the open file.
		Microsoft::Windows::Storage::Pickers::FileSavePicker picker(
		    XamlRoot().ContentIslandEnvironment().AppWindowId());
		const auto stem = m_path.stem().wstring();
		picker.SuggestedFileName(hstring(stem) + L" (pages)");
		picker.FileTypeChoices().Insert(
		    L"PDF document", single_threaded_vector<hstring>({L".pdf"}));
		picker.DefaultFileExtension(L".pdf");
		const auto chosen = co_await picker.PickSaveFileAsync();
		if (!chosen || m_closed) {
			co_return;
		}
		const std::filesystem::path destination(chosen.Path().c_str());
		const auto saved =
		    helpers::saveFile(destination, m_path, *reply->result.bytes);
		if (saved == helpers::FileSaveResult::Saved) {
			showNotice(L"Saved the pages to " +
			               hstring(destination.filename().wstring()) + L".",
			           InfoBarSeverity::Success);
		} else if (saved == helpers::FileSaveResult::Source) {
			showNotice(L"Choose a different file; the open PDF stays as it "
			           L"is.");
		} else if (saved != helpers::FileSaveResult::Canceled) {
			showNotice(L"Couldn't save the pages. Choose a writable folder.");
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not extract pages");
		m_organizeBusy = false;
		if (!m_closed) {
			showOrganizeState();
			showNotice(L"Couldn't extract these pages. Try again.");
		}
	}
}
} // namespace winrt::Sable::implementation
