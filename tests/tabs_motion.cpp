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
// Motion: every transition is on Sable's tokens, nothing
// runs longer than the longest one, and with Windows' animation effects off
// nothing animates at all.
#include "tabs_test.h"
#include "../app/Motion.h"
#include "../app/helpers/motion.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace winrt::Sable::implementation {
namespace {
// "00:00:00.083" and the like, as WinUI writes its own durations
std::chrono::milliseconds resourceDuration(const wchar_t* key) {
	const auto text = unbox_value<hstring>(
	    Application::Current().Resources().Lookup(box_value(key)));
	const std::wstring value(text);
	const auto seconds = std::stod(value.substr(value.rfind(L':') + 1));
	return std::chrono::milliseconds(std::lround(seconds * 1000));
}
// over this, a frame during a transition was dropped at 60 Hz
constexpr double FRAME_BUDGET_MS = 20;
// one frame at 60 Hz
constexpr double FRAME_MS = 1000. / 60;
constexpr int CYCLES = 20;
} // namespace

void TabsTest::motionWatchFrames() {
	motionCycles = 0;
	motionChangeOff = motionChangeOn = motionMoving = motionAfterOff = 0;
	motionFrame = 0;
	motionLastFrame = {};
	motionToggledAt = {};
	motionRendering = Media::CompositionTarget::Rendering(
	    auto_revoke, [this](auto&&, auto&&) {
		    const auto now = std::chrono::steady_clock::now();
		    // only the frames while something is moving count
		    if (motionLastFrame != std::chrono::steady_clock::time_point{} &&
		        now - motionToggledAt <= motion::LONGEST) {
			    const std::chrono::duration<double, std::milli> gap =
			        now - motionLastFrame;
			    // the frame that makes the change lays out and redraws the
			    // pages whether or not anything animates; the ones after it
			    // are the transition
			    const bool animated = motionCycles > 2 * CYCLES;
			    if (++motionFrame == 1) {
				    auto& change = animated ? motionChangeOn : motionChangeOff;
				    change = (std::max)(change, gap.count());
			    } else {
				    auto& after = animated ? motionMoving : motionAfterOff;
				    after = (std::max)(after, gap.count());
			    }
		    }
		    motionLastFrame = now;
	    });
}

// one open or close per tick, first with animations off as a baseline and
// then with them on; true once all of them have run
bool TabsTest::motionCycle(const std::function<void(bool)>& toggle) {
	if (motionCycles < 4 * CYCLES) {
		motion::setEnabledForTest(motionCycles >= 2 * CYCLES);
		motionToggledAt = std::chrono::steady_clock::now();
		motionFrame = 0;
		toggle(motionCycles % 2 == 0);
		++motionCycles;
		// the time limit is per toggle, not for all eighty of them
		ticks = 3;
		return false;
	}
	motionRendering.revoke();
	motion::setEnabledForTest(true);
	return true;
}

void TabsTest::motionTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || ticks < 3) {
		return;
	}
	using motion::installed;
	// enters over Fast, leaves over Faster, from where it should
	const auto expectMotion = [&](const UIElement& element, float x, float y,
	                              const std::string& what) {
		const auto got = installed(element);
		check(got && got->enter == motion::FAST &&
		          got->exit == motion::FASTER && got->slideX == x &&
		          got->slideY == y,
		      what + " doesn't enter over 167 ms and leave over 83 ms");
	};
	// with animations off: nothing installed, fully drawn at once
	const auto expectStill = [&](const UIElement& element,
	                             const std::string& what) {
		using Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;
		const auto got = installed(element);
		check(got && got->enter.count() == 0 && got->exit.count() == 0 &&
		          got->slideX == 0 && got->slideY == 0 &&
		          ElementCompositionPreview::GetElementVisual(element)
		                  .Opacity() == 1,
		      what + " animated with animations off");
	};
	const auto frames = [&](const std::string& what) {
		// the pages redraw after the change with or without motion, so a
		// slow frame that motion didn't add isn't held against it
		check(motionMoving <=
		          (std::max)(FRAME_BUDGET_MS, motionAfterOff + FRAME_MS),
		      "frames " + std::to_string(std::lround(motionMoving)) +
		          " ms apart while " + what + " moved (" +
		          std::to_string(std::lround(motionAfterOff)) +
		          " ms at most without motion)");
		// motion may not make the change itself drop a frame
		check(motionChangeOn <= motionChangeOff + FRAME_MS,
		      "changing " + what + " took " +
		          std::to_string(std::lround(motionChangeOn)) +
		          " ms with motion against " +
		          std::to_string(std::lround(motionChangeOff)) + " ms without");
	};
	switch (step) {
	case 0:
		owner.AppWindow().Resize({1920, 1040});
		break;
	case 1: {
		// the XAML copy of the tokens matches the one the code uses
		check(resourceDuration(L"SableMotionFaster") == motion::FASTER &&
		          resourceDuration(L"SableMotionFast") == motion::FAST &&
		          resourceDuration(L"SableMotionNormal") == motion::NORMAL &&
		          unbox_value<double>(Application::Current().Resources().Lookup(
		              box_value(L"SableMotionSlide"))) == motion::SLIDE,
		      "Theme.xaml's SableMotion* tokens differ from helpers/motion.h");
		// Space scrolls a page with an animated ChangeView; with animations
		// off it jumps
		motion::setEnabledForTest(false);
		motionIntermediate = motionViews = 0;
		motionOffset = v.Scroller().VerticalOffset();
		motionViewChanged = v.Scroller().ViewChanged(
		    auto_revoke,
		    [this](auto&&, const ScrollViewerViewChangedEventArgs& args) {
			    ++motionViews;
			    motionIntermediate += args.IsIntermediate() ? 1 : 0;
		    });
		focus(false);
		check(v.navigationKey(VK_SPACE, {}), "Space didn't scroll the pages");
		break;
	}
	case 2:
		motionViewChanged.revoke();
		check(motionViews > 0 &&
		          v.Scroller().VerticalOffset() > motionOffset + 1,
		      "Space didn't move the pages");
		check(motionIntermediate == 0,
		      "Space scrolled in steps with animations off (" +
		          std::to_string(motionIntermediate) + " intermediate views)");
		motion::setEnabledForTest({});
		break;
	case 3:
		motion::setEnabledForTest(true);
		v.setAllToolsOpen(true);
		break;
	case 4: {
		const auto pane = v.AllToolsPanel();
		check(pane.Visibility() == Visibility::Visible,
		      "All tools didn't open");
		expectMotion(pane, -motion::SLIDE, 0, "the tool pane");
		v.closePane();
		check(pane.Visibility() == Visibility::Collapsed,
		      "the tool pane's column waited for its fade");
		motion::setEnabledForTest(false);
		v.setAllToolsOpen(true);
		expectStill(pane, "the tool pane");
		v.closePane();
		motion::setEnabledForTest(true);
		motionWatchFrames();
		break;
	}
	case 5:
		if (!motionCycle([&v](bool open) {
			    if (open) {
				    v.setAllToolsOpen(true);
			    } else {
				    v.closePane();
			    }
		    })) {
			return;
		}
		frames("the tool pane");
		break;
	case 6:
		v.openFind();
		break;
	case 7: {
		// the command bar's contents cross-fade: Find, the Comment and
		// Fill & Sign rows, and what the bar holds the rest of the time
		using Row = ViewerPage::ToolRow;
		check(v.findOpen(), "Find didn't open");
		expectMotion(v.FindBar(), 0, 0, "Find");
		expectMotion(v.AllToolsButton(), 0, 0, "the bar's All tools button");
		expectMotion(v.BarActions(), 0, 0, "the bar's actions");
		v.closeFind();
		check(!v.findOpen() &&
		          v.BarActions().Visibility() == Visibility::Visible,
		      "the command bar waited for Find's fade");
		v.m_toolRow = Row::Comment;
		v.updateQuickTools();
		expectMotion(v.CommentTools(), 0, 0, "the Comment row");
		v.m_toolRow = Row::FillSign;
		v.updateQuickTools();
		expectMotion(v.FillSignTools(), 0, 0, "the Fill & Sign row");
		check(v.CommentTools().Visibility() == Visibility::Collapsed &&
		          v.FillSignTools().Visibility() == Visibility::Visible,
		      "Fill & Sign didn't take the Comment row's place");
		v.m_toolRow = Row::None;
		v.updateQuickTools();
		motion::setEnabledForTest(false);
		v.openFind();
		expectStill(v.FindBar(), "Find");
		expectStill(v.BarActions(), "the bar's actions");
		v.closeFind();
		expectStill(v.AllToolsButton(), "the bar's All tools button");
		v.m_toolRow = Row::Comment;
		v.updateQuickTools();
		expectStill(v.CommentTools(), "the Comment row");
		v.m_toolRow = Row::FillSign;
		v.updateQuickTools();
		expectStill(v.FillSignTools(), "the Fill & Sign row");
		v.m_toolRow = Row::None;
		v.updateQuickTools();
		motion::setEnabledForTest(true);
		motionWatchFrames();
		break;
	}
	case 8:
		if (!motionCycle([&v](bool open) {
			    if (open) {
				    v.openFind();
			    } else {
				    v.closeFind();
			    }
		    })) {
			return;
		}
		frames("Find");
		break;
	case 9: {
		// the notice row drops in under the command bar and fades out
		const auto notice = v.Message();
		v.showNotice(L"Motion probe", InfoBarSeverity::Error);
		check(notice.Visibility() == Visibility::Visible && notice.IsOpen(),
		      "the notice didn't show");
		expectMotion(notice, 0, -motion::SLIDE, "the notice row");
		v.clearNotice();
		check(notice.Visibility() == Visibility::Collapsed,
		      "the notice row waited for its fade");
		motion::setEnabledForTest(false);
		v.showNotice(L"Motion probe", InfoBarSeverity::Error);
		expectStill(notice, "the notice row");
		v.clearNotice();
		motion::setEnabledForTest(true);
		motionWatchFrames();
		break;
	}
	case 10:
		if (!motionCycle([&v](bool open) {
			    if (open) {
				    v.showNotice(L"Motion probe", InfoBarSeverity::Error);
			    } else {
				    v.clearNotice();
			    }
		    })) {
			return;
		}
		frames("the notice row");
		break;
	case 11: {
		// the music pane opens over 167 ms, fades out over 83 ms and then
		// closes, and changes tint and artwork over 250 ms
		auto& state = musicSnapshot.state;
		state.player = music::Player::Spotify;
		state.playback = music::Playback::Playing;
		state.title = "A test track";
		owner.m_music->update(musicSnapshot);
		motion::setEnabledForTest(true);
		owner.m_music->show();
		break;
	}
	case 12: {
		auto& pane = *owner.m_music;
		if (!pane.m_open) {
			return;
		}
		check(pane.m_enterLength == motion::FAST,
		      "the music pane doesn't open over 167 ms");
		pane.tint({200, 40, 40});
		check(pane.m_tintLength == motion::NORMAL &&
		          pane.m_background.TintTransitionDuration() ==
		              Windows::Foundation::TimeSpan(motion::NORMAL) &&
		          pane.m_transition.Children().Size() == 3,
		      "the music tint doesn't change over 250 ms in one go");
		pane.m_art.Source(Media::Imaging::WriteableBitmap(4, 4));
		pane.setArtwork(nullptr, {});
		check(pane.m_artLength == motion::NORMAL && pane.m_artFading,
		      "the music artwork doesn't cross-fade over 250 ms");
		musicClosings.clear();
		if (!musicClosingWatch) {
			musicClosingWatch = true;
			pane.m_flyout.Closing([this](auto&&, auto&&) {
				musicClosings.push_back(std::chrono::steady_clock::now());
			});
		}
		pane.m_flyout.Hide();
		check(pane.m_exitLength == motion::FASTER && pane.m_fadingOut,
		      "the music pane doesn't fade out over 83 ms");
		break;
	}
	case 13: {
		auto& pane = *owner.m_music;
		check(musicClosings.size() >= 2 && !pane.m_open,
		      "the music pane didn't close after its fade");
		const auto held = std::chrono::duration_cast<std::chrono::milliseconds>(
		    musicClosings[1] - musicClosings[0]);
		check(held >= motion::FASTER - std::chrono::milliseconds(20) &&
		          held <= motion::FASTER + std::chrono::milliseconds(50),
		      "the music pane held its close for " +
		          std::to_string(held.count()) + " ms");
		motion::setEnabledForTest(false);
		pane.show();
		break;
	}
	case 14: {
		auto& pane = *owner.m_music;
		if (!pane.m_open) {
			return;
		}
		check(pane.m_enterLength.count() == 0 &&
		          pane.m_surface.Opacity() == 1 && pane.m_slide.Y() == 0,
		      "the music pane animated open with animations off");
		pane.tint({20, 25, 30});
		check(pane.m_tintLength.count() == 0 &&
		          pane.m_transition.Children().Size() == 0,
		      "the music tint animated with animations off");
		pane.m_art.Source(Media::Imaging::WriteableBitmap(4, 4));
		pane.setArtwork(nullptr, {});
		check(pane.m_artLength.count() == 0 && !pane.m_artFading,
		      "the music artwork animated with animations off");
		musicClosings.clear();
		pane.m_flyout.Hide();
		break;
	}
	case 15:
		check(musicClosings.size() == 1 && !owner.m_music->m_open &&
		          !owner.m_music->m_fadingOut,
		      "the music pane waited to close with animations off");
		motion::setEnabledForTest({});
		break;
	case 16: {
		// MORTY drops in over 167 ms, the alerts below move at once, and
		// each fades over 250 ms once its time is up
		const auto stack = owner.MortyStack();
		check(!stack.ChildrenTransitions() ||
		          stack.ChildrenTransitions().Size() == 0,
		      "the Morty alerts still slide when one goes");
		motion::setEnabledForTest(true);
		owner.showMorty();
		const auto alert = owner.m_morties.back().alert;
		check(alert.Visibility() == Visibility::Visible && alert.IsOpen() &&
		          stack.Children().GetAt(0) == alert,
		      "a new Morty alert isn't showing on top");
		expectMotion(alert, 0, -motion::SLIDE, "a Morty alert");
		owner.onMortyTick(alert);
		check(owner.m_morties.back().fading &&
		          owner.m_morties.back().timer.Interval() ==
		              Windows::Foundation::TimeSpan(motion::NORMAL),
		      "a Morty alert doesn't fade over 250 ms");
		owner.removeMorty(alert);
		motion::setEnabledForTest(false);
		owner.showMorty();
		const auto still = owner.m_morties.back().alert;
		expectStill(still, "a Morty alert");
		owner.onMortyTick(still);
		check(still.Opacity() == 0 &&
		          owner.m_morties.back().timer.Interval().count() == 0,
		      "a Morty alert faded with animations off");
		owner.removeMorty(still);
		motion::setEnabledForTest({});
		break;
	}
	default:
		status.Text(L"PASS: motion tokens and the animation setting");
		probePass(owner.get_strong().as<Window>());
		timer.Stop();
		return;
	}
	++step;
	ticks = 0;
}
} // namespace winrt::Sable::implementation
