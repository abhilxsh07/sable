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
#include "NightRunPane.h"

#include <algorithm>
#include <string>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Text.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace night_run {
namespace {
constexpr auto STEP = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::duration<double>{1.0 / TICKS_PER_SECOND});

Media::SolidColorBrush brush(std::uint8_t red, std::uint8_t green,
                             std::uint8_t blue) {
	return Media::SolidColorBrush(Windows::UI::Color{255, red, green, blue});
}

void place(const Shapes::Rectangle& shape, double width, double height,
           double x, double y) {
	shape.Width(width);
	shape.Height(height);
	Canvas::SetLeft(shape, x);
	Canvas::SetTop(shape, y);
}
} // namespace

Pane::Pane(const Microsoft::UI::Dispatching::DispatcherQueue& queue) {
	m_root.Visibility(Visibility::Collapsed);
	m_root.HorizontalContentAlignment(HorizontalAlignment::Stretch);
	m_root.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);

	StackPanel layout;
	layout.Margin(Thickness{16, 20, 16, 20});
	layout.MaxWidth(FIELD_WIDTH);
	layout.Spacing(12);

	TextBlock title;
	title.Text(L"Night Run");
	title.FontSize(30);
	title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	layout.Children().Append(title);

	Canvas track;
	track.Width(FIELD_WIDTH);
	track.Height(FIELD_HEIGHT);
	track.Background(brush(13, 20, 36));
	Media::RectangleGeometry clip;
	clip.Rect(Windows::Foundation::Rect{0, 0, FIELD_WIDTH, FIELD_HEIGHT});
	track.Clip(clip);
	Shapes::Rectangle floor;
	floor.Fill(brush(34, 47, 67));
	place(floor, FIELD_WIDTH, FIELD_HEIGHT - GROUND, 0, GROUND);
	track.Children().Append(floor);
	for (int i = 0; i < 15; ++i) {
		Shapes::Rectangle star;
		star.Fill(brush(116, 134, 165));
		place(star, 2, 2, 17 + (i * 83) % 610, 13 + (i * 41) % 120);
		track.Children().Append(star);
	}
	m_runner.Fill(brush(123, 225, 186));
	m_obstacle.Fill(brush(248, 165, 112));
	track.Children().Append(m_runner);
	track.Children().Append(m_obstacle);

	Viewbox view;
	view.Child(track);
	view.Stretch(Media::Stretch::Uniform);
	m_field.Content(view);
	m_field.HorizontalContentAlignment(HorizontalAlignment::Stretch);
	m_field.IsTabStop(true);
	Automation::AutomationProperties::SetName(m_field, L"Night Run playfield");
	layout.Children().Append(m_field);

	StackPanel controls;
	controls.Orientation(Orientation::Horizontal);
	controls.Spacing(8);
	m_restart.Content(box_value(L"Restart"));
	m_mute.Content(box_value(L"Mute"));
	m_score.Text(L"Score 0");
	m_score.VerticalAlignment(VerticalAlignment::Center);
	controls.Children().Append(m_play);
	controls.Children().Append(m_restart);
	controls.Children().Append(m_mute);
	controls.Children().Append(m_score);
	layout.Children().Append(controls);
	m_status.TextWrapping(TextWrapping::Wrap);
	layout.Children().Append(m_status);
	m_root.Content(layout);

	m_timer = queue.CreateTimer();
	m_timer.IsRepeating(true);
	m_timer.Interval(
	    std::chrono::duration_cast<Windows::Foundation::TimeSpan>(STEP));
	render();
	updateControls();
}

std::shared_ptr<Pane>
Pane::create(const Microsoft::UI::Dispatching::DispatcherQueue& queue) {
	auto pane = std::shared_ptr<Pane>(new Pane(queue));
	const std::weak_ptr<Pane> weak = pane;
	pane->m_timer.Tick([weak](auto&&, auto&&) {
		if (const auto self = weak.lock()) {
			self->tick();
		}
	});
	pane->m_play.Click([weak](auto&&, auto&&) {
		if (const auto self = weak.lock()) {
			if (self->m_run.phase() == Phase::Running) {
				self->pause();
			} else {
				self->start(false);
				self->focus();
			}
		}
	});
	pane->m_restart.Click([weak](auto&&, auto&&) {
		if (const auto self = weak.lock()) {
			self->start(true);
			self->focus();
		}
	});
	pane->m_mute.Click([weak](auto&&, auto&&) {
		if (const auto self = weak.lock()) {
			self->m_muted = !self->m_muted;
			if (self->m_muted) {
				self->m_audio.stop();
			}
			self->m_mute.Content(
			    box_value(self->m_muted ? L"Unmute" : L"Mute"));
		}
	});
	return pane;
}

Pane::~Pane() {
	m_timer.Stop();
}

bool Pane::focus() {
	return m_field.Focus(FocusState::Programmatic);
}

void Pane::key() {
	if (m_run.phase() != Phase::Running) {
		start(false);
		focus();
	} else if (m_run.jump()) {
		sound(Cue::Jump);
	}
}

void Pane::start(bool restart) {
	m_audio.stop();
	if (!restart && m_run.phase() == Phase::Paused) {
		m_run.resume();
	} else {
		m_run.start();
	}
	m_next = Clock::now() + STEP;
	m_timer.Start();
	render();
	updateControls();
}

void Pane::pause() {
	m_audio.stop();
	m_timer.Stop();
	if (m_run.phase() == Phase::Running) {
		m_run.pause();
		updateControls();
	}
}

void Pane::tick() {
	if (m_run.phase() != Phase::Running) {
		return;
	}
	const auto now = Clock::now();
	// a stalled window catches up by at most six ticks
	m_next = (std::max)(m_next, now - 5 * STEP);
	while (m_next <= now) {
		m_next += STEP;
		if (m_run.tick()) {
			m_timer.Stop();
			sound(Cue::Crash);
			updateControls();
			break;
		}
	}
	render();
}

void Pane::render() {
	place(m_runner, RUNNER_WIDTH, RUNNER_HEIGHT, RUNNER_X,
	      GROUND - RUNNER_HEIGHT - m_run.lift());
	const double height = m_run.obstacleHeight();
	place(m_obstacle, OBSTACLE_WIDTH, height, m_run.obstacleX(),
	      GROUND - height);
	if (m_shownScore != m_run.score()) {
		m_shownScore = m_run.score();
		m_score.Text(L"Score " + std::to_wstring(m_shownScore));
	}
}

void Pane::updateControls() {
	switch (m_run.phase()) {
	case Phase::Ready:
		m_play.Content(box_value(L"Play"));
		m_status.Text(L"Press Play or Space to start. Jump with Space or Up.");
		break;
	case Phase::Running:
		m_play.Content(box_value(L"Pause"));
		m_status.Text(L"Jump with Space or Up.");
		break;
	case Phase::Paused:
		m_play.Content(box_value(L"Resume"));
		m_status.Text(L"Paused. Press Resume or Space to keep going.");
		break;
	case Phase::Crashed:
		m_play.Content(box_value(L"Play again"));
		m_status.Text(L"Run ended. Press Space to try again.");
		break;
	}
	m_restart.IsEnabled(m_run.phase() != Phase::Ready);
}

void Pane::sound(Cue cue) {
	if (!m_muted) {
		m_audio.play(cue);
	}
}
} // namespace night_run
