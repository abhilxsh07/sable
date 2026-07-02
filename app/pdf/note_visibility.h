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
#include <fpdf_annot.h>
#include <fpdfview.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace pdf::detail {
// i mask note icons only while rendering or serializing the output.
class NoteVisibility {
  public:
	bool collect(FPDF_PAGE page) {
		const int count = FPDFPage_GetAnnotCount(page);
		if (count < 0) {
			return false;
		}
		for (int index = 0; index < count; ++index) {
			Note note{Annotation(FPDFPage_GetAnnot(page, index)), 0};
			if (!note.annotation) {
				return false;
			}
			if (FPDFAnnot_GetSubtype(note.annotation.get()) !=
			    FPDF_ANNOT_TEXT) {
				continue;
			}
			note.flags = FPDFAnnot_GetFlags(note.annotation.get());
			if (hidden(note.flags) != note.flags) {
				if (m_notes.size() == 131072) {
					return false;
				}
				m_notes.push_back(std::move(note));
			}
		}
		return true;
	}
	bool empty() const {
		return m_notes.empty();
	}
	bool run(const std::function<bool()>& output) {
		for (const auto& note : m_notes) {
			++m_applied;
			if (!FPDFAnnot_SetFlags(note.annotation.get(),
			                        hidden(note.flags))) {
				restore();
				return false;
			}
		}
		bool result = false;
		try {
			result = output();
		} catch (...) {
			restore();
			throw;
		}
		restore();
		return result;
	}

  private:
	struct CloseAnnotation {
		void operator()(fpdf_annotation_t__* note) const {
			FPDFPage_CloseAnnot(note);
		}
	};
	using Annotation = std::unique_ptr<fpdf_annotation_t__, CloseAnnotation>;
	struct Note {
		Annotation annotation;
		int flags;
	};
	static int hidden(int flags) {
		return (flags | FPDF_ANNOT_FLAG_NOVIEW) & ~FPDF_ANNOT_FLAG_PRINT;
	}
	void restore() {
		bool restored = true;
		while (m_applied) {
			const auto& note = m_notes[--m_applied];
			restored =
			    (FPDFAnnot_SetFlags(note.annotation.get(), note.flags) != 0) &&
			    restored;
		}
		if (!restored) {
			throw std::runtime_error("Couldn't restore note visibility.");
		}
	}
	std::vector<Note> m_notes;
	std::size_t m_applied = 0;
};
} // namespace pdf::detail
