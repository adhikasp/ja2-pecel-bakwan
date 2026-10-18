#pragma once
// The tactical screen's remaining shortcuts as actions (issue #48 slice 3): the keycases the tactical HUD's command
// table (NativeUI/TacticalCommands) does not own, which the legacy screen matched raw in Turn_Based_Input.cc's
// HandleMod* switches and a few inline reads. Each becomes a named action with a default key, registered as a
// KeyBindings source under the same "tactical" scope as the command table, so a rebind reaches it and the Options >
// Controls page lists it beside the HUD's own commands.
//
// Name for the action, not for the screen: where both screens run the same logical action the name is theirs together
// (`file.quick_save`, `nav.save`, `act.settings`), and the registry keeps it one row on both. The map screen declares
// the shared names in MapCommands too, so each screen dispatches its own effect through the one action.
//
// No globals and no RmlUi, and no legacy headers: the table is data. The effects live beside the keycases they came
// from (Turn_Based_Input.cc's RunTacticalShortcut) - never through a key event - and this table's ForKey asks the
// registry which of these actions a key runs.
//
// The raw keycases that are not player actions are not here and stay raw: the cheats (Ctrl+A/T/Z, Alt+N/M/Q, the
// debug levels), Alt+X, the Pause key (answered on its release), and the special keys the game reads everywhere (Esc,
// the burst-spread cancel). So is 'w'/'*'/',' and the cover/LOS size keys, which toggle render debug no player can
// reach from a menu.

#include "KeyBindings.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace TacticalShortcuts
{
/** The screen these actions belong to, in the KeyBindings registry. */
inline constexpr char const* SCOPE = "tactical";

enum class Id : uint8_t
{
	// the game flow and what the HUD does not reach
	Reload,
	QuickSave,
	QuickLoad,
	AutoBandage,
	CycleEnemies,
	Range,
	CycleStack,
	ActionMode,
	Exchange,
	AutoReload,
	SwitchHeadgear,
	Settings,
	CycleKnownEnemies,
	// what the world shows
	Cursor3D,
	LightEffects,
	Wireframe,
	ZoomIn,
	ZoomOut,
	Tracking,
	// the soldier and the squad
	Continue,
	ClimbWindow,
	SquadStealth,
	// the screens
	Help,
	Save,
	Load,
	// the message log
	ClearLog,
	StopSpeech,
	// the audio
	MusicDown,
	// who is selected
	SelectAll,
	Count
};

constexpr int COUNT = std::to_underlying(Id::Count);

struct Command
{
	Id id;
	char const* name;  // "file.quick_save"
	char const* group; // "act", "view", "move", "nav", "panel", "select", "file"
	char const* label; // string key: tac.cmd.<name>
	char const* icon;  // an icon of the design system ("reload"); "" for none
	char const* key;   // the default key, "" for none ("alt+r", "ctrl+s", "home")
	bool shared;       // another screen registered this name: one row, and the tactical screen runs it too
};

/** The table, in Id order. */
std::vector<Command> const& All();
Command const& Get(Id);
/** The action named @a name ("file.quick_save"), or nullptr. */
Command const* Find(std::string_view name);
/** The action a key runs now - the rebindable one, not the table's default - or nullptr. */
Command const* ForKey(std::string_view key);
}
