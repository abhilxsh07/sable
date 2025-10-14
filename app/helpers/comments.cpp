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

CommentScan::CommentScan(int pages) {
	reset(pages);
}
void CommentScan::reset(int pages) {
	m_rows.clear();
	m_bytes = 0;
	m_pages = (std::max)(0, pages);
	m_next = 0;
	m_failed = m_limited = m_stopped = false;
}

bool CommentScan::append(pdf::Annotations snapshot) {
	if (done() || failed() || snapshot.page != m_next) {
		return false;
	}
	m_limited = m_limited || snapshot.limited;
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
		std::size_t remaining = BYTE_LIMIT - m_bytes;
		for (const auto* field :
		     {&entry.subtype, &entry.author, &entry.contents}) {
			if (field->size() > remaining) {
				m_stopped = true;
				break;
			}
			remaining -= field->size();
		}
		if (m_stopped) {
			break;
		}
		m_rows.push_back({snapshot.page, entry.index, std::move(entry.subtype),
		                  std::move(entry.author), std::move(entry.contents),
		                  entry.position});
		m_bytes = BYTE_LIMIT - remaining;
		if (m_rows.size() == ROW_LIMIT || m_bytes == BYTE_LIMIT) {
			m_stopped = true;
			break;
		}
	}
	m_limited = m_limited || m_stopped;
	++m_next;
	return true;
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
	return m_next;
}
int CommentScan::pageCount() const {
	return m_pages;
}
bool CommentScan::done() const {
	return m_stopped || m_next >= m_pages;
}
bool CommentScan::failed() const {
	return m_failed;
}
bool CommentScan::limited() const {
	return m_limited;
}
const std::vector<CommentRow>& CommentScan::rows() const {
	return m_rows;
}
} // namespace helpers
