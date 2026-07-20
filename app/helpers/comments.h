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
#include "../pdf/annotations.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helpers {
std::string_view commentLabel(std::string_view subtype);
// i use PDF points with an explicit decimal point.
std::string textBoxDimension(float value);
std::optional<float> parseTextBoxDimension(std::string_view text,
                                           std::optional<float> original = {});
struct CommentRow {
	int page = 0, index = 0;
	std::string subtype, author, text;
	std::optional<pdf::AnnotationPoint> position;
	pdf::AnnotationTarget target;
	std::optional<pdf::AnnotationColor> color;
};

// i keep at most 2000 rows and 4 MiB of their stored text.
class CommentScan {
  public:
	explicit CommentScan(int pages = 0);
	void reset(int pages);
	// a page whose rows can be swapped alone; false means reset instead.
	bool refreshPage(int page);
	bool append(pdf::Annotations snapshot);
	void fail();
	void retry();
	int nextPage() const;
	int pageCount() const;
	bool done() const;
	bool failed() const;
	bool limited() const;
	const std::vector<CommentRow>& rows() const;

  private:
	bool replace(pdf::Annotations snapshot);
	void stamp(std::uint64_t revision);
	std::vector<CommentRow> m_rows;
	std::optional<int> m_refresh;
	std::size_t m_bytes = 0;
	int m_pages = 0, m_next = 0;
	bool m_failed = false, m_limited = false, m_stopped = false;
};
} // namespace helpers
