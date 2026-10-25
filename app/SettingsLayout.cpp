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
#include "SettingsWindow.h"
#include "helpers/version.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Text.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {
Media::SolidColorBrush brush(unsigned rgb) {
	return Media::SolidColorBrush({255, static_cast<uint8_t>(rgb >> 16),
	                               static_cast<uint8_t>(rgb >> 8),
	                               static_cast<uint8_t>(rgb)});
}
Style style(const wchar_t* key) {
	return Application::Current()
	    .Resources()
	    .Lookup(box_value(key))
	    .as<Style>();
}
TextBlock label(const hstring& value, double size = 13,
                unsigned color = 0xeeeeef) {
	TextBlock text;
	text.Text(value);
	text.FontSize(size);
	text.Foreground(brush(color));
	text.TextWrapping(TextWrapping::Wrap);
	return text;
}
FontIcon glyph(const wchar_t* value, double size = 16) {
	FontIcon icon;
	icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
	icon.Glyph(value);
	icon.FontSize(size);
	return icon;
}
Border card(const UIElement& child) {
	Border frame;
	frame.Background(appearance::surface(0x242426));
	frame.BorderBrush(brush(0x343437));
	frame.BorderThickness({1, 1, 1, 1});
	frame.CornerRadius({8, 8, 8, 8});
	frame.Padding({20, 18, 20, 18});
	frame.Child(child);
	return frame;
}
Grid bindingColumns() {
	Grid row;
	row.ColumnSpacing(12);
	for (int i = 0; i < 3; ++i) {
		row.ColumnDefinitions().Append(ColumnDefinition());
	}
	return row;
}
} // namespace

Grid SettingsWindow::buildLayout() {
	const auto weak = weak_from_this();
	Grid root;
	root.Background(appearance::surface(0x1e1e20));
	ColumnDefinition left;
	left.Width({176, GridUnitType::Pixel});
	root.ColumnDefinitions().Append(left);
	root.ColumnDefinitions().Append(ColumnDefinition());
	Grid navigation;
	navigation.Background(appearance::surface(0x19191b));
	navigation.Padding({14, 24, 14, 20});
	RowDefinition heading;
	heading.Height({1, GridUnitType::Auto});
	navigation.RowDefinitions().Append(heading);
	navigation.RowDefinitions().Append(RowDefinition());
	RowDefinition footer;
	footer.Height({1, GridUnitType::Auto});
	navigation.RowDefinitions().Append(footer);
	auto title = label(L"Settings", 22);
	title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	title.Margin({12, 0, 0, 28});
	navigation.Children().Append(title);
	StackPanel menu;
	menu.Spacing(4);
	Grid::SetRow(menu, 1);
	struct Section {
		const char* key;
		const wchar_t* name;
		const wchar_t* icon;
	};
	for (const auto& section : {Section{"general", L"General", L"\xE713"},
	                            Section{"keybinds", L"Shortcuts", L"\xE765"},
	                            Section{"version", L"About", L"\xE946"}}) {
		Button item;
		item.Style(style(L"SableSettingsNavStyle"));
		item.HorizontalAlignment(HorizontalAlignment::Stretch);
		item.Tag(box_value(to_hstring(section.key)));
		StackPanel caption;
		caption.Orientation(Orientation::Horizontal);
		caption.Spacing(12);
		caption.Children().Append(glyph(section.icon));
		TextBlock name;
		name.Text(section.name);
		name.FontSize(13);
		caption.Children().Append(name);
		item.Content(caption);
		Automation::AutomationProperties::SetName(item, section.name);
		item.Click([weak, key = std::string(section.key)](auto&&, auto&&) {
			if (auto self = weak.lock()) {
				self->select(key);
			}
		});
		menu.Children().Append(item);
		m_sections.push_back(item);
	}
	navigation.Children().Append(menu);
	auto brand = label(L"Sable", 12, 0x8e8e96);
	brand.Margin({12, 20, 0, 0});
	Grid::SetRow(brand, 2);
	navigation.Children().Append(brand);
	root.Children().Append(navigation);

	Grid content;
	content.Margin({28, 27, 28, 24});
	Grid::SetColumn(content, 1);
	RowDefinition header;
	header.Height({1, GridUnitType::Auto});
	content.RowDefinitions().Append(header);
	content.RowDefinitions().Append(RowDefinition());
	RowDefinition error;
	error.Height({1, GridUnitType::Auto});
	content.RowDefinitions().Append(error);
	StackPanel intro;
	intro.Spacing(7);
	intro.Margin({0, 0, 0, 26});
	m_heading.FontSize(26);
	m_heading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	m_heading.Foreground(brush(0xf1f1f3));
	m_description.FontSize(12);
	m_description.Foreground(brush(0xa7a7ae));
	m_description.TextWrapping(TextWrapping::Wrap);
	intro.Children().Append(m_heading);
	intro.Children().Append(m_description);
	content.Children().Append(intro);
	Grid page;
	Grid::SetRow(page, 1);
	Grid details;
	m_general.Spacing(12);
	const char* keys[] = {"MaximizeOnOpen", "DisplayThumbs"};
	const wchar_t* names[] = {L"Maximize on open", L"Show page thumbnails"};
	const wchar_t* descriptions[] = {L"Open documents in a maximized window.",
	                                 L"Start with the thumbnail panel open."};
	for (std::size_t i = 0; i < m_checks.size(); ++i) {
		Grid row;
		row.ColumnSpacing(16);
		row.ColumnDefinitions().Append(ColumnDefinition());
		ColumnDefinition control;
		control.Width({1, GridUnitType::Auto});
		row.ColumnDefinitions().Append(control);
		StackPanel text;
		text.Spacing(5);
		auto name = label(names[i], 14);
		name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
		text.Children().Append(name);
		text.Children().Append(label(descriptions[i], 12, 0xa7a7ae));
		row.Children().Append(text);
		auto box = m_checks[i];
		box.MinWidth(0);
		box.Width(32);
		box.VerticalAlignment(VerticalAlignment::Center);
		box.Tag(box_value(to_hstring(keys[i])));
		box.IsChecked(i == 0 ? m_store.get().general.MaximizeOnOpen
		                     : m_store.get().general.DisplayThumbs);
		Automation::AutomationProperties::SetName(box, names[i]);
		Grid::SetColumn(box, 1);
		box.Click([weak](const auto& sender, auto&&) {
			if (auto self = weak.lock()) {
				self->save(sender.template as<CheckBox>());
			}
		});
		row.Children().Append(box);
		m_general.Children().Append(card(row));
	}
	StackPanel themeCard;
	themeCard.Spacing(8);
	auto themeTitle = label(L"Appearance", 14);
	themeTitle.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	themeCard.Children().Append(themeTitle);
	themeCard.Children().Append(label(
	    L"Obsidian brings pure black to every app surface.", 12, 0xa7a7ae));
	m_theme.Items().Append(box_value(L"Dark"));
	m_theme.Items().Append(box_value(L"Obsidian (AMOLED)"));
	m_theme.SelectedIndex(m_store.get().general.Theme == "obsidian" ? 1 : 0);
	m_theme.HorizontalAlignment(HorizontalAlignment::Stretch);
	m_theme.Margin({0, 6, 0, 0});
	Automation::AutomationProperties::SetName(m_theme, L"App theme");
	m_theme.SelectionChanged([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->saveTheme();
		}
	});
	themeCard.Children().Append(m_theme);
	m_general.Children().InsertAt(0, card(themeCard));
	details.Children().Append(m_general);

	m_about.Spacing(24);
	StackPanel identity;
	identity.Spacing(8);
	auto appName = label(L"Sable", 30);
	appName.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	identity.Children().Append(appName);
	identity.Children().Append(label(to_hstring(SABLE_VERSION), 14, 0xc2c2ca));
	identity.Children().Append(
	    label(L"A native PDF reader for Windows.", 13, 0xa7a7ae));
	identity.Margin({0, 10, 0, 4});
	m_about.Children().Append(identity);
	m_version.Text(to_hstring(helpers::versionString()));
	m_version.FontSize(12);
	m_version.LineHeight(22);
	m_version.Foreground(brush(0xb3b3bb));
	m_version.TextWrapping(TextWrapping::Wrap);
	m_version.IsTextSelectionEnabled(true);
	m_version.Margin({18, 0, 18, 18});
	m_buildDetails.Header(box_value(L"Build details"));
	m_buildDetails.Content(m_version);
	m_buildDetails.HorizontalAlignment(HorizontalAlignment::Stretch);
	m_buildDetails.HorizontalContentAlignment(HorizontalAlignment::Stretch);
	m_about.Children().Append(m_buildDetails);
	details.Children().Append(m_about);
	m_detailScroll.Content(details);
	m_detailScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
	m_detailScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
	m_detailScroll.Padding({0, 0, 12, 0});
	page.Children().Append(m_detailScroll);

	RowDefinition columns;
	columns.Height({1, GridUnitType::Auto});
	m_shortcuts.RowDefinitions().Append(columns);
	m_shortcuts.RowDefinitions().Append(RowDefinition());
	auto labels = bindingColumns();
	labels.Margin({0, 0, 12, 10});
	const wchar_t* headings[] = {L"Action", L"Primary", L"Alternative"};
	for (int i = 0; i < 3; ++i) {
		auto text = label(headings[i], 11, 0x9b9ba4);
		Grid::SetColumn(text, i);
		labels.Children().Append(text);
	}
	m_shortcuts.Children().Append(labels);
	m_bindingScroll.Content(m_keybinds);
	m_bindingScroll.HorizontalScrollBarVisibility(
	    ScrollBarVisibility::Disabled);
	m_bindingScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
	m_bindingScroll.Padding({0, 0, 12, 0});
	Grid::SetRow(m_bindingScroll, 1);
	m_shortcuts.Children().Append(m_bindingScroll);
	page.Children().Append(m_shortcuts);
	buildKeybinds();
	content.Children().Append(page);
	Grid::SetRow(m_error, 2);
	m_error.Margin({0, 16, 0, 0});
	content.Children().Append(m_error);
	root.Children().Append(content);
	Grid::SetColumnSpan(m_overlay, 2);
	m_overlay.Background(Media::SolidColorBrush({170, 0, 0, 0}));
	m_overlay.Visibility(Visibility::Collapsed);
	buildCaptureCard();
	return root;
}

void SettingsWindow::buildCaptureCard() {
	const auto weak = weak_from_this();
	StackPanel capture;
	capture.Spacing(16);
	auto title = label(L"Change shortcut", 22);
	title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	capture.Children().Append(title);
	capture.Children().Append(
	    label(L"Hold a modifier and press a key.", 13, 0xa7a7ae));
	m_captureText.FontSize(20);
	m_captureText.Foreground(brush(0xf1f1f3));
	m_captureText.TextWrapping(TextWrapping::Wrap);
	m_captureText.MinHeight(40);
	m_captureText.MaxWidth(320);
	capture.Children().Append(m_captureText);
	StackPanel buttons;
	buttons.Orientation(Orientation::Horizontal);
	buttons.HorizontalAlignment(HorizontalAlignment::Right);
	buttons.Spacing(10);
	m_cancelBinding.Content(box_value(L"Cancel"));
	m_cancelBinding.Style(style(L"SableSettingsButtonStyle"));
	m_cancelBinding.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->dismiss();
		}
	});
	m_saveBinding.Content(box_value(L"Save"));
	m_saveBinding.Style(style(L"SableSettingsPrimaryStyle"));
	m_saveBinding.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			if (self->m_capture.binding().key &&
			    self->saveBinding(self->m_action, self->m_bindingIndex,
			                      self->m_capture.binding())) {
				self->dismiss();
			}
		}
	});
	buttons.Children().Append(m_cancelBinding);
	buttons.Children().Append(m_saveBinding);
	capture.Children().Append(buttons);
	auto frame = card(capture);
	frame.Width(380);
	frame.Padding({26, 24, 26, 24});
	frame.HorizontalAlignment(HorizontalAlignment::Center);
	frame.VerticalAlignment(VerticalAlignment::Center);
	frame.PointerPressed([](auto&&, const Input::PointerRoutedEventArgs& args) {
		args.Handled(true);
	});
	m_overlay.Child(frame);
}

// port of app/render/settings.ts createKeybindAction/Setting() (L375-515)
void SettingsWindow::buildKeybinds() {
	m_keybinds.Children().Clear();
	helpers::KeybindsHelper bindings(m_store.get().keybinds, "win32");
	const auto weak = weak_from_this();
	for (const auto& action : bindings.actions) {
		auto row = bindingColumns();
		row.MinHeight(44);
		row.Padding({0, 4, 0, 4});
		row.BorderBrush(brush(0x303034));
		row.BorderThickness({0, 0, 0, 1});
		const auto name = to_hstring(bindings.getActionDisplayName(action));
		auto text = label(name);
		text.VerticalAlignment(VerticalAlignment::Center);
		row.Children().Append(text);
		const auto keys = bindings.getActionKeybindsString(action);
		for (std::size_t i = 0; i < 2; ++i) {
			Grid slot;
			slot.ColumnSpacing(4);
			slot.ColumnDefinitions().Append(ColumnDefinition());
			ColumnDefinition remove;
			remove.Width({28, GridUnitType::Pixel});
			slot.ColumnDefinitions().Append(remove);
			Grid::SetColumn(slot, static_cast<int>(i + 1));
			const bool assigned = i < keys.size();
			const auto chord = assigned ? to_hstring(keys[i]) : L"Not set";
			Button binding;
			binding.Style(style(L"SableSettingsShortcutStyle"));
			binding.HorizontalAlignment(HorizontalAlignment::Stretch);
			auto caption = label(chord, 12, assigned ? 0xe6e6ea : 0x9b9ba4);
			caption.TextWrapping(TextWrapping::NoWrap);
			caption.TextTrimming(TextTrimming::CharacterEllipsis);
			binding.Content(caption);
			ToolTipService::SetToolTip(binding, box_value(chord));
			Automation::AutomationProperties::SetName(
			    binding,
			    name + (i == 0 ? L" primary binding" : L" secondary binding"));
			if (action == m_action && i == m_bindingIndex) {
				m_returnFocus = binding;
			}
			binding.Click([weak, action, i](const auto& sender, auto&&) {
				if (auto self = weak.lock()) {
					self->m_returnFocus = sender.template as<Button>();
					self->edit(action, i);
				}
			});
			slot.Children().Append(binding);
			Button clear;
			clear.Style(style(L"SableSettingsClearStyle"));
			clear.Content(glyph(L"\xE711", 10));
			clear.IsEnabled(assigned);
			clear.Opacity(assigned ? .65 : 0);
			Grid::SetColumn(clear, 1);
			const auto description =
			    L"Clear " + name +
			    (i == 0 ? L" primary binding" : L" secondary binding");
			Automation::AutomationProperties::SetName(clear, description);
			ToolTipService::SetToolTip(clear, box_value(description));
			clear.Click([weak, action, i](auto&&, auto&&) {
				if (auto self = weak.lock()) {
					self->saveBinding(action, i, std::nullopt);
				}
			});
			slot.Children().Append(clear);
			row.Children().Append(slot);
		}
		m_keybinds.Children().Append(row);
	}
}
