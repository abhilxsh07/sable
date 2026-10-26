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
#include "Motion.h"
#include "helpers/motion.h"
#include <optional>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Animation;
namespace music {
namespace {
using Length = std::chrono::milliseconds;
KeySpline keySpline(::motion::Spline spline) {
	KeySpline result;
	result.ControlPoint1({spline.x1, spline.y1});
	result.ControlPoint2({spline.x2, spline.y2});
	return result;
}
KeyTime at(Length length) {
	return KeyTimeHelper::FromTimeSpan(length);
}
// one property to `to` over `length`. Without `from` it starts from where
// the property is now, part-way through an animation included, so a new
// change carries on from there instead of jumping
void animate(Storyboard const& storyboard, DependencyObject const& target,
             const wchar_t* property, std::optional<double> from, double to,
             Length length, ::motion::Spline spline) {
	DoubleAnimationUsingKeyFrames animation;
	if (from) {
		DiscreteDoubleKeyFrame start;
		start.KeyTime(at(Length{0}));
		start.Value(*from);
		animation.KeyFrames().Append(start);
	}
	SplineDoubleKeyFrame end;
	end.KeyTime(at(length));
	end.Value(to);
	end.KeySpline(keySpline(spline));
	animation.KeyFrames().Append(end);
	Storyboard::SetTarget(animation, target);
	Storyboard::SetTargetProperty(animation, property);
	storyboard.Children().Append(animation);
}
void animateColor(Storyboard const& storyboard, DependencyObject const& target,
                  const wchar_t* property, Windows::UI::Color from,
                  Windows::UI::Color to, Length length) {
	ColorAnimationUsingKeyFrames animation;
	DiscreteColorKeyFrame start;
	start.KeyTime(at(Length{0}));
	start.Value(from);
	animation.KeyFrames().Append(start);
	SplineColorKeyFrame end;
	end.KeyTime(at(length));
	end.Value(to);
	end.KeySpline(keySpline(::motion::ENTER));
	animation.KeyFrames().Append(end);
	animation.EnableDependentAnimation(true);
	Storyboard::SetTarget(animation, target);
	Storyboard::SetTargetProperty(animation, property);
	storyboard.Children().Append(animation);
}
} // namespace
void Pane::motion(bool opening) {
	++m_closeToken;
	if (!::motion::enabled()) {
		m_motion.Stop();
		m_motion = Storyboard();
		m_surface.Opacity(1);
		m_slide.Y(0);
		m_fadingOut = false;
		m_enterLength = m_exitLength = Length{0};
		return;
	}
	// a fresh open starts faded out and 16 px low; opening again while the
	// close is still fading carries on from where it got to
	const bool fresh = !m_fadingOut;
	m_motion = Storyboard();
	if (opening) {
		animate(m_motion, m_surface, L"Opacity",
		        fresh ? std::optional<double>(0) : std::nullopt, 1,
		        ::motion::FAST, ::motion::ENTER);
		animate(m_motion, m_slide, L"Y",
		        fresh ? std::optional<double>(::motion::SLIDE) : std::nullopt,
		        0, ::motion::FAST, ::motion::ENTER);
		m_fadingOut = false;
		m_enterLength = ::motion::FAST;
		if (m_closeFallback) {
			m_closeFallback.Stop();
		}
	} else {
		animate(m_motion, m_surface, L"Opacity", std::nullopt, 0,
		        ::motion::FASTER, ::motion::EXIT);
		m_fadingOut = true;
		m_exitLength = ::motion::FASTER;
		m_motion.Completed(
		    [weak = weak_from_this(), token = m_closeToken](auto&&, auto&&) {
			    if (const auto self = weak.lock()) {
				    self->finishClosing(token);
			    }
		    });
		// the flyout closes even if the fade never reports back
		if (!m_closeFallback) {
			m_closeFallback = m_button.DispatcherQueue().CreateTimer();
			m_closeFallback.IsRepeating(false);
			m_closeFallback.Interval(2 * ::motion::FASTER);
			m_closeFallback.Tick([weak = weak_from_this()](auto&&, auto&&) {
				if (const auto self = weak.lock()) {
					self->finishClosing(self->m_closeToken);
				}
			});
		}
		m_closeFallback.Stop();
		m_closeFallback.Start();
	}
	m_motion.Begin();
}
void Pane::finishClosing(std::uint64_t token) {
	if (token != m_closeToken) {
		return;
	}
	if (m_closeFallback) {
		m_closeFallback.Stop();
	}
	if (!m_closed && m_closing && m_fadingOut) {
		m_fadingOut = false;
		m_allowClose = true;
		m_flyout.Hide();
	}
}
void Pane::tint(Tint color) {
	if (m_closed) {
		return;
	}
	m_tint = color;
	// Obsidian and contrast themes get a plain window colour, not the art's
	const bool plain = appearance::obsidian() || appearance::highContrast();
	m_surface.Background(plain ? Media::Brush(appearance::surface(0x000000))
	                           : Media::Brush(m_background));
	if (plain) {
		m_transition.Stop();
		m_transition.Children().Clear();
		m_gradientTop.Color({0, 0, 0, 0});
		m_gradientBottom.Color({0, 0, 0, 0});
		return;
	}
	const auto length =
	    ::motion::duration(::motion::Token::Normal, ::motion::enabled());
	// where the colours are now, part-way through the last change included,
	// so a new colour starts from there
	const auto top0 = m_gradientTop.Color();
	const auto bottom0 = m_gradientBottom.Color();
	const auto fill0 = m_seekFill.Color();
	m_transition.Stop();
	m_transition.Children().Clear();
	// the acrylic brush makes its own change; the gradient and the seek bar
	// follow it with the same length
	const Windows::UI::Color next{255, color.r, color.g, color.b};
	m_background.TintTransitionDuration(length);
	m_background.TintColor(next);
	m_background.FallbackColor(next);
	const Windows::UI::Color top{55, color.r, color.g, color.b};
	const Windows::UI::Color bottom{0, color.r, color.g, color.b};
	const Windows::UI::Color fill{255,
	                              static_cast<std::uint8_t>(160 + color.r / 3),
	                              static_cast<std::uint8_t>(160 + color.g / 3),
	                              static_cast<std::uint8_t>(160 + color.b / 3)};
	m_gradientTop.Color(top);
	m_gradientBottom.Color(bottom);
	m_seekFill.Color(fill);
	m_tintLength = length;
	if (length.count() == 0) {
		return;
	}
	animateColor(m_transition, m_gradientTop, L"Color", top0, top, length);
	animateColor(m_transition, m_gradientBottom, L"Color", bottom0, bottom,
	             length);
	animateColor(m_transition, m_seekFill, L"Color", fill0, fill, length);
	m_transition.Begin();
}
void Pane::setArtwork(const ImageSource& source, Tint color) {
	const auto old = m_art.Source();
	m_art.Source(source);
	m_placeholder.Visibility(source ? Visibility::Collapsed
	                                : Visibility::Visible);
	tint(color);
	if (!m_open || !::motion::enabled() || (!old && !source)) {
		m_artFade.Stop();
		m_art.Opacity(1);
		m_previousArt.Source(nullptr);
		m_artFading = false;
		++m_artFadeToken;
		m_artLength = Length{0};
		return;
	}
	m_artFade = Storyboard();
	if (m_artFading) {
		// part-way through a change: the art that was arriving gives way to
		// this one, which carries on from its opacity, and what was leaving
		// keeps leaving
		animate(m_artFade, m_art, L"Opacity", std::nullopt, source ? 1 : 0,
		        ::motion::NORMAL, ::motion::ENTER);
	} else {
		m_previousArt.Source(old);
		animate(m_artFade, m_previousArt, L"Opacity", 1, 0, ::motion::NORMAL,
		        ::motion::ENTER);
		if (source) {
			animate(m_artFade, m_art, L"Opacity", 0, 1, ::motion::NORMAL,
			        ::motion::ENTER);
		}
	}
	m_artFading = true;
	m_artLength = ::motion::NORMAL;
	m_artFade.Completed(
	    [weak = weak_from_this(), token = ++m_artFadeToken](auto&&, auto&&) {
		    if (const auto self = weak.lock();
		        self && !self->m_closed && self->m_artFadeToken == token) {
			    self->m_previousArt.Source(nullptr);
			    self->m_artFading = false;
		    }
	    });
	m_artFade.Begin();
}
} // namespace music
