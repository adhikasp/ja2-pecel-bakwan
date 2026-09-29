#include "WorldSpikeRaster.h"

#include <algorithm>

namespace spike::world {

Sprite DecodeEtrle(uint8_t const* data, uint32_t length, int w, int h, int offX, int offY)
{
	Sprite s;
	s.w = w;
	s.h = h;
	s.offX = offX;
	s.offY = offY;
	s.index.assign(size_t(w) * h, 0);
	s.mask.assign(size_t(w) * h, 0);
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
			}
			else
			{
				for (int i = 0; i < b && p < length; ++i, ++x)
				{
					if (x < w)
					{
						s.index[size_t(y) * w + x] = data[p];
						s.mask[size_t(y) * w + x]  = 1;
					}
					++p;
				}
			}
		}
	}
	return s;
}

void Target::reset(int width, int height, uint16_t clearDepth)
{
	w = width;
	h = height;
	color.assign(size_t(w) * h, 0);
	depth.assign(size_t(w) * h, clearDepth);
	clipL = 0;
	clipT = 0;
	clipR = w;
	clipB = h;
}

void Draw(Target& t, Instance const& in)
{
	Sprite const& s = *in.sprite;
	int const x0 = std::max(in.x, t.clipL), x1 = std::min(in.x + s.w, t.clipR);
	int const y0 = std::max(in.y, t.clipT), y1 = std::min(in.y + s.h, t.clipB);
	if (x0 >= x1 || y0 >= y1) return;
	for (int y = y0; y < y1; ++y)
	{
		int const sy = y - in.y;
		uint8_t const* idx = &s.index[size_t(sy) * s.w];
		uint8_t const* msk = &s.mask[size_t(sy) * s.w];
		uint16_t* dst = &t.color[size_t(y) * t.w];
		uint16_t* dep = &t.depth[size_t(y) * t.w];
		// The legacy obscured blitters pixelate on (screen row parity) == (column parity)
		unsigned const lineFlag = unsigned(y) & 1;
		for (int x = x0; x < x1; ++x)
		{
			int const sx = x - in.x;
			if (!msk[sx]) continue;
			uint16_t const z = in.columnZ.empty() ? in.z : in.columnZ[sx];
			switch (in.op)
			{
				case Op::Plain:
					dst[x] = in.palette[idx[sx]];
					break;
				case Op::DepthGEqual:
					if (dep[x] <= z) { dep[x] = z; dst[x] = in.palette[idx[sx]]; }
					break;
				case Op::DepthGreater:
					if (dep[x] < z) { dep[x] = z; dst[x] = in.palette[idx[sx]]; }
					break;
				case Op::ShadowGreater:
					if (dep[x] < z) { dep[x] = z; dst[x] = t.shade[dst[x]]; }
					break;
				case Op::ObscuredGreater:
					if (dep[x] < z) dep[x] = z;
					else if (lineFlag != (unsigned(x) & 1)) break;
					dst[x] = in.palette[idx[sx]];
					break;
			}
		}
	}
}

DiffStats Compare(uint16_t const* a, uint16_t const* b, int w, int x0, int y0, int x1, int y1,
	std::vector<uint8_t>* diffRgb, int fullH)
{
	DiffStats st;
	if (diffRgb) diffRgb->assign(size_t(w) * fullH * 3, 0);
	for (int y = y0; y < y1; ++y)
	{
		for (int x = x0; x < x1; ++x)
		{
			size_t const i = size_t(y) * w + x;
			bool const d = a[i] != b[i];
			++st.pixels;
			if (d) ++st.different;
			if (diffRgb)
			{
				uint16_t const p = a[i];
				uint8_t const r = uint8_t(((p >> 11) & 31) * 255 / 31), g = uint8_t(((p >> 5) & 63) * 255 / 63), bl = uint8_t((p & 31) * 255 / 31);
				uint8_t* o = &(*diffRgb)[i * 3];
				if (d) { o[0] = 255; o[1] = 0; o[2] = 255; }
				else   { o[0] = r / 3; o[1] = g / 3; o[2] = bl / 3; }
			}
		}
	}
	return st;
}

} // namespace spike::world
