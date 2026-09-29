// Phase 0 spikes, checked in CI (they need no game data): both UI toolkits render headless and respond to
// input by element id, and the world spike's depth ops follow the legacy blitters' rules.
#include "UiSpike.h"
#include "WorldSpikeRaster.h"

#include "gtest/gtest.h"

namespace {

double Coverage(SDL_Surface* s)
{
	Uint32 const bg = static_cast<Uint32*>(s->pixels)[0];
	long long n = 0;
	for (int y = 0; y < s->h; ++y)
	{
		auto const* row = reinterpret_cast<Uint32 const*>(static_cast<Uint8 const*>(s->pixels) + y * s->pitch);
		for (int x = 0; x < s->w; ++x) n += row[x] != bg;
	}
	return double(n) / (double(s->w) * s->h);
}

void CheckToolkit(char const* kind)
{
	for (char const* state : { "default", "hover", "scrolled", "modal" })
	{
		spike::SaveListModel seen;
		auto r = spike::RenderOffscreen(kind, 960, 540, 1, [&](spike::Screen& s) {
			spike::ApplyState(s, state);
			seen = s.model();
			SDL_FRect list;
			EXPECT_TRUE(spike::FindElement(s, "list", list)) << kind;
			EXPECT_GT(list.h, 200.f) << kind;
		});
		ASSERT_NE(r.surface, nullptr);
		EXPECT_GT(Coverage(r.surface), 0.15) << kind << " " << state;
		SDL_DestroySurface(r.surface);
		if (std::string(state) == "hover")
		{
			EXPECT_EQ(seen.selected, 2) << kind;
			EXPECT_EQ(seen.hovered, 4) << kind;
		}
		if (std::string(state) == "modal")
		{
			EXPECT_TRUE(seen.modal) << kind;
			EXPECT_EQ(seen.selected, 1) << kind;
		}
	}
}

}

TEST(UiSpike, RmlUiScreen)    { CheckToolkit("rml"); }
TEST(UiSpike, InhouseScreen)  { CheckToolkit("inhouse"); }

TEST(UiSpike, DeleteThroughModal)
{
	for (char const* kind : { "rml", "inhouse" })
	{
		auto r = spike::RenderOffscreen(kind, 1280, 720, 0, [&](spike::Screen& s) {
			size_t const n = s.model().slots.size();
			spike::ApplyState(s, "modal");
			EXPECT_TRUE(spike::ClickElement(s, "confirm")) << kind;
			EXPECT_EQ(s.model().slots.size(), n - 1) << kind;
			EXPECT_FALSE(s.model().modal) << kind;
		});
		SDL_DestroySurface(r.surface);
	}
}

TEST(UiSpike, StyleDirectionsLoadCleanly)
{
	// Phase 1 style mocks: every direction x screen loads from ui-styles/ (next to the binary) without RmlUi
	// warnings, lays out its key elements and draws. No game data: portraits use the procedural placeholder.
	std::string const dir = spike::StyleDir();
	if (!SDL_GetPathInfo((dir + "/base.rcss").c_str(), nullptr)) GTEST_SKIP() << "no " << dir;
	for (char const* d : spike::StyleDirections)
	{
		for (char const* screen : spike::StyleScreens)
		{
			SDL_Surface* surface = SDL_CreateSurface(960, 540, SDL_PIXELFORMAT_ARGB8888);
			SDL_Renderer* r = SDL_CreateSoftwareRenderer(surface);
			ASSERT_NE(r, nullptr);
			spike::ResetRmlWarnings();
			{
				auto s = spike::CreateStyleDemoScreen(r, d, screen);
				s->setSize(960, 540);
				s->update(1.0 / 60);
				s->render();
				SDL_FlushRenderer(r);
				int ids = 0;
				for (auto const& e : s->elements())
				{
					++ids;
					EXPECT_GE(e.rect.x, -1.f) << d << " " << screen << " #" << e.id;
					EXPECT_LE(e.rect.x + e.rect.w, 961.f) << d << " " << screen << " #" << e.id;
					EXPECT_LE(e.rect.y + e.rect.h, 541.f) << d << " " << screen << " #" << e.id;
				}
				EXPECT_GT(ids, 5) << d << " " << screen;
			}
			EXPECT_EQ(spike::RmlWarnings(), 0) << d << " " << screen;
			EXPECT_GT(Coverage(surface), 0.5) << d << " " << screen;
			SDL_DestroyRenderer(r);
			SDL_DestroySurface(surface);
		}
	}
}

TEST(UiSpike, ProceduralTextures)
{
	for (char const* name : { "grain", "grain-light", "scan", "hatch", "grid", "topo", "topo-dark", "vignette", "rays", "dots", "silhouette" })
	{
		int w = 0, h = 0;
		bool repeat = false;
		auto const px = spike::GenerateProcedural(name, w, h, repeat);
		EXPECT_GT(w, 0) << name;
		EXPECT_EQ(px.size(), size_t(w) * h * 4) << name;
	}
	int w = 0, h = 0;
	bool repeat = false;
	EXPECT_TRUE(spike::GenerateProcedural("nope", w, h, repeat).empty());
}

using namespace spike::world;

TEST(WorldSpike, EtrleDecode)
{
	// row 0: 2 transparent, 2 literal (5, 6), end; row 1: 1 literal (7), end
	uint8_t const data[] = { 0x82, 2, 5, 6, 0, 1, 7, 0 };
	Sprite const s = DecodeEtrle(data, sizeof data, 4, 2, 0, 0);
	EXPECT_EQ(s.mask[0], 0);
	EXPECT_EQ(s.mask[2], 1);
	EXPECT_EQ(s.index[3], 6);
	EXPECT_EQ(s.index[4], 7);
	EXPECT_EQ(s.mask[5], 0);
}

TEST(WorldSpike, DepthRulesMatchLegacyBlitters)
{
	uint16_t palette[256];
	for (int i = 0; i < 256; ++i) palette[i] = uint16_t(i);
	static uint16_t shade[65536];
	for (int i = 0; i < 65536; ++i) shade[i] = uint16_t(i / 2);

	Sprite s;
	s.w = s.h = 2;
	s.index = { 10, 10, 10, 10 };
	s.mask = { 1, 1, 1, 1 };
	Sprite t = s;
	t.index = { 20, 20, 20, 20 };

	Target tg;
	tg.reset(2, 2, 0);
	tg.shade = shade;
	Instance a{ &s, palette, 0, 0, 5, Op::DepthGEqual, {} };
	Instance b{ &t, palette, 0, 0, 5, Op::DepthGreater, {} };
	Draw(tg, a);
	Draw(tg, b); // equal depth: BltTransZInc (<) does not overwrite
	EXPECT_EQ(tg.color[0], 10);
	b.op = Op::DepthGEqual;
	Draw(tg, b); // BltTransZ (<=) does
	EXPECT_EQ(tg.color[0], 20);
	Instance sh{ &s, palette, 0, 0, 6, Op::ShadowGreater, {} };
	Draw(tg, sh);
	EXPECT_EQ(tg.color[0], 10); // darkened destination
	Draw(tg, sh);               // depth now 6: no double shadow
	EXPECT_EQ(tg.color[0], 10);
	Instance col{ &t, palette, 0, 0, 0, Op::DepthGreater, { 5, 9 } }; // per-column depth
	Draw(tg, col);
	EXPECT_EQ(tg.color[0], 10);
	EXPECT_EQ(tg.color[1], 20);
}
