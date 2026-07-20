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
#include "comments.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <string_view>
#include <utility>

namespace helpers {
namespace {
constexpr std::size_t ROW_LIMIT = 2000, BYTE_LIMIT = 4 * 1024 * 1024;

bool isComment(const pdf::Annotation& entry) {
	const auto& type = entry.subtype;
	if (type == "Link" || type == "Widget" || type == "XFAWidget" ||
	    type == "Popup") {
		return false;
	}
	constexpr std::string_view types[] = {
	    "Text",      "FreeText", "Line",           "Square",
	    "Circle",    "Polygon",  "PolyLine",       "Highlight",
	    "Underline", "Squiggly", "StrikeOut",      "Stamp",
	    "Caret",     "Ink",      "FileAttachment", "Sound"};
	return !entry.contents.empty() ||
	       std::find(std::begin(types), std::end(types),
	                 std::string_view(type)) != std::end(types);
}

void sanitize(std::string& value, bool multiline) {
	std::size_t used = 0;
	for (std::size_t i = 0; i < value.size(); ++i) {
		char byte = value[i];
		const auto code = static_cast<unsigned char>(byte);
		if (multiline && byte == '\r') {
			byte = '\n';
			if (i + 1 < value.size() && value[i + 1] == '\n') {
				++i;
			}
		} else if ((code < 32 && !(multiline && byte == '\n')) || code == 127) {
			byte = ' ';
		}
		value[used++] = byte;
	}
	value.resize(used);
}
} // namespace

std::string textBoxDimension(float value) {
	if (!std::isfinite(value) || value <= 0) {
		return {};
	}
	char text[64];
	auto result = std::to_chars(std::begin(text), std::end(text), value,
	                            std::chars_format::fixed, 2);
	if (result.ec != std::errc{}) {
		return {};
	}
	std::string shown(text, result.ptr);
	while (shown.size() > 1 && shown.back() == '0') {
		shown.pop_back();
	}
	if (shown.back() == '.') {
		shown.pop_back();
	}
	// i keep tiny positive sizes readable instead of showing zero points.
	if (shown == "0") {
		result = std::to_chars(std::begin(text), std::end(text), value);
		shown = result.ec == std::errc{} ? std::string(text, result.ptr) : "";
	}
	return shown;
}

std::optional<float> parseTextBoxDimension(std::string_view text,
                                           std::optional<float> original) {
	// rounded display text must not change the original coordinates.
	if (original && text == textBoxDimension(*original) &&
	    std::isfinite(*original) && *original > 0) {
		return original;
	}
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
		text.remove_prefix(1);
	}
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
		text.remove_suffix(1);
	}
	if (!text.empty() && text.front() == '+') {
		text.remove_prefix(1);
	}
	if (text.empty()) {
		return {};
	}
	float value = 0;
	const auto [end, error] =
	    std::from_chars(text.data(), text.data() + text.size(), value);
	return error == std::errc{} && end == text.data() + text.size() &&
	               std::isfinite(value) && value > 0
	           ? std::optional{value}
	           : std::nullopt;
}

std::string_view commentLabel(std::string_view subtype) {
	if (subtype == "Text") {
		return "Note";
	}
	if (subtype == "StrikeOut") {
		return "Strikethrough";
	}
	if (subtype == "FreeText") {
		return "Text box";
	}
	if (subtype == "Square") {
		return "Rectangle";
	}
	if (subtype == "Circle") {
		return "Ellipse";
	}
	if (subtype == "Ink") {
		return "Drawing";
	}
	if (subtype == "PolyLine") {
		return "Polyline";
	}
	if (subtype == "FileAttachment") {
		return "Attachment";
	}
	return subtype;
}

CommentScan::CommentScan(int pages) {
	reset(pages);
}
void CommentScan::reset(int pages) {
	m_rows.clear();
	m_bytes = 0;
	m_pages = (std::max)(0, pages);
	m_next = 0;
	m_failed = m_limited = m_stopped = false;
	m_refresh.reset();
}

namespace {
std::size_t rowBytes(const CommentRow& row) {
	return row.subtype.size() + row.author.size() + row.text.size() +
	       row.target.subtype.size();
}
// adds a page's comment rows; false once a row or byte limit stops it.
bool addRows(pdf::Annotations& snapshot, std::vector<CommentRow>& rows,
             std::size_t& bytes) {
	for (auto& entry : snapshot.entries) {
		if (!isComment(entry)) {
			continue;
		}
		if (entry.subtype.empty()) {
			entry.subtype = "Annotation";
		}
		sanitize(entry.subtype, false);
		sanitize(entry.author, false);
		sanitize(entry.contents, true);
		std::size_t remaining = BYTE_LIMIT - bytes;
		for (const auto* field : {&entry.subtype, &entry.author,
		                          &entry.contents, &entry.target.subtype}) {
			if (field->size() > remaining) {
				return false;
			}
			remaining -= field->size();
		}
		rows.push_back({snapshot.page, entry.index, std::move(entry.subtype),
		                std::move(entry.author), std::move(entry.contents),
		                entry.position, std::move(entry.target), entry.color});
		bytes = BYTE_LIMIT - remaining;
		if (rows.size() == ROW_LIMIT || bytes == BYTE_LIMIT) {
			return false;
		}
	}
	return true;
}
} // namespace

bool CommentScan::refreshPage(int page) {
	if (m_failed || m_limited || m_refresh || page < 0 || page >= m_pages) {
		return false;
	}
	// a page the scan hasn't reached yet arrives with the new revision.
	if (page < m_next) {
		m_refresh = page;
	}
	return true;
}

bool CommentScan::append(pdf::Annotations snapshot) {
	if (m_refresh) {
		return replace(std::move(snapshot));
	}
	if (done() || failed() || snapshot.page != m_next) {
		return false;
	}
	m_limited = m_limited || snapshot.limited;
	m_stopped = !addRows(snapshot, m_rows, m_bytes);
	m_limited = m_limited || m_stopped;
	stamp(snapshot.revision);
	++m_next;
	return true;
}

bool CommentScan::replace(pdf::Annotations snapshot) {
	if (failed() || snapshot.page != *m_refresh) {
		return false;
	}
	const int page = *m_refresh;
	std::vector<CommentRow> rows;
	std::size_t bytes = 0;
	auto row = m_rows.begin();
	for (; row != m_rows.end() && row->page < page; ++row) {
		bytes += rowBytes(*row);
		rows.push_back(*row);
	}
	bool fits = !snapshot.limited && addRows(snapshot, rows, bytes);
	for (; fits && row != m_rows.end(); ++row) {
		if (row->page == page) {
			continue;
		}
		if (rows.size() + 1 >= ROW_LIMIT ||
		    rowBytes(*row) >= BYTE_LIMIT - bytes) {
			fits = false;
			break;
		}
		bytes += rowBytes(*row);
		rows.push_back(*row);
	}
	if (!fits) {
		// the swap would reach a limit, so i list every page again.
		reset(m_pages);
		return true;
	}
	m_rows = std::move(rows);
	m_bytes = bytes;
	m_refresh.reset();
	stamp(snapshot.revision);
	return true;
}

// a page-local edit leaves the other pages' rows valid at the new revision.
void CommentScan::stamp(std::uint64_t revision) {
	if (!revision) {
		return;
	}
	for (auto& row : m_rows) {
		row.target.revision = revision;
	}
}

void CommentScan::fail() {
	if (!done()) {
		m_failed = true;
	}
}
void CommentScan::retry() {
	m_failed = false;
}
int CommentScan::nextPage() const {
	return m_refresh.value_or(m_next);
}
int CommentScan::pageCount() const {
	return m_pages;
}
bool CommentScan::done() const {
	return !m_refresh && (m_stopped || m_next >= m_pages);
}
bool CommentScan::failed() const {
	return m_failed;
}
bool CommentScan::limited() const {
	return m_limited;
}
const std::vector<CommentRow>& CommentScan::rows() const {
	// i hide the old rows until the edited page's rows replace them.
	static const std::vector<CommentRow> none;
	return m_refresh ? none : m_rows;
}
} // namespace helpers
