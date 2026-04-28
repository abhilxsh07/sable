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
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/
#include <gtest/gtest.h>
#include <fpdf_annot.h>
#include <fpdfview.h>
#include <cstdio>
#include <set>
#include "pdf/document.h"
#include "pdf/engine.h"
#include "pdf_test_files.h"

namespace {
std::vector<std::uint8_t> deletionBytes(std::string extra = {},
                                        bool shared = false,
                                        bool independent = false,
                                        bool inbound = false,
                                        std::string_view subtype = "Text") {
	std::string data = "%PDF-1.7\n";
	std::vector<std::size_t> offsets;
	for (const auto& body : std::vector<std::string>{
	         "<< /Type /Catalog /Pages 2 0 R >>",
	         "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
	         "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
	         "/Annots 5 0 R >>",
	         "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
	         "/Annots " +
	             std::string(shared        ? "5 0 R"
	                         : independent ? "[6 0 R]"
	                                       : "[]") +
	             " >>",
	         "[6 0 R 7 0 R 8 0 R]",
	         "<< /Type /Annot /Subtype /" + std::string(subtype) +
	             " /Rect [40 50 60 70] /Contents (Same) " + extra + " >>",
	         "<< /Type /Annot /Subtype /Text /Rect [80 50 100 70] "
	         "/Contents (Same) /Popup 8 0 R " +
	             std::string(inbound ? "/IRT 6 0 R" : "") + " >>",
	         "<< /Type /Annot /Subtype /Popup /Rect [80 50 100 70] "
	         "/Parent 7 0 R /Contents (Keep popup) >>"}) {
		offsets.push_back(data.size());
		data +=
		    std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
	}
	const auto xref = data.size();
	data += "xref\n0 9\n0000000000 65535 f \n";
	for (const auto offset : offsets) {
		char line[21];
		std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
		data += line;
	}
	data += "trailer\n<< /Size 9 /Root 1 0 R >>\nstartxref\n" +
	        std::to_string(xref) + "\n%%EOF\n";
	return {data.begin(), data.end()};
}
} // namespace

TEST(AnnotationTargets, RemovingAnEarlierIdenticalNoteRejectsOldRequests) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(deletionBytes());
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto before = doc.annotations(0);
		    ASSERT_TRUE(before && before->entries.size() == 3);
		    const auto stale = before->entries[1].target;
		    ASSERT_EQ(doc.removeAnnotation(before->entries[0].target), true);
		    EXPECT_FALSE(doc.removeAnnotation(stale));
		    EXPECT_FALSE(doc.textNote(0, 0));
		    EXPECT_FALSE(doc.textNote(0, 0, stale.revision));
		    EXPECT_FALSE(doc.updateTextNote(0, 0, u"Same", u"Wrong"));
		    EXPECT_FALSE(
		        doc.updateTextNote(0, 0, u"Same", u"Wrong", stale.revision));
		    const auto after = doc.annotations(0);
		    ASSERT_TRUE(after && after->entries.size() == 2);
		    EXPECT_EQ(after->revision, before->revision + 1);
		    EXPECT_EQ(after->entries[0].contents, "Same");
		    EXPECT_EQ(after->entries[1].contents, "Keep popup");
		    EXPECT_EQ(
		        doc.updateTextNote(0, 0, u"Same", u"Right", after->revision),
		        true);
		    ASSERT_EQ(doc.addTextNote(0, 120, 50, u"Appended"), 2);
		    const auto bytes = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto saved = pdf::openDocument(*bytes);
		    ASSERT_TRUE(saved.document);
		    const auto notes = saved.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 3);
		    EXPECT_EQ(notes->entries[0].contents, "Right");
		    EXPECT_EQ(notes->entries[1].contents, "Keep popup");
		    EXPECT_EQ(notes->entries[2].contents, "Appended");
	    })
	    .get();
}

TEST(AnnotationTargets, SharedProtectedAndLinkedTargetsRemainUntouched) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto subtype : {"Text", "FreeText", "Square"}) {
			    const auto fixture =
			        [subtype](std::string extra = {}, bool shared = false,
			                  bool independent = false, bool inbound = false) {
				        return deletionBytes(extra, shared, independent,
				                             inbound, subtype);
			        };
			    for (const auto& source :
			         {fixture("/F 64"), fixture("/F 128"), fixture("/F 512"),
			          fixture("/Popup 8 0 R"), fixture("/IRT 7 0 R"),
			          fixture("/Parent 7 0 R"), fixture("/RT /R"),
			          fixture("", true), fixture("", false, true),
			          fixture("", false, false, true)}) {
				    const auto opened = pdf::openDocument(source);
				    ASSERT_TRUE(opened.document);
				    auto& doc = *opened.document;
				    const auto before = doc.annotations(0);
				    ASSERT_TRUE(before && before->entries.size() == 3);
				    EXPECT_EQ(before->entries[0].subtype, subtype);
				    EXPECT_FALSE(
				        doc.removeAnnotation(before->entries[0].target));
				    EXPECT_EQ(doc.annotationRevision(), before->revision);
				    EXPECT_FALSE(doc.modified());
				    EXPECT_EQ(doc.serialize(1024 * 1024), source);
			    }
		    }
	    })
	    .get();
}

TEST(AnnotationTargets,
     RemovingIndependentShapesAndTextBoxesKeepsNoteAndPopup) {
	pdf::Engine engine;
	engine
	    .run([] {
		    for (const auto subtype : {"FreeText", "Square"}) {
			    const auto opened = pdf::openDocument(
			        deletionBytes("", false, false, false, subtype));
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    const auto before = doc.annotations(0);
			    ASSERT_TRUE(before && before->entries.size() == 3);
			    ASSERT_EQ(before->entries[0].subtype, subtype);
			    ASSERT_EQ(doc.removeAnnotation(before->entries[0].target),
			              true);
			    const auto after = doc.annotations(0);
			    ASSERT_TRUE(after && after->entries.size() == 2);
			    EXPECT_EQ(after->revision, before->revision + 1);
			    EXPECT_EQ(after->entries[0].subtype, "Text");
			    EXPECT_EQ(after->entries[0].contents, "Same");
			    EXPECT_EQ(after->entries[1].subtype, "Popup");
			    EXPECT_EQ(after->entries[1].contents, "Keep popup");
			    EXPECT_FALSE(doc.removeAnnotation(before->entries[0].target));
		    }
	    })
	    .get();
}

TEST(AnnotationTargets, ClearingACommentRetainsItsMarkupAndAppearance) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("text.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    ASSERT_EQ(
		        doc.markText({{0, 0}, {0, 4}}, pdf::TextMarkup::Highlight), 1);
		    const auto comment =
		        doc.markupComment(0, 0, pdf::TextMarkup::Highlight);
		    ASSERT_TRUE(comment);
		    ASSERT_EQ(doc.updateMarkupComment(0, 0, pdf::TextMarkup::Highlight,
		                                      *comment, u"Review"),
		              true);
		    const auto before = doc.annotations(0);
		    const auto image = doc.render(0, 400, 300);
		    ASSERT_TRUE(before && image && before->entries.size() == 1);
		    ASSERT_FALSE(before->entries[0].quads.empty());
		    EXPECT_FALSE(doc.updateMarkupComment(
		        0, 0, pdf::TextMarkup::Highlight, {u"Review", true}, {}));
		    ASSERT_EQ(doc.clearMarkupComment(before->entries[0].target,
		                                     {u"Review", true}),
		              true);
		    const auto after = doc.annotations(0);
		    const auto current = doc.render(0, 400, 300);
		    ASSERT_TRUE(after && current && after->entries.size() == 1);
		    EXPECT_EQ(after->revision, before->revision);
		    EXPECT_EQ(after->entries[0].subtype, "Highlight");
		    EXPECT_TRUE(after->entries[0].contents.empty());
		    EXPECT_EQ(after->entries[0].quads.size(),
		              before->entries[0].quads.size());
		    EXPECT_EQ(current->pixels, image->pixels);
		    EXPECT_FALSE(doc.clearMarkupComment(before->entries[0].target,
		                                        {u"Review", true}));
		    EXPECT_EQ(
		        doc.clearMarkupComment(before->entries[0].target, {{}, true}),
		        false);
	    })
	    .get();
}

TEST(AnnotationTargets, ClearingCommentTextKeepsItsPopupReferences) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(deletionBytes(
		        "/Popup 8 0 R", false, false, false, "Highlight"));
		    ASSERT_TRUE(opened.document);
		    const auto notes = opened.document->annotations(0);
		    ASSERT_TRUE(notes && !notes->entries.empty());
		    const auto target = notes->entries[0].target;
		    EXPECT_EQ(
		        opened.document->clearMarkupComment(target, {u"Same", true}),
		        true);
		    const auto after = opened.document->annotations(0);
		    ASSERT_TRUE(after &&
		                after->entries.size() == notes->entries.size());
		    EXPECT_TRUE(after->entries[0].contents.empty());
		    EXPECT_EQ(after->entries[2].contents, "Keep popup");
	    })
	    .get();
}

TEST(AnnotationTargets, SavedSidebarNotesHaveNoScreenOrPrintIcon) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openFile(fixturePath("pages.pdf"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto plain = doc.render(1, 360, 280);
		    const auto print = doc.printRaster(1, 360, 280, 0, 0, 360, 280);
		    ASSERT_TRUE(plain && print);
		    ASSERT_EQ(doc.addTextNote(1, 40, 50, u"Sidebar only"), 0);
		    const auto bytes = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto reopened = pdf::openDocument(*bytes);
		    ASSERT_TRUE(reopened.document);
		    const auto notes = reopened.document->annotations(1);
		    ASSERT_TRUE(notes && notes->entries.size() == 1);
		    EXPECT_EQ(notes->entries[0].flags & FPDF_ANNOT_FLAG_NOVIEW,
		              FPDF_ANNOT_FLAG_NOVIEW);
		    EXPECT_EQ(notes->entries[0].flags & FPDF_ANNOT_FLAG_PRINT, 0);
		    EXPECT_EQ(notes->entries[0].contents, "Sidebar only");
		    const auto screen = reopened.document->render(1, 360, 280);
		    const auto printed =
		        reopened.document->printRaster(1, 360, 280, 0, 0, 360, 280);
		    ASSERT_TRUE(screen && printed);
		    EXPECT_EQ(screen->pixels, plain->pixels);
		    EXPECT_EQ(printed->pixels, print->pixels);
	    })
	    .get();
}

TEST(AnnotationTargets, EditingAnImportedNoteHidesOnlyItsIconFlags) {
	pdf::Engine engine;
	engine
	    .run([] {
		    const auto opened = pdf::openDocument(deletionBytes("/F 132"));
		    ASSERT_TRUE(opened.document);
		    auto& doc = *opened.document;
		    const auto before = doc.annotations(0);
		    ASSERT_TRUE(before && before->entries.size() == 3);
		    ASSERT_EQ(before->entries[0].flags, 132);
		    ASSERT_EQ(doc.updateTextNote(0, 0, u"Same", u"Changed"), true);
		    const auto bytes = doc.serialize(1024 * 1024);
		    ASSERT_TRUE(bytes);
		    const auto reopened = pdf::openDocument(*bytes);
		    ASSERT_TRUE(reopened.document);
		    const auto notes = reopened.document->annotations(0);
		    ASSERT_TRUE(notes && notes->entries.size() == 3);
		    EXPECT_EQ(notes->entries[0].contents, "Changed");
		    EXPECT_EQ(notes->entries[0].flags, 160);
		    EXPECT_EQ(notes->entries[1].flags, before->entries[1].flags);
		    EXPECT_EQ(notes->entries[1].contents, "Same");
	    })
	    .get();
}

TEST(AnnotationTargets, FiveHighlightColorsPersistBeforeAnyViewerRender) {
	pdf::Engine engine;
	engine
	    .run([] {
		    std::set<std::vector<std::uint8_t>> images;
		    for (const auto color :
		         {pdf::HighlightColor::Yellow, pdf::HighlightColor::Green,
		          pdf::HighlightColor::LightBlue, pdf::HighlightColor::Pink,
		          pdf::HighlightColor::Red}) {
			    const auto opened = pdf::openFile(fixturePath("text.pdf"));
			    ASSERT_TRUE(opened.document);
			    auto& doc = *opened.document;
			    ASSERT_EQ(doc.markText({{0, 0}, {0, 4}},
			                           pdf::TextMarkup::Highlight, color),
			              1);
			    const auto bytes = doc.serialize(1024 * 1024);
			    ASSERT_TRUE(bytes);
			    const auto reopened = pdf::openDocument(*bytes);
			    ASSERT_TRUE(reopened.document);
			    const auto before = doc.render(0, 400, 300);
			    const auto after = reopened.document->render(0, 400, 300);
			    ASSERT_TRUE(before && after);
			    EXPECT_EQ(after->pixels, before->pixels);
			    images.insert(after->pixels);
		    }
		    EXPECT_EQ(images.size(), 5u);
	    })
	    .get();
}
