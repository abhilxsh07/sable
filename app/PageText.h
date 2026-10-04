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

#include "PageTextHost.g.h"
#include "PageTextPeer.g.h"
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Text.h>
#include <functional>
#include <memory>
#include <string>

namespace winrt::Sable::implementation {
struct PageTextHost : PageTextHostT<PageTextHost> {
	PageTextHost() = default;
	hstring PageName() const {
		return m_name;
	}
	void PageName(const hstring& name);
	hstring PageText() const;
	void PageText(const hstring& text);
	void publish(const hstring& name, const hstring& text);
	Microsoft::UI::Xaml::Automation::Peers::AutomationPeer
	OnCreateAutomationPeer();
	// the text ranges keep the snapshot they were made from, so a page
	// change can't move their offsets under them
	std::shared_ptr<const std::wstring> snapshot() const {
		return m_text;
	}
	// set once a screen reader (or any UI Automation client) asks for the
	// peer; until then the viewer doesn't fetch page text for it
	bool wanted() const {
		return m_wanted;
	}
	std::function<void()> onWanted;

  private:
	hstring m_name;
	std::shared_ptr<const std::wstring> m_text =
	    std::make_shared<const std::wstring>();
	bool m_wanted = false;
};

struct PageTextPeer : PageTextPeerT<PageTextPeer> {
	explicit PageTextPeer(const Sable::PageTextHost& owner);
	Windows::Foundation::IInspectable GetPatternCore(
	    Microsoft::UI::Xaml::Automation::Peers::PatternInterface const&
	        pattern);
	Microsoft::UI::Xaml::Automation::Peers::AutomationControlType
	GetAutomationControlTypeCore();
	hstring GetClassNameCore();
	hstring GetNameCore();

	Microsoft::UI::Xaml::Automation::Provider::ITextRangeProvider
	DocumentRange();
	Microsoft::UI::Xaml::Automation::SupportedTextSelection
	SupportedTextSelection();
	com_array<Microsoft::UI::Xaml::Automation::Provider::ITextRangeProvider>
	GetSelection();
	com_array<Microsoft::UI::Xaml::Automation::Provider::ITextRangeProvider>
	GetVisibleRanges();
	Microsoft::UI::Xaml::Automation::Provider::ITextRangeProvider
	RangeFromChild(Microsoft::UI::Xaml::Automation::Provider::
	                   IRawElementProviderSimple const&);
	Microsoft::UI::Xaml::Automation::Provider::ITextRangeProvider
	RangeFromPoint(Windows::Foundation::Point const&);

  private:
	weak_ref<Sable::PageTextHost> m_owner;
};
} // namespace winrt::Sable::implementation

namespace winrt::Sable::factory_implementation {
struct PageTextHost
    : PageTextHostT<PageTextHost, implementation::PageTextHost> {};
struct PageTextPeer
    : PageTextPeerT<PageTextPeer, implementation::PageTextPeer> {};
} // namespace winrt::Sable::factory_implementation
