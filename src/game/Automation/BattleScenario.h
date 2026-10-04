#pragma once

#include "AutomationLua.h"
#include "JA2Types.h"

struct SOLDIERTYPE;

/** @file
 * Test aids for staging a deterministic tactical battle, the tactical e2e track
 * (docs/plan/e2e-tactical-battles.md). The Lua API is `ja2.debug("battle", spec)`
 * to set a fight up and `ja2.debug("fire", gridNo)` to order the selected merc to
 * shoot at a tile; both run the real soldier/combat code.
 */
namespace Automation
{
	/** Stage a fight in the loaded sector from a Lua spec and (by default) enter
	 * turn-based combat. Recognised fields:
	 *
	 *   enemies       how many enemies to spawn (default 10), or a table:
	 *                 { count, class, weapon, distance, grids = { grid, ... } }
	 *   class         "administrator" (default), "army" or "elite"
	 *   weapon        internal name of the gun to give our mercs (default "MP5K")
	 *   enemy_weapon  internal name of a gun to give every enemy (default: their
	 *                 own generated kit)
	 *   armour        true (default) / false / "kevlar" / "spectra": our mercs'
	 *                 body armour
	 *   distance      where enemies stand, in tiles from the team (default 6)
	 *   clear         true (default): remove the enemies already in the sector
	 *   start         true (default): enter turn-based combat with our turn first
	 *
	 *   our           per-merc setup, matched by `name` (or by position when no
	 *                 entry is named). Each entry:
	 *                 { name, weapon, armour, grid,
	 *                   items  = { internalName, ... },
	 *                   stats  = { marksmanship, agility, dexterity, strength,
	 *                              leadership, wisdom, medical, mechanical,
	 *                              explosive, morale, level = 1..10,
	 *                              health = sets life and life max } }
	 *
	 * `grid` / `grids` place actors on exact tiles; otherwise the enemies go on
	 * free tiles `distance` away from the team. Throws std::runtime_error with a
	 * message the script sees. */
	void StageBattle(sol::table const& spec);

	/** Order @a soldier to fire at @a targetGridNo through the real fire-weapon
	 * event, as a click on the tile would. */
	void FireAtGrid(SOLDIERTYPE* soldier, INT16 targetGridNo);
}
