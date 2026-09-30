#include "Clock.h"
#include "Cursor_Control.h"
#include "Headless.h"
#include "Debug.h"
#include "FPS.h"
#include "Logger.h"
#include "HImage.h"
#include "Local.h"
#include "RenderWorld.h"
#include "Render_Dirty.h"
#include "Types.h"
#include "VObject_Blitters.h"
#include "VSurface.h"
#include "Video.h"
#include "Visualizer.h"
#include "UILayout.h"
#include "Icon.h"
#include "JAScreens.h"
#include "ScreenIDs.h"
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#define MAX_CURSOR_WIDTH  64
#define MAX_CURSOR_HEIGHT 64

#define MAX_DIRTY_REGIONS 128

// Border colour around the canvas when the window is not an exact multiple of it
static constexpr Uint8 LETTERBOX_COLOR = 16;


// Globals for mouse cursor
static UINT16 gusMouseCursorWidth;
static UINT16 gusMouseCursorHeight;
static INT16  gsMouseCursorXOffset;
static INT16  gsMouseCursorYOffset;
INT16 gsMouseSizeYModifier = 0; // This can increase the size of gusMouseCursorHeight so image data (ie, text) outside the normal height of the mouse cursor can be copied onto screen buffer

static SDL_Rect MouseBackground = { 0, 0, 0, 0 };

// Dirty rectangle management variables
static SDL_Rect DirtyRegions[MAX_DIRTY_REGIONS];
static UINT32   guiDirtyRegionCount;
static BOOLEAN  gfForceFullScreenRefresh;


static SDL_Rect DirtyRegionsEx[MAX_DIRTY_REGIONS];
static UINT32   guiDirtyRegionExCount;


static SDL_Surface* MouseCursor;
static SDL_Surface* FrameBuffer;
SDL_Renderer*  GameRenderer;
SDL_Window* g_game_window;

static SDL_Surface* ScreenBuffer;
static SDL_Texture* ScreenTexture;
static SDL_Texture* ScaledScreenTexture;

// Layers, see Video.h. Only when the world has a scale of its own.
static bool         Layered = false;
static SDL_Surface* WorldBuffer;   // the world, world pixels (RGB565)
static SDL_Texture* WorldTexture;  // WorldBuffer on the GPU
static SDL_Texture* UiTexture;     // ScreenBuffer with the transparent colour turned into alpha (ARGB8888)
static SDL_Texture* CanvasTexture; // both layers at their scale, window sized: what is presented
static SDL_Rect     WorldDirty{ 0, 0, 0, 0 }; // what to upload of WorldBuffer next
static bool         WorldLayerShown = false;
static std::vector<uint32_t> UiLayerLut; // RGB565 -> ARGB8888 for the UI layer
static Uint32       g_window_flags = 0;
static VideoScaleQuality ScaleQuality = VideoScaleQuality::LINEAR;
// Sharp-bilinear: nearest-neighbour multiple of the canvas (0 = not needed)
static int          SharpBilinearScale = 0;
static VideoLayout::Size PresentedWindowSize{ 0, 0 };
static sgp::GameClock::duration TimeBetweenRefreshScreens;
static int32_t TargetFPS = 40;
static VideoDisplaySettings CurrentSettings{ 0, 0, 0, WindowMode::BorderlessDesktop };
static VideoOverlay* Overlay = nullptr;
static SDL_Rect     OverlayArea{ 0, 0, 0, 0 }; // what the software overlay covered last frame

static void DeletePrimaryVideoSurfaces(void);
static SDL_Rect ClipToSurface(SDL_Rect const& rect, SDL_Surface const* surface);

namespace
{
struct DesktopInfo
{
	SDL_DisplayID      display;
	VideoLayout::Size  pixels;  // physical pixels
	float              density; // pixels per window coordinate unit (HiDPI)
};

DesktopInfo QueryDesktop()
{
	DesktopInfo info{ SDL_GetPrimaryDisplay(), { 1280, 720 }, 1.0f };
	SDL_DisplayMode const* dm = info.display ? SDL_GetDesktopDisplayMode(info.display) : nullptr;
	if (dm)
	{
		info.density = dm->pixel_density > 0 ? dm->pixel_density : 1.0f;
		info.pixels  = { static_cast<int>(dm->w * info.density + 0.5f), static_cast<int>(dm->h * info.density + 0.5f) };
	}
	return info;
}

/** Size in physical pixels the canvas is derived from (before the scale is applied). */
VideoLayout::Size WindowBasis(VideoDisplaySettings const& settings, VideoLayout::Size const desktop)
{
	bool const isAuto = settings.resX <= 0 || settings.resY <= 0;
	if (isAuto && settings.windowMode == WindowMode::Windowed)
	{
		return VideoLayout::DefaultWindowedSize(desktop);
	}
	return VideoLayout::ResolveWindowSize(settings.resX, settings.resY, desktop);
}
}

// returns if desktop resolution is at least the game resolution
BOOLEAN IsDesktopLargeEnough()
{
	auto const desktop{ QueryDesktop().pixels };
	return desktop.w >= SCREEN_WIDTH && desktop.h >= SCREEN_HEIGHT;
}

VideoLayout::DisplayLayout VideoComputeLayout(VideoDisplaySettings const& settings)
{
	auto const desktop{ QueryDesktop().pixels };
	auto const basis{ WindowBasis(settings, desktop) };
	auto const layout{ VideoLayout::ComputeDisplayLayout(basis, settings.uiScale) };
	SLOGI("Desktop {}x{}, window basis {}x{}, UI scale {} -> logical canvas {}x{}",
		desktop.w, desktop.h, basis.w, basis.h,
		layout.scale, layout.logical.w, layout.logical.h);
	return layout;
}

SDL_Surface* GetScreenBuffer()
{
	return ScreenBuffer;
}

SDL_Rect GetMouseCursorRect()
{
	return MouseBackground;
}


void VideoSetFullScreen(const BOOLEAN enable)
{
	if (!g_game_window) return;
	SDL_SetWindowFullscreen(g_game_window, enable);
	SDL_SetWindowMouseGrab(g_game_window, enable);
}

void VideoToggleFullScreen(void)
{
	if (!g_game_window) return;
	VideoSetFullScreen(!(SDL_GetWindowFlags(g_game_window) & SDL_WINDOW_FULLSCREEN));
}

void VideoSetBrightness(float brightness)
{
	if (brightness < 0) return;

	if (ScreenTexture)        SDL_SetTextureColorModFloat(ScreenTexture, brightness, brightness, brightness);
	if (CanvasTexture)        SDL_SetTextureColorModFloat(CanvasTexture, brightness, brightness, brightness);
}


bool VideoIsLayered()
{
	return Layered;
}


bool VideoWorldLayerVisible()
{
	return Layered && WorldLayerShown;
}


static void GetRGBDistribution();


static void CreateSoftwareSurfaces();

static void UpdatePresentation(bool force);
static void CreateLayerTextures();
static void CreateTextures();

void InitializeVideoManager(const VideoScaleQuality quality, const int32_t targetFPS, VideoDisplaySettings const& settings)
{
	VideoSetTargetFPS(targetFPS);
	CurrentSettings = settings;
	Layered = g_ui.isLayered();

	if (sgp::IsHeadless())
	{
		// No window, renderer or textures: the game only ever draws into the
		// CPU-side surfaces, which is all a headless session needs.
		CreateSoftwareSurfaces();
		return;
	}

	ScaleQuality = quality;
	g_window_flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

	auto const desktop{ QueryDesktop() };

	// The window is created at its windowed size; the fullscreen modes then switch
	// away from it (and back to it, e.g. with Alt+Enter).
	bool const windowed{ settings.windowMode == WindowMode::Windowed };
	auto const windowPx{ windowed ? WindowBasis(settings, desktop.pixels)
	                              : VideoLayout::DefaultWindowedSize(desktop.pixels) };
	auto const toPoints = [&](int px) { return static_cast<int>(px / desktop.density + 0.5f); };

	g_game_window = SDL_CreateWindow(APPLICATION_NAME,
					toPoints(windowPx.w), toPoints(windowPx.h),
					g_window_flags);
	if (!g_game_window)
	{
		throw std::runtime_error(std::string("Failed to create the game window: ") + SDL_GetError());
	}
	SDL_SetWindowMinimumSize(g_game_window,
		toPoints(VideoLayout::MIN_LOGICAL_WIDTH), toPoints(VideoLayout::MIN_LOGICAL_HEIGHT));

	if (settings.windowMode == WindowMode::Fullscreen && settings.resX > 0 && settings.resY > 0)
	{
		// Exclusive fullscreen: switch the display to the closest mode to the request.
		SDL_DisplayMode mode;
		if (SDL_GetClosestFullscreenDisplayMode(desktop.display,
			toPoints(settings.resX), toPoints(settings.resY), 0.0f, true, &mode))
		{
			SDL_SetWindowFullscreenMode(g_game_window, &mode);
		}
	}

	GameRenderer = SDL_CreateRenderer(g_game_window, nullptr);
	SDL_SetRenderLogicalPresentation(GameRenderer, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	SurfaceUniquePtr windowIcon(SDL_CreateSurfaceFrom(
			gWindowIconData.width,
			gWindowIconData.height,
			SDL_PIXELFORMAT_ABGR8888,
			(void*)gWindowIconData.pixel_data,
			gWindowIconData.bytes_per_pixel*gWindowIconData.width
	));
	SDL_SetWindowIcon(g_game_window, windowIcon.get());


	CreateSoftwareSurfaces();

	CreateTextures();


	// Filtering and the presentation rectangle depend on the window size
	// (see UpdatePresentation), which changes when the window is resized or toggled.
	UpdatePresentation(true);

	if (settings.windowMode != WindowMode::Windowed)
	{
		VideoSetFullScreen(TRUE);
		SDL_SyncWindow(g_game_window);
		UpdatePresentation(true);
	}

	SDL_HideCursor();
}


static void CreateTextures()
{
	if (Layered)
	{
		CreateLayerTextures();
		return;
	}
	ScreenTexture = SDL_CreateTexture(GameRenderer,
					SDL_PIXELFORMAT_RGB565,
					SDL_TEXTUREACCESS_STREAMING,
					SCREEN_WIDTH, SCREEN_HEIGHT);
	if (ScreenTexture == NULL) {
		SLOGE("SDL_CreateTexture for ScreenTexture failed: {}\n", SDL_GetError());
	}
}


static void DestroyTextures()
{
	for (SDL_Texture** t : { &ScreenTexture, &ScaledScreenTexture, &WorldTexture, &UiTexture, &CanvasTexture })
	{
		if (*t != NULL)
		{
			SDL_DestroyTexture(*t);
			*t = NULL;
		}
	}
}


void VideoSetTargetFPS(int32_t const fps)
{
	// 0 (or less): no cap
	TargetFPS = fps > 0 ? fps : 0;
	TimeBetweenRefreshScreens = fps > 0 ? std::chrono::microseconds{1'000'000} / fps : std::chrono::microseconds{0};
}


int32_t VideoGetTargetFPS()
{
	return TargetFPS;
}


VideoDisplaySettings const& VideoGetDisplaySettings()
{
	return CurrentSettings;
}


VideoScaleQuality VideoGetScaleQuality()
{
	return ScaleQuality;
}


static VideoLayout::Size WindowPixelSize()
{
	int pw = 0;
	int ph = 0;
	if (g_game_window) SDL_GetWindowSizeInPixels(g_game_window, &pw, &ph);
	return { pw, ph };
}


VideoLayout::LayerLayout VideoApplyWindow(VideoDisplaySettings const& want, VideoScaleQuality const quality, bool const resizeWindow)
{
	ScaleQuality = quality;
	CurrentSettings = want;

	VideoLayout::DisplayLayout display;
	if (sgp::IsHeadless())
	{
		// No window: -res is the canvas, and the UI scale only means something when the world is a layer of its own.
		bool const layered = want.worldZoom != VideoLayout::WORLD_ZOOM_MATCH_UI;
		int const su = layered && want.uiScale != VideoLayout::UI_SCALE_AUTO ? want.uiScale : 1;
		VideoLayout::Size const window{ want.resX > 0 ? want.resX : SCREEN_WIDTH, want.resY > 0 ? want.resY : SCREEN_HEIGHT };
		display = VideoLayout::ComputeDisplayLayout(window, su);
		return VideoLayout::ComputeLayerLayout(display, want.worldZoom);
	}

	if (g_game_window && resizeWindow)
	{
		auto const desktop{ QueryDesktop() };
		auto const toPoints = [&](int px) { return static_cast<int>(px / desktop.density + 0.5f); };
		auto const flags = SDL_GetWindowFlags(g_game_window);
		switch (want.windowMode)
		{
			case WindowMode::Windowed:
			{
				if (flags & SDL_WINDOW_FULLSCREEN)
				{
					VideoSetFullScreen(FALSE);
					SDL_SyncWindow(g_game_window);
				}
				auto const size{ WindowBasis(want, desktop.pixels) };
				SDL_SetWindowSize(g_game_window, toPoints(size.w), toPoints(size.h));
				SDL_SyncWindow(g_game_window);
				SDL_SetWindowPosition(g_game_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
				SDL_SyncWindow(g_game_window);
				break;
			}

			case WindowMode::Fullscreen:
			case WindowMode::BorderlessDesktop:
			{
				SDL_DisplayMode mode;
				if (want.windowMode == WindowMode::Fullscreen && want.resX > 0 && want.resY > 0 &&
				    SDL_GetClosestFullscreenDisplayMode(desktop.display, toPoints(want.resX), toPoints(want.resY), 0.0f, true, &mode))
				{
					SDL_SetWindowFullscreenMode(g_game_window, &mode);
				}
				else
				{
					SDL_SetWindowFullscreenMode(g_game_window, nullptr); // the desktop mode
				}
				VideoSetFullScreen(TRUE);
				SDL_SyncWindow(g_game_window);
				break;
			}
		}
	}

	// What the window really is now (the desktop can limit a request)
	auto const actual{ WindowPixelSize() };
	SLOGI("Video change: window {}x{} px, UI scale {}, world zoom {}", actual.w, actual.h, want.uiScale, want.worldZoom);
	display = VideoLayout::ComputeDisplayLayout(actual, want.uiScale);
	return VideoLayout::ComputeLayerLayout(display, want.worldZoom);
}


bool VideoWindowSizeChanged()
{
	if (sgp::IsHeadless() || !g_game_window) return false;
	if (CurrentSettings.uiScale != VideoLayout::UI_SCALE_AUTO) return false;
	auto const actual{ WindowPixelSize() };
	if (actual.w <= 0 || actual.h <= 0) return false;
	auto const display = VideoLayout::ComputeDisplayLayout(actual, VideoLayout::UI_SCALE_AUTO);
	auto const layers = VideoLayout::ComputeLayerLayout(display, CurrentSettings.worldZoom);
	return layers.ui.w != SCREEN_WIDTH || layers.ui.h != SCREEN_HEIGHT
		|| layers.uiScale != g_ui.m_uiScale || layers.worldZoom != g_ui.m_worldZoom;
}


void VideoRebuildBuffers()
{
	Layered = g_ui.isLayered();

	DestroyTextures();
	SharpBilinearScale = 0;
	PresentedWindowSize = { 0, 0 };

	// The wrappers stay where they are: the whole game holds pointers to them.
	ScreenBuffer = g_back_buffer->Resize(SCREEN_WIDTH, SCREEN_HEIGHT);
	FrameBuffer = g_frame_buffer->Resize(SCREEN_WIDTH, SCREEN_HEIGHT);

	if (Layered)
	{
		if (WorldBuffer)
		{
			WorldBuffer = g_world_buffer->Resize(WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT);
		}
		else
		{
			WorldBuffer = SDL_CreateSurface(WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT, SDL_PIXELFORMAT_RGB565);
			if (!WorldBuffer) throw std::runtime_error("Failed to create the world buffer");
			g_world_buffer = new SGPVSurface(WorldBuffer);
		}
		WorldDirty = { 0, 0, WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT };
	}
	else if (WorldBuffer)
	{
		delete g_world_buffer;
		g_world_buffer = g_frame_buffer;
		WorldBuffer = nullptr;
		WorldDirty = { 0, 0, 0, 0 };
	}
	WorldLayerShown = false;

	ClippingRect.set(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	MouseBackground = { 0, 0, 0, 0 };
	guiDirtyRegionCount = 0;
	guiDirtyRegionExCount = 0;
	gfForceFullScreenRefresh = TRUE;

	if (GameRenderer)
	{
		CreateTextures();
		UpdatePresentation(true);
	}
}


/** Chooses how the canvas is put on the window, whenever the window's pixel size changes.
 *  - PERFECT: integer scale, letterbox around it.
 *  - LINEAR: fractional scale with linear filtering.
 *  - NEAR_PERFECT ("sharp bilinear"): when the window is an exact multiple of the canvas
 *    (what the UI scale logic produces) that is an integer scale; otherwise nearest-neighbour
 *    up to the largest integer multiple that fits, then linear to the final size. */
static void UpdatePresentation(bool const force)
{
	// What is put on the window: the canvas with both layers, or the one surface
	SDL_Texture* const presented = Layered ? CanvasTexture : ScreenTexture;
	if (!g_game_window || !GameRenderer || !presented) return;

	int pw = 0;
	int ph = 0;
	SDL_GetWindowSizeInPixels(g_game_window, &pw, &ph);
	VideoLayout::Size const window{ pw, ph };
	if (!force && window == PresentedWindowSize) return;
	PresentedWindowSize = window;

	auto const p = VideoLayout::ComputePresentation(window, { SCREEN_WIDTH, SCREEN_HEIGHT });

	SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_LETTERBOX;
	SDL_ScaleMode filter = SDL_SCALEMODE_LINEAR;
	SharpBilinearScale = 0;

	switch (ScaleQuality)
	{
		case VideoScaleQuality::PERFECT:
			filter = SDL_SCALEMODE_NEAREST;
			if (p.k >= 1) mode = SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
			break;

		case VideoScaleQuality::NEAR_PERFECT:
			if (p.integerFit)
			{
				filter = SDL_SCALEMODE_NEAREST;
				mode = SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
			}
			else if (p.k >= 2)
			{
				filter = SDL_SCALEMODE_NEAREST; // into the intermediate texture
				SharpBilinearScale = p.k;
			}
			break;

		default:
			break;
	}

	if (Layered)
	{
		// The canvas already has the UI scale (and the world its own) baked in: it is the size of the
		// window, so all that is left is putting it on it without blurring.
		SharpBilinearScale = 0;
		if (ScaleQuality != VideoScaleQuality::PERFECT)
		{
			filter = p.integerFit ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
		}
	}

	SDL_SetRenderLogicalPresentation(GameRenderer, SCREEN_WIDTH, SCREEN_HEIGHT, mode);
	SDL_SetTextureScaleMode(presented, filter);

	if (SharpBilinearScale > 0)
	{
		int const w = SCREEN_WIDTH * SharpBilinearScale;
		int const h = SCREEN_HEIGHT * SharpBilinearScale;
		if (!ScaledScreenTexture || ScaledScreenTexture->w != w || ScaledScreenTexture->h != h)
		{
			if (ScaledScreenTexture) SDL_DestroyTexture(ScaledScreenTexture);
			ScaledScreenTexture = SDL_CreateTexture(GameRenderer,
				SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
			if (ScaledScreenTexture)
			{
				SDL_SetTextureScaleMode(ScaledScreenTexture, SDL_SCALEMODE_LINEAR);
			}
			else
			{
				SLOGE("SDL_CreateTexture for ScaledScreenTexture failed: {}", SDL_GetError());
				SharpBilinearScale = 0;
				SDL_SetTextureScaleMode(ScreenTexture, SDL_SCALEMODE_LINEAR);
			}
		}
	}
}


static void CreateSoftwareSurfaces()
{
	ClippingRect.set(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	ScreenBuffer = SDL_CreateSurface(
					SCREEN_WIDTH,
					SCREEN_HEIGHT,
					SDL_PIXELFORMAT_RGB565
	);

	if (ScreenBuffer == NULL) {
		SLOGE("SDL_CreateRGBSurface for ScreenBuffer failed: {}\n", SDL_GetError());
	}

	FrameBuffer = SDL_CreateSurface(
		SCREEN_WIDTH,
		SCREEN_HEIGHT,
		SDL_PIXELFORMAT_RGB565
	);

	if (FrameBuffer == NULL)
	{
		SLOGE("SDL_CreateRGBSurface for FrameBuffer failed: {}\n", SDL_GetError());
	}

	if (Layered)
	{
		WorldBuffer = SDL_CreateSurface(
			WORLD_SCREEN_WIDTH,
			WORLD_SCREEN_HEIGHT,
			SDL_PIXELFORMAT_RGB565
		);

		if (WorldBuffer == NULL)
		{
			SLOGE("SDL_CreateRGBSurface for WorldBuffer failed: {}\n", SDL_GetError());
		}
		WorldDirty = { 0, 0, WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT };
	}

	MouseCursor = SDL_CreateSurface(
		MAX_CURSOR_WIDTH,
		MAX_CURSOR_HEIGHT,
		SDL_PIXELFORMAT_RGB565
	);
	SDL_SetSurfaceColorKey(MouseCursor, true, 0);

	if (MouseCursor == NULL)
	{
		SLOGE("SDL_CreateRGBSurface for MouseCursor failed: {}\n", SDL_GetError());
	}

	// Initialize state variables
	gfForceFullScreenRefresh = TRUE;

	// This function must be called to setup RGB information
	GetRGBDistribution();
}


void ShutdownVideoManager(void)
{
	// ScreenBuffer SDL surface freed by its SGPVSurface wrapper.
	ScreenBuffer = nullptr;

	if (ScreenTexture != NULL) {
		SDL_DestroyTexture(ScreenTexture);
		ScreenTexture = NULL;
	}

	if (ScaledScreenTexture != NULL) {
		SDL_DestroyTexture(ScaledScreenTexture);
		ScaledScreenTexture = NULL;
	}

	for (SDL_Texture** t : { &WorldTexture, &UiTexture, &CanvasTexture })
	{
		if (*t != NULL)
		{
			SDL_DestroyTexture(*t);
			*t = NULL;
		}
	}
	// WorldBuffer is freed by its SGPVSurface wrapper, like ScreenBuffer.
	WorldBuffer = nullptr;
	Layered = false;

	if (GameRenderer != NULL) {
		SDL_DestroyRenderer(GameRenderer);
		GameRenderer = NULL;
	}

	if (g_game_window != NULL) {
		SDL_DestroyWindow(g_game_window);
		g_game_window = NULL;
	}

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}


namespace {
void AddToGivenRegionsList(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom,
	decltype(DirtyRegions) & regions,
	decltype(guiDirtyRegionCount) & regionCount)
{
	if (gfForceFullScreenRefresh)
	{
		// There's no point in going on since we are forcing a full screen refresh
		return;
	}

	if (regionCount < MAX_DIRTY_REGIONS)
	{
		// Well we haven't broken the MAX_DIRTY_REGIONS limit yet, so we register the new region

		// DO SOME PREMIMARY CHECKS FOR VALID RECTS
		if (iLeft < 0) iLeft = 0;
		if (iTop  < 0) iTop  = 0;

		if (iRight  > SCREEN_WIDTH)  iRight  = SCREEN_WIDTH;
		if (iBottom > SCREEN_HEIGHT) iBottom = SCREEN_HEIGHT;

		if (iRight - iLeft <= 0) return;
		if (iBottom - iTop <= 0) return;

		auto & newRegion{ regions[regionCount] };

		newRegion.x = iLeft;
		newRegion.y = iTop;
		newRegion.w = iRight  - iLeft;
		newRegion.h = iBottom - iTop;
		++regionCount;
	}
	else
	{
		// The MAX_DIRTY_REGIONS limit has been exceeded. Therefore we arbitrarely invalidate the entire
		// screen and force a full screen refresh
		InvalidateScreen();
	}
}

void AddRegionEx(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom)
{
	AddToGivenRegionsList(iLeft, iTop, iRight, iBottom, DirtyRegionsEx, guiDirtyRegionExCount);
}
}

void InvalidateRegion(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom)
{
	AddToGivenRegionsList(iLeft, iTop, iRight, iBottom, DirtyRegions, guiDirtyRegionCount);
}



void InvalidateRegionEx(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom)
{
	if (gfForceFullScreenRefresh)
	{
		// There's no point in going on since we are forcing a full screen refresh
		return;
	}

	// Check if we are spanning the rectangle - if so slit it up!
	if (iTop <= gsVIEWPORT_WINDOW_END_Y && iBottom > gsVIEWPORT_WINDOW_END_Y)
	{
		// Add new top region
		AddRegionEx(iLeft, iTop, iRight, gsVIEWPORT_WINDOW_END_Y);

		// Add new bottom region
		AddRegionEx(iLeft, gsVIEWPORT_WINDOW_END_Y, iRight, iBottom);
	}
	else
	{
		AddRegionEx(iLeft, iTop, iRight, iBottom);
	}
}


void InvalidateScreen(void)
{
	guiDirtyRegionCount = 0;
	guiDirtyRegionExCount = 0;
	gfForceFullScreenRefresh = TRUE;
}


void InvalidateWorldRegion(INT32 const iLeft, INT32 const iTop, INT32 const iRight, INT32 const iBottom)
{
	if (!Layered)
	{
		InvalidateRegionEx(iLeft, iTop, iRight, iBottom);
		return;
	}

	SDL_Rect const bounds{ 0, 0, WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT };
	SDL_Rect const region{ iLeft, iTop, iRight - iLeft, iBottom - iTop };
	SDL_Rect clipped;
	if (!SDL_GetRectIntersection(&region, &bounds, &clipped)) return;
	if (WorldDirty.w <= 0 || WorldDirty.h <= 0) WorldDirty = clipped;
	else SDL_GetRectUnion(&WorldDirty, &clipped, &WorldDirty);
}


/* ---- The layers ------------------------------------------------------------------------------ */

static void BuildUiLayerLut()
{
	UiLayerLut.resize(65536);
	for (uint32_t p = 0; p < 65536; ++p)
	{
		if (UiLayerIsTransparentFamily(static_cast<UINT16>(p)))
		{
			// black with an alpha; the transparent colour itself has none
			UiLayerLut[p] = static_cast<uint32_t>(UiLayerShadowAlpha(static_cast<UINT16>(p))) << 24;
			continue;
		}
		uint32_t const r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
		UiLayerLut[p] = 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
	}
}


static void CreateLayerTextures()
{
	int const su = g_ui.m_uiScale;
	WorldTexture = SDL_CreateTexture(GameRenderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
		WorldBuffer->w, WorldBuffer->h);
	UiTexture = SDL_CreateTexture(GameRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		SCREEN_WIDTH, SCREEN_HEIGHT);
	CanvasTexture = SDL_CreateTexture(GameRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
		SCREEN_WIDTH * su, SCREEN_HEIGHT * su);
	if (!WorldTexture || !UiTexture || !CanvasTexture)
	{
		SLOGE("Creating the layer textures failed: {}", SDL_GetError());
		return;
	}
	// Each layer is made big with sharp pixels; the canvas carries the result to the window.
	SDL_SetTextureScaleMode(WorldTexture, SDL_SCALEMODE_NEAREST);
	SDL_SetTextureScaleMode(UiTexture, SDL_SCALEMODE_NEAREST);
	SDL_SetTextureBlendMode(UiTexture, SDL_BLENDMODE_BLEND);
	BuildUiLayerLut();
}


/** Upload a part of the UI layer, turning the transparent colours into alpha */
static void UploadUiLayer(SDL_Rect const& r)
{
	void* pixels;
	int pitch;
	if (!SDL_LockTexture(UiTexture, &r, &pixels, &pitch)) return;
	for (int y = 0; y < r.h; ++y)
	{
		auto const* src = reinterpret_cast<UINT16 const*>(
			static_cast<UINT8 const*>(ScreenBuffer->pixels) + (r.y + y) * ScreenBuffer->pitch) + r.x;
		auto* dst = reinterpret_cast<uint32_t*>(static_cast<UINT8*>(pixels) + y * pitch);
		for (int x = 0; x < r.w; ++x) dst[x] = UiLayerLut[src[x]];
	}
	SDL_UnlockTexture(UiTexture);
}


static void UploadWorldLayer()
{
	if (WorldDirty.w <= 0 || WorldDirty.h <= 0) return;
	SDL_Rect const r{ ClipToSurface(WorldDirty, WorldBuffer) };
	WorldDirty = { 0, 0, 0, 0 };
	if (r.w <= 0 || r.h <= 0) return;
	auto const* src = static_cast<uint8_t const*>(WorldBuffer->pixels) + r.y * WorldBuffer->pitch + r.x * 2;
	SDL_UpdateTexture(WorldTexture, &r, src, WorldBuffer->pitch);
}


/** Which screens show the tactical world: those that are drawn over it. The others are opaque, full
 * screen UI, and the world is left where it is for when they go away. */
static void UpdateWorldLayerVisibility()
{
	switch (guiCurrentScreen)
	{
		case GAME_SCREEN:
		case EDIT_SCREEN:
			WorldLayerShown = true;
			break;

		case MSG_BOX_SCREEN:
		case FADE_SCREEN:
		case ERROR_SCREEN:
		case DEBUG_SCREEN:
			break; // over whatever was there

		default:
			WorldLayerShown = false;
			break;
	}
}


static uint32_t World565To888(UINT16 const p)
{
	uint32_t const r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
	return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}


/** The colour at a pixel of the canvas (UI size * UI scale) */
static uint32_t ComposeAt(int const x, int const y)
{
	int const su = g_ui.m_uiScale;
	int const zw = g_ui.m_worldZoom;
	int const ux = std::clamp(x / su, 0, ScreenBuffer->w - 1);
	int const uy = std::clamp(y / su, 0, ScreenBuffer->h - 1);
	UINT16 const ui = reinterpret_cast<UINT16 const*>(
		static_cast<UINT8 const*>(ScreenBuffer->pixels) + uy * ScreenBuffer->pitch)[ux];
	if (!UiLayerIsTransparentFamily(ui)) return World565To888(ui);

	uint32_t under = 0;
	if (WorldLayerShown)
	{
		int const wx = std::clamp(x / zw, 0, WorldBuffer->w - 1);
		int const wy = std::clamp(y / zw, 0, WorldBuffer->h - 1);
		under = World565To888(reinterpret_cast<UINT16 const*>(
			static_cast<UINT8 const*>(WorldBuffer->pixels) + wy * WorldBuffer->pitch)[wx]);
	}
	uint32_t const keep = 255 - UiLayerShadowAlpha(ui);
	return (((under >> 16) & 0xff) * keep / 255) << 16
	     | (((under >>  8) & 0xff) * keep / 255) <<  8
	     |  ((under        & 0xff) * keep / 255);
}


/** ComposeAt with the software overlay (native UI) blended on top. */
static uint32_t ComposeWithOverlay(int const x, int const y)
{
	uint32_t const c = ComposeAt(x, y);
	if (!Overlay || OverlayArea.w <= 0) return c;
	int const su = g_ui.m_uiScale;
	uint32_t const o = Overlay->SoftwarePixel(std::clamp(x / su, 0, ScreenBuffer->w - 1), std::clamp(y / su, 0, ScreenBuffer->h - 1));
	uint32_t const a = o >> 24;
	if (a == 0) return c;
	auto const ch = [&](int shift) {
		uint32_t const v = ((o >> shift) & 0xff) + ((c >> shift) & 0xff) * (255 - a) / 255;
		return std::min<uint32_t>(v, 255) << shift;
	};
	return ch(16) | ch(8) | ch(0);
}


void VideoSetOverlay(VideoOverlay* const overlay)
{
	Overlay = overlay;
	OverlayArea = { 0, 0, 0, 0 };
	gfForceFullScreenRefresh = TRUE;
}


VideoOutputMapping VideoGetOutputMapping()
{
	VideoOutputMapping m{ 1, 1, 0, 0, ScreenBuffer ? ScreenBuffer->w : SCREEN_WIDTH, ScreenBuffer ? ScreenBuffer->h : SCREEN_HEIGHT, false };
	if (!GameRenderer || !g_game_window || !Overlay || !Overlay->UsesGpu()) return m;
	int pw = 0, ph = 0;
	SDL_GetWindowSizeInPixels(g_game_window, &pw, &ph);
	SDL_FRect r{};
	if (!SDL_GetRenderLogicalPresentationRect(GameRenderer, &r) || r.w <= 0 || r.h <= 0) r = { 0, 0, float(pw), float(ph) };
	m = { r.w / SCREEN_WIDTH, r.h / SCREEN_HEIGHT, r.x, r.y, pw, ph, true };
	return m;
}


static bool                 OutputCaptureWanted = false;
static std::vector<uint8_t> OutputCapture;
static int                  OutputCaptureW = 0, OutputCaptureH = 0;

/** Reads the finished output back from the GPU before it is presented (VideoRequestOutputCapture). */
static void CaptureOutputIfRequested()
{
	if (!OutputCaptureWanted || !GameRenderer) return;
	OutputCaptureWanted = false;
	int lw = 0, lh = 0;
	SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
	SDL_GetRenderLogicalPresentation(GameRenderer, &lw, &lh, &mode);
	SDL_SetRenderLogicalPresentation(GameRenderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
	SDL_Surface* s = SDL_RenderReadPixels(GameRenderer, nullptr);
	SDL_SetRenderLogicalPresentation(GameRenderer, lw, lh, mode);
	if (!s) return;
	SDL_Surface* rgb = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGB24);
	SDL_DestroySurface(s);
	if (!rgb) return;
	OutputCaptureW = rgb->w;
	OutputCaptureH = rgb->h;
	OutputCapture.resize(size_t(rgb->w) * rgb->h * 3);
	for (int y = 0; y < rgb->h; ++y)
		std::memcpy(&OutputCapture[size_t(y) * rgb->w * 3], static_cast<uint8_t const*>(rgb->pixels) + y * rgb->pitch, size_t(rgb->w) * 3);
	SDL_DestroySurface(rgb);
}

void VideoRequestOutputCapture()
{
	OutputCaptureWanted = true;
	OutputCapture.clear();
}

bool VideoTakeOutputCapture(std::vector<uint8_t>& rgb, int& w, int& h)
{
	if (OutputCapture.empty()) return false;
	rgb.swap(OutputCapture);
	w = OutputCaptureW;
	h = OutputCaptureH;
	OutputCapture.clear();
	return true;
}

/** GPU path: the overlay in window pixels, over whatever was presented. */
static void RenderOverlayGpu()
{
	if (!Overlay || !Overlay->UsesGpu()) return;
	int lw = 0, lh = 0;
	SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
	SDL_GetRenderLogicalPresentation(GameRenderer, &lw, &lh, &mode);
	SDL_SetRenderLogicalPresentation(GameRenderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
	Overlay->GpuRender(GameRenderer);
	SDL_SetRenderClipRect(GameRenderer, nullptr);
	SDL_SetRenderLogicalPresentation(GameRenderer, lw, lh, mode);
}


bool VideoComposeFrame(std::vector<uint8_t>& rgb, int& width, int& height)
{
	if (!Layered || !ScreenBuffer || !WorldBuffer) return false;
	width  = ScreenBuffer->w * g_ui.m_uiScale;
	height = ScreenBuffer->h * g_ui.m_uiScale;
	rgb.resize(size_t(width) * height * 3);
	for (int y = 0; y < height; ++y)
	{
		uint8_t* d = &rgb[size_t(y) * width * 3];
		for (int x = 0; x < width; ++x, d += 3)
		{
			uint32_t const c = ComposeWithOverlay(x, y);
			d[0] = c >> 16; d[1] = (c >> 8) & 0xff; d[2] = c & 0xff;
		}
	}
	return true;
}


uint32_t VideoComposePixel(int const uiX, int const uiY)
{
	int const su = g_ui.m_uiScale;
	return ComposeWithOverlay(uiX * su + su / 2, uiY * su + su / 2);
}


/* Clip an SDL Rect to the SDL_Surface. This was previously done automatically by SDL_BlitSurface */
static SDL_Rect ClipToSurface(SDL_Rect const& rect, SDL_Surface const* const surface)
{
	SDL_Rect const bounds{ 0, 0, surface->w, surface->h };
	SDL_Rect clipped;
	// On an empty intersection SDL leaves a rect with a negative extent behind.
	if (!SDL_GetRectIntersection(&rect, &bounds, &clipped)) return SDL_Rect{ 0, 0, 0, 0 };
	return clipped;
}


//#define SCROLL_TEST

static void ScrollJA2Background(INT16 sScrollXIncrement, INT16 sScrollYIncrement)
{
	SDL_Surface* Dest   = ScreenBuffer; // Back
	SDL_Rect     SrcRect;
	SDL_Rect     DstRect;
	SDL_Rect     StripRegions[2];
	UINT16       NumStrips = 0;

	int const width  = SCREEN_WIDTH;
	int const height = gsVIEWPORT_WINDOW_END_Y - gsVIEWPORT_WINDOW_START_Y;

	if (sScrollXIncrement < 0)
	{
		SrcRect.x = 0;
		SrcRect.w = width + sScrollXIncrement;
		DstRect.x = -sScrollXIncrement;
		StripRegions[0].x = gsVIEWPORT_START_X;
		StripRegions[0].y = gsVIEWPORT_WINDOW_START_Y;
		StripRegions[0].w = -sScrollXIncrement;
		StripRegions[0].h = height;
		++NumStrips;
	}
	else if (sScrollXIncrement > 0)
	{
		SrcRect.x = sScrollXIncrement;
		SrcRect.w = width - sScrollXIncrement;
		DstRect.x = 0;
		StripRegions[0].x = gsVIEWPORT_END_X - sScrollXIncrement;
		StripRegions[0].y = gsVIEWPORT_WINDOW_START_Y;
		StripRegions[0].w = sScrollXIncrement;
		StripRegions[0].h = height;
		++NumStrips;
	}
	else
	{
		SrcRect.x = 0;
		SrcRect.w = width;
		DstRect.x = 0;
	}

	if (sScrollYIncrement < 0)
	{
		SrcRect.y = gsVIEWPORT_WINDOW_START_Y;
		SrcRect.h = height + sScrollYIncrement;
		DstRect.y = gsVIEWPORT_WINDOW_START_Y - sScrollYIncrement;
		StripRegions[NumStrips].x = DstRect.x;
		StripRegions[NumStrips].y = gsVIEWPORT_WINDOW_START_Y;
		StripRegions[NumStrips].w = SrcRect.w;
		StripRegions[NumStrips].h = -sScrollYIncrement;
		++NumStrips;
	}
	else if (sScrollYIncrement > 0)
	{
		SrcRect.y = gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement;
		SrcRect.h = height - sScrollYIncrement;
		DstRect.y = gsVIEWPORT_WINDOW_START_Y;
		StripRegions[NumStrips].x = DstRect.x;
		StripRegions[NumStrips].y = gsVIEWPORT_WINDOW_END_Y - sScrollYIncrement;
		StripRegions[NumStrips].w = SrcRect.w;
		StripRegions[NumStrips].h = sScrollYIncrement;
		++NumStrips;
	}
	else
	{
		SrcRect.y = gsVIEWPORT_WINDOW_START_Y;
		SrcRect.h = height;
		DstRect.y = gsVIEWPORT_WINDOW_START_Y;
	}

#ifdef SCROLL_TEST
	SDL_FillRect(Dest, NULL, 0);
#endif

	SDL_BlitSurface(Dest, &SrcRect, Dest, &DstRect);

	for (UINT i = 0; i < NumStrips; i++)
	{
		INT16 const x = static_cast<INT16>(StripRegions[i].x);
		INT16 const y = static_cast<INT16>(StripRegions[i].y);
		INT16 const w = static_cast<INT16>(StripRegions[i].w);
		INT16 const h = static_cast<INT16>(StripRegions[i].h);
		for (int j = y; j < y + h; ++j)
		{
			std::fill_n(gpZBuffer + j * SCREEN_WIDTH + x, w, 0);
		}

		RenderStaticWorldRect(x, y, x + w, y + h, TRUE);
		SDL_BlitSurface(FrameBuffer, &StripRegions[i], Dest, &StripRegions[i]);
	}

	// RESTORE SHIFTED
	RestoreShiftedVideoOverlays(sScrollXIncrement, sScrollYIncrement);

	// SAVE NEW
	SaveVideoOverlaysArea(BACKBUFFER);

	// BLIT NEW
	ExecuteVideoOverlaysToAlternateBuffer(BACKBUFFER);
}

/** Scrolling with layers: the world is a surface of its own that nothing else draws into, so it is simply
 * moved, and the strip that comes into view is rendered. The UI is a layer above, it stays where it is. */
static void ScrollWorldBuffer(INT16 const sScrollXIncrement, INT16 const sScrollYIncrement)
{
	SDL_Surface* const world = WorldBuffer;
	int const startY = gsWORLD_VIEWPORT_WINDOW_START_Y;
	int const endY   = gsWORLD_VIEWPORT_WINDOW_END_Y;
	int const startX = gsWORLD_VIEWPORT_START_X;
	int const endX   = gsWORLD_VIEWPORT_END_X;

	SDL_Rect SrcRect{ startX, startY, endX - startX, endY - startY };
	SDL_Rect DstRect{ startX, startY, 0, 0 };
	SDL_Rect StripRegions[2];
	int NumStrips = 0;

	if (sScrollXIncrement < 0)
	{
		SrcRect.w += sScrollXIncrement;
		DstRect.x  = startX - sScrollXIncrement;
		StripRegions[NumStrips++] = { startX, startY, -sScrollXIncrement, endY - startY };
	}
	else if (sScrollXIncrement > 0)
	{
		SrcRect.x += sScrollXIncrement;
		SrcRect.w -= sScrollXIncrement;
		StripRegions[NumStrips++] = { endX - sScrollXIncrement, startY, sScrollXIncrement, endY - startY };
	}

	if (sScrollYIncrement < 0)
	{
		SrcRect.h += sScrollYIncrement;
		DstRect.y  = startY - sScrollYIncrement;
		StripRegions[NumStrips++] = { DstRect.x, startY, SrcRect.w, -sScrollYIncrement };
	}
	else if (sScrollYIncrement > 0)
	{
		SrcRect.y += sScrollYIncrement;
		SrcRect.h -= sScrollYIncrement;
		StripRegions[NumStrips++] = { DstRect.x, endY - sScrollYIncrement, SrcRect.w, sScrollYIncrement };
	}

	SDL_BlitSurface(world, &SrcRect, world, &DstRect);

	int const zPitch = gZBufferPitch / sizeof(*gpZBuffer);
	for (int i = 0; i < NumStrips; i++)
	{
		INT16 const x = static_cast<INT16>(StripRegions[i].x);
		INT16 const y = static_cast<INT16>(StripRegions[i].y);
		INT16 const w = static_cast<INT16>(StripRegions[i].w);
		INT16 const h = static_cast<INT16>(StripRegions[i].h);
		for (int j = y; j < y + h; ++j)
		{
			std::fill_n(gpZBuffer + j * zPitch + x, w, 0);
		}

		// draws into the WORLD_BUFFER, which is WorldBuffer
		RenderStaticWorldRect(x, y, x + w, y + h, TRUE);
	}

	WorldDirty = { startX, startY, endX - startX, endY - startY };
}


/** Upload what changed of each layer, put them on the canvas (world first, the UI over it) and the canvas
 * on the window. */
static void PresentLayers(SDL_Rect const& uiUpdate)
{
	if (uiUpdate.w > 0 && uiUpdate.h > 0) UploadUiLayer(uiUpdate);
	if (WorldLayerShown) UploadWorldLayer();

	UpdatePresentation(false);

	int const su = g_ui.m_uiScale;
	int const zw = g_ui.m_worldZoom;
	SDL_SetRenderTarget(GameRenderer, CanvasTexture);
	SDL_SetRenderDrawColor(GameRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(GameRenderer);
	if (WorldLayerShown)
	{
		SDL_FRect const dst{ 0, 0, float(WorldBuffer->w * zw), float(WorldBuffer->h * zw) };
		SDL_RenderTexture(GameRenderer, WorldTexture, nullptr, &dst);
	}
	SDL_FRect const uiDst{ 0, 0, float(ScreenBuffer->w * su), float(ScreenBuffer->h * su) };
	SDL_RenderTexture(GameRenderer, UiTexture, nullptr, &uiDst);
	SDL_SetRenderTarget(GameRenderer, nullptr);

	SDL_SetRenderDrawColor(GameRenderer, LETTERBOX_COLOR, LETTERBOX_COLOR, LETTERBOX_COLOR, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(GameRenderer);
	SDL_RenderTexture(GameRenderer, CanvasTexture, nullptr, nullptr);
}


void RefreshScreen(void)
{
	// Not initialised yet or already shut down?
	if (!ScreenBuffer) return;

	sgp::Clock::OnPresent();

	BOOLEAN scrolling = (gsScrollXIncrement != 0 || gsScrollYIncrement != 0);

	if (Layered)
	{
		UpdateWorldLayerVisibility();
		if (gfForceFullScreenRefresh) WorldDirty = { 0, 0, WorldBuffer->w, WorldBuffer->h };
		if (scrolling)
		{
			// Only the world moves; the rest of this is about the UI layer
			ScrollWorldBuffer(gsScrollXIncrement, gsScrollYIncrement);
			gsScrollXIncrement = 0;
			gsScrollYIncrement = 0;
			scrolling = FALSE;
		}
	}

	SDL_BlitSurface(FrameBuffer, &MouseBackground, ScreenBuffer, &MouseBackground);

	// This variable will hold the union of all modified regions.
	struct rect : SDL_Rect {
		void operator+=(SDL_Rect const& r) { if (r.w > 0 && r.h > 0) SDL_GetRectUnion(this, &r, this); }
	} ScreenTextureUpdateRect{ MouseBackground };

	// Software overlay (native UI): what it covered last frame and covers now is restored from the frame
	// buffer, so it is blended over the legacy picture exactly once.
	SDL_Rect overlayNow{ 0, 0, 0, 0 };
	bool const softOverlay = Overlay && !Overlay->UsesGpu();
	if (softOverlay)
	{
		overlayNow = Overlay->SoftwarePrepare();
		if (!Layered)
		{
			for (SDL_Rect r : { OverlayArea, overlayNow })
			{
				if (r.w <= 0 || r.h <= 0) continue;
				SDL_BlitSurface(FrameBuffer, &r, ScreenBuffer, &r);
				ScreenTextureUpdateRect += r;
			}
		}
	}

	if (gfForceFullScreenRefresh || guiDirtyRegionCount > 0 || guiDirtyRegionExCount > 0)
	{
		if (gfForceFullScreenRefresh)
		{
			SDL_BlitSurface(FrameBuffer, NULL, ScreenBuffer, NULL);
			ScreenTextureUpdateRect = { 0, 0, ScreenBuffer->w, ScreenBuffer->h };
		}
		else
		{
			for (UINT32 i = 0; i < guiDirtyRegionCount; i++)
			{
				ScreenTextureUpdateRect += DirtyRegions[i];
				SDL_BlitSurface(FrameBuffer, &DirtyRegions[i], ScreenBuffer, &DirtyRegions[i]);
			}

			for (UINT32 i = 0; i < guiDirtyRegionExCount; i++)
			{
				SDL_Rect* r = &DirtyRegionsEx[i];
				if (scrolling)
				{
					// Check if we are completely out of bounds
					if (r->y <= gsVIEWPORT_WINDOW_END_Y && r->y + r->h <= gsVIEWPORT_WINDOW_END_Y)
					{
						continue;
					}
				}
				ScreenTextureUpdateRect += *r;
				SDL_BlitSurface(FrameBuffer, r, ScreenBuffer, r);
			}
		}
		if (scrolling)
		{
			ScrollJA2Background(gsScrollXIncrement, gsScrollYIncrement);
			gsScrollXIncrement = 0;
			gsScrollYIncrement = 0;
			ScreenTextureUpdateRect += SDL_Rect{
				gsVIEWPORT_START_X, gsVIEWPORT_WINDOW_START_Y,
				gsVIEWPORT_END_X - gsVIEWPORT_START_X,
				gsVIEWPORT_WINDOW_END_Y - gsVIEWPORT_WINDOW_START_Y };
		}
		gfIgnoreScrollDueToCenterAdjust = FALSE;
	}

	if (softOverlay && !Layered && overlayNow.w > 0 && overlayNow.h > 0)
	{
		Overlay->SoftwareCompose(ScreenBuffer, overlayNow);
		ScreenTextureUpdateRect += overlayNow;
	}
	if (softOverlay) OverlayArea = overlayNow;
	else OverlayArea = { 0, 0, 0, 0 };

	auto const cursorPos{ GetCursorPos() };
	SDL_Rect src;
	src.x = 0;
	src.y = 0;
	src.w = gusMouseCursorWidth;
	// gsMouseSizeYModifier can push the height past the cursor surface
	src.h = std::min<int>(gusMouseCursorHeight + gsMouseSizeYModifier, MouseCursor->h);
	// The cursor hot spot offset can move this off screen on any side.
	SDL_Rect const dst{
		cursorPos.iX - gsMouseCursorXOffset,
		cursorPos.iY - gsMouseCursorYOffset,
		src.w, src.h };
	bool const drawCursor = !(Overlay && Overlay->HidesLegacyCursor());
	if (drawCursor) SDL_BlitSurface(MouseCursor, &src, ScreenBuffer, &dst);

	// The part that actually ended up on screen
	SDL_Rect const blitted{ drawCursor ? ClipToSurface(dst, ScreenBuffer) : SDL_Rect{ 0, 0, 0, 0 } };
	ScreenTextureUpdateRect += blitted;
	MouseBackground = blitted;

	gfForceFullScreenRefresh = FALSE;
	guiDirtyRegionCount = 0;
	guiDirtyRegionExCount = 0;

	if (Layered)
	{
		// Headless: the layers are composed when someone wants to see them, see VideoComposeFrame()
		if (!CanvasTexture) return;
		PresentLayers(ClipToSurface(ScreenTextureUpdateRect, ScreenBuffer));
		RenderOverlayGpu();
		Visualizer::Render(GameRenderer);
		CaptureOutputIfRequested();
		FPS::RenderPresentPtr(GameRenderer);
		return;
	}

	// Headless: the composited ScreenBuffer is the final output.
	if (!ScreenTexture) return;

	SDL_Rect const UpdateRect{ ClipToSurface(ScreenTextureUpdateRect, ScreenBuffer) };
	if (UpdateRect.w > 0 && UpdateRect.h > 0)
	{
		uint8_t const * SrcPixels = static_cast<uint8_t *>(ScreenBuffer->pixels)
			+ UpdateRect.y * ScreenBuffer->pitch
			+ UpdateRect.x * SDL_GetPixelFormatDetails(ScreenBuffer->format)->bytes_per_pixel;
		SDL_UpdateTexture(ScreenTexture, &UpdateRect,
		                  SrcPixels, ScreenBuffer->pitch);
	}

	UpdatePresentation(false);

	SDL_SetRenderDrawColor(GameRenderer, LETTERBOX_COLOR, LETTERBOX_COLOR, LETTERBOX_COLOR, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(GameRenderer);

	if (SharpBilinearScale > 0 && ScaledScreenTexture)
	{
		SDL_SetRenderTarget(GameRenderer, ScaledScreenTexture);
		SDL_RenderTexture(GameRenderer, ScreenTexture, nullptr, nullptr);

		SDL_SetRenderTarget(GameRenderer, nullptr);
		SDL_RenderTexture(GameRenderer, ScaledScreenTexture, nullptr, nullptr);
	}
	else
	{
		SDL_RenderTexture(GameRenderer, ScreenTexture, NULL, NULL);
	}

	RenderOverlayGpu();
	Visualizer::Render(GameRenderer);

	CaptureOutputIfRequested();
	FPS::RenderPresentPtr(GameRenderer);
}


// This is a semi-private function that is supposed to be called only
// by GameLoop(). This is why is has external linkage but is not
// declared in Video.h.
void RefreshScreenCapped()
{
	static sgp::GameClock::time_point LastRefresh;

	auto const now{ sgp::GameClock::now() };
	// Under virtual time every frame is presented so that what a driver sees
	// (screenshots, pixel reads, on-screen text) is always the current frame.
	if (sgp::Clock::IsVirtual() || gsScrollXIncrement != 0 || gsScrollYIncrement != 0 ||
	    now - LastRefresh >= TimeBetweenRefreshScreens)
	{
		LastRefresh = now;
		RefreshScreen();
	}
}


static void GetRGBDistribution()
{
	auto f = *SDL_GetPixelFormatDetails(ScreenBuffer->format);

	UINT32          const  r = f.Rmask;
	UINT32          const  g = f.Gmask;
	UINT32          const  b = f.Bmask;

	/* Mask the highest bit of each component. This is used for alpha blending. */
	guiTranslucentMask = (r & r >> 1) | (g & g >> 1) | (b & b >> 1);

	gusRedMask   = static_cast<UINT16>(r);
	gusGreenMask = static_cast<UINT16>(g);
	gusBlueMask  = static_cast<UINT16>(b);

	gusRedShift   = f.Rshift - (8 - f.Rbits);
	gusGreenShift = f.Gshift - (8 - f.Gbits);
	gusBlueShift  = f.Bshift - (8 - f.Bbits);
}


void SetMouseCursorProperties(INT16 sOffsetX, INT16 sOffsetY, UINT16 usCursorHeight, UINT16 usCursorWidth)
{
	gsMouseCursorXOffset = sOffsetX;
	gsMouseCursorYOffset = sOffsetY;
	gusMouseCursorWidth  = usCursorWidth;
	gusMouseCursorHeight = usCursorHeight;
}


static void SetPrimaryVideoSurfaces(void)
{
	// Delete surfaces if they exist
	DeletePrimaryVideoSurfaces();

	g_back_buffer  = new SGPVSurface(ScreenBuffer);
	g_mouse_buffer = new SGPVSurface(MouseCursor);
	g_frame_buffer = new SGPVSurface(FrameBuffer);
	// Without layers the world is drawn into the same surface as everything else
	g_world_buffer = WorldBuffer ? new SGPVSurface(WorldBuffer) : g_frame_buffer;
}

static void DeletePrimaryVideoSurfaces(void)
{
	delete g_back_buffer;
	g_back_buffer = NULL;

	if (g_world_buffer != g_frame_buffer) delete g_world_buffer;
	g_world_buffer = NULL;

	delete g_frame_buffer;
	g_frame_buffer = NULL;

	delete g_mouse_buffer;
	g_mouse_buffer = NULL;
}

SGPVSurface* gpVSurfaceHead = 0;

void InitializeVideoSurfaceManager(void)
{
	//Shouldn't be calling this if the video surface manager already exists.
	//Call shutdown first...
	Assert(gpVSurfaceHead == NULL);
	gpVSurfaceHead = NULL;

	// Create primary and backbuffer from globals
	SetPrimaryVideoSurfaces();
}


void ShutdownVideoSurfaceManager(void)
{
	// Delete primary viedeo surfaces
	DeletePrimaryVideoSurfaces();

	while (gpVSurfaceHead)
	{
		delete gpVSurfaceHead;
	}
}
