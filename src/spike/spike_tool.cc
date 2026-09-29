// ja2-spike: runs the Phase 0 spikes without the game or its data.
//
//   ja2-spike ui KIND WxH OUT.png [STATE]     render the save/load spike (KIND = rml | inhouse, STATE = default |
//                                             hover | scrolled | modal) headless through SDL's software renderer
//   ja2-spike perf [--quick]                  software-path frame times of both toolkits at 1080p and 4K
//   ja2-spike renderpath [--quick] [--no-gpu] SDL_Renderer drivers vs SDL_GPU, offscreen (markdown table)
//   ja2-spike style DIR SCREEN WxH OUT.png [HOVER_ID]
//                                             render a Phase 1 style-direction mock (DIR = a | b | c, SCREEN =
//                                             mainmenu | squadbar | mapscreen) from assets/ui-styles, no game data
//   ja2-spike selftest OUTDIR                 CI check: both toolkits x all states render, respond to input and
//                                             differ from an empty frame; writes the PNGs to OUTDIR
#include "RenderPathSpike.h"
#include "UiSpike.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace spike;

static int Usage()
{
	std::fprintf(stderr, "usage: ja2-spike ui KIND WxH OUT.png [STATE] | style DIR SCREEN WxH OUT.png [HOVER_ID] | perf [--quick] | renderpath [--quick] [--no-gpu] | selftest OUTDIR\n");
	return 2;
}

static bool ParseSize(char const* s, int& w, int& h) { return std::sscanf(s, "%dx%d", &w, &h) == 2 && w > 0 && h > 0; }

static int Ui(int argc, char** argv)
{
	int w, h;
	if (argc < 5 || !ParseSize(argv[3], w, h)) return Usage();
	std::string const state = argc > 5 ? argv[5] : "default";
	if (argc > 6 && std::strncmp(argv[6], "--gpu", 5) == 0)
	{
		// The same screen through a GPU renderer (hidden window, render target, readback)
		char const* driver = argv[6][5] == '=' ? argv[6] + 6 : nullptr;
		if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
		SDL_Window* win = SDL_CreateWindow("ja2-spike", 64, 64, SDL_WINDOW_HIDDEN);
		SDL_Renderer* r = win ? SDL_CreateRenderer(win, driver) : nullptr;
		if (!r) throw std::runtime_error(SDL_GetError());
		SDL_Texture* target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
		SDL_SetRenderTarget(r, target);
		bool ok = false;
		{
			auto screen = CreateScreen(argv[2], r, SaveListModel::Fake());
			screen->setSize(w, h);
			ApplyState(*screen, state);
			screen->update(1.0 / 60);
			screen->render();
			SDL_Surface* s = SDL_RenderReadPixels(r, nullptr);
			ok = s && SavePng(s, argv[4]);
			if (s) SDL_DestroySurface(s);
		}
		std::printf("%s %dx%d %s via %s -> %s\n", argv[2], w, h, state.c_str(), SDL_GetRendererName(r), argv[4]);
		SDL_DestroyTexture(target);
		SDL_DestroyRenderer(r);
		SDL_DestroyWindow(win);
		return ok ? 0 : 1;
	}
	auto res = RenderOffscreen(argv[2], w, h, 0, [&](Screen& s) { ApplyState(s, state); });
	bool const ok = SavePng(res.surface, argv[4]);
	SDL_DestroySurface(res.surface);
	std::printf("%s %dx%d %s: %.1f ms -> %s\n", argv[2], w, h, state.c_str(), res.msFirst, argv[4]);
	return ok ? 0 : 1;
}

static int Style(int argc, char** argv)
{
	int w, h;
	if (argc < 6 || !ParseSize(argv[4], w, h)) return Usage();
	SDL_Surface* surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
	SDL_Renderer* r = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
	if (!r) throw std::runtime_error(SDL_GetError());
	{
		auto screen = CreateStyleDemoScreen(r, argv[2], argv[3]);
		screen->setSize(w, h);
		screen->mouseMove(-1000, -1000);
		screen->update(1.0 / 60);
		SDL_FRect e;
		if (argc > 6 && FindElement(*screen, argv[6], e)) screen->mouseMove(e.x + e.w / 2, e.y + e.h / 2);
		screen->update(1.0 / 60);
		screen->render();
		SDL_FlushRenderer(r);
	}
	SDL_DestroyRenderer(r);
	bool const ok = SavePng(surface, argv[5]);
	SDL_DestroySurface(surface);
	std::printf("style %s %s %dx%d -> %s\n", argv[2], argv[3], w, h, argv[5]);
	return ok ? 0 : 1;
}

static int Perf(bool quick)
{
	std::printf("| Toolkit | Size | First frame (ms) | Steady frame (ms) |\n|---|---|---|---|\n");
	for (auto [w, h] : { std::pair{ 1920, 1080 }, std::pair{ 2560, 1440 }, std::pair{ 3840, 2160 } })
	{
		for (char const* kind : { "rml", "inhouse" })
		{
			auto r = RenderOffscreen(kind, w, h, quick ? 3 : 20, [](Screen& s) { ApplyState(s, "hover"); });
			SDL_DestroySurface(r.surface);
			std::printf("| %s | %dx%d | %.1f | %.2f |\n", kind, w, h, r.msFirst, r.msSteady);
		}
	}
	return 0;
}

/** Fraction of pixels that differ from the background colour. */
static double Coverage(SDL_Surface* s)
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

static int SelfTest(std::string const& out)
{
	int failures = 0;
	for (char const* kind : { "rml", "inhouse" })
	{
		for (char const* state : { "default", "hover", "scrolled", "modal" })
		{
			std::string model;
			auto r = RenderOffscreen(kind, 1280, 720, 1, [&](Screen& s) {
				ApplyState(s, state);
				SaveListModel const& m = s.model();
				model = "selected=" + std::to_string(m.selected) + " hovered=" + std::to_string(m.hovered) + " modal=" + std::to_string(m.modal);
				bool const ok =
					std::strcmp(state, "hover") == 0 ? m.selected == 2 && m.hovered == 4 :
					std::strcmp(state, "modal") == 0 ? m.selected == 1 && m.modal :
					!m.modal;
				if (!ok) { std::printf("FAIL %s %s: model %s\n", kind, state, model.c_str()); ++failures; }
			});
			double const cov = Coverage(r.surface);
			std::string const path = out + "/uispike_" + kind + "_" + state + ".png";
			SavePng(r.surface, path);
			SDL_DestroySurface(r.surface);
			bool const ok = cov > 0.15;
			std::printf("%s %s %s: coverage %.2f, %s, %.1f ms\n", ok ? "ok  " : "FAIL", kind, state, cov, model.c_str(), r.msFirst);
			if (!ok) ++failures;
		}
	}
	return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
	if (argc < 2) return Usage();
	if (char const* dir = SDL_getenv("JA2_SPIKE_ASSETS")) SetAssetOverrideDir(dir); // UI "mods": RML/RCSS/fonts on disk win
	std::string const cmd = argv[1];
	bool quick = false, gpu = true;
	for (int i = 2; i < argc; ++i)
	{
		if (!std::strcmp(argv[i], "--quick")) quick = true;
		if (!std::strcmp(argv[i], "--no-gpu")) gpu = false;
	}
	if (gpu && cmd == "renderpath" && !SDL_Init(SDL_INIT_VIDEO))
	{
		std::fprintf(stderr, "no video: %s (GPU rows skipped)\n", SDL_GetError());
		gpu = false;
	}
	try
	{
		if (cmd == "ui") return Ui(argc, argv);
		if (cmd == "style") return Style(argc, argv);
		if (cmd == "perf") return Perf(quick);
		if (cmd == "selftest") return argc > 2 ? SelfTest(argv[2]) : Usage();
		if (cmd == "renderpath")
		{
			std::fputs(RenderPathTable(RunRenderPathSpike(quick, gpu)).c_str(), stdout);
			return 0;
		}
	}
	catch (std::exception const& e)
	{
		std::fprintf(stderr, "error: %s\n", e.what());
		return 1;
	}
	return Usage();
}
