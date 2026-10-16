#include "BindingsScenario.h"

#include "KeyBindings.h"

#include <stdexcept>

// The Lua door onto the rebindable actions (BindingsScenario.h). The tactical command table is the only source so far
// (issue #48 slice 1); the map screen's actions join it in the next slice and show up here with no change.

namespace Automation
{

namespace
{
	std::string ResultName(KeyBindings::BindResult const r)
	{
		switch (r)
		{
			case KeyBindings::BindResult::Ok:            return "";
			case KeyBindings::BindResult::UnknownAction: return "unknown_action";
			case KeyBindings::BindResult::BadKey:        return "bad_key";
			case KeyBindings::BindResult::Unchanged:     return "";
		}
		return "bad_key";
	}

	sol::table BindingsTable(sol::state_view L)
	{
		sol::table t = L.create_table();
		int i = 1;
		for (KeyBindings::Action const& a : KeyBindings::Bindings().Actions())
		{
			sol::table e = L.create_table();
			e["scope"]   = a.scope;
			e["name"]    = a.name;
			e["group"]   = a.group;
			e["label"]   = a.label;
			e["key"]     = a.key;
			e["default"] = a.def;
			e["changed"] = a.changed;
			t[i++] = e;
		}
		return t;
	}

	void Require(std::string const& name)
	{
		if (!KeyBindings::Bindings().Find(name))
			throw std::runtime_error("no such action \"" + name + "\"");
	}
}

void RegisterBindingsLua(sol::state& L, sol::table& ja2)
{
	ja2.set_function("bindings", [&L] { return BindingsTable(L); });
	ja2.set_function("binding", [](std::string const& name) {
		Require(name);
		return KeyBindings::Bindings().Key(name);
	});
	ja2.set_function("bind", [&L](std::string const& name, std::string const& key) {
		KeyBindings::BindOutcome const out = KeyBindings::Bindings().Bind(name, key);
		// the direct call is the assertion path and reports rather than throws; a name that is not an action is a
		// test bug, though, so say so loudly
		if (out.result == KeyBindings::BindResult::UnknownAction) throw std::runtime_error("no such action \"" + name + "\"");
		sol::table t = L.create_table();
		t["ok"]    = out.result == KeyBindings::BindResult::Ok;
		t["why"]   = ResultName(out.result);
		t["loser"] = out.loser;
		return t;
	});
	ja2.set_function("resetBinding", [](std::string const& name) {
		Require(name);
		KeyBindings::Bindings().Reset(name);
	});
	ja2.set_function("resetBindings", [] { KeyBindings::Bindings().ResetAll(); });
}

}
