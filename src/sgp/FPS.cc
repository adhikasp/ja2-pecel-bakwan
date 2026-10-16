#include "Font.h"
#include "FPS.h"
#include "OverlayImage.h"
#include "SDL_helpers.h"
#include "VideoGpu.h"
#include "SDL3/SDL.h"
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <chrono>
#include <memory>
#include <numeric>
#include <vector>
#include <string_theory/format>


using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace FPS
{

GameLoopFunc_t  ActualGameLoop; // the real game loop function
GameLoopFunc_t  GameLoopPtr;    // the function MainLoop will call
RenderPresent_t RenderPresentPtr{ SDL_RenderPresent };

std::unique_ptr<SDL_Surface, SDLDeleter> Surface;
std::unique_ptr<SDL_Texture, SDLDeleter> Texture;
SGPVObject * DisplayFont;

std::vector<Clock::duration> LastGameLoopDurations;
Clock::time_point TimeLastDisplayed;

unsigned FramesSinceLastDisplay;

// The overlay's position in canvas (SCREEN_WIDTH x SCREEN_HEIGHT) pixels, as it always was.
constexpr float FPS_X = 11, FPS_Y = 23;

// The overlay's own path to the SDL_GPU compositor.
SDL_GPUTexture* GpuTexture{};
int GpuTextureW{}, GpuTextureH{};
std::vector<uint8_t> GpuRgba;

// Frames the counter has actually been drawn on (either presentation path).
unsigned long long DrawnFrames{};

bool Enabled()
{
	return GameLoopPtr != ActualGameLoop;
}

unsigned long long Drawn()
{
	return DrawnFrames;
}

static void UpdateSurface()
{
	SetFontAttributes(DisplayFont, FONT_FCOLOR_WHITE);

	SDL_FillSurfaceRect(Surface.get(), nullptr, 0);

	auto * const pixels = static_cast<UINT16*>(Surface->pixels);

	MPrintBuffer(pixels, Surface->pitch, 0, 0,
		ST::format("{} FPS", FramesSinceLastDisplay));

	if (!LastGameLoopDurations.empty())
	{
		auto const averageLoopDuration =
			std::accumulate(
				LastGameLoopDurations.begin(),
				LastGameLoopDurations.end(),
				Clock::duration{0})
			/ LastGameLoopDurations.size();

		MPrintBuffer(pixels, Surface->pitch, 0, 12,
			ST::format("Game Loop: {} micros",
				std::chrono::duration_cast<std::chrono::microseconds>(averageLoopDuration).count()));
	}
}

/** Counts the frame, and once a second rebuilds the surface. Returns whether it rebuilt it. */
static bool Tick()
{
	++FramesSinceLastDisplay;

	auto const now = Clock::now();
	if (now > TimeLastDisplayed + 1s)
	{
		UpdateSurface();
		TimeLastDisplayed = now;
		FramesSinceLastDisplay = 0;
		LastGameLoopDurations.clear();
		return true;
	}
	return false;
}

bool RenderPresentHook(SDL_Renderer* const renderer)
{
	// We must copy the texture each frame otherwise it would flicker,
	// but we only update its content once per second to make it less noisy.
	if (Tick() || !Texture)
	{
		Texture.reset(SDL_CreateTextureFromSurface(renderer, Surface.get()));
	}

	if (Texture)
	{
		SDL_FRect const dest{ FPS_X, FPS_Y, float(Surface->w), float(Surface->h) };
		SDL_RenderTexture(renderer, Texture.get(), nullptr, &dest);
		++DrawnFrames;
	}
	return SDL_RenderPresent(renderer);
}

bool RecordGpuQuad(float const originX, float const originY, float const scale)
{
	if (!Enabled() || !Surface) return false;

	if (Tick() || GpuRgba.empty())
	{
		GpuRgba.resize(size_t(Surface->w) * Surface->h * 4);
		// The FPS surface is RGB565 on a colour key: its black is the transparent background.
		OverlayImage::Rgb565KeyedToRgba(static_cast<UINT16 const*>(Surface->pixels), Surface->pitch, Surface->w,
										Surface->h, 0, GpuRgba.data());
	}

	SDL_GPUTexture* const tex = VideoGpu::UploadRgba(GpuTexture, GpuTextureW, GpuTextureH, GpuRgba.data(), Surface->w,
													 Surface->h, SDL_Rect{ 0, 0, Surface->w, Surface->h });
	if (!tex) return false;

	VideoGpu::OverlayTextured(tex, originX + FPS_X * scale, originY + FPS_Y * scale, Surface->w * scale,
							  Surface->h * scale, 1.0f, 1.0f, 1.0f, 1.0f, true);
	++DrawnFrames;
	return true;
}

void GameLoopHook()
{
	auto const before = Clock::now();
	ActualGameLoop();
	auto const elapsed = Clock::now() - before;

	// Only store "interesting" game loops
	if (elapsed > 150us)
	{
		LastGameLoopDurations.push_back(elapsed);
	}
}


void Init(GameLoopFunc_t const gameLoop, SGPVObject * const displayFont)
{
	GameLoopPtr = ActualGameLoop = gameLoop;
	DisplayFont = displayFont;
}


void ToggleOnOff()
{
	if (GameLoopPtr == ActualGameLoop)
	{
		// Currently disabled
		RenderPresentPtr = RenderPresentHook;
		GameLoopPtr = GameLoopHook;

		Surface.reset(SDL_CreateSurface(320, 26, SDL_PIXELFORMAT_RGB565));
		SDL_SetSurfaceColorKey(Surface.get(), true, 0);

		// Refresh on the next frame, whichever path draws it.
		TimeLastDisplayed = Clock::time_point{};
		GpuRgba.clear();
	}
	else
	{
		// Currently enabled
		RenderPresentPtr = SDL_RenderPresent;
		GameLoopPtr = ActualGameLoop;

		Surface.reset();
		Texture.reset();
		GpuRgba.clear();
	}
}

}
