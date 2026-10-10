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
#include "engine.h"

#include <atomic>
#include <cassert>

#include <fpdfview.h>

namespace pdf {
namespace {
thread_local bool t_onPdfiumThread = false;

// PDFium is set up once per process, a second engine would tear it down under
// the first one's feet
std::atomic<int> g_engines{0};
} // namespace

Engine::Engine() {
	[[maybe_unused]] const int others = g_engines.fetch_add(1);
	assert(others == 0 && "only one pdf::Engine at a time");
	m_thread = std::thread([this] { loop(); });
}

Engine::~Engine() {
	{
		std::lock_guard lock(m_mutex);
		m_stopping = true;
	}
	m_wake.notify_one();
	m_thread.join();
	g_engines.fetch_sub(1);
}

void Engine::post(std::function<void()> work) {
	{
		std::lock_guard lock(m_mutex);
		// too late, the thread is on its way out
		if (m_stopping) {
			return;
		}
		m_queue.push_back(std::move(work));
	}
	m_wake.notify_one();
}

bool Engine::onThread() {
	return t_onPdfiumThread;
}

void Engine::loop() {
	t_onPdfiumThread = true;

	// version 2 is the plain one: no V8, no XFA, default font paths
	FPDF_LIBRARY_CONFIG config{};
	config.version = 2;
	FPDF_InitLibraryWithConfig(&config);

	for (;;) {
		std::function<void()> work;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock,
			            [this] { return m_stopping || !m_queue.empty(); });
			// stopping and nothing left, otherwise finish the queue first
			if (m_queue.empty()) {
				break;
			}
			work = std::move(m_queue.front());
			m_queue.pop_front();
		}
		try {
			work();
		} catch (...) {
			// @todo: log it once logging is in. run() hands its exceptions
			// back through the future, so this is only post() work.
		}
	}

	FPDF_DestroyLibrary();
	t_onPdfiumThread = false;
}
} // namespace pdf
