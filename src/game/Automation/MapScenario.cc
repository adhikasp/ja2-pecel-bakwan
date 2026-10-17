#include "MapScenario.h"

#include "KeyBindings.h"
#include "MapCommands.h"
#include "MapCommandsAdapter.h"

#include <stdexcept>

// The Lua door onto the map screen's actions (MapScenario.h).

namespace Automation
{

namespace
{
sol::table ActionsTable(sol::state_view L)
{
	sol::table t = L.create_table();
	int i = 1;
	for (MapCommands::Command const& c : MapCommands::All())
	{
		KeyBindings::Action const* const a = KeyBindings::Bindings().Find(c.name);
		sol::table e = L.create_table();
		e["name"] = std::string(c.name);
		e["group"] = std::string(c.group);
		e["label"] = std::string(c.label);
		e["key"] = a ? a->key : std::string(); // the key it runs now, not the default
		e["default"] = std::string(c.key);
		e["shared"] = a ? a->shared : false;
		t[i++] = e;
	}
	return t;
}

sol::object KeyAction(sol::state_view const& L, std::string const& combo)
{
	bool ctrl = false, shift = false, alt = false;
	std::string key = combo;
	for (;;)
	{
		auto const plus = key.find('+');
		if (plus == std::string::npos || plus == 0 || plus + 1 == key.size()) break;
		std::string const mod = key.substr(0, plus);
		if (mod == "ctrl")
			ctrl = true;
		else if (mod == "shift")
			shift = true;
		else if (mod == "alt")
			alt = true;
		else
			break;
		key = key.substr(plus + 1);
	}
	MapCommands::Command const* const c = MapCommands::ForKey(KeyBindings::KeyName(key, 0, ctrl, shift, alt));
	if (!c) return sol::make_object(L, sol::lua_nil);
	return sol::make_object(L, std::string(c->name));
}
}

void RegisterMapLua(sol::state& L, sol::table& ja2)
{
	sol::table map = L.create_table();
	map.set_function("actions", [&L] { return ActionsTable(L); });
	map.set_function("key", [&L](std::string const& combo) { return KeyAction(L, combo); });
	map.set_function("run",
					 [](std::string const& name)
					 {
						 if (!MapCommands::Find(name))
							 throw std::runtime_error("ja2.map.run: no action \"" + name + "\"");
						 return RunMapActionNamed(name);
					 });
	ja2["map"] = map;
}

}
