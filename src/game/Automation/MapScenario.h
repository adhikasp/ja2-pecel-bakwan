#pragma once

#include "AutomationLua.h"

/** @file
 * The Lua door onto the map screen's actions (issue #48 slice 2, docs/ui/mapscreen.md §3). The rules are in
 * `NativeUI/MapCommands.h` and the binding is the same registry the Options page shows, so a script reads and runs the
 * map's actions with no click path and no key event:
 *
 *   ja2.map.actions()   { { scope, name, group, label, key, default, shared }... } - the map's actions and the
 *                       shared ones (the squads and the options screen), in table order, with the key each runs now
 *   ja2.map.key(combo)  the action a key press runs on the map ("w" -> "view.towns"), nil when none. The combo is
 *                       the registry's vocabulary ("w", "shift+1", "ctrl+s")
 *   ja2.map.run(name)   runs one now, through the same code the keyboard does; false when the map has no such action.
 *                       This is how a test asserts what an action *does* without a key event
 */
namespace Automation
{
void RegisterMapLua(sol::state& L, sol::table& ja2);
}
