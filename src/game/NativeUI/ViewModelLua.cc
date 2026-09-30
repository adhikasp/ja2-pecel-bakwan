#include "GameViewModelsLua.h"

#ifdef WITH_NATIVE_UI
#include "ViewModel.h"

#include <map>
#include <memory>
#include <stdexcept>

namespace
{
	/** View models automation created (they stay, so they keep following their topics). */
	std::map<std::string, std::unique_ptr<NativeUI::ViewModel>> g_owned;

	NativeUI::ViewModel& Get(std::string const& name)
	{
		// an open screen's view model first (not one automation made earlier, which would shadow it)
		auto& slot = g_owned[name];
		for (NativeUI::ViewModel* vm : NativeUI::ViewModel::Live())
		{
			if (vm->Name() == name && vm != slot.get()) return *vm;
		}
		// else a new one, made now: it reads the game as it is now
		auto made = NativeUI::MakeViewModel(name);
		if (!made)
		{
			if (slot) return *slot;
			throw std::invalid_argument("no view model \"" + name + "\" is open or can be made");
		}
		slot.reset();
		slot = std::move(made);
		return *slot;
	}

	sol::object ToLua(sol::state& lua, NativeUI::Value const& v)
	{
		using K = NativeUI::Value::Kind;
		switch (v.kind)
		{
			case K::String: return sol::make_object(lua, v.str);
			case K::Number: return sol::make_object(lua, v.num);
			case K::Bool:   return sol::make_object(lua, v.flag);
			case K::List:
			{
				sol::table t = lua.create_table();
				for (size_t i = 0; i < v.list.size(); ++i) t[i + 1] = ToLua(lua, v.list[i]);
				return t;
			}
			case K::Object:
			{
				sol::table t = lua.create_table();
				for (auto const& [k, x] : v.obj) t[k] = ToLua(lua, x);
				return t;
			}
		}
		return sol::lua_nil;
	}
}

void RegisterViewModelApi(sol::state& lua, sol::table ja2)
{
	// the fields of a view model now: an open screen's, or one made for the call ("status")
	ja2.set_function("viewModel", [&lua](std::string const& name) {
		NativeUI::ViewModel& vm = Get(name);
		vm.Update(true);
		return ToLua(lua, vm.Snapshot());
	});
	ja2.set_function("viewModelCommand", [](std::string const& name, std::string const& command, sol::variadic_args va) {
		NativeUI::Args args;
		for (auto v : va) args.push_back(v.is<std::string>() ? v.as<std::string>() : std::to_string(v.as<double>()));
		if (!Get(name).Invoke(command, args)) throw std::invalid_argument("view model " + name + " has no command " + command);
	});
	ja2.set_function("viewModels", [&lua] {
		sol::table t = lua.create_table();
		int i = 1;
		for (auto* vm : NativeUI::ViewModel::Live()) t[i++] = vm->Name();
		return t;
	});
}

#else

void RegisterViewModelApi(sol::state&, sol::table) {}

#endif
