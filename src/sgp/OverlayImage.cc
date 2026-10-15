#include "OverlayImage.h"

#include <cstddef>

namespace OverlayImage
{

void Rgb565ToRgba(uint16_t const pixel, uint16_t const colorKey, uint8_t out[4])
{
	if (pixel == colorKey)
	{
		out[0] = out[1] = out[2] = out[3] = 0;
		return;
	}
	// Expand 5/6/5 to 8 bits by repeating the top bits in the low ones (255 for full scale).
	uint8_t const r = uint8_t(((pixel >> 11) & 0x1f) * 255 / 31);
	uint8_t const g = uint8_t(((pixel >> 5) & 0x3f) * 255 / 63);
	uint8_t const b = uint8_t((pixel & 0x1f) * 255 / 31);
	out[0] = r;
	out[1] = g;
	out[2] = b;
	out[3] = 255;
}

void Rgb565KeyedToRgba(uint16_t const* const pixels, int const pitchBytes, int const w, int const h,
					   uint16_t const colorKey, uint8_t* const out)
{
	for (int y = 0; y < h; ++y)
	{
		auto const* const row =
			reinterpret_cast<uint16_t const*>(reinterpret_cast<uint8_t const*>(pixels) + size_t(y) * pitchBytes);
		uint8_t* const dst = out + size_t(y) * w * 4;
		for (int x = 0; x < w; ++x) Rgb565ToRgba(row[x], colorKey, dst + size_t(x) * 4);
	}
}

} // namespace OverlayImage
