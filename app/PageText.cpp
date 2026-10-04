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
#include "PageText.h"
#if __has_include("PageTextHost.g.cpp")
#include "PageTextHost.g.cpp"
#endif
#if __has_include("PageTextPeer.g.cpp")
#include "PageTextPeer.g.cpp"
#endif
#include "helpers/text_range.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Automation;
using namespace Microsoft::UI::Xaml::Automation::Peers;
using namespace Microsoft::UI::Xaml::Automation::Provider;
using Microsoft::UI::Xaml::Automation::Text::TextPatternRangeEndpoint;

namespace winrt::Sable::implementation {
namespace {
// One stretch of a page snapshot, [start, end). Ranges never outlive the
// text they index because they share it.
struct TextRange : implements<TextRange, ITextRangeProvider> {
	TextRange(std::shared_ptr<const std::wstring> text, std::size_t start,
	          std::size_t end, IRawElementProviderSimple element)
	    : m_text(std::move(text)), m_start(start), m_end(end),
	      m_element(std::move(element)) {}

	ITextRangeProvider Clone() {
		return make<TextRange>(m_text, m_start, m_end, m_element);
	}
	bool Compare(const ITextRangeProvider& other) {
		const auto range = get_self<TextRange>(other);
		return range->m_text == m_text && range->m_start == m_start &&
		       range->m_end == m_end;
	}
	int32_t CompareEndpoints(TextPatternRangeEndpoint endpoint,
	                         const ITextRangeProvider& other,
	                         TextPatternRangeEndpoint target) {
		const auto mine = at(endpoint);
		const auto theirs = get_self<TextRange>(other)->at(target);
		return mine < theirs ? -1 : mine > theirs ? 1 : 0;
	}
	void ExpandToEnclosingUnit(Text::TextUnit unit) {
		const auto range =
		    helpers::unitAround(*m_text, m_start, helpers::TextUnit(unit));
		m_start = range.first;
		m_end = range.second;
	}
	ITextRangeProvider
	FindAttribute(int32_t, const Windows::Foundation::IInspectable&, bool) {
		return nullptr;
	}
	ITextRangeProvider FindText(const hstring& text, bool backward,
	                            bool ignoreCase) {
		const auto found = helpers::findText(*m_text, m_start, m_end, text,
		                                     backward, ignoreCase);
		if (!found) {
			return nullptr;
		}
		return make<TextRange>(m_text, found->first, found->second, m_element);
	}
	Windows::Foundation::IInspectable GetAttributeValue(int32_t) {
		// no fonts or styles to report
		return DependencyProperty::UnsetValue();
	}
	void GetBoundingRectangles(com_array<double>& rectangles) {
		// the characters' boxes aren't mapped to the screen yet
		rectangles = com_array<double>();
	}
	IRawElementProviderSimple GetEnclosingElement() {
		return m_element;
	}
	hstring GetText(int32_t maxLength) {
		auto length = m_end - m_start;
		if (maxLength >= 0) {
			length = std::min<std::size_t>(length, maxLength);
		}
		return hstring(std::wstring_view(*m_text).substr(m_start, length));
	}
	// Collapses to the start of the unit, moves over whole units and takes
	// the unit it lands on. It never lands on the very end, which starts no
	// unit, so the last word reports that it couldn't move.
	int32_t Move(Text::TextUnit unit, int32_t count) {
		const auto kind = helpers::TextUnit(unit);
		const auto size = m_text->size();
		if (count == 0 || size == 0) {
			return 0;
		}
		const bool degenerate = m_start == m_end;
		auto pos = helpers::unitAround(*m_text, m_start, kind).first;
		int moved = 0;
		while (count > 0) {
			int step = 0;
			const auto next = helpers::moveByUnit(*m_text, pos, kind, 1, step);
			if (step == 0 || next >= size) {
				break;
			}
			pos = next;
			++moved;
			--count;
		}
		while (count < 0) {
			int step = 0;
			const auto next = helpers::moveByUnit(*m_text, pos, kind, -1, step);
			if (step == 0) {
				break;
			}
			pos = next;
			--moved;
			++count;
		}
		m_start = pos;
		m_end =
		    degenerate ? pos : helpers::unitAround(*m_text, pos, kind).second;
		return moved;
	}
	int32_t MoveEndpointByUnit(TextPatternRangeEndpoint endpoint,
	                           Text::TextUnit unit, int32_t count) {
		int moved = 0;
		at(endpoint) = helpers::moveByUnit(
		    *m_text, at(endpoint), helpers::TextUnit(unit), count, moved);
		order(endpoint);
		return moved;
	}
	void MoveEndpointByRange(TextPatternRangeEndpoint endpoint,
	                         const ITextRangeProvider& other,
	                         TextPatternRangeEndpoint target) {
		at(endpoint) = get_self<TextRange>(other)->at(target);
		order(endpoint);
	}
	// selecting would need the viewer's selection; reading doesn't
	void Select() {}
	void AddToSelection() {}
	void RemoveFromSelection() {}
	void ScrollIntoView(bool) {}
	com_array<IRawElementProviderSimple> GetChildren() {
		return com_array<IRawElementProviderSimple>();
	}

	std::size_t& at(TextPatternRangeEndpoint endpoint) {
		return endpoint == TextPatternRangeEndpoint::Start ? m_start : m_end;
	}

  private:
	// a moved endpoint that crosses the other one drags it along
	void order(TextPatternRangeEndpoint moved) {
		m_start = (std::min)(m_start, m_text->size());
		m_end = (std::min)(m_end, m_text->size());
		if (m_start > m_end) {
			(moved == TextPatternRangeEndpoint::Start ? m_end : m_start) =
			    at(moved);
		}
	}
	std::shared_ptr<const std::wstring> m_text;
	std::size_t m_start, m_end;
	IRawElementProviderSimple m_element;
};
} // namespace

void PageTextHost::PageName(const hstring& name) {
	publish(name, PageText());
}

hstring PageTextHost::PageText() const {
	return hstring(*m_text);
}

void PageTextHost::PageText(const hstring& text) {
	publish(m_name, text);
}

void PageTextHost::publish(const hstring& name, const hstring& text) {
	auto cleaned = helpers::screenReaderText(text);
	const bool nameChanged = name != m_name;
	const bool textChanged = cleaned != *m_text;
	const auto snapshot =
	    textChanged ? std::make_shared<const std::wstring>(std::move(cleaned))
	                : m_text;
	// both values are ready before a notification can make a client read them
	m_name = name;
	m_text = snapshot;
	if (const auto peer = FrameworkElementAutomationPeer::FromElement(*this)) {
		if (nameChanged) {
			peer.RaiseNotificationEvent(
			    AutomationNotificationKind::Other,
			    AutomationNotificationProcessing::MostRecent, name,
			    L"SablePage");
		}
		if (textChanged) {
			peer.RaiseAutomationEvent(
			    AutomationEvents::TextPatternOnTextChanged);
		}
	}
}

AutomationPeer PageTextHost::OnCreateAutomationPeer() {
	const bool first = !m_wanted;
	m_wanted = true;
	if (first && onWanted) {
		onWanted();
	}
	return make<implementation::PageTextPeer>(*this);
}

PageTextPeer::PageTextPeer(const Sable::PageTextHost& owner)
    : PageTextPeerT<PageTextPeer>(owner), m_owner(owner) {}

Windows::Foundation::IInspectable
PageTextPeer::GetPatternCore(const PatternInterface& pattern) {
	if (pattern == PatternInterface::Text) {
		return *this;
	}
	return PageTextPeerT<PageTextPeer>::GetPatternCore(pattern);
}

AutomationControlType PageTextPeer::GetAutomationControlTypeCore() {
	return AutomationControlType::Document;
}

hstring PageTextPeer::GetClassNameCore() {
	return L"PageTextHost";
}

hstring PageTextPeer::GetNameCore() {
	const auto owner = m_owner.get();
	return owner ? owner.PageName() : hstring();
}

ITextRangeProvider PageTextPeer::DocumentRange() {
	const auto owner = m_owner.get();
	auto text = owner ? get_self<PageTextHost>(owner)->snapshot()
	                  : std::make_shared<const std::wstring>();
	const auto size = text->size();
	return make<TextRange>(std::move(text), 0, size,
	                       ProviderFromPeer(get_strong().as<AutomationPeer>()));
}

SupportedTextSelection PageTextPeer::SupportedTextSelection() {
	return SupportedTextSelection::None;
}

com_array<ITextRangeProvider> PageTextPeer::GetSelection() {
	return com_array<ITextRangeProvider>();
}

com_array<ITextRangeProvider> PageTextPeer::GetVisibleRanges() {
	return com_array<ITextRangeProvider>(1, DocumentRange());
}

ITextRangeProvider
PageTextPeer::RangeFromChild(const IRawElementProviderSimple&) {
	return DocumentRange();
}

ITextRangeProvider
PageTextPeer::RangeFromPoint(const Windows::Foundation::Point&) {
	auto range = DocumentRange();
	range.MoveEndpointByRange(TextPatternRangeEndpoint::End, range,
	                          TextPatternRangeEndpoint::Start);
	return range;
}
} // namespace winrt::Sable::implementation
