#include "VideoOptionsScreen.h"
#include "NativeUI.h"

#include "Button_System.h"
#include "ContentManager.h"
#include "Cursors.h"
#include "Debug.h"
#include "Directories.h"
#include "Font.h"
#include "Font_Control.h"
#include "Game_Clock.h"
#include "GameLoop.h"
#include "GameMode.h"
#include "GameScreen.h"
#include "Headless.h"
#include "Input.h"
#include "Interface.h"
#include "Interface_Control.h"
#include "JAScreens.h"
#include "Laptop.h"
#include "GameInstance.h"
#include "HImage.h"
#include "GameRes.h"
#include "Map_Information.h"
#include "MainMenuScreen.h"
#include "MapScreen.h"
#include "MessageBoxScreen.h"
#include "Options_Screen.h"
#include "Overhead_Map.h"
#include "Radar_Screen.h"
#include "Render_Dirty.h"
#include "RenderWorld.h"
#include "ScreenBackdrop.h"
#include "SysUtil.h"
#include "Tactical_Placement_GUI.h"
#include "Text.h"
#include "UILayout.h"
#include "VObject.h"
#include "VSurface.h"
#include "Video.h"
#include "WordWrap.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>


/* ---- Changing the settings ----------------------------------------------------------------------- */

namespace
{
struct PendingRequest
{
	VideoDisplaySettings settings;
	VideoScaleQuality    quality;
	bool                 persist;
};
std::optional<PendingRequest> gRequested;

bool gWindowChanged = false;
std::chrono::steady_clock::time_point gWindowChangedAt;

bool gfVideoScreenEntry = true;

bool SupportedScreen(ScreenID const screen)
{
	switch (screen)
	{
		case GAME_SCREEN:
		case MAP_SCREEN:
		case LAPTOP_SCREEN:
		case MAINMENU_SCREEN:
		case VIDEO_OPTIONS_SCREEN:
			return true;
		case OPTIONS_SCREEN: // the native options screen has the video settings on a page and lays itself out again
			return NativeUI::ScreenKey(OPTIONS_SCREEN) && NativeUI::ResolveMode("options") == NativeUI::UiMode::Native;
		default:
			return false;
	}
}
}

static void ExitVideoOptions(void);


/** What the current screen has built for the old layout goes away, the way it does when the screen is left. */
static void TearDownScreen(ScreenID const screen)
{
	switch (screen)
	{
		case GAME_SCREEN:
			RemoveMouseRegionForPauseOfClock(); // it moves with the layout
			ResetInterfaceAndUI();
			ShutdownCurrentPanel();
			gRadarRegion.Disable();
			break;

		case MAP_SCREEN:
			EndMapScreen(FALSE);
			break;

		case LAPTOP_SCREEN:
			ExitLaptop();
			break;

		case MAINMENU_SCREEN:
			RelayoutMainMenu();
			break;

		case VIDEO_OPTIONS_SCREEN:
			ExitVideoOptions();
			gfVideoScreenEntry = true;
			break;

		default:
			break;
	}
}


/** The screen builds itself again, for the new layout, the way it does when it is entered. */
static void BringBackScreen(ScreenID const screen)
{
	switch (screen)
	{
		case GAME_SCREEN:
			CreateMouseRegionForPauseOfClock();
			EnterTacticalScreen();
			if (gfWorldLoaded)
			{
				// The world is another size: keep what is in the middle in the middle, inside the limits.
				SetRenderCenter(gsRenderCenterX, gsRenderCenterY);
			}
			SetRenderFlags(RENDER_FLAG_FULL);
			break;

		default:
			// The map screen, the laptop and the menus enter themselves in their next frame
			break;
	}
}


bool ChangeVideoSettings(VideoDisplaySettings const& want, VideoScaleQuality const quality, bool const persist,
			ST::string* const error, bool const resizeWindow)
{
	auto const fail = [&](char const* msg)
	{
		if (error) *error = msg;
		return false;
	};

	ScreenID const screen = guiCurrentScreen;
	if (!SupportedScreen(screen)) return fail("the video settings cannot be changed on this screen");
	if (guiPendingScreen != NO_PENDING_SCREEN) return fail("a screen change is pending");
	if (gfInMsgBox) return fail("close the dialog first");
	if (screen == GAME_SCREEN)
	{
		if (InOverheadMap()) return fail("leave the overhead map first");
		if (gfTacticalPlacementGUIActive) return fail("finish placing the mercs first");
	}

	TearDownScreen(screen);
	guiCurrentScreen = screen; // leaving the map screen or the laptop says where it goes next; it stays

	auto const layers = VideoApplyWindow(want, quality, resizeWindow);
	g_ui.setLayers(layers);
	g_ui.recalculatePositions();
	VideoRebuildBuffers();

	// The game's own buffers follow. Contents are lost: everything is drawn again.
	ResizeGameVideoObjects();
	ResizeZBuffer();
	ResizeTopMessage();
	InitializeBackgroundRects(); // also the dirty clipping rectangle
	InvalidateBackgroundRects();
	SetFontDestBuffer(FRAME_BUFFER, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	BringBackScreen(screen);
	NativeUI::ScreenRelaidOut();
	InvalidateScreen();

	if (persist && GCM)
	{
		if (!GCM->saveVideoSettings(want.resX, want.resY, want.uiScale, want.worldZoom,
					static_cast<int>(want.windowMode), static_cast<int>(quality)))
		{
			SLOGW("Could not write the video settings to ja2.json");
		}
	}
	return true;
}


void RequestVideoSettings(VideoDisplaySettings const& want, VideoScaleQuality const quality, bool const persist)
{
	gRequested = PendingRequest{ want, quality, persist };
}


void StepWorldZoom(int const delta)
{
	if (GameMode::getInstance()->isEditorMode()) return; // the editor draws over the world at one scale
	auto settings = VideoGetDisplaySettings();
	int const current = settings.worldZoom == VideoLayout::WORLD_ZOOM_MATCH_UI ? g_ui.m_uiScale : settings.worldZoom;
	int const next = std::clamp(current + delta, 1, VideoLayout::MAX_UI_SCALE);
	if (next == current) return;
	settings.worldZoom = next;
	RequestVideoSettings(settings, VideoGetScaleQuality(), !sgp::IsHeadless());
}


void VideoNotifyWindowChanged()
{
	gWindowChanged = true;
	gWindowChangedAt = std::chrono::steady_clock::now();
}


void HandlePendingVideoChanges()
{
	if (guiPendingScreen != NO_PENDING_SCREEN) return;

	if (gRequested)
	{
		auto const request = *gRequested;
		gRequested.reset();
		ST::string error;
		if (!ChangeVideoSettings(request.settings, request.quality, request.persist, &error))
		{
			SLOGW("Video settings not changed: {}", error);
		}
		return;
	}

	using namespace std::chrono_literals;
	if (gWindowChanged && std::chrono::steady_clock::now() - gWindowChangedAt > 300ms)
	{
		// Wait for the user to stop dragging the window
		gWindowChanged = false;
		if (VideoWindowSizeChanged() && SupportedScreen(guiCurrentScreen))
		{
			ST::string error;
			if (!ChangeVideoSettings(VideoGetDisplaySettings(), VideoGetScaleQuality(), false, &error, false))
			{
				SLOGW("Window size change not followed: {}", error);
				gWindowChanged = true; // try again
				gWindowChangedAt = std::chrono::steady_clock::now();
			}
		}
	}
}


/* ---- The Video screen ---------------------------------------------------------------------------- */

namespace
{
constexpr int ROWS = 6;

struct ResolutionChoice { int w; int h; };

std::vector<ResolutionChoice> gResolutions;
VideoDisplaySettings          gPending{};
VideoScaleQuality             gPendingQuality;
int32_t                       gPendingFPS;

SGPVObject* guiBackground;
SGPVObject* guiAddOns;
BUTTON_PICS* gButtonImages;
GUIButtonRef gRowButtons[ROWS];
GUIButtonRef gApplyButton;
GUIButtonRef gDoneButton;

bool     gfRedraw = true;
bool     gfApply = false;
ScreenID gExitScreen = VIDEO_OPTIONS_SCREEN;

constexpr int32_t FPS_CHOICES[] = { 30, 40, 60, 90, 120, 144, 0 };
constexpr WindowMode WINDOW_MODES[] = { WindowMode::Windowed, WindowMode::BorderlessDesktop, WindowMode::Fullscreen };
constexpr VideoScaleQuality QUALITIES[] = { VideoScaleQuality::NEAR_PERFECT, VideoScaleQuality::LINEAR, VideoScaleQuality::PERFECT };

template<typename T, size_t N> int IndexOf(T const (&arr)[N], T const v)
{
	for (size_t i = 0; i < N; ++i) if (arr[i] == v) return static_cast<int>(i);
	return 0;
}

int ResolutionIndex()
{
	for (size_t i = 0; i < gResolutions.size(); ++i)
	{
		if (gResolutions[i].w == gPending.resX && gResolutions[i].h == gPending.resY) return static_cast<int>(i);
	}
	return 0;
}

ST::string RowText(int const row)
{
	switch (row)
	{
		case 0:
		{
			auto const& r = gResolutions[ResolutionIndex()];
			return ST::format("Resolution: {}", r.w == 0 ? ST::string("Desktop (auto)") : ST::format("{}x{}", r.w, r.h));
		}
		case 1:
			switch (gPending.windowMode)
			{
				case WindowMode::Windowed:          return "Window: Windowed";
				case WindowMode::BorderlessDesktop: return "Window: Borderless desktop";
				case WindowMode::Fullscreen:        return "Window: Fullscreen";
			}
			return "Window";
		case 2:
			return gPending.uiScale == 0 ? ST::string("UI scale: Auto") : ST::format("UI scale: {}x", gPending.uiScale);
		case 3:
			return gPending.worldZoom == 0 ? ST::string("World zoom: Same as UI") : ST::format("World zoom: {}x", gPending.worldZoom);
		case 4:
			switch (gPendingQuality)
			{
				case VideoScaleQuality::NEAR_PERFECT: return "Filter: Sharp bilinear";
				case VideoScaleQuality::LINEAR:       return "Filter: Linear";
				default:                              return "Filter: Pixel perfect";
			}
		case 5:
			return gPendingFPS == 0 ? ST::string("FPS cap: Unlimited") : ST::format("FPS cap: {}", gPendingFPS);
	}
	return "";
}

void CycleRow(int const row)
{
	switch (row)
	{
		case 0:
		{
			int const i = (ResolutionIndex() + 1) % static_cast<int>(gResolutions.size());
			gPending.resX = gResolutions[i].w;
			gPending.resY = gResolutions[i].h;
			break;
		}
		case 1:
			gPending.windowMode = WINDOW_MODES[(IndexOf(WINDOW_MODES, gPending.windowMode) + 1) % 3];
			break;
		case 2:
			gPending.uiScale = (gPending.uiScale + 1) % (VideoLayout::MAX_UI_SCALE + 1);
			break;
		case 3:
			gPending.worldZoom = (gPending.worldZoom + 1) % (VideoLayout::MAX_UI_SCALE + 1);
			break;
		case 4:
			gPendingQuality = QUALITIES[(IndexOf(QUALITIES, gPendingQuality) + 1) % 3];
			break;
		case 5:
		{
			int const n = static_cast<int>(std::size(FPS_CHOICES));
			int idx = -1;
			for (int i = 0; i < n; ++i) if (FPS_CHOICES[i] == gPendingFPS) idx = i;
			gPendingFPS = FPS_CHOICES[(idx + 1) % n];
			break;
		}
	}
	gRowButtons[row]->SpecifyText(RowText(row));
}

void BtnRowCallback(GUI_BUTTON* btn, UINT32 reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP) CycleRow(btn->GetUserData());
}

void BtnApplyCallback(GUI_BUTTON*, UINT32 reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP) gfApply = true;
}

void BtnDoneCallback(GUI_BUTTON*, UINT32 reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP) gExitScreen = OPTIONS_SCREEN;
}
}

constexpr int VIDEO_LEFT_X = 250;
constexpr int VIDEO_ROW_Y  = 105;
constexpr int VIDEO_ROW_H  = 28;
constexpr int VIDEO_ROW_GAP = 12;


static void EnterVideoOptions(void)
{
	gExitScreen = VIDEO_OPTIONS_SCREEN;
	gfApply = false;

	gPending = VideoGetDisplaySettings();
	gPendingQuality = VideoGetScaleQuality();
	gPendingFPS = VideoGetTargetFPS();

	gResolutions = { {0, 0}, {640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1366, 768}, {1600, 900},
			{1920, 1080}, {2560, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160} };
	bool known = false;
	for (auto const& r : gResolutions) if (r.w == gPending.resX && r.h == gPending.resY) known = true;
	if (!known) gResolutions.push_back({ gPending.resX, gPending.resY });

	guiBackground = AddVideoObjectFromFile(INTERFACEDIR "/optionscreenbase.sti");
	guiAddOns     = AddVideoObjectFromFile(MLG_OPTIONHEADER);
	gButtonImages = LoadButtonImage(INTERFACEDIR "/optionscreenaddons.sti", 2, 3);

	int const x = STD_SCREEN_X + VIDEO_LEFT_X;
	int y = STD_SCREEN_Y + VIDEO_ROW_Y;
	for (int i = 0; i < ROWS; ++i)
	{
		gRowButtons[i] = CreateTextButton(RowText(i), FONT12ARIAL, OPT_BUTTON_ON_COLOR, FONT_MCOLOR_BLACK,
						x, y, 340, VIDEO_ROW_H, MSYS_PRIORITY_HIGH, BtnRowCallback);
		gRowButtons[i]->SetUserData(i);
		y += VIDEO_ROW_H + VIDEO_ROW_GAP;
	}

	auto const makeButton = [](INT16 bx, GUI_CALLBACK click, ST::string const& text)
	{
		return CreateIconAndTextButton(gButtonImages, text, OPT_BUTTON_FONT, OPT_BUTTON_ON_COLOR, DEFAULT_SHADOW,
					OPT_BUTTON_OFF_COLOR, DEFAULT_SHADOW, bx, STD_SCREEN_Y + 438, MSYS_PRIORITY_HIGH, click);
	};
	gApplyButton = makeButton(STD_SCREEN_X + 51,  BtnApplyCallback, "Apply");
	gDoneButton  = makeButton(STD_SCREEN_X + 469, BtnDoneCallback,  zOptionsText[OPT_DONE]);

	gfRedraw = true;
}


static void ExitVideoOptions(void)
{
	if (!guiBackground) return; // not entered

	for (auto& b : gRowButtons) RemoveButton(b);
	RemoveButton(gApplyButton);
	RemoveButton(gDoneButton);
	UnloadButtonImage(gButtonImages);
	DeleteVideoObject(guiBackground);
	DeleteVideoObject(guiAddOns);
	guiBackground = nullptr;
	guiAddOns = nullptr;
}


static void RenderVideoOptions(void)
{
	if (SGPVSurface* const backdrop = BeginScreenBackdrop())
	{
		BltVideoObject(backdrop, guiBackground, 0, 0, 0);
		BltVideoObject(backdrop, guiAddOns, 0, 0, 0);
		BltVideoObject(backdrop, guiAddOns, 1, 0, 434);
		EndScreenBackdrop(backdrop);
	}
	BltVideoObject(FRAME_BUFFER, guiBackground, 0, STD_SCREEN_X, STD_SCREEN_Y);
	BltVideoObject(FRAME_BUFFER, guiAddOns, 0, STD_SCREEN_X, STD_SCREEN_Y);
	BltVideoObject(FRAME_BUFFER, guiAddOns, 1, STD_SCREEN_X, STD_SCREEN_Y + 434);

	// The left panel has the sound sliders drawn in it: clear it for the text
	ColorFillVideoSurfaceArea(FRAME_BUFFER, STD_SCREEN_X + 36, STD_SCREEN_Y + 110, STD_SCREEN_X + 213, STD_SCREEN_Y + 392,
				Get16BPPColor(FROMRGB(10, 10, 8)));

	int const rx = STD_SCREEN_X + VIDEO_LEFT_X;
	DrawTextToScreen("Video", rx, STD_SCREEN_Y + 82, 340, FONT14ARIAL, FONT_MCOLOR_WHITE, FONT_MCOLOR_BLACK, CENTER_JUSTIFIED);

	// What is in effect now, in the left panel
	int const lx = STD_SCREEN_X + 42;
	int ly = STD_SCREEN_Y + 92;
	auto const line = [&](ST::string const& text)
	{
		DrawTextToScreen(text, lx, ly, 175, FONT12ARIAL, FONT_MCOLOR_LTGRAY, FONT_MCOLOR_BLACK, LEFT_JUSTIFIED);
		ly += 18;
	};
	int const su = g_ui.m_uiScale;
	line("In effect now");
	ly += 8;
	line(ST::format("Window {}x{}", SCREEN_WIDTH * su, SCREEN_HEIGHT * su));
	line(ST::format("UI canvas {}x{}", SCREEN_WIDTH, SCREEN_HEIGHT));
	line(ST::format("UI scale {}x", su));
	if (VideoIsLayered()) line(ST::format("World {}x{} at {}x", WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT, g_ui.m_worldZoom));
	else                  line("World same layer as UI");
	ly += 12;
	DisplayWrappedString(lx, ly, 175, 2, FONT10ARIAL, FONT_MCOLOR_LTGRAY, "Click a setting to change it, then Apply. Changes take effect at once and are saved.", FONT_MCOLOR_BLACK, LEFT_JUSTIFIED);

	InvalidateScreen();
}


ScreenID VideoOptionsScreenHandle()
{
	if (gfVideoScreenEntry)
	{
		PauseGame();
		EnterVideoOptions();
		gfVideoScreenEntry = false;
		RenderVideoOptions();
		BltVideoSurface(guiSAVEBUFFER, FRAME_BUFFER, 0, 0, NULL);
		InvalidateScreen();
	}

	RestoreBackgroundRects();

	InputAtom event;
	while (DequeueSpecificEvent(&event, KEYBOARD_EVENTS))
	{
		if (event.usEvent == KEY_DOWN && event.usParam == SDLK_ESCAPE) gExitScreen = OPTIONS_SCREEN;
	}

	if (gfRedraw)
	{
		RenderVideoOptions();
		RenderButtons();
		gfRedraw = false;
	}

	MarkButtonsDirty();
	RenderButtons();
	SaveBackgroundRects();
	RenderFastHelp();

	if (gfApply)
	{
		gfApply = false;
		VideoSetTargetFPS(gPendingFPS);
		// At the start of the next frame: the buttons are being handled right now
		RequestVideoSettings(gPending, gPendingQuality, !sgp::IsHeadless());
	}

	if (gExitScreen != VIDEO_OPTIONS_SCREEN)
	{
		ExitVideoOptions();
		gfVideoScreenEntry = true;
		UnPauseGame();
	}
	return gExitScreen;
}


bool VideoChangePending()
{
	return gRequested.has_value();
}
