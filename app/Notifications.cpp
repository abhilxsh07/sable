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
#include "Notifications.h"

#include <winrt/Microsoft.Windows.AppNotifications.Builder.h>

#include "helpers/logging.h"

namespace notices {
winrt::Microsoft::Windows::AppNotifications::AppNotification
Service::notification() {
	return winrt::Microsoft::Windows::AppNotifications::Builder::
	    AppNotificationBuilder()
	        .AddText(TITLE)
	        .AddText(BODY)
	        .BuildNotification();
}

bool Service::initialize(std::function<void()> activated) noexcept {
	try {
		if (!m_registered) {
			if (!winrt::Microsoft::Windows::AppNotifications::
			        AppNotificationManager::IsSupported()) {
				return false;
			}
			m_manager = winrt::Microsoft::Windows::AppNotifications::
			    AppNotificationManager::Default();
			m_invoked = m_manager.NotificationInvoked(
			    [activated = std::move(activated)](auto&&, auto&&) {
				    diagnostics::write(diagnostics::Level::Debug,
				                       "App notification invoked");
				    try {
					    if (activated) {
						    activated();
					    }
				    } catch (...) {
					    diagnostics::write(
					        diagnostics::Level::Warning,
					        "Couldn't activate a notification reader");
				    }
			    });
			try {
				m_manager.Register();
			} catch (...) {
				m_manager.NotificationInvoked(m_invoked);
				m_manager = nullptr;
				throw;
			}
			m_registered = true;
		}
		return true;
	} catch (...) {
		diagnostics::write(diagnostics::Level::Warning,
		                   "Couldn't register app notifications");
		return false;
	}
}

bool Service::show() noexcept {
	try {
		if (!initialize()) {
			return false;
		}
		auto toast = notification();
		m_manager.Show(toast);
		if (toast.Id() == 0) {
			diagnostics::write(diagnostics::Level::Warning,
			                   "Windows did not accept the app notification");
			return false;
		}
		diagnostics::write(diagnostics::Level::Debug,
		                   "App notification submitted");
		return true;
	} catch (...) {
		diagnostics::write(diagnostics::Level::Warning,
		                   "Couldn't show the app notification");
		return false;
	}
}

Service::~Service() {
	if (!m_manager) {
		return;
	}
	try {
		if (m_registered) {
			m_manager.Unregister();
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Warning,
		                   "Couldn't unregister app notifications");
	}
	try {
		m_manager.NotificationInvoked(m_invoked);
	} catch (...) {
	}
}
} // namespace notices
