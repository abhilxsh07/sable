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
#include <utility>
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

void place(const Shapes::Ellipse& shape, double size, double x, double y) {
	shape.Width(size);
	shape.Height(size);
	Canvas::SetLeft(shape, x - size / 2);
	Canvas::SetTop(shape, y - size / 2);
}

double laneX(int lane) {
	return 160.0 * (lane + 1);
}

// rows come down the track towards the board
double approach(double x) {
	return GROUND - (x - RUNNER_X);
}

void style(const Shapes::Rectangle& shape, Cell cell) {
	switch (cell) {
	case Cell::Rock:
		shape.Fill(brush(119, 137, 151));
		shape.StrokeThickness(0);
		shape.RadiusX(12);
		shape.RadiusY(12);
		break;
	case Cell::Staple:
		shape.Fill(brush(196, 204, 214));
		shape.Stroke(brush(120, 132, 146));
		shape.StrokeThickness(2);
		shape.RadiusX(3);
		shape.RadiusY(3);
		break;
	case Cell::Ink:
		shape.Fill(brush(24, 16, 40));
		shape.Stroke(brush(110, 86, 170));
		shape.StrokeThickness(2);
		shape.RadiusX(15);
		shape.RadiusY(15);
		break;
	default:
		shape.Fill(brush(255, 244, 201));
		shape.Stroke(brush(215, 177, 86));
		shape.StrokeThickness(2);
		shape.RadiusX(2);
		shape.RadiusY(2);
		break;
	}
}

// enough for every row between the top and bottom of the track
constexpr std::size_t CELLS = 24;
constexpr std::size_t WORM_SEGMENTS = 5;
} // namespace

Pane::Pane(const Microsoft::UI::Dispatching::DispatcherQueue& queue,
           std::filesystem::path best)
    : m_bestFile(std::move(best)), m_best(loadBest(m_bestFile)) {
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
	tips.Text(L"Left / Right or A / D to steer around rocks · Space / Up to "
	          L"hop staples and ink · Pages keep the bookworm back");
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
	for (std::size_t i = 0; i < CELLS; ++i) {
		Shapes::Rectangle cell;
		cell.Visibility(Visibility::Collapsed);
		track.Children().Append(cell);
		m_cells.push_back(cell);
	}
	m_cellKinds.assign(CELLS, Cell::Empty);
	for (std::size_t i = 0; i < WORM_SEGMENTS; ++i) {
		Shapes::Ellipse segment;
		segment.Fill(i == 0 ? brush(226, 140, 176) : brush(196, 112, 150));
		track.Children().Append(segment);
		m_worm.push_back(segment);
	}
	m_wormX = laneX(1);
	m_board = brush(123, 225, 186);
	m_inked = brush(150, 130, 200);
	m_runner.RadiusX(12);
	m_runner.RadiusY(12);
	m_head.Fill(brush(244, 204, 167));
	m_head.Width(14);
	m_head.Height(14);
	m_shield.Width(58);
	m_shield.Height(68);
	m_shield.Stroke(brush(255, 224, 135));
	m_shield.StrokeThickness(3);
	m_runner.Fill(m_board);
	track.Children().Append(m_runner);
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
Pane::create(const Microsoft::UI::Dispatching::DispatcherQueue& queue,
             std::filesystem::path best) {
	auto pane = std::shared_ptr<Pane>(new Pane(queue, std::move(best)));
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
	m_newBest = false;
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
		const bool ended = m_run.tick();
		const auto events = m_run.events();
		if (ended) {
			finish();
			break;
		}
		if (events & EVENT_SHIELD_LOST) {
			sound(Cue::Crash);
		} else if (events & EVENT_INK) {
			sound(Cue::Ink);
		} else if (events & EVENT_PAGE) {
			sound(Cue::Page);
		}
	}
	render();
}

void Pane::render() {
	const double x = laneX(m_run.lane());
	const double y = GROUND - m_run.lift() / 4;
	place(m_runner, 24, 48, x - 12, y - 24);
	m_runner.Fill(m_run.slowed() ? m_inked : m_board);
	Canvas::SetLeft(m_head, x - 7);
	Canvas::SetTop(m_head, y - 15);
	Canvas::SetLeft(m_shield, x - 29);
	Canvas::SetTop(m_shield, y - 34);
	m_shield.Visibility(m_run.shield() ? Visibility::Visible
	                                   : Visibility::Collapsed);

	std::size_t used = 0;
	for (const auto& row : m_run.rows()) {
		const double top = approach(row.x);
		if (top < -40 || top > FIELD_HEIGHT + 40) {
			continue;
		}
		for (int lane = 0; lane < 3 && used < m_cells.size(); ++lane) {
			const Cell cell = row.cells[static_cast<std::size_t>(lane)];
			if (cell == Cell::Empty || (cell == Cell::Page && row.taken)) {
				continue;
			}
			const auto& shape = m_cells[used];
			const double centre = laneX(lane);
			// restyle a pooled shape only when it changes kind
			const bool restyle = m_cellKinds[used] != cell;
			m_cellKinds[used++] = cell;
			if (restyle) {
				style(shape, cell);
			}
			switch (cell) {
			case Cell::Rock:
				place(shape, 38, 38, centre - 19, top - 19);
				break;
			case Cell::Staple:
				place(shape, 96, 10, centre - 48, top - 5);
				break;
			case Cell::Ink:
				place(shape, 50, 30, centre - 25, top - 15);
				break;
			default:
				place(shape, 22, 28, centre - 11, top - 14);
				break;
			}
			shape.Visibility(Visibility::Visible);
		}
	}
	for (; used < m_cells.size(); ++used) {
		m_cells[used].Visibility(Visibility::Collapsed);
	}

	// the bookworm trails the board from below and rises as it closes in
	m_wormX += (x - m_wormX) * 0.15;
	const double wormTop = GROUND + 36 + m_run.worm() * 0.9;
	const double wiggle = static_cast<double>(m_run.score()) * 0.4;
	for (std::size_t i = 0; i < m_worm.size(); ++i) {
		place(m_worm[i], i == 0 ? 30 : 26,
		      m_wormX + std::sin(wiggle + static_cast<double>(i)) * 6,
		      wormTop + 18 * static_cast<double>(i));
	}

	for (std::size_t i = 0; i < m_waves.size(); ++i) {
		place(m_waves[i], 28, 3, 38 + static_cast<double>((i * 97) % 560),
		      std::fmod(static_cast<double>(i * 47 + m_run.score() * 10),
		                FIELD_HEIGHT));
	}
	m_score.Text(L"Distance " + std::to_wstring(m_run.score()) + L" · Best " +
	             std::to_wstring((std::max)(m_best, m_run.score())));
	if (m_run.phase() == Phase::Running) {
		std::wstring status = L"Pages " + std::to_wstring(m_run.pages()) +
		                      (m_run.shield() ? L" · Shield ready"
		                                      : L" · Collect 3 for a shield");
		if (m_run.slowed()) {
			status += L" · Inked!";
		} else if (m_run.worm() < 60) {
			status += L" · The bookworm is closing in";
		}
		m_status.Text(status);
	}
}

void Pane::finish() {
	m_timer.Stop();
	sound(Cue::Crash);
	if (m_run.score() > m_best) {
		m_best = m_run.score();
		m_newBest = true;
		saveBest(m_bestFile, m_best);
	}
	updateControls();
}

void Pane::updateControls() {
	switch (m_run.phase()) {
	case Phase::Ready:
		m_play.Content(box_value(L"Play"));
		m_status.Text(L"Space to sail. Dodge rocks, hop staples and ink, and "
		              L"grab pages before the bookworm catches up.");
		break;
	case Phase::Running:
		m_play.Content(box_value(L"Pause"));
		m_status.Text(L"Collect pages for a shield. Steer around rocks, hop "
		              L"staples.");
		break;
	case Phase::Paused:
		m_play.Content(box_value(L"Resume"));
		m_status.Text(L"Paused. Press Resume or Space to keep going.");
		break;
	case Phase::Crashed: {
		m_play.Content(box_value(L"Play again"));
		std::wstring status;
		switch (m_run.ending()) {
		case Ending::Staple:
			status = L"You caught a staple.";
			break;
		case Ending::Worm:
			status = L"The bookworm caught you.";
			break;
		default:
			status = L"You hit a rock.";
			break;
		}
		if (m_newBest) {
			status += L" New best!";
		}
		m_status.Text(status + L" Press Space to try again.");
		break;
	}
	}
	m_restart.IsEnabled(m_run.phase() != Phase::Ready);
}

void Pane::sound(Cue cue) {
	if (!m_muted) {
		m_audio.play(cue);
	}
}
} // namespace night_run
