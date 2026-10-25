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
#include "MusicPane.h"
#include <winrt/Windows.UI.ViewManagement.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Animation;
namespace music {
namespace {
void fade(Storyboard const& storyboard, DependencyObject const& target,
          const wchar_t* property, double from, double to, int milliseconds) {
	DoubleAnimation animation;
	animation.From(from);
	animation.To(to);
	animation.Duration(
	    DurationHelper::FromTimeSpan(std::chrono::milliseconds(milliseconds)));
	Storyboard::SetTarget(animation, target);
	Storyboard::SetTargetProperty(animation, property);
	storyboard.Children().Append(animation);
}
} // namespace
bool Pane::animations() const {
#ifdef SABLE_SURFACE_TEST
	if (m_animationsOverride) {
		return *m_animationsOverride;
	}
#endif
	// see:
	// https://learn.microsoft.com/en-us/uwp/api/windows.ui.viewmanagement.uisettings.animationsenabled
	return Windows::UI::ViewManagement::UISettings().AnimationsEnabled();
}
void Pane::motion(bool opening) {
	m_motion.Stop();
	m_motion = Storyboard();
	if (!animations()) {
		m_surface.Opacity(1);
		m_slide.Y(0);
		return;
	}
	const auto duration = opening ? 150 : 80;
	fade(m_motion, m_surface, L"Opacity", opening ? 0 : 1, opening ? 1 : 0,
	     duration);
	fade(m_motion, m_slide, L"Y", opening ? 8 : 0, opening ? 0 : 4, duration);
	if (!opening) {
		m_motion.Completed([weak = weak_from_this()](auto&&, auto&&) {
			if (const auto self = weak.lock();
			    self && !self->m_closed && self->m_closing) {
				self->m_allowClose = true;
				self->m_flyout.Hide();
			}
		});
	}
	m_motion.Begin();
}
void Pane::tint(Tint color) {
	if (m_closed) {
		return;
	}
	m_tint = color;
	m_surface.Background(
	    appearance::obsidian()
	        ? Media::Brush(Media::SolidColorBrush({255, 0, 0, 0}))
	        : Media::Brush(m_background));
	if (appearance::obsidian()) {
		m_transition.Stop();
		m_transition.Children().Clear();
		m_gradientTop.Color({0, 0, 0, 0});
		m_gradientBottom.Color({0, 0, 0, 0});
		return;
	}
	const Windows::UI::Color next{255, color.r, color.g, color.b};
	const auto previous = m_background.TintColor();
	m_transition.Stop();
	m_transition.Children().Clear();
	m_background.TintTransitionDuration(animations()
	                                        ? std::chrono::milliseconds(300)
	                                        : std::chrono::milliseconds(0));
	m_background.TintColor(next);
	m_background.FallbackColor(next);
	const auto animateColor =
	    [&](const DependencyObject& target, const wchar_t* property,
	        Windows::UI::Color from, Windows::UI::Color to) {
		    ColorAnimation animation;
		    animation.From(from);
		    animation.To(to);
		    animation.Duration(
		        DurationHelper::FromTimeSpan(std::chrono::milliseconds(300)));
		    animation.EnableDependentAnimation(true);
		    Storyboard::SetTarget(animation, target);
		    Storyboard::SetTargetProperty(animation, property);
		    m_transition.Children().Append(animation);
	    };
	const Windows::UI::Color top{55, color.r, color.g, color.b};
	const Windows::UI::Color bottom{0, color.r, color.g, color.b};
	const Windows::UI::Color fill{255,
	                              static_cast<std::uint8_t>(160 + color.r / 3),
	                              static_cast<std::uint8_t>(160 + color.g / 3),
	                              static_cast<std::uint8_t>(160 + color.b / 3)};
	if (animations()) {
		animateColor(m_background, L"FallbackColor", previous, next);
		animateColor(m_gradientTop, L"Color", m_gradientTop.Color(), top);
		animateColor(m_gradientBottom, L"Color", m_gradientBottom.Color(),
		             bottom);
		animateColor(m_seekFill, L"Color", m_seekFill.Color(), fill);
	}
	m_gradientTop.Color(top);
	m_gradientBottom.Color(bottom);
	m_seekFill.Color(fill);
	if (animations()) {
		m_transition.Begin();
	}
}
void Pane::setArtwork(const ImageSource& source, Tint color) {
	const auto old = m_art.Source();
	m_artFade.Stop();
	m_artFade = Storyboard();
	m_previousArt.Source(old);
	m_previousArt.Opacity(1);
	m_art.Source(source);
	m_art.Opacity(1);
	m_placeholder.Visibility(source ? Visibility::Collapsed
	                                : Visibility::Visible);
	tint(color);
	if (!m_open || !animations() || (!old && !source)) {
		m_previousArt.Source(nullptr);
		return;
	}
	fade(m_artFade, m_previousArt, L"Opacity", 1, 0, 300);
	if (source) {
		fade(m_artFade, m_art, L"Opacity", 0, 1, 300);
	}
	const auto generation = m_artworkGeneration;
	m_artFade.Completed([weak = weak_from_this(), generation](auto&&, auto&&) {
		if (const auto self = weak.lock();
		    self && !self->m_closed &&
		    self->m_artworkGeneration == generation) {
			self->m_previousArt.Source(nullptr);
		}
	});
	m_artFade.Begin();
}
} // namespace music
