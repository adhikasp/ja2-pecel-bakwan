// Images of the native laptop (Phase 6, docs/ui/laptop.md), taken from the player's game data at runtime.
#include "NativeImages.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "GraphicModel.h"
#include "ItemModel.h"
#include "ItemSystem.h"
#include "LaptopContent.h"

#include <cstdlib>

namespace NativeUI
{

void RegisterLaptopImages()
{
	// "alumni-face-<index>": a former member's face from the A.I.M. alumni gallery
	// (laptop/old_aim.sti), the frame the gallery's caption and bio are keyed by too (LaptopContent.h).
	// Its frames are 56x50, so it is uplifted 2x and the card draws it at 112x100: a whole multiple,
	// cropped nowhere.
	RegisterImageSource("alumni-face-", [](std::string const& name) -> SDL_Surface* {
		SDL_Surface* const face = LoadStiFrame(LaptopContent::AimAlumniFaceFile(), LaptopContent::AimAlumniFace(std::atoi(name.c_str() + 12)));
		return face ? UpliftArt(face, 1) : nullptr;
	});
	// "shop-item-<index>": an item's big picture (the one Bobby Ray's and the item description box show), uplifted
	// 2x with Scale2x. The page draws it at a whole multiple of that size, so its aspect ratio is always kept.
	RegisterImageSource("shop-item-", [](std::string const& name) -> SDL_Surface* {
		ItemModel const* const item = GCM->getItem(uint16_t(std::atoi(name.c_str() + 10)), ItemSystem::nothrow);
		if (!item) return nullptr;
		GraphicModel const& g = item->getInventoryGraphicBig();
		SDL_Surface* const pic = LoadStiFrame(g.getPath().to_lower().to_std_string(), g.getSubImageIndex());
		if (!pic) return nullptr;
		// the item art keys its drop shadow in pure green; draw it as a soft dark shadow instead
		for (int y = 0; y < pic->h; ++y)
		{
			auto* p = static_cast<Uint8*>(pic->pixels) + y * pic->pitch;
			for (int x = 0; x < pic->w; ++x, p += 4)
			{
				if (p[3] && p[1] >= 200 && p[0] <= 64 && p[2] <= 64) { p[0] = p[1] = p[2] = 0; p[3] = 70; }
			}
		}
		return UpliftArt(pic, 1);
	});
}

}
