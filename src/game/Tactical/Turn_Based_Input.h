#ifndef TURN_BASED_INPUT_H
#define TURN_BASED_INPUT_H

#include "JA2Types.h"
#include "MouseSystem.h"


extern const SOLDIERTYPE* gUITargetSoldier;

INT8    HandleMoveModeInteractiveClick(UINT16 usMapPos);
BOOLEAN HandleUIReloading(SOLDIERTYPE* pSoldier);
void TacticalViewPortTouchCallbackTB(UINT32 reason);

// What the tactical commands (Tactical/TacticalCommandsAdapter.cc) run
void HandleStealthChangeFromUIKeys();
void SetBurstMode(void);
void ToggleTreeTops(void);
void HandleTBSwapHands();
/** Makes squad @a iSquad (0-based) the current one when it is in this sector and the buttons are free. */
void ChangeCurrentSquad(INT32 iSquad);

#endif
