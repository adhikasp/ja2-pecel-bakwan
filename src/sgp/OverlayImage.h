#pragma once
// The debug overlays' CPU-side image work, kept apart from any SDL_Renderer or GPU device so a unit test can
// check it: the FPS counter draws an RGB565 surface whose black (colour key 0) is transparent, and the SDL_GPU
// compositor needs that as premultiplied RGBA8.

#include <cstdint>

namespace OverlayImage
{
/** One RGB565 pixel to RGB888 in @a out; @a colorKey is transparent (all zero). */
void Rgb565ToRgba(uint16_t pixel, uint16_t colorKey, uint8_t out[4]);

/** A whole RGB565 surface (top-down rows of @a w pixels, @a pitchBytes apart) to premultiplied RGBA8 into
	 * @a out (w * h * 4 bytes): every pixel equal to @a colorKey becomes fully transparent. */
void Rgb565KeyedToRgba(uint16_t const* pixels, int pitchBytes, int w, int h, uint16_t colorKey, uint8_t* out);
}
