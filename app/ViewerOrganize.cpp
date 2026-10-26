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
#include "Motion.h"
#include "pdf/session.h"
#include "helpers/attachment_file.h"
#include "helpers/keybinds.h"
#include "helpers/logging.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
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
Windows::Foundation::IAsyncOperation<int>
writeExtractedPdf(std::filesystem::path destination,
                  std::filesystem::path source,
                  std::vector<std::uint8_t> bytes) {
	co_await resume_background();
	try {
		co_return static_cast<int>(
		    helpers::saveFile(destination, source, bytes));
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not write extracted pages");
		co_return static_cast<int>(helpers::FileSaveResult::Failed);
	}
}

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
	if (organizing() && tool != L"organize") {
		showNotice(L"Finish Organize before opening another tool.");
		return;
	}
	if (tool == L"readaloud") {
		// the entry toggles: a second click stops what the first started
		if (readingAloud()) {
			stopReadAloud();
		} else {
			startReadAloud();
		}
	} else if (tool == L"organize") {
		openOrganize();
	} else if (tool == L"combine") {
		openCombine();
	} else if (tool == L"export") {
		exportPages();
	} else if (tool == L"fillsign") {
		openFillSign();
	} else if (tool == L"comment") {
		if (const auto reason = annotationRefusal()) {
			showNotice(reason);
			return;
		}
		if (m_toolRow != ToolRow::Comment) {
			cancelNotePlacement();
		}
		m_toolRow = ToolRow::Comment;
		setCommentsOpen(true);
	}
}

// the row leaves the reading panel alone; focus waits for its layout.
void ViewerPage::openFillSign() {
	if (m_closed) {
		return;
	}
	if (m_toolRow != ToolRow::FillSign) {
		cancelNotePlacement();
		m_toolRow = ToolRow::FillSign;
	}
	updateQuickTools();
	if (const auto reason = annotationRefusal()) {
		showNotice(reason);
	}
	DispatcherQueue().TryEnqueue(
	    Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
	    [weak = get_weak()] {
		    const auto self = weak.get();
		    if (!self || self->m_closed ||
		        self->Visibility() != XVisibility::Visible ||
		        self->m_toolRow != ToolRow::FillSign || self->organizing() ||
		        self->presenting()) {
			    return;
		    }
		    const auto target =
		        self->FillSignActions().IsEnabled()
		            ? self->PlaceFillTextButton().as<Controls::Control>()
		            : self->FillSignCloseButton().as<Controls::Control>();
		    target.Focus(FocusState::Keyboard);
	    });
}

bool ViewerPage::organizing() {
	return m_mode == ViewerMode::Organize;
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
		if (self) {
			self->deleteOrganizeSelection();
		}
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
	m_organizeVisibility = RegisterPropertyChangedCallback(
	    UIElement::VisibilityProperty(), [weak](auto&&, auto&&) {
		    const auto self = weak.get();
		    if (!self || self->m_closed || !self->organizing()) {
			    return;
		    }
		    if (self->Visibility() == XVisibility::Visible) {
			    self->fillOrganize();
		    } else {
			    self->m_organizeHiddenSelection = self->organizeSelection();
			    self->releaseOrganizeTiles();
		    }
	    });
}

void ViewerPage::openOrganize() {
	if (m_closed || !m_layout || !m_session || presenting() || m_noteDialog ||
	    m_passwordDialog || pageOperationBusy() || m_noteBusy || m_noteSaving) {
		return;
	}
	if (organizing()) {
		OrganizeGrid().Focus(FocusState::Programmatic);
		return;
	}
	clearNotice();
	setupOrganize();
	m_organizeView = OrganizeView{thumbnailsOpen(),
	                              outlineOpen(),
	                              attachmentsOpen(),
	                              commentsOpen(),
	                              m_toolPane,
	                              findOpen(),
	                              readingAloud() && !m_readAloudPaused};
	m_mode = ViewerMode::Organize;
	cancelNotePlacement();
	clearAnnotationSelection();
	clearTextSelection();
	closeFind();
	pauseReadAloud();
	ReadAloudBar().Visibility(XVisibility::Collapsed);
	ReadAloudFrame().Visibility(XVisibility::Collapsed);
	setAllToolsOpen(false);
	setThumbnailsOpen(false);
	setOutlineOpen(false);
	setAttachmentsOpen(false);
	setCommentsOpen(false);
	for (const auto& button :
	     {ThumbnailsButton(), OutlineButton(), AttachmentButton(),
	      CommentsButton(), AllToolsButton()}) {
		button.IsEnabled(false);
	}
	BarFindButton().IsEnabled(false);
	Scroller().IsEnabled(false);
	OrganizePanel().Visibility(XVisibility::Visible);
	showOrganizeBar(true);
	updateQuickTools();
	fillOrganize();
	OrganizeGrid().Focus(FocusState::Programmatic);
}

// Organize's actions take the middle of the command bar and Done takes the
// place of find, save, print and More
void ViewerPage::showOrganizeBar(bool show) {
	OrganizeActions().Visibility(show ? XVisibility::Visible
	                                  : XVisibility::Collapsed);
	OrganizeDone().Visibility(show ? XVisibility::Visible
	                               : XVisibility::Collapsed);
	motion::show(BarActions(), !show);
}

void ViewerPage::releaseOrganizeTiles() {
	m_organizeTimer.Stop();
	++m_organizeVersion;
	m_organizeRendering = false;
	m_organizeTile = 0;
	OrganizeGrid().Items().Clear();
}

void ViewerPage::closeOrganize(bool restore) {
	if (!m_organizeSetup || !organizing()) {
		return;
	}
	const auto previous = *std::exchange(m_organizeView, std::nullopt);
	auto selected = organizeSelection();
	m_exportSelection = selected;
	refreshExportSelection();
	m_mode = ViewerMode::Read;
	clearNotice();
	OrganizePanel().Visibility(XVisibility::Collapsed);
	showOrganizeBar(false);
	releaseOrganizeTiles();
	m_organizeHiddenSelection.clear();
	if (!restore) {
		return;
	}
	Scroller().IsEnabled(true);
	for (const auto& button :
	     {ThumbnailsButton(), OutlineButton(), AttachmentButton(),
	      CommentsButton(), AllToolsButton()}) {
		button.IsEnabled(true);
	}
	BarFindButton().IsEnabled(true);
	updateRailAvailability();
	setThumbnailsOpen(previous.thumbnails);
	m_exportSelection = std::move(selected);
	refreshExportSelection();
	setOutlineOpen(previous.outline);
	setAttachmentsOpen(previous.attachments);
	setCommentsOpen(previous.comments);
	if (previous.pane != ToolPane::None) {
		openPane(previous.pane);
	} else {
		closePane();
	}
	if (previous.find) {
		openFind();
	}
	if (readingAloud()) {
		ReadAloudBar().Visibility(XVisibility::Visible);
		if (previous.resumeReadAloud) {
			resumeReadAloud();
		}
		updateReadAloudFrame();
	}
	// tiles took the render queue, so pages it turned away get a new try.
	m_failed.clear();
	m_retriedRenders.clear();
	updateVisible();
	takeFocus();
}

void ViewerPage::fillOrganize() {
	if (m_closed || Visibility() != XVisibility::Visible) {
		releaseOrganizeTiles();
		return;
	}
	++m_organizeVersion;
	auto items = OrganizeGrid().Items();
	items.Clear();
	for (std::size_t page = 0; page < m_sizes.size(); ++page) {
		StackPanel tile;
		tile.Width(TILE + 8);
		tile.Height(TALLEST + 34);
		tile.Spacing(6);
		tile.Padding({4, 4, 4, 4});
		tile.Tag(box_value(static_cast<int>(page)));
		Grid cell;
		cell.Width(TILE);
		cell.Height(TALLEST);
		Border frame;
		frame.HorizontalAlignment(HorizontalAlignment::Center);
		frame.VerticalAlignment(VerticalAlignment::Center);
		frame.Background(Media::SolidColorBrush({0xff, 0xff, 0xff, 0xff}));
		Image image;
		image.Stretch(Media::Stretch::Uniform);
		frame.Child(image);
		TextBlock caption;
		const auto name = L"Page " + to_hstring(page + 1);
		caption.Text(name);
		caption.HorizontalAlignment(HorizontalAlignment::Center);
		cell.Children().Append(frame);
		tile.Children().Append(cell);
		tile.Children().Append(caption);
		Automation::AutomationProperties::SetName(tile, name);
		items.Append(tile);
		sizeOrganizeTile(page);
	}
	for (const int page : m_organizeHiddenSelection) {
		if (page >= 0 && static_cast<uint32_t>(page) < items.Size()) {
			OrganizeGrid().SelectRange(Data::ItemIndexRange(page, 1));
		}
	}
	m_organizeHiddenSelection.clear();
	m_organizeTile = 0;
	m_organizeRendering = false;
	m_organizeTimer.Start();
	showOrganizeState();
}

void ViewerPage::sizeOrganizeTile(std::size_t page) {
	const auto& size = m_sizes.at(page);
	const double tall = size.width > 0 ? TILE * size.height / size.width : TILE;
	const auto frame = OrganizeGrid()
	                       .Items()
	                       .GetAt(static_cast<uint32_t>(page))
	                       .as<StackPanel>()
	                       .Children()
	                       .GetAt(0)
	                       .as<Grid>()
	                       .Children()
	                       .GetAt(0)
	                       .as<Border>();
	frame.Width(tall > TALLEST ? TALLEST * size.width / size.height : TILE);
	frame.Height((std::min)(tall, TALLEST));
	frame.Child().as<Image>().Source(nullptr);
}

void ViewerPage::renderOrganizeTile() {
	if (m_closed || !m_session || !organizing() ||
	    Visibility() != XVisibility::Visible) {
		m_organizeTimer.Stop();
		return;
	}
	if (m_organizeRendering) {
		return;
	}
	const auto items = OrganizeGrid().Items();
	while (m_organizeTile < items.Size() &&
	       items.GetAt(static_cast<uint32_t>(m_organizeTile))
	           .as<StackPanel>()
	           .Children()
	           .GetAt(0)
	           .as<Grid>()
	           .Children()
	           .GetAt(0)
	           .as<Border>()
	           .Child()
	           .as<Image>()
	           .Source()) {
		++m_organizeTile;
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
			    if (self->m_closed || version != self->m_organizeVersion) {
				    return;
			    }
			    self->m_organizeRendering = false;
			    if (rendered->canceled) {
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
	    .as<Grid>()
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

void ViewerPage::deleteOrganizeSelection() {
	if (pageOperationBusy() || m_noteSaving || m_noteBusy) {
		return;
	}
	auto pages = organizeSelection();
	if (pages.empty()) {
		return;
	}
	if (pages.size() >= m_sizes.size()) {
		showNotice(L"A PDF needs at least one page.");
		return;
	}
	organizeEdit({pdf::PageEditKind::Delete, std::move(pages)});
}

bool ViewerPage::organizeKey(unsigned key, unsigned modifiers, bool repeated) {
	if (key == VK_ESCAPE && modifiers == 0) {
		closeOrganize();
		return true;
	}
	if (key == 'A' && modifiers == helpers::SHORTCUT_CTRL) {
		OrganizeGrid().SelectAll();
		return true;
	}
	if (key == VK_DELETE && modifiers == 0) {
		if (!repeated) {
			deleteOrganizeSelection();
		}
		return true;
	}
	return false;
}

void ViewerPage::showOrganizeState() {
	const auto selected = organizeSelection().size();
	const bool idle = !pageOperationBusy() && !m_noteBusy && !m_noteSaving;
	OrganizeRotateLeft().IsEnabled(idle && selected);
	OrganizeRotateRight().IsEnabled(idle && selected);
	OrganizeDelete().IsEnabled(idle && selected);
	OrganizeExtract().IsEnabled(idle && selected);
	OrganizeInsert().IsEnabled(idle);
	OrganizeGrid().CanReorderItems(idle);
	OrganizeGrid().CanDragItems(idle);
	auto text = to_hstring(helpers::grouped(m_sizes.size())) +
	            (m_sizes.size() == 1 ? L" page" : L" pages");
	if (selected) {
		text = text + L" · " + to_hstring(helpers::grouped(selected)) +
		       L" selected";
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
	if (m_closed || !m_session || pageOperationBusy() || m_noteBusy ||
	    m_noteSaving) {
		if (!m_closed && organizing() && edit.kind == pdf::PageEditKind::Move) {
			fillOrganize();
			showNotice(L"Couldn't move those pages. Try again when the "
			           L"operation finishes.");
		}
		return;
	}
	edit.revision = m_session->annotationRevision();
	const auto rotated = edit.kind == pdf::PageEditKind::Rotate
	                         ? edit.pages
	                         : std::vector<int>{};
	m_organizeBusy = true;
	showOrganizeState();
	const auto session = m_session.get();
	bool queued = false;
	try {
		queued = session->editPages(
		    std::move(edit), [weak = get_weak(), queue = DispatcherQueue(),
		                      session, rotated](pdf::PageEditResult result) {
			    auto shared =
			        std::make_shared<pdf::PageEditResult>(std::move(result));
			    queue.TryEnqueue([weak, shared, session, rotated] {
				    if (const auto self = weak.get();
				        self && self->m_session.get() == session) {
					    self->finishPageEdit(std::move(*shared), rotated);
				    }
			    });
		    });
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue a page edit");
	}
	if (!queued) {
		m_organizeBusy = false;
		if (organizing()) {
			fillOrganize();
		}
		showOrganizeState();
		showNotice(L"Couldn't change these pages. Try again.");
	}
}

void ViewerPage::finishPageEdit(pdf::PageEditResult result,
                                std::vector<int> rotated) {
	m_organizeBusy = false;
	if (m_closed) {
		return;
	}
	if (result.canceled) {
		if (organizing()) {
			fillOrganize();
		}
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
	clearNotice();
	m_notesDirty = true;
	m_pagesDirty = true;
	pagesChanged(std::move(result.info));
	if (organizing()) {
		if (rotated.empty() || Visibility() != XVisibility::Visible ||
		    OrganizeGrid().Items().Size() != m_sizes.size()) {
			if (rotated.empty()) {
				m_organizeHiddenSelection.clear();
			}
			fillOrganize();
		} else {
			++m_organizeVersion;
			m_organizeRendering = false;
			for (const int page : rotated) {
				sizeOrganizeTile(static_cast<std::size_t>(page));
			}
			m_organizeTile = 0;
			m_organizeTimer.Start();
			showOrganizeState();
		}
	}
}

void ViewerPage::pagesChanged(pdf::SessionInfo info) {
	m_hasOutline = info.hasOutline;
	m_hasAttachments = info.hasAttachments;
	updateRailAvailability();
	m_editRefusal = info.editRefusal;
	m_noteAllowed =
	    info.canAnnotate && m_editRefusal == pdf::WriteRefusal::None;
	m_fillAllowed =
	    info.canFillForms && m_editRefusal == pdf::WriteRefusal::None;
	m_exportSelection.clear();
	refreshExportSelection();
	std::vector<helpers::PageSize> sizes;
	sizes.reserve(info.pages.size());
	for (const auto& size : info.pages) {
		sizes.push_back({size.width, size.height});
	}
	const auto page = (std::min)(m_current, sizes.size() - 1);
	m_sizes = std::move(sizes);
	++m_propertiesVersion;
	m_propertiesPending = false;
	m_propertiesFailed = false;
	m_properties.reset();
	m_propertiesPage = page;
	m_propertiesSize = helpers::rotatedSize(m_sizes[page], m_rotation);
	const auto readPage = m_readAloudPage;
	const bool wasPaused = m_readAloudPaused;
	if (readPage) {
		pauseReadAloud();
		++m_readAloudGeneration;
		m_readAloudPending = false;
		m_readAloudWords.clear();
	}
	// the same page number can now hold another page's text
	++m_pageTextVersion;
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
	if (propertiesOpen()) {
		requestProperties();
	}
	if (readPage) {
		readAloudPage((std::min)(*readPage, m_sizes.size() - 1));
		if (!wasPaused && !organizing()) {
			resumeReadAloud();
		}
	}
	refreshNotes();
	showPosition();
	showComments();
}

fire_and_forget ViewerPage::insertPagesFromFile() {
	const auto lifetime = get_strong();
	try {
		if (m_closed || !m_session || pageOperationBusy() || m_noteBusy ||
		    m_noteSaving || !XamlRoot()) {
			co_return;
		}
		Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(
		    XamlRoot().ContentIslandEnvironment().AppWindowId());
		picker.FileTypeFilter().Append(L".pdf");
		const auto file = co_await picker.PickSingleFileAsync();
		if (!file || m_closed || !m_session || pageOperationBusy() ||
		    m_noteBusy || m_noteSaving) {
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
		if (m_closed || !m_session || pageOperationBusy() || m_noteBusy ||
		    m_noteSaving || pages.empty() || !XamlRoot()) {
			co_return;
		}
		m_organizeBusy = true;
		const auto finish = std::shared_ptr<void>(nullptr, [lifetime](void*) {
			lifetime->m_organizeBusy = false;
			if (!lifetime->m_closed) {
				lifetime->showOrganizeState();
			}
		});
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
		    static_cast<helpers::FileSaveResult>(co_await writeExtractedPdf(
		        destination, m_path, std::move(*reply->result.bytes)));
		if (m_closed) {
			co_return;
		}
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
