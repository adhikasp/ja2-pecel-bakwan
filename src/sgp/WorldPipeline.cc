#include "WorldPipeline.h"

#include <algorithm>

namespace WorldPipe {

char const* OpName(Op const op)
{
	static char const* const names[] = {
		"Transparent", "TransZ", "TransZNB", "TransZObscured", "TransZInc", "TransZIncBurns", "TransZIncObscure",
		"TransShadow", "TransShadowZ", "TransShadowZNB", "TransShadowZNBObscured", "Shadow", "ShadowZ", "ShadowZNB",
		"TranslucentZ", "TranslucentZNB", "Outline", "OutlineZ", "OutlineZObscuredLE", "OutlineZObscuredLT",
		"OutlineZNB", "TransShadowZInc", "TransShadowZIncObscure" };
	static_assert(std::size(names) == size_t(Op::Count));
	return size_t(op) < std::size(names) ? names[size_t(op)] : "?";
}

void DecodeEtrle(uint8_t const* data, uint32_t length, int w, int h, uint16_t* out)
{
	std::fill_n(out, size_t(w) * h, uint16_t(0));
	uint32_t p = 0;
	for (int y = 0; y < h && p < length; ++y)
	{
		int x = 0;
		while (p < length)
		{
			uint8_t const b = data[p++];
			if (b == 0) break; // end of row
			if (b & 0x80)
			{
				x += b & 0x7F;
				continue;
			}
			for (int i = 0; i < b && p < length; ++i, ++x, ++p)
			{
				if (x < w) out[size_t(y) * w + x] = uint16_t(0x100 | data[p]);
			}
		}
	}
}

static uint32_t HashData(uint8_t const* d, uint32_t n)
{
	// FNV-1a over the start and the end: enough to notice another image loaded at a freed address
	uint32_t h = 2166136261u;
	uint32_t const k = std::min<uint32_t>(n, 64);
	for (uint32_t i = 0; i < k; ++i) h = (h ^ d[i]) * 16777619u;
	for (uint32_t i = n - k; i < n; ++i) h = (h ^ d[i]) * 16777619u;
	return h ^ n;
}

SpritePool::Entry const& SpritePool::Get(uint8_t const* etrle, uint32_t length, int w, int h)
{
	uint32_t const hash = HashData(etrle, length);
	auto it = byData.find(etrle);
	if (it != byData.end() && it->second.length == length && it->second.w == w && it->second.h == h && it->second.hash == hash)
	{
		return it->second;
	}
	Entry e{ uint32_t(pixels.size()), uint16_t(w), uint16_t(h), length, hash };
	pixels.resize(pixels.size() + size_t(w) * h);
	DecodeEtrle(etrle, length, w, h, pixels.data() + e.offset);
	return byData[etrle] = e;
}

void SpritePool::Reset()
{
	pixels.clear();
	byData.clear();
	uploaded = 0;
	++generation;
}

void Frame::Clear(int const w, int const h)
{
	width = w;
	height = h;
	instances.clear();
	palettes.clear();
	columns.clear();
	paletteIndex.clear();
}

uint32_t Frame::Palette(uint16_t const* const p)
{
	auto it = paletteIndex.find(p);
	if (it != paletteIndex.end()) return it->second;
	uint32_t const i = uint32_t(palettes.size() / 256);
	palettes.insert(palettes.end(), p, p + 256);
	paletteIndex.emplace(p, i);
	return i;
}

void Target::Clear(Frame const& f)
{
	w = f.width;
	h = f.height;
	color.assign(size_t(w) * h, f.clearColor);
	depth.assign(size_t(w) * h, f.clearDepth);
}

void Rasterize(Frame const& f, SpritePool const& pool, uint16_t const* const shade, Target& t)
{
	for (Instance const& in : f.instances)
	{
		uint16_t const* const pal = &f.palettes[size_t(in.palette) * 256];
		int const x1 = std::min<int>(in.x1, t.w), y1 = std::min<int>(in.y1, t.h);
		for (int y = in.y0; y < y1; ++y)
		{
			uint16_t const* const src = &pool.pixels[in.sprite + size_t(y - in.oy) * in.spriteW];
			uint16_t* const dst = &t.color[size_t(y) * t.w];
			uint16_t* const dep = &t.depth[size_t(y) * t.w];
			for (int x = in.x0; x < x1; ++x)
			{
				uint16_t const s = src[x - in.ox];
				if (!(s & 0x100)) continue;
				uint16_t const zc = DepthAt(in, f, x, y);
				ApplyPixel(in, s, x, y, zc, pal, shade, f.translucentMask, dst[x], dep[x]);
			}
		}
	}
}

void Bins::Build(Frame const& f)
{
	cols = (f.width + BIN - 1) / BIN;
	rows = (f.height + BIN - 1) / BIN;
	size_t const n = size_t(cols) * rows;
	std::vector<uint32_t> count(n, 0);
	auto each = [&](Instance const& in, auto&& fn) {
		if (in.x0 >= in.x1 || in.y0 >= in.y1) return;
		int const bx0 = in.x0 / BIN, bx1 = std::min(cols - 1, (in.x1 - 1) / BIN);
		int const by0 = in.y0 / BIN, by1 = std::min(rows - 1, (in.y1 - 1) / BIN);
		for (int by = by0; by <= by1; ++by)
			for (int bx = bx0; bx <= bx1; ++bx) fn(size_t(by) * cols + bx);
	};
	for (Instance const& in : f.instances) each(in, [&](size_t b) { ++count[b]; });
	ranges.assign(n * 2, 0);
	uint32_t total = 0;
	for (size_t b = 0; b < n; ++b)
	{
		ranges[b * 2] = total;
		total += count[b];
	}
	items.resize(total);
	std::vector<uint32_t> fill(n, 0);
	for (uint32_t i = 0; i < f.instances.size(); ++i)
	{
		each(f.instances[i], [&](size_t b) { items[ranges[b * 2] + fill[b]++] = i; });
	}
	for (size_t b = 0; b < n; ++b) ranges[b * 2 + 1] = count[b];
}

DiffStats Compare(uint16_t const* a, uint16_t const* b, int const w, int const h, int x0, int y0, int x1, int y1,
	std::vector<uint8_t>* const diffRgb)
{
	DiffStats st;
	if (diffRgb) diffRgb->assign(size_t(w) * h * 3, 0);
	x0 = std::max(x0, 0); y0 = std::max(y0, 0);
	x1 = std::min(x1, w); y1 = std::min(y1, h);
	for (int y = y0; y < y1; ++y)
	{
		for (int x = x0; x < x1; ++x)
		{
			size_t const i = size_t(y) * w + x;
			bool const d = a[i] != b[i];
			++st.pixels;
			if (d) ++st.different;
			if (!diffRgb) continue;
			uint8_t* o = &(*diffRgb)[i * 3];
			if (d)
			{
				o[0] = 255; o[1] = 0; o[2] = 255;
			}
			else
			{
				uint16_t const p = a[i];
				uint32_t const r = (p >> 11) & 31, g = (p >> 5) & 63, bl = p & 31;
				o[0] = uint8_t((r << 3 | r >> 2) / 3);
				o[1] = uint8_t((g << 2 | g >> 4) / 3);
				o[2] = uint8_t((bl << 3 | bl >> 2) / 3);
			}
		}
	}
	return st;
}

std::vector<uint8_t> ToRgb(uint16_t const* px, int const w, int const h)
{
	std::vector<uint8_t> rgb(size_t(w) * h * 3);
	for (size_t i = 0; i < size_t(w) * h; ++i)
	{
		uint16_t const p = px[i];
		uint32_t const r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
		rgb[i * 3 + 0] = uint8_t(r << 3 | r >> 2);
		rgb[i * 3 + 1] = uint8_t(g << 2 | g >> 4);
		rgb[i * 3 + 2] = uint8_t(b << 3 | b >> 2);
	}
	return rgb;
}

} // namespace WorldPipe
