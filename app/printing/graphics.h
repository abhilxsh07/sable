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
#include <d3d11.h>
#include <d2d1_1.h>
#include <wincodec.h>
#include <winrt/base.h>
#include "../pdf/print_layout.h"

namespace printing {
// Each worker owns its context. Nothing shares the viewer's GPU resources.
class Graphics {
  public:
	Graphics();
	winrt::com_ptr<IDXGISurface> preview(const pdf::Bitmap& raster,
	                                     pdf::PrintPaper paper,
	                                     pdf::PrintLayout layout, int width,
	                                     int height);
	winrt::com_ptr<ID2D1PrintControl>
	control(IPrintDocumentPackageTarget* target, float dpi);
	void beginPage();
	void tile(const pdf::Bitmap& raster, pdf::PrintLayout layout,
	          pdf::PrintRect tile);
	winrt::com_ptr<ID2D1CommandList> endPage();

  private:
	void draw(const pdf::Bitmap& raster, D2D1_RECT_F rect);
	winrt::com_ptr<ID3D11Device> m_d3d;
	winrt::com_ptr<ID3D11DeviceContext> m_immediate;
	winrt::com_ptr<ID2D1Factory1> m_factory;
	winrt::com_ptr<ID2D1Device> m_device;
	winrt::com_ptr<ID2D1DeviceContext> m_context;
	winrt::com_ptr<ID2D1CommandList> m_commands;
};
} // namespace printing
