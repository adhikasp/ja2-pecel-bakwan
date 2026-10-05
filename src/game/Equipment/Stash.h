#pragma once
// The strategic sector stash: the piles of items a sector holds, and the mass operations over them
// (stack & merge, sorting, filling magazines, repairing, moving a selection between two stashes).
//
// This is the pure core. It has no game state: everything it needs to know about an item type arrives
// through a lookup, so every rule here is unit-testable on its own (Stash_unittest.cc) and the game
// only has to fill a table from the item registry (SectorStash.cc).
//
// The design rules it encodes, from docs/plan/equipment-revamp.md:
//   - magazines are items and reloading is whole magazine: loose rounds top up the magazines in the
//     sector inventory, never in the middle of a fight;
//   - condition that earns its keep: repair spends points, worst item first;
//   - one pile takes as many as a pocket takes (getPerPocket), and money is always one pile.

#include "Item_Types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Equipment {

/** What the stash rules need to know about one item type. Filled from the item definition. */
struct StashTraits
{
	uint16_t    stackLimit = 1;  // getPerPocket: how many of this item go in one pile, 0 = one
	uint32_t    itemClass  = 0;  // IC_* mask, for the category
	std::string name;            // the name the player sorts by
	uint16_t    weight      = 0; // grams of one item

	bool     magazine = false;   // holds rounds (ubShotsLeft)
	uint16_t capacity = 0;       // rounds a magazine holds
	uint16_t calibre  = 0;       // calibre index (0: none)
	uint16_t ammoType = 0;       // ammo type index

	bool     gun      = false;   // takes a magazine
	uint16_t magSize  = 0;       // rounds the gun's own magazine holds

	bool     repairable = false;
	int8_t   repairEase = 0;     // +1: 10% cheaper per point, -1: 10% dearer
	bool     money      = false;
};

/** Fills StashTraits for one item id. */
using StashTraitsLookup = std::function<StashTraits(uint16_t)>;

/** One pile of items lying in a sector: what it is, what is on it, where it is, and how the player
 * has marked it. This is the core's own copy of a WORLDITEM's payload; the adapter maps it. */
struct StashPile
{
	uint16_t itemId = 0;              // 0: an empty slot
	uint8_t  count  = 0;
	int8_t   status[MAX_OBJECTS_PER_SLOT] = {}; // condition % of each item in the pile
	uint8_t  rounds[MAX_OBJECTS_PER_SLOT] = {}; // rounds in each magazine of an ammo pile
	uint16_t attach[MAX_ATTACHMENTS]  = {};     // what is fitted to this item
	int8_t   attachStatus[MAX_ATTACHMENTS] = {};
	uint16_t ammoItem = 0;                        // the magazine a gun holds
	uint32_t money    = 0;                        // a money pile carries an amount, not conditions

	int16_t  gridNo    = 0;
	uint8_t  level     = 0;
	int8_t   zHeight   = 0;
	uint16_t flags     = 0;
	bool     reachable = true;  // the squad standing here can pick it up
	bool     marked    = false; // the player's mark, for a mass operation
};

using Stash = std::vector<StashPile>;

// ---- categories -------------------------------------------------------------------------------------------
// One place decides what an item is "for" in the sector inventory, so the category filter and the
// type sort can never disagree.
enum class StashCategory : uint8_t { All, Guns, Ammo, Armour, Explosives, Medical, Other };
StashCategory CategoryOf(uint32_t itemClass);
uint32_t      CategoryMask(StashCategory);
const char*   CategoryKey(StashCategory); // "all", "guns", ... : the UI string and the Lua key

// ---- selection -------------------------------------------------------------------------------------------
// The mark rides on the pile, so it survives sorting: a player who ticks two piles and then sorts
// still has those two piles marked.
void ToggleMark(Stash&, size_t index);
int  MarkReachable(Stash&, bool on); // every non-empty reachable pile
int  InvertMarks(Stash&);
int  MarkedCount(Stash const&);

// ---- mass operations -------------------------------------------------------------------------------------
/** What one mass operation did. `note` is a stable key for the readout ("stash.note.merged"), never
 * prose: the view model and the Lua surface both localise it. */
struct StashReport
{
	int      piles       = 0; // piles emptied, filled or relocated
	int      items       = 0; // individual items affected
	int      magazines   = 0; // magazines filled, or swapped into a gun
	int      rounds      = 0; // rounds moved
	int      guns        = 0; // guns loaded
	int      repaired    = 0; // items brought back to full condition
	int      pointsSpent = 0;
	int      pointsLeft  = 0;
	uint32_t money       = 0; // dollars pooled or moved
	const char* note     = "";
};

/** Like piles go together, as many as one pile takes (stackLimit); money pools into a single pile;
 * the emptied piles are dropped. A pile the squad cannot reach never merges with one it can, and a
 * rifle with a silencer never merges into a bare rifle. */
StashReport MergeStash(Stash&, const StashTraitsLookup&);

/** Reorder the stash. Type sorts by category then name, name alphabetically, condition worst-first
 * and count largest-first, so the thing you came for is at the top. */
enum class StashSort : uint8_t { Type, Name, Condition, Count };
void SortStash(Stash&, StashSort, const StashTraitsLookup&);

/** Reload what is lying in the sector: rounds move out of a magazine with spares into one that is
 * not full (the same magazine, else the same calibre and ammo type), and then every gun in the stash
 * takes the fullest magazine that fits it and gives its own back to the stash. */
StashReport FillMagazines(Stash&, const StashTraitsLookup&);

/** Spend repair points on the stash: the most damaged repairable pile first, ties by position, until
 * the points or the damage run out. Only what the squad can reach is repaired. */
StashReport RepairStash(Stash&, const StashTraitsLookup&, int points);

/** The mass move: every marked pile leaves `from` and lands in `to`, stacking onto like piles there
 * and overflowing into new ones. Selective pickup (stash to a merc), drop-all (a merc's gear to the
 * stash) and the move between two sectors are all this one rule. */
StashReport MoveMarked(Stash& from, Stash& to, const StashTraitsLookup&);

/** Grams a stash weighs, for the load cap on a pickup. */
uint32_t StashWeight(Stash const&, const StashTraitsLookup&);

/** The piles a stash holds that are not empty. */
size_t StashPileCount(Stash const&);

const char* Describe(StashSort);

} // namespace Equipment