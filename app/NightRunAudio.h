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

#include <winrt/base.h>
#include <xaudio2.h>

namespace night_run {
enum class Cue { Jump, Crash };

class Audio {
  public:
	Audio() = default;
	~Audio();
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;
	void play(Cue cue) noexcept;
	void stop() noexcept;

  private:
	bool open() noexcept;
	winrt::com_ptr<IXAudio2> m_engine;
	IXAudio2MasteringVoice* m_output = nullptr;
	IXAudio2SourceVoice* m_voice = nullptr;
};
} // namespace night_run
