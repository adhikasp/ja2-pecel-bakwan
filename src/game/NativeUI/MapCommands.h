#pragma once
// The strategic map's shortcuts as actions (issue #48 slice 2; docs/ui/mapscreen.md §3). The legacy screen matched raw
// keycodes in four `HandleMod*` switches in Strategic/MapScreen.cc, each case doing its own work; here each is a named
// action with a default key, registered as a KeyBindings source, so the key is a binding rather than a keycode and the
// map's keys appear on the Options > Controls page beside the tactical table's.
//
// Name for the action, not for the screen: where both screens run the same logical action the name is theirs together
// (`squad.select.4`, `nav.options`), and the registry keeps it one row on both. A name a screen implements but another
// registered is declared here too, so the registry knows both run it.
//
// No globals and no RmlUi, and no legacy headers: the table is data. The adapter (Strategic/MapCommandsAdapter.h,
// implemented beside the legacy handlers) reads the game and runs the action - never through a key event - and the key
// dispatch (RunMapKey) asks the registry which of these actions a key runs.
//
// The legacy keycases that are not player actions are not here and stay raw: the cheats (Ctrl+A/T/Z), Alt+X, and the
// Pause key. So do Quick save/load (Alt+S/L), which the tactical screen reads raw too - naming them here alone would
// make a rebind work on one screen and silently not the other, so they are one shared action for the slice that
// converts the tactical screen's remaining shortcuts.

#include "KeyBindings.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace MapCommands
{
/** The screen these actions belong to, in the KeyBindings registry. */
inline constexpr char const* SCOPE = "map";

enum class Id : uint8_t
{
	// the clock
	TimeToggle,
	TimeMore,
	TimeLess,
	// the message log
	MsgUp,
	MsgDown,
	MsgPageUp,
	MsgPageDown,
	MsgFirst,
	MsgLast,
	MsgStopSpeech,
	// what the map shows
	LevelUp,
	LevelDown,
	ViewTowns,
	ViewMines,
	ViewTeams,
	ViewMilitia,
	ViewAirspace,
	ViewItems,
	// the team list
	TeamNext,
	TeamPrev,
	PanelMercInventory,
	TeamSortName,
	TeamSortAssignment,
	TeamSortStatus,
	TeamSortLocation,
	TeamSortDestination,
	TeamSortContract,
	// squads: 1-10 are the tactical table's action as well, 11-20 are the map's
	Squad1,
	Squad2,
	Squad3,
	Squad4,
	Squad5,
	Squad6,
	Squad7,
	Squad8,
	Squad9,
	Squad10,
	Squad11,
	Squad12,
	Squad13,
	Squad14,
	Squad15,
	Squad16,
	Squad17,
	Squad18,
	Squad19,
	Squad20,
	// the map's own actions
	ActContract,
	ActHelp,
	ActSettings,
	ActAssign,
	ActSectorInventory,
	PrebattleEnter,
	PrebattleRetreat,
	// the screens it leaves to
	NavLaptop,
	NavBack,
	NavSave,
	NavLoad,
	NavOptions,
	Count
};

constexpr int COUNT = std::to_underlying(Id::Count);

struct Command
{
	Id id;
	char const* name;  // "view.mines"
	char const* group; // "map_view": how the page groups it
	char const* label; // string key: map.cmd.<name>
	char const* key;   // the default key, "" for none ("space", "=", "shift+1", "escape")
	int arg;           // the n of a Squad<n> / TeamSort<n> action (0-based), else 0
	bool repeat;       // the key keeps running it while it is held (the message scroll, the team list)
	bool shared;       // another screen registered this name: one row, and the map runs it too
};

/** The table, in Id order. */
std::vector<Command> const& All();
Command const& Get(Id);
/** The action named @a name ("view.mines"), or nullptr. */
Command const* Find(std::string_view name);
/** The action a key runs now - the rebindable one, not the table's default - or nullptr. */
Command const* ForKey(std::string_view key);
}
