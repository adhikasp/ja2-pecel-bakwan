#pragma once

/* The scaled sector map of the strategic screen (see MapScreenGeometry in UILayout.h), and the art
 * pieces the expanded layout is put together from.
 *
 * The map is drawn at native size into the canvas, the top-left corner of the scaled map. Each frame:
 *   MapCanvasBeginFrame()  puts the native canvas back into the frame buffer (the stretch overwrote it),
 *   ... the map code draws ...
 *   MapCanvasPresent()     copies the canvas aside and stretches it over the whole scaled rectangle.
 * Popups and other UI drawn over the map come after MapCanvasPresent(). */

#include "Types.h"
#include "Object_Cache.h"

class SGPVSurface;

/** Is the map scaled and shown (not covered by the sector inventory)? */
bool MapCanvasActive();

void MapCanvasBeginFrame();
void MapCanvasPresent();

/** Stretch the native map in the save buffer over the scaled rectangle of the save buffer (for a static
 * backdrop, e.g. behind the sector inventory). */
void MapCanvasStretchSaveBuffer();

/** Forget the stretched copy and free the art (leaving the map screen, changing resolution). */
void MapCanvasShutdown();

/** Convert map drawing coordinates (canvas pixels, what GetScreenXYFromMapXY returns) to screen pixels. */
INT16 MapCanvasToScreenX(INT32 x);
INT16 MapCanvasToScreenY(INT32 y);


/* ---- Art pieces ---------------------------------------------------------------------------------- */

/** One image of an STI as a 16-bit surface, for copying parts of it. Cached; freed by MapCanvasShutdown(). */
SGPVSurface* MapArtSurface(cache_key_t file, UINT16 index);

/** Copy the part @a src of @a art to (x, y). */
void MapArtBlit(SGPVSurface* dst, SGPVSurface* art, SGPBox const& src, INT32 x, INT32 y);

/** Fill @a dst_box with copies of the part @a src of @a art, repeated from the top-left corner. */
void MapArtTile(SGPVSurface* dst, SGPVSurface* art, SGPBox const& src, SGPBox const& dst_box);

/** An empty panel in the style of the character list (dark green in riveted brass edges), for space that
 * the classic art does not cover. */
void MapArtPanel(SGPVSurface* dst, SGPBox const& box);

/** Draw the part @a src of @a art into @a dst_box, which may be larger: the corners (l, t, r, b pixels
 * wide) are copied as they are, the edges and the middle are repeated to fill. */
void MapArtNineSlice(SGPVSurface* dst, SGPVSurface* art, SGPBox const& src, SGPBox const& dst_box,
	INT32 l, INT32 t, INT32 r, INT32 b, bool middle = true);
