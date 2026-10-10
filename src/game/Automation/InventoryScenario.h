#pragma once

#include "AutomationLua.h"

#include <string>

/** @file
 * The Lua door onto the inventory core (issue #317, docs/plan/native-tactical.md "InventoryCore").
 * The rules are in `Equipment/InventoryCore.h`, the adapter in `Tactical/InventoryAdapter.cc`,
 * `Interface_Panels.cc` and `Interface_Items.cc`; this turns them into tables a script reads and
 * the moves into calls, so a move or a refusal is asserted as data with no click path.
 *
 *   ja2.inventory([merc])          the hand, the pending question, the last outcome and the
 *                                  pockets of a merc (default: the selected one) as data
 *   ja2.inventoryOp(op, spec)      one move; returns its outcome { ok, action, why, item }
 *   ja2.debug("hand", spec)        hold an item in the hand (set a scenario up)
 */
namespace Automation
{
	/** { hand = { item, name, count, from, fromSlot } | nil, asking = "merge" | "permanent" | nil,
	 *    last = { ok, action, why, item }, merc, ap, pockets = { [slot] = { item, name, count,
	 *    status, attach = { internalName... } } }, sheet = { open, item, money = {...} } }.
	 *  @a merc is a merc's name, or empty for the one the panel shows / the selected one. */
	sol::table InventoryState(sol::state& L, std::string const& merc);

	/** op is one of:
	 *   "click"   { merc?, slot, right?, ctrl? }   a click on a pocket
	 *   "answer"  { yes }                          the question a move asked
	 *   "attach"  { index, right? }                an attachment position of the open sheet
	 *   "unload"  {}                               the sheet's unload button
	 *   "money"   { which, right? }                which: 0 = 1000, 1 = 100, 2 = 10, 3 = done
	 *   "close"   {}                               close the sheet
	 *   "move"    { merc?, from, to }              pocket to pocket on one merc, no cursor
	 * An unknown op throws std::runtime_error with the name. */
	sol::table InventoryOp(sol::state& L, std::string const& op, sol::optional<sol::table> spec);

	/** Take @a item (internal name) into the hand of @a merc, as a click on a pocket would have. */
	void StageHand(std::string const& merc, std::string const& item, int count);
}
