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
#include "WorldSpikeRaster.h"
#include "Directories.h"
#include "Logger.h"
#include "HImage.h"
#include "VObject.h"
#include <cstdlib>

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

/** "face-<n>": the merc's big portrait (FACES/BIGFACES/<n>.sti) from the player's game data, as an RGBA surface.
 * Loaded at runtime only; nothing derived from it is written anywhere. */
SDL_Surface* LoadFaceSurface(std::string const& name)
{
	int const index = std::atoi(name.c_str() + 5);
	try
	{
		ST::string const file = ST::format(FACESDIR "/bigfaces/{02d}.sti", index);
		AutoSGPVObject vo(AddVideoObjectFromFile(file));
		if (vo->BPP() != 8) return nullptr;
		ETRLEObject const& e = vo->SubregionProperties(0);
		spike::world::Sprite const sp = spike::world::DecodeEtrle(vo->PixData(e), e.uiDataLength, e.usWidth, e.usHeight, 0, 0);
		SDL_Surface* s = SDL_CreateSurface(sp.w, sp.h, SDL_PIXELFORMAT_RGBA32);
		if (!s) return nullptr;
		SGPPaletteEntry const* pal = vo->Palette();
		for (int y = 0; y < sp.h; ++y)
		{
			auto* row = static_cast<Uint8*>(s->pixels) + y * s->pitch;
			for (int x = 0; x < sp.w; ++x)
			{
				size_t const i = size_t(y) * sp.w + x;
				SGPPaletteEntry const& c = pal[sp.index[i]];
				row[x * 4 + 0] = c.r;
				row[x * 4 + 1] = c.g;
				row[x * 4 + 2] = c.b;
				row[x * 4 + 3] = sp.mask[i] ? 255 : 0;
			}
		}
		return s;
	}
	catch (std::exception const& ex)
	{
		SLOGW("style demo: no face {}: {}", index, ex.what());
		return nullptr;
	}
}

void Create()
{
	auto a = std::make_unique<Active>();
	a->kind     = g_pendingKind;
	a->returnTo = g_pendingReturn;
	a->surface  = SDL_CreateSurface(SCREEN_WIDTH, SCREEN_HEIGHT, SDL_PIXELFORMAT_ARGB8888);
	a->renderer = a->surface ? SDL_CreateSoftwareRenderer(a->surface) : nullptr;
	if (!a->renderer) throw std::runtime_error(std::string("ui spike: ") + SDL_GetError());
	float uiScale = 1;
	if (a->kind.rfind("style:", 0) == 0)
	{
		// "style:<direction>:<screen>" (Phase 1 style directions)
		std::string const rest = a->kind.substr(6);
		size_t const colon = rest.find(':');
		spike::SetImageProvider(LoadFaceSurface);
		a->screen = spike::CreateStyleDemoScreen(a->renderer, rest.substr(0, colon), colon == std::string::npos ? "mainmenu" : rest.substr(colon + 1));
	}
	else if (a->kind.rfind("gallery:", 0) == 0)
	{
		// "gallery:<page>:<ui scale>" (Phase 1 design-system gallery)
		std::string const rest = a->kind.substr(8);
		size_t const colon = rest.find(':');
		if (colon != std::string::npos) uiScale = std::strtof(rest.c_str() + colon + 1, nullptr);
		spike::SetImageProvider(LoadFaceSurface);
		a->screen = spike::CreateGalleryScreen(a->renderer, rest.substr(0, colon));
	}
	else
	{
		a->screen = spike::CreateScreen(a->kind, a->renderer, spike::SaveListModel::Fake());
	}
	a->screen->setSize(SCREEN_WIDTH, SCREEN_HEIGHT, uiScale > 0 ? uiScale : 1.f);
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
	if (kind != "rml" && kind != "inhouse" && kind.rfind("style:", 0) != 0 && kind.rfind("gallery:", 0) != 0) throw std::runtime_error("ui spike: unknown toolkit " + kind);
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
	info.problems = g_spike->screen->layoutProblems();
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
