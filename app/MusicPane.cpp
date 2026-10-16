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
#include "helpers/logging.h"

#include <algorithm>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace music {
namespace {
void label(const DependencyObject& object, std::wstring_view name) {
	Automation::AutomationProperties::SetName(object, name);
}
FontIcon icon(const wchar_t* glyph) {
	FontIcon result;
	result.Glyph(glyph);
	result.FontSize(18);
	return result;
}
Button transport(const wchar_t* glyph, const wchar_t* name) {
	Button result;
	result.Content(icon(glyph));
	result.Width(44);
	result.Height(40);
	label(result, name);
	ToolTipService::SetToolTip(result, box_value(name));
	return result;
}
} // namespace
Pane::Pane(Button button, std::function<void()> layoutChanged)
    : m_button(std::move(button)), m_layoutChanged(std::move(layoutChanged)) {}
std::shared_ptr<Pane> Pane::attach(const Button& button,
                                   std::function<void()> layoutChanged,
                                   bool connect) {
	auto self =
	    std::shared_ptr<Pane>(new Pane(button, std::move(layoutChanged)));
	self->initialize(connect);
	return self;
}
Pane::~Pane() {
	close();
}

void Pane::initialize(bool connect) {
	const auto weak = weak_from_this();
	m_button.Visibility(Visibility::Collapsed);
	m_button.Flyout(m_flyout);
	m_flyout.Placement(Microsoft::UI::Xaml::Controls::Primitives::
	                       FlyoutPlacementMode::BottomEdgeAlignedRight);
	m_background.TintColor({255, 45, 45, 45});
	m_background.FallbackColor({255, 45, 45, 45});
	m_background.TintOpacity(0.8);
	m_background.TintLuminosityOpacity(0.85);
	Style presenter(xaml_typename<FlyoutPresenter>());
	presenter.Setters().Append(
	    Setter(Control::BackgroundProperty(), m_background));
	presenter.Setters().Append(
	    Setter(Control::PaddingProperty(), box_value(Thickness{16})));
	presenter.Setters().Append(
	    Setter(FrameworkElement::MinWidthProperty(), box_value(0.)));
	m_flyout.FlyoutPresenterStyle(presenter);

	StackPanel content;
	content.Width(304);
	content.Spacing(12);
	content.RequestedTheme(ElementTheme::Dark);
	Grid header;
	header.ColumnDefinitions().Append(ColumnDefinition());
	ColumnDefinition textColumn;
	textColumn.Width({1, GridUnitType::Star});
	header.ColumnDefinitions().Append(textColumn);
	header.ColumnDefinitions().GetAt(0).Width({88, GridUnitType::Pixel});
	header.ColumnSpacing(12);
	Grid cover;
	cover.Width(88);
	cover.Height(88);
	m_art.Stretch(Media::Stretch::UniformToFill);
	m_art.IsHitTestVisible(false);
	m_placeholder = icon(L"\uE8D6");
	m_placeholder.FontSize(34);
	m_placeholder.Opacity(0.65);
	cover.Children().Append(m_placeholder);
	cover.Children().Append(m_art);
	header.Children().Append(cover);
	StackPanel info;
	Grid::SetColumn(info, 1);
	info.Spacing(5);
	info.VerticalAlignment(VerticalAlignment::Center);
	m_title.FontSize(15);
	m_title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	m_title.TextWrapping(TextWrapping::Wrap);
	m_title.MaxLines(2);
	m_title.TextTrimming(TextTrimming::CharacterEllipsis);
	m_artist.TextWrapping(TextWrapping::Wrap);
	m_artist.MaxLines(2);
	m_artist.TextTrimming(TextTrimming::CharacterEllipsis);
	m_artist.FontSize(13);
	m_artist.Opacity(0.9);
	m_player.FontSize(12);
	m_player.Opacity(0.7);
	info.Children().Append(m_title);
	info.Children().Append(m_artist);
	info.Children().Append(m_player);
	header.Children().Append(info);
	content.Children().Append(header);

	StackPanel timeline;
	m_seek.Minimum(0);
	m_seek.Maximum(1);
	m_seek.StepFrequency(1);
	m_seek.IsThumbToolTipEnabled(false);
	label(m_seek, L"Playback position");
	timeline.Children().Append(m_seek);
	Grid times;
	m_elapsed.FontSize(12);
	m_remaining.FontSize(12);
	m_elapsed.Opacity(0.8);
	m_remaining.Opacity(0.8);
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
	m_playIcon = icon(L"\uE768");
	m_play.Content(m_playIcon);
	m_play.Width(52);
	m_play.Height(40);
	controls.Children().Append(m_previous);
	controls.Children().Append(m_play);
	controls.Children().Append(m_next);
	content.Children().Append(controls);
	StackPanel modes;
	modes.Orientation(Orientation::Horizontal);
	modes.HorizontalAlignment(HorizontalAlignment::Center);
	modes.Spacing(12);
	m_shuffle.Content(box_value(L"Shuffle"));
	label(m_shuffle, L"Shuffle");
	m_repeat.Content(box_value(L"Repeat: off"));
	modes.Children().Append(m_shuffle);
	modes.Children().Append(m_repeat);
	content.Children().Append(modes);
	m_error.TextWrapping(TextWrapping::Wrap);
	m_error.FontSize(12);
	m_error.Visibility(Visibility::Collapsed);
	Automation::AutomationProperties::SetLiveSetting(
	    m_error, Automation::Peers::AutomationLiveSetting::Polite);
	content.Children().Append(m_error);
	m_flyout.Content(content);

	m_previous.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::Previous);
		}
	});
	m_play.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::PlayPause);
		}
	});
	m_next.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::Next);
		}
	});
	m_shuffle.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::Shuffle);
		}
	});
	m_repeat.Click([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->command(Command::Repeat);
		}
	});
	const auto queue = m_button.DispatcherQueue();
	m_timer = queue.CreateTimer();
	m_timer.Interval(std::chrono::seconds(1));
	m_timer.Tick([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->progress();
		}
	});
	m_seekTimer = queue.CreateTimer();
	m_seekTimer.IsRepeating(false);
	m_seekTimer.Interval(std::chrono::milliseconds(350));
	m_seekTimer.Tick([weak](auto&&, auto&&) {
		if (auto self = weak.lock(); self && !self->m_closed) {
			self->m_seeking = false;
			if (self->m_session) {
				self->m_session->command(Command::Seek, self->m_seek.Value());
			}
		}
	});
	m_seek.ValueChanged([weak](auto&&, auto&&) {
		if (auto self = weak.lock();
		    self && !self->m_closed && !self->m_updating &&
		    enabled(self->m_snapshot.state, Command::Seek)) {
			self->m_seeking = true;
			self->m_seekTimer.Stop();
			self->m_seekTimer.Start();
		}
	});
	m_flyout.Opened([weak](auto&&, auto&&) {
		if (auto self = weak.lock(); self && !self->m_closed) {
			self->m_open = true;
			self->progress();
			self->m_timer.Start();
		}
	});
	m_flyout.Closed([weak](auto&&, auto&&) {
		if (auto self = weak.lock()) {
			self->m_open = false;
			self->m_timer.Stop();
			self->m_seekTimer.Stop();
			self->m_seeking = false;
		}
	});
	if (connect) {
		m_session = Session::create(queue, [weak](const Snapshot& snapshot) {
			if (const auto self = weak.lock()) {
				self->update(snapshot);
			}
		});
	}
}
void Pane::command(Command command) {
	if (!m_closed && m_session) {
		m_session->command(command);
	}
}
void Pane::update(const Snapshot& snapshot) {
	if (m_closed) {
		return;
	}
	const bool changedSession =
	    snapshot.state.player != m_snapshot.state.player;
	m_snapshot = snapshot;
	const auto& state = m_snapshot.state;
	const auto visibility =
	    state.visible() ? Visibility::Visible : Visibility::Collapsed;
	if (m_button.Visibility() != visibility) {
		m_button.Visibility(visibility);
		if (m_layoutChanged) {
			m_layoutChanged();
		}
	}
	if (!state.visible()) {
		m_flyout.Hide();
	}
	m_title.Text(
	    to_hstring(state.title.empty() ? "Unknown track" : state.title));
	m_artist.Text(to_hstring(state.artist));
	m_artist.Visibility(state.artist.empty() ? Visibility::Collapsed
	                                         : Visibility::Visible);
	m_player.Text(to_hstring(playerName(state.player)));
	m_error.Text(to_hstring(state.error));
	m_error.Visibility(state.error.empty() ? Visibility::Collapsed
	                                       : Visibility::Visible);
	m_previous.IsEnabled(enabled(state, Command::Previous));
	m_play.IsEnabled(enabled(state, Command::PlayPause));
	m_next.IsEnabled(enabled(state, Command::Next));
	m_seek.IsEnabled(enabled(state, Command::Seek));
	m_shuffle.IsEnabled(enabled(state, Command::Shuffle));
	m_shuffle.IsChecked(state.shuffle.value_or(false));
	m_repeat.IsEnabled(enabled(state, Command::Repeat));
	const bool playing = state.playback == Playback::Playing;
	m_playIcon.Glyph(playing ? L"\uE769" : L"\uE768");
	label(m_play, playing ? L"Pause" : L"Play");
	ToolTipService::SetToolTip(m_play, box_value(playing ? L"Pause" : L"Play"));
	const hstring repeat = state.repeat == Repeat::All   ? L"Repeat: all"
	                       : state.repeat == Repeat::One ? L"Repeat: one"
	                                                     : L"Repeat: off";
	m_repeat.Content(box_value(repeat));
	label(m_repeat, repeat);
	if (changedSession || snapshot.artworkVersion != m_artworkVersion ||
	    !enabled(state, Command::Seek)) {
		m_seekTimer.Stop();
		m_seeking = false;
	}
	if (snapshot.artworkVersion != m_artworkVersion) {
		m_artworkVersion = snapshot.artworkVersion;
		const auto generation = ++m_artworkGeneration;
		m_art.Source(nullptr);
		m_placeholder.Visibility(Visibility::Visible);
		if (snapshot.artwork) {
			artwork(weak_from_this(), snapshot.artwork, generation);
		} else {
			tint({});
		}
	}
	progress();
}
void Pane::progress() {
	if (m_closed) {
		return;
	}
	const auto& t = m_snapshot.state.timeline;
	const double elapsed =
	    std::chrono::duration<double>(std::chrono::steady_clock::now() -
	                                  m_snapshot.sampledAt)
	        .count();
	const auto current = position(t, elapsed);
	const bool valid = t.start >= 0 && t.end > t.start;
	m_updating = true;
	m_seek.Maximum(
	    valid ? static_cast<double>(t.end - t.start) / TICKS_PER_SECOND : 1);
	if (!m_seeking) {
		m_seek.Value(valid ? static_cast<double>(current - t.start) /
		                         TICKS_PER_SECOND
		                   : 0);
	}
	m_updating = false;
	const auto shown =
	    m_seeking ? seekPosition(t, m_seek.Value()).value_or(current) : current;
	m_elapsed.Text(to_hstring(timeText(valid ? shown - t.start : 0)));
	m_remaining.Text(valid ? L"-" + to_hstring(timeText(t.end - shown))
	                       : L"--:--");
}
void Pane::tint(Tint color) {
	if (m_closed) {
		return;
	}
	const Windows::UI::Color next{255, color.r, color.g, color.b};
	const auto previous = m_background.TintColor();
	m_transition.Stop();
	m_transition.Children().Clear();
	for (const auto property : {L"TintColor", L"FallbackColor"}) {
		Media::Animation::ColorAnimation animation;
		animation.From(previous);
		animation.To(next);
		animation.Duration(
		    DurationHelper::FromTimeSpan(std::chrono::milliseconds(300)));
		animation.EnableDependentAnimation(true);
		Media::Animation::Storyboard::SetTarget(animation, m_background);
		Media::Animation::Storyboard::SetTargetProperty(animation, property);
		m_transition.Children().Append(animation);
	}
	m_background.TintColor(next);
	m_background.FallbackColor(next);
	m_transition.Begin();
}
winrt::fire_and_forget
Pane::artwork(std::weak_ptr<Pane> weak,
              Windows::Storage::Streams::IRandomAccessStreamReference reference,
              std::uint64_t generation) {
	try {
		const auto current = [weak, generation] {
			const auto self = weak.lock();
			return self && !self->m_closed &&
			       self->m_artworkGeneration == generation;
		};
		const auto stream = co_await reference.OpenReadAsync();
		if (!current()) {
			co_return;
		}
		if (!stream || stream.Size() > 8 * 1024 * 1024) {
			throw hresult_invalid_argument();
		}
		using namespace Windows::Graphics::Imaging;
		const auto decoder = co_await BitmapDecoder::CreateAsync(stream);
		if (!current()) {
			co_return;
		}
		if (static_cast<std::uint64_t>(decoder.PixelWidth()) *
		        decoder.PixelHeight() >
		    16 * 1024 * 1024) {
			throw hresult_invalid_argument();
		}
		BitmapTransform transform;
		transform.ScaledWidth(16);
		transform.ScaledHeight(16);
		const auto data = co_await decoder.GetPixelDataAsync(
		    BitmapPixelFormat::Bgra8, BitmapAlphaMode::Straight, transform,
		    ExifOrientationMode::IgnoreExifOrientation,
		    ColorManagementMode::DoNotColorManage);
		const auto pixels = data.DetachPixelData();
		const auto color = artworkTint({pixels.data(), pixels.size()});
		if (!current()) {
			co_return;
		}
		stream.Seek(0);
		Media::Imaging::BitmapImage bitmap;
		bitmap.DecodePixelWidth(176);
		co_await bitmap.SetSourceAsync(stream);
		if (const auto self = weak.lock();
		    self && !self->m_closed &&
		    self->m_artworkGeneration == generation) {
			self->m_art.Source(bitmap);
			self->m_placeholder.Visibility(Visibility::Collapsed);
			self->tint(color);
		}
	} catch (...) {
		diagnostics::write(diagnostics::Level::Debug,
		                   "Couldn't decode music artwork");
		if (const auto self = weak.lock();
		    self && !self->m_closed &&
		    self->m_artworkGeneration == generation) {
			self->tint({});
		}
	}
}
void Pane::close() noexcept {
	if (m_closed) {
		return;
	}
	m_closed = true;
	++m_artworkGeneration;
	if (m_session) {
		m_session->close();
	}
	m_session.reset();
	m_layoutChanged = {};
	try {
		if (m_timer) {
			m_timer.Stop();
		}
		if (m_seekTimer) {
			m_seekTimer.Stop();
		}
		m_transition.Stop();
		m_flyout.Hide();
		m_button.Flyout(nullptr);
	} catch (...) {
	}
}
} // namespace music
