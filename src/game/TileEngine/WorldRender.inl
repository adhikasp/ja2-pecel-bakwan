/* ---- Phase 8 world renderer (WorldRender.h) --------------------------------------------------------------------
 * Included at the end of RenderWorld.cc: it needs RenderTiles and that file's render parameters. The recorded
 * renderers run the same passes a full redraw of the software renderer runs (RenderStaticWorld, then the drawing
 * passes of RenderDynamicWorld), every frame, with RenderTiles recording instead of blitting. */
#include "WorldRender.h"
#include "WorldGpu.h"
#include "Automation.h"
#include "Headless.h"
#include "Interface_Control.h"
#include "Lighting.h"
#include "stb_image_write.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {

WorldRendererKind   gWorldRequested = WorldRendererKind::Software;
WorldRendererKind   gWorldActive    = WorldRendererKind::Software;
bool                gWorldStarted   = false;  // the requested renderer was tried
std::string         gWorldError;
WorldPipe::SpritePool gWorldPool;
WorldPipe::Frame    gWorldFrame;
WorldPipe::Target   gWorldTarget;
WorldRenderStats    gWorldStats;
WorldGpu::Renderer* gWorldGpu = nullptr;
bool                gNoStaticCache = false; // measure the worst case (every frame re-records everything, as when scrolling)
bool                gNoReadback = false; // driven windows: skip the read-back into the WORLD_BUFFER (frame-rate runs)
std::vector<WorldPipe::PointLight> gExtraLights; // test/driving lights (ja2.setWorldLights)

/** How far past the viewport the static cache records (see the strip cache below): the recording clip is
 * [-kStripMargin, W + kStripMargin), drawn as the same range shifted by kStripMargin because SGPRect cannot hold
 * a negative edge. */
constexpr int kStripMargin = 2048;

using wr_clock = std::chrono::steady_clock;
double WrMs(wr_clock::time_point a, wr_clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

/** Byte i of a ZStripInfo's change list as the legacy strip blitters read it: past the 16 changes they run on
 * into the next fields of the struct. */
int StripChange(ZStripInfo const& zi, int i)
{
	if (i < int(std::size(zi.pbZChange))) return zi.pbZChange[i];
	switch (i - int(std::size(zi.pbZChange)))
	{
		case 0:  return zi.bInitialZChange;
		case 1:  return INT8(zi.ubFirstZStripWidth);
		case 2:  return INT8(zi.ubNumberOfZChanges);
		default: return 0;
	}
}

void RecordBlit(WorldPipe::Op const op, ClipInfo const& ci, UINT16 const* palette, UINT16 const z, UINT16 const outline,
	Strips const strips, INT16 const zIndex)
{
	WorldRecorder& r = *gWorldRecorder;
	if (ci.status == ClipInfo::Status::Completely_Clipped || ci.blitLength <= 0 || ci.blitHeight <= 0) return;

	WorldPipe::Instance in{};
	in.x0 = UINT16(ci.iTempX + ci.leftSkip);
	in.y0 = UINT16(ci.iTempY + ci.topSkip);
	in.x1 = UINT16(in.x0 + ci.blitLength);
	in.y1 = UINT16(in.y0 + ci.blitHeight);
	in.ox = INT16(ci.iTempX);
	in.oy = INT16(ci.iTempY);
	in.op = op;
	in.z = z;
	in.outline = outline;
	in.columns = WorldPipe::NO_COLUMNS;

	ETRLEObject const& e = ci.vobject->SubregionProperties(ci.vobjectIndex);
	WorldPipe::SpritePool::Entry const& sp = r.pool->Get(ci.vobject->PixData(e), e.uiDataLength, e.usWidth, e.usHeight);
	in.sprite = sp.offset;
	in.spriteW = sp.w;

	if (strips != Strips::None)
	{
		// The depths exactly as the strip blitters step through them, including their quirks
		auto const& all = ci.vobject->ppZStripInfo;
		UINT16 const which = strips == Strips::MultiZ ? ci.vobjectIndex : UINT16(zIndex);
		if (!all || !all[which]) return; // the legacy blitters draw nothing either
		ZStripInfo const& zi = *all[which];
		int const first = zi.ubFirstZStripWidth;
		int const ls = ci.leftSkip;
		std::vector<UINT16>& out = r.frame->columns;
		in.columns = UINT32(out.size());
		if (strips == Strips::MultiZ)
		{
			// MultiZBlitter: one depth per column
			int level = INT16(z) + zi.bInitialZChange * Z_STRIP_DELTA_Y;
			int cols = ls > first ? HALF_TILE - (ls - first) % HALF_TILE : ls < first ? first - ls : HALF_TILE;
			int index = 0;
			if (ls >= first)
			{
				index = 1 + (ls - first) / HALF_TILE;
				int sum = 0;
				for (int i = 0; i < index; ++i) sum += StripChange(zi, i);
				level += Z_STRIP_DELTA_Y * sum;
			}
			for (int c = 0; c < ci.blitLength; ++c)
			{
				out.push_back(UINT16(level));
				if (--cols == 0)
				{
					cols = HALF_TILE;
					level += StripChange(zi, index++) * Z_STRIP_DELTA_Y;
				}
			}
		}
		else
		{
			// BltTransZTransShadowInc(Obscure): its own start (left-clip steps of Z_SUBLAYERS)...
			UINT16 start = UINT16(INT16(z) + zi.bInitialZChange * Z_SUBLAYERS * 10);
			int const startCols = ls > first ? 20 - (ls - first) % 20 : ls < first ? first - ls : 20;
			int startIndex = 0;
			if (ls >= startCols)
			{
				startIndex = UINT16(1 + (ls - first) / 20);
				for (int i = 0; i < startIndex; ++i)
				{
					int const d = StripChange(zi, i);
					if (d == -1) start -= Z_SUBLAYERS;
					else if (d == 1) start += Z_SUBLAYERS;
				}
			}
			// ...and the non-obscured one steps by Z_SUBLAYERS over opaque pixels but Z_STRIP_DELTA_Y over transparent
			// runs, so its depth depends on the row: one depth per pixel then.
			bool const perPixel = op == WorldPipe::Op::TransShadowZInc;
			int const rows = perPixel ? ci.blitHeight : 1;
			for (int row = 0; row < rows; ++row)
			{
				UINT16 level = start;
				int cols = startCols, index = startIndex;
				UINT16 const* const src = &r.pool->pixels[sp.offset + size_t(ci.topSkip + row) * sp.w];
				for (int c = 0; c < ci.blitLength; ++c)
				{
					bool const opaque = src[ls + c] & 0x100;
					out.push_back(level);
					if (--cols == 0)
					{
						cols = 20;
						int const d = StripChange(zi, index++);
						int const step = perPixel && opaque ? Z_SUBLAYERS : Z_STRIP_DELTA_Y;
						if (d < 0) level -= step;
						else if (d > 0) level += step;
					}
				}
			}
			// The obscured one starts its checkerboard from the unclipped top row
			in.parity = UINT8((ci.topSkip & 1) | (perPixel ? WorldPipe::PER_PIXEL_DEPTH : 0));
		}
		static bool const check = std::getenv("JA2_WORLD_STRIP_CHECK") != nullptr;
		if (check)
		{
			// Debugging aid: the legacy strip blitter and the pipeline's rule for this instance on the same random
			// colour and depth buffers; they must leave the same pixels. The buffers are the recorded coordinate
			// space (the viewport shifted positive by the strip cache's margin), which the legacy blitters reach
			// by starting their pointers at the margin.
			int const W = WORLD_SCREEN_WIDTH + 2 * kStripMargin, H = WORLD_SCREEN_HEIGHT + 2 * kStripMargin;
			static std::vector<UINT16> c, zb, zb2;
			static std::vector<UINT32> c2;
			static UINT32 seed = 1;
			c.resize(size_t(W) * H);
			zb.resize(size_t(W) * H);
			UINT16* const cb = c.data() + kStripMargin * W + kStripMargin;
			UINT16* const zb0 = zb.data() + kStripMargin * W + kStripMargin;
			for (int y = in.y0; y < in.y1 && y < H; ++y)
				for (int x = in.x0; x < in.x1 && x < W; ++x)
				{
					size_t const i = size_t(y) * W + x;
					seed = seed * 1103515245u + 12345u;
					c[i] = UINT16(seed >> 8);
					zb[i] = UINT16(int(z) - 200 + int((seed >> 20) % 400));
				}
			c2.resize(c.size());
			for (size_t i = 0; i < c.size(); ++i) c2[i] = WorldPipe::Expand565(c[i]);
			zb2 = zb;
			switch (op)
			{
				case WorldPipe::Op::TransZInc:              BltTransZInc(ci, cb, W * 2, zb0, z); break;
				case WorldPipe::Op::TransZIncBurns:         BltTransZIncSameZBurnsThrough(ci, cb, W * 2, zb0, z); break;
				case WorldPipe::Op::TransZIncObscure:       BltTransZIncObscure(ci, cb, W * 2, zb0, z); break;
				case WorldPipe::Op::TransShadowZInc:        BltTransZTransShadowInc(ci, cb, W * 2, zb0, z, zIndex, palette); break;
				case WorldPipe::Op::TransShadowZIncObscure: BltTransZTransShadowIncObscure(ci, cb, W * 2, zb0, z, zIndex, palette); break;
				default: break;
			}
			UINT16 const* const pal = palette ? palette : ci.vobject->CurrentShade();
			std::vector<uint32_t> pal888(256);
			for (int i = 0; i < 256; ++i) pal888[i] = WorldPipe::Expand565(pal[i]);
			int bad = 0;
			for (int y = in.y0; y < in.y1 && y < H; ++y)
				for (int x = in.x0; x < in.x1 && x < W; ++x)
				{
					size_t const i = size_t(y) * W + x;
					UINT16 const s = r.pool->pixels[in.sprite + size_t(y - in.oy) * in.spriteW + (x - in.ox)];
					if (s & 0x100) WorldPipe::ApplyPixel(in, s, x, y, WorldPipe::DepthAt(in, *r.frame, x, y), pal888.data(), ShadeTable, 0x7BEF, c2[i], zb2[i]);
					if (c[i] != WorldPipe::Quantize565(c2[i]) || zb[i] != zb2[i]) ++bad;
				}
			if (bad) SLOGW("strip check: {} px differ, op {}, z {} leftSkip {} topSkip {} first {}", bad, WorldPipe::OpName(op), z, ls, ci.topSkip, first);
		}
	}

	static UINT16 const noPalette[256] = {};
	// Tiles and items draw with the object's own palette: take its 24-bit form (Phase 8, 32-bit colour). Entities
	// whose shade table the game built separately (mercs, corpses) keep their 565 table, expanded by the pipeline.
	UINT32 const* palette24 = nullptr;
	if (palette && palette == ci.vobject->CurrentShade()) palette24 = ci.vobject->CurrentShade24();
	in.palette = r.frame->Palette(palette ? palette : noPalette, palette24);
	if (r.glowTable) r.glowing.emplace_back(UINT32(r.frame->instances.size()), r.glowTable);
	r.glowTable = nullptr;
	r.frame->instances.push_back(in);
	++r.blits;
}

/** The passes of a full redraw: RenderStaticWorld, then what RenderDynamicWorld draws (not its dirty-rectangle pass).
 * The static part (everything up to the dynamic layers) only changes when the software renderer would redraw the
 * static world too (RENDER_FLAG_FULL/MARKED) or the view moves. CalcRenderParameters must have been called. */
void RenderWorldStatic()
{
	{
		RenderTiles const S(gsLStartPointX_M, gsLStartPointY_M, gsLStartPointX_S, gsLStartPointY_S, gsLEndXS, gsLEndYS);
		S(TILES_NONE, RENDER_STATIC_LAND);
		S(TILES_NONE, RENDER_STATIC_OBJECTS);
		if (gRenderFlags & RENDER_FLAG_SHADOWS) S(TILES_NONE, RENDER_STATIC_SHADOWS);
		S(TILES_NONE, RENDER_STATIC_STRUCTS, RENDER_STATIC_ROOF, RENDER_STATIC_ONROOF, RENDER_STATIC_TOPMOST);
		S(TILES_OBSCURED, RENDER_STATIC_STRUCTS, RENDER_STATIC_ONROOF);
	}
	RenderTiles const D(gsStartPointX_M, gsStartPointY_M, gsStartPointX_S, gsStartPointY_S, gsEndXS, gsEndYS);
	D(TILES_OBSCURED, RENDER_STATIC_STRUCTS);
}

void RenderWorldDynamic(bool const checkInteractive)
{
	RenderTiles const D(gsStartPointX_M, gsStartPointY_M, gsStartPointX_S, gsStartPointY_S, gsEndXS, gsEndYS);
	D(TILES_NONE, RENDER_DYNAMIC_OBJECTS, RENDER_DYNAMIC_SHADOWS, RENDER_DYNAMIC_STRUCT_MERCS, RENDER_DYNAMIC_MERCS, RENDER_DYNAMIC_STRUCTS);
	D(TILES_NONE, RENDER_DYNAMIC_ROOF, RENDER_DYNAMIC_HIGHMERCS, RENDER_DYNAMIC_ONROOF);
	D(checkInteractive ? TILES_DYNAMIC_CHECKFOR_INT_TILE : TILES_NONE, RENDER_DYNAMIC_TOPMOST);
}

void RenderWorldScene(bool const checkInteractive)
{
	RenderWorldStatic();
	RenderWorldDynamic(checkInteractive);
}

/** The frame's lighting (Phase 8 "lighting and shade tables as shaders"): the game's own dynamic lights
 * (muzzle flashes, explosions, lamps - every active LIGHT_SPRITE) as point lights the shader applies. */
WorldPipe::Lighting CollectLighting()
{
	WorldPipe::Lighting L;
	// The light templates carry no colour (LightGetColor() tints the baked light map, not the source, and is
	// often black), so a dynamic light is warm. Per-template colours are a follow-up.
	float const cr = 1.00f, cg = 0.82f, cb = 0.52f;
	FOR_EACH(LIGHT_SPRITE, l, LightSprites)
	{
		if (!(l->uiFlags & LIGHT_SPR_ACTIVE)) continue;
		if (l->iX < 0 || l->iY < 0 || l->iX >= WORLD_COLS || l->iY >= WORLD_ROWS) continue;
		INT16 const rTiles = LightSpriteRadius(l);
		if (rTiles <= 0) continue;
		// The tile centre in cell space, through the same isometric transform the tiles are drawn with
		float const dx = float(l->iX * CELL_X_SIZE + CELL_X_SIZE / 2) - gsRenderCenterX;
		float const dy = float(l->iY * CELL_Y_SIZE + CELL_Y_SIZE / 2) - gsRenderCenterY;
		float sx, sy;
		FloatFromCellToScreenCoordinates(dx, dy, &sx, &sy);
		WorldPipe::PointLight p;
		p.x = float(g_ui.m_tacticalMapCenterX + INT16(sx) - gsRenderWorldOffsetX);
		p.y = float(g_ui.m_tacticalMapCenterY + INT16(sy) - gsRenderWorldOffsetY);
		p.radius = float(std::min<INT16>(rTiles, 24)) * CELL_X_SIZE; // the template extent (a safety bound only)
		p.r = cr; p.g = cg; p.b = cb;
		p.intensity = 0.5f;
		L.points.push_back(p);
	}
	for (WorldPipe::PointLight const& p : gExtraLights) L.points.push_back(p);
	return L;
}

/* The static cache across camera moves, as strips of one walk row each.
 *
 * A walk row is one anti-diagonal of tiles (RenderTiles advances the walk by one screen row at a time, and a row
 * draws all its layers before the next row starts), so the static part of a frame is exactly: for each of the
 * fixed walk calls, for each row in order, that call's instances for that row. That makes a row the natural
 * cache unit - a camera move only changes which rows are on screen, and the rows still in view are the same
 * instances, translated.
 *
 * Two things make the translation exact:
 *  - the recording clip is the viewport widened by kStripMargin and the recorder shifts its coordinates positive
 *    by the same amount, so nothing the clip would cut at the edge is lost for the next camera position;
 *  - a row remembers the lattice origin (the screen position of world tile 0,0) it was recorded at, so an
 *    instance recorded for one camera can be translated to the current one.
 *
 * A row is keyed by the anti-diagonal x + y of its first tile, which is a world coordinate, so a row recorded at
 * one camera is found again at the next. Its instances cover a world tile x window with a margin either side
 * (kStripWindowTiles): while the camera stays inside that window the row is reused as it is, and only when it is
 * left does the row get re-recorded. Rows that scroll out of view are dropped, so scrolling only ever records
 * the rows entering the view (and the odd row whose window was left behind). */

constexpr int kStripOverhang    = 640;  // how far a tile's sprite may reach outside the row before it is not reusable
constexpr int kStripWindowTiles = 16;   // a row records this many tiles past its x range on each side
constexpr int kStripRowMargin   = 2;    // rows kept cached around the view (anti-diagonals)
constexpr int kStaticWalks      = 2;    // the larger-offset (S) and the normal (D) static walk
constexpr int kStaticCalls[2]   = { 5, 1 };

/** One of the two static walks: the anchors it starts row 0 with (the LARGER offsets for S, the normal for D). */
struct StaticWalkGeom
{
	INT16 xM, yM, xS, yS, endXS, endYS;
};

StaticWalkGeom StaticWalkOf(int const w)
{
	if (w == 0) return { gsLStartPointX_M, gsLStartPointY_M, gsLStartPointX_S, gsLStartPointY_S, gsLEndXS, gsLEndYS };
	return { gsStartPointX_M, gsStartPointY_M, gsStartPointX_S, gsStartPointY_S, gsEndXS, gsEndYS };
}

/** One row of a walk: the anchor RenderTiles starts that row's tiles with, and the tiles it covers. */
struct StaticRow
{
	INT32 xM, yM;  // the row's first tile
	INT32 xS, yS;  // where that tile is on screen
	INT32 cx, cy;  // the lattice origin: tile (x, y) is at (cx + 20 * (x - y), cy + 10 * (x + y))
	INT32 count;   // tiles in the row
	bool  odd;     // the row's half-tile x offset (the walk alternates it per row)
};

StaticRow StaticRowOf(StaticWalkGeom const& b, int const i)
{
	StaticRow r;
	r.odd  = (i & 1) != 0;
	r.xM   = b.xM + (i + 1) / 2;
	r.yM   = b.yM + i / 2;
	r.xS   = b.xS + (r.odd ? 20 : 0);
	r.yS   = b.yS + 10 * i;
	// The lattice origin is a property of the walk (the camera), not of the row: the row's own half-tile offset
	// is already in r.xS.
	r.cx   = b.xS - 20 * (b.xM - b.yM);
	r.cy   = b.yS - 10 * (b.xM + b.yM);
	int const span = b.endXS - r.xS;
	r.count = span > 0 ? (span + 39) / 40 : 1; // RenderTiles draws at least one tile per row
	return r;
}

/** The rows a walk covers (RenderTiles' do/while draws at least one). */
int StaticRowCount(StaticWalkGeom const& b)
{
	int const span = b.endYS - b.yS;
	return span > 0 ? (span + 9) / 10 : 1;
}

/** The instances of one call of the walk's fixed sequence, for one row. */
struct StaticStrip
{
	std::vector<WorldPipe::Instance> instances;
	std::vector<std::pair<UINT32, UINT16 const*>> glowing; // instance index -> glow table
};

/** One recorded row of a walk: its calls, and the depth runs and palettes they share (an instance's `columns` and
 * `palette` index into the row's, so the row copies them once no matter how many calls it has). */
struct StaticRowStrips
{
	std::vector<StaticStrip> calls;             // one per call
	std::vector<UINT16>      columns;           // the recording frame's depth runs
	std::vector<UINT32>      palettes;          // 256 colours each
	std::vector<UINT32>      remap;             // scratch: this row's palette index -> the frame's
	UINT32 colBase = 0;                         // where its depth runs landed in the frame being assembled
	INT32 cx = 0, cy = 0;                       // the lattice origin of the recording
	INT32 xFrom = 0, xTo = 0;                   // the world tile x range it covers
	size_t instances() const { size_t n = 0; for (StaticStrip const& s : calls) n += s.instances.size(); return n; }
};

struct StaticCache
{
	bool               valid = false;
	std::vector<INT32> key;
	std::map<INT32, StaticRowStrips> rows[kStaticWalks];
	int hits = 0, misses = 0, stripsRecorded = 0, stripsReused = 0;
	void Reset() { rows[0].clear(); rows[1].clear(); }
} gStaticCache;

/** Everything the static cache depends on *except* the camera: the camera moving is what the strips are for. */
std::vector<INT32> StaticKey()
{
	return { gsRenderHeight, gsWORLD_VIEWPORT_START_X, gsWORLD_VIEWPORT_START_Y,
		gsWORLD_VIEWPORT_END_X, gsWORLD_VIEWPORT_END_Y, WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT,
		gClippingRect.iLeft, gClippingRect.iTop, gClippingRect.iRight, gClippingRect.iBottom,
		INT32(gTacticalStatus.uiFlags & (SHOW_ALL_ROOFS | NOHIDE_REDUNDENCY | SHOW_ALL_ITEMS | SHOW_ALL_MERCS | RED_ITEM_GLOW_ON)),
		INT32(gGameSettings.fOptions[TOPTION_GLOW_ITEMS]), INT32(gGameSettings.fOptions[TOPTION_TOGGLE_WIREFRAME]),
		INT32(gRenderFlags & RENDER_FLAG_SHADOWS), INT32(gWorldPool.generation), INT32(gfWorldLoaded),
		INT32(guiTranslucentMask), INT32(ShadeTable[0x7BEF]) };
}

/** One call of the static walk, by number: the same sequence RenderWorldStatic runs, one layer group at a time
 * (which is the same order: a RenderTiles call draws its layers per row, so splitting a call by row and rejoining
 * the rows keeps it). */
void RunStaticCall(int const w, int const call, RenderTiles const& rt)
{
	if (w == 0)
	{
		switch (call)
		{
			case 0: rt(TILES_NONE, RENDER_STATIC_LAND); break;
			case 1: rt(TILES_NONE, RENDER_STATIC_OBJECTS); break;
			case 2: if (gRenderFlags & RENDER_FLAG_SHADOWS) rt(TILES_NONE, RENDER_STATIC_SHADOWS); break;
			case 3: rt(TILES_NONE, RENDER_STATIC_STRUCTS, RENDER_STATIC_ROOF, RENDER_STATIC_ONROOF, RENDER_STATIC_TOPMOST); break;
			case 4: rt(TILES_OBSCURED, RENDER_STATIC_STRUCTS, RENDER_STATIC_ONROOF); break;
			default: break;
		}
	}
	else
	{
		rt(TILES_OBSCURED, RENDER_STATIC_STRUCTS);
	}
}

/** Records one row of one walk into `out`: the calls in sequence, each into the shared temporary frame so the
 * palettes and depth runs are shared, with the clip widened so a later camera move can use it. */
void RecordStaticRow(WorldRecorder& rec, WorldPipe::Frame& tmp, int const w, StaticRow const& row,
	INT32 const xFrom, INT32 const xTo, StaticRowStrips& out)
{
	INT32 const k0 = xFrom - row.xM, k1 = xTo - row.xM;
	// The row's tiles, from xFrom: the walk's x_M is the tile x, y_M steps down as x_M steps up. RenderTiles
	// adds the row's half-tile x offset itself (row.odd), so the start x here is the walk's, without it; row.xS
	// already has it (the instances are placed relative to it).
	//
	// The whole walk is drawn kStripMargin pixels right and down: the recording clip is [-kStripMargin, W +
	// kStripMargin) and SGPRect cannot hold a negative edge, so the clip is that range shifted by kStripMargin
	// instead and the walk moves with it. A sprite that reaches outside the viewport is then recorded whole, and
	// the recorded coordinates are already in the shifted frame (a strip's lattice origin carries the shift).
	RenderTiles const rt(xFrom, row.yM - k0, row.xS - (row.odd ? 20 : 0) + 40 * k0 + kStripMargin,
		row.yS + kStripMargin, row.xS + 40 * k1 + kStripMargin, row.yS + 10 + kStripMargin, row.odd ? 1 : 0);
	// Mercs, corpses and items compute their own screen position from the world through the map centre rather
	// than from the walk's anchors, so it moves with the walk for the duration of the recording.
	INT16 const centerX = g_ui.m_tacticalMapCenterX, centerY = g_ui.m_tacticalMapCenterY;
	g_ui.m_tacticalMapCenterX = INT16(centerX + kStripMargin);
	g_ui.m_tacticalMapCenterY = INT16(centerY + kStripMargin);

	tmp.Clear(WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT);
	out.calls.assign(kStaticCalls[w], StaticStrip{});
	rec.frame = &tmp;
	rec.glowTable = nullptr;
	rec.glowing.clear();
	std::vector<std::pair<size_t, size_t>> ranges(kStaticCalls[w]);
	for (int c = 0; c < kStaticCalls[w]; ++c)
	{
		size_t const begin = tmp.instances.size();
		RunStaticCall(w, c, rt);
		ranges[c] = { begin, tmp.instances.size() };
	}
	for (int c = 0; c < kStaticCalls[w]; ++c)
		out.calls[size_t(c)].instances.assign(tmp.instances.begin() + ranges[c].first, tmp.instances.begin() + ranges[c].second);
	// The depth runs and palettes are the row's (the instances index into them), and the origin is the lattice of
	// the shifted frame the walk recorded in.
	out.columns  = std::move(tmp.columns);
	out.palettes = std::move(tmp.palettes);
	out.cx = row.cx + kStripMargin;
	out.cy = row.cy + kStripMargin;
	out.xFrom = xFrom; out.xTo = xTo;
	for (auto const& [index, table] : rec.glowing)
	{
		for (int c = 0; c < kStaticCalls[w]; ++c)
		{
			if (index >= ranges[c].first && index < ranges[c].second)
				out.calls[size_t(c)].glowing.emplace_back(UINT32(index - ranges[c].first), table);
		}
	}
	rec.frame = &gWorldFrame;
	g_ui.m_tacticalMapCenterX = centerX;
	g_ui.m_tacticalMapCenterY = centerY;
}

/** Appends one call of a cached row to the frame being assembled, translated to the current camera and clipped
 * to the target. The row's depth runs and palette remap are already in the frame (PutRowResources put them
 * there); `landed` is scratch for the glow patch. */
void AppendStrip(WorldPipe::Frame& dst, StaticRowStrips const& row, size_t const call, INT32 const dx, INT32 const dy,
	int const w, int const h, std::vector<UINT32>& landed)
{
	StaticStrip const& s = row.calls[call];
	if (s.instances.empty()) return;
	landed.assign(s.instances.size(), UINT32_MAX);
	for (size_t i = 0; i < s.instances.size(); ++i)
	{
		WorldPipe::Instance const& in = s.instances[i];
		int const x0 = int(in.x0) + dx, y0 = int(in.y0) + dy, x1 = int(in.x1) + dx, y1 = int(in.y1) + dy;
		if (x1 <= 0 || y1 <= 0 || x0 >= w || y0 >= h) continue; // entirely off the target
		int const cx0 = std::max(x0, 0), cy0 = std::max(y0, 0);
		WorldPipe::Instance o = in;
		o.x0 = UINT16(cx0); o.y0 = UINT16(cy0);
		o.x1 = UINT16(x1);  o.y1 = UINT16(y1);
		o.ox = INT16(in.ox + dx);
		o.oy = INT16(in.oy + dy);
		// The obscured passes' checkerboard is a function of the destination pixel: translating the instance by
		// (dx, dy) keeps the pattern only with this flip.
		o.parity = UINT8(in.parity ^ UINT8((dx + dy) & 1));
		o.palette = row.remap[in.palette];
		if (in.columns != WorldPipe::NO_COLUMNS)
		{
			int const sx = cx0 - x0, sy = cy0 - y0;
			if (sx == 0 && sy == 0)
			{
				o.columns = row.colBase + in.columns;
			}
			else
			{
				// The clip moved x0/y0, so the depths need their own run with the new geometry. A per-column run
				// is indexed by (x - x0) alone; a per-pixel one by (y - y0) * (x1 - x0) + (x - x0) - and that
				// stride is the recorded width, which the clamp does not change.
				UINT32 const base = UINT32(dst.columns.size());
				int const cols = x1 - cx0;
				if (in.parity & WorldPipe::PER_PIXEL_DEPTH)
				{
					int const stride = int(in.x1) - int(in.x0);
					for (int y = 0; y < y1 - cy0; ++y)
						for (int x = 0; x < cols; ++x)
							dst.columns.push_back(row.columns[size_t(in.columns) + size_t(y + sy) * stride + (x + sx)]);
				}
				else
				{
					for (int x = 0; x < cols; ++x) dst.columns.push_back(row.columns[size_t(in.columns) + (x + sx)]);
				}
				o.columns = base;
			}
		}
		landed[i] = UINT32(dst.instances.size());
		dst.instances.push_back(o);
	}
	// The item glow outline is a function of the glow animation frame, so it is applied here and not cached.
	for (auto const& [index, table] : s.glowing)
	{
		if (landed[index] != UINT32_MAX) dst.instances[landed[index]].outline = table[gsCurrentItemGlowFrame];
	}
}

/** Whether a cached row can be used as it is: it must cover the row's tiles, and it must have been recorded with
 * enough room around them (the clip reaches kStripMargin past the viewport, so the row must have been within it
 * by the sprite overhang). */
bool RowUsable(StaticRowStrips const& s, StaticRow const& row)
{
	if (s.xFrom > row.xM || s.xTo < row.xM + row.count) return false;
	// The recorded lattice origin is in the shifted frame; this is the camera's own move.
	INT32 const dx = row.cx - s.cx + kStripMargin, dy = row.cy - s.cy + kStripMargin;
	INT32 const x0 = row.xS - kStripOverhang - dx, x1 = row.xS + 40 * row.count + kStripOverhang - dx;
	INT32 const y0 = row.yS - kStripOverhang - dy, y1 = row.yS + 40 + kStripOverhang - dy;
	return x0 >= -kStripMargin && x1 <= WORLD_SCREEN_WIDTH + kStripMargin &&
		y0 >= -kStripMargin && y1 <= WORLD_SCREEN_HEIGHT + kStripMargin;
}

/** The static part of the frame, from the strip cache: reuses the cached rows, records the ones missing or left
 * behind, and assembles the frame call by call, row by row (the recorded order). */
void AssembleStatic(WorldRecorder& rec)
{
	gWorldFrame.Clear(WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT);
	gWorldFrame.clearColor = 0;
	gWorldFrame.clearDepth = LAND_Z_LEVEL;
	gWorldFrame.translucentMask = UINT16(guiTranslucentMask); // translucency stays in RGB565

	WorldPipe::Frame tmp;
	std::vector<UINT32> landed;
	// The recording clip is the viewport widened by kStripMargin: the walk is drawn shifted by kStripMargin so
	// SGPRect never has to hold a negative edge, which makes the clip really [-kStripMargin, W + kStripMargin). A
	// sprite that reaches past the viewport is then recorded whole and can be translated into view later.
	SGPRect const oldClip = gClippingRect;
	gClippingRect = { 0, 0, UINT16(WORLD_SCREEN_WIDTH + 2 * kStripMargin), UINT16(WORLD_SCREEN_HEIGHT + 2 * kStripMargin) };
	try
	{
		for (int w = 0; w < kStaticWalks; ++w)
		{
			StaticWalkGeom const b = StaticWalkOf(w);
			int const count = StaticRowCount(b);
			INT32 const first = b.xM + b.yM;
			std::map<INT32, StaticRowStrips>& store = gStaticCache.rows[w];
			std::vector<StaticRow> rows;
			rows.resize(size_t(count));
			for (int i = 0; i < count; ++i)
			{
				StaticRow const& row = rows[size_t(i)] = StaticRowOf(b, i);
				INT32 const a = first + i;
				auto it = store.find(a);
				if (it == store.end() || !RowUsable(it->second, row))
				{
					// The window is in world tile x, so it is clamped to the map (a tile outside it records
					// nothing, and a negative tile x would wrap FASTMAPROWCOLTOPOS into a real tile).
					INT32 const xFrom = std::max(0, row.xM - kStripWindowTiles);
					INT32 const xTo = std::min(INT32(WORLD_COLS), row.xM + row.count + kStripWindowTiles);
					StaticRowStrips strips;
					if (xTo > xFrom) RecordStaticRow(rec, tmp, w, row, xFrom, xTo, strips);
					store[a] = std::move(strips);
					++gStaticCache.stripsRecorded;
				}
				else
				{
					++gStaticCache.stripsReused;
				}
			}
			// drop the rows that scrolled out (above the walk's first row, below its last)
			for (auto jt = store.begin(); jt != store.end(); )
			{
				if (jt->first < first - kStripRowMargin || jt->first >= first + count + kStripRowMargin) jt = store.erase(jt);
				else ++jt;
			}
			// The rows' shared depth runs and palette tables go into the frame once (their instances index into
			// them), then the frame is assembled call by call, row by row - the order the walk recorded in.
			for (int i = 0; i < count; ++i)
			{
				auto it = store.find(first + i);
				if (it == store.end()) continue;
				StaticRowStrips& r = it->second;
				r.remap.resize(r.palettes.size() / 256);
				for (size_t p = 0; p < r.remap.size(); ++p)
					r.remap[p] = gWorldFrame.Palette(nullptr, &r.palettes[p * 256]);
				r.colBase = UINT32(gWorldFrame.columns.size());
				gWorldFrame.columns.insert(gWorldFrame.columns.end(), r.columns.begin(), r.columns.end());
			}
			for (int c = 0; c < kStaticCalls[w]; ++c)
			{
				for (int i = 0; i < count; ++i)
				{
					auto it = store.find(first + i);
					if (it == store.end() || size_t(c) >= it->second.calls.size()) continue;
					StaticRowStrips const& r = it->second;
					// The stored coordinates are the shifted frame's, the row's are the camera's: the move
					// between the two lattice origins is the translation, shift included.
					AppendStrip(gWorldFrame, r, size_t(c), rows[size_t(i)].cx - r.cx, rows[size_t(i)].cy - r.cy,
						WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT, landed);
				}
			}
		}
	}
	catch (...)
	{
		gClippingRect = oldClip;
		throw;
	}
	gClippingRect = oldClip;
}

/** Records the current view into gWorldFrame. With `cached`, the static part comes from the cache (which keeps
 * it across camera moves). */
void RecordWorldFrame(bool const mutate, bool const checkInteractive, bool const cached = false)
{
	if (gWorldPool.pixels.size() > (size_t(96) << 20)) gWorldPool.Reset(); // 192 MB: start over
	gWorldPool.NextFrame();
	WorldRecorder rec{ &gWorldFrame, &gWorldPool, mutate };
	std::vector<INT32> const key = cached ? StaticKey() : std::vector<INT32>{};
	bool const same = cached && gStaticCache.valid && gStaticCache.key == key;
	bool const reuse = same && !(gRenderFlags & (RENDER_FLAG_FULL | RENDER_FLAG_MARKED));
	gWorldRecorder = &rec;
	try
	{
		if (reuse)
		{
			++gStaticCache.hits;
			AssembleStatic(rec);
		}
		else
		{
			gWorldFrame.Clear(WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT);
			gWorldFrame.clearColor = 0;
			gWorldFrame.clearDepth = LAND_Z_LEVEL;
			gWorldFrame.translucentMask = UINT16(guiTranslucentMask); // translucency stays in RGB565
			ResetLayerOptimizing();
			if (cached)
			{
				// the cache was dropped (a flag, the viewport, the pool): record it again from the strips
				gStaticCache.Reset();
				gStaticCache.key = key;
				gStaticCache.valid = true;
				++gStaticCache.misses;
				AssembleStatic(rec);
			}
			else
			{
				RenderWorldStatic();
			}
		}
		// the dynamic layers: every frame (all of them, not only those used last frame)
		ResetLayerOptimizing();
		RenderWorldDynamic(checkInteractive);
	}
	catch (...)
	{
		gWorldRecorder = nullptr;
		throw;
	}
	gWorldRecorder = nullptr;
	for (LEVELNODE* n : rec.cleared) n->uiFlags |= LEVELNODE_LASTDYNAMIC;
	ResetLayerOptimizing();
	gWorldFrame.lighting = CollectLighting();
}

void CopyToWorldBuffer(UINT16 const* px, int w, int h)
{
	SGPVSurface::Lock l(WORLD_BUFFER);
	UINT16* const dst = l.Buffer<UINT16>();
	int const pitch = int(l.Pitch() / 2);
	int const rows = std::min<int>(h, WORLD_BUFFER->Height());
	int const cols = std::min<int>(w, WORLD_BUFFER->Width());
	for (int y = 0; y < rows; ++y) std::memcpy(dst + size_t(y) * pitch, px + size_t(y) * w, size_t(cols) * 2);
}

void ReadWorldBuffer(std::vector<UINT16>& out, int w, int h)
{
	SGPVSurface::Lock l(WORLD_BUFFER);
	UINT16 const* const src = l.Buffer<UINT16>();
	int const pitch = int(l.Pitch() / 2);
	out.assign(size_t(w) * h, 0);
	for (int y = 0; y < h; ++y) std::memcpy(&out[size_t(y) * w], src + size_t(y) * pitch, size_t(w) * 2);
}

WorldGpu::Renderer* GpuRenderer(std::string& error)
{
	if (gWorldGpu && gWorldGpu->Ready()) return gWorldGpu;
	if (!gWorldGpu) gWorldGpu = new WorldGpu::Renderer;
	// Headless sessions only start SDL's events; a GPU device without a window still needs the video subsystem
	if (!VideoGpuDevice() && !SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO))
	{
		error = std::string("no video subsystem: ") + SDL_GetError();
		return nullptr;
	}
	// Shares the presentation's device when it has one, else a device of its own (headless)
	if (!gWorldGpu->Init(VideoGpuDevice()))
	{
		error = gWorldGpu->Error();
		return nullptr;
	}
	return gWorldGpu;
}

bool WorldRecordingPossible()
{
	return VideoIsLayered() && !GameMode::getInstance()->isEditorMode();
}

void StartRequested()
{
	gWorldStarted = true;
	gWorldActive = WorldRendererKind::Software;
	gWorldError.clear();
	if (gWorldRequested == WorldRendererKind::Software) return;
	if (!WorldRecordingPossible())
	{
		gWorldError = "the world is not a layer of its own";
		return;
	}
	if (gWorldRequested == WorldRendererKind::Gpu && !GpuRenderer(gWorldError))
	{
		SLOGW("World renderer: gpu is not available ({}), using software", gWorldError);
		return;
	}
	gWorldActive = gWorldRequested;
	SLOGI("World renderer: {}{}", WorldRendererName(gWorldActive),
		gWorldActive == WorldRendererKind::Gpu ? " (" + gWorldGpu->DriverName() + ")" : std::string());
}

} // namespace


WorldRendererKind ParseWorldRenderer(std::string const& s)
{
	if (s == "gpu") return WorldRendererKind::Gpu;
	if (s == "pipeline") return WorldRendererKind::Pipeline;
	if (!s.empty() && s != "software") SLOGW("world_renderer: unknown value \"{}\", using software", s);
	return WorldRendererKind::Software;
}

char const* WorldRendererName(WorldRendererKind const k)
{
	switch (k)
	{
		case WorldRendererKind::Gpu:      return "gpu";
		case WorldRendererKind::Pipeline: return "pipeline";
		default:                          return "software";
	}
}

WorldRendererKind WorldRendererDefault(bool const driven)
{
	// Real windows draw the world on the GPU (pixel-equivalent, see docs/plan/native-modern-game-decisions.md,
	// Phase 8), falling back to software when there is no usable device. Driven sessions (headless, automation)
	// keep software: their screenshots are the reference images, the picture is identical (the 888 pipeline
	// expands to the same pixels for the shipped art), and the CPU pipeline is several times slower per frame,
	// which the resolution suite pays on every tour. The pipeline is still exercised there where it matters
	// (e2e_world_renderer, e2e_tactical_layers_pipeline, e2e_world_renderer_light).
	return driven ? WorldRendererKind::Software : WorldRendererKind::Gpu;
}

void WorldRendererConfigure(WorldRendererKind const k)
{
	VideoSetPresentHook(WorldRenderFrameTick);
	gWorldRequested = k;
	gWorldStarted = false;
}

WorldRendererKind WorldRendererRequested() { return gWorldRequested; }

WorldRendererKind WorldRendererActive()
{
	if (!gWorldStarted) StartRequested();
	if (gWorldActive != WorldRendererKind::Software && !WorldRecordingPossible()) return WorldRendererKind::Software;
	return gWorldActive;
}

WorldRendererKind WorldRendererSwitch(WorldRendererKind const k)
{
	WorldRendererConfigure(k);
	WorldRendererKind const now = WorldRendererActive();
	VideoSetWorldRecorded(now != WorldRendererKind::Software);
	if (now != WorldRendererKind::Gpu) VideoSetWorldGpuTexture(nullptr, 0, 0);
	if (now != WorldRendererKind::Pipeline) VideoClearWorld888();
	SetRenderFlags(RENDER_FLAG_FULL);
	return now;
}

std::string WorldRendererError() { return gWorldError; }

void WorldRendererSetReadback(bool const on) { gNoReadback = !on; }
void WorldRendererSetStaticCache(bool const on) { gNoStaticCache = !on; }
void WorldRendererSetExtraLights(std::vector<WorldPipe::PointLight> l) { gExtraLights = std::move(l); SetRenderFlags(RENDER_FLAG_FULL); }

std::vector<double> gFrameIntervals; // wall-clock intervals between RenderWorld calls since the last reset
uint64_t            gFrameTicks = 0;

void WorldRenderFrameTick()
{
	static wr_clock::time_point last{};
	auto const now = wr_clock::now();
	if (last != wr_clock::time_point{})
	{
		gWorldStats.frameMs = WrMs(last, now);
		if (gFrameIntervals.size() < 100000) gFrameIntervals.push_back(gWorldStats.frameMs);
	}
	last = now;
	++gFrameTicks;
}

WorldFrameTiming WorldRenderTiming(bool const reset)
{
	WorldFrameTiming t;
	t.frames = gFrameTicks;
	t.samples = int(gFrameIntervals.size());
	if (!gFrameIntervals.empty())
	{
		std::vector<double> v = gFrameIntervals;
		std::sort(v.begin(), v.end());
		double sum = 0;
		for (double x : v) sum += x;
		t.meanMs = sum / double(v.size());
		t.p50Ms = v[v.size() / 2];
		t.p95Ms = v[std::min(v.size() - 1, v.size() * 95 / 100)];
		t.maxMs = v.back();
	}
	if (reset) gFrameIntervals.clear();
	return t;
}

WorldRenderStats const& WorldRenderLastStats() { return gWorldStats; }

bool RenderWorldRecorded()
{
	WorldRendererKind const kind = WorldRendererActive();
	VideoSetWorldRecorded(kind != WorldRendererKind::Software);
	if (kind == WorldRendererKind::Software)
	{
		VideoSetWorldGpuTexture(nullptr, 0, 0);
		VideoClearWorld888();
		return false;
	}

	// Real windows: the game loop runs faster than frames are presented; a GPU world frame nobody saw yet is enough
	if (kind == WorldRendererKind::Gpu && VideoGpuDevice() && !sgp::IsHeadless() && !(Automation::GetOptions().Active() && !gNoReadback)
		&& !(gRenderFlags & (RENDER_FLAG_FULL | RENDER_FLAG_MARKED)) && !VideoTakeWorldGpuPresented())
	{
		return true;
	}

	if (gRenderFlags & RENDER_FLAG_FULL)
	{
		gfRenderFullThisFrame = TRUE;
		gfTopMessageDirty     = TRUE;
		fInterfacePanelDirty  = DIRTYLEVEL2;
		ApplyScrolling(gsRenderCenterX, gsRenderCenterY, TRUE, FALSE);
		ClearUiViewport();
	}

	CalcRenderParameters(gsWORLD_VIEWPORT_START_X, gsWORLD_VIEWPORT_START_Y, gsWORLD_VIEWPORT_END_X, gsWORLD_VIEWPORT_END_Y);
	RestoreBackgroundRects();
	if (!GameMode::getInstance()->isEditorMode() || !gfEditMode) RenderTacticalInterface();
	SaveBackgroundRects();

	int const stripsRecorded = gStaticCache.stripsRecorded;
	int const stripsReused = gStaticCache.stripsReused;
	auto const t0 = wr_clock::now();
	RecordWorldFrame(true, true, !gNoStaticCache);
	auto const t1 = wr_clock::now();
	ResetRenderParameters();

	double const frameMs = gWorldStats.frameMs;
	gWorldStats = {};
	gWorldStats.frameMs = frameMs;
	gWorldStats.instances = int(gWorldFrame.instances.size());
	gWorldStats.palettes = int(gWorldFrame.palettes.size() / 256);
	gWorldStats.spritePoolPixels = gWorldPool.pixels.size();
	gWorldStats.recordMs = WrMs(t0, t1);
	gWorldStats.staticHits = gStaticCache.hits;
	gWorldStats.staticMisses = gStaticCache.misses;
	gWorldStats.stripsRecorded = gStaticCache.stripsRecorded - stripsRecorded;
	gWorldStats.stripsReused = gStaticCache.stripsReused - stripsReused;

	// Whoever reads the world buffer (headless composition, screenshots of driven sessions) gets a copy
	bool const cpuCopy = sgp::IsHeadless() || (Automation::GetOptions().Active() && !gNoReadback) || !VideoGpuDevice();
	if (kind == WorldRendererKind::Gpu)
	{
		std::string error;
		WorldGpu::Renderer* const gpu = GpuRenderer(error);
		if (!gpu || !gpu->Render(gWorldFrame, gWorldPool, ShadeTable, cpuCopy))
		{
			SLOGE("World renderer: the GPU frame failed ({}), back to software", gpu ? gpu->Error() : error);
			gWorldActive = WorldRendererKind::Software;
			gWorldError = gpu ? gpu->Error() : error;
			VideoSetWorldRecorded(false);
			VideoSetWorldGpuTexture(nullptr, 0, 0);
			VideoClearWorld888();
			SetRenderFlags(RENDER_FLAG_FULL);
			return false;
		}
		gWorldStats.gpuBinMs = gpu->LastStats().buildMs;
		gWorldStats.gpuSubmitMs = gpu->LastStats().uploadMs;
		gWorldStats.uploadBytes = gpu->LastStats().uploadBytes;
		if (cpuCopy)
		{
			// The GPU's 888 texture is only seen in a real window; a driven session reads the frame back as RGB565
			// into the WORLD_BUFFER (headless composition, screenshots), so those shots are 565, not 888.
			std::vector<UINT16> px;
			if (gpu->Read565(px)) CopyToWorldBuffer(px.data(), gpu->Width(), gpu->Height());
			gWorldStats.gpuWaitMs = gpu->LastStats().waitMs;
		}
		VideoSetWorldGpuTexture(VideoGpuDevice() ? gpu->Texture() : nullptr, gpu->Width(), gpu->Height());
		VideoClearWorld888();
	}
	else
	{
		auto const r0 = wr_clock::now();
		gWorldTarget.Clear(gWorldFrame);
		WorldPipe::Rasterize(gWorldFrame, gWorldPool, gWorldTarget);
		VideoSetWorld888(gWorldTarget.color.data(), gWorldTarget.w, gWorldTarget.h);
		gWorldStats.rasterMs = WrMs(r0, wr_clock::now());
		VideoSetWorldGpuTexture(nullptr, 0, 0);
	}
	InvalidateWorldRegion(0, 0, WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT);
	if (gRenderFlags & RENDER_FLAG_FULL && !(gRenderFlags & RENDER_FLAG_SAVEOFF)) UpdateSaveBuffer();
	if (gRenderFlags & RENDER_FLAG_MARKED) ClearMarkedTiles();
	gRenderFlags &= ~(RENDER_FLAG_FULL | RENDER_FLAG_MARKED | RENDER_FLAG_ROOMIDS | RENDER_FLAG_CHECKZ);
	return true;
}


WorldEquivalenceResult RunWorldEquivalence(std::string const& outDir, bool const gpu, WorldEquivalenceOptions const& opts)
{
	if (!gfWorldLoaded) throw std::runtime_error("world equivalence: no sector loaded");
	WorldEquivalenceResult res;
	CalcRenderParameters(gsWORLD_VIEWPORT_START_X, gsWORLD_VIEWPORT_START_Y, gsWORLD_VIEWPORT_END_X, gsWORLD_VIEWPORT_END_Y);
	int const W = WORLD_SCREEN_WIDTH, H = WORLD_SCREEN_HEIGHT;

	// 0. With the cache on, prime it at this camera, then move the camera and record the view under test. What the
	// cache does across that move is the thing under test; with no move this is the plain cache-on record.
	if (opts.cache)
	{
		ClearRenderFlags(RENDER_FLAG_FULL | RENDER_FLAG_MARKED);
		RecordWorldFrame(false, false, true);
		if (opts.Scrolled())
		{
			gsRenderCenterX = INT16(gsRenderCenterX + opts.scrollTilesX * CELL_X_SIZE);
			gsRenderCenterY = INT16(gsRenderCenterY + opts.scrollTilesY * CELL_Y_SIZE);
			ApplyScrolling(gsRenderCenterX, gsRenderCenterY, FALSE, FALSE);
			CalcRenderParameters(gsWORLD_VIEWPORT_START_X, gsWORLD_VIEWPORT_START_Y, gsWORLD_VIEWPORT_END_X, gsWORLD_VIEWPORT_END_Y);
		}
	}
	SGPRect const clip = gClippingRect;

	// 1. Record (no side effects on the nodes, so the software renderer below sees the same world)
	int const stripsRecorded = gStaticCache.stripsRecorded, stripsReused = gStaticCache.stripsReused;
	auto t0 = wr_clock::now();
	RecordWorldFrame(false, false, opts.cache);
	res.recordMs = WrMs(t0, wr_clock::now());
	res.staticHits = gStaticCache.hits;
	res.staticMisses = gStaticCache.misses;
	res.staticStripsRecorded = gStaticCache.stripsRecorded - stripsRecorded;
	res.staticStripsReused = gStaticCache.stripsReused - stripsReused;
	res.instances = int(gWorldFrame.instances.size());
	res.palettes = int(gWorldFrame.palettes.size() / 256);
	res.sprites = int(gWorldPool.byData.size());
	for (WorldPipe::Instance const& in : gWorldFrame.instances) ++res.ops[WorldPipe::OpName(in.op)];

	// 2. The software renderer, as a full redraw onto black
	ResetLayerOptimizing();
	{
		std::vector<UINT16> const black(size_t(W) * H, 0);
		CopyToWorldBuffer(black.data(), W, H); // not ColorFill: it is clipped to the clipping rectangle
	}
	std::fill_n(gpZBuffer, size_t(gZBufferPitch / sizeof(*gpZBuffer)) * H, LAND_Z_LEVEL);
	t0 = wr_clock::now();
	RenderWorldScene(false);
	res.legacyMs = WrMs(t0, wr_clock::now());
	std::vector<UINT16> legacy;
	ReadWorldBuffer(legacy, W, H);
	std::vector<uint32_t> legacy888(legacy.size()); // the software renderer's 565 expanded, to compare with the 888 pipeline
	for (size_t i = 0; i < legacy.size(); ++i) legacy888[i] = WorldPipe::Expand565(legacy[i]);
	ResetRenderParameters();

	// 3. The CPU pipeline
	t0 = wr_clock::now();
	gWorldTarget.Clear(gWorldFrame);
	WorldPipe::Rasterize(gWorldFrame, gWorldPool, gWorldTarget);
	res.pipelineMs = WrMs(t0, wr_clock::now());

	std::vector<uint8_t> diff;
	bool const png = !outDir.empty();
	WorldPipe::DiffStats const dp = WorldPipe::Compare(legacy888.data(), gWorldTarget.color.data(), W, H,
		clip.iLeft, clip.iTop, clip.iRight, clip.iBottom, png ? &diff : nullptr);
	if (dp.different && std::getenv("JA2_WORLD_STRIP_CHECK"))
	{
		for (int i = 0; i < W * H; ++i)
		{
			if (legacy[i] == WorldPipe::Quantize565(gWorldTarget.color[i])) continue;
			int const x = i % W, y = i / W;
			if (x < clip.iLeft || x >= clip.iRight || y < clip.iTop || y >= clip.iBottom) continue;
			SLOGW("diff at {},{}: software {} pipeline {}", x, y, legacy[i], gWorldTarget.color[i]);
			for (WorldPipe::Instance const& in : gWorldFrame.instances)
			{
				if (x < in.x0 || x >= in.x1 || y < in.y0 || y >= in.y1) continue;
				UINT16 const s = gWorldPool.pixels[in.sprite + size_t(y - in.oy) * in.spriteW + (x - in.ox)];
				if (s & 0x100) SLOGW("  {} idx {} z {}", WorldPipe::OpName(in.op), s & 0xFF, WorldPipe::DepthAt(in, gWorldFrame, x, y));
			}
			break;
		}
	}
	res.width = clip.iRight - clip.iLeft;
	res.height = clip.iBottom - clip.iTop;
	res.pixels = dp.pixels;
	res.pipelineDifferent = dp.different;
	res.pipelinePercent = dp.percent();
	auto write = [&](std::string const& name, std::vector<uint8_t> const& rgb) {
		std::string const path = outDir + "/" + name;
		stbi_write_png(path.c_str(), W, H, 3, rgb.data(), W * 3);
		return path;
	};
	if (png)
	{
		res.legacyPng = write("world_software.png", WorldPipe::ToRgb(legacy888.data(), W, H));
		res.pipelinePng = write("world_pipeline.png", WorldPipe::ToRgb(gWorldTarget.color.data(), W, H));
		res.pipelineDiffPng = write("world_pipeline_diff.png", diff);
	}

	// 4. The GPU
	if (gpu)
	{
		WorldGpu::Renderer* const r = GpuRenderer(res.gpuError);
		if (r)
		{
			res.gpuDriver = r->DriverName();
			std::vector<UINT16> out;
			t0 = wr_clock::now();
			if (r->Render(gWorldFrame, gWorldPool, ShadeTable, true) && r->Read565(out))
			{
				res.gpuMs = WrMs(t0, wr_clock::now());
				res.gpuRan = true;
				std::vector<uint32_t> out888(out.size());
				for (size_t i = 0; i < out.size(); ++i) out888[i] = WorldPipe::Expand565(out[i]);
				WorldPipe::DiffStats const dg = WorldPipe::Compare(legacy888.data(), out888.data(), W, H,
					clip.iLeft, clip.iTop, clip.iRight, clip.iBottom, png ? &diff : nullptr);
				res.gpuDifferent = dg.different;
				res.gpuPercent = dg.percent();
				res.gpuVsPipelineDifferent = WorldPipe::Compare(gWorldTarget.color.data(), out888.data(), W, H,
					clip.iLeft, clip.iTop, clip.iRight, clip.iBottom).different;
				// The GPU is read back as 565, so compare the pipeline rounded to 565 with it: this is the exact
				// GPU-vs-pipeline lighting check (the 888-vs-565 difference above is just the readback).
				std::vector<uint32_t> pipelineQ(gWorldTarget.color.size());
				for (size_t i = 0; i < gWorldTarget.color.size(); ++i)
					pipelineQ[i] = WorldPipe::Expand565(WorldPipe::Quantize565(gWorldTarget.color[i]));
				res.gpuVsPipelineQuantizedDifferent = WorldPipe::Compare(pipelineQ.data(), out888.data(), W, H,
					clip.iLeft, clip.iTop, clip.iRight, clip.iBottom).different;
				if (png)
				{
					res.gpuPng = write("world_gpu.png", WorldPipe::ToRgb(out888.data(), W, H));
					res.gpuDiffPng = write("world_gpu_diff.png", diff);
				}
			}
			else
			{
				res.gpuError = r->Error();
			}
		}
	}
	SetRenderFlags(RENDER_FLAG_FULL);
	return res;
}
