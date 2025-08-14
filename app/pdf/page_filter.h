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

#include <d2d1_1.h>
#include <winrt/base.h>

#include <array>

#include "../helpers/sliders.h"

namespace pdf {
// UI-thread drawing only. The input is an opaque page bitmap, in sRGB.
// Recreate the chain along with its device after device loss.
class PageFilter {
  public:
	explicit PageFilter(ID2D1DeviceContext& context);
	void setInput(ID2D1Image* image);
	void setValues(const helpers::SliderValues& values);
	ID2D1Effect* output() const;

  private:
	std::array<winrt::com_ptr<ID2D1Effect>, 6> m_effects;
};
} // namespace pdf
