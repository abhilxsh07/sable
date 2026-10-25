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
#include "Appearance.h"
#include "HomePage.xaml.h"
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

#include <ShlObj.h>
#include <ctime>
#include <cwctype>
#include <string_view>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include "helpers/filter.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

// inside HomePage these names are also properties of the page, so the enums
// get their own names
using XVisibility = Microsoft::UI::Xaml::Visibility;
using XVerticalAlignment = Microsoft::UI::Xaml::VerticalAlignment;
using XHorizontalAlignment = Microsoft::UI::Xaml::HorizontalAlignment;

namespace {
namespace chr = std::chrono;
namespace fs = std::filesystem;

// below this the location and size columns go, the name needs the room
constexpr double COMPACT_WIDTH = 960;

#ifndef SABLE_SURFACE_TEST
// %APPDATA%\Sable\recent.json, next to config.json
fs::path recentsFile() {
	PWSTR raw = nullptr;
	const HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData,
	                                        KF_FLAG_DEFAULT, nullptr, &raw);
	fs::path dir;
	if (SUCCEEDED(hr)) {
		dir = raw;
	}
	CoTaskMemFree(raw);
	return dir.empty() ? fs::path{} : dir / L"Sable" / L"recent.json";
}
#endif

// how far local time is from UTC right now, DST included
chr::minutes utcOffset() {
	const std::time_t now = std::time(nullptr);
	std::tm local{};
	if (localtime_s(&local, &now) != 0) {
		return chr::minutes{0};
	}
	// _mkgmtime reads the local fields as if they were UTC
	const std::time_t shifted = _mkgmtime(&local);
	if (shifted == -1) {
		return chr::minutes{0};
	}
	return chr::duration_cast<chr::minutes>(chr::seconds{shifted - now});
}

std::wstring lowered(std::wstring_view text) {
	std::wstring out(text);
	for (wchar_t& ch : out) {
		ch = static_cast<wchar_t>(std::towlower(ch));
	}
	return out;
}

Windows::UI::Color rgb(std::uint32_t hex) {
	return {255, static_cast<std::uint8_t>(hex >> 16),
	        static_cast<std::uint8_t>(hex >> 8),
	        static_cast<std::uint8_t>(hex)};
}

SolidColorBrush brush(std::uint32_t hex) {
	return SolidColorBrush(rgb(hex));
}

void setColour(const ResourceDictionary& resources, const wchar_t* key,
               helpers::Rgb8 colour) {
	auto found = resources.TryLookup(box_value(hstring{key}));
	if (auto solid = found.try_as<SolidColorBrush>()) {
		solid.Color({255, colour.r, colour.g, colour.b});
	}
}

TextBlock cell(const hstring& text, int column) {
	TextBlock block;
	block.Text(text);
	block.FontSize(13);
	block.Foreground(brush(0xa6a6a6));
	block.VerticalAlignment(XVerticalAlignment::Center);
	block.TextTrimming(TextTrimming::CharacterEllipsis);
	block.Margin({0, 0, 16, 0});
	Grid::SetColumn(block, column);
	return block;
}

// same columns for the header and every row
void setColumns(const Grid& grid, bool compact) {
	auto columns = grid.ColumnDefinitions();
	columns.Clear();
	auto add = [&columns](double width, GridUnitType unit) {
		ColumnDefinition column;
		column.Width({width, unit});
		columns.Append(column);
	};
	add(52, GridUnitType::Pixel);
	add(1, GridUnitType::Star);
	add(compact ? 0 : 260, GridUnitType::Pixel); // location
	add(compact ? 150 : 190, GridUnitType::Pixel);
	add(compact ? 0 : 110, GridUnitType::Pixel); // size
	add(48, GridUnitType::Pixel);
}
} // namespace

namespace winrt::Sable::implementation {
// C++/WinRT calls this after the constructor, the named elements exist here
void HomePage::InitializeComponent() {
	HomePageT::InitializeComponent();

#ifndef SABLE_SURFACE_TEST
	// the probes open lots of files; they keep Recent in memory instead
	m_recentsFile = recentsFile();
	if (!m_recentsFile.empty()) {
		m_recents = helpers::loadRecents(m_recentsFile);
	}
#endif
	SizeChanged({this, &HomePage::onSizeChanged});
	// a row is its own Grid inside the list's container, and that Grid is
	// what ItemClick hands over; the star button keeps its clicks to itself
	RecentList().ItemClick(
	    [weak = get_weak()](auto&&, const ItemClickEventArgs& args) {
		    const auto row = args.ClickedItem().try_as<FrameworkElement>();
		    const auto path =
		        row ? unbox_value_or<hstring>(row.Tag(), {}) : hstring{};
		    if (auto self = weak.get();
		        self && !path.empty() && self->m_openFileRequested) {
			    self->m_openFileRequested(fs::path{std::wstring_view{path}});
		    }
	    });
	// Default is the active preset when NightPDF starts
	showPreset("default");
	rebuildList();
}

void HomePage::Remember(const std::filesystem::path& path) {
	// another window may have changed the list since this one read it
	if (!m_recentsFile.empty()) {
		m_recents = helpers::loadRecents(m_recentsFile);
	}
	helpers::RecentFile file;
	file.path = path;
	file.openedAt = chr::floor<chr::seconds>(chr::system_clock::now());
	std::error_code ec;
	const auto size = fs::file_size(path, ec);
	file.sizeBytes = ec ? 0 : size;
	m_recents.add(std::move(file));
	save();
	rebuildList();
}

void HomePage::onOpen(const Windows::Foundation::IInspectable&,
                      const RoutedEventArgs&) {
	if (m_openRequested) {
		m_openRequested();
	}
}

void HomePage::onOpenTapped(const Windows::Foundation::IInspectable& sender,
                            const Input::TappedRoutedEventArgs& args) {
	args.Handled(true);
	onOpen(sender, args);
}

void HomePage::ShowStarredOnly(bool starredOnly) {
	if (m_starredOnly == starredOnly) {
		return;
	}
	m_starredOnly = starredOnly;
	rebuildList();
}

void HomePage::onSizeChanged(const Windows::Foundation::IInspectable&,
                             const SizeChangedEventArgs& args) {
	const bool compact = args.NewSize().Width < COMPACT_WIDTH;
	if (compact != m_compact) {
		m_compact = compact;
		rebuildList();
	}
}

void HomePage::onPresetClick(const Windows::Foundation::IInspectable& sender,
                             const RoutedEventArgs&) {
	if (auto button = sender.try_as<Button>()) {
		showPreset(
		    to_string(unbox_value_or<hstring>(button.Tag(), L"default")));
	}
}

// Puts the sample page through the actual filter, so what you see is what a
// real page will get. Custom is "original" (no filter) the first time it's
// picked in NightPDF, so that's what it shows here.
void HomePage::showPreset(const std::string& preset) {
	m_preset = preset;
	helpers::handlePresetChange(preset == "custom" ? "original" : preset,
	                            m_values);

	const auto resources = Resources();
	setColour(resources, L"PreviewPaperBrush",
	          helpers::applyFilter(m_values, {255, 255, 255}));
	setColour(resources, L"PreviewInkBrush",
	          helpers::applyFilter(m_values, {32, 32, 32}));
	setColour(resources, L"PreviewMutedInkBrush",
	          helpers::applyFilter(m_values, {138, 138, 138}));
	setColour(resources, L"PreviewSkyBrush",
	          helpers::applyFilter(m_values, {90, 154, 214}));
	setColour(resources, L"PreviewHillBrush",
	          helpers::applyFilter(m_values, {79, 138, 70}));

	// the active one gets the hover border, like .button.active
	for (const Button& chip :
	     {DefaultPreset(), SepiaPreset(), RedeyePreset(), CustomPreset()}) {
		const auto tag = unbox_value_or<hstring>(chip.Tag(), hstring{});
		chip.BorderBrush(brush(to_string(tag) == preset ? 0x797979 : 0x1e1e1e));
	}

	// "Darkness 7% · Grayscale 95% · ..." in the slider order
	const double values[] = {m_values.brightness, m_values.grayness,
	                         m_values.inversion,  m_values.sepia,
	                         m_values.hue,        m_values.extraBrightness};
	std::string details;
	for (std::size_t i = 0; i < helpers::SLIDERS.size(); ++i) {
		if (!details.empty()) {
			details += "  \xC2\xB7  ";
		}
		details += std::string(helpers::SLIDERS[i].label) + " " +
		           helpers::sliderTooltip(helpers::SLIDERS[i], values[i]);
	}
	PresetDetails().Text(to_hstring(details));
}

void HomePage::onSearchChanged(const AutoSuggestBox& sender,
                               const AutoSuggestBoxTextChangedEventArgs&) {
	m_query = lowered(sender.Text());
	rebuildList();
}

// clears the list but keeps what's starred, that's what the star is for
void HomePage::onClearRecent(const Windows::Foundation::IInspectable&,
                             const RoutedEventArgs&) {
	const auto keep = m_recents.starred();
	m_recents.clear();
	// add() puts things on top, so oldest first
	for (auto it = keep.rbegin(); it != keep.rend(); ++it) {
		m_recents.add(*it);
	}
	save();
	rebuildList();
}

void HomePage::toggleStar(const fs::path& path) {
	m_recents.toggleStar(path);
	save();
	rebuildList();
}

void HomePage::save() {
	if (!m_recentsFile.empty()) {
		helpers::saveRecents(m_recentsFile, m_recents);
	}
}

void HomePage::rebuildList() {
	setColumns(ListHeader(), m_compact);

	auto items = RecentList().Items();
	items.Clear();
	const auto now = chr::floor<chr::seconds>(chr::system_clock::now());
	const auto offset = utcOffset();
	bool any = false;
	bool anyBeforeSearch = false;
	for (const helpers::RecentFile& file : m_recents.items()) {
		if (m_starredOnly && !file.starred) {
			continue;
		}
		anyBeforeSearch = true;
		if (!m_query.empty()) {
			const std::wstring name{to_hstring(file.name)};
			const std::wstring folder{
			    to_hstring(helpers::folderLabel(file.path))};
			if (lowered(name).find(m_query) == std::wstring::npos &&
			    lowered(folder).find(m_query) == std::wstring::npos) {
				continue;
			}
		}
		items.Append(makeRow(file, now, offset));
		any = true;
	}

	ListTitle().Text(m_starredOnly ? L"Starred" : L"Recent");
	SearchBox().PlaceholderText(m_starredOnly ? L"Search starred files"
	                                          : L"Search recent files");
	RecentList().Visibility(any ? XVisibility::Visible
	                            : XVisibility::Collapsed);
	EmptyState().Visibility(any ? XVisibility::Collapsed
	                            : XVisibility::Visible);
	ClearButton().Visibility(!m_starredOnly && !m_recents.empty()
	                             ? XVisibility::Visible
	                             : XVisibility::Collapsed);
	if (any) {
		return;
	}
	if (anyBeforeSearch) {
		EmptyIcon().Glyph(L"");
		EmptyTitle().Text(L"Nothing matches that");
		EmptyText().Text(L"Only file names and folder names are searched.");
	} else if (m_starredOnly) {
		EmptyIcon().Glyph(L"");
		EmptyTitle().Text(L"Nothing starred yet");
		EmptyText().Text(L"Star a file in Recent and it shows up here. Starred "
		                 L"files also survive Clear recent.");
	} else {
		EmptyIcon().Glyph(L"");
		EmptyTitle().Text(L"No recent files yet");
		EmptyText().Text(L"PDFs you open show up here, newest first. Star the "
		                 L"ones you keep coming back to.");
	}
}

// one row: page icon, name, folder, when, size, star
Grid HomePage::makeRow(const helpers::RecentFile& file, chr::sys_seconds now,
                       chr::minutes offset) {
	Grid row;
	row.MinHeight(60);
	// what a click on the row opens
	row.Tag(box_value(hstring{file.path.wstring()}));
	// otherwise Narrator just says "Grid"
	Automation::AutomationProperties::SetName(row, to_hstring(file.name));
	setColumns(row, m_compact);

	Border page;
	page.Width(30);
	page.Height(38);
	page.CornerRadius({3, 3, 3, 3});
	page.Background(appearance::surface(0x2d2d2d));
	page.BorderBrush(brush(0x3d3d3d));
	page.BorderThickness({1, 1, 1, 1});
	page.HorizontalAlignment(XHorizontalAlignment::Left);
	TextBlock badge;
	badge.Text(L"PDF");
	badge.FontSize(9);
	badge.FontWeight(Microsoft::UI::Text::FontWeights::Bold());
	badge.Foreground(brush(0xfc515d));
	badge.HorizontalAlignment(XHorizontalAlignment::Center);
	badge.VerticalAlignment(XVerticalAlignment::Bottom);
	badge.Margin({0, 0, 0, 5});
	page.Child(badge);
	row.Children().Append(page);

	StackPanel title;
	title.VerticalAlignment(XVerticalAlignment::Center);
	title.Margin({0, 0, 16, 0});
	Grid::SetColumn(title, 1);
	TextBlock name;
	name.Text(to_hstring(file.name));
	name.FontSize(14);
	name.Foreground(brush(0xe8e8e8));
	name.TextTrimming(TextTrimming::CharacterEllipsis);
	TextBlock kind;
	kind.Text(L"PDF");
	kind.FontSize(11);
	kind.Foreground(brush(0x7a7a7a));
	title.Children().Append(name);
	title.Children().Append(kind);
	row.Children().Append(title);

	auto location = cell(to_hstring(helpers::folderLabel(file.path)), 2);
	ToolTipService::SetToolTip(
	    location, box_value(hstring{file.path.parent_path().wstring()}));
	row.Children().Append(location);
	row.Children().Append(
	    cell(to_hstring(helpers::openedLabel(file.openedAt, now, offset)), 3));
	row.Children().Append(
	    cell(to_hstring(helpers::humanSize(file.sizeBytes)), 4));

	Button star;
	if (auto style = Application::Current()
	                     .Resources()
	                     .TryLookup(box_value(L"SableFlatButtonStyle"))
	                     .try_as<Microsoft::UI::Xaml::Style>()) {
		star.Style(style);
	}
	star.Padding({8, 8, 8, 8});
	star.HorizontalAlignment(XHorizontalAlignment::Right);
	FontIcon icon;
	icon.Glyph(file.starred ? L"" : L"");
	icon.FontSize(15);
	icon.Foreground(brush(file.starred ? 0xd773e9 : 0x7a7a7a));
	star.Content(icon);
	const hstring label = file.starred ? L"Unstar" : L"Star";
	ToolTipService::SetToolTip(star, box_value(label));
	Automation::AutomationProperties::SetName(star, label);
	star.Click([weak = get_weak(), path = file.path](auto&&, auto&&) {
		if (auto self = weak.get()) {
			self->toggleStar(path);
		}
	});
	Grid::SetColumn(star, 5);
	row.Children().Append(star);
	return row;
}
} // namespace winrt::Sable::implementation
