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
#include <cmath>
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
	title.Text(L"Night Run · Paper Tide");
	title.FontSize(30);
	title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	layout.Children().Append(title);
	TextBlock tips;
	tips.Text(L"Left / Right or A / D to steer · Space / Up to hop");
	tips.TextWrapping(TextWrapping::Wrap);
	layout.Children().Append(tips);

	Canvas track;
	track.Width(FIELD_WIDTH);
	track.Height(FIELD_HEIGHT);
	track.Background(brush(12, 44, 63));
	Media::RectangleGeometry clip;
	clip.Rect(Windows::Foundation::Rect{0, 0, FIELD_WIDTH, FIELD_HEIGHT});
	track.Clip(clip);
	for (int i = 0; i < 18; ++i) {
		Shapes::Rectangle wave;
		wave.Fill(brush(36, 91, 112));
		wave.RadiusX(2);
		wave.RadiusY(2);
		track.Children().Append(wave);
		m_waves.push_back(wave);
	}
	m_runner.RadiusX(12);
	m_runner.RadiusY(12);
	m_obstacle.RadiusX(12);
	m_obstacle.RadiusY(12);
	m_page.Fill(brush(255, 244, 201));
	m_page.Stroke(brush(215, 177, 86));
	m_page.StrokeThickness(2);
	m_head.Fill(brush(244, 204, 167));
	m_head.Width(14);
	m_head.Height(14);
	m_shield.Width(58);
	m_shield.Height(68);
	m_shield.Stroke(brush(255, 224, 135));
	m_shield.StrokeThickness(3);
	m_runner.Fill(brush(123, 225, 186));
	m_obstacle.Fill(brush(119, 137, 151));
	track.Children().Append(m_runner);
	track.Children().Append(m_obstacle);
	track.Children().Append(m_page);
	track.Children().Append(m_head);
	track.Children().Append(m_shield);

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
	pane->m_field.Loaded([weak](auto&&, auto&&) {
		if (const auto self = weak.lock();
		    self && self->m_root.Visibility() == Visibility::Visible) {
			self->focus();
		}
	});
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

void Pane::steer(int direction) {
	m_run.steer(direction);
	render();
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
	const double x = 160.0 * (m_run.lane() + 1);
	const double y = GROUND - m_run.lift() / 4;
	place(m_runner, 24, 48, x - 12, y - 24);
	Canvas::SetLeft(m_head, x - 7);
	Canvas::SetTop(m_head, y - 15);
	Canvas::SetLeft(m_shield, x - 29);
	Canvas::SetTop(m_shield, y - 34);
	m_shield.Visibility(m_run.shield() ? Visibility::Visible
	                                   : Visibility::Collapsed);
	const double approach = GROUND - (m_run.obstacleX() - RUNNER_X);
	place(m_obstacle, 38, 38, 160.0 * (m_run.obstacleLane() + 1) - 19,
	      approach - 19);
	place(m_page, 22, 28, 160.0 * (m_run.pageLane() + 1) - 11, approach - 14);
	m_page.Visibility(m_run.pageTaken() ? Visibility::Collapsed
	                                    : Visibility::Visible);
	for (std::size_t i = 0; i < m_waves.size(); ++i) {
		place(m_waves[i], 28, 3, 38 + static_cast<double>((i * 97) % 560),
		      std::fmod(static_cast<double>(i * 47 + m_run.score() * 10),
		                FIELD_HEIGHT));
	}
	m_score.Text(L"Distance " + std::to_wstring(m_run.score()));
	if (m_run.phase() == Phase::Running) {
		m_status.Text(L"Pages " + std::to_wstring(m_run.pages()) +
		              (m_run.shield() ? L" · Shield ready"
		                              : L" · Collect 3 for a shield"));
	}
}

void Pane::updateControls() {
	switch (m_run.phase()) {
	case Phase::Ready:
		m_play.Content(box_value(L"Play"));
		m_status.Text(L"Space to sail. Left / Right or A / D to steer. Space / "
		              L"Up to hop.");
		break;
	case Phase::Running:
		m_play.Content(box_value(L"Pause"));
		m_status.Text(
		    L"Collect pages for a shield. Dodge rocks or hop with Space.");
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
