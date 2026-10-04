#include "Debug.h"
#include "HImage.h"
#include "Shading.h"
#include "VObject_Blitters.h"
#include "VSurface.h"
#include "Video.h"
#include "TextRegistry.h"

#include "SDL3/SDL.h"
#include <string_theory/format>
#include <string_theory/string>

#include <algorithm>
#include <stdexcept>

extern SGPVSurface* gpVSurfaceHead;

namespace {

// Helper class to set a SDL surface's clipping rectangle to the equivalent
// of JA2's global clipping rectangle. Restores the previous clipping
// rectangle when it is destroyed.
class ApplyClippingRect
{
	SDL_Surface * surface;
	SDL_Rect oldClipRect;

public:
	ApplyClippingRect(SDL_Surface * s) : surface(s)
	{
          SDL_GetSurfaceClipRect(s, &oldClipRect);

          SGPRect clipRect = GetClippingRect();
          SDL_Rect newClipRect{clipRect.iLeft, clipRect.iTop,
                               clipRect.iRight - clipRect.iLeft + 1,
                               clipRect.iBottom - clipRect.iTop + 1};
          SDL_SetSurfaceClipRect(s, &newClipRect);
	}

	~ApplyClippingRect()
	{
		SDL_SetSurfaceClipRect(surface, &oldClipRect);
	}
};
}

SGPVSurface::SGPVSurface(UINT16 const w, UINT16 const h, UINT8 const bpp) :
	p16BPPPalette(),
	next_(gpVSurfaceHead)
{
	Assert(w > 0 && h > 0);

	SDL_PixelFormat pixelFormat;
	switch (bpp)
	{
		case 8:
			pixelFormat = SDL_PIXELFORMAT_INDEX8;
			break;

		case 16:
		{
			pixelFormat = SDL_PIXELFORMAT_RGB565;
			break;
		}

		default:
			throw std::logic_error("Tried to create video surface with invalid bpp, must be 8 or 16.");
	}
	SDL_Surface * const s = SDL_CreateSurface(w, h, pixelFormat);
	if (!s) throw std::runtime_error("Failed to create SDL surface");
	surface_.reset(s);
	gpVSurfaceHead = this;
}


SGPVSurface::SGPVSurface(SDL_Surface* const s) :
	surface_(s),
	p16BPPPalette(),
	next_(gpVSurfaceHead)
{
	gpVSurfaceHead = this;
}


SDL_Surface* SGPVSurface::Resize(UINT16 const w, UINT16 const h)
{
	Assert(w > 0 && h > 0);
	TextRegistry::OnSurfaceDeleted(this); // what was printed here is gone
	SDL_Surface* const s = SDL_CreateSurface(w, h, surface_->format);
	if (!s) throw std::runtime_error("Failed to create SDL surface");
	if (SDL_Palette* const pal = SDL_GetSurfacePalette(surface_.get())) SDL_SetSurfacePalette(s, pal);
	SDL_SetSurfaceClipRect(s, nullptr);
	surface_.reset(s);
	return s;
}


SGPVSurface::~SGPVSurface()
{
	TextRegistry::OnSurfaceDeleted(this);
	for (SGPVSurface** anchor = &gpVSurfaceHead;; anchor = &(*anchor)->next_)
	{
		if (*anchor != this) continue;
		*anchor = next_;
		break;
	}

	if (p16BPPPalette) delete[] p16BPPPalette;
}


void SGPVSurface::SetPalette(const SGPPaletteEntry* const src_pal)
{
	// SDL's palettes are reference counted, so we can
	// a) safely insert the newly created palette into a surface without
	//    checking for an existing one first
	// b) destroy the palette after doing so
	auto * palette = SDL_CreatePalette(256);
	SDL_SetPaletteColors(palette, src_pal, 0, 256);
	SDL_SetSurfacePalette(surface_.get(), palette);
	SDL_DestroyPalette(palette);

	if (p16BPPPalette != NULL) delete[] p16BPPPalette;
	p16BPPPalette = Create16BPPPalette(src_pal);
}


void SGPVSurface::SetTransparency(const COLORVAL colour)
{
	Uint32 colour_key;
	switch (BPP())
	{
		case  8: colour_key = colour;                break;
		case 16: colour_key = Get16BPPColor(colour); break;

		default: abort(); // HACK000E
	}
	SDL_SetSurfaceColorKey(surface_.get(), true, colour_key);
}


void SGPVSurface::Fill(const UINT16 colour)
{
	SDL_FillSurfaceRect(surface_.get(), NULL, colour);
}


/** Shading with layers: the UI surfaces have "transparent" pixels (where the world shows through), and
 * darkening those has to darken the world at composition time. The transparent colour has neighbours that
 * stand for black with an alpha, see UiLayerShadow*() in Video.h. Everything else is darkened as usual. */
static void LayeredShadowVideoSurfaceRect(SDL_Surface * dst, INT32 x1, INT32 y1, INT32 x2, INT32 y2, float shadeFactor)
{
	SGPRect const clip = GetClippingRect();
	x1 = std::max<INT32>(x1, clip.iLeft);
	y1 = std::max<INT32>(y1, clip.iTop);
	x2 = std::min<INT32>({ x2, INT32(clip.iRight), INT32(dst->w - 1) });
	y2 = std::min<INT32>({ y2, INT32(clip.iBottom), INT32(dst->h - 1) });
	x1 = std::max<INT32>(x1, 0);
	y1 = std::max<INT32>(y1, 0);
	if (x1 > x2 || y1 > y2) return;

	int const mod = static_cast<int>(255 * shadeFactor);
	float const darkening = 1.0f - shadeFactor;
	for (INT32 y = y1; y <= y2; ++y)
	{
		auto* row = reinterpret_cast<UINT16*>(static_cast<UINT8*>(dst->pixels) + y * dst->pitch);
		for (INT32 x = x1; x <= x2; ++x)
		{
			UINT16 const p = row[x];
			if (UiLayerIsTransparentFamily(p))
			{
				row[x] = UiLayerDarken(p, darkening);
				continue;
			}
			int r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
			r = ((r << 3 | r >> 2) * mod / 255) >> 3;
			g = ((g << 2 | g >> 4) * mod / 255) >> 2;
			b = ((b << 3 | b >> 2) * mod / 255) >> 3;
			row[x] = static_cast<UINT16>(r << 11 | g << 5 | b);
		}
	}
}


static void InternalShadowVideoSurfaceRect(SDL_Surface * dst, INT32 x1, INT32 y1, INT32 x2, INT32 y2, float shadeFactor)
{
	if (VideoIsLayered() && dst->format == SDL_PIXELFORMAT_RGB565)
	{
		LayeredShadowVideoSurfaceRect(dst, x1, y1, x2, y2, shadeFactor);
		return;
	}

	ApplyClippingRect acr{ dst };

	Uint8 modF = static_cast<Uint8>(255 * shadeFactor);
	SDL_SetSurfaceColorMod(dst, modF, modF, modF);

	SDL_Rect blitRect{ x1, y1, x2 - x1 + 1, y2 - y1 + 1 };
	SDL_BlitSurface(dst, &blitRect, dst, &blitRect);

	SDL_SetSurfaceColorMod(dst, 255, 255, 255);
}


void SGPVSurface::ShadowRect(INT32 const x1, INT32 const y1, INT32 const x2, INT32 const y2)
{
	InternalShadowVideoSurfaceRect(surface_.get(), x1, y1, x2, y2, GetShadeTablePercent());
}


void SGPVSurface::ShadowRectUsingLowPercentTable(INT32 const x1, INT32 const y1, INT32 const x2, INT32 const y2)
{
	// 0.8 is the factor that was used to compute the IntensityTable in vanilla.
	InternalShadowVideoSurfaceRect(surface_.get(), x1, y1, x2, y2, 0.80f);
}


void SGPVSurface::ShadowRectWithFactor(INT32 const x1, INT32 const y1, INT32 const x2, INT32 const y2, float const shadeFactor)
{
	InternalShadowVideoSurfaceRect(surface_.get(), x1, y1, x2, y2, std::clamp(shadeFactor, 0.0f, 1.0f));
}


SGPVSurface* AddVideoSurfaceFromFile(const char* const Filename)
{
	AutoSGPImage img(CreateImage(Filename, IMAGE_ALLIMAGEDATA));

	auto vs = std::make_unique<SGPVSurface>(img->usWidth, img->usHeight, img->ubBitDepth);

	// Copy palette from the SGPImage to the SGPVSurface if necessary.
	if (img->ubBitDepth == 8) vs->SetPalette(img->pPalette);

	auto && sdlSurface{ vs->GetSDLSurface() };
	auto const format{ sdlSurface.format };

	// Leave it to SDL to copy the pixel data from the image to the surface.
	SDL_ConvertPixels(img->usWidth, img->usHeight,
		format, &*img->pImageData, img->usWidth * img->ubBitDepth / 8,
		format, sdlSurface.pixels, sdlSurface.pitch);

	return vs.release();
}

void BltVideoSurfaceHalf(SGPVSurface* const dst, SGPVSurface* const src, INT32 const DestX, INT32 const DestY, SGPBox const* const src_rect)
{
	SGPVSurface::Lock lsrc(src);
	SGPVSurface::Lock ldst(dst);
	UINT8*  const SrcBuf         = lsrc.Buffer<UINT8>();
	UINT32  const SrcPitchBYTES  = lsrc.Pitch();
	UINT16* const DestBuf        = ldst.Buffer<UINT16>();
	UINT32  const DestPitchBYTES = ldst.Pitch();
	Blt8BPPDataTo16BPPBufferHalf(DestBuf, DestPitchBYTES, src, SrcBuf, SrcPitchBYTES, DestX, DestY, src_rect);
}


void ColorFillVideoSurfaceArea(SGPVSurface* const dst, INT32 iDestX1, INT32 iDestY1, INT32 iDestX2, INT32 iDestY2, const UINT16 Color16BPP)
{
	SGPRect const Clip = GetClippingRect();

	if (iDestX1 < Clip.iLeft) iDestX1 = Clip.iLeft;
	if (iDestX1 > Clip.iRight) return;

	if (iDestX2 > Clip.iRight) iDestX2 = Clip.iRight;
	if (iDestX2 < Clip.iLeft) return;

	if (iDestY1 < Clip.iTop) iDestY1 = Clip.iTop;
	if (iDestY1 > Clip.iBottom) return;

	if (iDestY2 > Clip.iBottom) iDestY2 = Clip.iBottom;
	if (iDestY2 < Clip.iTop) return;

	if (iDestX2 <= iDestX1 || iDestY2 <= iDestY1) return;

	SDL_Rect Rect;
	Rect.x = iDestX1;
	Rect.y = iDestY1;
	Rect.w = iDestX2 - iDestX1;
	Rect.h = iDestY2 - iDestY1;
	SDL_FillSurfaceRect(dst->surface_.get(), &Rect, Color16BPP);
}


// Will drop down into user-defined blitter if 8->16 BPP blitting is being done
void BltVideoSurface(SGPVSurface* const dst, SGPVSurface* const src, INT32 const iDestX, INT32 const iDestY, SGPBox const* const src_box)
{
	Assert(dst);
	Assert(src);

	SDL_Rect* src_rect = 0;
	SDL_Rect  r;
	if (src_box)
	{
		r.x = src_box->x;
		r.y = src_box->y;
		r.w = src_box->w;
		r.h = src_box->h;
		src_rect = &r;
	}

	SDL_Rect dstrect;
	dstrect.x = iDestX;
	dstrect.y = iDestY;
	SDL_BlitSurface(src->surface_.get(), src_rect, dst->surface_.get(), &dstrect);

	if (TextRegistry::IsEnabled())
	{
		SDL_Rect const from = src_rect ? *src_rect : SDL_Rect{ 0, 0, src->surface_->w, src->surface_->h };
		TextRegistry::OnBlit(dst, src, from, iDestX, iDestY);
	}
}


void BltStretchVideoSurface(SGPVSurface* const dst, SGPVSurface const* const src, SGPBox const* const src_rect, SGPBox const* const dst_rect)
{
	if (dst->BPP() != 16 || src->BPP() != 16) return;

	SDL_Surface const* const ssurface = src->surface_.get();
	SDL_Surface*       const dsurface = dst->surface_.get();

	const UINT32  s_pitch = ssurface->pitch >> 1;
	const UINT32  d_pitch = dsurface->pitch >> 1;
	UINT16 const* os      = (const UINT16*)ssurface->pixels + s_pitch * src_rect->y + src_rect->x;
	UINT16*       d       =       (UINT16*)dsurface->pixels + d_pitch * dst_rect->y + dst_rect->x;

	UINT const width  = dst_rect->w;
	UINT const height = dst_rect->h;
	UINT const dx     = src_rect->w;
	UINT const dy     = src_rect->h;
	UINT py = 0;
	if (ssurface->flags & SDL_SURFACE_PREALLOCATED)
	{
//		const UINT16 key = ssurface->format->colorkey;
		const UINT16 key = 0;
		for (UINT iy = 0; iy < height; ++iy)
		{
			const UINT16* s = os;
			UINT px = 0;
			for (UINT ix = 0; ix < width; ++ix)
			{
				if (*s != key) *d = *s;
				++d;
				px += dx;
				for (; px >= width; px -= width) ++s;
			}
			d += d_pitch - width;
			py += dy;
			for (; py >= height; py -= height) os += s_pitch;
		}
	}
	else
	{
		for (UINT iy = 0; iy < height; ++iy)
		{
			const UINT16* s = os;
			UINT px = 0;
			for (UINT ix = 0; ix < width; ++ix)
			{
				*d++ = *s;
				px += dx;
				for (; px >= width; px -= width) ++s;
			}
			d += d_pitch - width;
			py += dy;
			for (; py >= height; py -= height) os += s_pitch;
		}
	}
}


void BltVideoSurfaceOnce(SGPVSurface* const dst, const char* const filename, INT32 const x, INT32 const y)
{
	std::unique_ptr<SGPVSurface> src(AddVideoSurfaceFromFile(filename));
	BltVideoSurface(dst, src.get(), x, y, NULL);
}

/** Draw image on the video surface stretching the image if necessary. */
void BltVideoSurfaceOnceWithStretch(SGPVSurface* const dst, const char* const filename)
{
	std::unique_ptr<SGPVSurface> src(AddVideoSurfaceFromFile(filename));
	FillVideoSurfaceWithStretch(dst, src.get());
}

/** Fill video surface with another one with stretch. */
void FillVideoSurfaceWithStretch(SGPVSurface* const dst, SGPVSurface* const src)
{
	SGPBox srcRec;
	SGPBox dstRec;
	srcRec.set(0, 0, src->Width(), src->Height());
	dstRec.set(0, 0, dst->Width(), dst->Height());
	BltStretchVideoSurface(dst, src, &srcRec, &dstRec);
}

SGPPaletteEntry const * SGPVSurface::GetPalette() const
{
	auto * palette = SDL_GetSurfacePalette(surface_.get());
	return palette ? palette->colors : nullptr;
}

void DeleteVideoSurface(SGPVSurface const * vs)
{
	delete vs;
}
