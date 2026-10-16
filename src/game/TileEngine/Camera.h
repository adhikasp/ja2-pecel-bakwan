#ifndef CAMERA_H
#define CAMERA_H

#include "WorldDef.h"

/**
 * The tactical camera's centre, and the small pure core that says where it may rest.
 *
 * The centre is a position in the game's world ("cell") coordinates: CELL_X_SIZE (= 10) units per tile, so a
 * tile is 10 x 10 and its centre is at tile * 10 + 5. That is where the camera used to be pinned - the type was
 * an INT16 cell, there was no fraction anywhere in it, and a pan was always a whole tile (see #38 and
 * docs/plan/native-modern-game-decisions.md). #37's strip cache draws a recorded row at an offset, which is
 * already a sub-tile camera, so the state now carries the fraction and the renderer rounds the centre to the
 * pixels it draws (RenderWorld.cc, CalcRenderParameters).
 */
namespace Camera
{
struct Centre
{
	double x = 0.0;
	double y = 0.0;
};

/**
	 * The camera is free: it rests where a pan leaves it, not only on a tile centre. This is decision B2, kept
	 * behind a flag because it is the one part of #38 that was called out as cheap to reverse: flipping it to
	 * false restores the pre-#38 settle, where every pan rounded to the nearest tile centre.
	 */
constexpr bool kFreeCamera = true;

/** A tile's centre: the position the legacy camera could only ever rest on. */
constexpr Centre FromTile(int const tileX, int const tileY)
{
	return { double(tileX) * CELL_X_SIZE + CELL_X_SIZE / 2.0, double(tileY) * CELL_Y_SIZE + CELL_Y_SIZE / 2.0 };
}

/** The nearest tile centre (the legacy settle). */
Centre SnapToTileCentre(Centre c);

/** Whether a centre sits exactly on a tile centre: true of every resting position before #38. */
bool OnTileCentre(Centre c);
}

#endif
