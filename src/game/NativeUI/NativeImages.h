#pragma once
// Images the native UI takes from the player's game data at runtime (nothing derived from them is written anywhere).

#include <SDL3/SDL.h>

#include <functional>
#include <string>

namespace NativeUI
{
	/** Frame @a frame of an STI as a new RGBA surface (transparent where the image is), or nullptr. */
	SDL_Surface* LoadStiFrame(std::string const& file, int frame);
	/** A new RGBA surface: @a src cut to (x, y, w, h) and enlarged @a scale times with nearest-neighbour sampling. */
	SDL_Surface* CropScaled(SDL_Surface const* src, int x, int y, int w, int h, int scale);

	/** Images named "<prefix><rest>" come from @a source (called with the whole name). */
	void RegisterImageSource(std::string const& prefix, std::function<SDL_Surface*(std::string const&)> source);
	/** The image provider of the native UI: "face-<n>" (a merc's big portrait) and the registered sources. */
	SDL_Surface* ProvideGameImage(std::string const& name);
}
