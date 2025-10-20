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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "text.h"
#include "search.h"
#include "annotations.h"
#include "../helpers/attachment_file.h"

// PDFium's handle type, declared here so this header doesn't need fpdfview.h
struct fpdf_document_t__;

namespace pdf {
enum class Error {
	None,
	File,             // the file couldn't be read
	Format,           // not a PDF, or too damaged to open
	PasswordRequired, // it's encrypted and no password was given
	WrongPassword,
	Security, // protected in a way PDFium can't open
	Unknown,
};

using SaveResult = helpers::FileSaveResult;

// what to tell the user, in plain words ("" for None)
std::string_view errorMessage(Error error);

// in points (1/72 inch), the way the page is shown, so a page with /Rotate 90
// comes back with width and height swapped
struct PageSize {
	float width = 0;
	float height = 0;
};

// 8-bit BGRA, top row first, `stride` bytes per row. Pages are drawn onto
// white, so alpha is always 255.
struct Bitmap {
	int width = 0;
	int height = 0;
	int stride = 0;
	std::vector<std::uint8_t> pixels;
};

enum class LinkFit { Keep, Page, Width, Height, Rectangle };
struct LinkDestination {
	int page = -1;
	LinkFit fit = LinkFit::Keep;
	// fractions of the displayed page, after rotation and cropping
	std::optional<double> x, y;
	std::optional<double> zoom; // scale, where 1 is 100%; absent means keep it
	double width = 0, height = 0; // Rectangle only
};
struct Link {
	std::optional<LinkDestination> destination;
	std::string uri; // http(s) only; empty for an internal destination
};

struct OutlineNode {
	std::string title;
	bool canFollow = false;
	int parent = -1; // index of an earlier entry; -1 for a root
	bool expanded = false;
};
struct Outline {
	std::vector<OutlineNode> entries;
	bool limited = false;
};
struct Attachment {
	int index = 0;
	std::string name;
};
struct Attachments {
	std::vector<Attachment> entries;
	bool limited = false;
};

class Document;

struct OpenResult {
	std::unique_ptr<Document> document; // null unless error is None
	Error error = Error::None;
};

// Everything from here on only works on the PDFium thread, so call it from
// Engine::run()/post(), and destroy documents there too. Anywhere else it
// does nothing and returns an empty result (and asserts in debug builds).

// opens a PDF that's already in memory. The document keeps the data, PDFium
// reads from it for as long as the document is open.
OpenResult openDocument(std::vector<std::uint8_t> data,
                        const std::string& password = {});
// reads the whole file into memory first, so it isn't kept open or locked
OpenResult openFile(const std::filesystem::path& path,
                    const std::string& password = {});

class Document {
  public:
	~Document();

	Document(const Document&) = delete;
	Document& operator=(const Document&) = delete;

	int pageCount() const;
	// nullopt for an index that isn't a page
	std::optional<PageSize> pageSize(int index) const;
	// UTF-8, "" when the PDF doesn't label its pages
	std::string pageLabel(int index) const;
	bool canCopy() const;
	bool canAnnotate() const;
	// Lower left of a 20-point icon, in unrotated PDF coordinates.
	// In memory only. Returns its page annotation index, or nullopt on failure.
	// A failed rollback throws; the incomplete note may still be present.
	std::optional<int> addTextNote(int page, float x, float y,
	                               std::u16string_view contents,
	                               std::u16string_view author = {});
	// Serialization does not clear this flag or replace the backing bytes.
	bool modified() const;
	// Edited files with attachments or uninspectable annotation lists fail.
	// nullopt means failure, cancellation or the output exceeding limit.
	std::optional<std::vector<std::uint8_t>>
	serialize(std::size_t limit,
	          const std::function<bool()>& canceled = {}) const;
	// Writes edited bytes to a different file, keeping this document modified.
	SaveResult saveEdits(const std::filesystem::path& destination,
	                     const std::filesystem::path& source, std::size_t limit,
	                     const std::function<bool()>& canceled = {}) const;
	// All subtypes, in page order. nullopt means failure or cancellation.
	std::optional<Annotations>
	annotations(int page, const std::function<bool()>& canceled = {}) const;
	std::optional<Attachments>
	attachments(const std::function<bool()>& canceled = {}) const;
	std::optional<std::vector<std::uint8_t>>
	attachment(int index, const std::function<bool()>& canceled = {}) const;
	std::optional<Outline>
	outline(const std::function<bool()>& canceled = {}) const;
	std::optional<Link> outlineLink(std::size_t index) const;
	std::optional<TextPage> textPage(int index) const;
	std::optional<std::string> textRange(int page, int first, int end) const;
	std::optional<FindPage>
	findPage(int page, const std::u16string& query, FindOptions options,
	         std::size_t limit,
	         const std::function<bool()>& canceled = {}) const;
	// x/y are fractions of the displayed page, measured from its top left
	std::optional<Link> linkAt(int index, double x, double y) const;
	// Windows printer HDC; original colors, print-visible annotations.
	bool printPage(int index, void* device, int x, int y, int width,
	               int height) const;
	// draws a page into a width x height bitmap. rotation is extra quarter
	// turns clockwise (0-3) on top of the page's own /Rotate. nullopt if the
	// page won't load or the size is zero or too big to be sensible.
	std::optional<Bitmap> render(int index, int width, int height,
	                             int rotation = 0) const;

  private:
	friend OpenResult openDocument(std::vector<std::uint8_t> data,
	                               const std::string& password);
	Document(std::vector<std::uint8_t> data, fpdf_document_t__* handle);

	// PDFium reads straight from this, so it's declared first and goes last
	std::vector<std::uint8_t> m_data;
	fpdf_document_t__* m_handle = nullptr;
	bool m_modified = false;
	// Nonowning handles stay on the engine thread for lazy target resolution.
	mutable std::vector<void*> m_outlineBookmarks;
};
} // namespace pdf
