// GCC 16 reports a false -Wmaybe-uninitialized inside libstdc++ when it
// inlines an ST::string or std::function construct (it cannot see that the
// source is initialised). string_theory/libstdc++ trip it in a few TUs; it
// predates the warning flags added in #290.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "TextRegistry.h"
#include "VSurface.h"

#include <algorithm>
#include <deque>
#include <tuple>

namespace TextRegistry
{

namespace
{
	struct Item
	{
		SGPVSurface const*    surface;
		SDL_Rect              rect;
		ST::string            text;
		std::optional<UINT16> color;
		std::vector<UINT16>   pixels; // rect.w * rect.h snapshot taken right after printing
	};

	bool             g_enabled = false;
	std::deque<Item> g_items;

	constexpr size_t MAX_ITEMS = 6000;

	SDL_Rect Clip(SDL_Rect r, SDL_Surface const& s)
	{
		SDL_Rect const bounds{ 0, 0, s.w, s.h };
		SDL_Rect out;
		if (!SDL_GetRectIntersection(&r, &bounds, &out)) return SDL_Rect{ 0, 0, 0, 0 };
		return out;
	}

	UINT16 PixelAt(SDL_Surface const& s, int x, int y)
	{
		auto const* row = static_cast<UINT8 const*>(s.pixels) + y * s.pitch;
		return reinterpret_cast<UINT16 const*>(row)[x];
	}

	bool Is16Bpp(SDL_Surface const& s)
	{
		return SDL_BYTESPERPIXEL(s.format) == 2;
	}

	std::vector<UINT16> Snapshot(SDL_Surface const& s, SDL_Rect const& r)
	{
		std::vector<UINT16> px;
		px.reserve(size_t(r.w) * r.h);
		for (int y = r.y; y < r.y + r.h; ++y)
			for (int x = r.x; x < r.x + r.w; ++x)
				px.push_back(PixelAt(s, x, y));
		return px;
	}

	int Area(SDL_Rect const& r) { return r.w * r.h; }

	int OverlapArea(SDL_Rect const& a, SDL_Rect const& b)
	{
		SDL_Rect i;
		return SDL_GetRectIntersection(&a, &b, &i) ? Area(i) : 0;
	}

	void Add(Item item)
	{
		// Anything mostly covered by the new string has been printed over.
		std::erase_if(g_items, [&](Item const& old) {
			return old.surface == item.surface &&
				OverlapArea(old.rect, item.rect) * 2 >= Area(old.rect);
		});
		g_items.push_back(std::move(item));
		if (g_items.size() > MAX_ITEMS)
		{
			g_items.erase(g_items.begin(), g_items.begin() + MAX_ITEMS / 6);
		}
	}

	/* Does @a frame still show @a item? Pixels under @a exclude (the mouse
	 * cursor) are ignored. */
	bool StillShown(Item const& item, SDL_Surface const& frame, SDL_Rect const& exclude)
	{
		if (item.rect.x < 0 || item.rect.y < 0 || item.rect.x + item.rect.w > frame.w || item.rect.y + item.rect.h > frame.h) return false;
		unsigned total = 0, same = 0, ink = 0, inkSame = 0;
		size_t i = 0;
		for (int y = item.rect.y; y < item.rect.y + item.rect.h; ++y)
		{
			for (int x = item.rect.x; x < item.rect.x + item.rect.w; ++x, ++i)
			{
				if (x >= exclude.x && x < exclude.x + exclude.w &&
				    y >= exclude.y && y < exclude.y + exclude.h) continue;
				UINT16 const want = item.pixels[i];
				bool const eq = PixelAt(frame, x, y) == want;
				++total;
				same += eq;
				if (want == item.color)
				{
					++ink;
					inkSame += eq;
				}
			}
		}
		if (total == 0) return false;
		if (ink >= 3) return inkSame * 5 >= ink * 4 && same * 2 >= total;
		// Printed in a known colour but no ink landed (clipped away, or
		// printed invisibly): the snapshot only shows what was underneath.
		if (item.color) return false;
		return same * 20 >= total * 17;
	}
}

void SetEnabled(bool const enabled)
{
	g_enabled = enabled;
	if (!enabled) g_items.clear();
}

bool IsEnabled() { return g_enabled; }

void OnPrint(SGPVSurface* const dst, SDL_Rect const rect, ST::string const& text, std::optional<UINT16> const color)
{
	if (!g_enabled || !dst || text.empty()) return;
	SDL_Surface const& s = dst->GetSDLSurface();
	if (!Is16Bpp(s)) return;
	SDL_Rect const r = Clip(rect, s);
	if (r.w <= 0 || r.h <= 0) return;
	Add(Item{ dst, r, text, color, Snapshot(s, r) });
}

void OnBlit(SGPVSurface* const dst, SGPVSurface const* const src, SDL_Rect const srcRect, int const dx, int const dy)
{
	if (!g_enabled || !dst || !src || dst == src) return;
	SDL_Surface const& d = dst->GetSDLSurface();
	if (!Is16Bpp(d)) return;

	SDL_Rect const dstRect{ dx, dy, srcRect.w, srcRect.h };
	std::erase_if(g_items, [&](Item const& old) {
		return old.surface == dst && OverlapArea(old.rect, dstRect) == Area(old.rect);
	});

	std::vector<Item> copies;
	for (Item const& it : g_items)
	{
		if (it.surface != src) continue;
		if (OverlapArea(it.rect, srcRect) != Area(it.rect)) continue;
		SDL_Rect const moved{ it.rect.x - srcRect.x + dx, it.rect.y - srcRect.y + dy, it.rect.w, it.rect.h };
		SDL_Rect const r = Clip(moved, d);
		if (r.w != moved.w || r.h != moved.h) continue;
		copies.push_back(Item{ dst, r, it.text, it.color, Snapshot(d, r) });
	}
	for (Item& c : copies) Add(std::move(c));
}

void OnStretch(SGPVSurface* const dst, SGPVSurface const* const src, SDL_Rect const srcRect, SDL_Rect const dstRect)
{
	if (!g_enabled || !dst || !src || dst == src || srcRect.w <= 0 || srcRect.h <= 0) return;
	SDL_Surface const& d = dst->GetSDLSurface();
	if (!Is16Bpp(d)) return;

	std::erase_if(g_items, [&](Item const& old) {
		return old.surface == dst && OverlapArea(old.rect, dstRect) == Area(old.rect);
	});

	// Source pixel s lands on destination pixels [ceil(s * D / S), ceil((s + 1) * D / S)).
	auto map = [](int s, int S, int D) { return (s * D + S - 1) / S; };
	std::vector<Item> copies;
	for (Item const& it : g_items)
	{
		if (it.surface != src) continue;
		if (OverlapArea(it.rect, srcRect) != Area(it.rect)) continue;
		int const x0 = dstRect.x + map(it.rect.x - srcRect.x, srcRect.w, dstRect.w);
		int const y0 = dstRect.y + map(it.rect.y - srcRect.y, srcRect.h, dstRect.h);
		int const x1 = dstRect.x + map(it.rect.x + it.rect.w - srcRect.x, srcRect.w, dstRect.w);
		int const y1 = dstRect.y + map(it.rect.y + it.rect.h - srcRect.y, srcRect.h, dstRect.h);
		SDL_Rect const moved{ x0, y0, x1 - x0, y1 - y0 };
		SDL_Rect const r = Clip(moved, d);
		if (r.w != moved.w || r.h != moved.h || r.w <= 0 || r.h <= 0) continue;
		copies.push_back(Item{ dst, r, it.text, it.color, Snapshot(d, r) });
	}
	for (Item& c : copies) Add(std::move(c));
}

void OnSurfaceDeleted(SGPVSurface const* const s)
{
	if (g_items.empty()) return;
	std::erase_if(g_items, [&](Item const& it) { return it.surface == s; });
}

void Clear() { g_items.clear(); }

std::vector<VisibleText> Visible(SDL_Surface const* const frame, SDL_Rect const exclude)
{
	std::vector<VisibleText> out;
	if (!frame || !Is16Bpp(*frame)) return out;

	for (Item const& it : g_items)
	{
		SDL_Surface const& s = it.surface->GetSDLSurface();
		// Only full-screen surfaces map 1:1 onto the frame; smaller ones reach
		// the screen through blits, which carry their strings along.
		if (s.w != frame->w || s.h != frame->h) continue;
		if (!StillShown(it, *frame, exclude)) continue;
		out.push_back(VisibleText{ it.text, it.rect, it.color });
	}

	// The same string often lives in several buffers (frame + save buffer).
	std::sort(out.begin(), out.end(), [](VisibleText const& a, VisibleText const& b) {
		return std::tie(a.rect.y, a.rect.x, a.rect.w, a.rect.h, a.text) <
		       std::tie(b.rect.y, b.rect.x, b.rect.w, b.rect.h, b.text);
	});
	out.erase(std::unique(out.begin(), out.end(), [](VisibleText const& a, VisibleText const& b) {
		return a.text == b.text && a.rect.x == b.rect.x && a.rect.y == b.rect.y &&
		       a.rect.w == b.rect.w && a.rect.h == b.rect.h;
	}), out.end());

	// Leader dots ("Health . . . . 82") are printed one at a time; drop them.
	std::erase_if(out, [](VisibleText const& t) {
		return t.text.to_std_string().find_first_not_of(". ") == std::string::npos;
	});

	// Text fields print one character at a time; glue those back together.
	std::vector<VisibleText> merged;
	for (VisibleText& t : out)
	{
		if (!merged.empty())
		{
			VisibleText& prev = merged.back();
			bool const sameLine = prev.rect.y == t.rect.y && prev.rect.h == t.rect.h && prev.color == t.color;
			int  const gap      = t.rect.x - (prev.rect.x + prev.rect.w);
			if (sameLine && gap >= 0 && gap <= 1 && (t.text.size() == 1 || prev.text.size() == 1))
			{
				prev.text += t.text;
				prev.rect.w = t.rect.x + t.rect.w - prev.rect.x;
				continue;
			}
		}
		merged.push_back(std::move(t));
	}
	return merged;
}

}
