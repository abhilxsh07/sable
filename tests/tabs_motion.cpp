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
#include <cmath>
#include <string>

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
} // namespace

void TabsTest::motionTick(MainWindow& owner) {
	const auto current = owner.Viewer();
	if (!current) {
		return;
	}
	auto& v = *get_self<ViewerPage>(current);
	if (!ready(v) || ticks < 3) {
		return;
	}
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
