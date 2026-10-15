// The debug overlays' pixel conversion (OverlayImage.h), checked without a window: the FPS counter's RGB565
// surface becomes premultiplied RGBA8, with its black colour key fully transparent.
#include "gtest/gtest.h"

#include "OverlayImage.h"

TEST(OverlayImage, ColourKeyIsTransparent)
{
	uint8_t out[4] = { 9, 9, 9, 9 };
	OverlayImage::Rgb565ToRgba(0x0000, 0x0000, out);
	EXPECT_EQ(out[0], 0);
	EXPECT_EQ(out[1], 0);
	EXPECT_EQ(out[2], 0);
	EXPECT_EQ(out[3], 0);
}

TEST(OverlayImage, ChannelsExpandToFullScale)
{
	// Pure red 0xF800, pure green 0x07E0, pure blue 0x001F, white 0xFFFF.
	uint8_t r[4], g[4], b[4], w[4];
	OverlayImage::Rgb565ToRgba(0xF800, 0, r);
	OverlayImage::Rgb565ToRgba(0x07E0, 0, g);
	OverlayImage::Rgb565ToRgba(0x001F, 0, b);
	OverlayImage::Rgb565ToRgba(0xFFFF, 0, w);
	EXPECT_EQ(r[0], 255);
	EXPECT_EQ(r[1], 0);
	EXPECT_EQ(r[2], 0);
	EXPECT_EQ(r[3], 255);
	EXPECT_EQ(g[0], 0);
	EXPECT_EQ(g[1], 255);
	EXPECT_EQ(g[2], 0);
	EXPECT_EQ(g[3], 255);
	EXPECT_EQ(b[0], 0);
	EXPECT_EQ(b[1], 0);
	EXPECT_EQ(b[2], 255);
	EXPECT_EQ(b[3], 255);
	EXPECT_EQ(w[0], 255);
	EXPECT_EQ(w[1], 255);
	EXPECT_EQ(w[2], 255);
	EXPECT_EQ(w[3], 255);
}

TEST(OverlayImage, RowsFollowThePitch)
{
	// A 2x2 surface whose rows are 8 bytes apart (2 pixels of padding): the padding must not be read as pixels.
	uint16_t pixels[8] = { 0xF800, 0x0000, 0x1234, 0x5678, 0x0000, 0x07E0, 0x9ABC, 0xDEF0 };
	uint8_t out[2 * 2 * 4] = {};
	OverlayImage::Rgb565KeyedToRgba(pixels, int(sizeof(pixels) / 2), 2, 2, 0x0000, out);
	// row 0: red, transparent
	EXPECT_EQ(out[0], 255);
	EXPECT_EQ(out[3], 255);
	EXPECT_EQ(out[4 + 3], 0);
	// row 1: transparent, green
	EXPECT_EQ(out[8 + 3], 0);
	EXPECT_EQ(out[12 + 1], 255);
	EXPECT_EQ(out[12 + 3], 255);
}
