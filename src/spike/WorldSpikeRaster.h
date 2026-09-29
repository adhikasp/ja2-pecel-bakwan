#pragma once
// World renderer spike: the "GPU-style" half. Tiles become instances (sprite from an atlas, palette/shade
// LUT, screen position, depth, depth-test op) in the software renderer's submission order, and are drawn
// the way a fragment shader with a depth buffer would: per-instance depth (or a per-column depth ramp for
// JA2's multi-Z wall strips), GEQUAL/GREATER tests, and shadows as "darken destination" with a depth test.
// It runs on the CPU so it can be checked pixel by pixel against the legacy blitters headless and in CI; each
// op maps to one pipeline state on the GPU (see docs/plan/native-modern-game-decisions.md).

#include <cstdint>
#include <vector>

namespace spike::world {

/** One decoded frame (from ETRLE): palette indices + coverage. */
struct Sprite
{
	int w = 0, h = 0;
	int offX = 0, offY = 0;
	std::vector<uint8_t> index;
	std::vector<uint8_t> mask; // 1 = opaque
};

/** Decodes an ETRLE-compressed frame (0x80|n = n transparent, n = n literal indices, 0 = end of row). */
Sprite DecodeEtrle(uint8_t const* data, uint32_t length, int w, int h, int offX, int offY);

enum class Op : uint8_t
{
	Plain,          // no depth test or write (land)
	DepthGEqual,    // write if depth <= z (BltTransZ)
	DepthGreater,   // write if depth <  z (multi-Z tiles: BltTransZInc)
	ShadowGreater,  // darken the destination if depth < z, write depth (BltShadowZ)
	ObscuredGreater // write if depth < z, else on a checkerboard (BltTransZPixelateObscured)
};

struct Instance
{
	Sprite const*   sprite  = nullptr;
	uint16_t const* palette = nullptr;  // 256 RGB565 entries (the vobject's current shade)
	int             x = 0, y = 0;       // top-left on screen, offsets already applied
	uint16_t        z = 0;
	Op              op = Op::Plain;
	std::vector<uint16_t> columnZ;      // per sprite column depth (multi-Z), empty = z everywhere
};

struct Target
{
	int w = 0, h = 0;
	std::vector<uint16_t> color;   // RGB565
	std::vector<uint16_t> depth;
	int clipL = 0, clipT = 0, clipR = 0, clipB = 0; // [L, R) x [T, B)
	uint16_t const* shade = nullptr; // 65536-entry RGB565 darkening table (shadows)

	void reset(int width, int height, uint16_t clearDepth);
};

void Draw(Target&, Instance const&);

struct DiffStats
{
	long long pixels = 0;
	long long different = 0;
	double percent() const { return pixels ? 100.0 * double(different) / double(pixels) : 0; }
};

/** Compares two RGB565 images inside [x0,x1)x[y0,y1); writes an RGB888 diff image (differences in red over a
 * dimmed copy of a) if diffRgb is not null. */
DiffStats Compare(uint16_t const* a, uint16_t const* b, int w, int x0, int y0, int x1, int y1,
	std::vector<uint8_t>* diffRgb = nullptr, int fullH = 0);

} // namespace spike::world
