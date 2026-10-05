#pragma once

#include "AutomationLua.h"
#include "JA2Types.h"

#include <string>

/** @file
 * Test aids for the strategic sector inventory's mass operations (issue #124,
 * docs/plan/revamp-gameplay.md). The rules live in `Equipment/Stash.h` with no
 * game state in them; `Strategic/SectorStock.cc` is the game-side adapter; this
 * is the Lua door onto it, so a mass operation is assertable headless without a
 * click path.
 *
 *   ja2.stock()                 read the selected sector's stash back as data
 *   ja2.stockOp(op, arg)        run one mass operation and return its report
 *   ja2.debug("stock", spec)    stage items into the sector to set a scenario up
 */
namespace Automation
{
	/** Stage items into the selected sector's stash. spec:
	 *   { items = { "FAMAS", "CLIP556_30_AP", ... }, item names, required
	 *     count = { 1, 6, ... },                 items per pile (default 1)
	 *     condition = { 70, 100, ... },          condition % per pile (default 100)
	 *     money = { 5000, ... },                 dollars, for a MONEY pile (default 0)
	 *     hold = "TOOLKIT" }                     what the selected merc takes in hand (optional)
	 *
	 * An unknown item name throws std::runtime_error with the name; nothing is
	 * staged when one of them is unknown. */
	void StageStock(sol::table const& spec);

	/** The selected sector's stash: { sector, sort, money, weight, pileCount,
	 * itemCount, marked, piles = { index, item, name, count, condition, rounds,
	 * money, marked, reachable }, report = { ... } }. */
	sol::table StockState(sol::state& L);

	/** Run one mass operation on the selected sector and return its report as
	 * { items, rounds, guns, magazines, repaired, pointsSpent, pointsLeft, money,
	 * note }. op is one of:
	 *
	 *   "mark" (arg = a pile index)   "markall" (arg = "1"/"0")   "invert"   "clear"
	 *   "sort"  (arg = "type"|"name"|"condition"|"count")
	 *   "merge"  "load"  "repair"  "take"  "drop"
	 *
	 * An unknown op throws std::runtime_error with the name. */
	sol::table StockOp(sol::state& L, std::string const& op, std::string const& arg);
}