#include "MapCommands.h"

#include <bit>
#include <string>
#include <utility>

// The strategic map's action table (see MapCommands.h). The keys, the groups and the effects are exactly the legacy
// MapScreen.cc keycases': 'w'/'m'/'t'/'z'/'a'/'i' were the filters, '1'-'0' and their shifted twins the squads,
// F1-F6 the sort, Space and '='/'-' the clock, and so on. What was a `switch (keycode)` per modifier is one action.

namespace MapCommands
{

namespace
{
/** The team list's sort methods, in the legacy ChangeCharacterListSortMethod order (MAX_SORT_METHODS). */
constexpr int SORT_COUNT = 6;
char const* const SORT_KEYS[SORT_COUNT] = { "f1", "f2", "f3", "f4", "f5", "f6" };
char const* const SORT_NAMES[SORT_COUNT] = { "team.sort.name",     "team.sort.assignment",  "team.sort.status",
											 "team.sort.location", "team.sort.destination", "team.sort.contract" };
char const* const DIGIT_KEYS[10] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
char const* const SHIFT_DIGIT_KEYS[10] = { "shift+1", "shift+2", "shift+3", "shift+4", "shift+5",
										   "shift+6", "shift+7", "shift+8", "shift+9", "shift+0" };

std::vector<Command> BuildTable()
{
	std::vector<Command> t;
	auto add = [&](Id const id, char const* name, char const* group, char const* key, int const arg = 0,
				   bool const repeat = false, bool const shared = false)
	{ t.push_back({ id, name, group, "", key, arg, repeat, shared }); };

	// the clock (Space also ends the update box, as the legacy case did)
	add(Id::TimeToggle, "time.toggle", "map_time", "space");
	add(Id::TimeMore, "time.more", "map_time", "=");
	add(Id::TimeLess, "time.less", "map_time", "-");

	// the message log
	add(Id::MsgUp, "msg.up", "map_log", "up", 0, true);
	add(Id::MsgDown, "msg.down", "map_log", "down", 0, true);
	add(Id::MsgPageUp, "msg.page_up", "map_log", "pageup", 0, true);
	add(Id::MsgPageDown, "msg.page_down", "map_log", "pagedown", 0, true);
	add(Id::MsgFirst, "msg.first", "map_log", "home");
	add(Id::MsgLast, "msg.last", "map_log", "end");
	add(Id::MsgStopSpeech, "msg.stop_speech", "map_log", "backspace");

	// what the map shows: its levels, then the filters
	add(Id::LevelUp, "level.up", "map_view", "insert");
	add(Id::LevelDown, "level.down", "map_view", "delete");
	add(Id::ViewTowns, "view.towns", "map_view", "w");
	add(Id::ViewMines, "view.mines", "map_view", "m");
	add(Id::ViewTeams, "view.teams", "map_view", "t");
	add(Id::ViewMilitia, "view.militia", "map_view", "z");
	add(Id::ViewAirspace, "view.airspace", "map_view", "a");
	add(Id::ViewItems, "view.items", "map_view", "i");

	// the team list
	add(Id::TeamNext, "team.next", "map_team", "right", 0, true);
	add(Id::TeamPrev, "team.prev", "map_team", "left", 0, true);
	add(Id::PanelMercInventory, "panel.merc_inventory", "map_team", "return");
	for (int i = 0; i < SORT_COUNT; ++i)
		add(static_cast<Id>(std::to_underlying(Id::TeamSortName) + i), SORT_NAMES[i], "map_team", SORT_KEYS[i], i);

	// the squads: 1-10 are the tactical HUD's action too (the map selects the squad's mercs, tactical switches to
	// it), 11-20 are the map's - the tactical table has no eleventh squad to switch to
	static std::vector<std::string> squadNames;
	squadNames.clear();
	for (int i = 1; i <= 20; ++i) squadNames.push_back("squad.select." + std::to_string(i));
	for (int i = 0; i < 10; ++i)
		add(static_cast<Id>(std::to_underlying(Id::Squad1) + i), squadNames[i].c_str(), "select", DIGIT_KEYS[i], i,
			false, true);
	for (int i = 0; i < 10; ++i)
		add(static_cast<Id>(std::to_underlying(Id::Squad11) + i), squadNames[10 + i].c_str(), "map_squad",
			SHIFT_DIGIT_KEYS[i], 10 + i);

	// the map's own actions
	add(Id::ActContract, "act.contract", "map_act", "c");
	add(Id::ActHelp, "act.help", "map_act", "h");
	add(Id::ActSettings, "act.settings", "map_act", "v", 0, false, true);
	add(Id::ActAssign, "act.assign", "map_act", "alt+a");
	add(Id::ActSectorInventory, "act.sector_inventory", "map_act", "ctrl+i");
	add(Id::PrebattleEnter, "prebattle.enter_sector", "map_act", "e");
	add(Id::PrebattleRetreat, "prebattle.retreat", "map_act", "r");

	// the screens it leaves to (nav.options is the tactical HUD's action too: both open the options screen; nav.save
	// and nav.load are the tactical shortcut table's as well, both open the save/load screens)
	add(Id::NavLaptop, "nav.laptop", "map_nav", "l");
	add(Id::NavBack, "nav.back", "map_nav", "escape");
	add(Id::NavSave, "nav.save", "map_nav", "ctrl+s", 0, false, true);
	add(Id::NavLoad, "nav.load", "map_nav", "ctrl+l", 0, false, true);
	add(Id::NavOptions, "nav.options", "nav", "o", 0, false, true);

	// the game flow: the tactical screen's shortcut table declares these names too (issue #48 slice 3), so the
	// registry keeps one shared row, with the tactical screen's group and label (it registers first)
	add(Id::QuickSave, "file.quick_save", "map_act", "alt+s", 0, false, true);
	add(Id::QuickLoad, "file.quick_load", "map_act", "alt+l", 0, false, true);

	// the label of an action is its name under map.cmd. in the string table
	static std::vector<std::string> labels;
	labels.clear();
	labels.reserve(t.size());
	for (Command const& c : t) labels.push_back(std::string("map.cmd.") + c.name);
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
