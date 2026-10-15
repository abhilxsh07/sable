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
// port of app/main/app.ts yargs config (L345-376) and startup args (L394-410)
// see:
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/main/app.ts#L345-L376
#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "cli.h"

using helpers::CliArgs;
using helpers::fileUrlToPath;
using helpers::helpText;
using helpers::parseCli;

using Args = std::vector<std::string>;

// copied from what yargs 17.7.2 prints for this config with the script name
// set to "Sable" (and "sable.exe" below), piped, so 80 columns
TEST(HelpText, MatchesYargs) {
	const std::string expected = R"help(Usage: Sable [-p] <pdf>

Positionals:
  pdf, pdf  The pdf to open                                              [array]

Options:
      --help     Show help.                                            [boolean]
  -p, --pages    The page to open in the pdf                            [number]
      --version  Show version number                                   [boolean]

Examples:
  Sable -p 5 pdf.pdf  Loads pdf on the 5th page

copyright 2026)help";
	EXPECT_EQ(helpText("Sable", 2026), expected);
	EXPECT_EQ(helpers::cliText(parseCli({"--help"}), "Sable", "v", 2026),
	          expected);
	EXPECT_EQ(helpers::cliText(parseCli({"--version", "--help", "a.pdf"}),
	                           "Sable", "v", 2026),
	          expected);
}

TEST(HelpText, ColumnsFollowTheProgramName) {
	const std::string expected = R"help(Usage: sable.exe [-p] <pdf>

Positionals:
  pdf, pdf  The pdf to open                                              [array]

Options:
      --help     Show help.                                            [boolean]
  -p, --pages    The page to open in the pdf                            [number]
      --version  Show version number                                   [boolean]

Examples:
  sable.exe -p 5 pdf.pdf  Loads pdf on the 5th page

copyright 2026)help";
	EXPECT_EQ(helpText("sable.exe", 2026), expected);
}

TEST(HelpText, YearIsTheEpilog) {
	const auto text = helpText("Sable", 2031);
	EXPECT_NE(text.find("\n\ncopyright 2031"), std::string::npos) << text;
}

TEST(HelpText, NoLineIsWiderThanTheWrap) {
	std::istringstream lines(helpText("Sable", 2026));
	std::string line;
	while (std::getline(lines, line)) {
		EXPECT_LE(line.size(), 80u) << line;
	}
}

TEST(HelpText, HasNoCarriageReturnsOrTrailingNewline) {
	const auto text = helpText("Sable", 2026);
	EXPECT_EQ(text.find('\r'), std::string::npos);
	EXPECT_NE(text.back(), '\n');
}

TEST(ParseCli, NoArgs) {
	const CliArgs r = parseCli({});
	EXPECT_TRUE(r.files.empty());
	EXPECT_FALSE(r.page.has_value());
	EXPECT_FALSE(r.showHelp);
	EXPECT_FALSE(r.showVersion);
	EXPECT_TRUE(r.error.empty());
}

TEST(ParseCli, HelpAndVersion) {
	EXPECT_TRUE(parseCli({"--help"}).showHelp);
	EXPECT_FALSE(parseCli({"--help"}).showVersion);
	EXPECT_TRUE(parseCli({"--version"}).showVersion);
	EXPECT_FALSE(parseCli({"--version"}).showHelp);
	const CliArgs both = parseCli({"--version", "--help"});
	EXPECT_TRUE(both.showHelp);
	EXPECT_TRUE(both.showVersion);
}

TEST(ParseCli, SinglePdf) {
	const CliArgs r = parseCli({"a.pdf"});
	EXPECT_EQ(r.files, Args({"a.pdf"}));
	EXPECT_FALSE(r.page.has_value());
	EXPECT_TRUE(r.error.empty());
}

TEST(ParseCli, SeveralPdfs) {
	const CliArgs r = parseCli({"a.pdf", "b.pdf", "c.pdf"});
	EXPECT_EQ(r.files, Args({"a.pdf", "b.pdf", "c.pdf"}));
}

TEST(ParseCli, AllFourWaysToSayPage) {
	for (const Args& args :
	     {Args{"-p", "5", "a.pdf"}, Args{"-p=5", "a.pdf"},
	      Args{"--pages", "5", "a.pdf"}, Args{"--pages=5", "a.pdf"}}) {
		const CliArgs r = parseCli(args);
		EXPECT_EQ(r.files, Args({"a.pdf"})) << args[0];
		ASSERT_TRUE(r.page.has_value()) << args[0];
		EXPECT_EQ(*r.page, 5) << args[0];
		EXPECT_TRUE(r.error.empty()) << args[0];
	}
}

TEST(ParseCli, PageGlued) {
	const CliArgs r = parseCli({"-p12", "a.pdf"});
	ASSERT_TRUE(r.page.has_value());
	EXPECT_EQ(*r.page, 12);
}

TEST(ParseCli, OptionsAfterPositionals) {
	const CliArgs r = parseCli({"a.pdf", "b.pdf", "-p", "3"});
	EXPECT_EQ(r.files, Args({"a.pdf", "b.pdf"}));
	ASSERT_TRUE(r.page.has_value());
	EXPECT_EQ(*r.page, 3);
}

TEST(ParseCli, OptionsBetweenPositionals) {
	const CliArgs r = parseCli({"a.pdf", "--pages=7", "b.pdf"});
	EXPECT_EQ(r.files, Args({"a.pdf", "b.pdf"}));
	ASSERT_TRUE(r.page.has_value());
	EXPECT_EQ(*r.page, 7);
}

TEST(ParseCli, PageThatLooksLikeAFileName) {
	// the value belongs to -p, the pdf is the one after it
	const CliArgs r = parseCli({"-p", "5", "5.pdf"});
	EXPECT_EQ(r.files, Args({"5.pdf"}));
	EXPECT_EQ(r.page, 5);
}

TEST(ParseCli, LastPageWins) {
	const CliArgs r = parseCli({"-p", "1", "-p", "2", "a.pdf"});
	EXPECT_EQ(r.page, 2);
}

// yargs gives app.ts a NaN, which `if (p)` skips: open on the first page
TEST(ParseCli, PageThatIsNotANumberIsIgnored) {
	for (const char* bad : {"abc", "2.5", "1e2", "0x10", "", "5x"}) {
		const CliArgs r = parseCli({"-p", bad, "a.pdf"});
		EXPECT_EQ(r.files, Args({"a.pdf"})) << bad;
		EXPECT_FALSE(r.page.has_value()) << bad;
		EXPECT_TRUE(r.error.empty()) << bad;
	}
	const CliArgs eq = parseCli({"--pages=abc", "a.pdf"});
	EXPECT_FALSE(eq.page.has_value());
	EXPECT_TRUE(eq.error.empty());
}

TEST(ParseCli, PageZeroOrNegativeIsIgnored) {
	EXPECT_FALSE(parseCli({"-p", "0", "a.pdf"}).page.has_value());
	EXPECT_FALSE(parseCli({"-p", "-3", "a.pdf"}).page.has_value());
	EXPECT_FALSE(parseCli({"-p", "99999999999", "a.pdf"}).page.has_value());
}

TEST(ParseCli, PageWithoutAValue) {
	// at the end, or followed by another option
	const CliArgs end = parseCli({"a.pdf", "-p"});
	EXPECT_EQ(end.files, Args({"a.pdf"}));
	EXPECT_FALSE(end.page.has_value());
	EXPECT_TRUE(end.error.empty());
	const CliArgs help = parseCli({"-p", "--help"});
	EXPECT_TRUE(help.showHelp);
	EXPECT_FALSE(help.page.has_value());
}

TEST(ParseCli, PageWithoutAPdfIsDropped) {
	const CliArgs r = parseCli({"-p", "5"});
	EXPECT_TRUE(r.files.empty());
	EXPECT_FALSE(r.page.has_value());
}

TEST(ParseCli, UnknownOptionsAreIgnored) {
	const CliArgs r = parseCli({"a.pdf", "--hlep"});
	EXPECT_TRUE(r.error.empty());
	EXPECT_EQ(r.files, Args({"a.pdf"}));
	// and they don't take the next argument with them
	EXPECT_EQ(parseCli({"-x", "c.pdf"}).files, Args({"c.pdf"}));
	EXPECT_EQ(parseCli({"--color=red", "b.pdf"}).files, Args({"b.pdf"}));
}

TEST(FileUrl, LeavesNulAndBrokenUtf8Encoded) {
	EXPECT_EQ(helpers::fileUrlToPath("file:///C:/a%00b.pdf"), "C:/a%00b.pdf");
	EXPECT_EQ(helpers::fileUrlToPath("file:///C:/a%FFb.pdf"), "C:/a%FFb.pdf");
	EXPECT_EQ(helpers::fileUrlToPath("file:///C:/%C3%A9t%C3%A9.pdf"),
	          "C:/\xC3\xA9t\xC3\xA9.pdf");
}

TEST(ParseCli, DoubleDashEndsTheOptions) {
	const CliArgs r = parseCli({"--", "-p", "--help"});
	EXPECT_EQ(r.files, Args({"-p", "--help"}));
	EXPECT_FALSE(r.showHelp);
	EXPECT_TRUE(r.error.empty());
}

TEST(ParseCli, LoneDashAndNumbersAreFiles) {
	const CliArgs r = parseCli({"-", "-5", "12"});
	EXPECT_EQ(r.files, Args({"-", "-5", "12"}));
}

TEST(ParseCli, PathsWithSpacesAndUnicodeStayIntact) {
	// "Zo\u00eb" and "\u65e5\u672c\u8a9e" as UTF-8 bytes, in octal so the
	// compiler's source charset doesn't matter
	const std::string zoe = "Zo\303\253";
	const std::string nihongo = "\346\227\245\346\234\254\350\252\236";
	const std::string a =
	    "C:\\Users\\" + zoe + "\\My Docs\\" + nihongo + ".pdf";
	const std::string b = "/home/a b/c d.pdf";
	const CliArgs r = parseCli({a, "-p", "2", b});
	EXPECT_EQ(r.files, Args({a, b}));
	EXPECT_EQ(r.page, 2);
}

TEST(ParseCli, FileUrlsAreDecoded) {
	const CliArgs r = parseCli({"file:///C:/x%20y.pdf", "file:///D:/a.pdf"});
	EXPECT_EQ(r.files, Args({"C:/x y.pdf", "D:/a.pdf"}));
}

// app.ts threw the result of replace() away for a single path
TEST(ParseCli, SingleFileUrlIsDecodedToo) {
	const CliArgs r = parseCli({"file:///C:/x%20y.pdf", "-p", "4"});
	EXPECT_EQ(r.files, Args({"C:/x y.pdf"}));
	EXPECT_EQ(r.page, 4);
}

TEST(FileUrlToPath, DriveLetters) {
	EXPECT_EQ(fileUrlToPath("file:///C:/x%20y.pdf"), "C:/x y.pdf");
	EXPECT_EQ(fileUrlToPath("file:///c:/Users/me/a.pdf"), "c:/Users/me/a.pdf");
	EXPECT_EQ(fileUrlToPath("file://C:/a.pdf"), "C:/a.pdf");
	EXPECT_EQ(fileUrlToPath("file://localhost/C:/a.pdf"), "C:/a.pdf");
}

TEST(FileUrlToPath, OtherShapes) {
	EXPECT_EQ(fileUrlToPath("file:///tmp/a.pdf"), "/tmp/a.pdf");
	EXPECT_EQ(fileUrlToPath("FILE:///C:/a.pdf"), "C:/a.pdf");
	EXPECT_EQ(fileUrlToPath("file://server/share/a%20b.pdf"),
	          "//server/share/a b.pdf");
}

TEST(FileUrlToPath, DecodesUtf8) {
	EXPECT_EQ(fileUrlToPath("file:///C:/Zo%C3%AB/%E6%97%A5.pdf"),
	          "C:/Zo\303\253/\346\227\245.pdf");
}

TEST(FileUrlToPath, BrokenEscapesStay) {
	EXPECT_EQ(fileUrlToPath("file:///C:/100%.pdf"), "C:/100%.pdf");
	EXPECT_EQ(fileUrlToPath("file:///C:/a%zzb.pdf"), "C:/a%zzb.pdf");
	EXPECT_EQ(fileUrlToPath("file:///C:/a%2"), "C:/a%2");
}

TEST(FileUrlToPath, NormalPathsAreUntouched) {
	for (const char* path :
	     {"a.pdf", "C:\\docs\\a%20b.pdf", "C:/docs/a b.pdf", "/tmp/a%20b.pdf",
	      "\\\\server\\share\\a.pdf", "file.pdf", "file:a.pdf", ""}) {
		EXPECT_EQ(fileUrlToPath(path), path);
	}
}

TEST(CliText, VersionSkipsFilesAndPageArguments) {
	EXPECT_EQ(helpers::cliText(parseCli({"a.pdf", "-p", "2", "--version"}),
	                           "Sable", "Sable: example", 2026),
	          "Sable: example");
}

TEST(CliText, NormalLaunchHasNoOutput) {
	EXPECT_FALSE(helpers::cliText(parseCli({}), "Sable", "v", 2026));
	EXPECT_FALSE(
	    helpers::cliText(parseCli({"a.pdf", "-p", "2"}), "Sable", "v", 2026));
	EXPECT_FALSE(
	    helpers::cliText(parseCli({"--", "--help"}), "Sable", "v", 2026));
}
