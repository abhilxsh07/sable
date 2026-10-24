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
#pragma once
// The one place Sable asks Windows whether to animate, and the enter/exit
// motion for anything that appears and disappears by Visibility. Every
// duration comes from helpers/motion.h.
#include <chrono>
#include <optional>
#include <winrt/Microsoft.UI.Xaml.h>

namespace motion {
// Windows' "Animation effects" (Settings > Accessibility > Visual effects).
// When it's off, nothing in Sable animates.
bool enabled();

// what show() puts on an element: the lengths and where it enters from.
// The Motion probe holds them against the tokens
struct Installed {
	std::chrono::milliseconds enter{0};
	std::chrono::milliseconds exit{0};
	float slideX = 0;
	float slideY = 0;
};

// where an element comes from as it enters
enum class From { None, Left, Above, Below };

// Shows or hides an element. Entering fades in over FAST and travels SLIDE
// px from `from`; leaving fades out over FASTER. Visibility changes at once
// either way, so layout and everything that reads it see the new state
// straight away; only the drawing lags. With animations off both are
// instant.
void show(const winrt::Microsoft::UI::Xaml::UIElement& element, bool visible,
          From from = From::None);

#ifdef SABLE_SURFACE_TEST
// the probes' stand-in for the Windows setting; nullopt follows Windows
void setEnabledForTest(std::optional<bool> value);
std::optional<Installed>
installed(const winrt::Microsoft::UI::Xaml::UIElement& element);
#endif
} // namespace motion
