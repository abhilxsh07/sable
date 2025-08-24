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

#include <string>
#include <vector>

#include "helpers/cli.h"
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
} // namespace

namespace winrt::Sable::implementation {
App::App() {
	// don't call InitializeComponent here, XAML objects aren't ready yet
#if defined _DEBUG &&                                                          \
    !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
	UnhandledException(
	    [](IInspectable const&, UnhandledExceptionEventArgs const& e) {
		    if (IsDebuggerPresent()) {
			    auto errorMessage = e.Message();
			    __debugbreak();
		    }
	    });
#endif
}

void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e) {
#ifdef SABLE_SURFACE_TEST
	window = surfaceTest(m_pdf);
#else
	auto main = make_self<MainWindow>();
	// port of app.ts's startup (L394-410): open the first PDF given, at -p.
	// --help, --version and more than one file wait for the shell.
	const auto cli = helpers::parseCli(commandLineArgs());
	if (!cli.files.empty()) {
		main->openDocument(m_pdf, to_hstring(cli.files.front()).c_str(),
		                   cli.page);
	}
	window = main.as<Window>();
#endif
	// release the window while XAML is still up, not in App's destructor
	window.Closed([weak = get_weak()](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->window = nullptr;
		}
	});
	window.Activate();
}
} // namespace winrt::Sable::implementation
