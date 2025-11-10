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
#include <gtest/gtest.h>
#include <winrt/base.h>

int main(int argc, char** argv) {
	// Cached WinRT factories must not outlive a temporary test apartment.
	// see: https://devblogs.microsoft.com/oldnewthing/20211105-00/?p=105878
	winrt::init_apartment(winrt::apartment_type::multi_threaded);
	testing::InitGoogleTest(&argc, argv);
	const int result = RUN_ALL_TESTS();
	winrt::clear_factory_cache();
	winrt::uninit_apartment();
	return result;
}
