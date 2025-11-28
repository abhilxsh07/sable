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
#include <gtest/gtest.h>
#include <dxgi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <chrono>
#include <condition_variable>
#include <limits>
#include "printing/source.h"

namespace {
using namespace winrt;
namespace Print = Windows::Graphics::Printing;
struct Options
    : implements<Options, Print::IPrintTaskOptions, Print::IPrintTaskOptions2,
                 Print::IPrintTaskOptionsCore> {
	Print::PrintBordering Bordering() {
		return Print::PrintBordering::Default;
	}
	void Bordering(Print::PrintBordering) {}
	Windows::Storage::Streams::IRandomAccessStream
	GetPagePrintTicket(const Print::PrintPageInfo&) {
		return nullptr;
	}
	Print::PrintPageRangeOptions PageRangeOptions() {
		return nullptr;
	}
	auto CustomPageRanges() {
		return ranges;
	}
	Print::PrintPageDescription GetPageDescription(std::uint32_t page) {
		described.push_back(page);
		return description;
	}
	Windows::Foundation::Collections::IVector<Print::PrintPageRange> ranges =
	    single_threaded_vector<Print::PrintPageRange>();
	Print::PrintPageDescription description{
	    {600, 800}, {0, 0, 600, 800}, 300, 300};
	std::vector<std::uint32_t> described;
};
struct Target : implements<Target, IPrintDocumentPackageTarget,
                           IPrintPreviewDxgiPackageTarget> {
	HRESULT __stdcall GetPackageTargetTypes(UINT32* count,
	                                        GUID** types) noexcept final {
		if (!count || !types) {
			return E_POINTER;
		}
		*count = 0;
		*types = nullptr;
		return E_NOTIMPL;
	}
	HRESULT __stdcall GetPackageTarget(REFGUID type, REFIID iid,
	                                   void** value) noexcept final {
		if (type != ID_PREVIEWPACKAGETARGET_DXGI) {
			return E_NOINTERFACE;
		}
		return QueryInterface(iid, value);
	}
	HRESULT __stdcall Cancel() noexcept final {
		++canceled;
		return S_OK;
	}
	HRESULT __stdcall SetJobPageCount(PageCountType type,
	                                  UINT32 count) noexcept final {
		countType = type;
		pageCount = count;
		return S_OK;
	}
	HRESULT __stdcall DrawPage(UINT32 page, IDXGISurface* surface, FLOAT x,
	                           FLOAT y) noexcept final {
		DXGI_SURFACE_DESC description{};
		if (!surface) {
			return E_POINTER;
		}
		const auto result = surface->GetDesc(&description);
		if (FAILED(result)) {
			return result;
		}
		std::lock_guard lock(mutex);
		drawn.push_back(page);
		size = description;
		dpiX = x;
		dpiY = y;
		changed.notify_all();
		return S_OK;
	}
	HRESULT __stdcall InvalidatePreview() noexcept final {
		++invalidated;
		return S_OK;
	}
	bool waitDraw() {
		std::unique_lock lock(mutex);
		return changed.wait_for(lock, std::chrono::seconds(10),
		                        [&] { return !drawn.empty(); });
	}
	std::mutex mutex;
	std::condition_variable changed;
	std::vector<UINT32> drawn;
	DXGI_SURFACE_DESC size{};
	float dpiX = 0, dpiY = 0;
	PageCountType countType{};
	std::atomic<UINT32> pageCount{0}, canceled{0}, invalidated{0};
};
struct Requests {
	struct Item {
		pdf::PrintRasterRequest request;
		std::function<bool()> canceled;
		std::function<void(std::optional<pdf::Bitmap>)> done;
	};
	pdf::PrintRenderer renderer() {
		return [this](auto request, auto canceled, auto done) {
			std::lock_guard lock(mutex);
			items.push_back({request, std::move(canceled), std::move(done)});
			changed.notify_all();
			return true;
		};
	}
	Item take(std::size_t index) {
		std::unique_lock lock(mutex);
		if (!changed.wait_for(lock, std::chrono::seconds(10),
		                      [&] { return items.size() > index; })) {
			ADD_FAILURE() << "the raster request never arrived";
			return {};
		}
		return items[index];
	}
	static void finish(Item item) {
		const auto& tile = item.request.tile;
		pdf::Bitmap bitmap{tile.width, tile.height, tile.width * 4, {}};
		bitmap.pixels.resize(
		    static_cast<std::size_t>(bitmap.stride) * tile.height, 255);
		if (item.done) {
			item.done(std::move(bitmap));
		}
	}
	std::mutex mutex;
	std::condition_variable changed;
	std::vector<Item> items;
};
class PrintSourceTest : public ::testing::Test {
  protected:
	void SetUp() override {
		init_apartment(apartment_type::multi_threaded);
	}
	void TearDown() override {
		if (source) {
			source->cancel();
		}
		source = nullptr;
		options = nullptr;
		target = nullptr;
		uninit_apartment();
	}
	void create(int count = 3, int current = 0) {
		options = make_self<Options>();
		target = make_self<Target>();
		source = make_self<printing::Source>(
		    requests.renderer(), std::vector<pdf::PageSize>(count, {300, 400}),
		    current);
		com_ptr<IPrintPreviewPageCollection> collection;
		ASSERT_EQ(
		    source->GetPreviewPageCollection(target.get(), collection.put()),
		    S_OK);
		ASSERT_TRUE(collection);
	}
	HRESULT paginate() {
		const auto value = options.as<Print::PrintTaskOptions>();
		return source->Paginate(
		    1, reinterpret_cast<IInspectable*>(get_abi(value)));
	}
	Requests requests;
	com_ptr<Options> options;
	com_ptr<Target> target;
	com_ptr<printing::Source> source;
};
} // namespace

TEST_F(PrintSourceTest, ExposesNativeDocumentAndPreviewInterfaces) {
	create();
	EXPECT_TRUE(source.try_as<Print::IPrintDocumentSource>());
	EXPECT_TRUE(source.try_as<IPrintDocumentPageSource>());
	EXPECT_TRUE(source.try_as<IPrintPreviewPageCollection>());
	EXPECT_EQ(paginate(), S_OK);
	EXPECT_EQ(target->pageCount, 3u);
	EXPECT_EQ(target->countType, FinalPageCount);
	EXPECT_TRUE(requests.items.empty());
	source->invalidate();
	EXPECT_EQ(target->invalidated, 1u);
}

TEST_F(PrintSourceTest, RangesKeepDocumentPageNumbersAndLandscapeDescription) {
	create();
	options->ranges.Append(Print::PrintPageRange(2, 3));
	options->description = {{800, 600}, {10, 20, 780, 560}, 600, 300};
	ASSERT_EQ(paginate(), S_OK);
	EXPECT_EQ(target->pageCount, 3u);
	ASSERT_EQ(source->MakePage(2, 400, 300), S_OK);
	const auto item = requests.take(0);
	EXPECT_EQ(item.request.page, 1);
	EXPECT_EQ(options->described, (std::vector<std::uint32_t>{2}));
	Requests::finish(item);
	ASSERT_TRUE(target->waitDraw());
	std::lock_guard lock(target->mutex);
	EXPECT_EQ(target->drawn, (std::vector<UINT32>{2}));
	EXPECT_EQ(target->size.Width, 400u);
	EXPECT_EQ(target->size.Height, 300u);
	EXPECT_FLOAT_EQ(target->dpiX, 48);
	EXPECT_FLOAT_EQ(target->dpiY, 48);
}

TEST_F(PrintSourceTest, DiscardsSupersededPagesAndCanceledCallbacks) {
	create();
	ASSERT_EQ(paginate(), S_OK);
	ASSERT_EQ(source->MakePage(1, 300, 400), S_OK);
	auto old = requests.take(0);
	ASSERT_EQ(source->MakePage(2, 300, 400), S_OK);
	auto current = requests.take(1);
	EXPECT_TRUE(old.canceled());
	Requests::finish(old);
	Requests::finish(current);
	ASSERT_TRUE(target->waitDraw());
	ASSERT_EQ(source->MakePage(3, 300, 400), S_OK);
	auto closed = requests.take(2);
	source->cancel();
	EXPECT_TRUE(closed.canceled());
	Requests::finish(closed);
	EXPECT_EQ(target->canceled, 1u);
	std::lock_guard lock(target->mutex);
	EXPECT_EQ(target->drawn, (std::vector<UINT32>{2}));
}

TEST_F(PrintSourceTest, ValidatesPointersBoundsAndPageNumbers) {
	create();
	com_ptr<IPrintPreviewPageCollection> collection;
	EXPECT_EQ(source->GetPreviewPageCollection(nullptr, collection.put()),
	          E_POINTER);
	EXPECT_EQ(source->GetPreviewPageCollection(target.get(), nullptr),
	          E_POINTER);
	EXPECT_EQ(source->Paginate(1, nullptr), E_POINTER);
	ASSERT_EQ(paginate(), S_OK);
	EXPECT_EQ(source->MakePage(0, 300, 400), E_INVALIDARG);
	EXPECT_EQ(source->MakePage(4, 300, 400), E_INVALIDARG);
	for (const float size :
	     {0.f, -1.f, 8193.f, std::numeric_limits<float>::quiet_NaN()}) {
		EXPECT_EQ(source->MakePage(1, size, 400), E_INVALIDARG);
		EXPECT_EQ(source->MakePage(1, 300, size), E_INVALIDARG);
	}
	EXPECT_TRUE(requests.items.empty());
}

TEST_F(PrintSourceTest, ThousandPagePaginationOnlyRendersRequestedCurrentPage) {
	create(1000, 999);
	ASSERT_EQ(paginate(), S_OK);
	EXPECT_EQ(target->pageCount, 1000u);
	EXPECT_TRUE(requests.items.empty());
	ASSERT_EQ(source->MakePage(JOB_PAGE_APPLICATION_DEFINED, 300, 400), S_OK);
	const auto item = requests.take(0);
	EXPECT_EQ(item.request.page, 999);
	Requests::finish(item);
	ASSERT_TRUE(target->waitDraw());
	std::lock_guard lock(requests.mutex);
	EXPECT_EQ(requests.items.size(), 1u);
}
