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
#include <stdexcept>
#include "../helpers/logging.h"

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
	try {
		m_worker = std::thread([this] { backgroundLoop(); });
		m_thread = std::thread([this] { loop(); });
	} catch (...) {
		{
			std::lock_guard lock(m_mutex);
			m_workerStopping = true;
		}
		m_workerWake.notify_one();
		if (m_worker.joinable()) {
			m_worker.join();
		}
		g_engines.fetch_sub(1);
		throw;
	}
}

Engine::~Engine() {
	// joining itself would never return
	assert(!onThread() && "pdf::Engine destroyed from its own thread");
	{
		std::lock_guard lock(m_mutex);
		m_stopping = true;
	}
	m_wake.notify_one();
	m_thread.join();
	{
		std::lock_guard lock(m_mutex);
		m_workerStopping = true;
	}
	m_workerWake.notify_one();
	m_worker.join();
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

void Engine::background(std::function<void()> work,
                        std::function<void()> done) {
	assert(onThread() && "background jobs start on the PDFium thread");
	if (!onThread()) {
		throw std::logic_error("Background job started off the engine.");
	}
	{
		std::lock_guard lock(m_mutex);
		m_background.push_back({std::move(work), std::move(done)});
		++m_pending;
	}
	m_workerWake.notify_one();
}

void Engine::backgroundLoop() {
	for (;;) {
		std::list<Background> job;
		{
			std::unique_lock lock(m_mutex);
			m_workerWake.wait(lock, [this] {
				return m_workerStopping || !m_background.empty();
			});
			if (m_background.empty()) {
				return;
			}
			job.splice(job.end(), m_background, m_background.begin());
		}
		try {
			job.front().work();
		} catch (...) {
			diagnostics::write(diagnostics::Level::Error,
			                   "Couldn't finish background PDF file work.");
		}
		{
			std::lock_guard lock(m_mutex);
			// i move the allocated node so completion cannot fail to queue.
			m_completed.splice(m_completed.end(), job);
		}
		m_wake.notify_one();
	}
}

bool Engine::onThread() {
	return t_onPdfiumThread;
}

bool Engine::running() {
	return g_engines.load() > 0;
}

void Engine::loop() {
	t_onPdfiumThread = true;

	// version 2 is the plain one: no V8, no XFA, default font paths
	FPDF_LIBRARY_CONFIG config{};
	config.version = 2;
	FPDF_InitLibraryWithConfig(&config);
	diagnostics::write(diagnostics::Level::Debug, "PDFium engine initialized");

	for (;;) {
		std::function<void()> work;
		std::list<Background> completed;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [this] {
				return !m_queue.empty() || !m_completed.empty() ||
				       (m_stopping && m_pending == 0);
			});
			if (!m_queue.empty()) {
				work = std::move(m_queue.front());
				m_queue.pop_front();
			} else if (!m_completed.empty()) {
				completed.splice(completed.end(), m_completed,
				                 m_completed.begin());
				work = std::move(completed.front().done);
				--m_pending;
			} else {
				break;
			}
		}
		try {
			work();
		} catch (...) {
			diagnostics::write(
			    diagnostics::Level::Error,
			    "Unhandled exception in a PDFium engine callback");
		}
	}

	FPDF_DestroyLibrary();
	diagnostics::write(diagnostics::Level::Debug, "PDFium engine stopped");
	t_onPdfiumThread = false;
}
} // namespace pdf
