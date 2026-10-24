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
// Motion (audit section 7): every transition is on Sable's tokens, nothing
// runs longer than the longest one, and with Windows' animation effects off
// nothing animates at all.
#include "tabs_test.h"
#include "../app/Motion.h"
#include "../app/helpers/motion.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>

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
// over this, a frame during a transition was dropped at 60 Hz (audit 7.3)
constexpr double FRAME_BUDGET_MS = 20;
constexpr int CYCLES = 20;
} // namespace

void TabsTest::motionWatchFrames() {
	motionCycles = 0;
	motionWorstGap = 0;
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
			    motionWorstGap = (std::max)(motionWorstGap, gap.count());
		    }
		    motionLastFrame = now;
	    });
}

// one open or close per tick; true once all of them have run and the
// frames between stayed within budget
bool TabsTest::motionCycle(const std::function<void(bool)>& toggle) {
	if (motionCycles < 2 * CYCLES) {
		motionToggledAt = std::chrono::steady_clock::now();
		toggle(motionCycles % 2 == 0);
		++motionCycles;
		return false;
	}
	motionRendering.revoke();
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
		check(motionWorstGap <= FRAME_BUDGET_MS,
		      "frames " + std::to_string(std::lround(motionWorstGap)) +
		          " ms apart while " + what + " moved");
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
