#include "Directories.h"
#include "Object_Cache.h"
#include "Real_Time_Input.h"
#include "Spread_Burst.h"
#include "VObject.h"
#include "Soldier_Control.h"
#include "Soldier_Find.h"
#include "Isometric_Utils.h"
#include "RenderWorld.h"
#include "Render_Dirty.h"
#include "VSurface.h"
#include "UILayout.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "WeaponModels.h"

#define MAX_BURST_LOCATIONS 50


struct BURST_LOCATIONS
{
	INT16 sX;
	INT16 sY;
	INT16 sGridNo;
};


static BURST_LOCATIONS gsBurstLocations[MAX_BURST_LOCATIONS];
static INT8            gbNumBurstLocations = 0;

static cache_key_t const guiBURSTACCUM{ INTERFACEDIR "/burst1.sti" };


void ResetBurstLocations( )
{
	gbNumBurstLocations = 0;
}

void AccumulateBurstLocation( INT16 sGridNo )
{
	INT32 cnt;

	if ( gbNumBurstLocations < MAX_BURST_LOCATIONS )
	{
		// Check if it already exists!
		for ( cnt = 0; cnt < gbNumBurstLocations; cnt++ )
		{
			if ( gsBurstLocations[ cnt ].sGridNo == sGridNo )
			{
				return;
			}
		}

		gsBurstLocations[ gbNumBurstLocations ].sGridNo = sGridNo;

		// Get cell X, Y from mouse...
		GetMouseWorldCoords( &( gsBurstLocations[ gbNumBurstLocations ].sX ), &( gsBurstLocations[ gbNumBurstLocations ].sY ) );

		gbNumBurstLocations++;
	}
}



void PickBurstLocations( SOLDIERTYPE *pSoldier )
{
	UINT8 ubShotsPerBurst;
	FLOAT dAccululator = 0;
	FLOAT dStep = 0;
	INT32 cnt;
	UINT8 ubLocationNum;

	// OK, using the # of locations, spread them evenly between our current weapon shots per burst value

	// Get shots per burst
	ubShotsPerBurst = GCM->getWeapon( pSoldier->inv[ HANDPOS].usItem)->ubShotsPerBurst;

	// Use # gridnos accululated and # burst shots to determine accululator
	dStep = gbNumBurstLocations / (FLOAT)ubShotsPerBurst;

	//Loop through our shots!
	for ( cnt = 0; cnt < ubShotsPerBurst; cnt++ )
	{
		// Get index into list
		ubLocationNum = (UINT8)( dAccululator );

		// Add to merc location
		pSoldier->sSpreadLocations[ cnt ] = gsBurstLocations[ ubLocationNum ].sGridNo;

		// Acculuate index value
		dAccululator += dStep;
	}

	// OK, they have been added
}

void AIPickBurstLocations( SOLDIERTYPE *pSoldier, INT8 bTargets, SOLDIERTYPE *pTargets[5] )
{
	UINT8 ubShotsPerBurst;
	FLOAT dAccululator = 0;
	FLOAT dStep = 0;
	INT32 cnt;
	UINT8 ubLocationNum;

	// OK, using the # of locations, spread them evenly between our current weapon shots per burst value

	// Get shots per burst
	ubShotsPerBurst = GCM->getWeapon( pSoldier->inv[ HANDPOS].usItem)->ubShotsPerBurst;

	// Use # gridnos accululated and # burst shots to determine accululator
	//dStep = gbNumBurstLocations / (FLOAT)ubShotsPerBurst;
	// CJC: tweak!
	dStep = bTargets / (FLOAT)ubShotsPerBurst;

	//Loop through our shots!
	for ( cnt = 0; cnt < ubShotsPerBurst; cnt++ )
	{
		// Get index into list
		ubLocationNum = (UINT8)( dAccululator );

		// Add to merc location
		pSoldier->sSpreadLocations[ cnt ] = pTargets[ubLocationNum]->sGridNo;

		// Acculuate index value
		dAccululator += dStep;
	}

	// OK, they have been added
}


std::vector<INT16> AccumulatedBurstGridNos()
{
	std::vector<INT16> out;
	if (!gfBeginBurstSpreadTracking) return out;
	for (INT32 cnt = 0; cnt < gbNumBurstLocations; ++cnt) out.push_back(gsBurstLocations[cnt].sGridNo);
	return out;
}


void DeleteSpreadBurstGraphics()
{
	RemoveVObject(guiBURSTACCUM);
}
