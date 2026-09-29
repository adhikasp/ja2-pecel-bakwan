#include "UiSpikeScreen.h"

#include "GameLoop.h"
#include "Cursors.h"
#include "Input.h"
#include "MouseSystem.h"
#include "JAScreens.h"
#include "UILayout.h"
#include "Video.h"
#include "VSurface.h"

#include <chrono>
#include <memory>
#include <stdexcept>

#ifdef WITH_NATIVE_SPIKES
#include "UiSpike.h"

namespace {
struct Active
{
	std::string                    kind;
	SDL_Surface*                   surface  = nullptr;
	SDL_Renderer*                  renderer = nullptr;
	std::unique_ptr<spike::Screen> screen;
	ScreenID                       returnTo = MAINMENU_SCREEN;
	double                         lastMs = 0, totalMs = 0;
	int                            frames = 0;
	MOUSE_REGION                   region; // the game loop hands mouse buttons to regions, not to screens

	~Active()
	{
		MSYS_RemoveRegion(&region);
		screen.reset(); // before the renderer its textures belong to
		if (renderer) SDL_DestroyRenderer(renderer);
		if (surface) SDL_DestroySurface(surface);
	}
};
std::unique_ptr<Active> g_spike;
std::string             g_pendingKind;
ScreenID                g_pendingReturn = MAINMENU_SCREEN;

void Create()
{
	auto a = std::make_unique<Active>();
	a->kind     = g_pendingKind;
	a->returnTo = g_pendingReturn;
	a->surface  = SDL_CreateSurface(SCREEN_WIDTH, SCREEN_HEIGHT, SDL_PIXELFORMAT_ARGB8888);
	a->renderer = a->surface ? SDL_CreateSoftwareRenderer(a->surface) : nullptr;
	if (!a->renderer) throw std::runtime_error(std::string("ui spike: ") + SDL_GetError());
	a->screen = spike::CreateScreen(a->kind, a->renderer, spike::SaveListModel::Fake());
	a->screen->setSize(SCREEN_WIDTH, SCREEN_HEIGHT);
	a->screen->mouseMove(gusMouseXPos, gusMouseYPos);
	spike::Screen* const screen = a->screen.get();
	MSYS_DefineRegion(&a->region, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, MSYS_PRIORITY_HIGHEST, CURSOR_NORMAL, MSYS_NO_CALLBACK,
		[screen](MOUSE_REGION*, UINT32 reason) {
			float const x = gusMouseXPos, y = gusMouseYPos;
			if (reason & MSYS_CALLBACK_REASON_LBUTTON_DWN) screen->mouseButton(x, y, true);
			if (reason & MSYS_CALLBACK_REASON_LBUTTON_UP) screen->mouseButton(x, y, false);
			if (reason & MSYS_CALLBACK_REASON_WHEEL_UP)   screen->wheel(-1);
			if (reason & MSYS_CALLBACK_REASON_WHEEL_DOWN) screen->wheel(1);
		});
	a->region.SetName("UI spike");
	g_spike = std::move(a);
}

/** ARGB8888 -> the RGB565 frame buffer */
void Present(SDL_Surface* s)
{
	SGPVSurface::Lock l(FRAME_BUFFER);
	UINT16* const dst  = l.Buffer<UINT16>();
	UINT32  const pitch = l.Pitch() / 2;
	for (int y = 0; y < s->h; ++y)
	{
		auto const* src = reinterpret_cast<Uint32 const*>(static_cast<Uint8 const*>(s->pixels) + y * s->pitch);
		UINT16* d = dst + y * pitch;
		for (int x = 0; x < s->w; ++x)
		{
			Uint32 const p = src[x];
			d[x] = UINT16(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
		}
	}
}
}

void UiSpikeOpen(std::string const& kind)
{
	if (kind != "rml" && kind != "inhouse") throw std::runtime_error("ui spike: unknown toolkit " + kind);
	g_pendingKind = kind;
	if (guiCurrentScreen != UI_SPIKE_SCREEN) g_pendingReturn = guiCurrentScreen;
	g_spike.reset();
	SetPendingNewScreen(UI_SPIKE_SCREEN);
}

UiSpikeInfo UiSpikeGetInfo()
{
	UiSpikeInfo info;
	if (!g_spike) return info;
	info.toolkit     = g_spike->kind;
	info.lastFrameMs = g_spike->lastMs;
	info.frames      = g_spike->frames;
	info.meanFrameMs = g_spike->frames ? g_spike->totalMs / g_spike->frames : 0;
	spike::SaveListModel const& m = g_spike->screen->model();
	info.selected = m.selected;
	info.hovered  = m.hovered;
	info.modal    = m.modal;
	info.status   = m.status;
	for (auto const& e : g_spike->screen->elements())
	{
		info.elements.push_back({ e.id, e.rect.x, e.rect.y, e.rect.w, e.rect.h });
	}
	return info;
}

ScreenID UiSpikeScreenHandle()
{
	if (!g_spike) Create();
	Active& a = *g_spike;
	spike::Screen& s = *a.screen;

	InputAtom e;
	while (DequeueEvent(&e))
	{
		switch (e.usEvent)
		{
			case KEY_DOWN:
			case KEY_REPEAT:
				// Esc acts on release, so the release does not reach the screen we return to (the main menu quits on it)
				if (e.usParam != SDLK_ESCAPE) s.key(SDL_Keycode(e.usParam));
				break;
			case KEY_UP:
				if (e.usParam == SDLK_ESCAPE) s.key(SDLK_ESCAPE);
				break;
			default: break;
		}
	}
	s.mouseMove(gusMouseXPos, gusMouseYPos);

	auto const t0 = std::chrono::steady_clock::now();
	s.update(1.0 / 60); // one frame of the game's (virtual) clock
	s.render();
	SDL_FlushRenderer(a.renderer); // the software renderer batches until a flush
	Present(a.surface);
	a.lastMs   = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	a.totalMs += a.lastMs;
	++a.frames;
	InvalidateScreen();

	if (s.model().closeRequested)
	{
		ScreenID const back = a.returnTo;
		g_spike.reset();
		return back;
	}
	return UI_SPIKE_SCREEN;
}

#else

void UiSpikeOpen(std::string const&)
{
	throw std::runtime_error("the native spikes are not built in (cmake -DWITH_NATIVE_SPIKES=ON)");
}

UiSpikeInfo UiSpikeGetInfo() { return {}; }

ScreenID UiSpikeScreenHandle() { return MAINMENU_SCREEN; }

#endif
