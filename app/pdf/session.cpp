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
#include <limits>
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
	bool m_saving = false;
	std::uint64_t m_generation = 0;
	std::uint64_t m_findGeneration = 0;
	std::uint64_t m_outlineGeneration = 0;
	std::uint64_t m_annotationGeneration = 0;
	struct FindJob;
	// only the engine thread touches the document
	std::unique_ptr<Document> m_document;
};

struct Session::State::FindJob : std::enable_shared_from_this<FindJob> {
	std::shared_ptr<State> state;
	std::uint64_t generation = 0;
	std::u16string query;
	FindOptions options;
	std::function<void(FindUpdate)> callback;
	FindUpdate update;
	std::size_t total = 0;
	bool canceled() {
		std::lock_guard lock(state->m_mutex);
		return state->m_closed || generation != state->m_findGeneration;
	}
	void run() {
		try {
			if (!canceled()) {
				update.pageCount = state->m_document->pageCount();
				if (query.empty()) {
					update.done = true;
				} else {
					const auto page = state->m_document->findPage(
					    update.pagesScanned, query, options, 50'000 - total,
					    [this] { return canceled(); });
					if (!page) {
						update.failed = true;
					} else {
						total += page->matches.size();
						update.matches.insert(update.matches.end(),
						                      page->matches.begin(),
						                      page->matches.end());
						update.limited = page->limited || total == 50'000;
					}
					++update.pagesScanned;
				}
			}
		} catch (...) {
			update.failed = true;
		}
		update.canceled = canceled();
		update.done = update.done || update.canceled || update.failed ||
		              update.limited || update.pagesScanned == update.pageCount;
		if (update.canceled || update.failed) {
			update.matches.clear();
		}
		if (update.done || update.pagesScanned % 16 == 0) {
			try {
				callback(update);
			} catch (...) {
				return;
			}
			update.matches.clear();
		}
		if (!update.done) {
			const auto self = shared_from_this();
			state->m_engine.post([self] { self->run(); });
		}
	}
};
bool Session::search(std::u16string query, FindOptions options,
                     std::function<void(FindUpdate)> callback) {
	const auto state = m_state;
	auto job = std::make_shared<State::FindJob>();
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
		job->generation = ++state->m_findGeneration;
	}
	job->state = state;
	job->query = std::move(query);
	job->options = options;
	job->callback = std::move(callback);
	state->m_engine.post([job] { job->run(); });
	return true;
}
void Session::cancelSearch() {
	std::lock_guard lock(m_state->m_mutex);
	++m_state->m_findGeneration;
}

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
						info.canCopy = opened.document->canCopy();
						info.canAnnotate = opened.document->canAnnotate();
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
					info.canAnnotate = false;
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

bool Session::outlineLink(std::size_t index,
                          std::function<void(LinkResult)> callback) {
	const auto state = m_state;
	std::uint64_t generation = 0;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
		generation = ++state->m_outlineGeneration;
	}
	state->m_engine.post([state, index, generation,
	                      callback = std::move(callback)] {
		LinkResult result;
		const auto canceled = [state, generation] {
			std::lock_guard lock(state->m_mutex);
			return state->m_closed || generation != state->m_outlineGeneration;
		};
		try {
			if (!canceled()) {
				result.link = state->m_document->outlineLink(index);
			}
		} catch (...) {
		}
		result.canceled = canceled();
		if (result.canceled) {
			result.link.reset();
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::annotations(int page,
                          std::function<void(AnnotationsResult)> callback) {
	const auto state = m_state;
	std::uint64_t generation = 0;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
		generation = ++state->m_annotationGeneration;
	}
	state->m_engine.post(
	    [state, page, generation, callback = std::move(callback)] {
		    AnnotationsResult result;
		    result.page = page;
		    const auto canceled = [state, generation] {
			    std::lock_guard lock(state->m_mutex);
			    return state->m_closed ||
			           generation != state->m_annotationGeneration;
		    };
		    try {
			    if (!canceled()) {
				    result.annotations =
				        state->m_document->annotations(page, canceled);
			    }
		    } catch (...) {
			    diagnostics::write(diagnostics::Level::Error,
			                       "Couldn't read PDF page annotations.");
		    }
		    result.canceled = canceled();
		    if (result.canceled) {
			    result.annotations.reset();
		    }
		    callback(std::move(result));
	    });
	return true;
}

void Session::cancelAnnotations() {
	std::lock_guard lock(m_state->m_mutex);
	++m_state->m_annotationGeneration;
}

bool Session::addTextNote(int page, float x, float y, std::u16string contents,
                          std::u16string author,
                          std::function<void(NoteResult)> callback) {
	return writeTextNote(page, std::pair{x, y}, std::move(contents),
	                     std::move(author), std::move(callback));
}

bool Session::addCenteredTextNote(int page, std::u16string contents,
                                  std::u16string author,
                                  std::function<void(NoteResult)> callback) {
	return writeTextNote(page, {}, std::move(contents), std::move(author),
	                     std::move(callback));
}

bool Session::addTextNoteAt(int page, double x, double y, int rotation,
                            std::u16string contents, std::u16string author,
                            std::function<void(NoteResult)> callback) {
	return writeTextNote(page, std::pair{x, y}, std::move(contents),
	                     std::move(author), std::move(callback), rotation);
}

bool Session::writeTextNote(int page,
                            std::optional<std::pair<double, double>> point,
                            std::u16string contents, std::u16string author,
                            std::function<void(NoteResult)> callback,
                            std::optional<int> rotation) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed ||
		    contents.size() > 32767 || author.size() > 32767) {
			return false;
		}
	}
	state->m_engine.post([state, page, point, rotation,
	                      contents = std::move(contents),
	                      author = std::move(author),
	                      callback = std::move(callback)] {
		NoteResult result;
		result.page = page;
		// this is the boundary: close can discard a queued write, but not
		// turn an edit that has already started into a canceled reply.
		result.canceled = state->closed();
		try {
			if (!result.canceled) {
				if (!point) {
					result.index = state->m_document->addCenteredTextNote(
					    page, contents, author);
				} else if (rotation) {
					result.index = state->m_document->addTextNoteAt(
					    page, point->first, point->second, *rotation, contents,
					    author);
				} else {
					result.index = state->m_document->addTextNote(
					    page, static_cast<float>(point->first),
					    static_cast<float>(point->second), contents, author);
				}
				result.changed = result.index.has_value();
			}
		} catch (...) {
			// a failed rollback may have left an incomplete annotation.
			result.changed = state->m_document && state->m_document->modified();
			diagnostics::write(diagnostics::Level::Error,
			                   "Couldn't add a PDF text note.");
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::highlightText(TextSelection selection,
                            std::function<void(HighlightResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
	}
	state->m_engine.post([state, selection, callback = std::move(callback)] {
		HighlightResult result;
		result.canceled = state->closed();
		try {
			if (!result.canceled) {
				if (const auto added =
				        state->m_document->highlightText(selection)) {
					result.count = *added;
					result.changed = *added > 0;
				}
			}
		} catch (...) {
			result.changed = state->m_document && state->m_document->modified();
			diagnostics::write(diagnostics::Level::Error,
			                   "Couldn't highlight PDF text.");
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::textNote(int page, int index,
                       std::function<void(TextNoteResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
	}
	state->m_engine.post([state, page, index, callback = std::move(callback)] {
		TextNoteResult result;
		result.page = page;
		result.index = index;
		try {
			if (!state->closed()) {
				result.contents = state->m_document->textNote(page, index);
			}
		} catch (...) {
			diagnostics::write(diagnostics::Level::Error,
			                   "Couldn't read a PDF text note.");
		}
		result.canceled = state->closed();
		if (result.canceled) {
			result.contents.reset();
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::updateTextNote(int page, int index, std::u16string expected,
                             std::u16string contents,
                             std::function<void(NoteResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed ||
		    expected.size() > 32767 || contents.size() > 32767) {
			return false;
		}
	}
	state->m_engine.post([state, page, index, expected = std::move(expected),
	                      contents = std::move(contents),
	                      callback = std::move(callback)] {
		NoteResult result;
		result.page = page;
		result.canceled = state->closed();
		try {
			if (!result.canceled) {
				if (const auto changed = state->m_document->updateTextNote(
				        page, index, expected, contents)) {
					result.index = index;
					result.changed = *changed;
				}
			}
		} catch (...) {
			result.changed = state->m_document && state->m_document->modified();
			diagnostics::write(diagnostics::Level::Error,
			                   "Couldn't update a PDF text note.");
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::saveEdits(const std::filesystem::path& destination,
                        const std::filesystem::path& source, std::size_t limit,
                        std::function<bool()> canceled,
                        std::function<void(SaveResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed ||
		    state->m_saving) {
			return false;
		}
		state->m_saving = true;
	}
	try {
		state->m_engine.post([state, destination, source, limit,
		                      canceled = std::move(canceled),
		                      callback = std::move(callback)] {
			SaveResult result = SaveResult::Failed;
			try {
				const auto stopped = [&] {
					return state->closed() || (canceled && canceled());
				};
				result = stopped() ? SaveResult::Canceled
				                   : state->m_document->saveEdits(
				                         destination, source, limit, stopped);
			} catch (...) {
				diagnostics::write(diagnostics::Level::Error,
				                   "Couldn't save PDF edits.");
			}
			{
				std::lock_guard lock(state->m_mutex);
				state->m_saving = false;
			}
			callback(result);
		});
	} catch (...) {
		std::lock_guard lock(state->m_mutex);
		state->m_saving = false;
		throw;
	}
	return true;
}

bool Session::attachments(std::function<void(AttachmentsResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
	}
	state->m_engine.post([state, callback = std::move(callback)] {
		AttachmentsResult result;
		try {
			if (!state->closed()) {
				result.attachments = state->m_document->attachments(
				    [state] { return state->closed(); });
			}
		} catch (...) {
			diagnostics::write(diagnostics::Level::Error,
			                   "Couldn't read PDF attachments.");
		}
		result.canceled = state->closed();
		if (result.canceled) {
			result.attachments.reset();
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::attachment(int index,
                         std::function<void(AttachmentResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
	}
	state->m_engine.post([state, index, callback = std::move(callback)] {
		AttachmentResult result;
		try {
			if (!state->closed()) {
				result.bytes = state->m_document->attachment(
				    index, [state] { return state->closed(); });
			}
		} catch (...) {
			diagnostics::write(diagnostics::Level::Error,
			                   "Couldn't extract a PDF attachment.");
		}
		result.canceled = state->closed();
		if (result.canceled) {
			result.bytes.reset();
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::outline(std::function<void(OutlineResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
	}
	state->m_engine.post([state, callback = std::move(callback)] {
		OutlineResult result;
		try {
			if (!state->closed()) {
				result.outline = state->m_document->outline(
				    [state] { return state->closed(); });
			}
		} catch (...) {
		}
		result.canceled = state->closed();
		if (result.canceled) {
			result.outline.reset();
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::textPage(int page, std::function<void(TextResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
	}
	state->m_engine.post([state, page, callback = std::move(callback)] {
		TextResult result{page};
		try {
			if (!state->closed()) {
				result.text = state->m_document->textPage(page);
			}
		} catch (...) {
		}
		result.canceled = state->closed();
		if (result.canceled) {
			result.text.reset();
		}
		callback(std::move(result));
	});
	return true;
}

bool Session::copyText(TextSelection selection,
                       std::function<void(CopyResult)> callback) {
	const auto state = m_state;
	{
		std::lock_guard lock(state->m_mutex);
		if (!callback || !state->m_ready || state->m_closed) {
			return false;
		}
	}
	state->m_engine.post([state, selection, callback = std::move(callback)] {
		CopyResult result;
		try {
			const auto [first, last] = ordered(selection);
			if (!state->closed()) {
				if (first.page < 0 || first.index < 0 || last.index < 0 ||
				    last.page >= state->m_document->pageCount()) {
					result.failed = true;
				} else {
					for (int page = first.page;
					     page <= last.page && !state->closed(); ++page) {
						const int start = page == first.page ? first.index : 0;
						const int end =
						    page == last.page &&
						            last.index < std::numeric_limits<int>::max()
						        ? last.index + 1
						        : std::numeric_limits<int>::max();
						const auto text =
						    state->m_document->textRange(page, start, end);
						if (!text || result.text.size() + text->size() + 2 >
						                 16 * 1024 * 1024) {
							result.failed = true;
							break;
						}
						if (!result.text.empty() && !text->empty() &&
						    result.text.back() != '\n' &&
						    result.text.back() != '\r') {
							result.text += "\r\n";
						}
						result.text += *text;
					}
				}
			}
		} catch (...) {
			result.failed = true;
		}
		result.canceled = state->closed();
		if (result.failed || result.canceled) {
			result.text.clear();
		}
		callback(std::move(result));
	});
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
		++state->m_findGeneration;
	}
	state->m_engine.post([state] { state->m_document.reset(); });
}
} // namespace pdf
