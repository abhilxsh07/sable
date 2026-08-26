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
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <cwchar>
#include <shellapi.h>

// Sable is framework-dependent: Microsoft's Windows App Runtime is
// installed on its own, and the bootstrapper adds it to this process before
// anything uses WinUI. This does what the SDK's auto-initializer would, but
// a missing runtime gets Sable's own message and a link to Microsoft's
// download page.
// see:
// https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/use-windows-app-sdk-run-time
namespace {
#if defined(_M_ARM64)
constexpr wchar_t ARCHITECTURE[] = L"ARM64";
#else
constexpr wchar_t ARCHITECTURE[] = L"x64";
#endif
// see: https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads
constexpr wchar_t DOWNLOADS[] =
    L"https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads";

struct RuntimeCheck {
	RuntimeCheck() {
		const PACKAGE_VERSION minimum{WINDOWSAPPSDK_RUNTIME_VERSION_UINT64};
		const HRESULT result = MddBootstrapInitialize2(
		    WINDOWSAPPSDK_RELEASE_MAJORMINOR,
		    WINDOWSAPPSDK_RELEASE_VERSION_TAG_W, minimum,
		    MddBootstrapInitializeOptions_None);
		if (SUCCEEDED(result)) {
			return;
		}
		wchar_t text[640]{};
		if (result == HRESULT_FROM_WIN32(ERROR_NO_MATCH)) {
			swprintf_s(text,
			           L"Sable needs Microsoft's Windows App Runtime %d.%d or "
			           L"newer for %s, and it isn't installed on this PC.\n\n"
			           L"Open Microsoft's download page now? Install the %s "
			           L"runtime from there, then start Sable again.",
			           WINDOWSAPPSDK_RELEASE_MAJOR, WINDOWSAPPSDK_RELEASE_MINOR,
			           ARCHITECTURE, ARCHITECTURE);
		} else {
			swprintf_s(text,
			           L"Sable couldn't load Microsoft's Windows App Runtime "
			           L"(error 0x%08X).\n\nOpen Microsoft's download page "
			           L"now? Installing the %d.%d %s runtime again may fix "
			           L"it.",
			           static_cast<unsigned>(result),
			           WINDOWSAPPSDK_RELEASE_MAJOR, WINDOWSAPPSDK_RELEASE_MINOR,
			           ARCHITECTURE);
		}
		if (MessageBoxW(nullptr, text, L"Sable",
		                MB_YESNO | MB_ICONINFORMATION) == IDYES) {
			ShellExecuteW(nullptr, L"open", DOWNLOADS, nullptr, nullptr,
			              SW_SHOWNORMAL);
		}
		ExitProcess(static_cast<UINT>(result));
	}
	~RuntimeCheck() {
		MddBootstrapShutdown();
	}
	RuntimeCheck(const RuntimeCheck&) = delete;
	RuntimeCheck& operator=(const RuntimeCheck&) = delete;
};

// before any other static in the app, as the SDK's own initializer does
#pragma warning(disable : 4073)
#pragma init_seg(lib)
const RuntimeCheck runtimeCheck;
#pragma warning(default : 4073)
} // namespace
