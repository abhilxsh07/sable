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

#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <list>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace pdf {
// "None of the PDFium APIs are thread-safe. They expect to be called from a
// single thread." (fpdfview.h). So PDFium gets a thread of its own: the
// library is set up when it starts, torn down when it stops, and every
// FPDF_* call in between happens on it. App owns the one Engine.
class Engine {
  public:
	Engine();
	// runs whatever is still queued, shuts PDFium down and joins the thread
	~Engine();

	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;

	// queue work for the PDFium thread, it runs in the order it was posted.
	// False means shutdown has started and the work was rejected.
	bool post(std::function<void()> work);
	// stop accepting jobs and drain the queue. the destructor joins the
	// threads.
	void beginShutdown();
	// called on the engine: run I/O on a worker, then finish back here.
	// shutdown drains both parts; the worker must never call PDFium.
	void background(std::function<void()> work, std::function<void()> done);

	// queue work and get its result, or its exception, back as a future.
	// Don't wait on the future from the UI thread, or from work already
	// running on the PDFium thread (that one never finishes). And don't
	// hand a Document back through it, it has to die on the PDFium thread.
	template <typename F>
	auto run(F&& work) -> std::future<std::invoke_result_t<std::decay_t<F>&>> {
		using Work = std::decay_t<F>;
		using Result = std::invoke_result_t<Work&>;
		// std::function wants something copyable and the work might not be
		auto fn = std::make_shared<Work>(std::forward<F>(work));
		auto promise = std::make_shared<std::promise<Result>>();
		auto result = promise->get_future();
		// a rejected post leaves the future with a broken promise.
		(void)post([fn, promise] {
			try {
				if constexpr (std::is_void_v<Result>) {
					(*fn)();
					promise->set_value();
				} else {
					promise->set_value((*fn)());
				}
			} catch (...) {
				promise->set_exception(std::current_exception());
			}
		});
		return result;
	}

	// true on the PDFium thread, false everywhere else
	static bool onThread();
	// true while an Engine exists
	static bool running();

  private:
	void loop();
	void backgroundLoop();
	struct Background {
		std::function<void()> work, done;
	};

	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::deque<std::function<void()>> m_queue;
	bool m_stopping = false, m_workerStopping = false;
	std::condition_variable m_workerWake;
	std::list<Background> m_background, m_completed;
	std::size_t m_pending = 0;
	// last, so everything the thread uses exists before it starts
	std::thread m_worker, m_thread;
};
} // namespace pdf
