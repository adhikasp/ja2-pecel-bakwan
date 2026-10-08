#pragma once

#include "AutomationLua.h"
#include "JA2Types.h"

#include <vector>

struct SOLDIERTYPE;

/** @file
 * The range lane: a two-actor shooting lane with an exact distance, a fixed seed
 * and a record of what the damage pipeline decided for every shot (issue #263,
 * docs/plan/equipment-revamp.md). This is the shared test surface the equipment
 * issues assert through, so none of them has to invent its own.
 *
 * The Lua API is `ja2.debug("lane", spec)` to set the lane up, `ja2.debug("laneShot")`
 * to order one shot at it, and `ja2.lane()` to read the shots back. Both debug
 * calls run the real combat code: the same `HandleItem` a click on the tile would.
 */
namespace Automation
{
	/** Stage a lane in the loaded sector from a Lua spec. Recognised fields:
	 *
	 *   distance      tiles from the shooter to the target, exactly (default 8, and
	 *                 never inside the messy-death range, where one solid hit can end
	 *                 a soldier and the target cannot be restored between shots)
	 *   seed          the random seed the lane runs on (default: left alone, so the
	 *                 session seed applies); a seed makes a lane reproducible on its own
	 *   shooter       the loadout for our shooter: `name` picks the merc, plus the
	 *                 fields Scenario::ApplyEquipment takes (weapon, ammo, condition,
	 *                 attachments, lbe, pockets) and `stats`
	 *   target        the loadout for the one enemy: `class`, `health` and the same
	 *                 equipment fields
	 *
	 * Returns the lane: { shooter, shooterGrid, targetGrid, distance, ... }. */
	sol::table StageRangeLane(sol::state_view L, sol::table const& spec);

	/** Order one shot down the lane: the target is restored to full health and its
	 * armour to full condition first, so every shot is measured against a fresh
	 * target, and the shooter is given the AP for it. Returns true when the shot
	 * was ordered. */
	bool FireLaneShot();

	/** The lane and every shot fired down it: the lane's geometry and gear, then
	 * one entry per shot with the roll, the damage, the armour that absorbed it,
	 * whether the round got through, how loud it was and what it did to the gun. */
	sol::table LaneReport(sol::state_view L);

	/** One shot, as the lane recorded it. */
	struct LaneShot
	{
		// the trigger pull (Weapons.h: ShotFired)
		int      chanceToHit = 0;
		int      roll        = 0;
		bool     hit         = false;
		int      noiseVolume = 0;
		int      conditionBefore = 0;
		int      conditionAfter  = 0;

		// the round arriving (Weapons.h: ShotImpact); absent when the shot missed
		bool     impacted    = false;
		int      distance    = 0;
		int      hitLocation = 0;
		int      impactBeforeArmour = 0;
		int      armourProtection   = 0;
		int      damage      = 0;
		bool     penetrated  = false;
	};

	/** The recorded lane, in C++ form: for the unit tests and the Lua read-back. */
	struct Lane
	{
		bool staged = false;
		std::string shooterName;
		std::string targetName;
		int  distance    = 0;   // tiles the lane was asked for
		int  actualRange = 0;  // tiles between the two, as placed
		UINT8 shooterId  = 0;
		UINT8 targetId   = 0;
		UINT8 apPool     = 100; // the AP the shooter is refilled to, so the turn never leaves us
		int   aim        = 0;   // extra AP the shooter aims with (issue #102)
		int   mode       = 0;   // the fire mode: 0 normal, 1 burst, 3 autofire
		std::vector<LaneShot> shots;
	};
}