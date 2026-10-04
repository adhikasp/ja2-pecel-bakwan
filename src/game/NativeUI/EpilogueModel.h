#pragma once
// The victory epilogue's presentation model (docs/ui/epilogue.md): how the campaign's own counters become the
// summary page. Pure numbers and classes — no game state, no RmlUi (EpilogueModel_unittest.cc).

#include <string>
#include <vector>

namespace NativeUI
{

namespace EpilogueModel
{
	/** What the campaign leaves behind, in the game's own counters (Strategic_Status.h, Campaign.cc, the
	 * strategic map). Read once, when the screen opens. */
	struct Raw
	{
		int days = 0;         // GetWorldDay()
		int sectors = 0;      // surface sectors the player controls
		int sectorsTotal = 256;
		int killedAdmin = 0;  // gStrategicStatus.usEnemiesKilled[ENEMY_KILLED_TOTAL][ADMIN/TROOP/ELITE]
		int killedTroop = 0;
		int killedElite = 0;
		int effort = 0;       // CurrentPlayerProgressPercentage(), 0-100
		int served = 0;       // profiles recruited into the player's team
		int fell = 0;         // of them, MERC_IS_DEAD
	};

	/** One stat card: what it reports and the number under the big value (0: nothing extra to say). */
	struct Stat
	{
		std::string key;  // "days" | "sectors" | "killed" | "served"
		int value = 0;
		int subValue = 0; // sectors: of how many; served: how many fell
	};

	/** The cards, in the order the page shows them. */
	std::vector<Stat> Stats(Raw const&);

	/** Every enemy the player's people brought down, by rank. */
	int KilledTotal(Raw const&);

	/** The war-effort bar's fill: the game's 0-100 estimate, clamped. */
	int EffortPercent(Raw const&);
}

}
