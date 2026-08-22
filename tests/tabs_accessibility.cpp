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
#include "tabs_test.h"
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Text.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <fstream>
#include <set>

namespace winrt::Sable::implementation {
namespace {
using namespace Microsoft::UI::Xaml::Automation;
using Media::VisualTreeHelper;

std::wstring describe(const DependencyObject& node) {
	std::wstring text(get_class_name(node.as<IInspectable>()));
	if (const auto element = node.try_as<FrameworkElement>();
	    element && !element.Name().empty()) {
		text += L" " + std::wstring(element.Name());
	}
	return text;
}

bool shown(const DependencyObject& node, const DependencyObject& root) {
	for (auto at = node; at; at = VisualTreeHelper::GetParent(at)) {
		if (const auto element = at.try_as<UIElement>();
		    element && (element.Visibility() != Visibility::Visible ||
		                element.Opacity() == 0)) {
			return false;
		}
		if (at == root) {
			break;
		}
	}
	return true;
}

// The controls a keyboard user has to reach: anything UI Automation calls
// keyboard focusable, plus buttons, toggles, sliders and lists that turned
// their tab stop off while still enabled.
void collect(const DependencyObject& node, const DependencyObject& root,
             std::vector<Control>& controls) {
	if (const auto control = node.try_as<Control>();
	    control && control.IsEnabled() && shown(node, root)) {
		controls.push_back(control);
	}
	const auto count = VisualTreeHelper::GetChildrenCount(node);
	for (int32_t i = 0; i < count; ++i) {
		collect(VisualTreeHelper::GetChild(node, i), root, controls);
	}
}

bool interactive(const Control& control) {
	return control.try_as<Primitives::ButtonBase>() ||
	       control.try_as<Primitives::RangeBase>() ||
	       control.try_as<Primitives::Selector>() ||
	       control.try_as<TextBox>() || control.try_as<PasswordBox>() ||
	       control.try_as<RichEditBox>() || control.try_as<ToggleSwitch>();
}
} // namespace

// Writes one line per control that has no accessible name, can't take
// keyboard focus, or that Tab never reaches from the surface's first stop.
// Returns how many it found.
int TabsTest::auditSurface(const UIElement& root, const std::wstring& surface,
                           bool walkTabs) {
	std::vector<Control> controls;
	collect(root, root, controls);
	// menus and dialogs in popups are reached with arrows or their own
	// focus scope, so only their names are checked
	const auto inTree = controls.size();
	if (const auto xamlRoot = root.XamlRoot()) {
		for (const auto& popup :
		     VisualTreeHelper::GetOpenPopupsForXamlRoot(xamlRoot)) {
			if (const auto child = popup.Child()) {
				collect(child, child, controls);
			}
		}
	}
	std::set<std::wstring> lines;
	std::vector<Control> stops;
	for (std::size_t i = 0; i < controls.size(); ++i) {
		const auto& control = controls[i];
		const auto peer =
		    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
		        control);
		const bool focusable = peer && peer.IsKeyboardFocusable();
		if (focusable && i < inTree) {
			stops.push_back(control);
		}
		if (!interactive(control)) {
			continue;
		}
		// lists take focus through their items, and the search box's query
		// button is reached with Enter in its text box
		if (!focusable && control.IsTabStop() == false &&
		    !control.try_as<Primitives::ScrollBar>() &&
		    !control.try_as<Primitives::Thumb>() &&
		    !control.try_as<Primitives::Selector>() &&
		    control.Name() != L"QueryButton") {
			lines.insert(surface + L": not focusable: " + describe(control));
		}
		if (focusable && peer.GetName().empty()) {
			lines.insert(surface + L": no name: " + describe(control));
		}
	}
	if (walkTabs && !stops.empty() && root.XamlRoot()) {
		Input::FindNextElementOptions options;
		options.SearchRoot(root.XamlRoot().Content());
		std::set<void*> reached;
		const auto start = Input::FocusManager::FindFirstFocusableElement(root);
		if (const auto control = start.try_as<Control>()) {
			control.Focus(FocusState::Keyboard);
		}
		for (int i = 0; i < 600; ++i) {
			const auto focused =
			    Input::FocusManager::GetFocusedElement(root.XamlRoot());
			if (focused) {
				if (!reached
				         .insert(get_abi(
				             focused.as<Windows::Foundation::IUnknown>()))
				         .second &&
				    i > 0) {
					break;
				}
			}
			if (!Input::FocusManager::TryMoveFocus(
			        Input::FocusNavigationDirection::Next, options)) {
				break;
			}
		}
		for (const auto& control : stops) {
			if (reached.count(
			        get_abi(control.as<Windows::Foundation::IUnknown>()))) {
				continue;
			}
			// a TabFocusNavigation="Once" group is one stop; arrows move
			// inside it
			bool grouped = false;
			for (auto at = VisualTreeHelper::GetParent(control); at;
			     at = VisualTreeHelper::GetParent(at)) {
				if (const auto element = at.try_as<UIElement>();
				    element && element.TabFocusNavigation() ==
				                   Input::KeyboardNavigationMode::Once) {
					grouped = true;
					break;
				}
			}
			if (!grouped) {
				lines.insert(surface + L": Tab never reaches: " +
				             describe(control));
			}
		}
	}
	std::ofstream out(std::filesystem::temp_directory_path() /
	                      L"Sable-probe-logs" / L"accessibility.txt",
	                  std::ios::app);
	for (const auto& line : lines) {
		out << to_string(line) << "\n";
	}
	out << to_string(surface) << ": " << controls.size() << " controls, "
	    << stops.size() << " stops, " << lines.size() << " findings\n";
	return static_cast<int>(lines.size());
}

// Accessibility: Home, the viewer, Organize, Fill & Sign, Comments, Read
// Aloud, Export and Combine each have a name for every control and a Tab
// path to it.
void TabsTest::accessibilityTick(MainWindow& owner) {
	const auto home = owner.Home();
	switch (step) {
	case 0:
		if (!home.IsLoaded()) {
			return;
		}
		{
			std::error_code ignored;
			std::filesystem::create_directories(
			    std::filesystem::temp_directory_path() / L"Sable-probe-logs",
			    ignored);
			std::filesystem::remove(std::filesystem::temp_directory_path() /
			                            L"Sable-probe-logs" /
			                            L"accessibility.txt",
			                        ignored);
		}
		findings += auditSurface(home, L"Home", true);
		owner.openDocument(engine, L"tests/fixtures/text.pdf", 1);
		break;
	default: {
		const auto current = owner.Viewer();
		if (!current) {
			return;
		}
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v) || v.m_organizeBusy || ticks < 3) {
			return;
		}
		switch (step) {
		case 1:
			findings += auditSurface(current, L"Viewer", true);
			// asking for the peer is what a screen reader does first; the
			// page text follows on the PDFium thread
			pagePeer =
			    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
			        v.PageReader());
			v.openOrganize();
			break;
		case 2: {
			if (!v.organizing()) {
				check(ticks < 60, "Organize didn't open");
				return;
			}
			check(pagePeer.GetAutomationControlType() ==
			              Peers::AutomationControlType::Document &&
			          std::wstring_view(pagePeer.GetName())
			              .starts_with(L"Page 1 of "),
			      "the page isn't a UI Automation document named Page 1: " +
			          to_string(pagePeer.GetName()));
			const auto text = pagePeer.GetPattern(Peers::PatternInterface::Text)
			                      .as<Provider::ITextProvider>();
			const auto range = text.DocumentRange();
			if (range.GetText(-1).empty()) {
				check(ticks < 60, "the page text never reached UI Automation");
				return;
			}
			check(range.GetText(-1) == L"Alpha beta\nSecond line",
			      "UI Automation read: " + to_string(range.GetText(-1)));
			const auto line = range.Clone();
			line.MoveEndpointByRange(Text::TextPatternRangeEndpoint::End, line,
			                         Text::TextPatternRangeEndpoint::Start);
			line.ExpandToEnclosingUnit(Text::TextUnit::Line);
			check(line.GetText(-1) == L"Alpha beta\n",
			      "the first line read as: " + to_string(line.GetText(-1)));
			check(line.Move(Text::TextUnit::Word, 2) == 2 &&
			          line.GetText(-1) == L"Second ",
			      "moving two words read: " + to_string(line.GetText(-1)));
			const auto found = range.FindText(L"SECOND", false, true);
			check(found && found.GetText(-1) == L"Second",
			      "FindText didn't find Second");
			pagePeer = nullptr;
			findings += auditSurface(current, L"Organize", true);
			v.closeOrganize();
			v.requestTool(L"comment");
			break;
		}
		case 3:
			check(v.commentsOpen(), "Comments didn't open");
			findings += auditSurface(current, L"Comments", true);
			v.requestTool(L"fillsign");
			break;
		case 4:
			findings += auditSurface(current, L"Fill & Sign", false);
			v.CommentsMoreButton().Flyout().Hide();
			v.closeComments();
			v.startReadAloud();
			break;
		case 5:
			findings += auditSurface(current, L"Read Aloud", true);
			v.stopReadAloud();
			v.exportPages();
			break;
		case 6:
			if (!v.m_noteDialog) {
				check(ticks < 60, "Export didn't open");
				return;
			}
			findings += auditSurface(current, L"Export", false);
			v.m_noteDialog.Hide();
			break;
		case 7:
			if (v.m_noteDialog) {
				check(ticks < 60, "Export didn't close");
				return;
			}
			owner.setEngine(engine);
			owner.openTool(L"combine");
			break;
		case 8:
			if (!owner.m_combineDialog) {
				check(ticks < 60, "Combine didn't open");
				return;
			}
			findings += auditSurface(current, L"Combine", false);
			owner.m_combineDialog.Hide();
			break;
		case 9:
			if (owner.m_combining) {
				check(ticks < 60, "Combine didn't close");
				return;
			}
			check(findings == 0, std::to_string(findings) +
			                         " accessibility findings; see "
			                         "Sable-probe-logs\\accessibility.txt");
			v.close();
			status.Text(L"PASS: names and keyboard reach on every surface");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
	}
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
