#pragma once
// The adapter of the map screen's action table (issue #48 slice 2, NativeUI/MapCommands.h): what an action *does*,
// beside the legacy handlers it does it with (MapScreen.cc's `RunMapAction`), so the keyboard, the screen's buttons and
// Lua (ja2.map.run) all run the same code and none of them goes through a key event.
//
// The map screen has to be the one on screen: the actions touch its state (the message list, the team list, the
// filters, the exits).

#include "MapCommands.h"

#include <string_view>

/** Runs one of the map screen's actions. */
void RunMapAction(MapCommands::Id id);

/** Runs the action named @a name ("view.mines"), false when the map has no such action. */
bool RunMapActionNamed(std::string_view name);
