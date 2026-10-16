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
#include "session.h"

#include <cstdint>
#include <mutex>
#include <utility>

#include "engine.h"
#include "printing.h"
#include "../helpers/logging.h"

namespace pdf {
struct Session::State {
	explicit State(Engine& engine) : m_engine(engine) {}
	bool closed() {
		std::lock_guard lock(m_mutex);
		return m_closed;
	}
	Engine& m_engine;
	std::mutex m_mutex;
	bool m_opened = false;
	bool m_ready = false;
	bool m_closed = false;
	bool m_rendering = false;
	bool m_printing = false;
	std::uint64_t m_generation = 0;
	// only the engine thread touches the document
	std::unique_ptr<Document> m_document;
};

Session::Session(Engine& engine)
    : m_state(new State(engine), [&engine](State* state) {
	      if (Engine::onThread()) {
		      delete state;
	      } else {
		      // the document can't die off the engine thread, so if this
		      // can't be posted the state leaks instead
		      try {
			      engine.post([state] { delete state; });
		      } catch (...) {
		      }
	      }
      }) {}

Session::~Session() {
	// if the reset can't be posted, the document still goes with the state
	try {
		close();
	} catch (...) {
	}
}

bool Session::open(const std::filesystem::path& path,
                   const std::string& password,
                   std::function<void(SessionInfo)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || state->m_opened || state->m_closed) {
			return false;
		}
		state->m_opened = true;
	}
	// posted without the lock: the job checks for close() itself, and a
	// callback that never gets queued isn't destroyed with the lock held
	try {
		state->m_engine.post([state, path, password,
		                      callback = std::move(callback)] {
			SessionInfo info;
			OpenResult opened;
			try {
				if (!state->closed()) {
					opened = openFile(path, password);
					info.error = opened.error;
					if (opened.document) {
						const int count = opened.document->pageCount();
						for (int page = 0; page < count && !state->closed();
						     ++page) {
							const auto size = opened.document->pageSize(page);
							if (!size) {
								info.error = Error::Format;
								break;
							}
							info.pages.push_back(*size);
							info.labels.push_back(
							    opened.document->pageLabel(page));
						}
					}
				}
			} catch (...) {
				info.error = Error::Unknown;
			}
			{
				std::lock_guard finish(state->m_mutex);
				info.canceled = state->m_closed;
				if (info.canceled || info.error != Error::None) {
					info.pages.clear();
					info.labels.clear();
				} else {
					state->m_document = std::move(opened.document);
					state->m_ready = state->m_document != nullptr;
				}
			}
			// failed/canceled documents die here, before the callback
			opened.document.reset();
			if (!info.canceled) {
				diagnostics::write(
				    info.error == Error::None ? diagnostics::Level::Debug
				                              : diagnostics::Level::Warning,
				    info.error == Error::None ? "PDF session opened"
				                              : errorMessage(info.error));
			}
			callback(std::move(info));
		});
	} catch (...) {
		std::lock_guard lock(state->m_mutex);
		state->m_opened = false;
		throw;
	}
	return true;
}

bool Session::render(PageKey key, std::function<void(RenderResult)> callback) {
	const auto state = m_state;
	std::uint64_t generation = 0;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed ||
		    state->m_rendering) {
			return false;
		}
		state->m_rendering = true;
		generation = state->m_generation;
	}
	// the same as open(): a close or cancel from here on reaches the job
	try {
		state->m_engine.post(
		    [state, key, generation, callback = std::move(callback)] {
			    RenderResult result{key, std::nullopt};
			    {
				    std::lock_guard start(state->m_mutex);
				    result.canceled =
				        state->m_closed || generation != state->m_generation;
			    }
			    try {
				    if (!result.canceled) {
					    result.bitmap = state->m_document->render(
					        key.page, key.width, key.height, key.rotation);
				    }
			    } catch (...) {
				    result.bitmap.reset();
			    }
			    {
				    std::lock_guard finish(state->m_mutex);
				    result.canceled =
				        state->m_closed || generation != state->m_generation;
				    if (result.canceled) {
					    result.bitmap.reset();
				    }
				    state->m_rendering = false;
			    }
			    callback(std::move(result));
		    });
	} catch (...) {
		std::lock_guard lock(state->m_mutex);
		state->m_rendering = false;
		throw;
	}
	return true;
}

bool Session::linkAt(int page, double x, double y,
                     std::function<void(LinkResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
	}
	state->m_engine.post([state, page, x, y, callback = std::move(callback)] {
		LinkResult result;
		try {
			if (!state->closed()) {
				result.link = state->m_document->linkAt(page, x, y);
			}
		} catch (...) {
			result.link.reset();
		}
		result.canceled = state->closed();
		if (result.canceled) {
			result.link.reset();
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::print(PrintJob job, std::function<void(PrintResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !job.device || job.pages.empty() || !state->m_ready ||
		    state->m_closed || state->m_printing) {
			return false;
		}
		state->m_printing = true;
	}
	try {
		state->m_engine.post([state, job = std::move(job),
		                      callback = std::move(callback)] {
			PrintResult result = PrintResult::Failed;
			try {
				if (state->closed()) {
					result = PrintResult::Canceled;
				} else {
					result = printDocument(*state->m_document, job,
					                       [state] { return state->closed(); });
				}
			} catch (...) {
			}
			{
				std::lock_guard lock(state->m_mutex);
				state->m_printing = false;
				if (state->m_closed) {
					result = PrintResult::Canceled;
				}
			}
			callback(result);
		});
	} catch (...) {
		std::lock_guard lock(state->m_mutex);
		state->m_printing = false;
		throw;
	}
	return true;
}

void Session::cancelRender() {
	std::lock_guard lock(m_state->m_mutex);
	++m_state->m_generation;
}

void Session::close() {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (state->m_closed) {
			return;
		}
		state->m_closed = true;
		state->m_ready = false;
		++state->m_generation;
	}
	state->m_engine.post([state] { state->m_document.reset(); });
}
} // namespace pdf
