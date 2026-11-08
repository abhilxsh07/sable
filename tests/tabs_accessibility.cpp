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
// Aloud, Export, Combine and the tool pane each have a name for every
// control and a Tab path to it.
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
		{
			// the nav keeps Recent and Starred, with Settings below; the tools
			// are the tile row, so there is no Tools header or entries
			bool header = false;
			std::vector<hstring> tags;
			for (const auto& item : owner.Nav().MenuItems()) {
				if (item.try_as<Controls::NavigationViewItemHeader>()) {
					header = true;
				} else if (const auto entry =
				               item.try_as<Controls::NavigationViewItem>()) {
					tags.push_back(unbox_value_or<hstring>(entry.Tag(), L""));
				}
			}
			check(!header && tags.size() == 2 && tags[0] == L"recent" &&
			          tags[1] == L"starred" &&
			          owner.Nav().FooterMenuItems().Size() == 1,
			      "Home's nav still has the Tools group");
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
			v.closeComments();
			v.startReadAloud();
			break;
		case 5:
			findings += auditSurface(current, L"Read Aloud", true);
			v.stopReadAloud();
			v.exportPages();
			break;
		case 6:
			check(!v.m_noteDialog &&
			          v.AllToolsPanel().Visibility() == Visibility::Visible &&
			          v.ToolPaneTitle().Text() == L"Export",
			      "Export didn't open in the tool pane");
			findings += auditSurface(v.AllToolsPanel(), L"Export pane", true);
			v.closePane();
			break;
		case 7:
			owner.setEngine(engine);
			owner.openTool(L"combine");
			break;
		case 8:
			if (v.m_combineLoading) {
				check(ticks < 60, "Combine input details didn't load");
				return;
			}
			check(v.AllToolsPanel().Visibility() == Visibility::Visible &&
			          v.ToolPaneTitle().Text() == L"Combine files",
			      "Combine didn't open in the tool pane");
			findings += auditSurface(v.AllToolsPanel(), L"Combine pane", true);
			v.closePane();
			break;
		case 9:
			if (owner.m_combining) {
				check(ticks < 60, "Combine didn't close");
				return;
			}
			v.setAllToolsOpen(true);
			break;
		case 10: {
			// the tool pane names itself, offers a way out, and takes focus
			// when it's asked for while it's already open
			const auto pane = v.AllToolsPanel();
			check(pane.Visibility() == Visibility::Visible,
			      "All tools didn't open");
			const auto title = v.FindName(L"ToolPaneTitle").try_as<TextBlock>();
			check(title && title.Text() == L"All tools",
			      "the tool pane's header doesn't name All tools");
			const auto back = v.FindName(L"ToolPaneBack").try_as<Button>();
			check(back && back.Visibility() == Visibility::Collapsed,
			      "the launcher has no hidden back arrow for tool panes");
			const auto close = v.FindName(L"ToolPaneClose").try_as<Button>();
			check(close && Automation::AutomationProperties::GetName(close) ==
			                   L"Close tool pane",
			      "the tool pane has no Close tool pane button");
			findings += auditSurface(pane, L"Tool pane", true);
			// All tools lists Document properties and no longer lists Find
			bool listsFind = false, listsProperties = false;
			for (const auto& child : v.AllToolsList().Children()) {
				if (const auto button = child.try_as<Button>()) {
					const auto name =
					    Automation::AutomationProperties::GetName(button);
					listsFind = listsFind || name == L"Find";
					listsProperties =
					    listsProperties || name == L"Document properties";
				}
			}
			check(!listsFind && listsProperties,
			      "All tools should list Document properties, not Find");
			v.Scroller().Focus(FocusState::Programmatic);
			v.setAllToolsOpen(true);
			const auto inside = [&](const DependencyObject& node,
			                        const DependencyObject& root) {
				for (auto at = node; at; at = VisualTreeHelper::GetParent(at)) {
					if (at == root) {
						return true;
					}
				}
				return false;
			};
			const auto focused =
			    Input::FocusManager::GetFocusedElement(current.XamlRoot())
			        .try_as<DependencyObject>();
			check(focused && inside(focused, pane),
			      "opening the pane that's open didn't focus it");
			if (close) {
				Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
				    close)
				    .GetPattern(Peers::PatternInterface::Invoke)
				    .as<Provider::IInvokeProvider>()
				    .Invoke();
			}
			break;
		}
		case 11: {
			const auto checked = v.AllToolsButton().IsChecked();
			check(v.AllToolsPanel().Visibility() == Visibility::Collapsed &&
			          checked && !checked.Value(),
			      "Close tool pane didn't close the pane");
			const auto focused =
			    Input::FocusManager::GetFocusedElement(current.XamlRoot())
			        .try_as<DependencyObject>();
			bool onPages = false;
			for (auto at = focused; at; at = VisualTreeHelper::GetParent(at)) {
				onPages = onPages || at == v.Scroller();
			}
			check(onPages, "closing the pane didn't return focus to the pages");
			break;
		}
		case 12:
			// Document properties opens in the pane, from the All tools entry
			v.runTool(L"properties");
			break;
		case 13: {
			const auto title = v.FindName(L"ToolPaneTitle").try_as<TextBlock>();
			check(title && title.Text() == L"Document properties",
			      "the properties pane's header doesn't name it");
			const auto host =
			    v.FindName(L"PropertiesHost").try_as<StackPanel>();
			if (!host || !v.m_properties || v.m_propertiesPending) {
				check(host && ticks < 60, "properties didn't load in the pane");
				return;
			}
			const auto back = v.FindName(L"ToolPaneBack").try_as<Button>();
			check(back && back.Visibility() == Visibility::Visible,
			      "the properties pane has no back arrow to All tools");
			// a value cell used to be named after its label, so the value was
			// never announced: a screen reader gets the value, with its label,
			// and the label takes its colour from the theme
			const auto rows = host.Children().GetAt(0).as<Grid>();
			const auto style =
			    host.Resources().Lookup(box_value(L"PropertyLabelStyle"));
			check(rows.Children().Size() >= 4, "properties show no rows");
			for (uint32_t i = 0; i + 1 < rows.Children().Size(); i += 2) {
				const auto label = rows.Children().GetAt(i).as<TextBlock>();
				const auto value = rows.Children().GetAt(i + 1).as<TextBlock>();
				check(Automation::AutomationProperties::GetName(value) ==
				          label.Text() + L": " + value.Text(),
				      "a property value isn't named with its value");
				check(label.Style() ==
				          style.as<winrt::Microsoft::UI::Xaml::Style>(),
				      "a property label doesn't use the theme colour");
			}
			const auto focused =
			    Input::FocusManager::GetFocusedElement(current.XamlRoot())
			        .try_as<DependencyObject>();
			bool inPane = false;
			for (auto at = focused; at; at = VisualTreeHelper::GetParent(at)) {
				inPane = inPane || at == v.AllToolsPanel();
			}
			check(inPane, "opening properties didn't move focus into the pane");
			// (the walk below moves focus)
			findings +=
			    auditSurface(v.AllToolsPanel(), L"Properties pane", true);
			v.closePane();
			break;
		}
		case 14: {
			check(findings == 0, std::to_string(findings) +
			                         " accessibility findings; see "
			                         "Sable-probe-logs\\accessibility.txt");
			pagePeer =
			    Peers::FrameworkElementAutomationPeer::CreatePeerForElement(
			        v.PageReader());
			const auto text = pagePeer.GetPattern(Peers::PatternInterface::Text)
			                      .as<Provider::ITextProvider>();
			const auto beforeName = pagePeer.GetName();
			const auto beforeText = text.DocumentRange().GetText(-1);
			const auto gate = std::make_shared<winrt::handle>(
			    CreateEventW(nullptr, TRUE, FALSE, nullptr));
			check(static_cast<bool>(*gate));
			check(engine.post(
			          [gate] { WaitForSingleObject(gate->get(), 10000); }),
			      "could not delay the page text");
			v.goToPage(4);
			const bool paired = pagePeer.GetName() == beforeName &&
			                    text.DocumentRange().GetText(-1) == beforeText;
			SetEvent(gate->get());
			check(paired, "page name changed before its delayed text");
			break;
		}
		case 15: {
			const auto text = pagePeer.GetPattern(Peers::PatternInterface::Text)
			                      .as<Provider::ITextProvider>();
			if (pagePeer.GetName() != L"Page 5 of 5") {
				check(ticks < 60, "delayed name/text never arrived together");
				return;
			}
			const auto range = text.DocumentRange();
			check(range.GetText(-1) == L"\u03a9\U0001f600\u00e9",
			      "new page name came with another page's text");
			const auto session = v.m_session.get();
			const auto stale = v.m_pageTextVersion;
			v.m_pageTextPage.reset();
			v.updatePageText();
			const auto version = v.m_pageTextVersion;
			v.applyPageText(stale, session, 4,
			                {"stale same-page reply", false});
			v.applyPageText(version, nullptr, 4, {"stale session", false});
			v.applyPageText(version, session, 0, {"stale page", false});
			check(pagePeer.GetName() == L"Page 5 of 5" &&
			          text.DocumentRange().GetText(-1) == range.GetText(-1),
			      "a stale text reply replaced the page snapshot");
			v.applyPageText(version, session, 4, {std::nullopt, true});
			check(!v.m_pageTextPage &&
			          text.DocumentRange().GetText(-1) == range.GetText(-1),
			      "canceled text reply published a snapshot");
			v.updatePageText();
			const auto finalVersion = v.m_pageTextVersion;
			v.applyPageText(finalVersion, session, 4, {std::nullopt, false});
			check(pagePeer.GetName() == L"Page 5 of 5" &&
			          text.DocumentRange().GetText(-1).empty() &&
			          range.GetText(-1) == L"\u03a9\U0001f600\u00e9",
			      "unavailable text or a retained range changed its snapshot");
			v.close();
			v.applyPageText(finalVersion, session, 4, {"closed reply", false});
			check(text.DocumentRange().GetText(-1).empty(),
			      "closed page accepted late text");
			pagePeer = nullptr;
			status.Text(L"PASS: names and keyboard reach on every surface");
			probePass(owner.get_strong().as<Window>());
			timer.Stop();
			return;
		}
		}
	}
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
