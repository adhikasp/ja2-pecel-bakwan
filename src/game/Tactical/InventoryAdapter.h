#pragma once

#include "Equipment/InventoryCore.h"
#include "JA2Types.h"
#include "Observable.h"

#include <string>

/** @file
 * The adapter between the inventory core (Equipment/InventoryCore.h) and the legacy soldier / item
 * pointer globals (issue #317). The core decides; this fills its plain inputs from the live
 * soldiers, keeps the core's hand in step with `gpItemPointer` (the mirror #324 removes), records
 * what the last click did, and raises the Observables. The moves themselves run in
 * `Interface_Panels.cc` (pockets) and `Interface_Items.cc` (the item sheet), where the legacy
 * statics they apply the verdict through live.
 */

/** One decision point of an inventory move, as the Observables carry it. */
struct InventoryMove
{
	int         merc    = -1;   // the soldier the move is on
	int         slot    = -1;   // the pocket, -1 for the item sheet or the hand
	UINT16      item    = 0;    // the item that moves
	int         count   = 0;
	std::string action;         // "take" | "put" | "attach" | "merge" | "cleanup" | "describe" | "stack"
	                            // | "unload" | "money" | "apply"
	int         apFrom  = 0;    // AP charged to the giver
	int         apTo    = 0;    // AP charged to the receiver
};

/** What the last native inventory click did - the Lua surface and the HUD's hint line read it. */
struct InventoryOutcome
{
	bool        ok     = false;
	std::string action;         // as InventoryMove::action, "refused" when nothing happened, "asked" for a question
	std::string why;            // the refusal in words, or "" when it worked
	UINT16      item   = 0;     // the item involved
	int         apFrom = 0;     // AP the giver paid for a pass between two mercs
	int         apTo   = 0;     // AP the receiver paid
	unsigned    seq    = 0;     // counts outcomes, so a reader can tell a new one from the one it has shown
};

/** Raised before the verdict is applied; the adapter then moves the item. */
extern Observable<InventoryMove const&> BeforeInventoryMove;
/** Raised after the item has moved (or the question was answered). */
extern Observable<InventoryMove const&> OnInventoryMoved;

/** The one inventory core of the game: it owns the hand and the pending question. */
Equipment::InventoryCore& TacticalInventory();

/** A soldier as the rules see him: id, consciousness, whether he can pay the 3 AP check and
 *  whether the pockets of the panel showing him are within reach. */
Equipment::InvParty InventoryPartyOf(SOLDIERTYPE const* s);

/** Re-reads `gpItemPointer` into the core's hand. Called by the item pointer's writers. */
void SyncInventoryHand();
/** The hand, current: what is held, whose pocket it came out of. Read this, not `gpItemPointer`. */
Equipment::HeldStack const& InventoryHand();

InventoryOutcome const& LastInventoryOutcome();
void RecordInventoryOutcome(InventoryOutcome const& o);

/** Nails and his vest: true when the replacement is refused (no dialogue, unlike the legacy hook). */
bool NailsVestBlocks(SOLDIERTYPE const* s, UINT32 slot, UINT16 replacement);

// ---- driven by the native HUD, the loadout and the Lua surface; defined in Interface_Panels.cc ----

/** A click on pocket @a slot of @a merc: left picks up or puts down, right opens the stack popup or the
 *  description. @a ctrl is Ctrl held. Returns what happened. */
InventoryOutcome InventorySlotClick(SOLDIERTYPE* merc, int slot, bool right, bool ctrl);

/** Answers the question a move asked (the merge confirmation). No-op when nothing is pending. */
InventoryOutcome InventoryAnswer(bool yes);

// ---- one merc's own equipment: the loadout screen and the native map-screen gear panel ----------------------
// Moves inside a single soldier's pockets and the gun in his hand, with no cursor involved. Each raises
// BeforeInventoryMove / OnInventoryMoved and returns false when nothing moved (the object is back where it was).

/** Pocket @a from into pocket @a to, swapping and stacking as PlaceObject does. */
bool InventoryMoveSlot(SOLDIERTYPE& s, int from, int to);
/** One item of pocket @a from onto the gun at @a host; leftovers find a place. */
bool InventoryAttachFromSlot(SOLDIERTYPE& s, int from, int host);
/** Attachment @a index of the gun at @a host into pocket @a to (back on the gun when it does not fit). */
bool InventoryDetach(SOLDIERTYPE& s, int host, int index, int to);
