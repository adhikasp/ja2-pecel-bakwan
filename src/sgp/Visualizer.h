#ifndef SGP_VISUALIZER_H
#define SGP_VISUALIZER_H

#include <SDL3/SDL.h>

struct SDL_Renderer;

namespace Visualizer
{
	void Render(SDL_Renderer * const renderer);
	void ToggleOnOff();

	/** Whether the Visualizer is on (a Ctrl+C toggle, on the game screen at cheat level). */
	bool Enabled();
	/** Frames it has actually been drawn on, on whichever presentation path is active. A headless run draws none. */
	unsigned long long Drawn();

	/** A hitbox face: four corners (a parallelogram) in panel-local pixels, plus its colour. */
	struct Face
	{
		float x[4];
		float y[4];
		SDL_FColor color;
	};

	/** The faces of the hitbox at tile @a x,@a y, profile @a z: the top rhombus and, unless @a roofOnly, the
	 * right and left side parallelograms. Returns how many faces were written (1 or 3). Pure geometry, no
	 * renderer: a unit test places it without a GPU. */
	int HitboxFaces(int x, int y, int z, bool roofOnly, Face out[3]);

	/** The SDL_GPU compositor path: records the panel's quads (background, ground tile and every hitbox), mapping
	 * its canvas position through @a originX,@a originY and @a scale. Returns whether it recorded any. */
	bool RecordGpuQuads(float originX, float originY, float scale);
}

#endif
