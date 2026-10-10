#ifndef _SPREAD_BURST_H
#define _SPREAD_BURST_H

#include "JA2Types.h"

#include <vector>


void ResetBurstLocations(void);
void AccumulateBurstLocation( INT16 sGridNo );
void PickBurstLocations( SOLDIERTYPE *pSoldier );
void AIPickBurstLocations( SOLDIERTYPE *pSoldier, INT8 bTargets, SOLDIERTYPE *pTargets[5] );

void RenderAccumulatedBurstLocations(void);

/** The tiles of the accumulated burst spread (empty when not tracking): the native overlay draws them. */
std::vector<INT16> AccumulatedBurstGridNos();

void DeleteSpreadBurstGraphics();

#endif
