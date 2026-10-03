#ifndef __PREBATTLE_INTERFACE_H
#define __PREBATTLE_INTERFACE_H

#include "Types.h"
#include "Strategic_Movement.h"

#include <string>
#include <vector>

void InitPreBattleInterface(GROUP* battle_group, bool persistent_pbi);
void KillPreBattleInterface(void);
void RenderPreBattleInterface(void);

extern BOOLEAN gfPreBattleInterfaceActive;
extern bool    gfDisplayPotentialRetreatPaths;
extern BOOLEAN gfAutomaticallyStartAutoResolve;
extern BOOLEAN fDisableMapInterfaceDueToBattle;

extern GROUP *gpBattleGroup;

extern BOOLEAN gfTacticalTraversal;
extern GROUP *gpTacticalTraversalGroup;
extern SOLDIERTYPE *gpTacticalTraversalChosenSoldier;

extern BOOLEAN gfGotoSectorTransition;
extern BOOLEAN gfEnteringMapScreenToEnterPreBattleInterface;

enum
{
	//General encounter codes (gubEnemyEncounterCode)
	NO_ENCOUNTER_CODE,			//when there is no encounter
	ENEMY_INVASION_CODE,
	ENEMY_ENCOUNTER_CODE,
	ENEMY_AMBUSH_CODE,
	ENTERING_ENEMY_SECTOR_CODE,
	CREATURE_ATTACK_CODE,

	BLOODCAT_AMBUSH_CODE,
	ENTERING_BLOODCAT_LAIR_CODE,

	//Explicit encounter codes only (gubExplicitEnemyEncounterCode -- a superset of gubEnemyEncounterCode)
	FIGHTING_CREATURES_CODE,
	HOSTILE_CIVILIANS_CODE,
	HOSTILE_BLOODCATS_CODE,
};

extern BOOLEAN gfAutoAmbush;
extern BOOLEAN gfHighPotentialForAmbush;

void ActivatePreBattleAutoresolveAction(void);
void ActivatePreBattleEnterSectorAction(void);
void ActivatePreBattleRetreatAction(void);

void CalculateNonPersistantPBIInfo(void);

//SAVE START

//Using the ESC key in the PBI will get rid of the PBI and go back to mapscreen, but
//only if the PBI isn't persistant (!gfPersistantPBI).
extern BOOLEAN gfPersistantPBI;

//Contains general information about the type of encounter the player is faced with.  This
//determines whether or not you can autoresolve the battle or even retreat.  This code
//dictates the header that is used at the top of the PBI.
extern UINT8 gubEnemyEncounterCode;

//The autoresolve during tactical battle option needs more detailed information than the
//gubEnemyEncounterCode can provide.  The explicit version contains possibly unique codes
//for reasons not normally used in the PBI.  For example, if we were fighting the enemy
//in a normal situation, then shot at a civilian, the civilians associated with the victim
//would turn hostile, which would disable the ability to autoresolve the battle.
extern BOOLEAN gubExplicitEnemyEncounterCode;

//Location of the current battle (determines where the animated icon is blitted) and if the
//icon is to be blitted.
extern BOOLEAN gfBlitBattleSectorLocator;

extern SGPSector gubPBSector;

extern BOOLEAN gfCantRetreatInPBI;
//SAVE END

void WakeUpAllMercsInSectorUnderAttack();

void RetreatAllInvolvedPlayerGroups( void );

bool PlayerGroupInvolvedInThisCombat(GROUP const&);
bool PlayerMercInvolvedInThisCombat(SOLDIERTYPE const&);

enum
{
	LOG_DEFEAT,
	LOG_VICTORY,
};
void LogBattleResults( UINT8 ubVictoryCode);

extern BOOLEAN gfUsePersistantPBI;
extern BOOLEAN gfRenderPBInterface;

void HandlePreBattleInterfaceStates(void);

// ---- what the native pre-battle panel draws (NativeUI/MapScreenNative.cc, docs/ui/mapscreen.md) ---------
// A read-only snapshot plus the commands the native buttons send, so the native and the legacy panel run the
// same callbacks. The legacy pre-battle interface keeps running underneath (hotkeys a/e/r, transition, exit).
struct PreBattleMercInfo
{
	std::string name, assignment, condition, hp, bp; // the involved mercs
	std::string location, destination, departure;     // the uninvolved mercs (in place of condition/hp/bp)
};

struct PreBattleView
{
	bool        active = false;
	bool        persistent = false;
	bool        can_auto = true, can_enter = true, can_retreat = true;
	bool        blink = false;
	std::string header;
	std::string sector;       // the battle sector's name
	std::string enemy_label;  // "Enemies" / "Creatures" / "Bloodcats"
	std::string enemy_count;  // the known number, or "?"
	int         mercs = 0, militia = 0;
	std::string auto_help, enter_help, retreat_help;
	std::vector<PreBattleMercInfo> involved, uninvolved;
};

/** The pre-battle panel now, or active == false when none is up. */
PreBattleView GetPreBattleView();

#endif
