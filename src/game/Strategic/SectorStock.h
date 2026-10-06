#pragma once
// The game-side of the strategic sector stash (docs/plan/revamp-gameplay.md, issue #124): the sector
// inventory's mass operations, wired to the legacy globals.
//
// Equipment/Stash.h holds the rules with no game state in them; this is the only place that knows a
// stash is a list of WORLDITEMs in a sector, that a live merc has to stand in the sector to use it,
// and that repairing costs his repair points. The native sector inventory panel
// (NativeUI/MapScreenNative.cc) and the automation surface (ja2.stock(), ja2.debug("stock")) both go
// through here, so a mass operation is assertable headless without a click path.
//
// The stash has one owner: this module. While the panel is open its pool list is a projection of the
// stash, refreshed by SectorInventoryPoolResized()'s caller after every operation.

#include "Equipment/Stash.h"
#include "Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace SectorStock
{

// Whether the player may run a mass operation on the selected sector right now: a live merc stands
// in it and no battle is fought there. The panel need not be open.
bool CanOperate();

/** One pile, as the panel and the automation read it. */
struct PileView
{
	int      index     = 0;  // its position in the panel
	uint16_t item      = 0;
	std::string name;         // the item's short name, which is what the panel shows and what the
	                          // name sort orders by
	uint8_t  count     = 0;
	int      condition = 0;  // the worst item in the pile
	int      rounds    = 0;  // rounds in the first magazine (-1 for anything that holds none)
	uint32_t money     = 0;
	uint16_t gridNo    = 0;
	bool     reachable = true;
	bool     marked    = false;
};

struct StashView
{
	uint32_t money     = 0;
	uint32_t weight    = 0; // grams
	int      pileCount = 0;
	int      itemCount = 0;
	int      marked    = 0;
	Equipment::StashSort sort    = Equipment::StashSort::Type;
	std::string sector;
	std::vector<PileView> piles;
};

/** What the last mass operation did. `note` is a string key for the readout ("stash.note.merged"). */
struct Report
{
	int         items       = 0;
	int         rounds      = 0;
	int         guns        = 0;
	int         magazines   = 0;
	int         repaired    = 0;
	int         pointsSpent = 0;
	int         pointsLeft  = 0;
	uint32_t    money       = 0;
	std::string note;
};

// --- reading ------------------------------------------------------------------------------------
StashView             View();
Equipment::StashSort Sort();
Report LastReport();
const Equipment::StashTraitsLookup& Traits();

// --- selection ---------------------------------------------------------------------------------
void ToggleMark(int index); // a pile index, as View() reports it
void MarkAll(bool on);
void InvertMarks();
/** Empty the sector's stash, so a scenario starts from nothing. */
void Clear();

// --- the legacy panel's own lifetime -------------------------------------------------------------
/** The sector inventory panel is opening on @a sector: take the sector's visible items as the stash
 * and publish them into the panel's pool list, so the marks and the panel agree on one set of
 * piles. Called by Map_Screen_Interface_Map_Inventory.cc. */
void PanelOpened(SGPSector const& sector);

/** The panel is closing: it has written its own list back, so forget the cached stash and read the
 * sector's items again next time. */
void PanelClosed();

// --- the mass operations -----------------------------------------------------------------------
// Each returns what it did, with a note key for the readout ("stash.note.merged").
Report MergeStacks();
Report FillMagazines();
/** Repair the stash with the selected merc's repair points for one pass (he needs a toolkit). */
Report RepairStash();
/** Selective pickup: the marked piles go onto the selected merc, as far as his pockets and his back
 * take them. What does not fit stays in the sector. */
Report TakeMarked();
/** Drop-all: everything the selected merc carries is put down in the sector. */
Report DropAll();
/** Order the stash: Type, Name, Condition or Count. */
void SortBy(Equipment::StashSort);

// --- staging, for the automation harness ------------------------------------------------------
/** Puts the named items ("5.56 AP", ...) into the selected sector's stash, so a test can set up a
 * scenario without walking there. Counts and conditions are per entry and both are optional. */
void StageItems(std::vector<std::string> const& items, std::vector<int> const& counts,
		std::vector<int> const& conditions, std::vector<int> const& money,
		std::string const& hold = std::string());

} // namespace SectorStock