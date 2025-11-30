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
#include <initguid.h>
#include <xpsobjectmodel_1.h>
#include <xpsprint.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <thread>
#include "printing/source.h"
#include "pdf/engine.h"
#include "pdf_test_files.h"

namespace {
using namespace winrt;
namespace Print = Windows::Graphics::Printing;
struct JobState {
	std::mutex mutex;
	std::condition_variable changed;
	std::vector<XPS_SIZE> pages;
	bool closed = false, canceled = false;
	HRESULT result = E_PENDING;
	bool wait() {
		std::unique_lock lock(mutex);
		return changed.wait_for(lock, std::chrono::seconds(20),
		                        [&] { return closed || canceled; });
	}
};
struct Writer : implements<Writer, IXpsOMPackageWriter> {
	Writer(com_ptr<IXpsOMPackageWriter> value, std::shared_ptr<JobState> status)
	    : actual(std::move(value)), state(std::move(status)) {}
	HRESULT __stdcall StartNewDocument(
	    IOpcPartUri* name, IXpsOMPrintTicketResource* ticket,
	    IXpsOMDocumentStructureResource* structure,
	    IXpsOMSignatureBlockResourceCollection* signatures,
	    IXpsOMPartUriCollection* fonts) noexcept final {
		return actual->StartNewDocument(name, ticket, structure, signatures,
		                                fonts);
	}
	HRESULT __stdcall AddPage(IXpsOMPage* page, const XPS_SIZE* size,
	                          IXpsOMPartUriCollection* discard,
	                          IXpsOMStoryFragmentsResource* story,
	                          IXpsOMPrintTicketResource* ticket,
	                          IXpsOMImageResource* thumbnail) noexcept final {
		const auto result =
		    actual->AddPage(page, size, discard, story, ticket, thumbnail);
		if (SUCCEEDED(result)) {
			std::lock_guard lock(state->mutex);
			state->pages.push_back(*size);
		}
		return result;
	}
	HRESULT __stdcall AddResource(IXpsOMResource* value) noexcept final {
		return actual->AddResource(value);
	}
	HRESULT __stdcall Close() noexcept final {
		const auto result = actual->Close();
		std::lock_guard lock(state->mutex);
		state->closed = true;
		state->result = result;
		state->changed.notify_all();
		return result;
	}
	HRESULT __stdcall IsClosed(BOOL* value) noexcept final {
		return actual->IsClosed(value);
	}
	com_ptr<IXpsOMPackageWriter> actual;
	std::shared_ptr<JobState> state;
};
struct Package : implements<Package, IPrintDocumentPackageTarget,
                            IXpsDocumentPackageTarget> {
	Package() {
		check_hresult(CoCreateInstance(__uuidof(XpsOMObjectFactory), nullptr,
		                               CLSCTX_INPROC_SERVER,
		                               IID_PPV_ARGS(factory.put())));
		check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()));
	}
	HRESULT __stdcall GetPackageTargetTypes(UINT32* count,
	                                        GUID** types) noexcept final {
		if (!count || !types) {
			return E_POINTER;
		}
		*count = 0;
		*types = nullptr;
		if (reject) {
			return E_NOINTERFACE;
		}
		*types = static_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID)));
		if (!*types) {
			return E_OUTOFMEMORY;
		}
		**types = ID_DOCUMENTPACKAGETARGET_MSXPS;
		*count = 1;
		return S_OK;
	}
	HRESULT __stdcall GetPackageTarget(REFGUID type, REFIID iid,
	                                   void** value) noexcept final {
		if (!value) {
			return E_POINTER;
		}
		*value = nullptr;
		if (reject || type != ID_DOCUMENTPACKAGETARGET_MSXPS) {
			return E_NOINTERFACE;
		}
		return QueryInterface(iid, value);
	}
	HRESULT __stdcall Cancel() noexcept final {
		std::lock_guard lock(state->mutex);
		state->canceled = true;
		state->changed.notify_all();
		return S_OK;
	}
	HRESULT __stdcall GetXpsOMPackageWriter(
	    IOpcPartUri* sequence, IOpcPartUri* discard,
	    IXpsOMPackageWriter** result) noexcept final {
		try {
			if (!result) {
				return E_POINTER;
			}
			com_ptr<IXpsOMPackageWriter> actual;
			check_hresult(factory->CreatePackageWriterOnStream(
			    stream.get(), FALSE, XPS_INTERLEAVING_OFF, sequence, nullptr,
			    nullptr, nullptr, discard, actual.put()));
			auto writer = make_self<Writer>(std::move(actual), state);
			writer.as<IXpsOMPackageWriter>().copy_to(result);
			return S_OK;
		} catch (...) {
			return to_hresult();
		}
	}
	HRESULT __stdcall GetXpsOMFactory(
	    IXpsOMObjectFactory** value) noexcept final {
		if (!value) {
			return E_POINTER;
		}
		factory.copy_to(value);
		return S_OK;
	}
	HRESULT __stdcall GetXpsType(XPS_DOCUMENT_TYPE* value) noexcept final {
		if (!value) {
			return E_POINTER;
		}
		*value = XPS_DOCUMENT_TYPE_XPS;
		return S_OK;
	}
	com_ptr<IXpsOMObjectFactory> factory;
	com_ptr<IStream> stream;
	std::shared_ptr<JobState> state = std::make_shared<JobState>();
	bool reject = false;
};
struct JobOptions : implements<JobOptions, Print::IPrintTaskOptionsCore,
                               Print::IPrintTaskOptions2> {
	Print::PrintPageDescription GetPageDescription(std::uint32_t) {
		return description;
	}
	Print::PrintPageDescription description{
	    {800, 600}, {0, 0, 800, 600}, 600, 600};
	Print::PrintPageRangeOptions PageRangeOptions() {
		return nullptr;
	}
	auto CustomPageRanges() {
		return ranges;
	}
	Windows::Foundation::Collections::IVector<Print::PrintPageRange> ranges =
	    single_threaded_vector<Print::PrintPageRange>();
};
struct Trace {
	std::mutex mutex;
	std::vector<pdf::PrintRasterRequest> requests;
};
class PrintJobTest : public ::testing::Test {
  protected:
	void SetUp() override {
		init_apartment(apartment_type::multi_threaded);
		target = make_self<Package>();
		options = make_self<JobOptions>();
	}
	void TearDown() override {
		if (source) {
			source->cancel();
		}
		if (finished.valid()) {
			EXPECT_EQ(finished.wait_for(std::chrono::seconds(10)),
			          std::future_status::ready);
		}
		source = nullptr;
		options = nullptr;
		target = nullptr;
		uninit_apartment();
	}
	HRESULT start(pdf::PrintRenderer renderer,
	              std::vector<pdf::PageSize> pages = {{595, 842}}) {
		auto completed = std::make_shared<std::promise<void>>();
		finished = completed->get_future();
		source = make_self<printing::Source>(
		    std::move(renderer), std::move(pages), 0,
		    [completed] { completed->set_value(); });
		const auto value = options.as<Print::PrintTaskOptions>();
		return source->MakeDocument(
		    reinterpret_cast<::IInspectable*>(get_abi(value)), target.get());
	}
	com_ptr<Package> target;
	com_ptr<JobOptions> options;
	com_ptr<printing::Source> source;
	std::future<void> finished;
};
} // namespace

TEST_F(PrintJobTest, WritesSelectedPdfPagesOnceToRealXpsAtPrinterDpi) {
	pdf::Engine engine;
	pdf::Session session(engine);
	auto opened = std::make_shared<std::promise<pdf::SessionInfo>>();
	auto future = opened->get_future();
	ASSERT_TRUE(session.open(fixturePath("pages.pdf"), {}, [opened](auto info) {
		opened->set_value(std::move(info));
	}));
	ASSERT_EQ(future.wait_for(std::chrono::seconds(10)),
	          std::future_status::ready);
	const auto info = future.get();
	ASSERT_EQ(info.error, pdf::Error::None);
	options->ranges.Append(Print::PrintPageRange(1, 2));
	auto trace = std::make_shared<Trace>();
	const auto renderer = session.printRenderer();
	ASSERT_EQ(start(
	              [renderer, trace](auto request, auto canceled, auto done) {
		              {
			              std::lock_guard lock(trace->mutex);
			              trace->requests.push_back(request);
		              }
		              return renderer(request, std::move(canceled),
		                              std::move(done));
	              },
	              info.pages),
	          S_OK);
	ASSERT_TRUE(target->state->wait());
	EXPECT_FALSE(source->failed());
	{
		std::lock_guard lock(target->state->mutex);
		ASSERT_TRUE(target->state->closed);
		EXPECT_EQ(target->state->result, S_OK);
		ASSERT_EQ(target->state->pages.size(), 2u);
		for (const auto& size : target->state->pages) {
			EXPECT_FLOAT_EQ(size.width, 800);
			EXPECT_FLOAT_EQ(size.height, 600);
		}
	}
	STATSTG stat{};
	ASSERT_EQ(target->stream->Stat(&stat, STATFLAG_NONAME), S_OK);
	EXPECT_GT(stat.cbSize.QuadPart, 0u);
	ASSERT_EQ(target->stream->Seek({}, STREAM_SEEK_SET, nullptr), S_OK);
	com_ptr<IXpsOMPackage> package;
	ASSERT_EQ(target->factory->CreatePackageFromStream(target->stream.get(),
	                                                   FALSE, package.put()),
	          S_OK);
	com_ptr<IXpsOMDocumentSequence> sequence;
	ASSERT_EQ(package->GetDocumentSequence(sequence.put()), S_OK);
	com_ptr<IXpsOMDocumentCollection> documents;
	ASSERT_EQ(sequence->GetDocuments(documents.put()), S_OK);
	UINT32 count = 0;
	ASSERT_EQ(documents->GetCount(&count), S_OK);
	ASSERT_EQ(count, 1u);
	com_ptr<IXpsOMDocument> document;
	ASSERT_EQ(documents->GetAt(0, document.put()), S_OK);
	com_ptr<IXpsOMPageReferenceCollection> references;
	ASSERT_EQ(document->GetPageReferences(references.put()), S_OK);
	ASSERT_EQ(references->GetCount(&count), S_OK);
	EXPECT_EQ(count, 2u);
	std::lock_guard lock(trace->mutex);
	std::array<std::uint64_t, 2> areas{};
	for (const auto& request : trace->requests) {
		ASSERT_GE(request.page, 0);
		ASSERT_LT(request.page, 2);
		const auto layout = pdf::layoutPrint(
		    info.pages[request.page], {800, 600, 0, 0, 800, 600}, 600, 600);
		ASSERT_TRUE(layout);
		EXPECT_EQ(request.width, layout->rasterWidth);
		EXPECT_EQ(request.height, layout->rasterHeight);
		EXPECT_LE(request.tile.width, 2048);
		EXPECT_LE(request.tile.height, 2048);
		areas[request.page] += static_cast<std::uint64_t>(request.tile.width) *
		                       request.tile.height;
	}
	for (int page = 0; page < 2; ++page) {
		const auto layout = pdf::layoutPrint(
		    info.pages[page], {800, 600, 0, 0, 800, 600}, 600, 600);
		EXPECT_EQ(areas[page], static_cast<std::uint64_t>(layout->rasterWidth) *
		                           layout->rasterHeight);
	}
}

TEST_F(PrintJobTest, ClosingDuringPendingRasterCancelsWithoutWritingAPage) {
	struct Held {
		std::function<bool()> canceled;
		std::function<void(std::optional<pdf::Bitmap>)> done;
	};
	auto requested = std::make_shared<std::promise<Held>>();
	auto future = requested->get_future();
	ASSERT_EQ(start([requested](auto, auto canceled, auto done) {
		          requested->set_value({std::move(canceled), std::move(done)});
		          return true;
	          }),
	          S_OK);
	ASSERT_EQ(future.wait_for(std::chrono::seconds(10)),
	          std::future_status::ready);
	const auto held = future.get();
	source->cancel();
	EXPECT_TRUE(held.canceled());
	held.done(std::nullopt);
	ASSERT_TRUE(target->state->wait());
	EXPECT_FALSE(source->failed());
	std::lock_guard lock(target->state->mutex);
	EXPECT_TRUE(target->state->canceled);
	EXPECT_TRUE(target->state->pages.empty());
}

TEST_F(PrintJobTest, UnsupportedJobTargetFailsBeforeRequestingPdfPixels) {
	target->reject = true;
	auto called = std::make_shared<std::atomic<bool>>(false);
	ASSERT_EQ(start([called](auto, auto, auto) {
		          *called = true;
		          return false;
	          }),
	          S_OK);
	ASSERT_TRUE(target->state->wait());
	EXPECT_TRUE(source->failed());
	EXPECT_FALSE(*called);
}

// This legacy bridge is confined to the opt-in driver diagnostic below.
#pragma warning(push)
#pragma warning(disable : 4995)
namespace {
// This diagnostic only sends jobs to the named virtual printer, with an
// explicit new output file. It never uses the default printer.
struct VirtualJob {
	HMODULE module =
	    LoadLibraryExW(L"XpsPrint.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	handle completed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
	com_ptr<IXpsPrintJob> job;
	com_ptr<IXpsPrintJobStream> document, ticket;
	~VirtualJob() {
		if (job) {
			XPS_JOB_STATUS status{};
			if (FAILED(job->GetJobStatus(&status)) ||
			    status.completion == XPS_JOB_IN_PROGRESS) {
				job->Cancel();
				if (document) {
					document->Close();
				}
				if (ticket) {
					ticket->Close();
				}
				WaitForSingleObject(completed.get(), 30000);
			}
		}
		document = nullptr;
		ticket = nullptr;
		job = nullptr;
		if (module) {
			FreeLibrary(module);
		}
	}
};
void writeVirtualPdf(IStream* input, const std::filesystem::path& output,
                     bool landscape) {
	ASSERT_FALSE(std::filesystem::exists(output));
	VirtualJob printing;
	ASSERT_NE(printing.module, nullptr);
	ASSERT_TRUE(printing.completed);
	const auto start = reinterpret_cast<decltype(&StartXpsPrintJob)>(
	    GetProcAddress(printing.module, "StartXpsPrintJob"));
	ASSERT_NE(start, nullptr);
	// The older XPS entry point is used only to give this diagnostic an
	// explicit filename. The reader uses the modern Windows print UI.
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/api/xpsprint/nf-xpsprint-startxpsprintjob
	ASSERT_EQ(start(L"Microsoft Print to PDF", L"Sable virtual output check",
	                output.c_str(), nullptr, printing.completed.get(), nullptr,
	                0, printing.job.put(), printing.document.put(),
	                printing.ticket.put()),
	          S_OK);
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/printdocs/pageorientation
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/printdocs/pagemediasize
	const std::string ticket =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	    "<psf:PrintTicket version=\"1\" "
	    "xmlns:psf=\"http://schemas.microsoft.com/windows/2003/08/printing/"
	    "printschemaframework\" "
	    "xmlns:psk=\"http://schemas.microsoft.com/windows/2003/08/printing/"
	    "printschemakeywords\">"
	    "<psf:Feature name=\"psk:PageMediaSize\">"
	    "<psf:Option name=\"psk:NorthAmericaLetter\"/></psf:Feature>"
	    "<psf:Feature name=\"psk:PageOrientation\">"
	    "<psf:Option name=\"psk:" +
	    std::string(landscape ? "Landscape" : "Portrait") +
	    "\"/></psf:Feature></psf:PrintTicket>";
	ULONG written = 0;
	ASSERT_EQ(printing.ticket->Write(
	              ticket.data(), static_cast<ULONG>(ticket.size()), &written),
	          S_OK);
	ASSERT_EQ(written, ticket.size());
	ASSERT_EQ(printing.ticket->Close(), S_OK);
	ASSERT_EQ(input->Seek({}, STREAM_SEEK_SET, nullptr), S_OK);
	std::array<std::uint8_t, 65536> buffer{};
	for (;;) {
		ULONG read = 0;
		ASSERT_HRESULT_SUCCEEDED(input->Read(
		    buffer.data(), static_cast<ULONG>(buffer.size()), &read));
		if (!read) {
			break;
		}
		ASSERT_EQ(printing.document->Write(buffer.data(), read, &written),
		          S_OK);
		ASSERT_EQ(written, read);
	}
	ASSERT_EQ(printing.document->Close(), S_OK);
	ASSERT_EQ(WaitForSingleObject(printing.completed.get(), 30000),
	          WAIT_OBJECT_0);
	XPS_JOB_STATUS status{};
	ASSERT_EQ(printing.job->GetJobStatus(&status), S_OK);
	ASSERT_EQ(status.completion, XPS_JOB_COMPLETED)
	    << "driver result: " << std::hex << status.jobStatus;
	ASSERT_TRUE(std::filesystem::exists(output));
}
} // namespace
#pragma warning(pop)

// Explicit opt-in: this uses the installed Windows PDF driver and keeps the
// resulting files in the test's unique temp directory for visual review.
TEST_F(PrintJobTest,
       DISABLED_MicrosoftPdfOutputKeepsColorsRangesAndOrientation) {
	const auto directory = makeTempDir("virtual_print");
	std::wcout << L"Virtual PDF output: " << directory.native() << L'\n';
	pdf::Engine engine;
	for (const bool landscape : {false, true}) {
		pdf::Session session(engine);
		auto opened = std::make_shared<std::promise<pdf::SessionInfo>>();
		auto future = opened->get_future();
		ASSERT_TRUE(session.open(
		    fixturePath(landscape ? "pages.pdf" : "print.pdf"), {},
		    [opened](auto info) { opened->set_value(std::move(info)); }));
		ASSERT_EQ(future.wait_for(std::chrono::seconds(10)),
		          std::future_status::ready);
		const auto info = future.get();
		ASSERT_EQ(info.error, pdf::Error::None);
		target = make_self<Package>();
		options->ranges.Clear();
		const float width = landscape ? 1056.f : 816.f;
		const float height = landscape ? 816.f : 1056.f;
		options->description = {
		    {width, height}, {0, 0, width, height}, 300, 300};
		if (landscape) {
			options->ranges.Append(Print::PrintPageRange(2, 3));
		}
		ASSERT_EQ(start(session.printRenderer(), info.pages), S_OK);
		ASSERT_TRUE(target->state->wait());
		const auto deadline =
		    std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!source->jobComplete() &&
		       std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		ASSERT_TRUE(source->jobComplete());
		ASSERT_FALSE(source->failed());
		const auto output =
		    directory / (landscape ? "range.pdf" : "colors.pdf");
		ASSERT_NO_FATAL_FAILURE(
		    writeVirtualPdf(target->stream.get(), output, landscape));
		engine
		    .run([&] {
			    const auto reopened = pdf::openFile(output);
			    ASSERT_TRUE(reopened.document);
			    ASSERT_EQ(reopened.document->pageCount(), landscape ? 2 : 1);
			    for (int page = 0; page < reopened.document->pageCount();
			         ++page) {
				    const auto size = reopened.document->pageSize(page);
				    ASSERT_TRUE(size);
				    EXPECT_NEAR(size->width, width * .75f, 1);
				    EXPECT_NEAR(size->height, height * .75f, 1);
				    const auto bitmap =
				        reopened.document->render(page, static_cast<int>(width),
				                                  static_cast<int>(height));
				    ASSERT_TRUE(bitmap);
				    const int original = landscape ? page + 1 : 0;
				    const auto layout = pdf::layoutPrint(
				        info.pages[original],
				        {width, height, 0, 0, width, height}, 300, 300);
				    ASSERT_TRUE(layout);
				    const auto check = [&](double x, double y,
				                           std::array<int, 3> color) {
					    const auto column =
					        static_cast<int>(layout->x + x * layout->width);
					    const auto row =
					        static_cast<int>(layout->y + y * layout->height);
					    const auto at =
					        static_cast<std::size_t>(row) * bitmap->stride +
					        column * 4;
					    for (int channel = 0; channel < 3; ++channel) {
						    EXPECT_NEAR(bitmap->pixels.at(at + channel),
						                color[channel], 8)
						        << "output page " << page << ", point " << x
						        << ',' << y << ", channel " << channel;
					    }
				    };
				    if (!landscape) {
					    check(.2, .8, {0, 0, 255});
					    check(.6, .8, {0, 255, 0});
					    check(.2, .3, {255, 255, 255});
				    } else if (page == 0) {
					    check(200. / 792, 412. / 612, {0, 255, 0});
				    } else {
					    check(.5, .2, {0, 0, 0});
					    check(.5, .8, {255, 255, 255});
				    }
			    }
		    })
		    .get();
		source->cancel(false);
		source = nullptr;
	}
}
