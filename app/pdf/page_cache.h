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

#include <compare>
#include <cstddef>
#include <list>
#include <map>

#include "document.h"

namespace pdf {
struct PageKey {
	int page;
	int width;
	int height;
	int rotation = 0;
	auto operator<=>(const PageKey&) const = default;
};

// One document's unfiltered rasters, used only by its UI-thread owner.
// Clear before switching or editing the document. The caller must discard
// stale render results before inserting them.
class PageCache {
  public:
	// budget covers retained pixel allocations, including spare capacity.
	// GPU copies, incoming renders and container overhead are separate.
	explicit PageCache(std::size_t budget);
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;

	// a hit becomes most recently used. The pointer lasts until that entry
	// is replaced or evicted, or the cache is cleared or destroyed.
	const Bitmap* find(PageKey key);
	// takes ownership. Invalid or over-budget bitmaps leave the cache alone.
	bool put(PageKey key, Bitmap bitmap);
	void clear();
	std::size_t usedBytes() const;
	std::size_t size() const;

  private:
	struct Entry {
		PageKey key;
		Bitmap bitmap;
	};
	std::size_t m_budget;
	std::size_t m_bytes = 0;
	std::list<Entry> m_recent;
	std::map<PageKey, std::list<Entry>::iterator> m_index;
};
} // namespace pdf
