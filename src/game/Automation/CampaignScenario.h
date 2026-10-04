#pragma once

#include "AutomationLua.h"
#include "JA2Types.h"

#include <string>

/** @file
 * Test aids for staging a whole campaign state, the strategic e2e track
 * (docs/plan/e2e-campaign-state.md). The Lua API is `ja2.debug("campaign", spec)`
 * to author money, the clock, difficulty, town ownership/loyalty/militia, sector
 * garrisons, the merc roster, per-merc gear and quest/fact progress on the live
 * campaign globals — so a save taken after staging is a real save. `ja2.campaign()`
 * reads the same state back, and `ja2.debug("entersector", spec)` loads a sector
 * into tactical (optionally with townsfolk) for the world-map steps.
 */
namespace Automation
{
	/** Stage a campaign state from a Lua spec on the live game. Recognised fields:
	 *
	 *   day, hour, minute  world clock (any subset; the rest is kept)
	 *   money              LaptopSaveInfo.iCurrentBalance
	 *   difficulty         1..3 or "easy"/"medium"/"hard"
	 *   towns              keyed by town id or name:
	 *                      { owned = true, loyalty = 0..100,
	 *                        militia = { green = n, regular = n, elite = n } }
	 *   sectors            keyed by "A9" etc:
	 *                      { enemy = bool, admins = n, troops = n, elites = n }
	 *   mercs              array of
	 *                      { name, sector, assignment, contract_days_left,
	 *                        weapon, armour, health, items = { internalName, ... } }
	 *   progress           { quests = { NAME = "done" | "in_progress" | "not_started" },
	 *                        facts  = { FACT_NAME = true } }
	 *
	 * Unknown town / sector / merc / quest / fact / item names throw
	 * std::runtime_error with the name; a spec never half-applies silently.
	 * Staging replaces the named dimensions, it does not add to them. */
	void StageCampaign(sol::table const& spec);

	/** Read the staged campaign state back: clock, money, difficulty, towns,
	 * sectors, the roster with gear, and quest/fact progress. */
	sol::table CampaignState(sol::state& L);

	/** Load a sector into tactical for a world-map step. Recognised fields:
	 *
	 *   sector         "A9" etc (required)
	 *   battle         a ja2.debug("battle") spec, staged after the sector loads
	 *                  (its garrison is cleared first so nothing spawns early)
	 *   clear_enemies  drop the strategic garrison before loading (default: true
	 *                  when `battle` is given)
	 *
	 * The team is moved there and `SetCurrentWorldSector` is called; the screen
	 * change is pending, so the caller waits for GAME_SCREEN. */
	void EnterSector(sol::table const& spec);

	/** Spawn townsfolk / named NPCs in the loaded sector, near the team. spec:
	 * a count, or { count = n, profiles = { "TONY", ... } }. */
	void SpawnNpcs(sol::object const& spec);
}

namespace Automation::Scenario
{
	/** Set a sector's strategic garrison, keeping the "in battle" counters in step
	 * (the reconciliation invariant the harness guarantees). Values are clamped. */
	void SetSectorGarrison(const SGPSector& sector, int admins, int troops, int elites);

	/** Resolve a quest / fact name (with or without its QUEST_/FACT_ prefix) to its
	 * id. Throws std::runtime_error with the name when unknown. */
	int ResolveQuest(std::string const& name);
	int ResolveFact(std::string const& name);
}
