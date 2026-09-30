// Support for the Phase 3 front-end screens that is not a screen itself: save thumbnails and the runtime "uplift"
// of full-screen art from the player's game data. Nothing derived from game art is stored in the repository.
#include "NativeImages.h"
#include "NativeUI.h"

#include "Logger.h"
#include "Video.h"
#include "VSurface.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace NativeUI
{

namespace
{
	constexpr int THUMB_W = 480;

	struct Snapshot
	{
		std::vector<uint8_t> rgb; // THUMB_W x h
		int h = 0;
	};
	Snapshot g_snapshot;
}

void SnapshotGameFrame()
{
	try
	{
		std::vector<uint8_t> src;
		int w = 0, h = 0;
		if (!VideoComposeFrame(src, w, h))
		{
			SGPVSurface::Lock l(FRAME_BUFFER);
			UINT16 const* p = l.Buffer<UINT16>();
			int const pitch = int(l.Pitch() / 2);
			w = FRAME_BUFFER->Width();
			h = FRAME_BUFFER->Height();
			src.resize(size_t(w) * h * 3);
			for (int y = 0; y < h; ++y)
			{
				for (int x = 0; x < w; ++x)
				{
					UINT16 const c = p[y * pitch + x];
					uint8_t* d = &src[(size_t(y) * w + x) * 3];
					d[0] = uint8_t(((c >> 11) & 0x1F) * 255 / 31);
					d[1] = uint8_t(((c >> 5) & 0x3F) * 255 / 63);
					d[2] = uint8_t((c & 0x1F) * 255 / 31);
				}
			}
		}
		if (w <= 0 || h <= 0) return;
		// box filter down to THUMB_W wide
		int const th = std::max(1, h * THUMB_W / w);
		Snapshot s;
		s.h = th;
		s.rgb.assign(size_t(THUMB_W) * th * 3, 0);
		for (int ty = 0; ty < th; ++ty)
		{
			int const y0 = ty * h / th, y1 = std::max(y0 + 1, (ty + 1) * h / th);
			for (int tx = 0; tx < THUMB_W; ++tx)
			{
				int const x0 = tx * w / THUMB_W, x1 = std::max(x0 + 1, (tx + 1) * w / THUMB_W);
				unsigned sum[3] = {}, n = 0;
				for (int y = y0; y < y1; ++y)
					for (int x = x0; x < x1; ++x, ++n)
						for (int c = 0; c < 3; ++c) sum[c] += src[(size_t(y) * w + x) * 3 + c];
				for (int c = 0; c < 3; ++c) s.rgb[(size_t(ty) * THUMB_W + tx) * 3 + c] = uint8_t(sum[c] / std::max(1u, n));
			}
		}
		g_snapshot = std::move(s);
	}
	catch (std::exception const& e)
	{
		SLOGW("save thumbnail: no snapshot: {}", e.what());
	}
}

void WriteSaveThumbnail(std::string const& path)
{
	if (g_snapshot.rgb.empty()) return;
	SDL_Surface* s = SDL_CreateSurfaceFrom(THUMB_W, g_snapshot.h, SDL_PIXELFORMAT_RGB24, g_snapshot.rgb.data(), THUMB_W * 3);
	if (!s) return;
	if (!SDL_SavePNG(s, path.c_str())) SLOGW("save thumbnail {}: {}", path, SDL_GetError());
	SDL_DestroySurface(s);
}

SDL_Surface* LoadThumbnail(std::string const& path)
{
	SDL_Surface* s = SDL_LoadPNG(path.c_str());
	if (!s) return nullptr;
	SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(s);
	return rgba;
}

SDL_Surface* UpliftArt(SDL_Surface* src, int passes)
{
	// Scale2x (EPX) per pass: doubles the size and keeps hard edges of pixel art hard; the UI then filters the
	// result smoothly to the size it is drawn at, so a 640x480 image covers a 4K screen without blocks or blur.
	if (!src) return nullptr;
	SDL_Surface* cur = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(src);
	for (int pass = 0; cur && pass < passes; ++pass)
	{
		int const w = cur->w, h = cur->h;
		SDL_Surface* out = SDL_CreateSurface(w * 2, h * 2, SDL_PIXELFORMAT_RGBA32);
		if (!out) break;
		auto px = [&](int x, int y) {
			x = std::clamp(x, 0, w - 1);
			y = std::clamp(y, 0, h - 1);
			return reinterpret_cast<Uint32 const*>(static_cast<Uint8 const*>(cur->pixels) + size_t(y) * cur->pitch)[x];
		};
		for (int y = 0; y < h; ++y)
		{
			auto* r0 = reinterpret_cast<Uint32*>(static_cast<Uint8*>(out->pixels) + size_t(2 * y) * out->pitch);
			auto* r1 = reinterpret_cast<Uint32*>(static_cast<Uint8*>(out->pixels) + size_t(2 * y + 1) * out->pitch);
			for (int x = 0; x < w; ++x)
			{
				Uint32 const P = px(x, y), A = px(x, y - 1), B = px(x + 1, y), C = px(x - 1, y), D = px(x, y + 1);
				r0[2 * x]     = (C == A && C != D && A != B) ? A : P;
				r0[2 * x + 1] = (A == B && A != C && B != D) ? B : P;
				r1[2 * x]     = (D == C && D != B && C != A) ? C : P;
				r1[2 * x + 1] = (B == D && B != A && D != C) ? D : P;
			}
		}
		SDL_DestroySurface(cur);
		cur = out;
	}
	return cur;
}

}
