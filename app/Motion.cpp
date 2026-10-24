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
#include "Motion.h"
#include <winrt/Windows.UI.ViewManagement.h>

namespace {
#ifdef SABLE_SURFACE_TEST
std::optional<bool> s_enabled;
#endif
} // namespace

bool motion::enabled() {
#ifdef SABLE_SURFACE_TEST
	if (s_enabled) {
		return *s_enabled;
	}
#endif
	// see:
	// https://learn.microsoft.com/en-us/uwp/api/windows.ui.viewmanagement.uisettings.animationsenabled
	return winrt::Windows::UI::ViewManagement::UISettings().AnimationsEnabled();
}

#ifdef SABLE_SURFACE_TEST
void motion::setEnabledForTest(std::optional<bool> value) {
	s_enabled = value;
}
#endif
