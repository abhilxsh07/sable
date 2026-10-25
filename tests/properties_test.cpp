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
#include <limits>
#include "properties.h"

TEST(PropertiesFormat, DatesKeepOffsetsAndValidateCalendar) {
	EXPECT_EQ(helpers::propertyDate("D:20260102123456+05'30'"),
	          "2026-01-02  12:34:56 UTC+05:30");
	EXPECT_EQ(helpers::propertyDate("20261018060000Z"),
	          "2026-10-18  06:00:00 UTC");
	EXPECT_EQ(helpers::propertyDate("D:2024"), "2024-01-01  00:00:00");
	EXPECT_EQ(helpers::propertyDate("D:20240229"), "2024-02-29  00:00:00");
	EXPECT_EQ(helpers::propertyDate("D:20260102123456-08'00'"),
	          "2026-01-02  12:34:56 UTC-08:00");
	for (const auto invalid :
	     {"", "D:", "D:20230229", "D:20261301", "D:2026010", "D:20260101240000",
	      "D:20260101006000", "D:20260101000000+25'00'",
	      "D:20260101000000+05'99'", "D:20260101000000Zjunk"}) {
		EXPECT_TRUE(helpers::propertyDate(invalid).empty()) << invalid;
	}
}
TEST(PropertiesFormat, FileSizesRetainExactBytes) {
	EXPECT_EQ(helpers::propertyFileSize(0), "0 bytes");
	EXPECT_EQ(helpers::propertyFileSize(1), "1 byte");
	EXPECT_EQ(helpers::propertyFileSize(1024), "1.0 KiB (1024 bytes)");
	EXPECT_EQ(helpers::propertyFileSize(1048576), "1.0 MiB (1048576 bytes)");
	EXPECT_NE(
	    helpers::propertyFileSize((std::numeric_limits<std::uint64_t>::max)())
	        .find("18446744073709551615 bytes"),
	    std::string::npos);
}
TEST(PropertiesFormat, PageSizesFollowRotationAndRecognizeCommonPaper) {
	EXPECT_EQ(helpers::propertyPageSize(612, 792),
	          "215.9 x 279.4 mm (Letter, portrait)");
	EXPECT_EQ(helpers::propertyPageSize(792, 612),
	          "279.4 x 215.9 mm (Letter, landscape)");
	EXPECT_EQ(helpers::propertyPageSize(595.28, 841.89),
	          "210.0 x 297.0 mm (A4, portrait)");
	EXPECT_EQ(helpers::propertyPageSize(100, 100), "35.3 x 35.3 mm (square)");
	EXPECT_TRUE(helpers::propertyPageSize(0, 100).empty());
	EXPECT_TRUE(
	    helpers::propertyPageSize(std::numeric_limits<double>::infinity(), 100)
	        .empty());
	EXPECT_EQ(helpers::propertyText(std::string("a\0b\tc\nd", 7)), "a b c\nd");
}
