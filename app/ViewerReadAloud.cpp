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
#include "pch.h"
#include "ViewerPage.xaml.h"
#include "pdf/session.h"
#include "helpers/logging.h"
#include "helpers/read_aloud.h"
#include <algorithm>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace Speech = Windows::Media::SpeechSynthesis;

// Read Aloud reads the document a page at a time: PDFium gives the page's
// text on its own thread, Windows' speech synthesizer turns it into audio,
// and a MediaPlayer plays it, so pause and resume pick up mid-sentence.
// When a page ends the next one starts, and the view follows it.
// see:
// https://learn.microsoft.com/en-us/uwp/api/windows.media.speechsynthesis.speechsynthesizer
namespace winrt::Sable::implementation {
namespace {
// past this a page is cut short rather than handed to the voice whole
constexpr std::size_t MAX_SPOKEN = 100000;
} // namespace

bool ViewerPage::readingAloud() const {
	return m_readAloudPage.has_value();
}

void ViewerPage::setupReadAloud() {
	if (m_readAloudSetup) {
		return;
	}
	m_readAloudSetup = true;
	const auto weak = get_weak();
	ReadAloudPrevious().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->stepReadAloud(-1);
		}
	});
	ReadAloudNext().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->stepReadAloud(1);
		}
	});
	ReadAloudStop().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->stopReadAloud();
		}
	});
	ReadAloudPlay().Click([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			if (self->m_readAloudPaused) {
				self->resumeReadAloud();
			} else {
				self->pauseReadAloud();
			}
		}
	});
	for (const double rate : helpers::readAloudRates) {
		ReadAloudSpeed().Items().Append(
		    box_value(hstring(helpers::readAloudRateLabel(rate))));
	}
	ReadAloudVoice().SelectionChanged([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->chooseReadAloud();
		}
	});
	ReadAloudSpeed().SelectionChanged([weak](auto&&, auto&&) {
		if (const auto self = weak.get()) {
			self->chooseReadAloud();
		}
	});
}

void ViewerPage::startReadAloud() {
	if (m_closed || !m_session || !m_layout || organizing() ||
	    m_sizes.empty()) {
		return;
	}
	if (readingAloud()) {
		if (m_readAloudPaused) {
			resumeReadAloud();
		}
		return;
	}
	setupReadAloud();
#ifdef SABLE_SURFACE_TEST
	wchar_t mode[8]{};
	m_readAloudSilent =
	    GetEnvironmentVariableW(L"SableReadAloudTest", mode, 8) &&
	    std::wstring_view(mode) == L"true";
#endif
	const auto choice = helpers::loadReadAloud(s_readAloudFile);
	m_readAloudFilling = true;
	try {
		if (!m_readAloudSilent && !m_speech) {
			m_speech = Speech::SpeechSynthesizer();
			m_speechPlayer = Windows::Media::Playback::MediaPlayer();
			m_speechPlayer.AutoPlay(false);
			m_speechEnded = m_speechPlayer.MediaEnded(
			    auto_revoke,
			    [weak = get_weak(), queue = DispatcherQueue()](auto&&, auto&&) {
				    queue.TryEnqueue([weak] {
					    if (const auto self = weak.get()) {
						    self->readAloudEnded();
					    }
				    });
			    });
			m_readAloudVoices.clear();
			ReadAloudVoice().Items().Clear();
			for (const auto& voice : Speech::SpeechSynthesizer::AllVoices()) {
				m_readAloudVoices.push_back(voice);
				ReadAloudVoice().Items().Append(box_value(voice.DisplayName()));
			}
		}
		if (m_speech) {
			int selected = -1;
			for (std::size_t i = 0; i < m_readAloudVoices.size(); ++i) {
				if (to_string(m_readAloudVoices[i].Id()) == choice.voice) {
					selected = static_cast<int>(i);
					m_speech.Voice(m_readAloudVoices[i]);
				}
			}
			if (selected < 0) {
				const auto current = m_speech.Voice().Id();
				for (std::size_t i = 0; i < m_readAloudVoices.size(); ++i) {
					if (m_readAloudVoices[i].Id() == current) {
						selected = static_cast<int>(i);
					}
				}
			}
			ReadAloudVoice().SelectedIndex(selected);
			m_speech.Options().SpeakingRate(choice.rate);
		}
		const auto rates = std::begin(helpers::readAloudRates);
		ReadAloudSpeed().SelectedIndex(static_cast<int>(
		    std::find(rates, std::end(helpers::readAloudRates), choice.rate) -
		    rates));
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not start the speech synthesizer");
		m_readAloudFilling = false;
		m_speech = nullptr;
		m_speechEnded.revoke();
		m_speechPlayer = nullptr;
		showNotice(L"Read Aloud needs a Windows voice. Add one in Settings > "
		           L"Time & language > Speech.");
		return;
	}
	m_readAloudFilling = false;
	ReadAloudVoice().Visibility(m_readAloudSilent ? Visibility::Collapsed
	                                              : Visibility::Visible);
	m_readAloudViewChanged = Scroller().ViewChanged(
	    auto_revoke, [weak = get_weak()](auto&&, auto&&) {
		    if (const auto self = weak.get()) {
			    self->updateReadAloudFrame();
		    }
	    });
	// a hidden tab goes quiet; Play carries on when it's back
	m_readAloudVisibility = RegisterPropertyChangedCallback(
	    UIElement::VisibilityProperty(), [weak = get_weak()](auto&&, auto&&) {
		    if (const auto self = weak.get();
		        self && self->Visibility() != Visibility::Visible) {
			    self->pauseReadAloud();
		    }
	    });
	ReadAloudBar().Visibility(Visibility::Visible);
	readAloudPage((std::min)(m_current, m_sizes.size() - 1));
}
void ViewerPage::readAloudPage(std::size_t page) {
	if (m_closed || !m_session) {
		stopReadAloud();
		return;
	}
	if (page >= m_sizes.size()) {
		stopReadAloud();
		showNotice(L"Read Aloud reached the end of the document.");
		return;
	}
	const auto generation = ++m_readAloudGeneration;
	m_readAloudPage = page;
	m_readAloudPending = true;
	m_readAloudWords.clear();
	if (m_speechPlayer) {
		m_speechPlayer.Pause();
	}
	if (m_current != page) {
		goToPage(page);
	}
	updateReadAloudBar();
	updateReadAloudFrame();
	try {
		if (m_session->documentText(
		        {static_cast<int>(page)},
		        [weak = get_weak(), queue = DispatcherQueue(), generation,
		         page](pdf::DocumentTextResult result) {
			        queue.TryEnqueue([weak, generation, page,
			                          text = std::move(result.text)]() mutable {
				        if (const auto self = weak.get()) {
					        self->speakReadAloud(generation, page,
					                             std::move(text));
				        }
			        });
		        })) {
			return;
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not queue the page text for Read Aloud");
	}
	stopReadAloud();
	showNotice(L"Couldn't read this page aloud. Try again.");
}

void ViewerPage::speakReadAloud(std::uint64_t generation, std::size_t page,
                                std::optional<std::string> text) {
	if (m_closed || generation != m_readAloudGeneration ||
	    m_readAloudPage != page) {
		return;
	}
	if (!text) {
		stopReadAloud();
		showNotice(L"Couldn't get the text on page " + to_hstring(page + 1) +
		           L" to read aloud.");
		return;
	}
	auto words = helpers::speakableText(*text);
	if (words.empty()) {
		// a scanned page has nothing to say; the next one may
		showNotice(L"Page " + to_hstring(page + 1) +
		           L" has no text to read, so Read Aloud went on.");
		readAloudPage(page + 1);
		return;
	}
	if (words.size() > MAX_SPOKEN) {
		words.resize(MAX_SPOKEN);
	}
	m_readAloudWords = words;
	if (m_readAloudSilent || !m_speech) {
		m_readAloudPending = false;
		updateReadAloudBar();
		return;
	}
	synthesizeReadAloud(generation, to_hstring(words));
}

fire_and_forget ViewerPage::synthesizeReadAloud(std::uint64_t generation,
                                                hstring text) {
	const auto lifetime = get_strong();
	try {
		const auto stream = co_await m_speech.SynthesizeTextToStreamAsync(text);
		if (m_closed || generation != m_readAloudGeneration ||
		    !m_speechPlayer) {
			co_return;
		}
		m_speechPlayer.Source(
		    Windows::Media::Core::MediaSource::CreateFromStream(
		        stream, stream.ContentType()));
		m_readAloudPending = false;
		if (!m_readAloudPaused) {
			m_speechPlayer.Play();
		}
		updateReadAloudBar();
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Error,
		                    "Could not speak the page");
		if (!m_closed && generation == m_readAloudGeneration) {
			stopReadAloud();
			showNotice(L"Couldn't read this page aloud. Try again.");
		}
	}
}

void ViewerPage::readAloudEnded() {
	if (m_closed || !m_readAloudPage || m_readAloudPending ||
	    m_readAloudPaused) {
		return;
	}
	readAloudPage(*m_readAloudPage + 1);
}

void ViewerPage::pauseReadAloud() {
	if (!readingAloud() || m_readAloudPaused) {
		return;
	}
	m_readAloudPaused = true;
	if (m_speechPlayer) {
		m_speechPlayer.Pause();
	}
	updateReadAloudBar();
}

void ViewerPage::resumeReadAloud() {
	if (!readingAloud() || !m_readAloudPaused) {
		return;
	}
	m_readAloudPaused = false;
	if (m_speechPlayer && !m_readAloudPending) {
		m_speechPlayer.Play();
	}
	updateReadAloudBar();
}

void ViewerPage::stopReadAloud() {
	++m_readAloudGeneration;
	m_readAloudPage.reset();
	m_readAloudPaused = false;
	m_readAloudPending = false;
	m_readAloudWords.clear();
	if (m_speechPlayer) {
		try {
			m_speechPlayer.Pause();
			m_speechPlayer.Source(nullptr);
		} catch (...) {
			diagnostics::caught(diagnostics::Level::Warning,
			                    "Could not stop the Read Aloud player");
		}
	}
	m_readAloudViewChanged.revoke();
	if (m_readAloudVisibility) {
		UnregisterPropertyChangedCallback(UIElement::VisibilityProperty(),
		                                  m_readAloudVisibility);
		m_readAloudVisibility = 0;
	}
	ReadAloudBar().Visibility(Visibility::Collapsed);
	ReadAloudFrame().Visibility(Visibility::Collapsed);
}

void ViewerPage::stepReadAloud(int delta) {
	if (!m_readAloudPage) {
		return;
	}
	const auto page = static_cast<long long>(*m_readAloudPage) + delta;
	if (page < 0 || page >= static_cast<long long>(m_sizes.size())) {
		return;
	}
	readAloudPage(static_cast<std::size_t>(page));
}
// a new voice or speed is kept in read-aloud.json and takes over from the
// start of the page being read.
void ViewerPage::chooseReadAloud() {
	if (m_readAloudFilling || !readingAloud()) {
		return;
	}
	auto choice = helpers::loadReadAloud(s_readAloudFile);
	const int voice = ReadAloudVoice().SelectedIndex();
	const int speed = ReadAloudSpeed().SelectedIndex();
	try {
		if (m_speech && voice >= 0 &&
		    voice < static_cast<int>(m_readAloudVoices.size())) {
			const auto& chosen =
			    m_readAloudVoices[static_cast<std::size_t>(voice)];
			m_speech.Voice(chosen);
			choice.voice = to_string(chosen.Id());
		}
		if (speed >= 0 &&
		    speed < static_cast<int>(std::size(helpers::readAloudRates))) {
			choice.rate = helpers::readAloudRates[speed];
			if (m_speech) {
				m_speech.Options().SpeakingRate(choice.rate);
			}
		}
	} catch (...) {
		diagnostics::caught(diagnostics::Level::Warning,
		                    "Could not change the Read Aloud voice");
	}
	helpers::saveReadAloud(s_readAloudFile, choice);
	if (m_readAloudPage) {
		readAloudPage(*m_readAloudPage);
	}
}

void ViewerPage::updateReadAloudBar() {
	if (!m_readAloudPage) {
		return;
	}
	ReadAloudPosition().Text(L"Reading page " +
	                         to_hstring(*m_readAloudPage + 1) + L" of " +
	                         to_hstring(m_sizes.size()));
	const hstring action = m_readAloudPaused ? L"Play" : L"Pause";
	ReadAloudPlayIcon().Glyph(m_readAloudPaused ? L"\uE768" : L"\uE769");
	ToolTipService::SetToolTip(ReadAloudPlay(), box_value(action));
	Automation::AutomationProperties::SetName(ReadAloudPlay(), action);
	ReadAloudPrevious().IsEnabled(*m_readAloudPage > 0);
	ReadAloudNext().IsEnabled(*m_readAloudPage + 1 < m_sizes.size());
}

// the outline follows the page through scrolling and zoom
void ViewerPage::updateReadAloudFrame() {
	if (!m_readAloudPage || !m_layout || organizing() ||
	    *m_readAloudPage >= m_layout->pages().size()) {
		ReadAloudFrame().Visibility(Visibility::Collapsed);
		return;
	}
	try {
		const auto& rect = m_layout->pages()[*m_readAloudPage];
		const auto shown =
		    Pages()
		        .TransformToVisual(ReadAloudFrame())
		        .TransformBounds({static_cast<float>(rect.x),
		                          static_cast<float>(rect.y),
		                          static_cast<float>(rect.width),
		                          static_cast<float>(rect.height)});
		const auto outline = ReadAloudOutline();
		Canvas::SetLeft(outline, shown.X - 3);
		Canvas::SetTop(outline, shown.Y - 3);
		outline.Width((std::max)(0.f, shown.Width + 6));
		outline.Height((std::max)(0.f, shown.Height + 6));
		Media::RectangleGeometry clip;
		clip.Rect({0, 0, static_cast<float>(Scroller().ActualWidth()),
		           static_cast<float>(Scroller().ActualHeight())});
		ReadAloudFrame().Clip(clip);
		ReadAloudFrame().Visibility(Visibility::Visible);
	} catch (...) {
		ReadAloudFrame().Visibility(Visibility::Collapsed);
	}
}
} // namespace winrt::Sable::implementation
