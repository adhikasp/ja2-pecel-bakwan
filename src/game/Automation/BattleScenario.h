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
	 *                 { count, class, weapon, distance, grids = { grid, ... },
	 *                   units = { { grid, class, weapon, direction }, ... } }
	 *                 `units` spawns exactly those enemies, each on its own grid
	 *                 with its own class, gun and facing, for pin-point, staggered
	 *                 placements; it overrides `count`/`grids`.
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
	 *   militia       the player's own AI soldiers (MILITIA_TEAM): a count, or a
	 *                 table { count, class, grids = { grid, ... },
	 *                         units = { { grid, class, direction }, ... } }.
	 *                 class is "green" (default), "regular" or "elite"; `units`
	 *                 pins each militia to its own grid. The staged militia
	 *                 replace whatever militia the sector already had, so the
	 *                 scenario's list is the whole force. They fight on the
	 *                 player's side on their own AI turn, so a scenario can
	 *                 stage an AI battle and assert its outcome.
	 *
	 *   our           per-merc setup, matched by `name` (or by position when no
	 *                 entry is named). Each entry:
	 *                 { name, weapon, armour, grid, direction,
	 *                   items  = { internalName, ... },
	 *                   stats  = { marksmanship, agility, dexterity, strength,
	 *                              leadership, wisdom, medical, mechanical,
	 *                              explosive, morale, level = 1..10,
	 *                              health = sets life and life max } }
	 *
	 * `grid` / `grids` place actors on exact tiles; otherwise the enemies go on
	 * free tiles `distance` away from the team. `direction` is 0..7 (0 = north,
	 * increasing clockwise, as the game uses it) and leaves the facing alone when
	 * omitted. Throws std::runtime_error with a message the script sees. */
	void StageBattle(sol::table const& spec);

	/** Order @a soldier to fire at @a targetGridNo through the real fire-weapon
	 * event, as a click on the tile would. */
	void FireAtGrid(SOLDIERTYPE* soldier, INT16 targetGridNo);
}
