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
// port of app/main/app.ts versionString() (L110-113)
// see:
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/main/app.ts#L110-L113
#include "version.h"

// Directory.Build.props fills these in from VERSION and .pdfium_version. The
// fallbacks are only there so the file still compiles outside MSBuild.
#ifndef SABLE_VERSION
#define SABLE_VERSION "dev"
#endif
#ifndef SABLE_PDFIUM_VERSION
#define SABLE_PDFIUM_VERSION "unknown"
#endif
#ifndef SABLE_WINAPPSDK_VERSION
#define SABLE_WINAPPSDK_VERSION "unknown"
#endif

namespace helpers {
std::string versionString() {
	return std::string("Sable: ") + SABLE_VERSION +
	       " PDFium: " + SABLE_PDFIUM_VERSION +
	       " WindowsAppSDK: " + SABLE_WINAPPSDK_VERSION;
}
} // namespace helpers
