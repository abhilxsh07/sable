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

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf_test_files.h"

// The sample files come from tests/fixtures/make_fixtures.py. Pages are drawn
// 1:1 unless a test says otherwise, so a point is a pixel. PDF's y runs up
// from the bottom of the page and the rows of the bitmap run down from the
// top, so row = page height - y. On the first page (595 x 842) the black
// square at y 592..792 sits in rows 50..250. I only look at the middle of a
// shape, the edges are anti-aliased.
namespace {
// a colour in the order the bitmap stores it, blue first
struct Color {
	int blue;
	int green;
	int red;
};

constexpr Color kBlack{.blue = 0, .green = 0, .red = 0};
constexpr Color kWhite{.blue = 255, .green = 255, .red = 255};
constexpr Color kRed{.blue = 0, .green = 0, .red = 255};
constexpr Color kGreen{.blue = 0, .green = 255, .red = 0};
constexpr Color kBlue{.blue = 255, .green = 0, .red = 0};

// where the pixel at column x, row y starts in the bitmap's bytes
std::size_t offsetOf(const pdf::Bitmap& bitmap, int x, int y) {
	const auto row = static_cast<std::size_t>(y);
	const auto column = static_cast<std::size_t>(x);
	return row * static_cast<std::size_t>(bitmap.stride) + column * 4;
}

// is the pixel at column x, row y this colour, and opaque?
::testing::AssertionResult hasColor(const pdf::Bitmap& bitmap, int x, int y,
                                    const Color& want) {
	if (x < 0 || y < 0 || x >= bitmap.width || y >= bitmap.height) {
		return ::testing::AssertionFailure()
		       << "(" << x << ", " << y << ") is outside the " << bitmap.width
		       << " x " << bitmap.height << " bitmap";
	}
	const std::size_t at = offsetOf(bitmap, x, y);
	if (at + 4 > bitmap.pixels.size()) {
		return ::testing::AssertionFailure()
		       << "the pixels stop before (" << x << ", " << y << ")";
	}
	const int blue = bitmap.pixels[at];
	const int green = bitmap.pixels[at + 1];
	const int red = bitmap.pixels[at + 2];
	const int alpha = bitmap.pixels[at + 3];
	if (blue == want.blue && green == want.green && red == want.red &&
	    alpha == 255) {
		return ::testing::AssertionSuccess();
	}
	return ::testing::AssertionFailure()
	       << "pixel (" << x << ", " << y << ") is B" << blue << " G" << green
	       << " R" << red << " A" << alpha << ", wanted B" << want.blue << " G"
	       << want.green << " R" << want.red << " A255";
}

// how many pixels in the block from (left, top) up to but not including
// (right, bottom) are dark, with blue, green and red all under 128
int countDark(const pdf::Bitmap& bitmap, int left, int top, int right,
              int bottom) {
	int dark = 0;
	for (int y = top; y < bottom; ++y) {
		for (int x = left; x < right; ++x) {
			const std::size_t at = offsetOf(bitmap, x, y);
			if (bitmap.pixels.at(at) < 128 && bitmap.pixels.at(at + 1) < 128 &&
			    bitmap.pixels.at(at + 2) < 128) {
				++dark;
			}
		}
	}
	return dark;
}

// Every test has an engine of its own. The pdf:: calls are all made inside
// the lambda given to engine.run(), and only the bitmap comes back out.
class PdfRenderTest : public ::testing::Test {
  protected:
	// Opens a sample file and draws one page of it, both on the PDFium thread.
	// nullopt when either of them fails.
	std::optional<pdf::Bitmap> render(std::string_view file, int index,
	                                  int width, int height, int rotation = 0,
	                                  const std::string& password = {}) {
		const auto path = fixturePath(file);
		// by reference is fine, the future is waited for before this returns
		const auto work = [&]() -> std::optional<pdf::Bitmap> {
			const auto opened = pdf::openFile(path, password);
			if (!opened.document) {
				return std::nullopt;
			}
			return opened.document->render(index, width, height, rotation);
		};
		return engine.run(work).get();
	}

	pdf::Engine engine;
};
} // namespace

TEST_F(PdfRenderTest, DrawsTheFirstPageAtFullSize) {
	const auto bitmap = render("pages.pdf", 0, 595, 842);
	ASSERT_TRUE(bitmap.has_value());
	EXPECT_EQ(bitmap->width, 595);
	EXPECT_EQ(bitmap->height, 842);
	EXPECT_EQ(bitmap->stride, 595 * 4);
	EXPECT_EQ(bitmap->pixels.size(),
	          static_cast<std::size_t>(bitmap->stride) *
	              static_cast<std::size_t>(bitmap->height));

	EXPECT_TRUE(hasColor(*bitmap, 150, 150, kBlack));
	EXPECT_TRUE(hasColor(*bitmap, 445, 150, kRed));
	EXPECT_TRUE(hasColor(*bitmap, 150, 692, kBlue));
	EXPECT_TRUE(hasColor(*bitmap, 450, 600, kWhite));
	EXPECT_TRUE(hasColor(*bitmap, 10, 10, kWhite));
}

// "Sable test page 1" is 24 pt with its baseline at y 400, so row 442. Its
// letters take up rows 424 to 447 or so.
TEST_F(PdfRenderTest, DrawsTheText) {
	const auto bitmap = render("pages.pdf", 0, 595, 842);
	ASSERT_TRUE(bitmap.has_value());
	EXPECT_GT(countDark(*bitmap, 50, 410, 300, 450), 200);
	// and the rows between the squares and the text are left blank
	EXPECT_EQ(countDark(*bitmap, 0, 260, 595, 410), 0);
}

TEST_F(PdfRenderTest, ScalesDownToHalfSize) {
	const auto bitmap = render("pages.pdf", 0, 298, 421);
	ASSERT_TRUE(bitmap.has_value());
	EXPECT_EQ(bitmap->width, 298);
	EXPECT_EQ(bitmap->height, 421);
	EXPECT_EQ(bitmap->stride, 298 * 4);

	EXPECT_TRUE(hasColor(*bitmap, 75, 75, kBlack));
	EXPECT_TRUE(hasColor(*bitmap, 222, 75, kRed));
	EXPECT_TRUE(hasColor(*bitmap, 75, 346, kBlue));
	EXPECT_TRUE(hasColor(*bitmap, 222, 346, kWhite));
}

// the green square on page 2 is at y 100..300 of 612, so rows 312..512
TEST_F(PdfRenderTest, DrawsTheLandscapePage) {
	const auto bitmap = render("pages.pdf", 1, 792, 612);
	ASSERT_TRUE(bitmap.has_value());
	EXPECT_EQ(bitmap->width, 792);
	EXPECT_EQ(bitmap->height, 612);

	EXPECT_TRUE(hasColor(*bitmap, 200, 412, kGreen));
	EXPECT_TRUE(hasColor(*bitmap, 600, 500, kWhite));
	EXPECT_TRUE(hasColor(*bitmap, 50, 50, kWhite));
}

// Page 3 is stored 300 x 200 with /Rotate 90 and the black on its left half.
// Shown turned, it's 200 x 300 with the black on top.
TEST_F(PdfRenderTest, AppliesThePagesOwnRotation) {
	const auto bitmap = render("pages.pdf", 2, 200, 300);
	ASSERT_TRUE(bitmap.has_value());
	EXPECT_EQ(bitmap->width, 200);
	EXPECT_EQ(bitmap->height, 300);

	EXPECT_TRUE(hasColor(*bitmap, 100, 40, kBlack));
	EXPECT_TRUE(hasColor(*bitmap, 100, 140, kBlack));
	EXPECT_TRUE(hasColor(*bitmap, 20, 280, kWhite));
	EXPECT_TRUE(hasColor(*bitmap, 150, 250, kWhite));
}

// a quarter turn clockwise: the top left corner goes to the top right, the top
// right to the bottom right and the bottom left to the top left
TEST_F(PdfRenderTest, RotatesAQuarterTurnClockwise) {
	const auto bitmap = render("pages.pdf", 0, 842, 595, 1);
	ASSERT_TRUE(bitmap.has_value());
	EXPECT_EQ(bitmap->width, 842);
	EXPECT_EQ(bitmap->height, 595);
	EXPECT_EQ(bitmap->stride, 842 * 4);

	EXPECT_TRUE(hasColor(*bitmap, 692, 150, kBlack));
	EXPECT_TRUE(hasColor(*bitmap, 692, 445, kRed));
	EXPECT_TRUE(hasColor(*bitmap, 150, 150, kBlue));
	EXPECT_TRUE(hasColor(*bitmap, 150, 445, kWhite));
}

// upside down: black ends up bottom right, red bottom left, blue top right
TEST_F(PdfRenderTest, RotatesAHalfTurn) {
	const auto bitmap = render("pages.pdf", 0, 595, 842, 2);
	ASSERT_TRUE(bitmap.has_value());
	EXPECT_EQ(bitmap->width, 595);
	EXPECT_EQ(bitmap->height, 842);

	EXPECT_TRUE(hasColor(*bitmap, 445, 692, kBlack));
	EXPECT_TRUE(hasColor(*bitmap, 150, 692, kRed));
	EXPECT_TRUE(hasColor(*bitmap, 445, 150, kBlue));
	EXPECT_TRUE(hasColor(*bitmap, 150, 150, kWhite));
}

// three quarter turns clockwise is one counter-clockwise: black ends up bottom
// left, red top left, blue bottom right
TEST_F(PdfRenderTest, RotatesThreeQuarterTurns) {
	const auto bitmap = render("pages.pdf", 0, 842, 595, 3);
	ASSERT_TRUE(bitmap.has_value());
	EXPECT_EQ(bitmap->width, 842);
	EXPECT_EQ(bitmap->height, 595);

	EXPECT_TRUE(hasColor(*bitmap, 150, 445, kBlack));
	EXPECT_TRUE(hasColor(*bitmap, 150, 150, kRed));
	EXPECT_TRUE(hasColor(*bitmap, 692, 445, kBlue));
	EXPECT_TRUE(hasColor(*bitmap, 692, 150, kWhite));
}

// Page 3 already has /Rotate 90, so one more quarter turn makes it a half turn
// from how it's stored: 300 x 200 with the black on the right.
TEST_F(PdfRenderTest, RotationComesOnTopOfThePagesOwn) {
	const auto bitmap = render("pages.pdf", 2, 300, 200, 1);
	ASSERT_TRUE(bitmap.has_value());
	EXPECT_EQ(bitmap->width, 300);
	EXPECT_EQ(bitmap->height, 200);

	EXPECT_TRUE(hasColor(*bitmap, 225, 100, kBlack));
	EXPECT_TRUE(hasColor(*bitmap, 75, 100, kWhite));
}

// password.pdf and restricted.pdf have the same page as pages.pdf, so what
// gets drawn has to match it to the last byte
TEST_F(PdfRenderTest, PasswordFileDrawsTheSamePixels) {
	const auto plain = render("pages.pdf", 0, 595, 842);
	const auto locked = render("password.pdf", 0, 595, 842, 0, "sable");
	ASSERT_TRUE(plain.has_value());
	ASSERT_TRUE(locked.has_value());
	EXPECT_EQ(locked->width, plain->width);
	EXPECT_EQ(locked->height, plain->height);
	EXPECT_TRUE(locked->pixels == plain->pixels);
}

TEST_F(PdfRenderTest, RestrictedFileDrawsTheSamePixels) {
	const auto plain = render("pages.pdf", 0, 595, 842);
	const auto restricted = render("restricted.pdf", 0, 595, 842);
	ASSERT_TRUE(plain.has_value());
	ASSERT_TRUE(restricted.has_value());
	EXPECT_EQ(restricted->width, plain->width);
	EXPECT_EQ(restricted->height, plain->height);
	EXPECT_TRUE(restricted->pixels == plain->pixels);
}

TEST_F(PdfRenderTest, RefusesWhatItCannotDraw) {
	struct Case {
		const char* what;
		int index;
		int width;
		int height;
		int rotation;
	};
	const std::vector<Case> cases = {
	    {"index -1", -1, 595, 842, 0},
	    {"index 3, one past the last page", 3, 595, 842, 0},
	    {"width 0", 0, 0, 842, 0},
	    {"height 0", 0, 595, 0, 0},
	    {"negative width", 0, -595, 842, 0},
	    {"negative height", 0, 595, -842, 0},
	    {"20000 x 20000", 0, 20000, 20000, 0},
	    {"70000 x 70000", 0, 70000, 70000, 0},
	    {"rotation 4", 0, 595, 842, 4},
	    {"rotation -1", 0, 595, 842, -1},
	};
	for (const Case& c : cases) {
		const auto bitmap =
		    render("pages.pdf", c.index, c.width, c.height, c.rotation);
		EXPECT_FALSE(bitmap.has_value()) << c.what;
	}
}
