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
#include "page_cache.h"

#include <utility>

namespace pdf {
PageCache::PageCache(std::size_t budget) : m_budget(budget) {}

const Bitmap* PageCache::find(PageKey key) {
	const auto found = m_index.find(key);
	if (found == m_index.end()) {
		return nullptr;
	}
	m_recent.splice(m_recent.begin(), m_recent, found->second);
	return &found->second->bitmap;
}

bool PageCache::put(PageKey key, Bitmap bitmap) {
	if (key.page < 0 || key.rotation < 0 || key.rotation > 3 ||
	    key.width <= 0 || key.height <= 0 || bitmap.width != key.width ||
	    bitmap.height != key.height || bitmap.stride <= 0 ||
	    bitmap.width > bitmap.stride / 4) {
		return false;
	}
	const auto stride = static_cast<std::size_t>(bitmap.stride);
	// division keeps malformed dimensions from overflowing a byte count
	if (bitmap.pixels.size() / stride !=
	        static_cast<std::size_t>(bitmap.height) ||
	    bitmap.pixels.size() % stride != 0) {
		return false;
	}
	const auto bytes = bitmap.pixels.capacity();
	if (bytes > m_budget) {
		return false;
	}

	// allocate before evicting, so a failed allocation keeps the old entries
	m_recent.push_front({key, std::move(bitmap)});
	try {
		const auto [slot, inserted] =
		    m_index.try_emplace(key, m_recent.begin());
		if (!inserted) {
			m_bytes -= slot->second->bitmap.pixels.capacity();
			m_recent.erase(slot->second);
			slot->second = m_recent.begin();
		}
	} catch (...) {
		m_recent.pop_front();
		throw;
	}
	// subtract first: adding an incoming allocation could overflow size_t
	while (m_bytes > m_budget - bytes) {
		const auto& oldest = m_recent.back();
		m_bytes -= oldest.bitmap.pixels.capacity();
		m_index.erase(oldest.key);
		m_recent.pop_back();
	}
	m_bytes += bytes;
	return true;
}

void PageCache::clear() {
	m_index.clear();
	m_recent.clear();
	m_bytes = 0;
}

std::size_t PageCache::usedBytes() const {
	return m_bytes;
}

std::size_t PageCache::size() const {
	return m_recent.size();
}
} // namespace pdf
