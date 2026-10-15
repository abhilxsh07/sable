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
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "helpers/cli.h"
#include "helpers/version.h"
#ifdef SABLE_SURFACE_TEST
#include "../tests/surface_test.h"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace {
// everything after the program name, as UTF-8 for helpers::parseCli
std::vector<std::string> commandLineArgs() {
	std::vector<std::string> args;
	int count = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
	if (!argv) {
		return args;
	}
	const std::unique_ptr<wchar_t*, decltype(&LocalFree)> owned(argv,
	                                                            &LocalFree);
	for (int i = 1; i < count; ++i) {
		args.push_back(to_string(argv[i]));
	}
	return args;
}

// preserve pipe/file handles; a GUI launch may instead need the parent console
// see: https://learn.microsoft.com/en-us/windows/console/attachconsole
// see: https://learn.microsoft.com/en-us/windows/console/writeconsole
bool writeCli(const std::string& text) {
	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!output || output == INVALID_HANDLE_VALUE ||
	    GetFileType(output) == FILE_TYPE_UNKNOWN) {
		AttachConsole(ATTACH_PARENT_PROCESS);
		output = GetStdHandle(STD_OUTPUT_HANDLE);
	}
	DWORD mode = 0;
	DWORD written = 0;
	if (GetConsoleMode(output, &mode)) {
		const auto wide = to_hstring(text);
		return WriteConsoleW(output, wide.c_str(), wide.size(), &written,
		                     nullptr) &&
		       written == wide.size();
	}
	return WriteFile(output, text.data(), static_cast<DWORD>(text.size()),
	                 &written, nullptr) &&
	       written == text.size();
}
} // namespace

namespace winrt::Sable::implementation {
App::App() {
	// don't call InitializeComponent here, XAML objects aren't ready yet
#if defined _DEBUG &&                                                          \
    !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
	UnhandledException(
	    [](IInspectable const&, UnhandledExceptionEventArgs const& e) {
#ifdef SABLE_SURFACE_TEST
		    probeFail(L"FAIL: unhandled XAML exception: " + e.Message());
#endif
		    if (IsDebuggerPresent()) {
			    auto errorMessage = e.Message();
			    __debugbreak();
		    }
	    });
#endif
}

void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e) {
#ifdef SABLE_SURFACE_TEST
	Window window{nullptr};
	try {
		m_pdf = std::make_unique<pdf::Engine>();
		window = surfaceTest(*m_pdf, *this);
	} catch (...) {
		probeFail(L"FAIL: probe startup: " + to_message());
	}
	trackWindow(window);
	window.Closed([](auto&&, auto&&) { probeClosed(); });
	window.Activate();
#else
	// port of app.ts's yargs dispatch/startup (L348-410)
	const auto cli = helpers::parseCli(commandLineArgs());
	SYSTEMTIME now{};
	GetLocalTime(&now);
	if (const auto text = helpers::cliText(
	        cli, "Sable", helpers::versionString(), now.wYear)) {
		if (!writeCli(*text + "\n")) {
			OutputDebugStringW(L"Couldn't write CLI output.\n");
			// no document, engine or window has been created here
			ExitProcess(1);
		}
		Exit();
		return;
	}
	std::vector<std::filesystem::path> files;
	for (const auto& file : cli.files) {
		files.emplace_back(to_hstring(file).c_str());
	}
	createWindow(files, cli.page);
#endif
}

// port of app/main/app.ts createWindow() and closed handler (L127-184).
// i remove only the closed window, rather than clearing all of them (Q5).
void App::trackWindow(const Window& window) {
	window.Closed([weak = get_weak()](const auto& sender, auto&&) {
		if (auto self = weak.get()) {
			std::erase(self->m_windows, sender.template as<Window>());
		}
	});
	m_windows.push_back(window);
}

com_ptr<MainWindow>
App::createWindow(const std::vector<std::filesystem::path>& files,
                  std::optional<int> page) {
	if (!m_pdf) {
		m_pdf = std::make_unique<pdf::Engine>();
	}
	auto main = make_self<MainWindow>();
	main->setEngine(*m_pdf);
	trackWindow(main.as<Window>());
	try {
		for (const auto& file : files) {
			main->openDocument(*m_pdf, file, page);
		}
		main->Activate();
	} catch (...) {
		main->Close();
		throw;
	}
	return main;
}
} // namespace winrt::Sable::implementation
