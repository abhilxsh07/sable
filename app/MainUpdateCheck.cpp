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
#include "SettingsWindow.h"
#include "helpers/logging.h"
#include "helpers/update_check.h"
#include "helpers/version.h"
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <atomic>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
namespace {
// once per process, like NightPDF after Q6 (guide §5): its check ran for
// every new window
std::atomic_bool checked{false};
// GitHub's list of ten releases is a few kB; anything far bigger is wrong
constexpr std::uint32_t maxResponse = 1u << 20;

void note(const std::string& text) {
	diagnostics::write(diagnostics::Level::Info, "Update check: " + text);
}
} // namespace

// One GET to GitHub's release list, off the UI thread. GitHub sees the IP
// address and the "Sable/<version>" user agent and nothing else. Failures
// stay quiet and go to the log.
fire_and_forget MainWindow::checkForUpdates() {
#ifdef SABLE_SURFACE_TEST
	co_return;
#else
	if (checked.exchange(true)) {
		co_return;
	}
	const auto weak = get_weak();
	const auto queue = DispatcherQueue();
	const auto file = ::SettingsWindow::s_updateCheckFile;
	const auto current = helpers::appVersion();
	co_await resume_background();
	if (!helpers::loadUpdateCheck(file).enabled) {
		note("turned off");
		co_return;
	}
	std::optional<helpers::AvailableRelease> found;
	try {
		Windows::Web::Http::HttpClient client;
		const auto headers = client.DefaultRequestHeaders();
		headers.UserAgent().TryParseAdd(L"Sable/" + to_hstring(current));
		headers.Accept().TryParseAdd(L"application/vnd.github+json");
		const auto response = co_await client.GetAsync(
		    Windows::Foundation::Uri{helpers::updateCheckUrl});
		if (!response.IsSuccessStatusCode()) {
			note("GitHub answered " +
			     std::to_string(static_cast<int>(response.StatusCode())));
			co_return;
		}
		const auto content = response.Content();
		if (const auto length = content.Headers().ContentLength();
		    length && length.Value() > maxResponse) {
			note("response too large");
			co_return;
		}
		const auto body = co_await content.ReadAsStringAsync();
		if (body.size() > maxResponse) {
			note("response too large");
			co_return;
		}
		found = helpers::newerRelease(to_string(body), current);
	} catch (...) {
		note("failed: " + to_string(to_message()));
		co_return;
	}
	if (!found) {
		note("no newer release than " + current);
		co_return;
	}
	note(found->tag + " is available");
	// the window can close while GitHub answers; its queue then refuses
	try {
		co_await wil::resume_foreground(queue);
		if (const auto self = weak.get()) {
			self->showUpdate(to_hstring(found->tag), to_hstring(found->page));
		}
	} catch (...) {
		note("window closed before the notice");
	}
#endif
}

void MainWindow::showUpdate(const hstring& tag, const hstring& page) {
	UpdateNotice().Title(L"Sable " + tag + L" is available");
	UpdateNotice().Message(L"You have " + to_hstring(helpers::appVersion()) +
	                       L".");
	UpdatePage().NavigateUri(Windows::Foundation::Uri{page});
	UpdateNotice().IsOpen(true);
}
} // namespace winrt::Sable::implementation
