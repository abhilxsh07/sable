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
#include "Appearance.h"
#include <algorithm>
#include <vector>
#include <map>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Media;

namespace appearance {
namespace {
struct Surface {
	SolidColorBrush brush{nullptr};
	Windows::UI::Color dark;
};
struct AcrylicSurface {
	AcrylicBrush brush{nullptr};
	Windows::UI::Color fallback;
	bool alwaysFallback;
};
bool active = false, initialized = false;
// XAML can retain a brush's native value while releasing its projection.
// Retain these few shared palette brushes until App destruction. Cache C++
// surfaces by colour so opening windows/recent rows does not grow the palette.
std::vector<Surface> surfaces;
std::map<unsigned, SolidColorBrush> programmatic;
std::vector<AcrylicSurface> acrylics;
std::vector<std::weak_ptr<std::function<void()>>> observers;
constexpr Windows::UI::Color black{255, 0, 0, 0};

Windows::Foundation::IInspectable
findResource(const ResourceDictionary& dictionary,
             const Windows::Foundation::IInspectable& key) {
	const auto themes = dictionary.ThemeDictionaries();
	for (const auto name : {L"Dark", L"Default"}) {
		const auto theme = box_value(name);
		if (themes.HasKey(theme)) {
			if (const auto found = findResource(
			        themes.Lookup(theme).as<ResourceDictionary>(), key)) {
				return found;
			}
			break;
		}
	}
	if (dictionary.HasKey(key)) {
		return dictionary.Lookup(key);
	}
	const auto merged = dictionary.MergedDictionaries();
	for (auto i = merged.Size(); i > 0; --i) {
		if (const auto found = findResource(merged.GetAt(i - 1), key)) {
			return found;
		}
	}
	return nullptr;
}
void collectResources() {
	const auto dictionary = Application::Current().Resources();
	// Only resting surface fills: foreground, focus, hover and selection keep
	// their contrast. Acrylic uses an opaque fallback to avoid noise/tint.
	for (const auto key : {L"SableTitleBarBrush",
	                       L"SablePaneBrush",
	                       L"SableContentBrush",
	                       L"SableViewerBrush",
	                       L"SableCardBrush",
	                       L"SableSettingsButtonBrush",
	                       L"SableSettingsShortcutBrush",
	                       L"SablePopupBrush",
	                       L"NavigationViewExpandedPaneBackground",
	                       L"NavigationViewDefaultPaneBackground",
	                       L"NavigationViewContentBackground",
	                       L"SolidBackgroundFillColorBaseBrush",
	                       L"SolidBackgroundFillColorSecondaryBrush",
	                       L"SolidBackgroundFillColorTertiaryBrush",
	                       L"SolidBackgroundFillColorQuarternaryBrush",
	                       L"SolidBackgroundFillColorQuinaryBrush",
	                       L"SolidBackgroundFillColorSenaryBrush",
	                       L"LayerFillColorDefaultBrush",
	                       L"LayerFillColorAltBrush",
	                       L"CardBackgroundFillColorDefaultBrush",
	                       L"CardBackgroundFillColorSecondaryBrush",
	                       L"ControlFillColorDefaultBrush",
	                       L"ControlFillColorInputActiveBrush",
	                       L"ControlFillColorDisabledBrush",
	                       L"AcrylicInAppFillColorDefaultBrush",
	                       L"DesktopAcrylicTransparentBrush",
	                       L"TabViewItemHeaderBackgroundSelected",
	                       L"TabViewItemHeaderDragBackground",
	                       L"TabViewItemHeaderBackground",
	                       L"TabViewBackground",
	                       L"FlyoutPresenterBackground",
	                       L"MenuFlyoutPresenterBackground",
	                       L"ComboBoxDropDownBackground",
	                       L"ContentDialogBackground",
	                       L"ContentDialogTopOverlay",
	                       L"CommandBarBackground",
	                       L"AppBarBackground",
	                       L"ToolTipBackground",
	                       L"TeachingTipBackground"}) {
		const auto name = box_value(key);
		const auto value = findResource(dictionary, name);
		if (!value) {
			continue;
		}
		if (const auto brush = value.try_as<SolidColorBrush>()) {
			const auto found = std::find_if(
			    surfaces.begin(), surfaces.end(),
			    [&](const Surface& item) { return item.brush == brush; });
			if (found == surfaces.end()) {
				surfaces.push_back({brush, brush.Color()});
			}
		} else if (const auto acrylic = value.try_as<AcrylicBrush>()) {
			const auto found = std::find_if(acrylics.begin(), acrylics.end(),
			                                [&](const AcrylicSurface& item) {
				                                return item.brush == acrylic;
			                                });
			if (found == acrylics.end()) {
				acrylics.push_back({acrylic, acrylic.FallbackColor(),
				                    acrylic.AlwaysUseFallback()});
			}
		}
	}
}
} // namespace
bool obsidian() {
	return active;
}
void initialize(bool trueBlack) {
	if (!initialized) {
		initialized = true;
		collectResources();
		apply(trueBlack);
	}
}
void apply(bool trueBlack) {
	if (!initialized) {
		initialize(trueBlack);
		return;
	}
	active = trueBlack;
	for (const auto& item : surfaces) {
		if (const auto brush = item.brush) {
			brush.Color(active ? black : item.dark);
		}
	}
	for (const auto& item : acrylics) {
		if (const auto brush = item.brush) {
			brush.FallbackColor(active ? black : item.fallback);
			brush.AlwaysUseFallback(active || item.alwaysFallback);
		}
	}
	std::erase_if(observers, [](const auto& item) { return item.expired(); });
	const auto snapshot = observers;
	for (const auto& item : snapshot) {
		if (const auto callback = item.lock()) {
			(*callback)();
		}
	}
}
Subscription observe(std::function<void()> changed) {
	auto subscription =
	    std::make_shared<std::function<void()>>(std::move(changed));
	observers.push_back(subscription);
	return subscription;
}
void shutdown() {
	observers.clear();
	surfaces.clear();
	acrylics.clear();
	programmatic.clear();
	initialized = false;
	active = false;
}
SolidColorBrush surface(unsigned darkRgb) {
	if (const auto found = programmatic.find(darkRgb);
	    found != programmatic.end()) {
		return found->second;
	}
	const Windows::UI::Color dark{255, static_cast<uint8_t>(darkRgb >> 16),
	                              static_cast<uint8_t>(darkRgb >> 8),
	                              static_cast<uint8_t>(darkRgb)};
	SolidColorBrush brush(active ? black : dark);
	surfaces.push_back({brush, dark});
	programmatic.emplace(darkRgb, brush);
	return brush;
}
} // namespace appearance
