#ifndef SQUAD_ROSTER_H
#define SQUAD_ROSTER_H

// Who is in the squad bar, and what a click on a merc's card does (issue #319, docs/plan/native-tactical.md
// "SquadRoster"). The slot order is the core in NativeUI/SquadRosterModel.h; this is its adapter onto the soldiers.
// F1-F10, "next merc" and the cards of the native HUD all read it. It used to be the legacy team panel's slots.

#include "JA2Types.h"

/** Sizes the bar for the squad size the game policy allows. */
void SquadRosterInit();
int  SquadRosterSlots();
/** The soldier in a slot, NULL when it is empty. */
SOLDIERTYPE* SquadRosterAt(int slot);
/** The slot a soldier stands in, -1 when he is not in the bar. */
int  SquadRosterSlotOf(SOLDIERTYPE const* s);
void SquadRosterClear();
bool SquadRosterRemove(SOLDIERTYPE const* s);
/** Puts a merc of the current squad that is in this sector into the bar (a vehicle puts its passengers). A merc of
 * another sector is taken out of the world instead. */
void SquadRosterAdd(SOLDIERTYPE* s);
/** The merc that follows @a prev in the bar, among those that can be given orders. */
SOLDIERTYPE* SquadRosterNext(SOLDIERTYPE* prev);
/** Bit n is set when slot n has a merc. */
unsigned SquadRosterMask();

// ---- what the player does to a card ------------------------------------------------------------------------------
/** A click on the face: select him, locate him, or continue his move when he waits for it. */
void SquadRosterCardClick(int slot);
/** A second click or the context action: open his detail panel. */
void SquadRosterCardDetails(int slot);
/** The enemy-seen mark: cycle through the enemies he sees (or act as a click when there are none). */
void SquadRosterCardEnemyClick(int slot);
/** The pointer is over a card: his path to continue is shown, and taken away again. */
void SquadRosterCardHover(int slot, bool on);
/** The pointer is over the face of the merc the detail panel shows. */
void SquadRosterDetailHover(bool on);
/** A click on the hand strip of a card: @a hand 0 is the main hand, 1 the other; @a secondary the right button. */
void SquadRosterHandClick(int slot, int hand, bool secondary);

/** Selects or locates a merc as a click on his face does (ALT and the options for the old selection method). */
void HandleLocateSelectMerc(SOLDIERTYPE* s, bool force_select);
void HandleMouseOverSoldierFaceForContMove(SOLDIERTYPE* s, BOOLEAN fOn);
void UpdateForContOverPortrait(SOLDIERTYPE* s, BOOLEAN fOn);

#endif
