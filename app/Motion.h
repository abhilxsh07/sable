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
// The one place Sable asks Windows whether to animate. Every duration
// comes from helpers/motion.h.
#include <optional>

namespace motion {
// Windows' "Animation effects" (Settings > Accessibility > Visual effects).
// When it's off, nothing in Sable animates.
bool enabled();

#ifdef SABLE_SURFACE_TEST
// the probes' stand-in for the Windows setting; nullopt follows Windows
void setEnabledForTest(std::optional<bool> value);
#endif
} // namespace motion
