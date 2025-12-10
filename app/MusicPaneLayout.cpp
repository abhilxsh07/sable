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
#include "MusicPane.h"
#include <algorithm>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace music {
namespace {
void roundedClip(const FrameworkElement& element, float radius) {
	const auto update = [radius](const FrameworkElement& target) {
		const auto visual =
		    Hosting::ElementCompositionPreview::GetElementVisual(target);
		const auto compositor = visual.Compositor();
		const auto geometry = compositor.CreateRoundedRectangleGeometry();
		geometry.CornerRadius({radius, radius});
		geometry.Size({static_cast<float>(target.ActualWidth()),
		               static_cast<float>(target.ActualHeight())});
		visual.Clip(compositor.CreateGeometricClip(geometry));
	};
	element.SizeChanged([update](const auto& sender, auto&&) {
		update(sender.template as<FrameworkElement>());
	});
}
FontIcon icon(const wchar_t* glyph) {
	FontIcon result;
	result.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
	result.Glyph(glyph);
	result.FontSize(18);
	return result;
}
void name(const DependencyObject& object, const wchar_t* text) {
	Automation::AutomationProperties::SetName(object, text);
	ToolTipService::SetToolTip(object, box_value(text));
}
Style style(const wchar_t* key) {
	return Application::Current()
	    .Resources()
	    .Lookup(box_value(key))
	    .as<Style>();
}
Button transport(const wchar_t* glyph, const wchar_t* text) {
	Button result;
	result.Style(style(L"SableMusicButtonStyle"));
	result.Content(icon(glyph));
	name(result, text);
	return result;
}
} // namespace
void Pane::layout() {
	const auto weak = weak_from_this();
	m_button.Visibility(Visibility::Collapsed);
	Grid buttonContent;
	buttonContent.Children().Append(icon(L"\uE8D6"));
	m_playingDot.Width(4);
	m_playingDot.Height(4);
	m_playingDot.CornerRadius({2, 2, 2, 2});
	m_playingDot.Background(Media::SolidColorBrush({255, 41, 192, 234}));
	m_playingDot.HorizontalAlignment(HorizontalAlignment::Right);
	m_playingDot.VerticalAlignment(VerticalAlignment::Bottom);
	m_playingDot.Margin({0, 0, -4, -3});
	m_playingDot.Visibility(Visibility::Collapsed);
	buttonContent.Children().Append(m_playingDot);
	m_button.Content(buttonContent);
	m_button.Click([weak](auto&&, auto&&) {
		if (const auto self = weak.lock(); self && !self->m_closed) {
			self->show();
		}
	});
	m_flyout.ShouldConstrainToRootBounds(true);
	m_flyout.AreOpenCloseAnimationsEnabled(false);
	Style presenter(xaml_typename<FlyoutPresenter>());
	const auto set = [&](auto property, auto value) {
		presenter.Setters().Append(Setter(property, box_value(value)));
	};
	set(Control::BackgroundProperty(), Media::SolidColorBrush({0, 0, 0, 0}));
	set(Control::PaddingProperty(), Thickness{0});
	set(Control::BorderThicknessProperty(), Thickness{0});
	set(Control::CornerRadiusProperty(), CornerRadius{12, 12, 12, 12});
	set(FrameworkElement::MinWidthProperty(), 0.);
	set(FrameworkElement::MaxWidthProperty(), 340.);
	m_flyout.FlyoutPresenterStyle(presenter);
	// see:
	// https://learn.microsoft.com/en-us/windows/apps/develop/ui/in-app-acrylic
	m_background.TintColor({255, 45, 45, 45});
	m_background.FallbackColor({255, 45, 45, 45});
	m_background.TintOpacity(0.62);
	m_background.TintLuminosityOpacity(0.92);
	m_surface.Width(340);
	m_surface.CornerRadius({12, 12, 12, 12});
	m_surface.BorderThickness({1, 1, 1, 1});
	m_surface.BorderBrush(Media::SolidColorBrush({26, 255, 255, 255}));
	m_surface.Background(m_background);
	m_surface.RequestedTheme(ElementTheme::Dark);
	m_surface.RenderTransform(m_slide);
	m_surface.Shadow(Media::ThemeShadow());
	m_surface.Translation({0, 0, 32});
	Grid layers;
	layers.CornerRadius({11, 11, 11, 11});
	roundedClip(layers, 11);
	Media::LinearGradientBrush gradient;
	gradient.StartPoint({0.5f, 0});
	gradient.EndPoint({0.5f, 1});
	m_gradientTop.Color({45, 45, 45, 45});
	m_gradientBottom.Color({0, 45, 45, 45});
	m_gradientBottom.Offset(1);
	gradient.GradientStops().Append(m_gradientTop);
	gradient.GradientStops().Append(m_gradientBottom);
	layers.Background(gradient);
	StackPanel content;
	content.Margin({15, 15, 15, 15});
	content.Spacing(16);
	Grid header;
	ColumnDefinition coverColumn;
	coverColumn.Width({96, GridUnitType::Pixel});
	header.ColumnDefinitions().Append(coverColumn);
	header.ColumnDefinitions().Append(ColumnDefinition());
	header.ColumnSpacing(16);
	Border cover;
	cover.Width(96);
	cover.Height(96);
	cover.CornerRadius({8, 8, 8, 8});
	cover.Background(Media::SolidColorBrush({20, 255, 255, 255}));
	Grid artwork;
	artwork.CornerRadius({8, 8, 8, 8});
	roundedClip(artwork, 8);
	m_art.Stretch(Media::Stretch::UniformToFill);
	m_previousArt.Stretch(Media::Stretch::UniformToFill);
	m_art.IsHitTestVisible(false);
	m_previousArt.IsHitTestVisible(false);
	m_placeholder = icon(L"\uE8D6");
	m_placeholder.FontSize(30);
	m_placeholder.Opacity(0.55);
	artwork.Children().Append(m_placeholder);
	artwork.Children().Append(m_previousArt);
	artwork.Children().Append(m_art);
	cover.Child(artwork);
	header.Children().Append(cover);
	StackPanel info;
	Grid::SetColumn(info, 1);
	info.Spacing(6);
	info.VerticalAlignment(VerticalAlignment::Center);
	m_title.FontSize(16);
	m_title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	m_title.TextWrapping(TextWrapping::NoWrap);
	m_title.TextTrimming(TextTrimming::CharacterEllipsis);
	m_artist.FontSize(14);
	m_artist.Opacity(0.7);
	m_artist.TextTrimming(TextTrimming::CharacterEllipsis);
	m_player.FontSize(12);
	m_player.Opacity(0.55);
	info.Children().Append(m_title);
	info.Children().Append(m_artist);
	info.Children().Append(m_player);
	header.Children().Append(info);
	content.Children().Append(header);
	StackPanel timeline;
	m_seek.Style(style(L"SableMusicSeekStyle"));
	m_seek.Minimum(0);
	m_seek.Maximum(1);
	m_seek.StepFrequency(1);
	m_seek.IsThumbToolTipEnabled(false);
	m_seekFill.Color({255, 185, 185, 185});
	m_seek.Foreground(m_seekFill);
	name(m_seek, L"Playback position");
	timeline.Children().Append(m_seek);
	Grid times;
	for (const auto text : {m_elapsed, m_remaining}) {
		text.FontSize(12);
		text.Opacity(0.7);
		Documents::Typography::SetNumeralAlignment(
		    text, FontNumeralAlignment::Tabular);
	}
	m_remaining.HorizontalAlignment(HorizontalAlignment::Right);
	times.Children().Append(m_elapsed);
	times.Children().Append(m_remaining);
	timeline.Children().Append(times);
	content.Children().Append(timeline);
	StackPanel controls;
	controls.Orientation(Orientation::Horizontal);
	controls.HorizontalAlignment(HorizontalAlignment::Center);
	controls.Spacing(12);
	m_previous = transport(L"\uE892", L"Previous track");
	m_next = transport(L"\uE893", L"Next track");
	m_play.Style(style(L"SableMusicPlayStyle"));
	for (const auto key :
	     {L"ButtonBackgroundPointerOver", L"ButtonBackgroundPressed"}) {
		m_play.Resources().Insert(box_value(key),
		                          Media::SolidColorBrush({255, 220, 220, 220}));
	}
	for (const auto key :
	     {L"ButtonForegroundPointerOver", L"ButtonForegroundPressed"}) {
		m_play.Resources().Insert(box_value(key),
		                          Media::SolidColorBrush({255, 32, 32, 32}));
	}
	m_playIcon = icon(L"\uE768");
	m_playIcon.FontSize(20);
	m_play.Content(m_playIcon);
	m_shuffle.Style(style(L"SableMusicToggleStyle"));
	m_shuffle.Content(icon(L"\uE8B1"));
	name(m_shuffle, L"Shuffle");
	m_repeat.Style(style(L"SableMusicToggleStyle"));
	m_repeatIcon = icon(L"\uE8EE");
	m_repeat.Content(m_repeatIcon);
	name(m_repeat, L"Repeat off");
	for (const auto control :
	     {m_shuffle.as<UIElement>(), m_previous.as<UIElement>(),
	      m_play.as<UIElement>(), m_next.as<UIElement>(),
	      m_repeat.as<UIElement>()}) {
		controls.Children().Append(control);
	}
	content.Children().Append(controls);
	m_error.TextWrapping(TextWrapping::Wrap);
	m_error.FontSize(12);
	m_error.Visibility(Visibility::Collapsed);
	Automation::AutomationProperties::SetLiveSetting(
	    m_error, Automation::Peers::AutomationLiveSetting::Polite);
	content.Children().Append(m_error);
	layers.Children().Append(content);
	m_surface.Child(layers);
	m_flyout.Content(m_surface);
	m_flyout.Closing([weak](auto&&, const auto& args) {
		if (const auto self = weak.lock(); self && !self->m_closed &&
		                                   !self->m_allowClose &&
		                                   self->animations()) {
			args.Cancel(true);
			if (!self->m_closing) {
				self->m_closing = true;
				self->m_open = false;
				self->m_timer.Stop();
				self->motion(false);
			}
		}
	});
}
void Pane::show() {
	if (m_closed || !m_button.XamlRoot()) {
		return;
	}
	const auto root = m_button.XamlRoot().Content().as<FrameworkElement>();
	auto titlebar = m_button.as<DependencyObject>();
	while (const auto parent = Media::VisualTreeHelper::GetParent(titlebar)) {
		if (parent == root) {
			break;
		}
		titlebar = parent;
	}
	const auto bar = titlebar.as<FrameworkElement>();
	const auto bottom =
	    bar.TransformToVisual(root)
	        .TransformPoint({0, static_cast<float>(bar.ActualHeight())})
	        .Y;
	const auto point = m_button.TransformToVisual(root).TransformPoint(
	    {static_cast<float>(m_button.ActualWidth()), 0});
	const auto width =
	    (std::min)(340., (std::max)(0., root.ActualWidth() - 16));
	m_surface.Width(width);
	m_surface.MaxHeight((std::max)(0., root.ActualHeight() - bottom - 16));
	Primitives::FlyoutShowOptions options;
	options.Placement(Primitives::FlyoutPlacementMode::BottomEdgeAlignedLeft);
	options.Position(Windows::Foundation::Point{
	    static_cast<float>(
	        (std::clamp)(point.X - width, 8.,
	                     (std::max)(8., root.ActualWidth() - width - 8))),
	    bottom + 8});
	options.ExclusionRect(Windows::Foundation::Rect{
	    0, 0, static_cast<float>(root.ActualWidth()), bottom + 8});
	m_allowClose = m_closing = false;
	m_flyout.ShowAt(root, options);
}
} // namespace music
