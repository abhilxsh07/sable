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

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::Sable::implementation {
// port of viewer.mjs webViewerKeyDown() and NightPDF's fixed menu keys.
bool MainWindow::routeKey(unsigned key, unsigned modifiers, bool repeated) {
	auto self = this;
	const auto current = Viewer();
	const auto viewer = current ? get_self<ViewerPage>(current) : nullptr;
	if (m_closing || m_settingsWindow ||
	    (viewer && (viewer->m_propertiesDialog || viewer->m_passwordDialog))) {
		return false;
	}
	if (presentationKey(key, modifiers, repeated)) {
		return true;
	}
	if (key == VK_F11 && modifiers == 0) {
		if (!repeated) {
			onToggleFullScreen({}, {});
		}
		return true;
	}
	if (key == 'M' && modifiers == helpers::SHORTCUT_CTRL) {
		onMinimize({}, {});
		return true;
	}
	const auto focused =
	    Input::FocusManager::GetFocusedElement(self->RootGrid().XamlRoot());
	const bool editing = focused.try_as<Controls::TextBox>() ||
	                     focused.try_as<Controls::PasswordBox>() ||
	                     focused.try_as<Controls::RichEditBox>();
	if (const auto active = self->Viewer()) {
		const auto pdf = get_self<ViewerPage>(active);
		if (modifiers == helpers::SHORTCUT_CTRL && key == 'F') {
			pdf->openFind();
			return true;
		}
		if ((key == VK_F3 &&
		     (modifiers == 0 || modifiers == helpers::SHORTCUT_SHIFT)) ||
		    (key == 'G' && (modifiers == helpers::SHORTCUT_CTRL ||
		                    modifiers == (helpers::SHORTCUT_CTRL |
		                                  helpers::SHORTCUT_SHIFT)))) {
			pdf->navigateFind((modifiers & helpers::SHORTCUT_SHIFT) ? -1 : 1);
			return true;
		}
		if (key == VK_ESCAPE && modifiers == 0 && pdf->findOpen()) {
			pdf->closeFind();
			return true;
		}
	}
	if (modifiers == helpers::SHORTCUT_CTRL &&
	    (key == 'Z' || key == 'Y' || key == 'X' || key == 'C' || key == 'V' ||
	     key == 'A') &&
	    editing) {
		// The text field's native edit keys take priority while editing.
		return false;
	}
	if (self->m_easterEgg && key == 'T' &&
	    modifiers == (helpers::SHORTCUT_ALT | helpers::SHORTCUT_SHIFT)) {
		if (!repeated) {
			self->showEasterEgg();
		}
		return true;
	}
	if (!editing) {
		if (const auto active = self->Viewer()) {
			const auto pdf = get_self<ViewerPage>(active);
			if (modifiers == helpers::SHORTCUT_CTRL && key == 'A') {
				pdf->selectAllText();
				return true;
			}
			if (modifiers == helpers::SHORTCUT_CTRL && key == 'C' &&
			    pdf->hasTextSelection()) {
				pdf->copySelection();
				return true;
			}
			if (modifiers == 0 && key == VK_ESCAPE && pdf->hasTextSelection()) {
				pdf->clearTextSelection();
				return true;
			}
		}
	}
	// these belong to NightPDF's fixed menu, not the settings store
	if (modifiers == helpers::SHORTCUT_CTRL && key >= '1' && key <= '9') {
		self->selectTabPosition(
		    key == '9' ? static_cast<int>(self->Tabs().TabItems().Size()) - 1
		               : static_cast<int>(key - '1'));
		return true;
	}
	if (modifiers == helpers::SHORTCUT_ALT && key == 'S') {
		self->onOpenSettings({}, {});
		return true;
	}
	if (modifiers == helpers::SHORTCUT_CTRL && key == 'P') {
		self->onPrint({}, {});
		return true;
	}
	if (modifiers == helpers::SHORTCUT_CTRL && key == 'S') {
		self->onSaveCopy({}, {});
		return true;
	}
	if (modifiers == helpers::SHORTCUT_CTRL && key == 'O') {
		pickDocuments();
		return true;
	}
	if (const auto* binding =
	        helpers::matchShortcut(self->m_shortcuts, key, modifiers)) {
		self->dispatchShortcut(*binding);
		return true;
	}
	if (viewer &&
	    (modifiers == helpers::SHORTCUT_CTRL ||
	     modifiers == (helpers::SHORTCUT_CTRL | helpers::SHORTCUT_SHIFT))) {
		if (key == VK_OEM_PLUS || key == VK_ADD) {
			viewer->zoomIn();
			return true;
		}
		if (key == VK_OEM_MINUS || key == VK_SUBTRACT) {
			viewer->zoomOut();
			return true;
		}
		if (key == '0' || key == VK_NUMPAD0) {
			viewer->zoomReset();
			return true;
		}
	}
	return viewer && viewer->navigationKey(
	                     key, {(modifiers & helpers::SHORTCUT_CTRL) != 0,
	                           (modifiers & helpers::SHORTCUT_ALT) != 0,
	                           (modifiers & helpers::SHORTCUT_SHIFT) != 0,
	                           (modifiers & helpers::SHORTCUT_WIN) != 0});
}
} // namespace winrt::Sable::implementation
