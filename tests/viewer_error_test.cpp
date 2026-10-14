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
#include "../app/pch.h"
#include "../app/ViewerPage.xaml.h"
#include "../app/pdf/engine.h"
#include "../app/pdf/session.h"
#include "surface_test.h"

#include <winrt/Microsoft.UI.Windowing.h>

#include <array>
#include <chrono>
#include <memory>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
namespace {
struct ErrorCase {
	const wchar_t* name;
	const wchar_t* file;
	pdf::Error error;
	const wchar_t* message;
};

constexpr std::array CASES = {
    ErrorCase{L"missing", L"tests/fixtures/not-there.pdf", pdf::Error::File,
              L"Couldn't read the file."},
    ErrorCase{L"empty", L"tests/fixtures/empty.pdf", pdf::Error::Format,
              L"This isn't a PDF, or it's too damaged to open."},
    ErrorCase{L"plain text", L"tests/fixtures/not_pdf.pdf", pdf::Error::Format,
              L"This isn't a PDF, or it's too damaged to open."},
    ErrorCase{L"damaged", L"tests/fixtures/broken.pdf", pdf::Error::Format,
              L"This isn't a PDF, or it's too damaged to open."},
    ErrorCase{L"no pages", L"tests/fixtures/no_pages.pdf", pdf::Error::None,
              L"This PDF has no pages."},
    // these results are injected; the real fixtures don't produce them
    ErrorCase{L"unsupported protection", nullptr, pdf::Error::Security,
              L"This PDF is protected in a way Sable can't open."},
    ErrorCase{L"unknown", nullptr, pdf::Error::Unknown,
              L"Something went wrong opening this PDF."},
};
constexpr int CANCELED = static_cast<int>(CASES.size());
constexpr int CLOSED = CANCELED + 1;
constexpr int PASSWORD = CLOSED + 1;
constexpr int WRONG_PASSWORD = PASSWORD + 1;
constexpr int NORMAL = WRONG_PASSWORD + 1;
} // namespace

// the real ViewerPage, including its dispatcher and XAML error state
struct ViewerErrorTest {
	explicit ViewerErrorTest(pdf::Engine& pdfEngine) : engine(pdfEngine) {}
	pdf::Engine& engine;
	com_ptr<ViewerPage> viewer;
	TextBlock status;
	ContentControl host;
	Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
	int step = -1;
	int ticks = 0;
	bool canceling = false;

	void fail(const hstring& why) {
		status.Text(L"FAIL: case " + to_hstring(step) + L": " + why);
		timer.Stop();
	}

	void next() {
		if (viewer) {
			viewer->close();
		}
		host.Content(nullptr);
		viewer = make_self<ViewerPage>();
		host.Content(viewer.as<Sable::ViewerPage>());
		ticks = 0;
		canceling = false;
		++step;
		if (step < CANCELED) {
			const auto& test = CASES[static_cast<std::size_t>(step)];
			status.Text(L"Checking " + hstring(test.name));
			if (test.file) {
				viewer->open(engine, test.file, std::nullopt, true);
			} else {
				pdf::SessionInfo info;
				info.error = test.error;
				viewer->opened(std::move(info));
			}
		} else if (step == CANCELED) {
			status.Text(L"Checking a canceled password result");
			pdf::SessionInfo info;
			info.error = pdf::Error::PasswordRequired;
			info.canceled = true;
			viewer->opened(std::move(info));
		} else if (step == CLOSED) {
			status.Text(L"Checking results after close");
			viewer->open(engine, L"tests/fixtures/broken.pdf", std::nullopt,
			             true);
			viewer->close();
			for (const auto error :
			     {pdf::Error::None, pdf::Error::File, pdf::Error::Format,
			      pdf::Error::PasswordRequired, pdf::Error::WrongPassword,
			      pdf::Error::Security, pdf::Error::Unknown}) {
				pdf::SessionInfo info;
				info.error = error;
				viewer->opened(std::move(info));
			}
			viewer->showMessage(L"late error");
			viewer->rendered({{}, std::nullopt, false});
		} else if (step == PASSWORD || step == WRONG_PASSWORD) {
			status.Text(L"Checking password prompt cancellation");
			viewer->m_engine = &engine;
			viewer->m_path = L"tests/fixtures/password.pdf";
			pdf::SessionInfo info;
			info.error = step == PASSWORD ? pdf::Error::PasswordRequired
			                              : pdf::Error::WrongPassword;
			viewer->opened(std::move(info));
		} else {
			status.Text(L"Checking a normal document");
			viewer->open(engine, L"tests/fixtures/pages.pdf", std::nullopt,
			             true);
		}
	}

	void tick() {
		try {
			if (step < 0) {
				next();
				return;
			}
			if (++ticks > 40) {
				fail(L"timed out");
				return;
			}
			if (step == CANCELED || step == CLOSED) {
				if (viewer->Message().IsOpen() || viewer->m_passwordDialog ||
				    !viewer->Message().Message().empty()) {
					fail(L"ignored result changed the viewer");
					return;
				}
				next();
				return;
			}
			if (step == NORMAL) {
				if (!viewer->m_layout || viewer->m_rendering) {
					return;
				}
				if (viewer->Message().IsOpen() ||
				    viewer->Scroller().Visibility() != Visibility::Visible ||
				    viewer->Rail().Visibility() != Visibility::Visible ||
				    !viewer->thumbnailsOpen()) {
					fail(L"normal document hidden by an error");
					return;
				}
				status.Text(L"PASS: " + to_hstring(NORMAL + 1) +
				            L" viewer cases; errors, passwords, late results");
				timer.Stop();
				return;
			}
			if ((step == PASSWORD || step == WRONG_PASSWORD) && !canceling) {
				if (!viewer->m_passwordDialog) {
					if (viewer->Message().IsOpen()) {
						fail(L"password result bypassed its prompt");
					}
					return;
				}
				viewer->m_passwordDialog.Hide();
				canceling = true;
				return;
			}
			if (!viewer->Message().IsOpen()) {
				return;
			}
			const hstring expected =
			    step < CANCELED ? CASES[static_cast<std::size_t>(step)].message
			                    : L"This PDF is protected with a password.";
			if (viewer->Message().Message() != expected ||
			    viewer->Message().Severity() != InfoBarSeverity::Error ||
			    viewer->Message().IsClosable() ||
			    viewer->Message().Visibility() != Visibility::Visible ||
			    viewer->Scroller().Visibility() != Visibility::Collapsed ||
			    viewer->Rail().Visibility() != Visibility::Collapsed ||
			    viewer->ThumbnailPanel().Visibility() !=
			        Visibility::Collapsed) {
				fail(L"wrong message or error layout: " +
				     viewer->Message().Message());
				return;
			}
			next();
		} catch (...) {
			fail(to_message());
		}
	}
};
} // namespace winrt::Sable::implementation

Window viewerErrorTest(pdf::Engine& engine) {
	Window window;
	window.Title(L"Sable viewer error test");
	window.AppWindow().Resize({960, 600});
	auto state =
	    std::make_shared<Sable::implementation::ViewerErrorTest>(engine);
	std::weak_ptr<Sable::implementation::ViewerErrorTest> weak = state;
	Grid grid;
	RowDefinition top;
	top.Height({60, GridUnitType::Pixel});
	grid.RowDefinitions().Append(top);
	grid.RowDefinitions().Append(RowDefinition());
	state->status.Margin({16, 16, 16, 16});
	state->status.Text(L"Waiting for layout");
	grid.Children().Append(state->status);
	Grid::SetRow(state->host, 1);
	grid.Children().Append(state->host);
	window.Content(grid);
	window.Closed([state](auto&&, auto&&) {
		state->timer.Stop();
		if (state->viewer) {
			state->viewer->close();
		}
		state->host.Content(nullptr);
	});
	state->timer = window.DispatcherQueue().CreateTimer();
	state->timer.Interval(std::chrono::milliseconds(250));
	state->timer.Tick([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->tick();
		}
	});
	state->timer.Start();
	return window;
}
