#ifndef VIDEO_H
#define VIDEO_H

#include "Types.h"
#include "RustInterface.h"
#include "VideoLayout.h"
#include "SDL3/SDL.h"


#define VIDEO_DEFAULT_TO_NO_CURSOR 0xFFFE // VIDEO_DEFAULT_TO_NO_CURSOR is equal to VIDEO_NO_CURSOR unless always_show_cursor_in_tactical is true
#define VIDEO_NO_CURSOR 0xFFFF
#define GAME_WINDOW g_game_window

extern INT16 gsMouseSizeYModifier;
extern SDL_Window* g_game_window;
extern SDL_Renderer* GameRenderer;

using VideoScaleQuality = ScalingQuality;

/** What the user asked for: window size in physical pixels (0x0 = auto = desktop size),
 * UI scale (0 = auto, else 1..4) and how the window is shown. */
struct VideoDisplaySettings
{
	int        resX;
	int        resY;
	int        uiScale;
	WindowMode windowMode;
};

/** Logical canvas size and effective scale for the settings. Queries the desktop, so SDL's
 * video subsystem has to be initialised. Not used for headless sessions (logical = -res). */
VideoLayout::DisplayLayout VideoComputeLayout(VideoDisplaySettings const& settings);

void         VideoSetFullScreen(BOOLEAN enable);
/** Creates the window and renderer. The logical canvas (SCREEN_WIDTH x SCREEN_HEIGHT) must have
 * been set from VideoComputeLayout() beforehand. */
void         InitializeVideoManager(VideoScaleQuality quality, int32_t targetFPS, VideoDisplaySettings const& settings);
void         ShutdownVideoManager(void);
void         InvalidateRegion(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom);
void         InvalidateScreen(void);

void VideoSetBrightness(float brightness);

/* Toggle between fullscreen and window mode after initialising the video
 * manager */
void VideoToggleFullScreen(void);

void SetMouseCursorProperties(INT16 sOffsetX, INT16 sOffsetY, UINT16 usCursorHeight, UINT16 usCursorWidth);

void InvalidateRegionEx(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom);

void RefreshScreen(void);

/** The composited frame (logical resolution, RGB565) as last presented. */
SDL_Surface* GetScreenBuffer();

/** Where the mouse cursor was drawn into the last presented frame. */
SDL_Rect GetMouseCursorRect();

// Creates a list to contain video Surfaces
void InitializeVideoSurfaceManager(void);

// Deletes any video Surface placed into list
void ShutdownVideoSurfaceManager(void);

#endif
