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
#include "NightRunAudio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace night_run {
namespace {
constexpr unsigned SAMPLE_RATE = 22050;

template <std::size_t Size>
std::array<std::int16_t, Size> sweep(double start, double end) {
	std::array<std::int16_t, Size> samples{};
	double phase = 0;
	for (std::size_t i = 0; i < Size; ++i) {
		const double time = static_cast<double>(i) / (Size - 1);
		const double envelope = (std::min)(time * 25, 1.0) * (1 - time);
		samples[i] =
		    static_cast<std::int16_t>(std::sin(phase) * envelope * 3300);
		phase +=
		    2 * std::numbers::pi * (start + (end - start) * time) / SAMPLE_RATE;
	}
	return samples;
}
} // namespace

Audio::~Audio() {
	stop();
	if (m_output) {
		m_output->DestroyVoice();
	}
}

bool Audio::open() noexcept {
	if (!m_engine && FAILED(XAudio2Create(m_engine.put()))) {
		return false;
	}
	if (!m_output &&
	    FAILED(m_engine->CreateMasteringVoice(
	        &m_output, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0,
	        nullptr, nullptr, AudioCategory_GameEffects))) {
		return false;
	}

	WAVEFORMATEX format{};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 1;
	format.nSamplesPerSec = SAMPLE_RATE;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 2;
	format.nAvgBytesPerSec = SAMPLE_RATE * format.nBlockAlign;
	return SUCCEEDED(m_engine->CreateSourceVoice(&m_voice, &format));
}

void Audio::play(Cue cue) noexcept {
	if (!m_voice && !open()) {
		return;
	}
	// the voice reads these samples after SubmitSourceBuffer returns
	// see:
	// https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2sourcevoice-submitsourcebuffer
	static const auto jump = sweep<SAMPLE_RATE / 12>(390, 770);
	static const auto crash = sweep<SAMPLE_RATE / 5>(245, 80);
	XAUDIO2_BUFFER buffer{};
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	if (cue == Cue::Jump) {
		buffer.pAudioData = reinterpret_cast<const BYTE*>(jump.data());
		buffer.AudioBytes = static_cast<UINT32>(jump.size() * sizeof(jump[0]));
	} else {
		buffer.pAudioData = reinterpret_cast<const BYTE*>(crash.data());
		buffer.AudioBytes =
		    static_cast<UINT32>(crash.size() * sizeof(crash[0]));
	}
	m_voice->FlushSourceBuffers();
	if (SUCCEEDED(m_voice->SubmitSourceBuffer(&buffer))) {
		m_voice->Start();
	}
}

void Audio::stop() noexcept {
	if (m_voice) {
		m_voice->DestroyVoice();
		m_voice = nullptr;
	}
}
} // namespace night_run
