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
#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf/write_guard.h"
#include "pdf_test_files.h"

TEST(WriteGuard, TokensOnlyCountRealSignatureNames) {
	using pdf::detail::signatureTokens;
	EXPECT_TRUE(signatureTokens("<< /FT /Sig /V << /Contents <00> >> >>"));
	EXPECT_TRUE(
	    signatureTokens("<< /V << /Contents (signed) >> /FT /S#69g >>"));
	EXPECT_TRUE(signatureTokens("<< /TransformMethod /D#6fcMDP >>"));
	EXPECT_TRUE(signatureTokens("<< /Perms << /UR3 9 0 R >> >>"));
	EXPECT_TRUE(signatureTokens("<< /Perms << >> >>"));
	EXPECT_TRUE(signatureTokens("<< /Length 4 >>\nstream\nabcd\nendstream\n"
	                            "<< /FT /Sig /V << /Contents <00> >> >>"));
	EXPECT_FALSE(signatureTokens(""));
	EXPECT_FALSE(
	    signatureTokens("<< /SigFlags 0 /BaseFont /Signika-Regular >>"));
	EXPECT_FALSE(signatureTokens("<< /Font << /Sig 5 0 R >> >>"));
	EXPECT_FALSE(signatureTokens("1 0 obj\n<< /FT /Sig >>\nendobj"));
	EXPECT_FALSE(signatureTokens("7 0 obj\n/Sig\nendobj"));
	EXPECT_FALSE(signatureTokens("<< /Type /Sig /Contents <00> >>"));
	EXPECT_FALSE(signatureTokens("<< /FT /Sig /V null >>"));
	EXPECT_FALSE(signatureTokens("<< /FT /Sig /V << /Type /Sig >> >>"));
	EXPECT_FALSE(signatureTokens("<< /FT /Sig /V << /Contents < > >> >>"));
	EXPECT_FALSE(signatureTokens("<< /FT /Sig /V << /Contents () >> >>"));
	EXPECT_FALSE(signatureTokens("<< /UR3 9 0 R >>"));
	EXPECT_FALSE(signatureTokens("<< /Contents (/FT /Sig \\) /DocMDP) >>"));
	EXPECT_FALSE(
	    signatureTokens("% /FT /Sig\n<< /T <2F5369672F446F634D4450> >>"));
	EXPECT_FALSE(signatureTokens(
	    "<< /Length 18 >>\nstream\n/FT /Sig /DocMDP x\nendstream"));
	// a wrong /Length falls back to the endstream keyword.
	EXPECT_FALSE(signatureTokens("<< /Length 3 >>\nstream\n/FT /Sig /DocMDP\n"
	                             "endstream\n<< /SigFlags 1 >>"));
}

TEST(WriteGuard, FollowsSignatureValuesAndInheritedFieldTypes) {
	using pdf::detail::signatureTokens;
	const std::string value = "8 2 obj\n<< /Contents <00> >>\nendobj\n";
	EXPECT_TRUE(
	    signatureTokens(value + "1 0 obj\n<< /FT /Sig /V 8 2 R >>\nendobj"));
	EXPECT_TRUE(
	    signatureTokens("1 0 obj\n<< /FT /Sig /V 8 2 R >>\nendobj\n" + value));
	EXPECT_TRUE(signatureTokens(
	    "1 0 obj\n<< /FT /Sig >>\nendobj\n"
	    "2 0 obj\n<< /Parent 1 0 R /V 8 % a reference\n2 R >>\nendobj\n" +
	    value));
	EXPECT_FALSE(
	    signatureTokens(value + "1 0 obj\n<< /FT /Sig /V 8 0 R >>\nendobj"));
	EXPECT_FALSE(
	    signatureTokens(value + "1 0 obj\n<< /FT /Tx /V 8 2 R >>\nendobj"));
	EXPECT_FALSE(
	    signatureTokens("1 0 obj\n<< /Parent 2 0 R /V 8 2 R >>\nendobj\n"
	                    "2 0 obj\n<< /Parent 1 0 R >>\nendobj\n" +
	                    value));
	EXPECT_FALSE(
	    signatureTokens("<< /FT /Sig /V << /Other << /Contents <00> >> >> >>"));
	EXPECT_FALSE(signatureTokens("<< /FT /Sig /V << /Contents (\\\n) >> >>"));
	EXPECT_FALSE(signatureTokens("<< /FT /Sig /V << /Contents [<00>] >> >>"));
}

TEST(WriteGuard, SignedPdfRefusesNotesHighlightsAndDelete) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("note_signed.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto original = doc.serialize(1024 * 1024);
		    const auto notes = doc.annotations(0);
		    ASSERT_TRUE(original && notes);
		    ASSERT_EQ(notes->entries.size(), 1u);
		    EXPECT_EQ(doc.writeRefusal(), pdf::WriteRefusal::None);
		    EXPECT_FALSE(doc.addTextNote(0, 80, 100, u"New"));
		    EXPECT_EQ(doc.writeRefusal(), pdf::WriteRefusal::Signed);
		    EXPECT_FALSE(doc.highlightText({{0, 0}, {0, 4}}));
		    EXPECT_FALSE(
		        doc.markText({{0, 0}, {0, 4}}, pdf::TextMarkup::Underline));
		    EXPECT_FALSE(
		        doc.markText({{0, 0}, {0, 4}}, pdf::TextMarkup::StrikeOut));
		    EXPECT_FALSE(doc.removeAnnotation(notes->entries[0].target));
		    EXPECT_FALSE(doc.updateTextNote(0, 0, u"From another app",
		                                    u"Changed", notes->revision));
		    EXPECT_FALSE(
		        doc.addRectangle(0, {60, 200, 160, 260}, notes->revision));
		    EXPECT_EQ(doc.canWrite({}), pdf::WriteRefusal::Signed);
		    EXPECT_FALSE(doc.modified());
		    EXPECT_EQ(doc.serialize(1024 * 1024), original);
		    const auto after = doc.annotations(0);
		    ASSERT_TRUE(after);
		    EXPECT_EQ(after->entries.size(), 1u);
		    EXPECT_EQ(after->revision, notes->revision);
	    })
	    .get();
}

TEST(WriteGuard, NamesThatOnlyLookSignedStillAcceptEdits) {
	pdf::Engine engine;
	engine
	    .run([] {
		    // the fixture has /SigFlags 0, a /Signika-Regular font and a
		    // page comment that spells out /FT /Sig /DocMDP.
		    const auto opened = pdf::openFile(fixturePath("note_signika.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    EXPECT_EQ(doc.canWrite({}), pdf::WriteRefusal::None);
		    ASSERT_EQ(doc.addTextNote(0, 80, 100, u"New"), 1);
		    EXPECT_EQ(doc.highlightText({{0, 0}, {0, 4}}), 1);
		    auto notes = doc.annotations(0);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 3u);
		    EXPECT_TRUE(
		        doc.addRectangle(0, {60, 200, 160, 260}, notes->revision));
		    notes = doc.annotations(0);
		    ASSERT_TRUE(notes);
		    ASSERT_EQ(notes->entries.size(), 4u);
		    EXPECT_EQ(notes->entries[0].contents, "From another app");
		    EXPECT_EQ(doc.removeAnnotation(notes->entries[0].target), true);
		    EXPECT_EQ(doc.writeRefusal(), pdf::WriteRefusal::None);
		    EXPECT_TRUE(doc.modified());
		    const auto bytes = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto kept = saved.document->annotations(0);
		    ASSERT_TRUE(kept);
		    EXPECT_EQ(kept->entries.size(), 3u);
	    })
	    .get();
}
