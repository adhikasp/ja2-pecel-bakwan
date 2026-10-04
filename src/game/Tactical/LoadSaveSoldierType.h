#ifndef LOADSAVESOLDIERTYPE_H
#define LOADSAVESOLDIERTYPE_H

#include "JA2Types.h"


void ExtractSoldierType(const BYTE* Src, SOLDIERTYPE* Soldier, UINT32 uiSavedGameVersion);

void InjectSoldierType(BYTE* Dst, const SOLDIERTYPE* Soldier);

#endif
