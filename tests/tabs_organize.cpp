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
#include <fstream>
#include <iterator>

namespace winrt::Sable::implementation {
// pages.pdf: 595 x 842, 792 x 612 and 300 x 200 turned 90 degrees.
void TabsTest::organizeTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_organizeBusy) {
		return;
	}
	const auto invoke = [](const FrameworkElement& element) {
		using namespace Microsoft::UI::Xaml::Automation;
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	const auto select = [&](std::initializer_list<int> pages) {
		v.OrganizeGrid().SelectedItems().Clear();
		for (const int page : pages) {
			v.OrganizeGrid().SelectRange(Data::ItemIndexRange(page, 1));
		}
	};
	const auto sized = [&](std::size_t page, double width, double height) {
		return page < v.m_sizes.size() &&
		       std::abs(v.m_sizes[page].width - width) < .5 &&
		       std::abs(v.m_sizes[page].height - height) < .5;
	};
	switch (step) {
	case 0:
		owner.openTool(L"organize");
		check(v.organizing() && v.OrganizeGrid().Items().Size() == 3,
		      "Organize did not show a tile per page");
		break;
	case 1:
		if (v.m_organizeTile < 3) {
			check(ticks < 100, "Organize tiles did not render");
			return;
		}
		check(v.OrganizeStatus().Text() == L"3 pages" &&
		          !v.OrganizeDelete().IsEnabled(),
		      "Organize state without a selection");
		select({0, 2});
		check(v.OrganizeStatus().Text() == L"3 pages · 2 selected",
		      "Organize did not count the selection");
		invoke(v.OrganizeRotateRight());
		break;
	case 2:
		check(v.m_notesDirty && sized(0, 842, 595) && sized(1, 792, 612) &&
		          sized(2, 300, 200) && v.OrganizeGrid().Items().Size() == 3,
		      "Rotate right did not turn the selected pages");
		{
			// what the grid does after a drag: the last tile goes first.
			auto items = v.OrganizeGrid().Items();
			const auto last = items.GetAt(2);
			items.RemoveAt(2);
			items.InsertAt(0, last);
			v.reorderPages({2});
		}
		break;
	case 3:
		check(sized(0, 300, 200) && sized(1, 842, 595) && sized(2, 792, 612),
		      "dragging the last page first did not move it");
		select({0});
		invoke(v.OrganizeDelete());
		break;
	case 4:
		check(v.m_sizes.size() == 2 && sized(0, 842, 595) &&
		          v.OrganizeGrid().Items().Size() == 2,
		      "Delete did not remove the selected page");
		select({0, 1});
		invoke(v.OrganizeDelete());
		check(v.Message().IsOpen() &&
		          v.Message().Message() == L"A PDF needs at least one page." &&
		          v.m_sizes.size() == 2,
		      "deleting every page was not refused");
		invoke(v.OrganizeDone());
		break;
	case 5:
		check(!v.organizing() && v.m_layout && v.m_layout->pages().size() == 2,
		      "Done did not return to the changed pages");
		v.close();
		status.Text(L"PASS: Organize rotate, move, delete and refusal");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	default:
		check(false, "unexpected organize step");
	}
	++step;
	ticks = 0;
}

// Combine: two fixtures through the dialog into one saved, opened PDF.
void TabsTest::combineTick(MainWindow& owner) {
	using namespace Microsoft::UI::Xaml::Automation;
	const auto pressPrimary = [&](const ContentDialog& dialog) {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& node) -> Button {
			if (const auto button = node.try_as<Button>();
			    button && button.Name() == L"PrimaryButton") {
				return button;
			}
			for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node);
			     ++i) {
				if (const auto button =
				        find(Media::VisualTreeHelper::GetChild(node, i))) {
					return button;
				}
			}
			return nullptr;
		};
		const auto button = find(dialog);
		check(button && button.IsEnabled(), "Combine and save is disabled");
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
	};
	switch (step) {
	case 0:
		// the app hands its window the engine at launch; this probe opens
		// no PDF that would.
		owner.setEngine(engine);
		owner.openTool(L"combine");
		check(owner.m_combining && owner.m_combineDialog,
		      "Combine files did not open its dialog");
		break;
	case 1: {
		const auto dialog = owner.m_combineDialog;
		check(static_cast<bool>(dialog), "the Combine dialog closed early");
		const auto list = dialog.Content()
		                      .as<StackPanel>()
		                      .Children()
		                      .GetAt(1)
		                      .as<ListView>();
		for (const auto* name :
		     {L"tests/fixtures/pages.pdf", L"tests/fixtures/text.pdf"}) {
			TextBlock row;
			row.Text(name);
			row.Tag(
			    box_value(hstring(std::filesystem::absolute(name).wstring())));
			list.Items().Append(row);
		}
		list.SelectedIndex(1);
		notesPath = makeTempDir("combine") / L"combined.pdf";
		owner.m_notesPicker = [path = hstring(notesPath.wstring())](hstring) {
			return attachmentChoice(path, {});
		};
		pressPrimary(dialog);
		break;
	}
	case 2: {
		if (owner.m_combining) {
			check(ticks < 100, "Combine did not finish");
			return;
		}
		check(!owner.PickerError().IsOpen(),
		      "Combine reported: " + to_string(owner.PickerError().Message()));
		check(std::filesystem::exists(notesPath),
		      "the combined PDF was not saved");
		const auto current = owner.Viewer();
		check(current &&
		          get_self<ViewerPage>(current)->m_path ==
		              std::filesystem::absolute(notesPath).lexically_normal(),
		      "the combined PDF was not opened");
		break;
	}
	case 3: {
		const auto current = owner.Viewer();
		auto& v = *get_self<ViewerPage>(current);
		if (!ready(v)) {
			return;
		}
		check(v.m_sizes.size() > 3, "the combined PDF lost pages");
		status.Text(L"PASS: Combine files saved and opened one PDF");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	default:
		check(false, "unexpected combine step");
	}
	++step;
	ticks = 0;
}

// Export: PNG for every page, JPEG for a range, then the text.
void TabsTest::exportTick(MainWindow& owner) {
	using namespace Microsoft::UI::Xaml::Automation;
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || v.m_exporting) {
		return;
	}
	const auto bytes = [](const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file), {});
	};
	// opens the dialog with a format, pages and range; the next tick
	// presses Export once its buttons exist.
	const auto open = [&](int format, const wchar_t* range) {
		v.exportPages();
		const auto dialog = v.m_noteDialog;
		check(dialog && unbox_value<hstring>(dialog.Title()) == L"Export pages",
		      "Export did not open its dialog");
		const auto fields = dialog.Content().as<StackPanel>().Children();
		fields.GetAt(0).as<RadioButtons>().SelectedIndex(format);
		if (range) {
			fields.GetAt(1).as<RadioButtons>().SelectedIndex(1);
			fields.GetAt(2).as<TextBox>().Text(range);
		}
	};
	const auto press = [&] {
		std::function<Button(DependencyObject)> find;
		find = [&](const DependencyObject& node) -> Button {
			if (const auto button = node.try_as<Button>();
			    button && button.Name() == L"PrimaryButton") {
				return button;
			}
			for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(node);
			     ++i) {
				if (const auto button =
				        find(Media::VisualTreeHelper::GetChild(node, i))) {
					return button;
				}
			}
			return nullptr;
		};
		const auto button = v.m_noteDialog ? find(v.m_noteDialog) : nullptr;
		if (!button) {
			check(ticks < 40, "Export had no Export button");
			return false;
		}
		Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button)
		    .GetPattern(Peers::PatternInterface::Invoke)
		    .as<Provider::IInvokeProvider>()
		    .Invoke();
		return true;
	};
	switch (step) {
	case 0:
		notesPath = makeTempDir("export");
		v.m_exportPicker = [folder = notesPath](bool wantsFolder) {
			const auto path = wantsFolder ? folder : folder / L"pages text.txt";
			return attachmentChoice(hstring(path.wstring()), {});
		};
		open(0, nullptr);
		break;
	case 1:
	case 3:
	case 5:
		if (!press()) {
			return;
		}
		break;
	case 2:
		if (v.m_noteDialog) {
			return;
		}
		for (const auto* name :
		     {L"pages - page 001.png", L"pages - page 002.png",
		      L"pages - page 003.png"}) {
			check(bytes(notesPath / name).rfind("\x89PNG", 0) == 0,
			      "a PNG page is missing or isn't a PNG");
		}
		open(1, L"2");
		break;
	case 4:
		if (v.m_noteDialog) {
			return;
		}
		check(bytes(notesPath / L"pages - page 002.jpg").rfind("\xff\xd8", 0) ==
		              0 &&
		          !std::filesystem::exists(notesPath / L"pages - page 001.jpg"),
		      "the JPEG range did not export page 2 alone");
		open(2, nullptr);
		break;
	case 6: {
		if (v.m_noteDialog) {
			return;
		}
		const auto text = bytes(notesPath / L"pages text.txt");
		check(text.find("Sable test page 1") != std::string::npos &&
		          text.find("Sable test page 2") != std::string::npos &&
		          text.find('\f') != std::string::npos,
		      "the text export lost a page");
		v.close();
		status.Text(L"PASS: Export PNG, JPEG range and text");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	default:
		check(false, "unexpected export step");
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
