/* ---- Phase 0 world renderer spike (WorldSpike.h) ------------------------------------------------------------
 * Included at the end of RenderWorld.cc: it needs that file's render parameters, Z constants and RenderTiles.
 * The same passes, rows and per-node rules as RenderTiles for the static layers, but instead of blitting, each
 * node becomes an instance for the GPU-style renderer in src/spike/WorldSpikeRaster.h. */
#include "WorldSpike.h"

#ifdef WITH_NATIVE_SPIKES
#include "Map_Information.h"
#include "WorldSpikeRaster.h"
#include "stb_image_write.h"

#include <chrono>
#include <map>

namespace {

using namespace spike::world;

struct SpikeBuilder
{
	std::map<std::pair<SGPVObject const*, UINT16>, Sprite> sprites;
	std::vector<Instance> instances;
	int skipped = 0;

	Sprite const& SpriteFor(SGPVObject const* vo, UINT16 index)
	{
		auto const key = std::make_pair(vo, index);
		auto it = sprites.find(key);
		if (it != sprites.end()) return it->second;
		ETRLEObject const& e = vo->SubregionProperties(index);
		return sprites.emplace(key, DecodeEtrle(vo->PixData(e), e.uiDataLength, e.usWidth, e.usHeight, e.sOffsetX, e.sOffsetY)).first->second;
	}

	/** Per-column depth for multi-Z tiles, as MultiZBlitter computes it. False if the frame has no strip info
	 * (the legacy blitter then draws nothing either). */
	static bool ColumnZ(SGPVObject const* vo, UINT16 index, int width, UINT16 z, std::vector<uint16_t>& out)
	{
		if (!vo->ppZStripInfo || !vo->ppZStripInfo[index]) return false;
		ZStripInfo const& zi = *vo->ppZStripInfo[index];
		int const z0 = INT16(z) + zi.bInitialZChange * Z_STRIP_DELTA_Y;
		out.resize(size_t(width));
		for (int c = 0; c < width; ++c)
		{
			int zc = z0;
			if (c >= zi.ubFirstZStripWidth)
			{
				int const k = 1 + (c - zi.ubFirstZStripWidth) / HALF_TILE;
				for (int i = 0; i < k && i < int(std::size(zi.pbZChange)); ++i) zc += zi.pbZChange[i] * Z_STRIP_DELTA_Y;
			}
			out[size_t(c)] = uint16_t(zc);
		}
		return true;
	}

	void Pass(bool obscured, std::initializer_list<RenderLayerFlags> layers)
	{
		INT32 anchorX_M = gsLStartPointX_M, anchorY_M = gsLStartPointY_M;
		INT32 const anchorX_S = gsLStartPointX_S;
		INT32 anchorY_S = gsLStartPointY_S;
		bool odd = false;
		do
		{
			for (RenderLayerFlags const layer : layers)
			{
				INT32 x_M = anchorX_M, y_M = anchorY_M, x_S = anchorX_S + (odd ? 20 : 0);
				INT32 const y_S = anchorY_S;
				do
				{
					UINT32 const grid = FASTMAPROWCOLTOPOS(y_M, x_M);
					if (grid < GRIDSIZE) Tile(obscured, layer, gpWorldLevelData[grid], x_M, y_M, x_S, y_S);
					x_S += 40;
					++x_M;
					--y_M;
				}
				while (x_S < gsLEndXS);
			}
			if (odd) ++anchorY_M; else ++anchorX_M;
			odd = !odd;
			anchorY_S += 10;
		}
		while (anchorY_S < gsLEndYS);
	}

	void Tile(bool obscured, RenderLayerFlags layer, MAP_ELEMENT const& me, INT32 x_M, INT32 y_M, INT32 x_S, INT32 y_S)
	{
		MAP_ELEMENT::NodeIndex start{};
		bool forward = true, checkRedundant = false, obscuredLayer = false;
		switch (layer)
		{
			case TILES_STATIC_LAND:       start = MAP_ELEMENT::LAND_START_INDEX;    forward = false; checkRedundant = true; break;
			case TILES_STATIC_OBJECTS:    start = MAP_ELEMENT::OBJECT_START_INDEX;  checkRedundant = true; break;
			case TILES_STATIC_SHADOWS:    start = MAP_ELEMENT::SHADOW_START_INDEX;  break;
			case TILES_STATIC_STRUCTURES: start = MAP_ELEMENT::STRUCT_START_INDEX;  obscuredLayer = true; break;
			case TILES_STATIC_ROOF:       start = MAP_ELEMENT::ROOF_START_INDEX;    break;
			case TILES_STATIC_ONROOF:     start = MAP_ELEMENT::ONROOF_START_INDEX;  obscuredLayer = true; break;
			case TILES_STATIC_TOPMOST:    start = MAP_ELEMENT::TOPMOST_START_INDEX; break;
			default: return;
		}
		for (LEVELNODE const* n = me.pLevelNodes[start]; n; n = forward ? n->pNext : n->pPrevNode)
		{
			if (checkRedundant && me.uiFlags & MAPELEMENT_REDUNDENT && !(me.uiFlags & MAPELEMENT_REEVALUATE_REDUNDENCY) &&
				!(gTacticalStatus.uiFlags & NOHIDE_REDUNDENCY)) break;
			LevelnodeFlags const f = n->uiFlags;
			if (f & LEVELNODE_REVEAL) continue; // the pixelated reveal is dynamic-only
			if (f & (LEVELNODE_ROTTINGCORPSE | LEVELNODE_CACHEDANITILE))
			{
				if (!(f & LEVELNODE_DYNAMIC)) ++skipped; // static corpses/anitiles: not in the spike
				continue;
			}
			TILE_ELEMENT const& te = f & LEVELNODE_REVEALTREES ? gTileDatabase[n->usIndex + 2] : gTileDatabase[n->usIndex];
			if (te.uiFlags & ANIMATED_TILE || ((te.uiFlags & DYNAMIC_TILE || f & LEVELNODE_DYNAMIC) && !obscured)) continue;
			bool obscuredBlit = false;
			if (obscuredLayer)
			{
				if (obscured)
				{
					if (!(f & LEVELNODE_SHOW_THROUGH)) continue;
					obscuredBlit = true;
				}
				else if (f & LEVELNODE_SHOW_THROUGH)
				{
					continue;
				}
			}
			if (f & (LEVELNODE_ITEM | LEVELNODE_USEABSOLUTEPOS | LEVELNODE_PHYSICSOBJECT | LEVELNODE_DISPLAY_AP)) { ++skipped; continue; }
			if (f & LEVELNODE_WIREFRAME && !gGameSettings.fOptions[TOPTION_TOGGLE_WIREFRAME]) continue;
			if (f & LEVELNODE_HIDDEN && (!(te.uiFlags & ROOF_TILE) || !(gTacticalStatus.uiFlags & SHOW_ALL_ROOFS))) continue;

			INT16 const height = me.sHeight;
			INT16 modHeight = (height / 80 - 1) * 80;
			if (modHeight < 0) modHeight = 0;
			INT32 x = x_S, y = y_S;
			if (te.uiFlags & IGNORE_WORLD_HEIGHT) y -= modHeight;
			else if (!(f & LEVELNODE_IGNOREHEIGHT)) y -= height;
			if (f & LEVELNODE_USERELPOS) { x += n->sRelativeX; y += n->sRelativeY; }
			if (f & LEVELNODE_USEZ) y -= n->sRelativeZ;

			Instance in;
			bool multiZ = false, wall = false;
			INT16 z = 0;
			switch (layer)
			{
				case TILES_STATIC_LAND:
					z = LAND_Z_LEVEL;
					in.op = Op::Plain;
					break;
				case TILES_STATIC_OBJECTS:
					z = te.uiFlags & CLIFFHANG_TILE ? LAND_Z_LEVEL :
						te.uiFlags & OBJECTLAYER_USEZHEIGHT ? INT16(GetMapXYWorldY(x_M, y_M) * Z_SUBLAYERS + LAND_Z_LEVEL) :
						INT16(OBJECT_Z_LEVEL);
					in.op = Op::DepthGEqual;
					break;
				case TILES_STATIC_SHADOWS:
					z = INT16(std::max((GetMapXYWorldY(x_M, y_M) - 80) * Z_SUBLAYERS + SHADOW_Z_LEVEL, 0));
					in.op = Op::ShadowGreater;
					break;
				case TILES_STATIC_STRUCTURES:
				{
					multiZ = te.uiFlags & MULTI_Z_TILE;
					wall   = te.uiFlags & WALL_TILE;
					INT16 wy = GetMapXYWorldY(x_M, y_M);
					if (f & LEVELNODE_USEZ)
					{
						wy += f & LEVELNODE_NOZBLITTER ? 40 : n->sRelativeZ;
						z = ONROOF_Z_LEVEL;
					}
					else
					{
						z = STRUCT_Z_LEVEL;
					}
					z += wy * Z_SUBLAYERS;
					in.op = Op::DepthGEqual;
					break;
				}
				case TILES_STATIC_ROOF:
					y -= WALL_HEIGHT;
					z = INT16((WALL_HEIGHT + GetMapXYWorldY(x_M, y_M)) * Z_SUBLAYERS + ROOF_Z_LEVEL);
					in.op = te.uiFlags & ROOFSHADOW_TILE ? Op::ShadowGreater : Op::DepthGEqual;
					break;
				case TILES_STATIC_ONROOF:
					y -= WALL_HEIGHT;
					z = INT16((GetMapXYWorldY(x_M, y_M) + WALL_HEIGHT) * Z_SUBLAYERS + ONROOF_Z_LEVEL);
					in.op = Op::DepthGEqual;
					break;
				case TILES_STATIC_TOPMOST:
					z = TOPMOST_Z_LEVEL;
					in.op = Op::DepthGEqual;
					break;
				default: break;
			}
			y += gsRenderHeight;

			SGPVObject* const vo = te.hTileSurface;
			UINT16 const index = te.usRegionIndex;
			vo->CurrentShade(n->ubShadeLevel);
			Sprite const& s = SpriteFor(vo, index);
			in.sprite  = &s;
			in.palette = vo->CurrentShade();
			in.x = x + s.offX;
			in.y = y + s.offY;
			in.z = uint16_t(z);
			if (multiZ)
			{
				if (!ColumnZ(vo, index, s.w, in.z, in.columnZ)) continue;
				in.op = obscuredBlit ? Op::ObscuredGreater : wall ? Op::DepthGEqual : Op::DepthGreater;
			}
			else if (obscuredBlit)
			{
				in.op = Op::ObscuredGreater;
			}
			instances.push_back(std::move(in));
		}
	}
};

void WriteRgb565Png(std::string const& path, UINT16 const* px, int w, int h)
{
	std::vector<uint8_t> rgb(size_t(w) * h * 3);
	for (size_t i = 0; i < size_t(w) * h; ++i)
	{
		UINT16 const p = px[i];
		rgb[i * 3 + 0] = uint8_t(((p >> 11) & 31) * 255 / 31);
		rgb[i * 3 + 1] = uint8_t(((p >> 5) & 63) * 255 / 63);
		rgb[i * 3 + 2] = uint8_t((p & 31) * 255 / 31);
	}
	stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3);
}

}

WorldSpikeResult RunWorldSpike(std::string const& outDir)
{
	using clock = std::chrono::steady_clock;
	auto ms = [](clock::time_point a, clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
	if (!gfWorldLoaded) throw std::runtime_error("world spike: no sector loaded");

	WorldSpikeResult res;
	CalcRenderParameters(gsWORLD_VIEWPORT_START_X, gsWORLD_VIEWPORT_START_Y, gsWORLD_VIEWPORT_END_X, gsWORLD_VIEWPORT_END_Y);
	SGPRect const clip = gClippingRect;
	int const W = WORLD_SCREEN_WIDTH, H = WORLD_SCREEN_HEIGHT;

	// Legacy: the static passes of RenderStaticWorld() as a full redraw does them (all layers enabled), onto black
	ResetLayerOptimizing();
	ColorFillVideoSurfaceArea(WORLD_BUFFER, clip.iLeft, clip.iTop, clip.iRight, clip.iBottom, 0);
	std::fill_n(gpZBuffer, gsWORLD_VIEWPORT_END_Y * WORLD_SCREEN_WIDTH, LAND_Z_LEVEL);
	auto t0 = clock::now();
	{
		RenderTiles const RenderTiles(gsLStartPointX_M, gsLStartPointY_M, gsLStartPointX_S, gsLStartPointY_S, gsLEndXS, gsLEndYS);
		RenderTiles(TILES_NONE, RENDER_STATIC_LAND);
		RenderTiles(TILES_NONE, RENDER_STATIC_OBJECTS);
		if (gRenderFlags & RENDER_FLAG_SHADOWS) RenderTiles(TILES_NONE, RENDER_STATIC_SHADOWS);
		RenderTiles(TILES_NONE, RENDER_STATIC_STRUCTS, RENDER_STATIC_ROOF, RENDER_STATIC_ONROOF, RENDER_STATIC_TOPMOST);
		RenderTiles(TILES_OBSCURED, RENDER_STATIC_STRUCTS, RENDER_STATIC_ONROOF);
	}
	res.legacyMs = ms(t0, clock::now());
	std::vector<UINT16> legacy(size_t(W) * H);
	{
		SGPVSurface::Lock l(WORLD_BUFFER);
		for (int y = 0; y < H; ++y) std::copy_n(l.Buffer<UINT16>() + size_t(y) * (l.Pitch() / 2), W, &legacy[size_t(y) * W]);
	}

	// Spike: instances in the legacy submission order, then the depth-tested raster
	t0 = clock::now();
	SpikeBuilder b;
	b.Pass(false, { TILES_STATIC_LAND });
	b.Pass(false, { TILES_STATIC_OBJECTS });
	if (gRenderFlags & RENDER_FLAG_SHADOWS) b.Pass(false, { TILES_STATIC_SHADOWS });
	b.Pass(false, { TILES_STATIC_STRUCTURES, TILES_STATIC_ROOF, TILES_STATIC_ONROOF, TILES_STATIC_TOPMOST });
	b.Pass(true,  { TILES_STATIC_STRUCTURES, TILES_STATIC_ONROOF });
	res.buildMs = ms(t0, clock::now());
	t0 = clock::now();
	Target t;
	t.reset(W, H, LAND_Z_LEVEL);
	t.clipL = clip.iLeft;
	t.clipT = clip.iTop;
	t.clipR = clip.iRight;
	t.clipB = clip.iBottom;
	t.shade = ShadeTable;
	for (Instance const& in : b.instances) Draw(t, in);
	res.rasterMs = ms(t0, clock::now());
	ResetRenderParameters();

	res.instances = int(b.instances.size());
	res.skipped   = b.skipped;
	res.sprites   = int(b.sprites.size());
	std::vector<uint8_t> diff;
	DiffStats const d = Compare(legacy.data(), t.color.data(), W, clip.iLeft, clip.iTop, clip.iRight, clip.iBottom,
		outDir.empty() ? nullptr : &diff, H);
	res.width     = clip.iRight - clip.iLeft;
	res.height    = clip.iBottom - clip.iTop;
	res.pixels    = d.pixels;
	res.different = d.different;
	res.percent   = d.percent();

	if (!outDir.empty())
	{
		res.legacyPng = outDir + "/world_legacy.png";
		res.spikePng  = outDir + "/world_spike.png";
		res.diffPng   = outDir + "/world_diff.png";
		WriteRgb565Png(res.legacyPng, legacy.data(), W, H);
		WriteRgb565Png(res.spikePng, t.color.data(), W, H);
		stbi_write_png(res.diffPng.c_str(), W, H, 3, diff.data(), W * 3);
	}
	SetRenderFlags(RENDER_FLAG_FULL);
	return res;
}

#else

WorldSpikeResult RunWorldSpike(std::string const&)
{
	throw std::runtime_error("the native spikes are not built in (cmake -DWITH_NATIVE_SPIKES=ON)");
}

#endif
