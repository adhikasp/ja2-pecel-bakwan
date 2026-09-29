#include "AutomationLua.h"
#include "Automation.h"
#include "AutomationSession.h"

#include "Assignments.h"
#include "Font_Control.h"
#include "Game_Clock.h"
#include "Input.h"
#include "Isometric_Utils.h"
#include "UILayout.h"
#include "VideoOptionsScreen.h"
#include "GameLoop.h"
#include "JAScreens.h"
#include "Soldier_Find.h"
#include "WorldDef.h"
#include "Laptop.h"
#include "LaptopSave.h"
#include "Logger.h"
#include "MessageBoxScreen.h"
#include "Overhead.h"
#include "Soldier_Control.h"
#include "Soldier_Profile.h"
#include "Dialogue_Control.h"
#include "Message.h"
#include "Strategic_Exit_GUI.h"
#include "Strategic_Movement.h"
#include "PreBattle_Interface.h"
#include "Auto_Resolve.h"
#include "Animated_ProgressBar.h"
#include "Loading_Screen.h"
#include "Campaign_Types.h"
#include "Strategic_Movement.h"
#include "Tactical_Placement_GUI.h"
#include "Overhead_Types.h"
#include "StrategicMap.h"
#include "Text.h"

#include <string_theory/format>

#include <cstdio>
#include <cstring>
#include <sstream>

namespace Automation
{

namespace
{
	sol::state  g_lua;
	FailureKind g_lastFailure = FailureKind::None;
	int         g_checkFailures = 0;

	constexpr unsigned DEFAULT_TIMEOUT_MS = 10'000;
	constexpr unsigned LONG_TIMEOUT_MS    = 120'000;

	/* Run an API call, remembering what kind of failure (if any) it raised so
	 * that the runner can pick the right exit code. The exception itself is
	 * turned into a Lua error by sol2. */
	template<typename F>
	auto Guarded(F&& f)
	{
		g_lastFailure = FailureKind::None;
		try
		{
			return f();
		}
		catch (TimeoutError const&)      { g_lastFailure = FailureKind::Timeout;     throw; }
		catch (GameCrashedError const&)  { g_lastFailure = FailureKind::Crash;       throw; }
		catch (GameExitedError const&)   { g_lastFailure = FailureKind::Exited;      throw; }
		catch (ExpectationError const&)  { g_lastFailure = FailureKind::Expectation; throw; }
		catch (std::exception const&)    { g_lastFailure = FailureKind::Script;      throw; }
	}

	unsigned Timeout(sol::optional<unsigned> const t, unsigned const def)
	{
		return t ? *t : def;
	}

	SDL_Rect RectFrom(sol::table const& t)
	{
		return { t.get_or("x", 0), t.get_or("y", 0), t.get_or("w", 0), t.get_or("h", 0) };
	}

	/* A locator is either a string (text to look for), or a table with
	 * text/exact/index/within, or x/y coordinates. */
	Locator LocatorFrom(sol::object const& o)
	{
		Locator loc;
		if (o.is<std::string>())
		{
			loc.text = o.as<std::string>();
		}
		else if (o.is<sol::table>())
		{
			sol::table t = o.as<sol::table>();
			loc.text  = t.get_or<std::string>("text", "");
			loc.exact = t.get_or("exact", false);
			loc.index = t.get_or("index", 1);
			sol::optional<int> x = t["x"], y = t["y"];
			if (x && y) loc.point = SDL_Point{ *x, *y };
			sol::optional<sol::table> within = t["within"];
			if (within) loc.within = RectFrom(*within);
		}
		else
		{
			throw std::invalid_argument("expected a string or a table locator");
		}
		return loc;
	}

	sol::table ElementTable(Element const& e)
	{
		sol::table t = g_lua.create_table();
		t["id"] = e.id;
		t["kind"] = e.kind;
		t["x"] = e.rect.x; t["y"] = e.rect.y; t["w"] = e.rect.w; t["h"] = e.rect.h;
		t["label"] = e.label;
		if (!e.text.empty()) t["text"] = e.text;
		if (!e.name.empty()) t["name"] = e.name;
		if (!e.help.empty()) t["help"] = e.help;
		t["enabled"] = e.enabled;
		t["clickable"] = e.clickable;
		return t;
	}

	sol::table TargetTable(Target const& target)
	{
		sol::table t = target.element ? ElementTable(*target.element) : g_lua.create_table();
		t["cx"] = target.point.x;
		t["cy"] = target.point.y;
		t["description"] = target.description;
		return t;
	}

	/* ja2.click("Load Game") / ja2.click(100, 200) / ja2.click{text=..., index=2}
	 * with an optional trailing options table {button=, count=, timeout=}. */
	sol::table PointerAction(sol::variadic_args va, int button, int count, bool press)
	{
		if (va.size() == 0) throw std::invalid_argument("expected a locator or x, y");
		Locator loc;
		size_t next = 1;
		if (va[0].is<int>() && va.size() >= 2 && va[1].is<int>())
		{
			loc.point = SDL_Point{ va[0].as<int>(), va[1].as<int>() };
			next = 2;
		}
		else
		{
			loc = LocatorFrom(va[0]);
		}
		unsigned timeout = DEFAULT_TIMEOUT_MS;
		if (va.size() > next && va[next].is<sol::table>())
		{
			sol::table opts = va[next].as<sol::table>();
			std::string const b = opts.get_or<std::string>("button", "");
			if (b == "right") button = 2;
			else if (b == "middle") button = 3;
			count   = opts.get_or("count", count);
			timeout = opts.get_or("timeout", timeout);
		}
		Target const target = Session::Resolve(loc, timeout);
		SLOGI("[automation] {} {}", press ? "click" : "hover", target.description);
		if (press) Session::Click(target.point.x, target.point.y, button, count);
		else       Session::MouseMove(target.point.x, target.point.y);
		return TargetTable(target);
	}

	std::string HexColor(uint32_t const c)
	{
		char buf[8];
		std::snprintf(buf, sizeof(buf), "#%06x", c & 0xffffff);
		return buf;
	}

	uint32_t ParseColor(std::string const& s)
	{
		if (s.size() == 7 && s[0] == '#') return std::stoul(s.substr(1), nullptr, 16);
		if (s.size() == 4 && s[0] == '#')
		{
			uint32_t const v = std::stoul(s.substr(1), nullptr, 16);
			return ((v >> 8 & 0xf) * 17) << 16 | ((v >> 4 & 0xf) * 17) << 8 | (v & 0xf) * 17;
		}
		throw std::invalid_argument("expected a colour like #rrggbb, got " + s);
	}

	bool ColorNear(uint32_t const a, uint32_t const b, int const tolerance)
	{
		for (int shift = 0; shift <= 16; shift += 8)
		{
			if (std::abs(int(a >> shift & 0xff) - int(b >> shift & 0xff)) > tolerance) return false;
		}
		return true;
	}

	// The tile under screen point (x, y), as the mouse code would see it, or -1.
	int GridUnder(int const x, int const y)
	{
		if (y >= gsVIEWPORT_WINDOW_END_Y) return -1; // the interface panel, not the map
		INT16 wx, wy;
		if (!GetWorldCoordsAtScreenPos(static_cast<INT16>(x), static_cast<INT16>(y), &wx, &wy)) return -1;
		int const grid = MAPROWCOLTOPOS(wy / CELL_Y_SIZE, wx / CELL_X_SIZE);
		return grid == 0xffff ? -1 : grid;
	}

	// Where to click to hit tile @a grid: the centre of the screen area that
	// maps to it. GetGridNoScreenPos() is tuned for drawing sprites and can
	// be a tile off, so search around it with the mouse code's own mapping.
	std::optional<SDL_Point> GridClickPos(int const grid, int const level)
	{
		INT16 sx, sy;
		GetGridNoScreenPos(static_cast<INT16>(grid), static_cast<UINT8>(level), &sx, &sy);
		// that is in world pixels; clicks are in UI pixels (the same without layers)
		LayerPoint const ui = g_ui.worldToUi(sx, sy);
		sx = static_cast<INT16>(ui.x);
		sy = static_cast<INT16>(ui.y);
		int const stepX = g_ui.isLayered() ? 1 : 4;
		int const stepY = g_ui.isLayered() ? 1 : 2;
		long sumX = 0, sumY = 0, n = 0;
		for (int dy = -30; dy <= 30; dy += stepY)
		{
			for (int dx = -60; dx <= 60; dx += stepX)
			{
				if (GridUnder(sx + dx, sy + dy) != grid) continue;
				sumX += sx + dx;
				sumY += sy + dy;
				++n;
			}
		}
		if (n == 0) return std::nullopt;
		return SDL_Point{ int(sumX / n), int(sumY / n) };
	}

	sol::table GameState()
	{
		sol::state& L = g_lua;
		sol::table s = L.create_table();
		s["screen"]     = Session::ScreenName();
		s["frame"]      = Session::Frame();
		s["ms"]         = Session::ElapsedMs();
		s["idle"]       = Session::IsIdle();
		s["messageBox"] = gfInMsgBox;
		if (gfInMsgBox) s["messageBoxText"] = Session::MessageBoxText();

		sol::table time = L.create_table();
		time["day"]    = GetWorldDay();
		time["hour"]   = GetWorldHour();
		time["minute"] = GetWorldMinutesInDay() % 60;
		time["totalMinutes"] = GetWorldTotalMin();
		time["totalSeconds"] = GetWorldTotalSeconds();
		time["compressed"]   = static_cast<bool>(IsTimeBeingCompressed());
		time["paused"] = static_cast<bool>(GamePaused() || gfPauseDueToPlayerGamePause);
		s["time"] = time;

		s["money"]      = LaptopSaveInfo.iCurrentBalance;
		s["laptopMode"] = static_cast<int>(guiCurrentLaptopMode);
		s["sector"]     = gWorldSector.AsShortString().to_std_string();

		sol::table tactical = L.create_table();
		tactical["inCombat"]    = (gTacticalStatus.uiFlags & INCOMBAT) != 0;
		tactical["currentTeam"] = gTacticalStatus.ubCurrentTeam;
		tactical["enemyInSector"] = static_cast<bool>(gTacticalStatus.fEnemyInSector);
		s["tactical"] = tactical;

		sol::table mercs = L.create_table();
		int i = 1;
		CFOR_EACH_IN_TEAM(m, OUR_TEAM)
		{
			sol::table t = L.create_table();
			t["name"]       = m->name.to_std_string();
			t["profile"]    = static_cast<int>(m->ubProfile);
			t["sector"]     = m->sSector.AsShortString().to_std_string();
			t["assignment"] = static_cast<int>(m->bAssignment);
			if (m->bAssignment >= 0 && m->bAssignment <= ASSIGNMENT_EMPTY)
			{
				t["assignmentName"] = pAssignmentStrings[m->bAssignment].to_std_string();
			}
			t["life"]     = static_cast<int>(m->bLife);
			t["lifeMax"]  = static_cast<int>(m->bLifeMax);
			t["inSector"] = m->bInSector != 0;
			t["gridNo"]   = m->sGridNo;
			if (guiCurrentScreen == GAME_SCREEN && m->bInSector && m->sGridNo != NOWHERE)
			{
				// Where to click on this merc (the tile they stand on).
				if (auto const p = GridClickPos(m->sGridNo, m->bLevel))
				{
					t["screenX"] = p->x;
					t["screenY"] = p->y;
				}
			}
			mercs[i++] = t;
		}
		s["mercs"] = mercs;
		return s;
	}

	// Turn the enemies standing in the loaded sector into a strategic encounter, as if we had walked into them.
	void FakeEncounter()
	{
		if (!gWorldSector.IsValid()) throw std::runtime_error("no sector is loaded");
		SECTORINFO& si = SectorInfo[gWorldSector.AsByte()];
		si.ubNumAdmins = si.ubNumTroops = si.ubNumElites = 0;
		FOR_EACH_IN_TEAM(e, ENEMY_TEAM)
		{
			if (!e->bInSector || e->bLife == 0) continue;
			if      (e->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR) ++si.ubNumAdmins;
			else if (e->ubSoldierClass == SOLDIER_CLASS_ELITE)         ++si.ubNumElites;
			else                                                       ++si.ubNumTroops;
		}
		gubPBSector = gWorldSector;
		gubEnemyEncounterCode = ENEMY_ENCOUNTER_CODE;
	}

	void RegisterApi(sol::table ja2)
	{
		sol::state& L = g_lua;
		Options const& opt = GetOptions();

		sol::table args = L.create_table();
		for (size_t i = 0; i < opt.scriptArgs.size(); ++i) args[i + 1] = opt.scriptArgs[i];
		ja2["args"]     = args;
		ja2["headless"] = opt.Headless();

		// --- time ---
		ja2.set_function("step", [](sol::optional<unsigned> frames) {
			return Guarded([&] { Session::Step(frames.value_or(1)); return Session::Frame(); });
		});
		ja2.set_function("wait", [](unsigned ms) { Guarded([&] { Session::Wait(ms); }); });
		ja2.set_function("frame", [] { return Session::Frame(); });
		ja2.set_function("time", [] { return Session::ElapsedMs(); });
		ja2.set_function("waitUntil", [](sol::protected_function pred, sol::optional<unsigned> timeout, sol::optional<std::string> what) {
			Guarded([&] {
				Session::WaitUntil([&] {
					sol::protected_function_result r = pred();
					if (!r.valid())
					{
						sol::error err = r;
						throw std::runtime_error(err.what());
					}
					return r.get<sol::object>().is<bool>() ? r.get<bool>() : r.get<sol::object>() != sol::lua_nil;
				}, Timeout(timeout, DEFAULT_TIMEOUT_MS), what.value_or("condition"));
			});
		});
		ja2.set_function("waitIdle", [](sol::optional<unsigned> timeout) {
			Guarded([&] { Session::WaitIdle(Timeout(timeout, LONG_TIMEOUT_MS)); });
		});
		ja2.set_function("waitScreen", [](std::string const& screen, sol::optional<unsigned> timeout) {
			Guarded([&] {
				Session::WaitUntil([&] { return Session::ScreenName() == screen && Session::IsIdle(); },
					Timeout(timeout, LONG_TIMEOUT_MS), "screen " + screen);
			});
		});
		ja2.set_function("waitFor", [](sol::object loc, sol::optional<unsigned> timeout) {
			return Guarded([&] { return TargetTable(Session::Resolve(LocatorFrom(loc), Timeout(timeout, DEFAULT_TIMEOUT_MS))); });
		});
		ja2.set_function("waitGone", [](sol::object loc, sol::optional<unsigned> timeout) {
			Guarded([&] {
				Locator const l = LocatorFrom(loc);
				Session::WaitUntil([&] { return !Session::Find(l); }, Timeout(timeout, DEFAULT_TIMEOUT_MS), l.Describe() + " to disappear");
			});
		});
		ja2.set_function("waitPixel", [](int x, int y, std::string const& color, sol::optional<int> tolerance, sol::optional<unsigned> timeout) {
			Guarded([&] {
				uint32_t const want = ParseColor(color);
				Session::WaitUntil([&] { return ColorNear(Session::Pixel(x, y), want, tolerance.value_or(8)); },
					Timeout(timeout, LONG_TIMEOUT_MS), ST::format("pixel ({}, {}) to be {}", x, y, color).to_std_string());
			});
		});
		ja2.set_function("waitStable", [](int x, int y, sol::optional<unsigned> timeout) {
			Guarded([&] {
				uint32_t last = Session::Pixel(x, y);
				int same = 0;
				Session::WaitUntil([&] {
					uint32_t const now = Session::Pixel(x, y);
					same = now == last ? same + 1 : 0;
					last = now;
					return same >= 3;
				}, Timeout(timeout, DEFAULT_TIMEOUT_MS), ST::format("pixel ({}, {}) to stop changing", x, y).to_std_string());
			});
		});

		// --- input ---
		ja2.set_function("click", [](sol::variadic_args va) { return Guarded([&] { return PointerAction(va, 1, 1, true); }); });
		ja2.set_function("rclick", [](sol::variadic_args va) { return Guarded([&] { return PointerAction(va, 2, 1, true); }); });
		ja2.set_function("dblclick", [](sol::variadic_args va) { return Guarded([&] { return PointerAction(va, 1, 2, true); }); });
		ja2.set_function("hover", [](sol::variadic_args va) { return Guarded([&] { return PointerAction(va, 1, 1, false); }); });
		ja2.set_function("move", [](int x, int y) { Guarded([&] { Session::MouseMove(x, y); }); });
		ja2.set_function("mouse", [] { auto const p = Session::MousePos(); return std::make_tuple(p.x, p.y); });
		ja2.set_function("mousedown", [](sol::optional<std::string> b) {
			Guarded([&] { Session::MouseButton(b.value_or("left") == "right" ? 2 : 1, true); });
		});
		ja2.set_function("mouseup", [](sol::optional<std::string> b) {
			Guarded([&] { Session::MouseButton(b.value_or("left") == "right" ? 2 : 1, false); });
		});
		ja2.set_function("drag", [](int x0, int y0, int x1, int y1) { Guarded([&] { Session::Drag(x0, y0, x1, y1); }); });
		ja2.set_function("wheel", [](int dy, sol::optional<int> x, sol::optional<int> y) {
			Guarded([&] {
				auto const p = Session::MousePos();
				Session::Wheel(x.value_or(p.x), y.value_or(p.y), dy);
			});
		});
		ja2.set_function("key", [](std::string const& combo, sol::optional<int> times) {
			Guarded([&] { for (int i = 0; i < times.value_or(1); ++i) Session::Key(combo); });
		});
		ja2.set_function("keydown", [](std::string const& k) { Guarded([&] { Session::KeyDown(k); }); });
		ja2.set_function("keyup", [](std::string const& k) { Guarded([&] { Session::KeyUp(k); }); });
		ja2.set_function("type", [](std::string const& text) { Guarded([&] { Session::Type(text); }); });

		// --- looking ---
		ja2.set_function("screen", [] { return Session::ScreenName(); });
		ja2.set_function("idle", [] { return Session::IsIdle(); });
		ja2.set_function("find", [](sol::object loc) -> sol::object {
			return Guarded([&]() -> sol::object {
				auto const t = Session::Find(LocatorFrom(loc));
				if (!t) return sol::lua_nil;
				return TargetTable(*t);
			});
		});
		ja2.set_function("exists", [](sol::object loc) {
			return Guarded([&] { return Session::Find(LocatorFrom(loc)).has_value(); });
		});
		ja2.set_function("ui", [](sol::optional<sol::table> opts) {
			return Guarded([&] {
				bool const all = opts && opts->get_or("all", false);
				sol::table list = g_lua.create_table();
				int i = 1;
				for (Element const& e : Session::Elements())
				{
					// By default: labelled things a user could click, plus
					// greyed-out buttons (worth knowing they exist).
					bool const shown = e.clickable || (e.kind == "button" && !e.enabled);
					if (!all && (!shown || e.label.empty())) continue;
					list[i++] = ElementTable(e);
				}
				return list;
			});
		});
		ja2.set_function("texts", [] {
			return Guarded([&] {
				sol::table list = g_lua.create_table();
				int i = 1;
				for (auto const& t : Session::Texts())
				{
					sol::table e = g_lua.create_table();
					e["text"] = t.text.to_std_string();
					e["x"] = t.rect.x; e["y"] = t.rect.y; e["w"] = t.rect.w; e["h"] = t.rect.h;
					list[i++] = e;
				}
				return list;
			});
		});
		ja2.set_function("pixel", [](int x, int y) { return Guarded([&] { return HexColor(Session::Pixel(x, y)); }); });
		ja2.set_function("pixelIs", [](int x, int y, std::string const& color, sol::optional<int> tolerance) {
			return Guarded([&] { return ColorNear(Session::Pixel(x, y), ParseColor(color), tolerance.value_or(8)); });
		});
		ja2.set_function("screenshot", [](std::string const& path) {
			return Guarded([&] {
				std::string const out = Session::ResolveOutputPath(path);
				Session::Screenshot(out);
				SLOGI("[automation] screenshot {}", out);
				return out;
			});
		});
		ja2.set_function("state", [] { return Guarded([] { return GameState(); }); });
		// {w, h, stdX, stdY}: the screen size and where the classic 640x480 area starts in it.
		ja2.set_function("screenSize", [] {
			sol::table t = g_lua.create_table();
			t["w"] = SCREEN_WIDTH; t["h"] = SCREEN_HEIGHT;
			t["stdX"] = STD_SCREEN_X; t["stdY"] = STD_SCREEN_Y;
			return t;
		});

		// Changes the video settings while the game runs, like the Video options do; the current screen is built again
		// for the new layout. Fields (all optional, the rest stays): res = "1280x720" (the window, "auto" = the desktop),
		// uiscale = 0 (auto) .. 4, worldzoom = 0 (same as the UI) .. 4, window = "windowed" | "borderless" | "fullscreen",
		// filter = "linear" | "sharp" | "pixel". Headless sessions have no window: res is the canvas, and the UI scale only
		// applies when the world is a layer of its own (worldzoom differs from uiscale). Returns the screenSize() table
		// plus uiScale, worldZoom and layered. Nothing is written to ja2.json.
		ja2.set_function("setVideo", [](sol::table t) {
			return Guarded([&] {
				auto want = VideoGetDisplaySettings();
				auto quality = VideoGetScaleQuality();
				if (sol::optional<std::string> res = t["res"])
				{
					int w = 0, h = 0;
					if (*res == "auto") want.resX = want.resY = 0;
					else if (std::sscanf(res->c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { want.resX = w; want.resY = h; }
					else throw std::runtime_error("ja2.setVideo: res must be \"WIDTHxHEIGHT\" or \"auto\"");
				}
				if (sol::optional<int> v = t["uiscale"]) want.uiScale = *v;
				if (sol::optional<int> v = t["worldzoom"]) want.worldZoom = *v;
				if (sol::optional<std::string> m = t["window"])
				{
					if      (*m == "windowed")   want.windowMode = WindowMode::Windowed;
					else if (*m == "borderless") want.windowMode = WindowMode::BorderlessDesktop;
					else if (*m == "fullscreen") want.windowMode = WindowMode::Fullscreen;
					else throw std::runtime_error("ja2.setVideo: window must be windowed, borderless or fullscreen");
				}
				if (sol::optional<std::string> f = t["filter"])
				{
					if      (*f == "linear") quality = VideoScaleQuality::LINEAR;
					else if (*f == "sharp")  quality = VideoScaleQuality::NEAR_PERFECT;
					else if (*f == "pixel")  quality = VideoScaleQuality::PERFECT;
					else throw std::runtime_error("ja2.setVideo: filter must be linear, sharp or pixel");
				}
				while (guiPendingScreen != NO_PENDING_SCREEN) Session::Step(1); // let a screen change finish
				ST::string error;
				if (!ChangeVideoSettings(want, quality, false, &error))
				{
					throw std::runtime_error(("ja2.setVideo: " + error).to_std_string());
				}
				Session::Step(3); // the screen builds itself again
				sol::table r = g_lua.create_table();
				r["w"] = SCREEN_WIDTH; r["h"] = SCREEN_HEIGHT;
				r["stdX"] = STD_SCREEN_X; r["stdY"] = STD_SCREEN_Y;
				r["uiScale"] = int(g_ui.m_uiScale); r["worldZoom"] = int(g_ui.m_worldZoom);
				r["layered"] = VideoIsLayered();
				return r;
			});
		});

		// --- tactical map ---
		ja2.set_function("gridPos", [](int grid, sol::optional<int> level) {
			return Guarded([&] {
				if (grid < 0 || grid >= WORLD_MAX) throw std::out_of_range("no such grid number");
				auto const p = GridClickPos(grid, level.value_or(0));
				if (!p) throw std::runtime_error(ST::format("tile {} is not on screen", grid).to_std_string());
				return std::make_tuple(p->x, p->y);
			});
		});
		ja2.set_function("gridAt", [](int x, int y) {
			return Guarded([&] {
				return GridUnder(x, y);
			});
		});

		// ja2.debug(what, [a]): open a piece of tactical UI directly, for layout tests that cannot
		// easily reach it through play. what = "exitmenu" (a = direction), "placement", "quote"
		// (a = quote number, spoken by the selected merc), "message" (a = text), "msgbox" (a = text),
		// "loadscreen" (a = id), "prebattle" and "autoresolve" (fake a fight in the current sector).
		ja2.set_function("debug", [](std::string const& what, sol::optional<sol::object> a) {
			Guarded([&] {
				if (what == "exitmenu")
				{
					InitSectorExitMenu(a && a->is<int>() ? a->as<int>() : NORTH, 0);
				}
				else if (what == "placement")
				{
					// The GUI reads the battle group's sector: fake one in the current sector.
					static GROUP dummy;
					dummy.ubSector = gWorldSector;
					gpBattleGroup = &dummy;
					InitTacticalPlacementGUI();
				}
				else if (what == "quote")
				{
					SOLDIERTYPE const* const s = GetSelectedMan();
					if (!s) throw std::runtime_error("no selected merc");
					TacticalCharacterDialogue(s, a && a->is<int>() ? a->as<int>() : 0);
				}
				else if (what == "message")
				{
					ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_INTERFACE, ST::string(a && a->is<std::string>() ? a->as<std::string>() : "debug message"));
				}
				else if (what == "msgbox")
				{
					DoMessageBox(MSG_BOX_BASIC_STYLE, ST::string(a && a->is<std::string>() ? a->as<std::string>() : "Debug message box"), guiCurrentScreen, MSG_BOX_FLAG_OK);
				}
				else if (what == "loadscreen")
				{
					// A loading screen with its progress bar half full (a = loading screen id).
					DisplayLoadScreenWithID(a && a->is<int>() ? a->as<int>() : LOADINGSCREEN_DAYGENERIC);
					CreateLoadingScreenProgressBar();
					RenderProgressBar(0, 60);
				}
				else if (what == "prebattle")
				{
					// Fake an enemy encounter in the current sector on the map screen and open the pre-battle panel.
					FakeEncounter();
					InitPreBattleInterface(nullptr, false);
				}
				else if (what == "autoresolve")
				{
					// Fake an enemy encounter in the current sector and go straight into auto resolve.
					FakeEncounter();
					EnterAutoResolveMode(gubPBSector);
				}
				else
				{
					throw std::runtime_error(("ja2.debug: unknown target " + what).c_str());
				}
			});
		});

		// --- game ---
		ja2.set_function("saves", [] {
			sol::table list = g_lua.create_table();
			int i = 1;
			for (auto const& s : Session::Saves()) list[i++] = s;
			return list;
		});
		ja2.set_function("load", [](std::string const& save, sol::optional<unsigned> timeout) {
			Guarded([&] { Session::Load(save, Timeout(timeout, LONG_TIMEOUT_MS)); });
		});
		ja2.set_function("save", [](std::string const& name, sol::optional<std::string> desc) {
			Guarded([&] { Session::Save(name, desc.value_or(name)); });
		});
		ja2.set_function("quit", [] { Session::Quit(); });

		// --- test helpers ---
		ja2.set_function("log", [](sol::variadic_args va) {
			std::string msg;
			for (auto v : va) msg += (msg.empty() ? "" : " ") + g_lua["tostring"](v).get<std::string>();
			SLOGI("[script] {}", msg);
			std::fprintf(stderr, "%s\n", msg.c_str());
		});
		ja2.set_function("expect", [](sol::object cond, sol::optional<std::string> msg) {
			Guarded([&] {
				if (!cond.valid() || cond == sol::lua_nil || (cond.is<bool>() && !cond.as<bool>()))
				{
					throw ExpectationError("expectation failed: " + msg.value_or("(no message)"));
				}
			});
		});
		ja2.set_function("assertInsideScreen", [] {
			Guarded([&] {
				auto const bad = Session::OffscreenElements();
				if (bad.empty()) return;
				std::string msg = ST::format("{} mouse region(s)/button(s) lie outside the {}x{} screen:",
					bad.size(), SCREEN_WIDTH, SCREEN_HEIGHT).to_std_string();
				for (Element const& e : bad)
				{
					msg += ST::format("\n  {} \"{}\" at ({}, {}, {}x{})",
						e.kind, e.label, e.rect.x, e.rect.y, e.rect.w, e.rect.h).to_std_string();
				}
				throw ExpectationError(msg);
			});
		});
		ja2.set_function("check", [](sol::object cond, sol::optional<std::string> msg) {
			bool const ok = cond.valid() && cond != sol::lua_nil && !(cond.is<bool>() && !cond.as<bool>());
			if (!ok)
			{
				++g_checkFailures;
				SLOGE("[script] check failed: {}", msg.value_or("(no message)"));
				std::fprintf(stderr, "check failed: %s\n", msg.value_or("(no message)").c_str());
			}
			return ok;
		});
	}

	std::string LuaQuote(std::string const& s)
	{
		std::string out = "\"";
		for (char const c : s)
		{
			switch (c)
			{
				case '"':  out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n";  break;
				case '\r': break;
				default:   out += c;      break;
			}
		}
		return out + "\"";
	}
}


sol::state& Lua() { return g_lua; }
FailureKind LastFailure() { return g_lastFailure; }
int CheckFailures() { return g_checkFailures; }

void InitLua(std::vector<std::string> const& paths)
{
	g_lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::table,
		sol::lib::math, sol::lib::os, sol::lib::io, sol::lib::debug);
	// Turn C++ exceptions into plain Lua errors; the runner reports them.
	g_lua.set_exception_handler([](lua_State* L, sol::optional<std::exception const&>, sol::string_view what) {
		return sol::stack::push(L, what);
	});
	std::string searchPath;
	for (auto const& dir : paths) searchPath += dir + "/?.lua;";
	std::string const defaults = g_lua["package"]["path"];
	g_lua["package"]["path"] = searchPath + defaults;
	RegisterApi(g_lua.create_named_table("ja2"));
}


std::string TranslateLegacyScript(std::string const& source, std::string& error)
{
	std::ostringstream lua;
	std::istringstream in(source);
	std::string line;
	int n = 0;
	while (std::getline(in, line))
	{
		++n;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		auto const hash = line.find('#');
		// "#" also starts colours (#rrggbb); only a leading one is a comment.
		auto const first = line.find_first_not_of(" \t");
		if (first == std::string::npos || (hash == first)) continue;

		std::istringstream words(line);
		std::string verb;
		words >> verb;
		std::vector<std::string> a;
		for (std::string w; words >> w;)
		{
			if (w[0] == '#' && w.size() != 4 && w.size() != 7) break; // trailing comment
			a.push_back(w);
		}
		auto need = [&](size_t count, char const* usage) {
			if (a.size() >= count) return true;
			error = ST::format("line {}: {} expects {}", n, verb, usage).to_std_string();
			return false;
		};
		auto arg = [&](size_t i, char const* def) { return i < a.size() ? a[i] : std::string(def); };

		if (verb == "move")        { if (!need(2, "X Y")) return {}; lua << "ja2.move(" << a[0] << ", " << a[1] << ")\n"; }
		else if (verb == "click")  { if (!need(2, "X Y")) return {}; lua << "ja2.click(" << a[0] << ", " << a[1] << ")\n"; }
		else if (verb == "rclick") { if (!need(2, "X Y")) return {}; lua << "ja2.rclick(" << a[0] << ", " << a[1] << ")\n"; }
		else if (verb == "key")    { if (!need(1, "NAME")) return {}; lua << "ja2.key(" << LuaQuote(a[0]) << ")\n"; }
		else if (verb == "type")
		{
			auto const pos = line.find("type") + 4;
			auto const text = line.substr(line.find_first_not_of(" \t", pos));
			lua << "ja2.type(" << LuaQuote(text) << ")\n";
		}
		else if (verb == "wait")   { if (!need(1, "MS")) return {}; lua << "ja2.wait(" << a[0] << ")\n"; }
		else if (verb == "waitpixel")
		{
			if (!need(3, "X Y #RRGGBB [tol] [timeoutMs]")) return {};
			lua << "ja2.waitPixel(" << a[0] << ", " << a[1] << ", " << LuaQuote(a[2]) << ", "
			    << arg(3, "8") << ", " << arg(4, "30000") << ")\n";
		}
		else if (verb == "waitstable")
		{
			if (!need(2, "X Y [timeoutMs]")) return {};
			lua << "ja2.waitStable(" << a[0] << ", " << a[1] << ", " << arg(2, "10000") << ")\n";
		}
		else if (verb == "assertpixel")
		{
			if (!need(3, "X Y #RRGGBB [tol]")) return {};
			lua << "ja2.check(ja2.pixelIs(" << a[0] << ", " << a[1] << ", " << LuaQuote(a[2]) << ", " << arg(3, "8")
			    << "), " << LuaQuote(ST::format("line {}: pixel ({}, {}) should be {}", n, a[0], a[1], a[2]).to_std_string())
			    << " .. ', got ' .. ja2.pixel(" << a[0] << ", " << a[1] << "))\n";
		}
		else if (verb == "screenshot") { if (!need(1, "PATH")) return {}; lua << "ja2.screenshot(" << LuaQuote(a[0]) << ")\n"; }
		else
		{
			error = ST::format("line {}: unknown command '{}'", n, verb).to_std_string();
			return {};
		}
	}
	return lua.str();
}

}
