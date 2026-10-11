#pragma once

#include "AutomationLua.h"

#include <string>

/** @file
 * The Lua door onto the tactical commands, the squad roster and the world pointer (issue #319,
 * docs/plan/native-tactical.md "TacticalCommands", "SquadRoster", "WorldPointer"). The rules are in
 * `NativeUI/TacticalCommands.h`, `NativeUI/SquadRosterModel.h` and `NativeUI/PointerRouter.h`; the adapters in
 * `Tactical/TacticalCommandsAdapter.cc`, `Tactical/SquadRoster.cc` and `Tactical/WorldPointer.cc`. Everything the HUD's
 * buttons and the keyboard can do is asserted as data, with no click path and no key event:
 *
 *   ja2.tactical.commands()  { { name, group, key, label, icon, hud, enabled, why }... } in table order, the state
 *                            of every command now; why is "" when enabled
 *   ja2.tactical.run(name)   runs the command when it is available: { ran, why }
 *   ja2.tactical.key(combo)  the command a key combo ("shift+space", "c") is bound to: its name or nil
 *   ja2.tactical.roster()    { slots, filled, mask, members = { { slot, name, id, selected }... } }
 *   ja2.tactical.pointer()   { inWorld, buttons, x, y, worldX, worldY, edges = { left, right, top, bottom },
 *                              viewport = { w, h } }
 */
namespace Automation
{
	void RegisterTacticalLua(sol::state& L, sol::table& ja2);
}
