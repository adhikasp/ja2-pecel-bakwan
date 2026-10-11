#pragma once
// The adapter of the tactical command table (issue #319, NativeUI/TacticalCommands.h): reads the game into the table's
// Context, answers {enabled, why} for a command and executes it through the legacy action functions
// (HandleStanceChangeFromUIKeys, UIHandleChangeLevel, GoToMapScreenFromTactical, ...) - never through a key event.
// The HUD's buttons, the keyboard and Lua (ja2.tactical.run) all come through here.

#include "TacticalCommands.h"

#include <string>

/** The game as the availability functions see it. */
TacticalCommands::Context ReadTacticalContext();

/** What a command can do now. */
TacticalCommands::Availability TacticalCommandState(TacticalCommands::Id id);

struct CommandOutcome
{
	bool        ran = false;
	std::string why;   // TacticalCommands::WhyName of the refusal, "" when it ran
};

/** Runs a command when it is available; says why in a screen message when it is not (@a announce). */
CommandOutcome RunTacticalCommand(TacticalCommands::Id id, bool announce = true);

/** A key press of the tactical screen: the command bound to it runs. True when the key is a command's (it was
 * handled, whether the command could run or not). @a mods are the SHIFT_DOWN / CTRL_DOWN / ALT_DOWN bits. */
bool RunTacticalKey(unsigned key, unsigned mods);
