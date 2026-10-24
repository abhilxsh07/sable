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
#include "helpers/motion.h"
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace {
using namespace winrt;
using namespace Microsoft::UI::Composition;
using Microsoft::UI::Xaml::UIElement;
using Microsoft::UI::Xaml::Visibility;
using Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;

#ifdef SABLE_SURFACE_TEST
std::optional<bool> s_enabled;
std::vector<std::pair<weak_ref<UIElement>, motion::Installed>> s_installed;
void remember(const UIElement& element, const motion::Installed& value) {
	std::erase_if(s_installed,
	              [](const auto& entry) { return !entry.first.get(); });
	for (auto& entry : s_installed) {
		if (entry.first.get() == element) {
			entry.second = value;
			return;
		}
	}
	s_installed.emplace_back(make_weak(element), value);
}
#endif

CubicBezierEasingFunction easing(const Compositor& compositor,
                                 motion::Spline spline) {
	return compositor.CreateCubicBezierEasingFunction({spline.x1, spline.y1},
	                                                  {spline.x2, spline.y2});
}
} // namespace

bool motion::enabled() {
#ifdef SABLE_SURFACE_TEST
	if (s_enabled) {
		return *s_enabled;
	}
#endif
	// see:
	// https://learn.microsoft.com/en-us/uwp/api/windows.ui.viewmanagement.uisettings.animationsenabled
	return Windows::UI::ViewManagement::UISettings().AnimationsEnabled();
}

void motion::show(const UIElement& element, bool visible, From from) {
	const auto target = visible ? Visibility::Visible : Visibility::Collapsed;
	// an implicit animation runs only on a change, so there's nothing to set
	if (element.Visibility() == target) {
		return;
	}
	const auto visual = ElementCompositionPreview::GetElementVisual(element);
	const bool animate = enabled();
	Installed installed;
	if (from == From::Left) {
		installed.slideX = -SLIDE;
	} else if (from == From::Above) {
		installed.slideY = -SLIDE;
	} else if (from == From::Below) {
		installed.slideY = SLIDE;
	}
	if (!animate) {
		ElementCompositionPreview::SetImplicitShowAnimation(element, nullptr);
		ElementCompositionPreview::SetImplicitHideAnimation(element, nullptr);
		// a hide that ran earlier leaves the visual faded out
		visual.Opacity(1);
		installed.slideX = installed.slideY = 0;
	} else {
		const auto compositor = visual.Compositor();
		// in: from nothing, decelerating
		const auto enter = compositor.CreateAnimationGroup();
		const auto fadeIn = compositor.CreateScalarKeyFrameAnimation();
		fadeIn.Target(L"Opacity");
		fadeIn.InsertKeyFrame(0, 0);
		fadeIn.InsertKeyFrame(1, 1, easing(compositor, ENTER));
		fadeIn.Duration(duration(Token::Fast, true));
		enter.Add(fadeIn);
		if (from != From::None) {
			ElementCompositionPreview::SetIsTranslationEnabled(element, true);
			const auto slide = compositor.CreateVector3KeyFrameAnimation();
			slide.Target(L"Translation");
			slide.InsertKeyFrame(0, {installed.slideX, installed.slideY, 0});
			slide.InsertKeyFrame(1, {0, 0, 0}, easing(compositor, ENTER));
			slide.Duration(duration(Token::Fast, true));
			enter.Add(slide);
		}
		// out: from wherever it is now, accelerating
		const auto fadeOut = compositor.CreateScalarKeyFrameAnimation();
		fadeOut.Target(L"Opacity");
		fadeOut.InsertExpressionKeyFrame(0, L"this.StartingValue");
		fadeOut.InsertKeyFrame(1, 0, easing(compositor, EXIT));
		fadeOut.Duration(duration(Token::Faster, true));
		ElementCompositionPreview::SetImplicitShowAnimation(element, enter);
		ElementCompositionPreview::SetImplicitHideAnimation(element, fadeOut);
		installed.enter = duration(Token::Fast, true);
		installed.exit = duration(Token::Faster, true);
	}
#ifdef SABLE_SURFACE_TEST
	remember(element, installed);
#endif
	element.Visibility(target);
}

#ifdef SABLE_SURFACE_TEST
void motion::setEnabledForTest(std::optional<bool> value) {
	s_enabled = value;
}
std::optional<motion::Installed> motion::installed(const UIElement& element) {
	for (const auto& entry : s_installed) {
		if (entry.first.get() == element) {
			return entry.second;
		}
	}
	return {};
}
#endif
