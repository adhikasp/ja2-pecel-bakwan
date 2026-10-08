#ifndef LOADSAVESOLDIERTYPE_H
#define LOADSAVESOLDIERTYPE_H

#include "JA2Types.h"

// The size of one serialized soldier in a save game. It follows the struct, so
// it moves whenever the soldier's inventory does - callers must size their
// buffers with it rather than with a literal, or they overflow.
constexpr size_t SOLDIERTYPE_BINARY_SIZE = 2444;

void ExtractSoldierType(const BYTE* Src, SOLDIERTYPE* Soldier, UINT32 uiSavedGameVersion);

void InjectSoldierType(BYTE* Dst, const SOLDIERTYPE* Soldier);

#endif
