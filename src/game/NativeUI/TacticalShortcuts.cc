#include "TacticalShortcuts.h"

#include <string>
#include <utility>

// The tactical screen's shortcut table (see TacticalShortcuts.h). The keys and the effects are exactly the legacy
// Turn_Based_Input.cc keycases': 'a' auto-bandaged, 'r' ran (Alt+R reloaded), Home toggled the 3D cursor, Ctrl+S and
// Ctrl+L opened the save/load screens, and so on. What was a `switch (keycode)` per modifier is one action; the
// effect is beside the keycase (RunTacticalShortcut), not here.

namespace TacticalShortcuts
{

namespace
{
std::vector<Command> BuildTable()
{
	std::vector<Command> t;
	auto add = [&](Id const id, char const* name, char const* group, char const* icon, char const* key,
				   bool const shared = false) { t.push_back({ id, name, group, "", icon, key, shared }); };

	// the game flow and what the HUD does not reach
	add(Id::Reload, "act.reload", "act", "reload", "alt+r");
	add(Id::QuickSave, "file.quick_save", "file", "save", "alt+s", true);
	add(Id::QuickLoad, "file.quick_load", "file", "load", "alt+l", true);
	add(Id::AutoBandage, "act.auto_bandage", "act", "medkit", "a");
	add(Id::CycleEnemies, "act.cycle_enemies", "act", "enemy", "e");
	add(Id::Range, "act.range", "act", "range", "f");
	add(Id::CycleStack, "act.cycle_stack", "act", "search", "n");
	add(Id::ActionMode, "act.action_mode", "act", "pointer", "u");
	add(Id::Exchange, "act.exchange", "act", "swap-hands", "x");
	add(Id::AutoReload, "act.auto_reload", "act", "reload", "ctrl+shift+r");
	add(Id::SwitchHeadgear, "act.switch_headgear", "act", "face-gear", "ctrl+n");
	add(Id::Settings, "act.settings", "act", "options", "v", true);

	// what the world shows
	add(Id::Cursor3D, "view.cursor_3d", "view", "pointer", "home");
	add(Id::LightEffects, "view.merc_lights", "view", "power", "g");
	add(Id::Wireframe, "view.wireframe", "view", "unexplored", "w");
	add(Id::ZoomIn, "view.zoom_in", "view", "add", "ctrl+=");
	add(Id::ZoomOut, "view.zoom_out", "view", "remove", "ctrl+-");
	add(Id::Tracking, "view.tracking", "view", "target", "alt+f");

	// the soldier and the squad
	add(Id::Continue, "move.continue", "move", "fast-forward", "end");
	add(Id::ClimbWindow, "move.climb_window", "move", "climb", "shift+j");
	add(Id::SquadStealth, "move.squad_stealth", "move", "sneak", "alt+z");

	// the screens
	add(Id::Help, "nav.help", "nav", "question", "shift+h");
	add(Id::Save, "nav.save", "nav", "save", "ctrl+s", true);
	add(Id::Load, "nav.load", "nav", "load", "ctrl+l", true);

	// the message log
	add(Id::ClearLog, "panel.clear_log", "panel", "close", "f12");

	// who is selected
	add(Id::SelectAll, "select.all", "select", "squad", "=");

	// the label of an action is its name under tac.cmd. in the string table
	static std::vector<std::string> labels;
	labels.clear();
	labels.reserve(t.size());
	for (Command const& c : t) labels.push_back(std::string("tac.cmd.") + c.name);
	for (size_t i = 0; i < t.size(); ++i) t[i].label = labels[i].c_str();
	return t;
}
}

std::vector<Command> const& All()
{
	static std::vector<Command> const table = BuildTable();
	return table;
}

Command const& Get(Id const id)
{
	return All()[std::to_underlying(id)];
}

Command const* Find(std::string_view const name)
{
	for (Command const& c : All())
		if (name == c.name) return &c;
	return nullptr;
}

Command const* ForKey(std::string_view const key)
{
	KeyBindings::Action const* const a = KeyBindings::Bindings().Owner(SCOPE, key);
	return a ? Find(a->name) : nullptr;
}

}
