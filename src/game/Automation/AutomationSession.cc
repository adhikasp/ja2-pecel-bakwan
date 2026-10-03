#include "AutomationSession.h"
#include "NativeUI.h"
#include "Automation.h"

#include "Button_System.h"
#include "Clock.h"
#include "Dialogue_Control.h"
#include "Fade_Screen.h"
#include "GameLoop.h"
#include "VideoOptionsScreen.h"
#include "Input.h"
#include "JAScreens.h"
#include "Laptop.h"
#include "Logger.h"
#include "MainMenuScreen.h"
#include "Merc_Entering.h"
#include "MessageBoxScreen.h"
#include "MouseSystem.h"
#include "Options_Screen.h"
#include "Overhead.h"
#include "SaveLoadGame.h"
#include "SaveLoadScreen.h"
#include "ScreenIDs.h"
#include "SGP.h"
#include "Soldier_Control.h"
#include "Video.h"

#include <SDL3/SDL.h>
#include <string_theory/format>
#include <string_theory/string>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace Automation
{

namespace
{
	bool g_crashed = false;

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		return s;
	}

	// Lower-case and collapse runs of whitespace, so that "LOAD  GAME" and
	// "Load Game" compare equal.
	std::string Normalize(std::string const& s)
	{
		std::string out;
		bool space = false;
		for (unsigned char c : s)
		{
			if (std::isspace(c)) { space = !out.empty(); continue; }
			if (space) { out += ' '; space = false; }
			out += static_cast<char>(std::tolower(c));
		}
		return out;
	}

	std::string ToStd(ST::utf32_buffer const& b)
	{
		if (b.empty()) return {};
		try { return ST::string::from_utf32(b).to_std_string(); } catch (...) { return {}; }
	}

	bool Contains(SDL_Rect const& r, SDL_Point const& p)
	{
		return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
	}

	uint64_t g_quietSinceFrame = 0;
	bool     g_quiet = false;
	ScreenID g_lastScreen = ERROR_SCREEN;

	// Input always starts a new quiet period: whatever it triggers may take
	// a frame or two to show up.
	void Dispatch(SDL_Event const& e)
	{
		sgp::DispatchInputEvent(e);
		g_quiet = false;
	}

	SDL_Keymod g_mods = SDL_KMOD_NONE;

	struct KeyName { char const* name; SDL_Keycode key; };
	KeyName const KEY_NAMES[] = {
		{ "escape", SDLK_ESCAPE }, { "esc", SDLK_ESCAPE },
		{ "enter", SDLK_RETURN }, { "return", SDLK_RETURN },
		{ "space", SDLK_SPACE }, { "tab", SDLK_TAB },
		{ "backspace", SDLK_BACKSPACE }, { "delete", SDLK_DELETE }, { "insert", SDLK_INSERT },
		{ "up", SDLK_UP }, { "down", SDLK_DOWN }, { "left", SDLK_LEFT }, { "right", SDLK_RIGHT },
		{ "home", SDLK_HOME }, { "end", SDLK_END }, { "pageup", SDLK_PAGEUP }, { "pagedown", SDLK_PAGEDOWN },
		{ "f1", SDLK_F1 }, { "f2", SDLK_F2 }, { "f3", SDLK_F3 }, { "f4", SDLK_F4 },
		{ "f5", SDLK_F5 }, { "f6", SDLK_F6 }, { "f7", SDLK_F7 }, { "f8", SDLK_F8 },
		{ "f9", SDLK_F9 }, { "f10", SDLK_F10 }, { "f11", SDLK_F11 }, { "f12", SDLK_F12 },
		{ "shift", SDLK_LSHIFT }, { "ctrl", SDLK_LCTRL }, { "alt", SDLK_LALT },
		{ "plus", SDLK_PLUS }, { "minus", SDLK_MINUS }, { "pause", SDLK_PAUSE },
	};

	bool ParseKey(std::string const& name, SDL_Keycode& key)
	{
		std::string const n = Lower(name);
		for (auto const& k : KEY_NAMES)
		{
			if (n == k.name) { key = k.key; return true; }
		}
		if (name.size() == 1 && static_cast<unsigned char>(name[0]) >= 0x20 && static_cast<unsigned char>(name[0]) < 0x7f)
		{
			// SDL keycodes of printable keys are their (lower-case) characters.
			key = static_cast<SDL_Keycode>(std::tolower(static_cast<unsigned char>(name[0])));
			return true;
		}
		return false;
	}

	SDL_Keymod ModFor(SDL_Keycode const key)
	{
		switch (key)
		{
			case SDLK_LSHIFT: return SDL_KMOD_LSHIFT;
			case SDLK_LCTRL:  return SDL_KMOD_LCTRL;
			case SDLK_LALT:   return SDL_KMOD_LALT;
			default:          return SDL_KMOD_NONE;
		}
	}

	void SendKey(SDL_Keycode const key, bool const down)
	{
		if (down) g_mods = static_cast<SDL_Keymod>(g_mods | ModFor(key));
		SDL_Event e{};
		e.type     = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		e.key.key  = key;
		e.key.mod  = g_mods;
		e.key.down = down;
		Dispatch(e);
		if (!down) g_mods = static_cast<SDL_Keymod>(g_mods & ~ModFor(key));
	}

	SDL_Surface const* FrameSurface()
	{
		SDL_Surface const* s = GetScreenBuffer();
		if (!s) throw std::runtime_error("the video system is not initialised");
		return s;
	}

	uint32_t Rgb565To888(UINT16 const p)
	{
		uint32_t const r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
		return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
	}

	char const* ScreenIdName(ScreenID const id)
	{
		switch (id)
		{
			case EDIT_SCREEN:              return "EDIT_SCREEN";
			case ERROR_SCREEN:             return "ERROR_SCREEN";
			case INIT_SCREEN:              return "INIT_SCREEN";
			case GAME_SCREEN:              return "GAME_SCREEN";
			case PALEDIT_SCREEN:           return "PALEDIT_SCREEN";
			case DEBUG_SCREEN:             return "DEBUG_SCREEN";
			case MAP_SCREEN:               return "MAP_SCREEN";
			case LAPTOP_SCREEN:            return "LAPTOP_SCREEN";
			case LOADSAVE_SCREEN:          return "LOADSAVE_SCREEN";
			case MAPUTILITY_SCREEN:        return "MAPUTILITY_SCREEN";
			case FADE_SCREEN:              return "FADE_SCREEN";
			case MSG_BOX_SCREEN:           return "MSG_BOX_SCREEN";
			case MAINMENU_SCREEN:          return "MAINMENU_SCREEN";
			case AUTORESOLVE_SCREEN:       return "AUTORESOLVE_SCREEN";
			case SAVE_LOAD_SCREEN:         return "SAVE_LOAD_SCREEN";
			case OPTIONS_SCREEN:           return "OPTIONS_SCREEN";
			case SHOPKEEPER_SCREEN:        return "SHOPKEEPER_SCREEN";
			case SEX_SCREEN:               return "SEX_SCREEN";
			case GAME_INIT_OPTIONS_SCREEN: return "GAME_INIT_OPTIONS_SCREEN";
			case DEMO_EXIT_SCREEN:         return "DEMO_EXIT_SCREEN";
			case INTRO_SCREEN:             return "INTRO_SCREEN";
			case CREDIT_SCREEN:            return "CREDIT_SCREEN";
			case QUEST_DEBUG_SCREEN:       return "QUEST_DEBUG_SCREEN";
			case VIDEO_OPTIONS_SCREEN:     return "VIDEO_OPTIONS_SCREEN";
			case UI_SPIKE_SCREEN:          return "UI_SPIKE_SCREEN";
			default:                       return "UNKNOWN_SCREEN";
		}
	}

	// Structural "nothing is in flight" test; see Session::IsIdle().
	bool NothingInFlight()
	{
		if (guiPendingScreen != NO_PENDING_SCREEN) return false;
		if (VideoChangePending()) return false;
		switch (guiCurrentScreen)
		{
			case INIT_SCREEN:
			case INTRO_SCREEN:
			case FADE_SCREEN:
				return false;
			default:
				break;
		}
		if (gfFadeInitialized || gfFadeIn || gfFadeOut) return false;
		if (guiCurrentScreen == MAINMENU_SCREEN && !MainMenuIsReady()) return false;
		if (guiCurrentScreen == LAPTOP_SCREEN && LaptopIsBusy()) return false;
		if (!DialogueQueueIsEmptyAndNobodyIsTalking()) return false;

		if (guiCurrentScreen == GAME_SCREEN)
		{
			if (gTacticalStatus.ubAttackBusyCount > 0) return false;
			if (HeliDropInProgress()) return false;
			if (gTacticalStatus.uiFlags & INCOMBAT && gTacticalStatus.ubCurrentTeam != OUR_TEAM) return false;
			CFOR_EACH_IN_TEAM(s, OUR_TEAM)
			{
				if (s->bInSector && s->sGridNo != s->sFinalDestination) return false;
			}
		}
		return true;
	}

	void TrackIdle()
	{
		bool const quiet = NothingInFlight() && guiCurrentScreen == g_lastScreen;
		g_lastScreen = guiCurrentScreen;
		if (quiet && !g_quiet) g_quietSinceFrame = sgp::Clock::FrameCount();
		g_quiet = quiet;
	}

	// How far (px) a caption may be from the check box it labels.
	constexpr int MAX_LABEL_GAP = 160;
	// How far (px) below an icon its caption may start.
	constexpr int MAX_CAPTION_GAP = 12;

	// Frames of quiet required before calling the game idle: enough for
	// follow-up work queued by the last event (redraws, screen entry) to run.
	constexpr uint64_t IDLE_FRAMES = 3;
}


std::string Locator::Describe() const
{
	if (point) return ST::format("({}, {})", point->x, point->y).to_std_string();
	if (!id.empty()) return "id \"" + id + "\"";
	std::string d = exact ? "exact \"" + text + "\"" : "\"" + text + "\"";
	if (index != 1) d += ST::format(" #{}", index).to_std_string();
	return d;
}


bool ParseKeyCombo(std::string const& combo, SDL_Keycode& key, SDL_Keymod& mods)
{
	mods = SDL_KMOD_NONE;
	std::string rest = combo;
	// "+" alone (or a trailing "+") is the plus key, not a separator.
	for (;;)
	{
		auto const plus = rest.find('+');
		if (plus == std::string::npos || plus == 0 || plus + 1 == rest.size()) break;
		std::string const mod = Lower(rest.substr(0, plus));
		if      (mod == "shift") mods = static_cast<SDL_Keymod>(mods | SDL_KMOD_LSHIFT);
		else if (mod == "ctrl" || mod == "control") mods = static_cast<SDL_Keymod>(mods | SDL_KMOD_LCTRL);
		else if (mod == "alt")   mods = static_cast<SDL_Keymod>(mods | SDL_KMOD_LALT);
		else return false;
		rest = rest.substr(plus + 1);
	}
	return ParseKey(rest, key);
}


namespace Session
{

bool Crashed() { return g_crashed; }

void Step(unsigned frames)
{
	if (g_crashed) throw GameCrashedError("the game crashed earlier in this session");
	if (sgp::QuitRequested()) throw GameExitedError("the game has exited");
	while (frames-- > 0)
	{
		bool running;
		try
		{
			running = sgp::StepFrame();
		}
		catch (std::exception const& e)
		{
			g_crashed = true;
			SLOGE("Game crashed: {}", e.what());
			throw GameCrashedError(std::string("game crashed: ") + e.what());
		}
		TrackIdle();
		if (!running) throw GameExitedError("the game has exited");
	}
}

uint64_t Frame()     { return sgp::Clock::FrameCount(); }
uint64_t ElapsedMs() { return sgp::Clock::ElapsedMs(); }

void Wait(unsigned const ms)
{
	uint64_t const until = ElapsedMs() + ms;
	while (ElapsedMs() < until) Step();
}

void WaitUntil(std::function<bool()> const& predicate, unsigned const timeoutMs, std::string const& what)
{
	uint64_t const deadline = ElapsedMs() + timeoutMs;
	while (!predicate())
	{
		if (ElapsedMs() >= deadline)
		{
			std::string msg = ST::format("timed out after {} ms waiting for {} (screen: {})",
				timeoutMs, what, ScreenName()).to_std_string();
			// The usual reason for being stuck is a message box nobody answered.
			std::string const box = MessageBoxText();
			if (!box.empty()) msg += "; a message box is open: \"" + box + "\"";
			throw TimeoutError(msg);
		}
		Step();
	}
}


SDL_Point MousePos()
{
	auto const p = GetMousePos();
	return { p.iX, p.iY };
}

void MouseMove(int const x, int const y)
{
	SDL_Event e{};
	e.type     = SDL_EVENT_MOUSE_MOTION;
	e.motion.x = static_cast<float>(x);
	e.motion.y = static_cast<float>(y);
	Dispatch(e);
	Step();
}

void MouseButton(int const button, bool const down)
{
	auto const pos = MousePos();
	SDL_Event e{};
	e.type          = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	e.button.button = static_cast<Uint8>(button == 2 ? SDL_BUTTON_RIGHT : button == 3 ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT);
	e.button.down   = down;
	e.button.clicks = 1;
	e.button.x      = static_cast<float>(pos.x);
	e.button.y      = static_cast<float>(pos.y);
	Dispatch(e);
	Step();
}

void Click(int const x, int const y, int const button, int const count)
{
	MouseMove(x, y);
	for (int i = 0; i < std::max(1, count); ++i)
	{
		MouseButton(button, true);
		MouseButton(button, false);
	}
}

void Drag(int const x0, int const y0, int const x1, int const y1)
{
	MouseMove(x0, y0);
	MouseButton(1, true);
	constexpr int STEPS = 8;
	for (int i = 1; i <= STEPS; ++i)
	{
		MouseMove(x0 + (x1 - x0) * i / STEPS, y0 + (y1 - y0) * i / STEPS);
	}
	MouseButton(1, false);
}

void Wheel(int const x, int const y, int const dy)
{
	MouseMove(x, y);
	for (int i = 0; i < std::abs(dy); ++i)
	{
		SDL_Event e{};
		e.type    = SDL_EVENT_MOUSE_WHEEL;
		e.wheel.y = dy > 0 ? 1.0f : -1.0f;
		e.wheel.mouse_x = static_cast<float>(x);
		e.wheel.mouse_y = static_cast<float>(y);
		Dispatch(e);
		Step();
	}
}

void KeyDown(std::string const& name)
{
	SDL_Keycode key;
	if (!ParseKey(name, key)) throw std::invalid_argument("unknown key: " + name);
	SendKey(key, true);
	Step();
}

void KeyUp(std::string const& name)
{
	SDL_Keycode key;
	if (!ParseKey(name, key)) throw std::invalid_argument("unknown key: " + name);
	SendKey(key, false);
	Step();
}

void Key(std::string const& combo)
{
	SDL_Keycode key;
	SDL_Keymod  mods;
	if (!ParseKeyCombo(combo, key, mods)) throw std::invalid_argument("unknown key combo: " + combo);

	std::vector<SDL_Keycode> held;
	if (mods & SDL_KMOD_LCTRL)  held.push_back(SDLK_LCTRL);
	if (mods & SDL_KMOD_LALT)   held.push_back(SDLK_LALT);
	if (mods & SDL_KMOD_LSHIFT) held.push_back(SDLK_LSHIFT);
	for (auto k : held) SendKey(k, true);
	SendKey(key, true);
	Step();
	SendKey(key, false);
	Step();
	for (auto k = held.rbegin(); k != held.rend(); ++k) SendKey(*k, false);
	if (!held.empty()) Step();
}

void Type(std::string const& text)
{
	ST::utf32_buffer codepoints;
	try { codepoints = ST::string(text).to_utf32(); }
	catch (...) { throw std::invalid_argument("text is not valid UTF-8"); }

	for (char32_t const c : codepoints)
	{
		// A real keyboard produces a key press and a text event for each
		// printable ASCII character; do the same.
		bool const hasKey = c >= 0x20 && c < 0x7f;
		SDL_Keycode const key = hasKey ? static_cast<SDL_Keycode>(std::tolower(static_cast<int>(c))) : SDLK_UNKNOWN;
		bool const shift = hasKey && std::isupper(static_cast<int>(c));
		if (shift) SendKey(SDLK_LSHIFT, true);
		if (hasKey) SendKey(key, true);

		std::string const utf8 = ST::string::from_utf32(ST::utf32_buffer(&c, 1)).to_std_string();
		SDL_Event e{};
		e.type      = SDL_EVENT_TEXT_INPUT;
		e.text.text = utf8.c_str();
		Dispatch(e);

		if (hasKey) SendKey(key, false);
		if (shift) SendKey(SDLK_LSHIFT, false);
		Step();
	}
}


std::string ScreenName() { return ScreenIdName(guiCurrentScreen); }

std::string MessageBoxText()
{
	if (!gfInMsgBox) return {};
	if (NativeUI::MessageBoxOpen()) return NativeUI::MessageBoxText();
	SDL_Rect const box{ gMsgBox.uX, gMsgBox.uY, gMsgBox.usWidth, gMsgBox.usHeight };
	std::string text;
	for (auto const& t : Texts())
	{
		if (!Contains(box, SDL_Point{ t.rect.x + t.rect.w / 2, t.rect.y + t.rect.h / 2 })) continue;
		if (!text.empty()) text += ' ';
		text += t.text.to_std_string();
	}
	return text;
}

std::vector<TextRegistry::VisibleText> Texts()
{
	// the native UI's text first (it is on top), then what the legacy screen printed and is still visible
	std::vector<TextRegistry::VisibleText> out;
	for (NativeUI::TextInfo const& t : NativeUI::Texts())
	{
		out.push_back({ ST::string(t.text), SDL_Rect{ t.x, t.y, t.w, t.h }, std::nullopt });
	}
	auto legacy = TextRegistry::Visible(FrameSurface(), GetMouseCursorRect());
	if (!NativeUI::CapturesMouse() || !NativeUI::ScreenKey(guiCurrentScreen))
	{
		out.insert(out.end(), legacy.begin(), legacy.end());
	}
	return out;
}

std::vector<Element> Elements()
{
	std::map<MOUSE_REGION const*, GUI_BUTTON const*> buttons;
	for (GUI_BUTTON const* b : ButtonList)
	{
		if (b) buttons[b->Area.Region()] = b;
	}

	auto const texts = TextRegistry::Visible(FrameSurface(), GetMouseCursorRect());
	std::vector<Element> out;
	int id = 0;
	// Native elements first: they are drawn over the legacy screen
	for (NativeUI::ElementInfo const& n : NativeUI::Elements())
	{
		Element e;
		e.id        = id++;
		e.kind      = "native";
		e.nativeId  = n.id;
		e.rect      = { n.x, n.y, n.w, n.h };
		e.label     = n.label;
		e.text      = n.text;
		e.help      = n.help;
		e.name      = n.role;
		e.enabled   = n.enabled;
		e.clickable = n.clickable;
		e.focused   = n.focused;
		e.priority  = 1000;
		out.push_back(std::move(e));
	}
	bool const nativeOnTop = NativeUI::CapturesMouse();
	MSYS_ForEachRegion([&](MOUSE_REGION const& r) {
		Element e;
		e.id       = id++;
		e.rect     = { r.RegionTopLeftX, r.RegionTopLeftY,
		               r.RegionBottomRightX - r.RegionTopLeftX, r.RegionBottomRightY - r.RegionTopLeftY };
		e.priority = r.PriorityLevel;
		e.enabled  = (r.uiFlags & MSYS_REGION_ENABLED) != 0;
		e.help     = ToStd(r.FastHelpText);
		// Help texts mark hotkey letters with '|' ("|Options"); those are
		// formatting, not text.
		std::erase(e.help, '|');
		if (r.Name) e.name = r.Name;

		auto const b = buttons.find(&r);
		if (b != buttons.end())
		{
			e.kind = "button";
			e.text = ToStd(b->second->codepoints);
			e.enabled = e.enabled && b->second->Enabled();
		}
		else
		{
			e.kind = "region";
		}

		auto const c = e.Center();
		e.clickable = e.enabled && !nativeOnTop && MSYS_RegionAt(static_cast<INT16>(c.x), static_cast<INT16>(c.y)) == &r;

		e.label = !e.name.empty() ? e.name : e.text;
		// Backdrops (modal blockers, screen-wide click catchers) contain all
		// the text on screen; that is not their caption.
		SDL_Surface const* frame = GetScreenBuffer();
		bool const widgetSized = frame && e.rect.w * e.rect.h * 4 <= frame->w * frame->h;
		if (e.label.empty() && widgetSized)
		{
			// Most JA2 widgets are images with the caption drawn on top.
			for (auto const& t : texts)
			{
				SDL_Point const tc{ t.rect.x + t.rect.w / 2, t.rect.y + t.rect.h / 2 };
				if (!Contains(e.rect, tc)) continue;
				if (!e.label.empty()) e.label += ' ';
				e.label += t.text.to_std_string();
			}
		}
		bool const unnamed = e.label.empty() && e.help.empty();
		if (unnamed && widgetSized)
		{
			// Icons and picture links: the caption is printed right below.
			int best = MAX_CAPTION_GAP + 1;
			for (auto const& t : texts)
			{
				int const cx  = t.rect.x + t.rect.w / 2;
				int const gap = t.rect.y - (e.rect.y + e.rect.h);
				if (cx < e.rect.x || cx > e.rect.x + e.rect.w || gap < 0 || gap >= best) continue;
				best = gap;
				e.label = t.text.to_std_string();
				e.labelFrom = t.rect;
			}
		}
		if (unnamed && e.label.empty() && e.rect.w <= 64 && e.rect.h <= 64)
		{
			// Check boxes and radio buttons: the caption is printed next to
			// the control on the same row, like an HTML <label>.
			int best = MAX_LABEL_GAP + 1;
			for (auto const& t : texts)
			{
				int const cy = t.rect.y + t.rect.h / 2;
				if (cy < e.rect.y || cy > e.rect.y + e.rect.h) continue;
				int const gap = t.rect.x + t.rect.w <= e.rect.x ? e.rect.x - (t.rect.x + t.rect.w)
				              : t.rect.x >= e.rect.x + e.rect.w ? t.rect.x - (e.rect.x + e.rect.w)
				              : -1;
				if (gap < 0 || gap >= best) continue;
				best = gap;
				e.label = t.text.to_std_string();
				e.labelFrom = t.rect;
			}
		}
		if (e.label.empty()) e.label = e.help;
		out.push_back(std::move(e));
	});
	return out;
}

std::optional<Target> Find(Locator const& loc)
{
	if (loc.point) return Target{ *loc.point, loc.Describe(), std::nullopt };
	if (!loc.id.empty())
	{
		// by id: native elements, exact
		for (Element const& e : Elements())
		{
			if (e.kind != "native" || e.nativeId != loc.id) continue;
			if (loc.within && !Contains(*loc.within, e.Center())) continue;
			std::string const desc = ST::format("native #{} \"{}\" at ({}, {}, {}x{})",
				e.nativeId, e.label, e.rect.x, e.rect.y, e.rect.w, e.rect.h).to_std_string();
			return Target{ e.Center(), desc, e };
		}
		return std::nullopt;
	}

	std::string const want = Normalize(loc.text);
	if (want.empty()) throw std::invalid_argument("a locator needs text or coordinates");

	struct Candidate { Target target; bool exactMatch; SDL_Rect rect; };
	std::vector<Candidate> found;

	auto matches = [&](std::string const& s, bool& exactMatch) {
		std::string const n = Normalize(s);
		if (n.empty()) return false;
		exactMatch = n == want;
		return exactMatch || (!loc.exact && n.find(want) != std::string::npos);
	};
	auto inside = [&](SDL_Rect const& r) {
		return !loc.within || Contains(*loc.within, SDL_Point{ r.x + r.w / 2, r.y + r.h / 2 });
	};

	auto const elements = Elements();
	for (Element const& e : elements)
	{
		if (!e.clickable || !inside(e.rect)) continue;
		if (e.kind == "native" && e.label.empty()) continue;
		bool exactMatch = false;
		if (matches(e.label, exactMatch) || matches(e.name, exactMatch) || matches(e.text, exactMatch) || matches(e.help, exactMatch))
		{
			std::string const desc = ST::format("{} \"{}\" at ({}, {}, {}x{})",
				e.kind, e.label, e.rect.x, e.rect.y, e.rect.w, e.rect.h).to_std_string();
			found.push_back({ Target{ e.Center(), desc, e }, exactMatch, e.rect });
		}
	}

	// Plain text on screen, unless it is just the caption of an element found above.
	for (auto const& t : Texts())
	{
		if (!inside(t.rect)) continue;
		bool exactMatch = false;
		if (!matches(t.text.to_std_string(), exactMatch)) continue;
		SDL_Point const c{ t.rect.x + t.rect.w / 2, t.rect.y + t.rect.h / 2 };
		bool const covered = std::any_of(found.begin(), found.end(), [&](Candidate const& f) {
			if (!f.target.element) return false;
			auto const& from = f.target.element->labelFrom;
			return Contains(f.rect, c) || (from && SDL_RectsEqual(&*from, &t.rect));
		});
		if (covered) continue;
		std::string const desc = ST::format("text \"{}\" at ({}, {}, {}x{})",
			t.text, t.rect.x, t.rect.y, t.rect.w, t.rect.h).to_std_string();
		found.push_back({ Target{ c, desc, std::nullopt }, exactMatch, t.rect });
	}

	// Whole-string matches first, then reading order.
	std::stable_sort(found.begin(), found.end(), [](Candidate const& a, Candidate const& b) {
		if (a.exactMatch != b.exactMatch) return a.exactMatch;
		if (a.rect.y != b.rect.y) return a.rect.y < b.rect.y;
		return a.rect.x < b.rect.x;
	});
	if (loc.index < 1 || static_cast<size_t>(loc.index) > found.size()) return std::nullopt;
	return found[loc.index - 1].target;
}

std::vector<Element> OffscreenElements()
{
	SDL_Surface const* frame = GetScreenBuffer();
	int const sw = frame ? frame->w : 0;
	int const sh = frame ? frame->h : 0;
	std::vector<Element> bad;
	for (Element const& e : Elements())
	{
		// Degenerate regions (zero-sized placeholders) cannot be clicked.
		if (e.rect.w <= 0 || e.rect.h <= 0) continue;
		if (e.kind == "native") continue; // NativeUI::LayoutAudit checks those
		if (e.rect.x < 0 || e.rect.y < 0 || e.rect.x + e.rect.w > sw || e.rect.y + e.rect.h > sh)
		{
			bad.push_back(e);
		}
	}
	return bad;
}

std::vector<std::string> LayoutProblems()
{
	std::vector<std::string> out;
	SDL_Surface const* frame = GetScreenBuffer();
	for (Element const& e : OffscreenElements())
	{
		if (e.kind == "native") continue; // the native audit below covers them
		out.push_back(ST::format("{} \"{}\" at ({}, {}, {}x{}) lies outside the {}x{} screen", e.kind, e.label,
			e.rect.x, e.rect.y, e.rect.w, e.rect.h, frame ? frame->w : 0, frame ? frame->h : 0).to_std_string());
	}
	for (std::string const& p : NativeUI::LayoutAudit()) out.push_back("native UI: " + p);
	return out;
}

Target Resolve(Locator const& loc, unsigned const timeoutMs)
{
	std::optional<Target> t;
	WaitUntil([&] {
		t = Find(loc);
		return t && (!t->element || t->element->enabled);
	}, timeoutMs, loc.Describe());
	return *t;
}


bool IsIdle()
{
	return g_quiet && Frame() - g_quietSinceFrame >= IDLE_FRAMES;
}

void WaitIdle(unsigned const timeoutMs)
{
	TrackIdle();
	WaitUntil(IsIdle, timeoutMs, "the game to become idle");
}

uint32_t Pixel(int const x, int const y)
{
	SDL_Surface const* s = FrameSurface();
	if (x < 0 || y < 0 || x >= s->w || y >= s->h)
	{
		throw std::out_of_range(ST::format("pixel ({}, {}) is outside the {}x{} screen", x, y, s->w, s->h).to_std_string());
	}
	if (VideoIsLayered()) return VideoComposePixel(x, y);
	auto const* row = static_cast<UINT8 const*>(s->pixels) + y * s->pitch;
	return Rgb565To888(reinterpret_cast<UINT16 const*>(row)[x]);
}

std::string ResolveOutputPath(std::string const& path)
{
	std::filesystem::path p{ path };
	if (p.is_relative() && !GetOptions().outDir.empty()) p = std::filesystem::path{ GetOptions().outDir } / p;
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
	return p.string();
}

void Screenshot(std::string const& path)
{
	if (VideoGetOutputMapping().gpu)
	{
		// The native UI is drawn by the GPU (a window with JA2_NATIVE_UI_RENDERER=gpu): read the window back
		std::vector<uint8_t> out;
		int ow = 0, oh = 0;
		VideoRequestOutputCapture();
		NativeUI::CaptureFrame();
		RefreshScreen();
		if (VideoTakeOutputCapture(out, ow, oh))
		{
			if (!stbi_write_png(path.c_str(), ow, oh, 3, out.data(), ow * 3)) throw std::runtime_error("could not write screenshot to " + path);
			return;
		}
	}
	{
		// With layers the picture is the UI over the world, at the size of the window. Only the rendering runs, no
		// game loop: a driven command can change the state while the game is idle (a paused door menu, a view model)
		// and the picture must be of the game as it is now — while a one-frame draw (a loading screen) is captured
		// where it is (it is gone with the next frame, docs/ui/loadingscreen.md).
		NativeUI::CaptureFrame();
		RefreshScreen();
		std::vector<uint8_t> composed;
		int cw = 0, ch = 0;
		if (VideoComposeFrame(composed, cw, ch))
		{
			if (!stbi_write_png(path.c_str(), cw, ch, 3, composed.data(), cw * 3))
			{
				throw std::runtime_error("could not write screenshot to " + path);
			}
			return;
		}
	}
	SDL_Surface const* s = FrameSurface();
	std::vector<uint8_t> rgb(size_t(s->w) * s->h * 3);
	for (int y = 0; y < s->h; ++y)
	{
		auto const* row = reinterpret_cast<UINT16 const*>(static_cast<UINT8 const*>(s->pixels) + y * s->pitch);
		for (int x = 0; x < s->w; ++x)
		{
			uint32_t const c = Rgb565To888(row[x]);
			uint8_t* d = &rgb[(size_t(y) * s->w + x) * 3];
			d[0] = c >> 16; d[1] = (c >> 8) & 0xff; d[2] = c & 0xff;
		}
	}
	if (!stbi_write_png(path.c_str(), s->w, s->h, 3, rgb.data(), s->w * 3))
	{
		throw std::runtime_error("could not write screenshot to " + path);
	}
}


std::vector<std::string> Saves()
{
	std::vector<std::string> out;
	for (auto const& n : GetLoadableSaveNames()) out.push_back(n.to_std_string());
	return out;
}

void Load(std::string const& save, unsigned const timeoutMs)
{
	auto const saves = Saves();
	if (std::find(saves.begin(), saves.end(), save) == saves.end())
	{
		std::string list;
		for (auto const& s : saves) list += (list.empty() ? "" : ", ") + s;
		throw std::invalid_argument("no save named \"" + save + "\" (available: " + (list.empty() ? "none" : list) + ")");
	}

	UINT32 const loadsBefore = guiSavedGameLoadCount;
	bool const inGame = guiCurrentScreen == MAP_SCREEN || guiCurrentScreen == GAME_SCREEN;
	if (!inGame)
	{
		// Go through the main menu's "continue saved game" path.
		WaitUntil([] { return guiCurrentScreen == MAINMENU_SCREEN && IsIdle(); }, timeoutMs, "the main menu");
		if (!DoLoadSavedGameByName(save)) throw std::runtime_error("could not load " + save);
		Key("alt+c");
	}
	else if (!DoLoadSavedGameByName(save))
	{
		throw std::runtime_error("could not load " + save);
	}

	WaitUntil([&] {
		return guiSavedGameLoadCount > loadsBefore &&
			(guiCurrentScreen == MAP_SCREEN || guiCurrentScreen == GAME_SCREEN) && IsIdle();
	}, timeoutMs, "save \"" + save + "\" to load");
}

void Save(std::string const& name, std::string const& description)
{
	if (guiCurrentScreen != MAP_SCREEN && guiCurrentScreen != GAME_SCREEN)
	{
		throw std::runtime_error("can only save from the map screen or tactical, not " + ScreenName());
	}
	// The save records which screen to return to after loading; like the
	// quick save, that is the screen we are saving from.
	guiPreviousOptionScreen = guiCurrentScreen;
	if (!SaveGame(name, description)) throw std::runtime_error("saving \"" + name + "\" failed");
}

void Quit()
{
	requestGameExit();
	try
	{
		for (int i = 0; i < 10; ++i) Step();
	}
	catch (GameExitedError const&)
	{
	}
}

}

}
