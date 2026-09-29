#include "Map_Screen_Canvas.h"

#include "Directories.h"
#include "HImage.h"
#include "MapScreen.h"
#include "Map_Screen_Interface_Map_Inventory.h"
#include "SysUtil.h"
#include "TextRegistry.h"
#include "UILayout.h"
#include "VObject.h"
#include "VSurface.h"
#include "Video.h"

#include <algorithm>
#include <map>
#include <utility>


namespace {

// The native canvas as of the last MapCanvasPresent(), to put back before the next frame is drawn.
SGPVSurface* g_canvas_copy;
bool         g_canvas_valid;

std::map<std::pair<std::string, UINT16>, SGPVSurface*> g_art;


SDL_Rect ToRect(SGPBox const& b) { return SDL_Rect{ b.x, b.y, b.w, b.h }; }


SGPVSurface* CanvasCopy()
{
	SGPBox const& c = g_ui.m_map.canvas;
	if (g_canvas_copy && (g_canvas_copy->Width() != c.w || g_canvas_copy->Height() != c.h))
	{
		DeleteVideoSurface(g_canvas_copy);
		g_canvas_copy  = nullptr;
		g_canvas_valid = false;
	}
	if (!g_canvas_copy) g_canvas_copy = AddVideoSurface(c.w, c.h, 16);
	return g_canvas_copy;
}


/** Stretch the whole canvas copy over the grid of dst. */
void StretchCopyTo(SGPVSurface* const dst)
{
	MapScreenGeometry const& g = g_ui.m_map;
	SGPBox const src{ 0, 0, g.canvas.w, g.canvas.h };
	BltStretchVideoSurface(dst, g_canvas_copy, &src, &g.grid);
	TextRegistry::OnStretch(dst, g_canvas_copy, ToRect(src), ToRect(g.grid));
}

}


bool MapCanvasActive()
{
	return g_ui.m_map.scaled() && fInMapMode && !fShowMapInventoryPool;
}


void MapCanvasBeginFrame()
{
	if (!MapCanvasActive() || !g_canvas_valid || !g_canvas_copy) return;
	SGPBox const& c = g_ui.m_map.canvas;
	if (g_canvas_copy->Width() != c.w || g_canvas_copy->Height() != c.h) return;
	BltVideoSurface(FRAME_BUFFER, g_canvas_copy, c.x, c.y, nullptr);
}


void MapCanvasPresent()
{
	if (!MapCanvasActive()) return;
	MapScreenGeometry const& g = g_ui.m_map;

	SGPVSurface* const copy = CanvasCopy();
	BltVideoSurface(copy, FRAME_BUFFER, 0, 0, &g.canvas);
	g_canvas_valid = true;

	StretchCopyTo(FRAME_BUFFER);

	/* Keep the save buffer showing the scaled map too, so that popups closing over it restore the right
	 * pixels. Not where the canvas is: the map code restores its native drawing from there. */
	SGPBox const right{ (UINT16)(g.canvas.x + g.canvas.w), g.grid.y, (UINT16)(g.grid.w - g.canvas.w), g.grid.h };
	SGPBox const below{ g.grid.x, (UINT16)(g.canvas.y + g.canvas.h), g.canvas.w, (UINT16)(g.grid.h - g.canvas.h) };
	BltVideoSurface(guiSAVEBUFFER, FRAME_BUFFER, right.x, right.y, &right);
	BltVideoSurface(guiSAVEBUFFER, FRAME_BUFFER, below.x, below.y, &below);

	InvalidateRegion(g.grid.x, g.grid.y, g.grid.x + g.grid.w, g.grid.y + g.grid.h);
}


void MapCanvasStretchSaveBuffer()
{
	MapScreenGeometry const& g = g_ui.m_map;
	if (!g.scaled()) return;
	SGPVSurface* const copy = CanvasCopy();
	BltVideoSurface(copy, guiSAVEBUFFER, 0, 0, &g.canvas);
	StretchCopyTo(guiSAVEBUFFER);
	g_canvas_valid = false; // the frame buffer canvas is not what the copy holds
}


void MapCanvasShutdown()
{
	if (g_canvas_copy) DeleteVideoSurface(g_canvas_copy);
	g_canvas_copy  = nullptr;
	g_canvas_valid = false;
	for (auto& [key, s] : g_art) DeleteVideoSurface(s);
	g_art.clear();
}


INT16 MapCanvasToScreenX(INT32 const x)
{
	return (INT16)g_ui.m_map.canvasToScreen(x, g_ui.m_map.canvas.y).x;
}


INT16 MapCanvasToScreenY(INT32 const y)
{
	return (INT16)g_ui.m_map.canvasToScreen(g_ui.m_map.canvas.x, y).y;
}


SGPVSurface* MapArtSurface(cache_key_t const file, UINT16 const index)
{
	auto const key = std::make_pair(std::string(file), index);
	auto const it  = g_art.find(key);
	if (it != g_art.end()) return it->second;

	SGPVObject const* const vo = GetVObject(file);
	ETRLEObject const& e = vo->SubregionProperties(index);
	SGPVSurface* const s = AddVideoSurface(e.usWidth + std::max<INT16>(0, e.sOffsetX), e.usHeight + std::max<INT16>(0, e.sOffsetY), 16);
	s->Fill(0);
	BltVideoObject(s, vo, index, 0, 0);
	g_art[key] = s;
	return s;
}


void MapArtBlit(SGPVSurface* const dst, SGPVSurface* const art, SGPBox const& src, INT32 const x, INT32 const y)
{
	if (src.w == 0 || src.h == 0) return;
	BltVideoSurface(dst, art, x, y, &src);
}


void MapArtTile(SGPVSurface* const dst, SGPVSurface* const art, SGPBox const& src, SGPBox const& box)
{
	if (src.w == 0 || src.h == 0) return;
	for (INT32 y = 0; y < box.h; y += src.h)
	{
		for (INT32 x = 0; x < box.w; x += src.w)
		{
			SGPBox part{ src.x, src.y, (UINT16)std::min<INT32>(src.w, box.w - x), (UINT16)std::min<INT32>(src.h, box.h - y) };
			BltVideoSurface(dst, art, box.x + x, box.y + y, &part);
		}
	}
}


void MapArtPanel(SGPVSurface* const dst, SGPBox const& box)
{
	if (box.w == 0 || box.h == 0) return;
	SGPVSurface* const art = MapArtSurface(INTERFACEDIR "/newgoldpiece3.sti", 0);
	// the dark green of the character list's columns, in its riveted brass edges
	MapArtTile(dst, art, SGPBox{ 14, 60, 44, 120 }, box);
	if (box.w >= 24 && box.h >= 30) MapArtNineSlice(dst, art, SGPBox{ 0, 0, 261, 252 }, box, 10, 16, 10, 10, false);
}


void MapArtNineSlice(SGPVSurface* const dst, SGPVSurface* const art, SGPBox const& s, SGPBox const& d,
	INT32 const l, INT32 const t, INT32 const r, INT32 const b, bool const middle)
{
	UINT16 const sw = s.w - l - r, sh = s.h - t - b; // middle of the source
	UINT16 const dw = d.w - l - r, dh = d.h - t - b; // middle of the destination
	auto box = [](INT32 x, INT32 y, INT32 w, INT32 h) { return SGPBox{ (UINT16)x, (UINT16)y, (UINT16)w, (UINT16)h }; };

	// corners
	MapArtBlit(dst, art, box(s.x,              s.y,              l, t), d.x,              d.y);
	MapArtBlit(dst, art, box(s.x + s.w - r,    s.y,              r, t), d.x + d.w - r,    d.y);
	MapArtBlit(dst, art, box(s.x,              s.y + s.h - b,    l, b), d.x,              d.y + d.h - b);
	MapArtBlit(dst, art, box(s.x + s.w - r,    s.y + s.h - b,    r, b), d.x + d.w - r,    d.y + d.h - b);
	// edges
	MapArtTile(dst, art, box(s.x + l,          s.y,              sw, t), box(d.x + l,       d.y,             dw, t));
	MapArtTile(dst, art, box(s.x + l,          s.y + s.h - b,    sw, b), box(d.x + l,       d.y + d.h - b,   dw, b));
	MapArtTile(dst, art, box(s.x,              s.y + t,          l, sh), box(d.x,           d.y + t,         l, dh));
	MapArtTile(dst, art, box(s.x + s.w - r,    s.y + t,          r, sh), box(d.x + d.w - r, d.y + t,         r, dh));
	if (middle)
	{
		MapArtTile(dst, art, box(s.x + l, s.y + t, sw, sh), box(d.x + l, d.y + t, dw, dh));
	}
}
