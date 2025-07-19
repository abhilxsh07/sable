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
#include <gtest/gtest.h>

#include "version.h"

TEST(VersionString, HasAllThreeParts) {
	const auto v = helpers::versionString();
	EXPECT_EQ(v.rfind("Sable: v", 0), 0u) << v;
	EXPECT_NE(v.find(" PDFium: chromium/"), std::string::npos) << v;
	EXPECT_NE(v.find(" WindowsAppSDK: "), std::string::npos) << v;
}

// the build reads these from VERSION and .pdfium_version. If the macros
// didn't make it through, versionString() falls back to "dev"/"unknown".
TEST(VersionString, BuildFilledInTheVersions) {
	const auto v = helpers::versionString();
	EXPECT_EQ(v.find("dev"), std::string::npos) << v;
	EXPECT_EQ(v.find("unknown"), std::string::npos) << v;
}
