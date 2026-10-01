#ifndef VIDEO_H
#define VIDEO_H

#include "Types.h"
#include "RustInterface.h"
#include "VideoLayout.h"
#include "SDL3/SDL.h"
#include <cstdint>
#include <vector>


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
	int        worldZoom = VideoLayout::WORLD_ZOOM_MATCH_UI; // 0 = the world follows the UI scale (one layer)
};

/** Logical canvas size and effective scale for the settings. Queries the desktop, so SDL's
 * video subsystem has to be initialised. Not used for headless sessions (logical = -res). */
VideoLayout::DisplayLayout VideoComputeLayout(VideoDisplaySettings const& settings);

/* ---- Layers -------------------------------------------------------------------------------------
 * By default the whole picture (the tactical world and the UI) is one surface at one scale. With
 * "world_zoom" set to a value other than the UI scale it is two: the world in a WORLD_BUFFER of
 * window / Zw pixels, and under it the UI, the surfaces the rest of the game draws into (FRAME_BUFFER,
 * ...), at window / Su pixels. The compositor puts the UI over the world. The UI surfaces are RGB565
 * like always, so what is transparent in them is a reserved colour: */

/** Where the UI layer has nothing, so the world shows through. */
constexpr UINT16 UI_LAYER_TRANSPARENT = 0xF81F;

/** Colours next to the transparent one stand for black with an alpha of level/16 (the result of shading
 * the transparent area: pop-up shadows, fades). Level 0 is the transparent colour itself. */
constexpr int    UI_LAYER_SHADOW_LEVELS = 16;

constexpr bool UiLayerIsTransparentFamily(UINT16 const p)
{
	return p >= UI_LAYER_TRANSPARENT - (UI_LAYER_SHADOW_LEVELS - 1) && p <= UI_LAYER_TRANSPARENT;
}

/** 0 (fully transparent) to 15. p must be in the transparent family. */
constexpr int UiLayerShadowLevel(UINT16 const p)
{
	return UI_LAYER_TRANSPARENT - p;
}

/** The colour of a transparent-family pixel after being darkened by `darkening` (0..1) */
constexpr UINT16 UiLayerDarken(UINT16 const p, float const darkening)
{
	float const alpha    = UiLayerShadowLevel(p) / float(UI_LAYER_SHADOW_LEVELS);
	float const newAlpha = alpha + (1.0f - alpha) * darkening;
	int level = static_cast<int>(newAlpha * UI_LAYER_SHADOW_LEVELS + 0.5f);
	if (darkening > 0 && level <= UiLayerShadowLevel(p)) level = UiLayerShadowLevel(p) + 1;
	if (level > UI_LAYER_SHADOW_LEVELS - 1) level = UI_LAYER_SHADOW_LEVELS - 1;
	return static_cast<UINT16>(UI_LAYER_TRANSPARENT - level);
}

/** 8-bit alpha of a transparent-family colour, as composed over the world. */
constexpr UINT8 UiLayerShadowAlpha(UINT16 const p)
{
	return static_cast<UINT8>(UiLayerShadowLevel(p) * 17);
}

/* ---- Changing the video settings while the game runs ---------------------------------------------
 * See VideoOptionsScreen.h (ChangeVideoSettings), which drives these in order:
 * VideoApplyWindow -> g_ui.setLayers + recalculatePositions -> VideoRebuildBuffers -> the game's own buffers. */

/** What is currently applied (as last requested; the window may have been resized since). */
VideoDisplaySettings const& VideoGetDisplaySettings();
VideoScaleQuality           VideoGetScaleQuality();

/** Puts the window in the requested mode and size (unless resizeWindow is false: it has changed already, e.g. dragged
 * by the user) and returns the layers for it. Headless sessions have no window: -res is the canvas. Does not touch g_ui. */
VideoLayout::LayerLayout VideoApplyWindow(VideoDisplaySettings const& want, VideoScaleQuality quality, bool resizeWindow);

/** Recreates the video manager's surfaces and textures for the size and layers in g_ui, keeping the FRAME_BUFFER,
 * BACKBUFFER and WORLD_BUFFER objects the game points at. Contents are lost: the whole screen is redrawn. */
void VideoRebuildBuffers();

/** With an automatic UI scale: whether the window's pixel size (resized by the user or moved to another display)
 * now calls for another canvas size or scale than the current one. */
bool VideoWindowSizeChanged();

/** Frame rate limit for presenting. 0 = unlimited. */
void    VideoSetTargetFPS(int32_t fps);
int32_t VideoGetTargetFPS();

/** Whether the world is a layer of its own (window sized) under the UI. Set by InitializeVideoManager(). */
bool VideoIsLayered();

/** Shows or hides the world layer. It is only seen on the tactical screens; whatever else is on screen is
 * an opaque UI. Kept up to date by the video manager from the current screen. */
bool VideoWorldLayerVisible();

/** Like InvalidateRegionEx() for a rectangle in world pixels: something changed in the WORLD_BUFFER. */
void InvalidateWorldRegion(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom);

/** The frame as the player sees it when layered: UI over world, at the size of the window canvas
 * (UI size * UI scale), 8 bit RGB. Returns false, leaving the arguments alone, when there are no layers
 * (then GetScreenBuffer() is the whole picture). */
bool VideoComposeFrame(std::vector<uint8_t>& rgb, int& width, int& height);

/** The colour (0xRRGGBB) the player sees at a UI pixel: the UI layer's, or when that is transparent the
 * world's under its centre. */
uint32_t VideoComposePixel(int uiX, int uiY);

/* ---- Overlay -------------------------------------------------------------------------------------
 * Something drawn over the finished frame at the output's own resolution: the native UI
 * (src/game/NativeUI). It draws either through the GPU renderer, after the frame is put on the window,
 * or (headless, automation) into a layer of its own that is blended into the ScreenBuffer, so
 * screenshots and pixel reads include it. */
struct VideoOverlay
{
	virtual ~VideoOverlay() = default;
	/** Whether this frame is drawn with GpuRender (a window and a GPU renderer) instead of the software path. */
	virtual bool UsesGpu() = 0;
	/** Software path: bring the layer (ScreenBuffer sized) up to date. Returns the area it covers, empty if none.
	 * Called once per RefreshScreen. */
	virtual SDL_Rect SoftwarePrepare() = 0;
	/** Software path: blend the layer over @a dst (the RGB565 ScreenBuffer) inside @a area. */
	virtual void SoftwareCompose(SDL_Surface* dst, SDL_Rect const& area) = 0;
	/** Software path: the layer at a ScreenBuffer pixel, premultiplied 0xAARRGGBB (0 = nothing there). */
	virtual uint32_t SoftwarePixel(int x, int y) = 0;
	/** GPU path: draw onto the window in output pixels (the logical presentation is switched off meanwhile). */
	virtual void GpuRender(SDL_Renderer*) = 0;
	/** The overlay draws the mouse cursor itself: the legacy one is not drawn. */
	virtual bool HidesLegacyCursor() = 0;
};
void VideoSetOverlay(VideoOverlay*);

/** Reads the next presented frame back from the GPU (window pixels, what the player sees, native UI included).
 * Call VideoRequestOutputCapture, let a frame be presented, then VideoTakeOutputCapture (false: no window/renderer). */
void VideoRequestOutputCapture();
bool VideoTakeOutputCapture(std::vector<uint8_t>& rgb, int& w, int& h);

/** Where canvas pixel (x, y) lands in the output: (x * sx + ox, y * sy + oy); the output is w x h pixels (the
 * window's pixels, or the ScreenBuffer headless, where the mapping is the identity). */
struct VideoOutputMapping { float sx, sy, ox, oy; int w, h; bool gpu; };
VideoOutputMapping VideoGetOutputMapping();

/* ---- Phase 8: the world drawn by a recording renderer (src/game/TileEngine/WorldRender.h) ---------------------
 * Set before InitializeVideoManager: */
/** The world is always a layer of its own (at the UI scale when no world zoom is set). */
void VideoSetForceLayered(bool);
bool VideoForceLayered();
/** Create the renderer on an SDL_GPU Vulkan device that the world renderer shares (SDL's "gpu" render driver). */
void VideoRequestGpuDevice(bool);
/** That device, or null (headless, not asked for, or not available). */
SDL_GPUDevice* VideoGpuDevice();
/** The world layer is this texture (RGBA8, on VideoGpuDevice(), w x h world pixels) instead of the WORLD_BUFFER;
 * null goes back to the WORLD_BUFFER. */
void VideoSetWorldGpuTexture(SDL_GPUTexture*, int w, int h);
/** The world is redrawn whole every frame: scrolling does not move and patch the WORLD_BUFFER. */
void VideoSetWorldRecorded(bool);

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
