#pragma once

#include "Types.h"
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <string_theory/string>
#include <optional>
#include <vector>

class SGPVSurface;

/** @file
 * Records every string the engine prints, so an automation driver can read
 * the screen as text instead of OCRing pixels.
 *
 * Text is printed into off-screen surfaces and later copied around, partially
 * overdrawn, or erased, and the engine does not track any of that. Rather than
 * hook every blitter, each recorded string keeps a copy of the pixels it
 * produced. A string counts as visible when the final composited frame still
 * shows (nearly) those pixels at that spot. Copies between surfaces made with
 * BltVideoSurface carry their strings along.
 *
 * Disabled by default; the automation layer enables it.
 */
namespace TextRegistry
{
	struct VisibleText
	{
		ST::string text;
		SDL_Rect   rect;
		std::optional<UINT16> color; // RGB565 ink colour, if the text was printed in one colour
	};

	void SetEnabled(bool);
	bool IsEnabled();

	/** A string was just printed into @a dst at @a rect. */
	void OnPrint(SGPVSurface* dst, SDL_Rect rect, ST::string const& text, std::optional<UINT16> color);

	/** @a srcRect of @a src was copied to (@a dx, @a dy) in @a dst. */
	void OnBlit(SGPVSurface* dst, SGPVSurface const* src, SDL_Rect srcRect, int dx, int dy);

	/** @a srcRect of @a src was stretched over @a dstRect of @a dst (nearest neighbour). */
	void OnStretch(SGPVSurface* dst, SGPVSurface const* src, SDL_Rect srcRect, SDL_Rect dstRect);

	void OnSurfaceDeleted(SGPVSurface const*);

	/** Forget everything, e.g. after a screen change. */
	void Clear();

	/** All recorded strings that are still visible in @a frame, top to bottom,
	 * left to right. */
	std::vector<VisibleText> Visible(SDL_Surface const* frame, SDL_Rect exclude);
}
