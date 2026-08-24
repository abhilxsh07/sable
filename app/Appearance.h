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

#include <functional>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.ViewManagement.h>

// UI-thread only. Shared brushes update existing and future windows together.
namespace appearance {
using Subscription = std::shared_ptr<std::function<void()>>;
bool obsidian();
void initialize(bool trueBlack);
void apply(bool trueBlack);
void shutdown();
Subscription observe(std::function<void()> changed);
winrt::Microsoft::UI::Xaml::Media::SolidColorBrush surface(unsigned darkRgb);
// A shared text brush. Under a Windows contrast theme it takes the theme's
// window text colour, as surface brushes take its window colour.
winrt::Microsoft::UI::Xaml::Media::SolidColorBrush text(unsigned rgb);
// A shared brush that keeps its own colour unless a Windows contrast theme
// is on, and then takes the theme's `contrast` system colour.
winrt::Microsoft::UI::Xaml::Media::SolidColorBrush
themed(winrt::Windows::UI::Color normal,
       winrt::Windows::UI::ViewManagement::UIElementType contrast);
winrt::Microsoft::UI::Xaml::Media::SolidColorBrush
themed(unsigned rgb,
       winrt::Windows::UI::ViewManagement::UIElementType contrast);
// Colours a filter preset chip from its Tag. The chips preview their filter
// like NightPDF's preset buttons; under a contrast theme they take button
// colours and the selected one takes the highlight.
void presetChip(const winrt::Microsoft::UI::Xaml::Controls::Button& chip,
                bool selected);
// whether a Windows contrast theme is on; changes re-run apply()
bool highContrast();
} // namespace appearance
