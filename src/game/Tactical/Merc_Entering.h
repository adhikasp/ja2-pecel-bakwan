#ifndef _MERC_ENTRING_H
#define _MERC_ENTRING_H

#include "JA2Types.h"

void ResetHeliSeats(void);
void AddMercToHeli(SOLDIERTYPE* s);

void StartHelicopterRun( INT16 sGridNoSweetSpot );


void HandleHeliDrop(void);


extern BOOLEAN gfIngagedInDrop;

// True while the helicopter is bringing mercs in (or flying off again).
bool HeliDropInProgress();

#endif
