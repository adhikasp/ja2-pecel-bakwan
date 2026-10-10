#pragma once
// The world renderer's pipeline (docs/plan/native-modern-game.md, Phase 8).
//
// A frame of the tactical world is a list of sprite instances in the order the legacy renderer (RenderTiles)
// submits its blits. Each instance names one of the legacy blitters as an Op, which fixes what it does to one
// destination pixel: which depth test it uses, whether it writes depth, and how the colour comes out (palette
// lookup, shade-table darkening of the destination, 50 % translucency, outline colour, checkerboard pixelation).
//
// Two implementations run the same per-pixel rules (ApplyPixel below):
//  - the GPU one (WorldGpu.h): a compute shader on SDL_GPU, one thread per pixel walking the instances that
//    cover its 16x16 bin, in submission order; colour and depth stay in registers, so ops that read the
//    destination (shadows, translucency) are exact;
//  - the CPU one here (Rasterize), used headless, in CI and as the reference for the shader.
// Neither depends on game code: the game side (RenderWorld.cc) records the instances.
//
// The pipeline works in 8 bits per channel (0xRRGGBB), not RGB565: palettes are the art's 24-bit colours, so
// shading and lighting gradients are smooth instead of stepping (docs/plan/native-modern-game.md Phase 8 and
// the 32-bit colour follow-up). A finished frame is lit (ApplyLighting): a global ambient and sun, plus the
// point lights the game adds (muzzle flashes, explosions, lamps).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldPipe {

/** One legacy blitter each. In the comments: Z = depth buffer, z = the instance's depth (the column's for the
 * strip ops), D = destination colour, P = the palette, S = the shade table (darkening), L = the pixelation
 * checkerboard ((y + parity) & 1) == (x & 1), o = the outline colour, s = the source index. */
enum class Op : uint8_t
{
	Transparent,            // D = P[s]
	TransZ,                 // Z <= z: Z = z, D = P[s]
	TransZNB,               // Z <= z: D = P[s]
	TransZObscured,         // Z <  z: Z = z, D = P[s]; else L: D = P[s]
	TransZInc,              // strips; Z <  z: Z = z, D = P[s]
	TransZIncBurns,         // strips; Z <= z: Z = z, D = P[s]
	TransZIncObscure,       // strips; Z <  z or L: Z = z, D = P[s]
	TransShadow,            // D = s == 254 ? S[D] : P[s]
	TransShadowZ,           // Z <  z: Z = z, D = s == 254 ? S[D] : P[s]
	TransShadowZNB,         // Z <  z: D = s == 254 ? S[D] : P[s]
	TransShadowZNBObscured, // s == 254: Z < z: D = S[D];  else Z <= z or L: D = P[s]
	Shadow,                 // D = S[D]
	ShadowZ,                // Z <  z: Z = z, D = S[D]
	ShadowZNB,              // Z <  z: D = S[D]
	TranslucentZ,           // Z <= z: Z = z, D = (P[s] >> 1 & m) + (D >> 1 & m)
	TranslucentZNB,         // Z <= z: D = (P[s] >> 1 & m) + (D >> 1 & m)
	Outline,                // s == 254: (o != 0: D = o); else D = P[s]
	OutlineZ,               // Z <= z: s == 254 ? D = o : (Z = z, D = P[s])
	OutlineZObscuredLE,     // Z <= z: Z = z; else !L: skip;  D = s == 254 ? o : P[s]
	OutlineZObscuredLT,     // Z <  z: Z = z; else !L: skip;  D = s == 254 ? o : P[s]
	OutlineZNB,             // Z <  z and s != 254: D = P[s]
	TransShadowZInc,        // strips; Z <= z: Z = z, D = s == 254 ? S[D] : P[s]
	TransShadowZIncObscure, // strips; Z <  z or L: Z = z, D = s == 254 ? S[D] : P[s]
	Count
};

char const* OpName(Op);

constexpr uint32_t NO_COLUMNS = 0xFFFFFFFFu;

/** 32 bytes, the same layout as the shader's (8 uints). */
struct Instance
{
	uint16_t x0, y0, x1, y1;   // the pixels it covers, [x0, x1) x [y0, y1) (already clipped)
	int16_t  ox, oy;           // top-left of the sprite frame on screen
	uint32_t sprite;           // offset of the frame in the sprite pool
	uint16_t spriteW;          // frame width (pool row length)
	Op       op;
	uint8_t  parity;           // bit 0: added to y for the checkerboard; PER_PIXEL_DEPTH: `columns` has a depth per pixel
	uint32_t palette;          // index of the 256-entry palette
	uint16_t z;
	uint16_t outline;          // outline colour (RGB565, expanded to 888 at apply time)
	uint32_t columns;          // offset of the depths (per column: x - x0; per pixel: (y - y0) * (x1 - x0) + x - x0),
	                           // NO_COLUMNS if the instance has one depth, z
};
static_assert(sizeof(Instance) == 32);
constexpr uint8_t PER_PIXEL_DEPTH = 2;

/** Decoded sprite frames: one uint16 per pixel, low byte = palette index, 0x100 = opaque. Frames are added as
 * they are first drawn and kept (the pool only grows until Reset); `uploaded` is how much a GPU copy has. */
struct SpritePool
{
	std::vector<uint16_t> pixels;
	size_t                uploaded = 0;
	uint32_t              generation = 1;  // changes when the pool is reset (a GPU copy must be redone)

	struct Entry { uint32_t offset; uint16_t w, h; uint32_t length; uint32_t hash; uint32_t checked; };
	uint32_t              frame = 1;       // NextFrame(): data is re-checked once per frame (images can be reloaded)
	void NextFrame() { ++frame; }
	std::unordered_map<uint8_t const*, Entry> byData;

	/** The frame for ETRLE data (decoded on first use, again if the data at that address changed). */
	Entry const& Get(uint8_t const* etrle, uint32_t length, int w, int h);
	void Reset();
};

/** ETRLE (0x80|n = n transparent, n = n literal indices, 0 = end of row) into pool pixels. */
void DecodeEtrle(uint8_t const* data, uint32_t length, int w, int h, uint16_t* out);

/** A dynamic point light the game adds: world pixel position, radius, colour and intensity. */
struct PointLight
{
	float x = 0, y = 0;
	float radius = 0;
	float r = 1, g = 1, b = 1;
	float intensity = 1;
};

/** The frame's lighting (Phase 8 "lighting and shade tables as shaders"): a global ambient and sun applied to
 * every pixel, plus the point lights. Identity (the defaults, no points) leaves the frame as the legacy
 * renderer drew it, which is what the op-equivalence unit test uses. */
struct Lighting
{
	float ambientR = 1.0f, ambientG = 1.0f, ambientB = 1.0f;
	float sunR = 0.0f, sunG = 0.0f, sunB = 0.0f; // a flat directional add (a warm day, a cool night)
	float falloff = 2.0f;
	std::vector<PointLight> points;

	bool Identity() const
	{
		return ambientR == 1.0f && ambientG == 1.0f && ambientB == 1.0f &&
			sunR == 0.0f && sunG == 0.0f && sunB == 0.0f && points.empty();
	}
};

/** The 8-bit colour helpers shared by ApplyPixel, the lighting and the shader. */
inline uint16_t Quantize565(uint32_t const c)
{
	return uint16_t(((c >> 19) & 0x1F) << 11 | ((c >> 10) & 0x3F) << 5 | ((c >> 3) & 0x1F));
}
inline uint32_t Expand565(uint16_t const p)
{
	uint32_t const r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
	return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}
/** The legacy shade table maps RGB565 to RGB565; the pipeline rounds the destination to 565 to look it up (a
 * darkening, where the lost precision is invisible) and expands the result, so shadows keep the legacy values. */
inline uint32_t ShadeOf(uint32_t const d, uint16_t const* const shade)
{
	return Expand565(shade[Quantize565(d)]);
}

struct Frame
{
	int width = 0, height = 0;             // the render target (the world buffer)
	uint32_t clearColor = 0;               // 0xRRGGBB
	uint16_t clearDepth = 0;
	uint32_t translucentMask = 0x7BEF;      // the RGB565 halving mask (translucency stays in 565)
	Lighting lighting;
	std::vector<Instance> instances;
	std::vector<uint32_t> palettes;        // 256 entries each, 0xRRGGBB
	std::vector<uint16_t> columns;         // per-column depths of the strip ops
	std::unordered_map<uint16_t const*, uint32_t> paletteIndex;
	struct PaletteSlot { uint16_t const* p; uint32_t index; };
	PaletteSlot paletteCache[1024] = {};   // direct-mapped in front of paletteIndex

	void Clear(int w, int h);
	/** Index of a palette in this frame. `p24` (the art's 24-bit colours) is used when given; `p565` (the legacy
	 * 16-bit table) is expanded otherwise. `p565` identifies the entry (it is what the recorder has by pointer). */
	uint32_t Palette(uint16_t const* p565, uint32_t const* p24);
};

/** The per-pixel rule of each op. `s` is the pool pixel (opaque bit set), `zc` the depth to test with. The
 * compute shader (src/sgp/shaders/world_raster.comp) is a transcription of this function. */
inline void ApplyPixel(Instance const& in, uint16_t const s, int const x, int const y, uint16_t const zc,
	uint32_t const* pal, uint16_t const* shade, uint32_t const mask, uint32_t& D, uint16_t& Z)
{
	uint8_t const idx = uint8_t(s & 0xFF);
	bool const L = (((y + (in.parity & 1)) & 1) == (x & 1));
	switch (in.op)
	{
		case Op::Transparent:            D = pal[idx]; break;
		case Op::TransZ:                 if (Z <= zc) { Z = zc; D = pal[idx]; } break;
		case Op::TransZNB:               if (Z <= zc) D = pal[idx]; break;
		case Op::TransZObscured:         if (Z < zc) { Z = zc; D = pal[idx]; } else if (L) D = pal[idx]; break;
		case Op::TransZInc:              if (Z < zc) { Z = zc; D = pal[idx]; } break;
		case Op::TransZIncBurns:         if (Z <= zc) { Z = zc; D = pal[idx]; } break;
		case Op::TransZIncObscure:       if (Z < zc || L) { Z = zc; D = pal[idx]; } break;
		case Op::TransShadow:            D = idx == 254 ? ShadeOf(D, shade) : pal[idx]; break;
		case Op::TransShadowZ:           if (Z < zc) { Z = zc; D = idx == 254 ? ShadeOf(D, shade) : pal[idx]; } break;
		case Op::TransShadowZNB:         if (Z < zc) D = idx == 254 ? ShadeOf(D, shade) : pal[idx]; break;
		case Op::TransShadowZNBObscured:
			if (idx == 254) { if (Z < zc) D = ShadeOf(D, shade); }
			else if (Z <= zc || L) D = pal[idx];
			break;
		case Op::Shadow:                 D = ShadeOf(D, shade); break;
		case Op::ShadowZ:                if (Z < zc) { Z = zc; D = ShadeOf(D, shade); } break;
		case Op::ShadowZNB:              if (Z < zc) D = ShadeOf(D, shade); break;
		case Op::TranslucentZ:
			if (Z <= zc) { Z = zc; D = Expand565(((Quantize565(pal[idx]) >> 1) & mask) + ((Quantize565(D) >> 1) & mask)); }
			break;
		case Op::TranslucentZNB:
			if (Z <= zc) D = Expand565(((Quantize565(pal[idx]) >> 1) & mask) + ((Quantize565(D) >> 1) & mask));
			break;
		case Op::Outline:                if (idx != 254) D = pal[idx]; else if (in.outline != 0) D = Expand565(in.outline); break;
		case Op::OutlineZ:
			if (Z <= zc) { if (idx == 254) D = Expand565(in.outline); else { Z = zc; D = pal[idx]; } }
			break;
		case Op::OutlineZObscuredLE:
			if (Z <= zc) Z = zc; else if (!L) break;
			D = idx == 254 ? Expand565(in.outline) : pal[idx];
			break;
		case Op::OutlineZObscuredLT:
			if (Z < zc) Z = zc; else if (!L) break;
			D = idx == 254 ? Expand565(in.outline) : pal[idx];
			break;
		case Op::OutlineZNB:             if (Z < zc && idx != 254) D = pal[idx]; break;
		case Op::TransShadowZInc:        if (Z <= zc) { Z = zc; D = idx == 254 ? ShadeOf(D, shade) : pal[idx]; } break;
		case Op::TransShadowZIncObscure: if (Z < zc || L) { Z = zc; D = idx == 254 ? ShadeOf(D, shade) : pal[idx]; } break;
		default: break;
	}
}

/** The lighting at a world pixel (Phase 8): ambient + sun + every point light, as a per-channel multiplier. */
inline void ApplyLighting(uint32_t& D, int const x, int const y, Lighting const& L)
{
	if (L.Identity()) return;
	float lr = L.ambientR + L.sunR, lg = L.ambientG + L.sunG, lb = L.ambientB + L.sunB;
	for (PointLight const& p : L.points)
	{
		if (p.radius <= 0.0f) continue;
		float const dx = x - p.x, dy = y - p.y;
		float const d2 = dx * dx + dy * dy;
		float const r2 = p.radius * p.radius;
		if (d2 >= r2) continue;
		float const f = std::pow(1.0f - std::sqrt(d2) / p.radius, L.falloff) * p.intensity;
		lr += f * p.r; lg += f * p.g; lb += f * p.b;
	}
	int const r = int(((D >> 16) & 0xFF) * lr);
	int const g = int(((D >>  8) & 0xFF) * lg);
	int const b = int(( D        & 0xFF) * lb);
	D = uint32_t(std::clamp(r, 0, 255)) << 16 | uint32_t(std::clamp(g, 0, 255)) << 8 | uint32_t(std::clamp(b, 0, 255));
}

/** The depth an instance tests with at a pixel. */
inline uint16_t DepthAt(Instance const& in, Frame const& f, int const x, int const y)
{
	if (in.columns == NO_COLUMNS) return in.z;
	if (in.parity & PER_PIXEL_DEPTH) return f.columns[in.columns + size_t(y - in.y0) * (in.x1 - in.x0) + (x - in.x0)];
	return f.columns[in.columns + (x - in.x0)];
}

/** The CPU implementation. */
struct Target
{
	int w = 0, h = 0;
	std::vector<uint32_t> color;
	std::vector<uint16_t> depth;
	void Clear(Frame const&);
};
void Rasterize(Frame const&, SpritePool const&, Target&);

/** The GPU's work split: for each 16x16 bin of the target, the instances covering it, in order. */
constexpr int BIN = 16;
struct Bins
{
	int cols = 0, rows = 0;
	std::vector<uint32_t> ranges; // 2 per bin: first, count (into items)
	std::vector<uint32_t> items;
	void Build(Frame const&);
};

struct DiffStats
{
	long long pixels = 0, different = 0;
	double percent() const { return pixels ? 100.0 * double(different) / double(pixels) : 0; }
};
/** Compares two 888 images in [x0,x1)x[y0,y1); fills an RGB888 diff picture of w x h (differences magenta over a
 * dimmed copy of a) when diffRgb is given. */
DiffStats Compare(uint32_t const* a, uint32_t const* b, int w, int h, int x0, int y0, int x1, int y1,
	std::vector<uint8_t>* diffRgb = nullptr);

/** 888 -> RGB888 bytes. */
std::vector<uint8_t> ToRgb(uint32_t const* px, int w, int h);

} // namespace WorldPipe
