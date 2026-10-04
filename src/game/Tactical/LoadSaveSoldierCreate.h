#ifndef LOADSAVESOLIDERCREATE_H
#define LOADSAVESOLIDERCREATE_H

#include "Soldier_Create.h"


UINT16 CalcSoldierCreateCheckSum(const SOLDIERCREATE_STRUCT* const s);

void ExtractSoldierCreateFromFile(HWFILE, SOLDIERCREATE_STRUCT*);

/**
* Load SOLDIERCREATE_STRUCT structure and checksum from the file. */
void ExtractSoldierCreateFromFileWithChecksum(HWFILE, SOLDIERCREATE_STRUCT*, UINT16 *checksum);

void InjectSoldierCreateIntoFile(HWFILE, SOLDIERCREATE_STRUCT const*);

#endif
