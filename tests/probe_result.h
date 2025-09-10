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

// pending is a failure too: closing a check early must not look like a pass
class ProbeResult {
  public:
	void pass() {
		if (m_state == State::Pending) {
			m_state = State::Passed;
		}
	}
	void fail() {
		m_state = State::Failed;
	}
	int exitCode() const {
		return m_state == State::Passed ? 0 : 1;
	}

  private:
	enum class State { Pending, Passed, Failed };
	State m_state = State::Pending;
};
