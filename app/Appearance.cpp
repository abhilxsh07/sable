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
#include <cstdint>
#include <iterator>
#include <string_view>
#include <utility>
#include <winrt/Windows.UI.ViewManagement.h>
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
// code-made text colours, recoloured for contrast themes
std::vector<Surface> texts;
std::map<unsigned, SolidColorBrush> programmaticText;
// code-made brushes that take a chosen system colour under contrast themes
struct Themed {
	SolidColorBrush brush{nullptr};
	Windows::UI::Color normal;
	Windows::UI::ViewManagement::UIElementType contrast;
};
std::vector<Themed> themedBrushes;
std::map<std::pair<std::uint32_t, int>, SolidColorBrush> themedCache;
Windows::UI::ViewManagement::AccessibilitySettings accessibility{nullptr};
event_token contrastChanged{};

Windows::UI::Color
systemColor(Windows::UI::ViewManagement::UIElementType type) {
	return Windows::UI::ViewManagement::UISettings().UIElementColor(type);
}

// Contrast themes switch XAML to the HighContrast dictionaries in
// styles/Theme.xaml by themselves; this only follows for code-made brushes.
void watchContrast() {
	try {
		accessibility = Windows::UI::ViewManagement::AccessibilitySettings();
		const auto queue =
		    Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
		contrastChanged =
		    accessibility.HighContrastChanged([queue](auto&&, auto&&) {
			    queue.TryEnqueue([] {
				    if (initialized) {
					    apply(active);
				    }
			    });
		    });
	} catch (...) {
		accessibility = nullptr;
	}
}
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
		watchContrast();
		apply(trueBlack);
	}
}
void apply(bool trueBlack) {
	if (!initialized) {
		initialize(trueBlack);
		return;
	}
	active = trueBlack;
	using Windows::UI::ViewManagement::UIElementType;
	const bool contrast = highContrast();
	const auto window = contrast ? systemColor(UIElementType::Window) : black;
	const auto windowText =
	    contrast ? systemColor(UIElementType::WindowText) : black;
	for (const auto& item : surfaces) {
		if (const auto brush = item.brush) {
			brush.Color(contrast ? window : active ? black : item.dark);
		}
	}
	for (const auto& item : acrylics) {
		if (const auto brush = item.brush) {
			brush.FallbackColor(contrast || active ? window : item.fallback);
			brush.AlwaysUseFallback(contrast || active || item.alwaysFallback);
		}
	}
	for (const auto& item : texts) {
		if (const auto brush = item.brush) {
			brush.Color(contrast ? windowText : item.dark);
		}
	}
	for (const auto& item : themedBrushes) {
		if (const auto brush = item.brush) {
			brush.Color(contrast ? systemColor(item.contrast) : item.normal);
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
	if (accessibility) {
		accessibility.HighContrastChanged(contrastChanged);
		accessibility = nullptr;
	}
	texts.clear();
	programmaticText.clear();
	themedBrushes.clear();
	themedCache.clear();
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
	SolidColorBrush brush(
	    highContrast()
	        ? systemColor(Windows::UI::ViewManagement::UIElementType::Window)
	    : active ? black
	             : dark);
	surfaces.push_back({brush, dark});
	programmatic.emplace(darkRgb, brush);
	return brush;
}
SolidColorBrush text(unsigned rgb) {
	if (const auto found = programmaticText.find(rgb);
	    found != programmaticText.end()) {
		return found->second;
	}
	const Windows::UI::Color normal{255, static_cast<uint8_t>(rgb >> 16),
	                                static_cast<uint8_t>(rgb >> 8),
	                                static_cast<uint8_t>(rgb)};
	SolidColorBrush brush(
	    highContrast()
	        ? systemColor(
	              Windows::UI::ViewManagement::UIElementType::WindowText)
	        : normal);
	texts.push_back({brush, normal});
	programmaticText.emplace(rgb, brush);
	return brush;
}
SolidColorBrush themed(Windows::UI::Color normal,
                       Windows::UI::ViewManagement::UIElementType contrast) {
	const std::pair key{(std::uint32_t{normal.A} << 24) |
	                        (std::uint32_t{normal.R} << 16) |
	                        (std::uint32_t{normal.G} << 8) | normal.B,
	                    static_cast<int>(contrast)};
	if (const auto found = themedCache.find(key); found != themedCache.end()) {
		return found->second;
	}
	SolidColorBrush brush(highContrast() ? systemColor(contrast) : normal);
	themedBrushes.push_back({brush, normal, contrast});
	themedCache.emplace(key, brush);
	return brush;
}
SolidColorBrush themed(unsigned rgb,
                       Windows::UI::ViewManagement::UIElementType contrast) {
	return themed(Windows::UI::Color{255, static_cast<uint8_t>(rgb >> 16),
	                                 static_cast<uint8_t>(rgb >> 8),
	                                 static_cast<uint8_t>(rgb)},
	              contrast);
}
void presetChip(const Controls::Button& chip, bool selected) {
	using Windows::UI::ViewManagement::UIElementType;
	// NightPDF's preset button colours: background, then text
	struct Look {
		std::wstring_view tag;
		unsigned background, foreground;
	};
	static constexpr Look looks[]{{L"default", 0x1e2024, 0xfbfbff},
	                              {L"sepia", 0x9e9174, 0x40392d},
	                              {L"redeye", 0x36272b, 0xbc8b96},
	                              {L"custom", 0xc1c1c1, 0x232323}};
	const auto tag = unbox_value_or<hstring>(chip.Tag(), hstring{});
	const auto look =
	    std::ranges::find(looks, std::wstring_view{tag}, &Look::tag);
	if (look == std::end(looks)) {
		return;
	}
	chip.Background(themed(look->background, selected
	                                             ? UIElementType::Highlight
	                                             : UIElementType::ButtonFace));
	chip.Foreground(themed(look->foreground, selected
	                                             ? UIElementType::HighlightText
	                                             : UIElementType::ButtonText));
	// the active one gets the hover border, like .button.active
	chip.BorderBrush(themed(selected ? 0x797979u : 0x1e1e1eu,
	                        selected ? UIElementType::Highlight
	                                 : UIElementType::ButtonText));
}
bool highContrast() {
	try {
		return accessibility && accessibility.HighContrast();
	} catch (...) {
		return false;
	}
}
} // namespace appearance
