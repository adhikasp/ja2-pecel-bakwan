// The world pipeline's per-pixel rules (WorldPipeline.h) against the legacy blitters they stand for: the same
// sprite, destination, depth buffer and clip rectangle through both, compared pixel by pixel, colour and depth.
// This is what CI checks without a GPU; the shader is a transcription of the same rules (checked on a GPU by
// tests/e2e/world_renderer.lua).
#include "gtest/gtest.h"

#include "HImage.h"
#include "Shading.h"
#include "VObject.h"
#include "VObject_Blitters.h"
#include "WorldPipeline.h"

#include <functional>
#include <random>

using namespace WorldPipe;

namespace {

constexpr int W = 64, H = 40;

/** A random ETRLE sprite (runs of transparent and literal pixels, indices including 254) as a video object. */
struct TestSprite
{
	std::unique_ptr<SGPVObject> vo;
	std::vector<uint8_t> data;
	int w, h;

	TestSprite(std::mt19937& rng, int w_, int h_, int offX, int offY) : w(w_), h(h_)
	{
		for (int y = 0; y < h; ++y)
		{
			int x = 0;
			while (x < w)
			{
				int const n = std::min<int>(w - x, 1 + int(rng() % 9));
				if (rng() % 3 == 0)
				{
					data.push_back(uint8_t(0x80 | n));
				}
				else
				{
					data.push_back(uint8_t(n));
					for (int i = 0; i < n; ++i) data.push_back(rng() % 5 == 0 ? 254 : uint8_t(1 + rng() % 250));
				}
				x += n;
			}
			data.push_back(0);
		}
		SGPImage img(uint16_t(w), uint16_t(h), 8);
		img.fFlags = IMAGE_TRLECOMPRESSED;
		SGPPaletteEntry* pal = img.pPalette.Allocate(256);
		std::fill_n(pal, 256, SGPPaletteEntry{});
		uint8_t* pix = img.pImageData.Allocate(data.size());
		std::copy(data.begin(), data.end(), pix);
		img.uiSizePixData = uint32_t(data.size());
		ETRLEObject* e = img.pETRLEObject.Allocate(1);
		*e = ETRLEObject{ 0, uint32_t(data.size()), int16_t(offX), int16_t(offY), uint16_t(h), uint16_t(w) };
		img.usNumberOfObjects = 1;
		vo = std::make_unique<SGPVObject>(&img);
		auto* shade = new UINT16[256];
		for (int i = 0; i < 256; ++i) shade[i] = uint16_t(0x1000 + i * 97);
		vo->pShades[0] = shade;
		vo->CurrentShade(0);
	}
};

struct Buffers
{
	std::vector<uint16_t> color, depth;
	void Random(std::mt19937& rng)
	{
		color.resize(W * H);
		depth.resize(W * H);
		for (auto& c : color) c = uint16_t(rng());
		for (auto& d : depth) d = uint16_t(90 + rng() % 21); // around the instance depth (100)
	}
};

/** An instance the way RenderWorld's recorder makes one from a ClipInfo. */
Instance FromClip(ClipInfo const& ci, SpritePool& pool, Frame& f, Op op, uint16_t z, uint16_t outline, uint16_t const* pal)
{
	ETRLEObject const& e = ci.vobject->SubregionProperties(0);
	SpritePool::Entry const& sp = pool.Get(ci.vobject->PixData(e), e.uiDataLength, e.usWidth, e.usHeight);
	Instance in{};
	in.x0 = uint16_t(ci.iTempX + ci.leftSkip);
	in.y0 = uint16_t(ci.iTempY + ci.topSkip);
	in.x1 = uint16_t(in.x0 + ci.blitLength);
	in.y1 = uint16_t(in.y0 + ci.blitHeight);
	in.ox = int16_t(ci.iTempX);
	in.oy = int16_t(ci.iTempY);
	in.sprite = sp.offset;
	in.spriteW = sp.w;
	in.op = op;
	in.z = z;
	in.outline = outline;
	in.columns = NO_COLUMNS;
	in.palette = f.Palette(pal, nullptr);
	return in;
}

using Legacy = std::function<void(ClipInfo const&, SGPVObject*, int x, int y, uint16_t* buf, uint16_t* zbuf)>;

/** Runs the legacy blitter and the pipeline on the same random state; returns the number of differing pixels. */
int Mismatches(Op op, Legacy const& legacy, bool clipped, uint32_t seed, uint16_t outline = 0x07E0)
{
	std::mt19937 rng(seed);
	int const sw = 10 + int(rng() % 20), sh = 6 + int(rng() % 18);
	TestSprite s(rng, sw, sh, -int(rng() % 5), -int(rng() % 5));
	int const x = clipped ? int(rng() % 60) - 12 : 6 + int(rng() % (W - sw - 8));
	int const y = clipped ? int(rng() % 36) - 10 : 6 + int(rng() % (H - sh - 8));
	SGPRect const clip = clipped ? SGPRect{ 4, 3, W - 5, H - 4 } : SGPRect{ 0, 0, W, H };
	ClipInfo const ci(s.vo.get(), x, y, 0, &clip);
	if (ci.status == ClipInfo::Status::Completely_Clipped) return 0;
	if (!clipped && ci.status != ClipInfo::Status::Not_Clipped) return -1;

	Buffers a;
	a.Random(rng);
	Buffers b = a;
	legacy(ci, s.vo.get(), x, y, a.color.data(), a.depth.data());

	SpritePool pool;
	Frame f;
	f.Clear(W, H);
	f.translucentMask = uint16_t(guiTranslucentMask);
	f.instances.push_back(FromClip(ci, pool, f, op, 100, outline, s.vo->CurrentShade()));
	Target t;
	t.w = W;
	t.h = H;
	t.color.resize(size_t(W) * H);
	for (int i = 0; i < W * H; ++i) t.color[i] = Expand565(b.color[i]);
	t.depth = b.depth;
	Rasterize(f, pool, t);

	int bad = 0;
	for (int i = 0; i < W * H; ++i) bad += (Expand565(a.color[i]) != t.color[i]) + (a.depth[i] != t.depth[i]);
	return bad;
}

void Check(Op op, Legacy const& legacy, bool clipped)
{
	for (uint32_t seed = 1; seed <= 40; ++seed)
	{
		EXPECT_EQ(Mismatches(op, legacy, clipped, seed), 0) << OpName(op) << (clipped ? " clipped" : "") << " seed " << seed;
	}
}

struct ShadeSetup
{
	ShadeSetup()
	{
		for (int i = 0; i < 65536; ++i) ShadeTable[i] = uint16_t((i * 7 + 3) & 0xFFFF);
		guiTranslucentMask = 0x7BEF;
	}
};

constexpr UINT32 P = W * 2; // pitch in bytes
UINT16 const* Pal(SGPVObject* vo) { return vo->CurrentShade(); }

} // namespace

TEST(WorldPipeline, EtrleDecode)
{
	// row 0: 2 transparent, 2 literal (5, 6), end; row 1: 1 literal (7), end
	uint8_t const data[] = { 0x82, 2, 5, 6, 0, 1, 7, 0 };
	uint16_t px[8];
	DecodeEtrle(data, sizeof data, 4, 2, px);
	EXPECT_EQ(px[0], 0);
	EXPECT_EQ(px[2], 0x105);
	EXPECT_EQ(px[3], 0x106);
	EXPECT_EQ(px[4], 0x107);
	EXPECT_EQ(px[5], 0);
}

TEST(WorldPipeline, SpritePoolReusesAndNoticesChanges)
{
	uint8_t data[] = { 2, 5, 6, 0 };
	SpritePool pool;
	auto const a = pool.Get(data, sizeof data, 2, 1);
	auto const b = pool.Get(data, sizeof data, 2, 1);
	EXPECT_EQ(a.offset, b.offset);
	data[1] = 9; // another image at the same address (noticed from the next frame on)
	pool.NextFrame();
	auto const c = pool.Get(data, sizeof data, 2, 1);
	EXPECT_NE(a.offset, c.offset);
	EXPECT_EQ(pool.pixels[c.offset], 0x109);
}

TEST(WorldPipeline, PaletteKeepsThe24BitColours)
{
	Frame f;
	f.Clear(8, 8);
	uint16_t p565[256];
	uint32_t p24[256];
	for (int i = 0; i < 256; ++i)
	{
		p565[i] = uint16_t(i * 257);
		p24[i] = (uint32_t(i) << 16) | (uint32_t(255 - i) << 8) | uint32_t(i);
	}
	uint32_t const a = f.Palette(p565, nullptr);      // no 24-bit palette: the 565 table expanded
	uint16_t p565b[256] = {};
	uint32_t const b = f.Palette(p565b, p24);         // 24-bit colours: used as they are
	EXPECT_NE(a, b);
	for (int i = 0; i < 256; ++i)
	{
		EXPECT_EQ(f.palettes[a * 256 + i], Expand565(p565[i]));
		EXPECT_EQ(f.palettes[b * 256 + i], p24[i]);
	}
}

TEST(WorldPipeline, BinsKeepSubmissionOrder){
	Frame f;
	f.Clear(40, 20);
	Instance in{};
	in.x0 = 0; in.y0 = 0; in.x1 = 40; in.y1 = 20;
	f.instances.push_back(in);
	in.x0 = 20; in.x1 = 30;
	f.instances.push_back(in);
	Bins b;
	b.Build(f);
	EXPECT_EQ(b.cols, 3);
	EXPECT_EQ(b.rows, 2);
	// bin 1 (x 16..31) has both, in order
	EXPECT_EQ(b.ranges[1 * 2 + 1], 2u);
	EXPECT_EQ(b.items[b.ranges[1 * 2]], 0u);
	EXPECT_EQ(b.items[b.ranges[1 * 2] + 1], 1u);
	// bin 0 only the first
	EXPECT_EQ(b.ranges[0 * 2 + 1], 1u);
}

TEST(WorldPipeline, OpsMatchLegacyBlitters)
{
	ShadeSetup const shades;
	using V = SGPVObject*;
	// Unclipped: the Blt8BPPDataTo16BPPBuffer* blitters
	Check(Op::Transparent, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t*) { Blt8BPPDataTo16BPPBufferTransparent(c, P, vo, x, y, 0); }, false);
	Check(Op::TransZ, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferTransZ(c, P, z, 100, vo, x, y, 0); }, false);
	Check(Op::TransZNB, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferTransZNB(c, P, z, 100, vo, x, y, 0); }, false);
	Check(Op::TransZObscured, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferTransZPixelateObscured(c, P, z, 100, vo, x, y, 0); }, false);
	Check(Op::TransShadow, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t*) { Blt8BPPDataTo16BPPBufferTransShadow(c, P, vo, x, y, 0, Pal(vo)); }, false);
	Check(Op::TransShadowZ, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferTransShadowZ(c, P, z, 100, vo, x, y, 0, Pal(vo)); }, false);
	Check(Op::TransShadowZNB, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferTransShadowZNB(c, P, z, 100, vo, x, y, 0, Pal(vo)); }, false);
	Check(Op::TransShadowZNBObscured, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferTransShadowZNBObscured(c, P, z, 100, vo, x, y, 0, Pal(vo)); }, false);
	Check(Op::Shadow, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t*) { Blt8BPPDataTo16BPPBufferShadow(c, P, vo, x, y, 0); }, false);
	Check(Op::ShadowZ, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferShadowZ(c, P, z, 100, vo, x, y, 0); }, false);
	Check(Op::ShadowZNB, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferShadowZNB(c, P, z, 100, vo, x, y, 0); }, false);
	Check(Op::TranslucentZ, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferTransZTranslucent(c, P, z, 100, vo, x, y, 0); }, false);
	Check(Op::TranslucentZNB, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferTransZNBTranslucent(c, P, z, 100, vo, x, y, 0); }, false);
	Check(Op::Outline, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t*) { Blt8BPPDataTo16BPPBufferOutline(c, P, vo, x, y, 0, 0x07E0); }, false);
	Check(Op::OutlineZ, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferOutlineZ(c, P, z, 100, vo, x, y, 0, 0x07E0); }, false);
	Check(Op::OutlineZObscuredLT, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferOutlineZPixelateObscured(c, P, z, 100, vo, x, y, 0, 0x07E0); }, false);
	Check(Op::OutlineZNB, [](ClipInfo const&, V vo, int x, int y, uint16_t* c, uint16_t* z) { Blt8BPPDataTo16BPPBufferOutlineZNB(c, P, z, 100, vo, x, y, 0); }, false);

	// Clipped: the Blt*(ClipInfo) blitters, sprites hanging over every side of the clip rectangle
	Check(Op::Transparent, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t*) { BltTransparent(ci, c, P); }, true);
	Check(Op::TransZ, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t* z) { BltTransZ(ci, c, P, z, 100); }, true);
	Check(Op::TransZNB, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t* z) { BltTransZNB(ci, c, P, z, 100); }, true);
	Check(Op::TransZObscured, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t* z) { BltTransZPixelateObscured(ci, c, P, z, 100); }, true);
	Check(Op::TransShadow, [](ClipInfo const& ci, V vo, int, int, uint16_t* c, uint16_t*) { BltTransShadow(ci, c, P, Pal(vo)); }, true);
	Check(Op::TransShadowZ, [](ClipInfo const& ci, V vo, int, int, uint16_t* c, uint16_t* z) { BltTransShadowZ(ci, c, P, z, 100, Pal(vo)); }, true);
	Check(Op::TransShadowZNB, [](ClipInfo const& ci, V vo, int, int, uint16_t* c, uint16_t* z) { BltTransShadowZNB(ci, c, P, z, 100, Pal(vo)); }, true);
	Check(Op::TransShadowZNBObscured, [](ClipInfo const& ci, V vo, int, int, uint16_t* c, uint16_t* z) { BltTransShadowZNBObscured(ci, c, P, z, 100, Pal(vo)); }, true);
	Check(Op::Shadow, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t*) { BltShadow(ci, c, P); }, true);
	Check(Op::ShadowZ, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t* z) { BltShadowZ(ci, c, P, z, 100); }, true);
	Check(Op::ShadowZNB, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t* z) { BltShadowZNB(ci, c, P, z, 100); }, true);
	Check(Op::TranslucentZNB, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t* z) { BltTransZNBTranslucent(ci, c, P, z, 100); }, true);
	Check(Op::Outline, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t*) { BltOutline(ci, c, P, 0x07E0); }, true);
	Check(Op::OutlineZ, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t* z) { BltOutlineZ(ci, c, P, z, 100, 0x07E0); }, true);
	Check(Op::OutlineZObscuredLE, [](ClipInfo const& ci, V, int, int, uint16_t* c, uint16_t* z) { BltOutlineZPixelateObscured(ci, c, P, z, 100, 0x07E0); }, true);
}

TEST(WorldPipeline, OutlineTransparentLeavesTheDestination)
{
	ShadeSetup const shades;
	// BltOutline with SGP_TRANSPARENT (physics objects hanging off the view): index 254 draws nothing
	EXPECT_EQ(Mismatches(Op::Outline, [](ClipInfo const& ci, SGPVObject*, int, int, uint16_t* c, uint16_t*) {
		BltOutline(ci, c, P, SGP_TRANSPARENT); }, true, 7, 0), 0);
}

TEST(WorldPipeline, TheComparisonNoticesAWrongRule)
{
	ShadeSetup const shades;
	// The check above is not vacuous: a depth-writing op against a non-writing blitter, or a shadow against a
	// palette blit, differs
	int total = 0, total2 = 0;
	for (uint32_t seed = 1; seed <= 10; ++seed)
	{
		total += Mismatches(Op::TransZ, [](ClipInfo const& ci, SGPVObject*, int, int, uint16_t* c, uint16_t* z) { BltTransZNB(ci, c, P, z, 100); }, true, seed);
		total2 += Mismatches(Op::ShadowZ, [](ClipInfo const& ci, SGPVObject*, int, int, uint16_t* c, uint16_t* z) { BltTransZ(ci, c, P, z, 100); }, true, seed);
	}
	EXPECT_GT(total, 0);
	EXPECT_GT(total2, 0);
}
