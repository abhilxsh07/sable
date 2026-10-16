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

#include <winrt/Microsoft.Windows.AppNotifications.h>
#include <functional>

namespace notices {
inline constexpr auto TITLE = L"Trans rights";
inline constexpr auto BODY =
    L"Trans rights are human rights \U0001f3f3\ufe0f\u200d\u26a7\ufe0f";
// Register before reading activation arguments so a notification click can
// activate a reader instead of being interpreted as a document path.
struct Service {
	Service() = default;
	Service(const Service&) = delete;
	Service& operator=(const Service&) = delete;
	~Service();
	bool initialize(std::function<void()> activated = {}) noexcept;
	bool show() noexcept;
	static winrt::Microsoft::Windows::AppNotifications::AppNotification
	notification();

  private:
	winrt::Microsoft::Windows::AppNotifications::AppNotificationManager
	    m_manager{nullptr};
	winrt::event_token m_invoked{};
	bool m_registered = false;
};
} // namespace notices
