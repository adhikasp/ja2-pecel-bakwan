#include "ScreenBackdrop.h"

#include "UILayout.h"
#include "VSurface.h"

#include <algorithm>


SGPVSurface* BeginScreenBackdrop()
{
	if (!g_ui.isBigScreen()) return nullptr;
	SGPVSurface* const s = AddVideoSurface(640, 480, 16);
	s->Fill(0);
	return s;
}


void BltScreenBackdrop(SGPVSurface const* const art)
{
	if (!g_ui.isBigScreen()) return;

	UINT32 const sw = g_ui.m_screenWidth;
	UINT32 const sh = g_ui.m_screenHeight;
	UINT32 const aw = art->Width();
	UINT32 const ah = art->Height();

	// Cover: crop the source so that its aspect ratio matches the screen.
	SGPBox src = { 0, 0, static_cast<UINT16>(aw), static_cast<UINT16>(ah) };
	if (sw * ah >= sh * aw)
	{ // screen is wider than the art: crop rows
		UINT32 const h = std::max<UINT32>(1, aw * sh / sw);
		src.h = h;
		src.y = (ah - h) / 2;
	}
	else
	{ // screen is taller: crop columns
		UINT32 const w = std::max<UINT32>(1, ah * sw / sh);
		src.w = w;
		src.x = (aw - w) / 2;
	}
	SGPBox const dst = { 0, 0, static_cast<UINT16>(sw), static_cast<UINT16>(sh) };
	BltStretchVideoSurface(FRAME_BUFFER, art, &src, &dst);

	// Dim it so the (undimmed) standard box stands out from its surroundings.
	SGPBox const std = g_ui.stdBox();
	if (std.x > 0)
	{
		FRAME_BUFFER->ShadowRect(0, 0, std.x - 1, sh - 1);
		FRAME_BUFFER->ShadowRect(std.x + std.w, 0, sw - 1, sh - 1);
	}
	if (std.y > 0)
	{
		FRAME_BUFFER->ShadowRect(std.x, 0, std.x + std.w - 1, std.y - 1);
		FRAME_BUFFER->ShadowRect(std.x, std.y + std.h, std.x + std.w - 1, sh - 1);
	}
}


void EndScreenBackdrop(SGPVSurface* const scratch)
{
	if (!scratch) return;
	BltScreenBackdrop(scratch);
	DeleteVideoSurface(scratch);
}
