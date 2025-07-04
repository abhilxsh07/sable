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

using namespace winrt;
using namespace Microsoft::UI::Xaml;

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
	window = make<MainWindow>();
	window.Activate();
}
} // namespace winrt::Sable::implementation
