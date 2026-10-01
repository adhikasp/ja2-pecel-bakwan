#pragma once
// The world renderer's pipeline (docs/plan/native-modern-game.md, Phase 8).
//
// A frame of the tactical world is a list of sprite instances in the order the legacy renderer (RenderTiles)
// submits its blits. Each instance names one of the legacy blitters as an Op, which fixes what it does to one
// destination pixel: which depth test it uses, whether it writes depth, and how the colour comes out (palette
// lookup, shade-table darkening of the destination, 50 % translucency, outline colour, checkerboard pixelation).
// Rasterising the instances in order therefore gives the legacy picture pixel for pixel.
//
// Two implementations run the same per-pixel rules (ApplyPixel below):
//  - the GPU one (WorldGpu.h): a compute shader on SDL_GPU, one thread per pixel walking the instances that
//    cover its 16x16 bin, in submission order; colour and depth stay in registers, so ops that read the
//    destination (shadows, translucency) are exact;
//  - the CPU one here (Rasterize), used headless, in CI and as the reference for the shader.
// Neither depends on game code: the game side (RenderWorld.cc) records the instances.

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
	uint16_t outline;          // outline colour (RGB565)
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

struct Frame
{
	int width = 0, height = 0;             // the render target (the world buffer)
	uint16_t clearColor = 0, clearDepth = 0;
	uint16_t translucentMask = 0x7BEF;     // guiTranslucentMask
	std::vector<Instance> instances;
	std::vector<uint16_t> palettes;        // 256 entries each
	std::vector<uint16_t> columns;         // per-column depths of the strip ops
	std::unordered_map<uint16_t const*, uint32_t> paletteIndex;
	struct PaletteSlot { uint16_t const* p; uint32_t index; };
	PaletteSlot paletteCache[1024] = {}; // direct-mapped in front of paletteIndex

	void Clear(int w, int h);
	/** Index of a palette in this frame (copied the first time). */
	uint32_t Palette(uint16_t const* p);
};

/** The per-pixel rule of each op. `s` is the pool pixel (opaque bit set), `zc` the depth to test with. The
 * compute shader (src/sgp/shaders/world_raster.comp) is a transcription of this function. */
inline void ApplyPixel(Instance const& in, uint16_t const s, int const x, int const y, uint16_t const zc,
	uint16_t const* pal, uint16_t const* shade, uint16_t const mask, uint16_t& D, uint16_t& Z)
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
		case Op::TransShadow:            D = idx == 254 ? shade[D] : pal[idx]; break;
		case Op::TransShadowZ:           if (Z < zc) { Z = zc; D = idx == 254 ? shade[D] : pal[idx]; } break;
		case Op::TransShadowZNB:         if (Z < zc) D = idx == 254 ? shade[D] : pal[idx]; break;
		case Op::TransShadowZNBObscured:
			if (idx == 254) { if (Z < zc) D = shade[D]; }
			else if (Z <= zc || L) D = pal[idx];
			break;
		case Op::Shadow:                 D = shade[D]; break;
		case Op::ShadowZ:                if (Z < zc) { Z = zc; D = shade[D]; } break;
		case Op::ShadowZNB:              if (Z < zc) D = shade[D]; break;
		case Op::TranslucentZ:
			if (Z <= zc) { Z = zc; D = uint16_t(((pal[idx] >> 1) & mask) + ((D >> 1) & mask)); }
			break;
		case Op::TranslucentZNB:
			if (Z <= zc) D = uint16_t(((pal[idx] >> 1) & mask) + ((D >> 1) & mask));
			break;
		case Op::Outline:                if (idx != 254) D = pal[idx]; else if (in.outline != 0) D = in.outline; break;
		case Op::OutlineZ:
			if (Z <= zc) { if (idx == 254) D = in.outline; else { Z = zc; D = pal[idx]; } }
			break;
		case Op::OutlineZObscuredLE:
			if (Z <= zc) Z = zc; else if (!L) break;
			D = idx == 254 ? in.outline : pal[idx];
			break;
		case Op::OutlineZObscuredLT:
			if (Z < zc) Z = zc; else if (!L) break;
			D = idx == 254 ? in.outline : pal[idx];
			break;
		case Op::OutlineZNB:             if (Z < zc && idx != 254) D = pal[idx]; break;
		case Op::TransShadowZInc:        if (Z <= zc) { Z = zc; D = idx == 254 ? shade[D] : pal[idx]; } break;
		case Op::TransShadowZIncObscure: if (Z < zc || L) { Z = zc; D = idx == 254 ? shade[D] : pal[idx]; } break;
		default: break;
	}
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
	std::vector<uint16_t> color, depth;
	void Clear(Frame const&);
};
void Rasterize(Frame const&, SpritePool const&, uint16_t const* shadeTable, Target&);

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
/** Compares two RGB565 images in [x0,x1)x[y0,y1); fills an RGB888 diff picture of w x h (differences magenta over a
 * dimmed copy of a) when diffRgb is given. */
DiffStats Compare(uint16_t const* a, uint16_t const* b, int w, int h, int x0, int y0, int x1, int y1,
	std::vector<uint8_t>* diffRgb = nullptr);

/** RGB565 -> RGB888 by bit replication (what the software presentation shows). */
std::vector<uint8_t> ToRgb(uint16_t const* px, int w, int h);

} // namespace WorldPipe
