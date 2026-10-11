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
#include "page_filter.h"

#include <d2d1effects.h>
#include <cstring>

#include "../helpers/filter.h"

namespace pdf {
PageFilter::PageFilter(ID2D1DeviceContext& context) {
	for (size_t i = 0; i < m_effects.size(); i++) {
		auto& effect = m_effects[i];
		winrt::check_hresult(
		    context.CreateEffect(CLSID_D2D1ColorMatrix, effect.put()));
		// the default is false; each CSS step needs its own clamp
		// see:
		// https://learn.microsoft.com/en-us/windows/win32/direct2d/color-matrix
		winrt::check_hresult(
		    effect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE));
		if (i != 0) {
			effect->SetInputEffect(0, m_effects[i - 1].get());
		}
	}
}

void PageFilter::setInput(ID2D1Image* image) {
	m_effects.front()->SetInput(0, image);
}

void PageFilter::setValues(const helpers::SliderValues& values) {
	const auto matrices = helpers::buildMatrices(values);
	for (size_t i = 0; i < m_effects.size(); i++) {
		D2D1_MATRIX_5X4_F matrix;
		static_assert(sizeof(matrix) == sizeof(matrices[i]));
		// helpers/filter already stores the transposed, row-vector form
		std::memcpy(&matrix, &matrices[i], sizeof(matrix));
		winrt::check_hresult(
		    m_effects[i]->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix));
	}
}

ID2D1Effect* PageFilter::output() const {
	return m_effects.back().get();
}
} // namespace pdf
