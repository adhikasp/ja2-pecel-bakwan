#include "NativeImages.h"

#include "Directories.h"
#include "Logger.h"
#include "HImage.h"
#include "VObject.h"
#include "ContentManager.h"
#include "GameInstance.h"

#include <string_theory/format>

#include <cstdlib>
#include <map>
#include <memory>

namespace NativeUI
{

namespace
{
	std::map<std::string, std::function<SDL_Surface*(std::string const&)>>& Sources()
	{
		static std::map<std::string, std::function<SDL_Surface*(std::string const&)>> sources;
		return sources;
	}
}

SDL_Surface* LoadStiFrame(std::string const& file, int const frame)
{
	try
	{
		AutoSGPVObject vo(AddVideoObjectFromFile(file.c_str()));
		if (vo->BPP() != 8 || frame < 0 || frame >= int(vo->SubregionCount())) return nullptr;
		ETRLEObject const& e = vo->SubregionProperties(frame);
		SDL_Surface* s = SDL_CreateSurface(e.usWidth, e.usHeight, SDL_PIXELFORMAT_RGBA32);
		if (!s) return nullptr;
		SDL_FillSurfaceRect(s, nullptr, 0);
		SGPPaletteEntry const* pal = vo->Palette();
		UINT8 const* src = vo->PixData(e);
		UINT8 const* const end = src + e.uiDataLength;
		// ETRLE: 0 ends a row; bit 7 set = that many transparent pixels; else that many palette indices follow
		int x = 0, y = 0;
		while (src < end && y < e.usHeight)
		{
			UINT8 const b = *src++;
			if (b == 0) { x = 0; ++y; continue; }
			int const n = b & 0x7F;
			if (b & 0x80) { x += n; continue; }
			auto* row = static_cast<Uint8*>(s->pixels) + y * s->pitch;
			for (int i = 0; i < n && src < end; ++i, ++x)
			{
				UINT8 const idx = *src++;
				if (x >= e.usWidth) continue;
				row[x * 4 + 0] = pal[idx].r;
				row[x * 4 + 1] = pal[idx].g;
				row[x * 4 + 2] = pal[idx].b;
				row[x * 4 + 3] = 255;
			}
		}
		return s;
	}
	catch (std::exception const& ex)
	{
		SLOGW("native UI: no image {} #{}: {}", file, frame, ex.what());
		return nullptr;
	}
}

SDL_Surface* CropScaled(SDL_Surface const* src, int const x, int const y, int const w, int const h, int const scale)
{
	if (!src || w <= 0 || h <= 0 || scale <= 0) return nullptr;
	SDL_Surface* s = SDL_CreateSurface(w * scale, h * scale, SDL_PIXELFORMAT_RGBA32);
	if (!s) return nullptr;
	for (int dy = 0; dy < h * scale; ++dy)
	{
		int const sy = y + dy / scale;
		auto* d = static_cast<Uint8*>(s->pixels) + dy * s->pitch;
		for (int dx = 0; dx < w * scale; ++dx)
		{
			int const sx = x + dx / scale;
			Uint32 p = 0;
			if (sx >= 0 && sy >= 0 && sx < src->w && sy < src->h)
				p = reinterpret_cast<Uint32 const*>(static_cast<Uint8 const*>(src->pixels) + sy * src->pitch)[sx];
			reinterpret_cast<Uint32*>(d)[dx] = p;
		}
	}
	return s;
}

void RegisterImageSource(std::string const& prefix, std::function<SDL_Surface*(std::string const&)> source)
{
	Sources()[prefix] = std::move(source);
}

SDL_Surface* ProvideGameImage(std::string const& name)
{
	if (name.rfind("face-", 0) == 0)
	{
		// Player mercs have a bigfaces/NN.sti; many NPCs only have the legacy bNN.sti face.
		int const n = std::atoi(name.c_str() + 5);
		ST::string const bigface = ST::format(FACESDIR "/bigfaces/{02d}.sti", n);
		if (GCM->doesGameResExists(bigface)) return LoadStiFrame(bigface.to_std_string(), 0);
		return LoadStiFrame(ST::format(FACESDIR "/b{02d}.sti", n).to_std_string(), 0);
	}
	if (name.rfind("smface-", 0) == 0)
	{
		// the generic soldier/militia/creature faces the auto-resolve cells use (Auto_Resolve.cc)
		return LoadStiFrame(INTERFACEDIR "/smfaces.sti", std::atoi(name.c_str() + 7));
	}
	// the longest matching prefix wins
	std::function<SDL_Surface*(std::string const&)> const* best = nullptr;
	size_t bestLen = 0;
	for (auto const& [prefix, fn] : Sources())
	{
		if (name.rfind(prefix, 0) == 0 && prefix.size() >= bestLen) { best = &fn; bestLen = prefix.size(); }
	}
	return best ? (*best)(name) : nullptr;
}

}
