// Phase 0 spikes, checked in CI (they need no game data): both UI toolkits render headless and respond to
// input by element id, and the world spike's depth ops follow the legacy blitters' rules.
#include "UiSpike.h"
#include "WorldSpikeRaster.h"

#include "gtest/gtest.h"

#include <fstream>
#include <iterator>

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
	// Phase 1 style mocks: every direction x screen loads from ui/mocks (next to the binary) without RmlUi
	// warnings, lays out its key elements and draws. No game data: portraits use the procedural placeholder.
	std::string const dir = spike::StyleDir();
	if (!SDL_GetPathInfo((dir + "/tokens.rcss").c_str(), nullptr)) GTEST_SKIP() << "no " << dir;
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

TEST(UiSpike, GalleryLoadsCleanlyAtEveryResolutionAndScale)
{
	// Phase 1 design system: every gallery page at every reference resolution and UI scale loads without RmlUi
	// warnings (unknown properties, tokens, icons), and the layout audit finds nothing off screen, clipped or
	// overflowing; at 100% no page needs scrolling.
	if (!SDL_GetPathInfo((spike::StyleDir() + "/gallery.rml").c_str(), nullptr)) GTEST_SKIP() << "no " << spike::StyleDir();
	struct Res { int w, h; };
	for (Res const res : { Res{ 1280, 720 }, Res{ 1920, 1080 }, Res{ 2560, 1440 }, Res{ 3440, 1440 }, Res{ 3840, 2160 } })
	{
		for (float const scale : { 1.f, 1.25f, 1.5f, 2.f })
		{
			if ((res.w == 3440 || res.w == 3840) && scale != 1.f && scale != 2.f) continue; // layout (dp) is identical to 2560x1440
			for (char const* page : spike::GalleryPages)
			{
				SDL_Surface* surface = SDL_CreateSurface(res.w, res.h, SDL_PIXELFORMAT_ARGB8888);
				SDL_Renderer* r = SDL_CreateSoftwareRenderer(surface);
				ASSERT_NE(r, nullptr);
				spike::ResetRmlWarnings();
				{
					auto s = spike::CreateGalleryScreen(r, page);
					s->setSize(res.w, res.h, scale);
					s->mouseMove(-1000, -1000);
					s->update(1.0 / 60);
					for (auto const& p : s->layoutProblems()) ADD_FAILURE() << page << " " << res.w << "x" << res.h << " @" << scale << ": " << p;
					if (res.w == 1920 && scale == 1.f)
					{
						s->render(); // draw only once per page: the software path is slow at 4K
						SDL_FlushRenderer(r);
					}
				}
				EXPECT_EQ(spike::RmlWarnings(), 0) << page << " " << res.w << "x" << res.h << " @" << scale;
				SDL_DestroyRenderer(r);
				SDL_DestroySurface(surface);
			}
		}
	}
}

TEST(UiSpike, GalleryInteractions)
{
	if (!SDL_GetPathInfo((spike::StyleDir() + "/gallery.rml").c_str(), nullptr)) GTEST_SKIP();
	SDL_Surface* surface = SDL_CreateSurface(1920, 1080, SDL_PIXELFORMAT_ARGB8888);
	SDL_Renderer* r = SDL_CreateSoftwareRenderer(surface);
	{
		// drag an item from Ivan's inventory onto an empty ground slot
		auto s = spike::CreateGalleryScreen(r, "game");
		s->setSize(1920, 1080);
		SDL_FRect from, to;
		ASSERT_TRUE(spike::FindElement(*s, "inv0", from));
		ASSERT_TRUE(spike::FindElement(*s, "ground1", to));
		s->mouseMove(from.x + from.w / 2, from.y + from.h / 2);
		s->update(1.0 / 60);
		s->mouseButton(from.x + from.w / 2, from.y + from.h / 2, true);
		for (int i = 1; i <= 8; ++i)
		{
			s->mouseMove(from.x + (to.x - from.x) * i / 8 + from.w / 2, from.y + (to.y - from.y) * i / 8 + from.h / 2);
			s->update(1.0 / 60);
		}
		s->mouseButton(to.x + to.w / 2, to.y + to.h / 2, false);
		s->update(1.0 / 60);
		EXPECT_NE(s->model().status.find("Moved gun to ground slot 2"), std::string::npos) << s->model().status;

		// page tabs switch pages
		EXPECT_TRUE(spike::ClickElement(*s, "tab-icons"));
		SDL_FRect icons;
		EXPECT_TRUE(spike::FindElement(*s, "page-icons", icons));
	}
	SDL_DestroyRenderer(r);
	SDL_DestroySurface(surface);
}

TEST(UiSpike, IconsRasterize)
{
	// every icon file parses and draws something, inside its box, without touching the edge pixels
	auto const names = spike::IconNames();
	if (names.empty()) GTEST_SKIP() << "no icons in " << spike::StyleDir();
	EXPECT_GE(names.size(), 100u);
	for (auto const& n : names)
	{
		std::ifstream f(spike::StyleDir() + "/icons/" + n + ".svg", std::ios::binary);
		std::string const svg{ std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
		std::string error;
		auto const px = spike::RasterizeSvg(svg, 48, &error);
		ASSERT_EQ(px.size(), 48u * 48 * 4) << n << ": " << error;
		long covered = 0;
		for (size_t i = 3; i < px.size(); i += 4) covered += px[i] > 127;
		EXPECT_GT(covered, 40) << n;
	}
	// the parser handles compact arc flags and relative commands
	std::string const svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M6 12a6 6 0 0112 0z"/></svg>)";
	EXPECT_FALSE(spike::RasterizeSvg(svg, 24).empty());
}

TEST(UiSpike, TokensExpand)
{
	if (!SDL_GetPathInfo((spike::StyleDir() + "/tokens.rcss").c_str(), nullptr)) GTEST_SKIP();
	spike::ResetRmlWarnings();
	std::string const css = spike::ExpandTokens(".x { color: var(--c-accent); border: var(--border) var(--e2-line); }");
	EXPECT_EQ(css, ".x { color: #D8B25E; border: 1dp #7A6136; }");
	EXPECT_EQ(spike::RmlWarnings(), 0);
	spike::ExpandTokens("var(--no-such-token)");
	EXPECT_EQ(spike::RmlWarnings(), 1);
}

TEST(UiSpike, ProceduralTextures)
{
	for (char const* name : { "grain", "grain-light", "scan", "hatch", "grid", "topo", "topo-dark", "vignette", "rays", "dots", "silhouette",
		"mottle", "leather", "brushed", "wood", "groove", "frame", "frame-plain", "well", "brackets", "hazard", "granite", "screw", "doll" })
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
