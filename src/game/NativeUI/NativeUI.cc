// The native UI runtime (NativeUI.h): one RmlUi context over the whole output, the native screens and the shared
// overlays, input, both render paths, and what automation sees.
#include "NativeUI.h"
#include "NativeUIRuntime.h"
#include "NativeImages.h"
#include "ViewModel.h"

#include "Automation.h"
#include "Cursor_Control.h"
#include "Cursors.h"
#include "GameRes.h"
#include "Headless.h"
#include "Input.h"
#include "JAScreens.h"
#include "Localization.h"
#include "Logger.h"
#include "Timer.h"
#include "UiCore.h"
#include "Video.h"
#include "json/Json.h"

#include <string_theory/format>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>

namespace NativeUI
{

namespace
{
	constexpr double TOAST_SECONDS = 5.0;
	constexpr int    MAX_TOASTS    = 4;

	struct ToastRow
	{
		int id = 0;
		std::string kind, icon, title, text;
		double until = 0;
	};

	struct Route
	{
		ScreenID id;
		char const* key;
		std::unique_ptr<Screen> (*make)();
	};
	Route const g_routes[] = {
		{ CREDIT_SCREEN, "credits", &CreateCreditsScreen },
	};

	class Runtime final : public VideoOverlay
	{
	public:
		bool running = false;
		bool failed = false; // could not start: do not retry every frame
		bool gpu = false;
		std::unique_ptr<nui::SdlRenderInterface> ri;
		SDL_Surface* layer = nullptr;      // software path: premultiplied ARGB, output sized
		Rml::Context* ctx = nullptr;
		Rml::ElementDocument* overlays = nullptr;
		Rml::ElementDocument* msgbox = nullptr;
		int w = 0, h = 0;
		float dp = 0;
		float userScale = 1;
		int dirtyFrames = 0;
		SDL_Rect area{ 0, 0, 0, 0 };
		VideoOutputMapping map{ 1, 1, 0, 0, 0, 0, false };
		float mouseX = -1, mouseY = -1; // output pixels

		// screen routing
		ScreenID                routedScreen = ERROR_SCREEN;
		std::unique_ptr<Screen> screen;
		std::string             screenKey;
		ScreenID                suspendedFor = ERROR_SCREEN; // a native screen waiting under a message box

		// overlays
		Rml::DataModelHandle toastModel, boxModel;
		std::vector<ToastRow> toasts;
		int  nextToast = 1;
		std::string boxText, boxTitle;
		bool boxDanger = false;
		std::vector<MessageBoxButton> boxButtons;
		int  boxResult = 0;
		bool tipShown = false;

		std::map<std::string, std::string> strings;

		// --- VideoOverlay
		bool UsesGpu() override { return running && gpu; }
		SDL_Rect SoftwarePrepare() override;
		void SoftwareCompose(SDL_Surface* dst, SDL_Rect const& area) override;
		uint32_t SoftwarePixel(int x, int y) override;
		void GpuRender(SDL_Renderer*) override;
		bool HidesLegacyCursor() override { return running && NativeCursorShown(); }

		bool AnythingShown() const;
		bool NativeCursorShown() const;
		void Sync();
		void UpdateOverlays();
	};

	Runtime g_rt;

	bool WantsGpu()
	{
		if (!GameRenderer || sgp::IsHeadless()) return false;
		char const* env = std::getenv("JA2_NATIVE_UI_RENDERER");
		std::string const want = env ? env : "";
		if (want == "gpu") return true; // also in a --show automation session: screenshots then read the window back
		return want != "software" && !Automation::GetOptions().Active();
	}

	/** Output size the native UI would get now. */
	void OutputSize(int& w, int& h)
	{
		if (WantsGpu() && g_game_window)
		{
			SDL_GetWindowSizeInPixels(g_game_window, &w, &h);
			return;
		}
		SDL_Surface const* s = GetScreenBuffer();
		w = s ? s->w : 0;
		h = s ? s->h : 0;
	}

	float ComputeDp(int const w, int const h, float const user)
	{
		float scale = std::min(w / 1920.f, h / 1080.f) * user;
		// the layout never gets smaller than 1280x720 dp
		scale = std::min({ scale, w / float(MIN_WIDTH), h / float(MIN_HEIGHT) });
		return std::max(scale, 0.25f);
	}

	std::string UiDir()
	{
		// <binary dir>/ui (or $JA2_UI_DIR); an install on Linux keeps it in the assets dir (share/ja2/ui)
		std::string d = nui::StyleDir();
		if (SDL_GetPathInfo((d + "/tokens.rcss").c_str(), nullptr)) return d;
		return {};
	}

	void LoadStrings()
	{
		g_rt.strings.clear();
		auto load = [](std::string const& suffix) {
			std::string const text = nui::ReadUiFile("strings/strings" + suffix + ".json");
			if (text.empty()) return;
			try
			{
				JsonObject const obj = JsonValue::deserialize(text.c_str()).toObject();
				for (ST::string const& k : obj.keys())
				{
					std::string const key = k.to_std_string();
					if (!g_rt.strings.count(key)) g_rt.strings[key] = obj.GetString(k.c_str()).to_std_string();
				}
			}
			catch (std::exception const& e)
			{
				SLOGW("native UI strings{}: {}", suffix, e.what());
			}
		};
		load(L10n::GetSuffix(getGameVersion(), true)); // the game's language first, English fills the gaps
		load("-eng");
	}

	Rml::Input::KeyIdentifier ToRmlKey(SDL_Keycode const k)
	{
		using namespace Rml::Input;
		if (k >= SDLK_A && k <= SDLK_Z) return KeyIdentifier(KI_A + (k - SDLK_A));
		if (k >= SDLK_0 && k <= SDLK_9) return KeyIdentifier(KI_0 + (k - SDLK_0));
		if (k >= SDLK_F1 && k <= SDLK_F12) return KeyIdentifier(KI_F1 + (k - SDLK_F1));
		switch (k)
		{
			case SDLK_TAB:       return KI_TAB;
			case SDLK_RETURN:    return KI_RETURN;
			case SDLK_KP_ENTER:  return KI_NUMPADENTER;
			case SDLK_ESCAPE:    return KI_ESCAPE;
			case SDLK_SPACE:     return KI_SPACE;
			case SDLK_BACKSPACE: return KI_BACK;
			case SDLK_DELETE:    return KI_DELETE;
			case SDLK_INSERT:    return KI_INSERT;
			case SDLK_HOME:      return KI_HOME;
			case SDLK_END:       return KI_END;
			case SDLK_PAGEUP:    return KI_PRIOR;
			case SDLK_PAGEDOWN:  return KI_NEXT;
			case SDLK_LEFT:      return KI_LEFT;
			case SDLK_RIGHT:     return KI_RIGHT;
			case SDLK_UP:        return KI_UP;
			case SDLK_DOWN:      return KI_DOWN;
			default:             return KI_UNKNOWN;
		}
	}

	int ToRmlMods(UINT16 const state)
	{
		int m = 0;
		if (state & SHIFT_DOWN) m |= Rml::Input::KM_SHIFT;
		if (state & CTRL_DOWN)  m |= Rml::Input::KM_CTRL;
		if (state & ALT_DOWN)   m |= Rml::Input::KM_ALT;
		return m;
	}

	std::string Utf8(char32_t const* s)
	{
		std::string out;
		for (; s && *s; ++s)
		{
			char32_t const c = *s;
			if (c < 0x80) out += char(c);
			else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
			else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
			else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
		}
		return out;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Start, size, clock

bool Built() { return true; }

bool Available(std::string* reason)
{
	if (g_rt.failed)
	{
		if (reason) *reason = "the native UI failed to start (see the log)";
		return false;
	}
	if (UiDir().empty())
	{
		if (reason) *reason = "no UI assets (" + nui::StyleDir() + "/tokens.rcss)";
		return false;
	}
	int w = 0, h = 0;
	OutputSize(w, h);
	if (w < MIN_WIDTH || h < MIN_HEIGHT)
	{
		if (reason) *reason = ST::format("the output is {}x{}, below {}x{}", w, h, MIN_WIDTH, MIN_HEIGHT).to_std_string();
		return false;
	}
	return true;
}

float UserScale() { return g_rt.userScale; }

void SetUserScale(float const s)
{
	g_rt.userScale = std::isfinite(s) ? std::clamp(s, 0.5f, 3.f) : 1.f;
	if (g_rt.running) g_rt.Sync();
}

float DpScale() { return g_rt.running ? g_rt.dp : 0; }

bool Start()
{
	if (g_rt.running) return true;
	if (g_rt.failed || !Available()) return false;
	try
	{
		nui::InitRml();
		nui::LoadUiFonts();
		nui::RmlClock().onKeyboard = [](bool const on) {
			if (!g_game_window || sgp::IsHeadless()) return;
			if (on) SDL_StartTextInput(g_game_window);
			else SDL_StopTextInput(g_game_window);
		};
		nui::SetImageProvider(ProvideGameImage);
		LoadStrings();
		g_rt.gpu = WantsGpu();
		OutputSize(g_rt.w, g_rt.h);
		if (g_rt.gpu)
		{
			g_rt.ri = std::make_unique<nui::SdlRenderInterface>(GameRenderer);
		}
		else
		{
			g_rt.layer = SDL_CreateSurface(g_rt.w, g_rt.h, SDL_PIXELFORMAT_ARGB8888);
			if (!g_rt.layer) throw std::runtime_error(SDL_GetError());
			SDL_FillSurfaceRect(g_rt.layer, nullptr, 0);
			g_rt.ri = std::make_unique<nui::SdlRenderInterface>(g_rt.layer);
		}
		g_rt.ctx = Rml::CreateContext("native", { g_rt.w, g_rt.h }, g_rt.ri.get());
		if (!g_rt.ctx) throw std::runtime_error("RmlUi: CreateContext failed");
		g_rt.dp = ComputeDp(g_rt.w, g_rt.h, g_rt.userScale);
		g_rt.ctx->SetDensityIndependentPixelRatio(g_rt.dp);
		g_rt.running = true;

		// the shared overlays (toasts, tooltip, cursor) are always loaded
		Rml::DataModelConstructor c = g_rt.ctx->CreateDataModel("toasts");
		if (auto s = c.RegisterStruct<ToastRow>())
		{
			s.RegisterMember("id", &ToastRow::id);
			s.RegisterMember("kind", &ToastRow::kind);
			s.RegisterMember("icon", &ToastRow::icon);
			s.RegisterMember("title", &ToastRow::title);
			s.RegisterMember("text", &ToastRow::text);
		}
		c.RegisterArray<std::vector<ToastRow>>();
		c.Bind("toasts", &g_rt.toasts);
		c.BindEventCallback("dismiss", [](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const& a) {
			int const id = a.empty() ? 0 : a[0].Get<int>();
			std::erase_if(g_rt.toasts, [&](ToastRow const& t) { return t.id == id; });
			h.DirtyVariable("toasts");
			Invalidate();
		});
		g_rt.toastModel = c.GetModelHandle();

		Rml::DataModelConstructor b = g_rt.ctx->CreateDataModel("msgbox");
		if (auto s = b.RegisterStruct<MessageBoxButton>())
		{
			s.RegisterMember("id", &MessageBoxButton::id);
			s.RegisterMember("label", &MessageBoxButton::label);
			s.RegisterMember("key", &MessageBoxButton::key);
			s.RegisterMember("result", &MessageBoxButton::result);
			s.RegisterMember("primary", &MessageBoxButton::primary);
		}
		b.RegisterArray<std::vector<MessageBoxButton>>();
		b.Bind("text", &g_rt.boxText);
		b.Bind("title", &g_rt.boxTitle);
		b.Bind("danger", &g_rt.boxDanger);
		b.Bind("buttons", &g_rt.boxButtons);
		b.BindEventCallback("press", [](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& a) {
			g_rt.boxResult = a.empty() ? 0 : a[0].Get<int>();
		});
		g_rt.boxModel = b.GetModelHandle();

		g_rt.overlays = LoadDocument("overlays/overlays.rml");
		g_rt.overlays->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
		VideoSetOverlay(&g_rt);
		SLOGI("native UI: {} {}x{}, 1dp = {.2f}px", g_rt.gpu ? "GPU" : "software", g_rt.w, g_rt.h, g_rt.dp);
		return true;
	}
	catch (std::exception const& e)
	{
		SLOGE("native UI: cannot start: {}", e.what());
		g_rt.failed = true;
		g_rt.running = false;
		return false;
	}
}

Rml::Context* Context() { return g_rt.ctx; }

Rml::ElementDocument* LoadDocument(std::string const& path)
{
	if (!g_rt.ctx) throw std::runtime_error("native UI is not running");
	std::string const rml = nui::ExpandTokens(nui::ReadUiFile(path));
	if (rml.empty()) throw std::runtime_error("native UI: no " + nui::StyleDir() + "/" + path);
	Rml::ElementDocument* d = g_rt.ctx->LoadDocumentFromMemory(rml, path);
	if (!d) throw std::runtime_error("native UI: cannot load " + path);
	Invalidate();
	return d;
}

void CloseDocument(Rml::ElementDocument* d)
{
	if (!d) return;
	d->Close();
	Invalidate();
}

void Invalidate(int const frames)
{
	g_rt.dirtyFrames = std::max(g_rt.dirtyFrames, frames);
}

std::string Str(std::string const& key)
{
	if (g_rt.strings.empty() && g_rt.running) LoadStrings();
	auto it = g_rt.strings.find(key);
	return it != g_rt.strings.end() ? it->second : key;
}

std::string Escape(std::string const& text)
{
	std::string out;
	for (char const c : text)
	{
		switch (c)
		{
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			default: out += c;
		}
	}
	return out;
}

Rml::Vector2f MousePosition() { return { g_rt.mouseX, g_rt.mouseY }; }

void Runtime::Sync()
{
	int nw = 0, nh = 0;
	OutputSize(nw, nh);
	map = VideoGetOutputMapping();
	float const ndp = ComputeDp(nw, nh, userScale);
	if (nw == w && nh == h && std::abs(ndp - dp) < 1e-4f) return;
	w = nw; h = nh; dp = ndp;
	if (!gpu)
	{
		SDL_Surface* s = SDL_CreateSurface(std::max(w, 1), std::max(h, 1), SDL_PIXELFORMAT_ARGB8888);
		if (s)
		{
			SDL_FillSurfaceRect(s, nullptr, 0);
			ri->SetSoftwareTarget(s);
			if (layer) SDL_DestroySurface(layer);
			layer = s;
		}
	}
	ctx->SetDimensions({ w, h });
	ctx->SetDensityIndependentPixelRatio(dp);
	SLOGI("native UI: output {}x{}, 1dp = {.2f}px", w, h, dp);
	if (screen) screen->Resized();
	Invalidate();
}

bool Runtime::AnythingShown() const
{
	if (!ctx) return false;
	if (screen || (msgbox && msgbox->IsVisible()) || !toasts.empty() || tipShown || NativeCursorShown()) return true;
	return false;
}

bool Runtime::NativeCursorShown() const
{
	if (!running) return false;
	if (screen || (msgbox && msgbox->IsVisible())) return true;
	return ConfiguredMode("cursor") == UiMode::Native && GetCurrentCursorIndex() == CURSOR_NORMAL;
}

void Runtime::UpdateOverlays()
{
	// toasts expire on the game clock
	double const now = nui::RmlClock().now;
	size_t const before = toasts.size();
	std::erase_if(toasts, [&](ToastRow const& t) { return t.until <= now; });
	if (toasts.size() != before) { toastModel.DirtyVariable("toasts"); Invalidate(); }

	if (!overlays) return;
	if (Rml::Element* c = overlays->GetElementById("nui-cursor"))
	{
		bool const show = NativeCursorShown() && mouseX >= 0;
		c->SetClass("shown", show);
		if (show)
		{
			c->SetProperty(Rml::PropertyId::Left, Rml::Property(std::floor(mouseX), Rml::Unit::PX));
			c->SetProperty(Rml::PropertyId::Top, Rml::Property(std::floor(mouseY), Rml::Unit::PX));
		}
	}
	// the overlay document stays above everything else
	overlays->PullToFront();
}

void BeginFrame()
{
	ViewModel::UpdateAll();
	if (!g_rt.running)
	{
		// screens and overlays start the runtime; nothing to do until then
		return;
	}
	g_rt.Sync();
	nui::RmlClock().now = GetClock() / 1000.0;
	g_rt.UpdateOverlays();
}

SDL_Rect Runtime::SoftwarePrepare()
{
	if (!running || gpu) return { 0, 0, 0, 0 };
	nui::RmlClock().now = GetClock() / 1000.0;
	UpdateOverlays();
	if (!AnythingShown())
	{
		if (area.w > 0) { SDL_FillSurfaceRect(layer, &area, 0); area = { 0, 0, 0, 0 }; }
		ctx->Update();
		return area;
	}
	ctx->Update();
	if (dirtyFrames > 0 || area.w <= 0)
	{
		if (dirtyFrames > 0) --dirtyFrames;
		if (area.w > 0) SDL_FillSurfaceRect(layer, &area, 0);
		ri->TakeDrawnBounds();
		ctx->Render();
		area = ri->TakeDrawnBounds();
	}
	return area;
}

void Runtime::SoftwareCompose(SDL_Surface* const dst, SDL_Rect const& a)
{
	if (!layer || dst->format != SDL_PIXELFORMAT_RGB565) return;
	int const x0 = std::max(0, a.x), y0 = std::max(0, a.y);
	int const x1 = std::min({ a.x + a.w, dst->w, layer->w }), y1 = std::min({ a.y + a.h, dst->h, layer->h });
	for (int y = y0; y < y1; ++y)
	{
		auto const* src = reinterpret_cast<Uint32 const*>(static_cast<Uint8 const*>(layer->pixels) + size_t(y) * layer->pitch);
		auto* d = reinterpret_cast<Uint16*>(static_cast<Uint8*>(dst->pixels) + size_t(y) * dst->pitch);
		for (int x = x0; x < x1; ++x)
		{
			Uint32 const p = src[x];
			Uint32 const alpha = p >> 24;
			if (alpha == 0) continue;
			Uint32 r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
			if (alpha < 255)
			{
				Uint16 const q = d[x];
				Uint32 const dr = ((q >> 11) & 0x1F) * 255 / 31, dg = ((q >> 5) & 0x3F) * 255 / 63, db = (q & 0x1F) * 255 / 31;
				Uint32 const k = 255 - alpha;
				r = std::min<Uint32>(255, r + dr * k / 255);
				g = std::min<Uint32>(255, g + dg * k / 255);
				b = std::min<Uint32>(255, b + db * k / 255);
			}
			d[x] = Uint16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		}
	}
}

uint32_t Runtime::SoftwarePixel(int const x, int const y)
{
	if (!layer || x < 0 || y < 0 || x >= layer->w || y >= layer->h) return 0;
	return reinterpret_cast<Uint32 const*>(static_cast<Uint8 const*>(layer->pixels) + size_t(y) * layer->pitch)[x];
}

void Runtime::GpuRender(SDL_Renderer*)
{
	if (!running || !gpu || !AnythingShown()) return;
	nui::RmlClock().now = GetClock() / 1000.0;
	ctx->Update();
	ctx->Render();
}

// ---------------------------------------------------------------------------------------------------------------
// Input

bool CapturesMouse()
{
	return g_rt.running && (g_rt.screen || (g_rt.msgbox && g_rt.msgbox->IsVisible()));
}

void MouseMoved(int const canvasX, int const canvasY)
{
	if (!g_rt.running) return;
	float const x = canvasX * g_rt.map.sx + g_rt.map.ox + g_rt.map.sx / 2;
	float const y = canvasY * g_rt.map.sy + g_rt.map.oy + g_rt.map.sy / 2;
	if (x == g_rt.mouseX && y == g_rt.mouseY) return;
	g_rt.mouseX = x;
	g_rt.mouseY = y;
	if (CapturesMouse()) g_rt.ctx->ProcessMouseMove(int(x), int(y), 0);
	if (g_rt.NativeCursorShown()) Invalidate(2);
	else if (CapturesMouse()) Invalidate();
}

void HandleMouseEvent(InputAtom const& e)
{
	if (!g_rt.running) return;
	Rml::Context* const c = g_rt.ctx;
	int const button = e.usParam == MOUSE_BUTTON_RIGHT ? 1 : e.usParam == MOUSE_BUTTON_MIDDLE ? 2 : 0;
	switch (e.usEvent)
	{
		case MOUSE_BUTTON_DOWN: c->ProcessMouseButtonDown(button, 0); break;
		case MOUSE_BUTTON_UP:   c->ProcessMouseButtonUp(button, 0); break;
		case MOUSE_WHEEL_UP:    c->ProcessMouseWheel(Rml::Vector2f(0, -1), 0); break;
		case MOUSE_WHEEL_DOWN:  c->ProcessMouseWheel(Rml::Vector2f(0, 1), 0); break;
		default: return;
	}
	Invalidate();
}

bool ProcessKey(InputAtom const& e)
{
	if (!g_rt.running) return false;
	Rml::Context* const c = g_rt.ctx;
	Invalidate();
	switch (e.usEvent)
	{
		case TEXT_INPUT:
		{
			std::string const text = Utf8(e.codepoints.c_str());
			if (text.empty()) return false;
			// only a focused text field takes text
			Rml::Element* f = c->GetFocusElement();
			if (!f || f->GetTagName() != "input") return false;
			c->ProcessTextInput(text);
			return true;
		}
		case KEY_DOWN:
		case KEY_REPEAT:
		{
			auto const k = ToRmlKey(SDL_Keycode(e.usParam));
			if (k == Rml::Input::KI_UNKNOWN)
			{
				// letters typed into a text field arrive as TEXT_INPUT; swallow the key so screens ignore it
				Rml::Element* f = c->GetFocusElement();
				return f && f->GetTagName() == "input";
			}
			bool const notConsumed = c->ProcessKeyDown(k, ToRmlMods(e.usKeyState));
			Rml::Element* f = c->GetFocusElement();
			// a focused text field keeps letters and editing keys from screen shortcuts
			bool const typing = f && f->GetTagName() == "input" && k != Rml::Input::KI_ESCAPE && k != Rml::Input::KI_TAB;
			return !notConsumed || typing;
		}
		case KEY_UP:
		{
			auto const k = ToRmlKey(SDL_Keycode(e.usParam));
			if (k == Rml::Input::KI_UNKNOWN) return false;
			return !c->ProcessKeyUp(k, ToRmlMods(e.usKeyState));
		}
		default:
			return false;
	}
}

bool HandleKeyEvent(InputAtom const& e)
{
	return ProcessKey(e);
}

// ---------------------------------------------------------------------------------------------------------------
// Screen routing

char const* ScreenKey(ScreenID const id)
{
	for (Route const& r : g_routes)
	{
		if (r.id == id) return r.key;
	}
	return nullptr;
}

ScreenID HandleScreen(ScreenID const id, ScreenID (* const legacy)())
{
	Runtime& rt = g_rt;
	if (id != rt.routedScreen)
	{
		if (rt.screen && id == MSG_BOX_SCREEN)
		{
			// a message box over the native screen: keep it (drawn, not handled) until the box is gone
			rt.suspendedFor = rt.routedScreen;
		}
		else if (rt.screen && id != rt.suspendedFor)
		{
			rt.screen->Exit();
			rt.screen.reset();
			rt.screenKey.clear();
			rt.suspendedFor = ERROR_SCREEN;
			Invalidate();
		}
		ScreenID const previous = rt.routedScreen;
		rt.routedScreen = id;
		if (rt.screen && id == rt.suspendedFor)
		{
			rt.suspendedFor = ERROR_SCREEN; // back from the message box
		}
		else if (!rt.screen)
		{
			// entering a screen: resolve its ui_mode once, for the whole visit
			if (char const* key = ScreenKey(id))
			{
				std::string reason;
				UiMode const mode = ResolveMode(key, &reason);
				if (mode == UiMode::Native && Start())
				{
					for (Route const& r : g_routes)
					{
						if (r.id != id) continue;
						try
						{
							rt.screen = r.make();
							rt.screenKey = key;
							rt.screen->Enter();
						}
						catch (std::exception const& e)
						{
							SLOGE("native {} screen failed to open, using the legacy one: {}", key, e.what());
							rt.screen.reset();
							rt.screenKey.clear();
						}
					}
				}
				SLOGI("screen {}: {} UI ({})", key, rt.screen ? "native" : "legacy", rt.screen ? std::string("configured native") : mode == UiMode::Native ? std::string("the native UI could not start") : reason);
			}
		}
		(void)previous;
	}

	if (rt.screen && rt.suspendedFor == ERROR_SCREEN)
	{
		ScreenID const next = rt.screen->Handle();
		if (rt.screen->Finished())
		{
			// a mock closed: the legacy screen underneath runs again (and draws itself anew)
			rt.screen->Exit();
			rt.screen.reset();
			rt.screenKey.clear();
			rt.routedScreen = id;
			Invalidate();
			InvalidateScreen();
			return legacy();
		}
		if (next != id && next != MSG_BOX_SCREEN)
		{
			rt.screen->Exit();
			rt.screen.reset();
			rt.screenKey.clear();
			rt.routedScreen = ERROR_SCREEN;
			Invalidate();
		}
		return next;
	}
	return legacy();
}

void OpenMock(std::string const& path)
{
	Runtime& rt = g_rt;
	if (!Start()) throw std::runtime_error("the native UI cannot run here (see ja2.nativeUi())");
	if (rt.screen)
	{
		rt.screen->Exit();
		rt.screen.reset();
	}
	rt.routedScreen = guiCurrentScreen;
	rt.suspendedFor = ERROR_SCREEN;
	rt.screen = CreateMockScreen(path, guiCurrentScreen);
	rt.screenKey = "mock";
	rt.screen->Enter();
	Invalidate();
}

// ---------------------------------------------------------------------------------------------------------------
// Overlays

bool ToastsActive()
{
	if (g_rt.screen) return true;
	return ResolveMode("toasts") == UiMode::Native;
}

void Toast(std::string const& text, ToastKind const kind, std::string const& title)
{
	if (!Start()) return;
	ToastRow t;
	t.id = g_rt.nextToast++;
	switch (kind)
	{
		case ToastKind::Ok:     t.kind = "ok";     t.icon = "icon-ok";      break;
		case ToastKind::Warn:   t.kind = "warn";   t.icon = "icon-warning"; break;
		case ToastKind::Danger: t.kind = "danger"; t.icon = "icon-error";   break;
		default:                t.kind = "";       t.icon = "icon-info";    break;
	}
	t.title = title;
	t.text = text;
	t.until = GetClock() / 1000.0 + TOAST_SECONDS;
	g_rt.toasts.push_back(t);
	while (g_rt.toasts.size() > MAX_TOASTS) g_rt.toasts.erase(g_rt.toasts.begin());
	g_rt.toastModel.DirtyVariable("toasts");
	Invalidate();
}

bool ShowFastHelp(char32_t const* text, int const x, int const y, int const w, int const h)
{
	if (ResolveMode("tooltip") != UiMode::Native || !Start() || !g_rt.overlays) return false;
	Rml::Element* tip = g_rt.overlays->GetElementById("nui-tooltip");
	if (!tip) return false;
	// legacy markup: |X bold (the hotkey), ^X green, ~X red; lines separated by \n
	std::string rml = "<div class=\"tip-body\">";
	for (char32_t const* p = text; *p; ++p)
	{
		char32_t c = *p;
		char const* cls = nullptr;
		if ((c == U'|' || c == U'^' || c == U'~') && p[1]) { cls = c == U'|' ? "tip-key" : c == U'^' ? "tip-good" : "tip-bad"; c = *++p; }
		if (c == U'\n') { rml += "<br/>"; continue; }
		char32_t const one[2] = { c, 0 };
		std::string const ch = Escape(Utf8(one));
		rml += cls ? std::string("<span class=\"") + cls + "\">" + ch + "</span>" : ch;
	}
	rml += "</div>";
	tip->SetInnerRML(rml);
	// below the region, or above it near the bottom of the screen; in output pixels
	VideoOutputMapping const& m = g_rt.map;
	float const left = x * m.sx + m.ox;
	float const top  = (y + h) * m.sy + m.oy + 8 * g_rt.dp;
	tip->SetProperty(Rml::PropertyId::Left, Rml::Property(std::floor(left), Rml::Unit::PX));
	tip->SetProperty(Rml::PropertyId::Top, Rml::Property(std::floor(top), Rml::Unit::PX));
	tip->SetClass("shown", true);
	g_rt.ctx->Update();
	// keep it on screen
	Rml::Vector2f const size = tip->GetBox().GetSize(Rml::BoxArea::Border);
	float const maxX = g_rt.w - size.x - 4, maxY = g_rt.h - size.y - 4;
	if (left > maxX) tip->SetProperty(Rml::PropertyId::Left, Rml::Property(std::floor(std::max(0.f, maxX)), Rml::Unit::PX));
	if (top > maxY) tip->SetProperty(Rml::PropertyId::Top, Rml::Property(std::floor(std::max(0.f, y * m.sy + m.oy - size.y - 8 * g_rt.dp)), Rml::Unit::PX));
	(void)w;
	g_rt.tipShown = true;
	Invalidate();
	return true;
}

void HideFastHelp()
{
	if (!g_rt.tipShown || !g_rt.overlays) return;
	if (Rml::Element* tip = g_rt.overlays->GetElementById("nui-tooltip")) tip->SetClass("shown", false);
	g_rt.tipShown = false;
	Invalidate();
}

bool MessageBoxWanted()
{
	if (g_rt.screen) return Start();
	return ResolveMode("msgbox") == UiMode::Native && Start();
}

void OpenMessageBox(std::string const& text, std::vector<MessageBoxButton> const& buttons, bool const danger)
{
	if (!Start()) throw std::runtime_error("native UI is not running");
	g_rt.boxText = text;
	g_rt.boxTitle = Str(buttons.size() > 1 ? "msgbox.confirm" : "msgbox.notice");
	g_rt.boxButtons = buttons;
	g_rt.boxDanger = danger;
	g_rt.boxResult = 0;
	g_rt.boxModel.DirtyAllVariables();
	if (!g_rt.msgbox) g_rt.msgbox = LoadDocument("overlays/msgbox.rml");
	g_rt.msgbox->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
	g_rt.ctx->Update();
	// the primary button has the focus (without the ring): Enter presses it, Tab moves on
	for (MessageBoxButton const& b : buttons)
	{
		if (!b.primary) continue;
		if (Rml::Element* e = g_rt.msgbox->GetElementById(b.id)) e->Focus(false);
	}
	g_rt.overlays->PullToFront();
	Invalidate();
}

bool MessageBoxOpen() { return g_rt.msgbox && g_rt.msgbox->IsVisible(); }
int  MessageBoxResult() { return g_rt.boxResult; }

void CloseMessageBox()
{
	if (!g_rt.msgbox) return;
	g_rt.msgbox->Hide();
	g_rt.boxResult = 0;
	Invalidate();
}

// ---------------------------------------------------------------------------------------------------------------
// Automation

namespace
{
	SDL_FRect Box(Rml::Element* e)
	{
		Rml::Vector2f const p = e->GetAbsoluteOffset(Rml::BoxArea::Border);
		Rml::Vector2f const s = e->GetBox().GetSize(Rml::BoxArea::Border);
		return { p.x, p.y, s.x, s.y };
	}

	/** Output pixels -> canvas pixels (what clicks and the legacy element list use). */
	void ToCanvas(SDL_FRect const& b, int& x, int& y, int& w, int& h)
	{
		VideoOutputMapping const& m = g_rt.map;
		float const x0 = (b.x - m.ox) / m.sx, y0 = (b.y - m.oy) / m.sy;
		float const x1 = (b.x + b.w - m.ox) / m.sx, y1 = (b.y + b.h - m.oy) / m.sy;
		x = int(std::floor(x0)); y = int(std::floor(y0));
		w = std::max(1, int(std::ceil(x1)) - x); h = std::max(1, int(std::ceil(y1)) - y);
	}

	void CollectText(Rml::Element* e, std::string& out)
	{
		if (!e->IsVisible(true)) return;
		if (auto* t = rmlui_dynamic_cast<Rml::ElementText*>(e))
		{
			std::string s = t->GetText();
			if (!s.empty())
			{
				if (!out.empty() && out.back() != ' ') out += ' ';
				out += s;
			}
		}
		for (int i = 0; i < e->GetNumChildren(); ++i) CollectText(e->GetChild(i), out);
	}

	std::string Collapse(std::string s)
	{
		std::string out;
		bool space = false;
		for (char const c : s)
		{
			if (c == ' ' || c == '\n' || c == '\t' || c == '\r') { space = !out.empty(); continue; }
			if (space) { out += ' '; space = false; }
			out += c;
		}
		return out;
	}

	bool Interactive(Rml::Element* e)
	{
		return e->GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto || e->GetTagName() == "button" ||
			e->GetTagName() == "input" || e->HasAttribute("data-event-click");
	}

	template<typename F>
	void ForEachVisible(Rml::Element* e, F&& f)
	{
		if (!e->IsVisible(true)) return;
		f(e);
		for (int i = 0; i < e->GetNumChildren(); ++i) ForEachVisible(e->GetChild(i), f);
	}

	std::vector<Rml::ElementDocument*> Documents()
	{
		std::vector<Rml::ElementDocument*> out;
		if (!g_rt.ctx) return out;
		for (int i = 0; i < g_rt.ctx->GetNumDocuments(); ++i)
		{
			Rml::ElementDocument* d = g_rt.ctx->GetDocument(i);
			if (d && d->IsVisible()) out.push_back(d);
		}
		return out;
	}

	std::string Describe(Rml::Element* e)
	{
		std::string d = e->GetTagName();
		if (!e->GetId().empty()) d += "#" + e->GetId();
		else if (!e->GetClassNames().empty()) d += "." + e->GetClassNames();
		SDL_FRect const b = Box(e);
		return d + ST::format(" [{},{} {}x{}]", int(b.x), int(b.y), int(b.w), int(b.h)).to_std_string();
	}
}

Info GetInfo()
{
	Info i;
	i.running = g_rt.running;
	i.userScale = g_rt.userScale;
	if (!g_rt.running) return i;
	i.renderer = g_rt.gpu ? std::string("gpu (") + SDL_GetRendererName(GameRenderer) + ")" : "software";
	i.width = g_rt.w;
	i.height = g_rt.h;
	i.dp = g_rt.dp;
	i.screen = g_rt.screenKey;
	for (auto* d : Documents()) i.documents.push_back(d->GetSourceURL());
	i.warnings = nui::RmlWarnings();
	return i;
}

std::vector<ElementInfo> Elements()
{
	std::vector<ElementInfo> out;
	if (!g_rt.running) return out;
	g_rt.ctx->Update();
	Rml::Element* focus = g_rt.ctx->GetFocusElement();
	for (Rml::ElementDocument* d : Documents())
	{
		ForEachVisible(d, [&](Rml::Element* e) {
			if (e->GetId().empty() || e == d) return;
			if (e->GetId().rfind("nui-", 0) == 0) return; // the runtime's own (cursor, tooltip)
			SDL_FRect const b = Box(e);
			if (b.w < 1 || b.h < 1) return;
			ElementInfo i;
			i.id = e->GetId();
			i.document = d->GetSourceURL();
			std::string text;
			CollectText(e, text);
			i.text = Collapse(text);
			i.help = e->GetAttribute<Rml::String>("title", "");
			i.label = e->GetAttribute<Rml::String>("data-label", "");
			if (i.label.empty()) i.label = i.text;
			if (i.label.empty()) i.label = i.help;
			i.role = e->GetTagName() == "input" ? "input" : Interactive(e) ? "button" : e->GetTagName();
			i.enabled = !e->IsPseudoClassSet("disabled") && !e->HasAttribute("disabled") && !e->IsClassSet("is-disabled");
			i.focused = focus == e;
			ToCanvas(b, i.x, i.y, i.w, i.h);
			// a click at its centre reaches it (or something inside it)
			Rml::Element* hit = g_rt.ctx->GetElementAtPoint({ b.x + b.w / 2, b.y + b.h / 2 });
			bool inside = false;
			for (Rml::Element* a = hit; a; a = a->GetParentNode()) if (a == e) { inside = true; break; }
			i.clickable = inside && i.enabled && Interactive(e);
			out.push_back(std::move(i));
		});
	}
	return out;
}

std::vector<TextInfo> Texts()
{
	std::vector<TextInfo> out;
	if (!g_rt.running) return out;
	g_rt.ctx->Update();
	for (Rml::ElementDocument* d : Documents())
	{
		ForEachVisible(d, [&](Rml::Element* e) {
			auto* t = rmlui_dynamic_cast<Rml::ElementText*>(e);
			if (!t) return;
			std::string const s = Collapse(t->GetText());
			Rml::Element* p = e->GetParentNode();
			if (s.empty() || !p) return;
			SDL_FRect const b = Box(p);
			if (b.x + b.w <= 0 || b.y + b.h <= 0 || b.x >= g_rt.w || b.y >= g_rt.h) return;
			TextInfo i{ s, 0, 0, 0, 0 };
			ToCanvas(b, i.x, i.y, i.w, i.h);
			out.push_back(i);
		});
	}
	return out;
}

std::vector<std::string> LayoutAudit()
{
	std::vector<std::string> out;
	if (!g_rt.running) return out;
	g_rt.ctx->Update();
	float const tol = 1.5f;
	for (Rml::ElementDocument* d : Documents())
	{
		std::vector<Rml::Element*> interactive;
		ForEachVisible(d, [&](Rml::Element* e) {
			if (out.size() >= 40 || e == d || e->GetId().rfind("nui-", 0) == 0) return;
			if (e->IsClassSet("audit-skip")) return;
			for (Rml::Element* a = e->GetParentNode(); a; a = a->GetParentNode()) if (a->IsClassSet("audit-skip")) return;
			if (e->GetComputedValues().position() == Rml::Style::Position::Absolute && e->IsClassSet("offscreen-ok")) return;
			SDL_FRect const b = Box(e);
			if (b.w <= 0.5f || b.h <= 0.5f) return;
			// the nearest ancestor that clips
			Rml::Element* clip = nullptr;
			for (Rml::Element* a = e->GetParentNode(); a; a = a->GetParentNode())
			{
				auto const& cv = a->GetComputedValues();
				if (cv.overflow_x() != Rml::Style::Overflow::Visible || cv.overflow_y() != Rml::Style::Overflow::Visible) { clip = a; break; }
			}
			if (!clip)
			{
				if (b.x < -tol || b.y < -tol || b.x + b.w > g_rt.w + tol || b.y + b.h > g_rt.h + tol) out.push_back("off screen: " + Describe(e));
			}
			else if (!rmlui_dynamic_cast<Rml::ElementText*>(e))
			{
				// scroll areas may cut vertically; nothing may be cut sideways
				SDL_FRect const c = Box(clip);
				float const sx = clip->GetScrollLeft();
				float const left = c.x + clip->GetClientLeft(), right = left + clip->GetClientWidth();
				if (b.x + sx < left - tol || b.x + b.w + sx > right + tol) out.push_back("clipped sideways by " + Describe(clip) + ": " + Describe(e));
			}
			// text wider than its box (cut, or spilling out)
			bool hasText = false;
			for (int i = 0; i < e->GetNumChildren(); ++i) if (rmlui_dynamic_cast<Rml::ElementText*>(e->GetChild(i))) hasText = true;
			if (hasText && e->GetScrollWidth() > e->GetClientWidth() + tol && e->GetClientWidth() > 0 && !e->HasAttribute("title"))
				out.push_back("truncated text: " + Describe(e));
			if (Interactive(e)) interactive.push_back(e);
		});
		// interactive elements must not overlap (one would hide the other's clicks)
		for (size_t i = 0; i < interactive.size() && out.size() < 40; ++i)
			for (size_t j = i + 1; j < interactive.size(); ++j)
			{
				Rml::Element* a = interactive[i];
				Rml::Element* b = interactive[j];
				bool nested = false;
				for (Rml::Element* p = b->GetParentNode(); p; p = p->GetParentNode()) if (p == a) nested = true;
				for (Rml::Element* p = a->GetParentNode(); p; p = p->GetParentNode()) if (p == b) nested = true;
				if (nested) continue;
				SDL_FRect const ra = Box(a), rb = Box(b);
				float const ox = std::min(ra.x + ra.w, rb.x + rb.w) - std::max(ra.x, rb.x);
				float const oy = std::min(ra.y + ra.h, rb.y + rb.h) - std::max(ra.y, rb.y);
				if (ox > tol && oy > tol) out.push_back("overlap: " + Describe(a) + " and " + Describe(b));
			}
	}
	return out;
}

bool Focus(std::string const& id)
{
	if (!g_rt.running) return false;
	for (Rml::ElementDocument* d : Documents())
	{
		if (Rml::Element* e = d->GetElementById(id))
		{
			Invalidate();
			return e->Focus(true);
		}
	}
	return false;
}

std::string FocusedId()
{
	if (!g_rt.running) return {};
	Rml::Element* f = g_rt.ctx->GetFocusElement();
	return f ? f->GetId() : std::string();
}

}
