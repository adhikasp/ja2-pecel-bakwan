#include "TacticalScenario.h"

#include "Input.h"
#include "KeyBindings.h"
#include "Overhead.h"
#include "Soldier_Control.h"
#include "SquadRoster.h"
#include "TacticalCommands.h"
#include "TacticalCommandsAdapter.h"
#include "WorldPointer.h"

#include <stdexcept>

// The Lua door onto the tactical commands, the squad roster and the world pointer (TacticalScenario.h).

namespace Automation
{

namespace
{
	sol::table CommandsTable(sol::state_view L)
	{
		TacticalCommands::Context const ctx = ReadTacticalContext();
		sol::table t = L.create_table();
		int i = 1;
		for (TacticalCommands::Command const& c : TacticalCommands::All())
		{
			TacticalCommands::Availability const a = TacticalCommands::Check(ctx, c.id);
			sol::table e = L.create_table();
			e["name"]    = std::string(c.name);
			e["group"]   = std::string(c.group);
			e["key"] = KeyBindings::Bindings().Key(c.name); // the key it runs now, not the default
			e["default"] = std::string(c.key);
			e["label"]   = std::string(c.label);
			e["icon"]    = std::string(c.icon);
			e["hud"]     = std::string(c.hud);
			e["enabled"] = a.enabled;
			e["why"]     = std::string(TacticalCommands::WhyName(a.why));
			t[i++] = e;
		}
		return t;
	}

	sol::table RunCommand(sol::state_view L, std::string const& name)
	{
		TacticalCommands::Command const* const c = TacticalCommands::Find(name);
		if (!c) throw std::runtime_error("ja2.tactical.run: no command \"" + name + "\"");
		CommandOutcome const o = RunTacticalCommand(c->id, false);
		sol::table t = L.create_table();
		t["ran"] = o.ran;
		t["why"] = o.why;
		return t;
	}

	sol::object KeyCommand(sol::state_view L, std::string const& combo)
	{
		bool ctrl = false, shift = false, alt = false;
		std::string key = combo;
		for (;;)
		{
			auto const plus = key.find('+');
			if (plus == std::string::npos || plus == 0 || plus + 1 == key.size()) break;
			std::string const mod = key.substr(0, plus);
			if (mod == "ctrl") ctrl = true;
			else if (mod == "shift") shift = true;
			else if (mod == "alt") alt = true;
			else break;
			key = key.substr(plus + 1);
		}
		TacticalCommands::Command const* const c = TacticalCommands::ForKey(key, ctrl, shift, alt);
		if (!c) return sol::make_object(L, sol::lua_nil);
		return sol::make_object(L, std::string(c->name));
	}

	sol::table RosterTable(sol::state_view L)
	{
		sol::table t = L.create_table();
		t["slots"] = SquadRosterSlots();
		t["mask"]  = static_cast<int>(SquadRosterMask());
		sol::table members = L.create_table();
		SOLDIERTYPE const* const sel = GetSelectedMan();
		int n = 0;
		for (int slot = 0; slot < SquadRosterSlots(); ++slot)
		{
			SOLDIERTYPE const* const s = SquadRosterAt(slot);
			if (!s) continue;
			sol::table m = L.create_table();
			m["slot"]     = slot;
			m["name"]     = s->name.to_std_string();
			m["id"]       = static_cast<int>(s->ubID);
			m["selected"] = s == sel;
			members[++n]  = m;
		}
		t["filled"]  = n;
		t["members"] = members;
		return t;
	}

	sol::table PointerTable(sol::state_view L)
	{
		sol::table t = L.create_table();
		t["inWorld"]  = WorldPointer::In();
		t["buttons"]  = static_cast<int>(WorldPointer::Buttons());
		SGPPoint const p = GetMousePos();
		t["x"] = static_cast<int>(p.iX);
		t["y"] = static_cast<int>(p.iY);
		LayerPoint const w = WorldPointer::World();
		t["worldX"] = w.x;
		t["worldY"] = w.y;
		PointerRouter::Edges const e = WorldPointer::Edges();
		sol::table edges = L.create_table();
		edges["left"] = e.left; edges["right"] = e.right; edges["top"] = e.top; edges["bottom"] = e.bottom;
		t["edges"] = edges;
		sol::table vp = L.create_table();
		vp["w"] = static_cast<int>(WorldPointer::ViewportRight());
		vp["h"] = static_cast<int>(WorldPointer::ViewportBottom());
		t["viewport"] = vp;
		return t;
	}
}


void RegisterTacticalLua(sol::state& L, sol::table& ja2)
{
	sol::table tactical = L.create_table();
	tactical.set_function("commands", [&L] { return CommandsTable(L); });
	tactical.set_function("run", [&L](std::string const& name) { return RunCommand(L, name); });
	tactical.set_function("key", [&L](std::string const& combo) { return KeyCommand(L, combo); });
	tactical.set_function("roster", [&L] { return RosterTable(L); });
	tactical.set_function("pointer", [&L] { return PointerTable(L); });
	ja2["tactical"] = tactical;
}

}
