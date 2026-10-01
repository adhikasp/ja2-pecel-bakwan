// Support for the Phase 5 tactical HUD mocks (M2, docs/ui/tactical.md): game pictures at an integer scale, and the
// tactical world shown behind a mock without the legacy HUD.
//
// Pictures. "nitem-<item>", "nitembig-<item>" and "sface-<profile face>" are an item's small and big inventory
// picture and a merc's small face, from the player's game data. A mock writes them without a size:
//   <img src="nitem-25" data-x="2"/>
// and PrepareMockDocument gives each one an integer scale k = floor(data-x * dp) (at least 1; data-x defaults to 2),
// asks for "<name>@<k>" (2x and 4x are uplifted with Scale2x, 3x is nearest neighbour) and sizes the element to
// exactly k times the picture in output pixels. A picture is never stretched and keeps its aspect ratio.
//
// Anchors. An element with data-anchor="<soldier name>" (or "enemy<n>", the n-th enemy in the sector, from 0) is
// placed over that soldier's head, as the native HUD will place names and bars (WorldToUi): its centre at the
// soldier's centre plus data-dx dp, its top at the head plus data-dy dp (data-align="left" puts its left edge there).
//
// World. A mock whose <body> has data-world="clear", opened on the tactical screen while the world is a layer of its
// own (ja2.setVideo{worldzoom=...}), clears the legacy UI layer so that the whole world shows behind the mock.
#include "NativeImages.h"
#include "NativeUIRuntime.h"

#include "ContentManager.h"
#include "Directories.h"
#include "GameInstance.h"
#include "GraphicModel.h"
#include "ItemModel.h"
#include "ItemSystem.h"
#include "Interface.h"
#include "RenderWorld.h"
#include "ScreenIDs.h"
#include "Overhead.h"
#include "Tactical_Placement_GUI.h"
#include "Soldier_Control.h"
#include "UILayout.h"
#include "Video.h"
#include "VSurface.h"

#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

extern ScreenID guiCurrentScreen;

namespace NativeUI
{

namespace
{
	/** "overhead-snap": the legacy overhead map (640 x 320 UI pixels) as it was drawn when the mock opened. */
	SDL_Surface* g_overhead = nullptr;

	void SnapOverhead()
	{
		if (g_overhead) { SDL_DestroySurface(g_overhead); g_overhead = nullptr; }
		int const x0 = STD_SCREEN_X;
		int const y0 = gfTacticalPlacementGUIActive ? SCREEN_HEIGHT - 480 : STD_SCREEN_Y;
		SDL_Surface* const s = SDL_CreateSurface(640, 320, SDL_PIXELFORMAT_RGBA32);
		SGPVSurface::Lock l(FRAME_BUFFER);
		UINT16 const* const src = l.Buffer<UINT16>();
		UINT32 const pitch = l.Pitch() / 2;
		for (int y = 0; y < 320; ++y)
		{
			auto* d = static_cast<Uint8*>(s->pixels) + y * s->pitch;
			for (int x = 0; x < 640; ++x, d += 4)
			{
				UINT16 const p = src[(y0 + y) * pitch + x0 + x];
				Uint8 const r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
				d[0] = Uint8(r << 3 | r >> 2); d[1] = Uint8(g << 2 | g >> 4); d[2] = Uint8(b << 3 | b >> 2); d[3] = 255;
			}
		}
		g_overhead = s;
	}

	/** The picture of name (without "@k") at 1x, or nullptr. */
	SDL_Surface* LoadBase(std::string const& name)
	{
		auto item = [](char const* digits, bool big) -> SDL_Surface* {
			ItemModel const* const it = GCM->getItem(uint16_t(std::atoi(digits)), ItemSystem::nothrow);
			if (!it) return nullptr;
			GraphicModel const& g = big ? it->getInventoryGraphicBig() : it->getInventoryGraphicSmall();
			SDL_Surface* const pic = LoadStiFrame(g.getPath().to_lower().to_std_string(), g.getSubImageIndex());
			if (!pic) return nullptr;
			// the item art keys its drop shadow in pure green: draw it as a soft dark shadow
			for (int y = 0; y < pic->h; ++y)
			{
				auto* p = static_cast<Uint8*>(pic->pixels) + y * pic->pitch;
				for (int x = 0; x < pic->w; ++x, p += 4)
				{
					if (p[3] && p[1] >= 200 && p[0] <= 64 && p[2] <= 64) { p[0] = p[1] = p[2] = 0; p[3] = 70; }
				}
			}
			return pic;
		};
		if (name.rfind("nitembig-", 0) == 0) return item(name.c_str() + 9, true);
		if (name.rfind("nitem-", 0) == 0)    return item(name.c_str() + 6, false);
		if (name.rfind("overhead-snap", 0) == 0)
		{
			return g_overhead ? SDL_DuplicateSurface(g_overhead) : nullptr;
		}
		if (name.rfind("sface-", 0) == 0)
		{
			return LoadStiFrame(ST::format(FACESDIR "/{02d}.sti", std::atoi(name.c_str() + 6)).to_std_string(), 0);
		}
		return nullptr;
	}

	SDL_Surface* Scaled(SDL_Surface* base, int const k)
	{
		if (!base || k <= 1) return base;
		if (k == 2) return UpliftArt(base, 1);
		if (k == 4) return UpliftArt(base, 2);
		SDL_Surface* const s = CropScaled(base, 0, 0, base->w, base->h, k);
		SDL_DestroySurface(base);
		return s;
	}

	/** Width and height of the 1x picture (cached; 0 x 0 if there is none). */
	std::pair<int, int> BaseSize(std::string const& name)
	{
		static std::map<std::string, std::pair<int, int>> sizes;
		auto const i = sizes.find(name);
		if (i != sizes.end()) return i->second;
		SDL_Surface* const s = LoadBase(name);
		std::pair<int, int> const wh = s ? std::make_pair(s->w, s->h) : std::make_pair(0, 0);
		if (s) SDL_DestroySurface(s);
		return sizes[name] = wh;
	}

	SDL_Surface* Provide(std::string const& full)
	{
		std::string name = full;
		int k = 1;
		if (auto const at = full.rfind('@'); at != std::string::npos)
		{
			name = full.substr(0, at);
			k = std::max(1, std::atoi(full.c_str() + at + 1));
		}
		return Scaled(LoadBase(name), k);
	}
}

void RegisterTacticalMockImages()
{
	RegisterImageSource("nitem-", Provide);
	RegisterImageSource("nitembig-", Provide);
	RegisterImageSource("sface-", Provide);
	RegisterImageSource("overhead-snap", Provide);
}

void PrepareMockDocument(Rml::ElementDocument* const doc)
{
	if (!doc) return;
	float const dp = std::max(0.01f, DpScale());
	Rml::ElementList imgs;
	doc->GetElementsByTagName(imgs, "img");
	for (Rml::Element* const e : imgs)
	{
		if (e->GetAttribute<Rml::String>("src", "") == "overhead-snap") { SnapOverhead(); break; }
	}
	imgs.clear();
	doc->GetElementsByTagName(imgs, "img");
	for (Rml::Element* const e : imgs)
	{
		std::string const src = e->GetAttribute<Rml::String>("src", "");
		if (src.find('@') != std::string::npos) continue;
		if (src.rfind("nitem-", 0) != 0 && src.rfind("nitembig-", 0) != 0 && src.rfind("sface-", 0) != 0 && src != "overhead-snap") continue;
		auto const [w, h] = src == "overhead-snap" ? std::make_pair(640, 320) : BaseSize(src);
		if (!w) continue;
		float const want = e->GetAttribute<float>("data-x", 2.0f);
		int const k = std::max(1, int(std::floor(want * dp + 0.001f)));
		// a new snapshot is a new texture name (the runtime caches textures by name)
		static int snaps = 0;
		std::string const base = src == "overhead-snap" ? src + "~" + std::to_string(++snaps) : src;
		e->SetAttribute("src", base + "@" + std::to_string(k));
		e->SetProperty(Rml::PropertyId::Width, Rml::Property(float(w * k), Rml::Unit::PX));
		e->SetProperty(Rml::PropertyId::Height, Rml::Property(float(h * k), Rml::Unit::PX));
	}

	if (guiCurrentScreen == GAME_SCREEN)
	{
		doc->UpdateDocument();
		Rml::ElementList anchored;
		doc->QuerySelectorAll(anchored, "[data-anchor]");
		for (Rml::Element* const e : anchored)
		{
			std::string const who = e->GetAttribute<Rml::String>("data-anchor", "");
			SOLDIERTYPE const* found = nullptr;
			if (who.rfind("enemy", 0) == 0)
			{
				int n = std::atoi(who.c_str() + 5);
				CFOR_EACH_IN_TEAM(s, ENEMY_TEAM)
				{
					if (s->bLife <= 0 || !s->bInSector) continue;
					if (n-- == 0) { found = s; break; }
				}
			}
			else
			{
				FOR_EACH_SOLDIER(s)
				{
					if (s->bInSector && s->name.to_std_string() == who) { found = s; break; }
				}
			}
			if (!found) { e->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None)); continue; }
			INT16 x, y;
			GetSoldierAboveGuyPositions(found, &x, &y, FALSE);
			float const su = float(g_ui.m_uiScale);
			float const cx = (x + 40) * su + e->GetAttribute<float>("data-dx", 0.0f) * dp;
			float const top = y * su + e->GetAttribute<float>("data-dy", 0.0f) * dp;
			bool const left = e->GetAttribute<Rml::String>("data-align", "") == "left";
			float const w = e->GetOffsetWidth();
			e->SetProperty(Rml::PropertyId::Left, Rml::Property(std::round(left ? cx : cx - w / 2), Rml::Unit::PX));
			e->SetProperty(Rml::PropertyId::Top, Rml::Property(std::round(top), Rml::Unit::PX));
		}
	}

	if (doc->GetAttribute<Rml::String>("data-world", "") == "clear" && guiCurrentScreen == GAME_SCREEN && VideoIsLayered())
	{
		FRAME_BUFFER->Fill(UI_LAYER_TRANSPARENT);
		InvalidateScreen();
	}
}

void RestoreAfterMock()
{
	if (guiCurrentScreen != GAME_SCREEN) return;
	fInterfacePanelDirty = DIRTYLEVEL2;
	SetRenderFlags(RENDER_FLAG_FULL);
}

}
