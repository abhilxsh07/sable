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
#include "MainWindow.xaml.h"
#include <winrt/Windows.System.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Windowing;
namespace winrt::Sable::implementation {
void MainWindow::setupPresentation() {
	AppWindow().Changed([weak = get_weak()](auto&&, const auto& args) {
		if (const auto self = weak.get();
		    self && !self->m_closing && self->m_presentationViewer &&
		    args.DidPresenterChange() &&
		    self->AppWindow().Presenter().Kind() !=
		        AppWindowPresenterKind::FullScreen) {
			self->endPresentation(false);
		}
	});
}

bool MainWindow::presentationKey(unsigned key, unsigned modifiers,
                                 bool repeated) {
	const bool control = (modifiers & helpers::SHORTCUT_CTRL) != 0;
	const bool alt = (modifiers & helpers::SHORTCUT_ALT) != 0;
	const bool shift = (modifiers & helpers::SHORTCUT_SHIFT) != 0;
	const bool windows = (modifiers & helpers::SHORTCUT_WIN) != 0;
	if (control && alt && !shift && !windows && key == 'P') {
		if (!repeated) {
			onPresentation({}, {});
		}
		return true;
	}
	if (!m_presentationViewer) {
		return false;
	}
	if (modifiers == helpers::SHORTCUT_CTRL &&
	    (key == 'S' || key == 'P' || key == 'O')) {
		endPresentation();
		return false;
	}
	if (modifiers == 0 && (key == VK_ESCAPE || key == VK_F11)) {
		endPresentation();
		return true;
	}
	const auto viewer = get_self<ViewerPage>(m_presentationViewer);
	viewer->showPresentationControls();
	const auto focused =
	    Input::FocusManager::GetFocusedElement(RootGrid().XamlRoot());
	if ((!control && !alt && !windows && key == VK_TAB) ||
	    (!control && !alt && !windows &&
	     (key == VK_RETURN || key == VK_SPACE) &&
	     focused.try_as<Controls::Primitives::ButtonBase>())) {
		return false;
	}
	if (control && !alt && !windows && (key == VK_UP || key == VK_DOWN)) {
		viewer->presentationKey(key == VK_UP ? VK_HOME : VK_END, false);
	} else if (!control && !alt && !windows &&
	           (!shift || key == VK_SPACE || key == VK_RETURN)) {
		viewer->presentationKey(key, shift);
	}
	return true;
}

// Behaviour follows the bundled viewer's PDFPresentationMode, not its code.
void MainWindow::onPresentation(const Windows::Foundation::IInspectable&,
                                const RoutedEventArgs&) {
	if (m_closing) {
		return;
	}
	if (m_presentationViewer) {
		endPresentation();
		return;
	}
	commitMenuEdit();
	const auto current = Viewer();
	if (!current || m_printing || m_settingsWindow) {
		return;
	}
	const auto viewer = get_self<ViewerPage>(current);
	if (!viewer->beginPresentation([weak = get_weak()] {
		    if (const auto self = weak.get()) {
			    self->endPresentation();
		    }
	    })) {
		return;
	}
	m_presentationViewer = current;
	m_beforePresentation = AppWindow().Presenter();
	try {
		MainMenu().Hide();
		if (const auto flyout = MusicButton().Flyout()) {
			flyout.Hide();
		}
		DarkConfigurator().Visibility(Visibility::Collapsed);
		AppWindow().SetPresenter(FullScreenPresenter::Create());
		AppTitleBar().Visibility(Visibility::Collapsed);
		RootGrid().UpdateLayout();
		viewer->reflowViewport();
		viewer->Scroller().Focus(FocusState::Programmatic);
	} catch (...) {
		endPresentation();
		viewer->showNotice(L"Couldn't enter presentation mode.");
	}
}

void MainWindow::endPresentation(bool restorePresenter) {
	const auto current = std::exchange(m_presentationViewer, nullptr);
	if (!current) {
		return;
	}
	const auto previous = std::exchange(m_beforePresentation, nullptr);
	AppTitleBar().Visibility(Visibility::Visible);
	try {
		if (restorePresenter && previous && !m_closing) {
			AppWindow().SetPresenter(previous);
		}
	} catch (...) {
		OutputDebugStringW(to_message().c_str());
	}
	RootGrid().UpdateLayout();
	get_self<ViewerPage>(current)->endPresentation();
	updateLayoutMenu();
	updateCursorToolMenu();
}
} // namespace winrt::Sable::implementation
