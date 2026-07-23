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
#pragma once
#include "../app/pch.h"
#include "../app/pdf/session.h"
#include "../app/App.xaml.h"
#include "../app/SettingsWindow.h"
#include "../app/MusicPane.h"
#include "../app/NightRunPane.h"
#include "../app/StampImage.h"
#include "../app/helpers/version.h"
#include "../app/helpers/logging.h"
#include "../app/helpers/filter.h"
#include "../app/MainWindow.xaml.h"
#include "../app/pdf/engine.h"
#include "../app/pdf/printing.h"
#include "../app/printing/controller.h"
#include "surface_test.h"
#include "music_capture.h"
#include "pdf_test_files.h"

#include <chrono>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <microsoft.ui.xaml.window.h>
#include <PrintManagerInterop.h>
#include <stdexcept>
#include <string_view>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Sable::implementation {
struct TabsTest {
	weak_ref<MainWindow> window;
	pdf::Engine& engine;
	App& app;
	TextBlock status;
	Border report;
	Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
	bool shortcuts = false;
	bool settingsOnly = false;
	bool windowsOnly = false;
	int settingsCapturePhase = -1;
	std::filesystem::path settingsCaptureDirectory;
	SIZE settingsCaptureSize{};
	std::optional<helpers::NightPDFSettings> settingsCaptureSnapshot;
	bool filtersOnly = false;
	bool propertiesOnly = false;
	bool panOnly = false;
	bool gesturesOnly = false;
	bool longGesturesOnly = false;
	Windows::Foundation::Point gesturePoint{};
	int gestureRound = 0;
	int freezeShows = 0;
	helpers::PageSize gestureSize{};
	bool layoutOnly = false;
	bool presentationOnly = false;
	bool saveCopyOnly = false;
	bool keyboardOnly = false;
	bool keyboardInputOnly = false;
	HWND inputWindow = nullptr;
	double inputZoom = 0;
	hstring inputText;
	std::size_t inputMatch = 0;
	int layoutRound = 0;
	pdf::PageKey layoutKey{};
	double panX = 0, panY = 0;
	std::uint64_t propertiesRequest = 0;
	bool linksOnly = false;
	bool printOnly = false;
	std::shared_ptr<printing::Controller> printPreviewController;
	bool finishOnly = false;
	bool editOnly = false;
	bool musicOnly = false;
	bool selectionOnly = false;
	bool rotationOnly = false;
	bool redrawOnly = false;
	winrt::com_ptr<ID2D1Device> redrawDevice;
	int rotationRound = 0;
	helpers::PageSize rotationSize{};
	bool findOnly = false;
	bool outlineOnly = false;
	bool highlightOnly = false;
	bool annotationActionsOnly = false;
	int annotationMenus = 0;
	ListViewItem annotationRow{nullptr};
	std::optional<pdf::Annotation> annotationSnapshot;
	pdf::TextMarkup selectionMarkup = pdf::TextMarkup::Highlight;
	std::string markupSubtype = "Highlight";
	bool noteEditOnly = false;
	bool checkBoxOnly = false;
	bool radioOnly = false;
	bool choiceOnly = false, choiceList = false;
	bool mixedFormOnly = false;
	bool passwordFormOnly = false;
	bool multilineOnly = false;
	bool combOnly = false;
	bool inheritedOnly = false;
	bool inheritedCheckBoxOnly = false;
	bool noSpellCheckOnly = false;
	bool sortedChoiceOnly = false;
	bool clearListOnly = false, defaultListOnly = false;
	int boundedTextStage = 0;
	int inheritedCheckBoxStage = 0;
	int defaultListStage = 0;
	std::uint64_t mixedRevision = 0;
	std::filesystem::path buttonFieldPath;
	bool markupCommentOnly = false;
	bool notePlacementOnly = false;
	bool ellipsePlacementOnly = false;
	bool inkDrawingOnly = false;
	bool stampOnly = false;
	bool fillMarksOnly = false;
	Windows::Foundation::IAsyncAction stampFixtures{nullptr};
	std::filesystem::path stampPath;
	bool notesOnly = false;
	bool notesRefusalTried = false;
	Windows::Foundation::IAsyncOperation<bool> notesSave{nullptr};
	std::filesystem::path notesPath;
	std::shared_ptr<std::promise<void>> notesGate;
	com_ptr<ID2D1Bitmap1> notesOldRaster;
	bool notesFailedReplacement = false;
	static Windows::Foundation::IAsyncOperation<ContentDialogResult>
	notesChoice(ContentDialogResult choice) {
		co_return choice;
	}
	bool commentsOnly = false;
	bool commentLocationsOnly = false;
	bool commentFactorStarted = false, commentFactorSeen = false;
	bool commentFactorCorrect = false;
	ScrollViewer::ViewChanged_revoker commentFactorChanged;
	std::uint64_t commentVersion = 0;
	bool attachmentsOnly = false;
	bool attachmentSaveOnly = false;
	std::filesystem::path attachmentTarget;
	std::shared_ptr<winrt::handle> attachmentGate;
	hstring attachmentName;
	int attachmentPicks = 0;
	static Windows::Foundation::IAsyncOperation<hstring>
	attachmentChoice(hstring path, std::shared_ptr<winrt::handle> gate) {
		if (gate) {
			co_await resume_on_signal(gate->get());
		}
		co_return path;
	}
	ListViewItem attachmentItem{nullptr};
	std::size_t outlinePage = 0;
	std::string copiedText;
	int copyCalls = 0;
	int musicSeeks = 0;
	music::Snapshot musicSnapshot;
	Windows::Foundation::IAsyncOperation<Windows::Storage::Streams::IBuffer>
	    musicArt{nullptr};
	std::weak_ptr<music::Session> closedMusic;
	int editCopies = 0;
	int editCuts = 0;
	int step = 0;
	int ticks = 0;
	int dragStarts = 0;
	int dragFinishes = 0;
	int dragResult = 0;
	using DropView = Windows::ApplicationModel::DataTransfer::DataPackageView;
	Windows::Foundation::IAsyncOperation<DropView> dropSetup{nullptr};
	DropView dropData{nullptr};
	com_ptr<MainWindow> closedDropWindow;
	com_ptr<MainWindow> sizingWindow;
	com_ptr<MainWindow> peerWindow;
	std::filesystem::path settingsPath;
	std::unique_ptr<helpers::SettingsStore> uiStore;
	double peerZoom = 0;

	static Windows::Foundation::IAsyncOperation<DropView> makeDropData() {
		using namespace Windows::Storage;
		const auto pages = co_await StorageFile::GetFileFromPathAsync(
		    std::filesystem::absolute(L"tests/fixtures/pages.pdf").wstring());
		const auto invalid = co_await StorageFile::GetFileFromPathAsync(
		    std::filesystem::absolute(L"tests/fixtures/not_pdf.pdf").wstring());
		Windows::ApplicationModel::DataTransfer::DataPackage data;
		data.SetStorageItems(std::vector<IStorageItem>{pages, pages, invalid});
		co_return data.GetView();
	}

	double zoom = 0;
	bool positioned = false;
	com_ptr<ViewerPage> first;

	TabsTest(MainWindow& owner, pdf::Engine& pdf, App& application)
	    : window(owner.get_weak()), engine(pdf), app(application) {
		settingsPath =
		    std::filesystem::temp_directory_path() /
		    ("sable_settings_ui_" + std::to_string(GetCurrentProcessId()) +
		     "_" +
		     std::to_string(
		         std::chrono::steady_clock::now().time_since_epoch().count()) +
		     ".json");
		uiStore = std::make_unique<helpers::SettingsStore>(
		    settingsPath, helpers::versionString());
	}
	~TabsTest() {
		std::error_code ignored;
		std::filesystem::remove(settingsPath, ignored);
		if (!attachmentTarget.empty()) {
			std::filesystem::remove(attachmentTarget, ignored);
		}
	}

	void focus(bool pageBox) {
		if (auto owner = window.get()) {
			if (auto current = owner->Viewer()) {
				auto v = get_self<ViewerPage>(current);
				if (pageBox) {
					v->PageBox().Focus(FocusState::Programmatic);
				} else {
					v->Scroller().Focus(FocusState::Programmatic);
				}
			}
		}
	}
	void clearBindings() {
		if (auto owner = window.get()) {
			owner->m_shortcuts.clear();
			if (auto tab = owner->Tabs().SelectedItem().try_as<TabViewItem>()) {
				tab.Focus(FocusState::Programmatic);
			}
		}
	}
	void check(bool condition,
	           const std::string& message = "tab check failed") {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}
	bool ready(ViewerPage& viewer) {
		return viewer.m_layout && !viewer.m_startPage && !viewer.m_settling &&
		       !viewer.m_landingAt;
	}
	void settingsCaptureTick(MainWindow& owner);
	void keybindSettings(SettingsWindow& settings);
	void propertiesTick(MainWindow& owner);
	void filterValues(MainWindow& owner, const helpers::SliderValues& expected);
	void filterTick(MainWindow& owner);
	void linkTick(MainWindow& owner);
	void printTick(MainWindow& owner);
	void openNightRun(MainWindow& owner);
	void checkNightRunKeys(MainWindow& owner);
	void closeNightRun(MainWindow& owner);
	void finishTick(MainWindow& owner);
	static Windows::Foundation::IAsyncOperation<
	    Windows::Storage::Streams::IBuffer>
	makeMusicArtwork(int kind = 0) {
		using namespace Windows::Graphics::Imaging;
		Windows::Storage::Streams::InMemoryRandomAccessStream stream;
		const auto encoder = co_await BitmapEncoder::CreateAsync(
		    BitmapEncoder::PngEncoderId(), stream);
		std::vector<std::uint8_t> pixels(96 * 96 * 4);
		for (int y = 0; y < 96; ++y) {
			for (int x = 0; x < 96; ++x) {
				const auto at = static_cast<std::size_t>((y * 96 + x) * 4);
				const bool circle =
				    (x - 48) * (x - 48) + (y - 48) * (y - 48) < 700;
				pixels[at] = kind == 0   ? 0
				             : kind == 1 ? (circle ? 60 : 140)
				                         : (circle ? 70 : 25);
				pixels[at + 1] = kind == 0   ? 0
				                 : kind == 1 ? (circle ? 180 : 225)
				                             : (circle ? 42 : 15);
				pixels[at + 2] = kind == 0   ? 255
				                 : kind == 1 ? 255
				                             : (circle ? 28 : 8);
				pixels[at + 3] = 255;
			}
		}
		encoder.SetPixelData(BitmapPixelFormat::Bgra8,
		                     BitmapAlphaMode::Straight, 96, 96, 96, 96, pixels);
		co_await encoder.FlushAsync();
		stream.Seek(0);
		Windows::Storage::Streams::DataReader reader(stream);
		const auto size = static_cast<std::uint32_t>(stream.Size());
		co_await reader.LoadAsync(size);
		co_return reader.ReadBuffer(size);
	}
	void musicTick(MainWindow& owner);
	void editTick(MainWindow& owner);

	void
	deliverKeys(std::initializer_list<std::pair<unsigned, unsigned>> chords);

	void keyboardInputTick(MainWindow& owner);

	void redrawTick(MainWindow& owner);

	void keyboardTick(MainWindow& owner);

	void saveCopyTick(MainWindow& owner);

	void attachmentSaveTick(MainWindow& owner);

	void commentLocationsTick(MainWindow& owner);
	void annotationActionsTick(MainWindow& owner);
	void highlightTick(MainWindow& owner);
	void passwordFormTick(MainWindow& owner);
	void mixedFormTick(MainWindow& owner);

	void choiceFieldTick(MainWindow& owner);
	void buttonFieldTick(MainWindow& owner);
	void noteEditTick(MainWindow& owner);
	static Windows::Foundation::IAsyncAction
	makeStampFixtures(std::filesystem::path png) {
		const auto store =
		    [](const std::filesystem::path& path,
		       const Windows::Storage::Streams::IBuffer& encoded) {
			    std::vector<std::uint8_t> pixels(encoded.Length());
			    Windows::Storage::Streams::DataReader::FromBuffer(encoded)
			        .ReadBytes(pixels);
			    std::ofstream out(path, std::ios::binary | std::ios::trunc);
			    out.write(reinterpret_cast<const char*>(pixels.data()),
			              static_cast<std::streamsize>(pixels.size()));
			    check_bool(static_cast<bool>(out));
		    };
		const auto artwork = co_await makeMusicArtwork();
		store(png, artwork);
		store(png.parent_path() / L"wrong.jpg", artwork);
		using namespace Windows::Graphics::Imaging;
		using namespace Windows::Storage::Streams;
		InMemoryRandomAccessStream stream;
		const auto encoder = co_await BitmapEncoder::CreateAsync(
		    BitmapEncoder::JpegEncoderId(), stream);
		std::vector<std::uint8_t> pixels(16 * 8 * 4, 255);
		encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore,
		                     16, 8, 96, 96, pixels);
		co_await encoder.FlushAsync();
		stream.Seek(0);
		DataReader reader(stream);
		const auto size = static_cast<std::uint32_t>(stream.Size());
		co_await reader.LoadAsync(size);
		const auto jpg = png.parent_path() / L"stamp.jpg";
		store(jpg, reader.ReadBuffer(size));
		const auto decoded = std::make_shared<pdf::StampImage>();
		co_await decodeStampImage(hstring(jpg.wstring()), decoded);
		check_bool(decoded->valid() && decoded->width == 16 &&
		           decoded->height == 8);
	}
	void fillMarksTick(MainWindow& owner);
	void stampTick(MainWindow& owner);
	void inkDrawingTick(MainWindow& owner);
	void notePlacementTick(MainWindow& owner);
	void notesTick(MainWindow& owner);
	void commentsTick(MainWindow& owner);
	void attachmentsTick(MainWindow& owner);
	void outlineTick(MainWindow& owner);
	void findTick(MainWindow& owner);
	void selectionTick(MainWindow& owner);
	void presentationTick(MainWindow& owner);
	void layoutTick(MainWindow& owner);
	void longGesturesTick(MainWindow& owner);
	void gesturesTick(MainWindow& owner);
	void panTick(MainWindow& owner);
	void rotationTick(MainWindow& owner);
	void tick();
};
} // namespace winrt::Sable::implementation
