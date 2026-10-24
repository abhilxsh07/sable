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
// Sable's motion tokens (audit section 7.1). styles/Theme.xaml carries the
// same values as SableMotion* for anything written in XAML; the Motion probe
// checks the two agree. The durations are WinUI's own
// ControlFasterAnimationDuration, ControlFastAnimationDuration and
// ControlNormalAnimationDuration, and the enter spline is its
// ControlFastOutSlowInKeySpline.
// see:
// https://learn.microsoft.com/en-us/windows/apps/design/motion/timing-and-easing
#include <chrono>

namespace motion {
enum class Token { Faster, Fast, Normal };

// hover, press and check; every exit
inline constexpr std::chrono::milliseconds FASTER{83};
// pane enter, command-bar swap, notice enter, flyout enter
inline constexpr std::chrono::milliseconds FAST{167};
// the ceiling: music tint and artwork, MORTY's fade
inline constexpr std::chrono::milliseconds NORMAL{250};
inline constexpr std::chrono::milliseconds LONGEST = NORMAL;
// how far a pane, a notice or a flyout travels as it enters
inline constexpr float SLIDE = 16;

struct Spline {
	float x1, y1, x2, y2;
};
// Fast Out, Slow In for everything entering; Slow Out, Fast In for exits
inline constexpr Spline ENTER{0, 0, 0, 1};
inline constexpr Spline EXIT{1, 0, 1, 1};

// a token's length, or nothing at all with Windows' animation effects off
constexpr std::chrono::milliseconds duration(Token token, bool animated) {
	if (!animated) {
		return std::chrono::milliseconds{0};
	}
	switch (token) {
	case Token::Faster:
		return FASTER;
	case Token::Fast:
		return FAST;
	case Token::Normal:
		return NORMAL;
	}
	return std::chrono::milliseconds{0};
}
} // namespace motion
