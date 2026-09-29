#ifndef SCREEN_BACKDROP_H
#define SCREEN_BACKDROP_H

#include "Types.h"

class SGPVSurface;

/** Backdrops for the fixed-size (640x480) menu screens on bigger displays (Phase 3, treatment B).
 *
 * The screen keeps drawing its art 1:1 in the standard box centred on the screen. Before that, the same art
 * is scaled (nearest neighbour, "cover" -- cropping the overflowing axis) over the whole screen and dimmed,
 * so the margins show a matching backdrop instead of black.
 *
 * Usage:
 *   if (SGPVSurface* const s = BeginScreenBackdrop()) { BltVideoObject(s, bg, 0, 0, 0); EndScreenBackdrop(s); }
 *   BltVideoObject(FRAME_BUFFER, bg, 0, STD_SCREEN_X, STD_SCREEN_Y);
 * Both are no-ops (nullptr / nothing) at the classic 640x480 size. */

/** A 640x480 scratch surface to draw the screen art at (0, 0), or nullptr when there are no margins. */
SGPVSurface* BeginScreenBackdrop();

/** Stretch the scratch surface over the whole frame buffer, dim it and delete the surface. */
void EndScreenBackdrop(SGPVSurface* scratch);

/** Same for art that is already a surface (any size), e.g. a loading screen picture. Does not take ownership. */
void BltScreenBackdrop(SGPVSurface const* art);

#endif
