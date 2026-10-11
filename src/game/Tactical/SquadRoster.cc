#include "SquadRoster.h"

#include "Animation_Control.h"
#include "ContentManager.h"
#include "Faces.h"
#include "GameInstance.h"
#include "GamePolicy.h"
#include "GameSettings.h"
#include "Handle_UI.h"
#include "English.h"
#include "Input.h"
#include "Interface.h"
#include "Interface_Control.h"
#include "InventoryActions.h"
#include "JAScreens.h"
#include "Items.h"
#include "Logger.h"
#include "Overhead.h"
#include "Overhead_Map.h"
#include "PathAI.h"
#include "Render_Dirty.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Soldier_Find.h"
#include "SquadRosterModel.h"
#include "Squads.h"
#include "Text.h"
#include "UILayout.h"
#include "Vehicles.h"
#include "WorldMan.h"
#include "Interface_Items.h"
#include "Strategic.h"
#include "StrategicMap.h"
#include "Soldier_Ani.h"
#include "Points.h"
#include "Weapons.h"
#include "Animation_Data.h"
#include "Soldier_Functions.h"
#include "Interface_Utils.h"
#include "OverlayAdapter.h"

// The slot order of the squad bar. The rules (first free slot, no duplicates, who is "next") are the model's; this
// file knows the soldiers: which merc is in this sector and on the current squad, and the clicks on a card.

namespace
{
	SquadRosterModel::Roster g_roster(6);

	SOLDIERTYPE* ById(int const id)
	{
		return id == SquadRosterModel::NONE ? nullptr : &GetMan(static_cast<UINT>(id));
	}

	int IdOf(SOLDIERTYPE const* const s)
	{
		return s ? s->ubID : SquadRosterModel::NONE;
	}

	/** The slot the pointer is over the card of, -1 none (set by the HUD through SquadRosterCardHover). */
	int g_hoverSlot = -1;
	bool g_detailHover = false;
}


void SquadRosterInit()
{
	int const wanted = std::clamp(static_cast<int>(gamepolicy(squad_size)), 6, 12);
	g_roster.Resize(wanted);
	g_roster.Clear();
}

int SquadRosterSlots() { return g_roster.Slots(); }

SOLDIERTYPE* SquadRosterAt(int const slot) { return ById(g_roster.At(slot)); }

int SquadRosterSlotOf(SOLDIERTYPE const* const s) { return s ? g_roster.SlotOf(IdOf(s)) : -1; }

unsigned SquadRosterMask() { return g_roster.Mask(); }


static void LeaveSlot(SOLDIERTYPE* const s)
{
	if (!s) return;
	if (!(s->uiStatusFlags & SOLDIER_DEAD))
	{
		// Set Id to close
		s->fUICloseMerc = TRUE;
	}

	// Set face to inactive...
	if (s->face) SetAutoFaceInActive(*s->face);

	// DIRTY INTERFACE
	fInterfacePanelDirty = DIRTYLEVEL2;
}


void SquadRosterClear()
{
	for (int id : g_roster.Members()) LeaveSlot(ById(id));
	g_roster.Clear();
}


bool SquadRosterRemove(SOLDIERTYPE const* const s)
{
	if (!g_roster.Remove(IdOf(s))) return false;
	LeaveSlot(const_cast<SOLDIERTYPE*>(s));
	return true;
}


static void AddToSlot(SOLDIERTYPE* const s)
{
	if (g_roster.Add(IdOf(s))) fInterfacePanelDirty = DIRTYLEVEL2;
}


SOLDIERTYPE* SquadRosterNext(SOLDIERTYPE* const prev)
{
	int const next = g_roster.Next(IdOf(prev), [&](int const id) {
		SOLDIERTYPE* const s = ById(id);
		return s && OK_CONTROLLABLE_MERC(s) && OK_INTERRUPT_MERC(s) && s->bAssignment == prev->bAssignment;
	});
	return ById(next);
}


void SquadRosterAdd(SOLDIERTYPE* const s)
{
	if (!s->bActive) return;
	if (s->bTeam != OUR_TEAM) return;

	// Are we in the loaded sector?
	if (s->sSector == gWorldSector && !s->fBetweenSectors && s->bInSector)
	{
		// IF on duty....
		INT32 const cur_squad = CurrentSquad();
		if (s->bAssignment == cur_squad || SoldierIsDeadAndWasOnSquad(s, cur_squad))
		{
			if (s->bAssignment == ASSIGNMENT_DEAD) s->fUICloseMerc = FALSE;

			if (s->uiStatusFlags & SOLDIER_VEHICLE)
			{
				VEHICLETYPE const& v = GetVehicle(s->bVehicleID);
				CFOR_EACH_PASSENGER(v, i) AddToSlot(*i);
			}
			else if (s->ubStrategicInsertionCode != INSERTION_CODE_CHOPPER)
			{
				// ATE: If we have the insertion code of helicopter, don't add just yet!
				// (will add in heli code)
				AddToSlot(s);
			}
		}
	}
	else
	{
		// Make sure we are NOT in this world!
		RemoveSoldierFromGridNo(*s);
		RemoveMercSlot(s);
	}
}


void HandleMouseOverSoldierFaceForContMove(SOLDIERTYPE* pSoldier, BOOLEAN fOn)
{
	INT16 sGridNo;

	if ( pSoldier == NULL )
	{
		return;
	}

	if ( fOn )
	{
		// Check if we are waiting to continue move...
		if ( CheckForMercContMove( pSoldier ) )
		{
			// Display 'cont' on face....
			// Get face
			FACETYPE* const pFace = pSoldier->face;

			pFace->fDisplayTextOver = FACE_DRAW_TEXT_OVER;
			pFace->zDisplayText = TacticalStr[ CONTINUE_OVER_FACE_STR ];

			sGridNo = pSoldier->sFinalDestination;

			if ( pSoldier->bGoodContPath )
			{
				sGridNo = pSoldier->sContPathLocation;
			}

			// While our mouse is here, draw a path!
			PlotPath(pSoldier, sGridNo, NO_COPYROUTE, PLOT, pSoldier->usUIMovementMode, pSoldier->bActionPoints);
		}
	}
	else
	{
		// Remove 'cont' on face....
		// Get face
		FACETYPE* const pFace = pSoldier->face;

		pFace->fDisplayTextOver = FACE_ERASE_TEXT_OVER;

		// Erase path!
		ErasePath();
	}

	fInterfacePanelDirty = DIRTYLEVEL2;
}


void HandleLocateSelectMerc(SOLDIERTYPE* const s, bool const force_select)
{
	if (!s->bActive) return;

	if (gpItemPointer != NULL)
	{
		// ATE: Disable for feel purposes....
		//return;
	}

	// ATE: No matter what we do... if below OKLIFE, just locate....
	if (s->bLife < OKLIFE)
	{
		LocateSoldier(s, SETLOCATOR);
		return;
	}

	if (_KeyDown(ALT))
	{
		if (gGameSettings.fOptions[TOPTION_OLD_SELECTION_METHOD])
		{
			// Select merc
			SelectSoldier(s, SELSOLDIER_ACKNOWLEDGE | SELSOLDIER_FROM_UI);
			s->fFlashLocator = FALSE;
			ResetMultiSelection();
		}
		else
		{
			// Just locate....
			LocateSoldier(s, SETLOCATOR);
		}
	}
	else
	{
		BOOLEAN fSelect = force_select;

		if (s->fFlashLocator == FALSE)
		{
			// If we are currently selected, slide to location
			if (s == GetSelectedMan())
			{
				SlideTo(s, SETLOCATOR);
			}
			else
			{
				if (gGameSettings.fOptions[TOPTION_OLD_SELECTION_METHOD])
				{
					LocateSoldier(s, SETLOCATOR);
				}
				else
				{
					fSelect = TRUE;
				}
			}
		}
		else
		{
			if (gGameSettings.fOptions[TOPTION_OLD_SELECTION_METHOD])
			{
				// If we are currently selected, slide to location
				if (s == GetSelectedMan())
				{
					SlideTo(s, DONTSETLOCATOR);
				}
				else
				{
					LocateSoldier(s, DONTSETLOCATOR);
				}

				fSelect = TRUE;
			}
			else
			{
				if (s == GetSelectedMan())
				{
					LocateSoldier(s, DONTSETLOCATOR);
				}
				else
				{
					fSelect = TRUE;
				}
			}
		}

		if (fSelect)
		{
			// Select merc, only if alive!
			if (!(s->uiStatusFlags & SOLDIER_DEAD))
			{
				SelectSoldier(s, SELSOLDIER_ACKNOWLEDGE | SELSOLDIER_FROM_UI);
			}
		}
	}

	ResetMultiSelection();

	// Handle locate select merc....
	HandleMouseOverSoldierFaceForContMove(s, TRUE);
}


void UpdateForContOverPortrait(SOLDIERTYPE* const s, BOOLEAN const fOn)
{
	// the "continue" path follows the pointer over his card (or over the face of the merc the detail panel shows)
	if (g_detailHover && gsCurInterfacePanel == SM_PANEL && gpSMCurrentMerc == s)
	{
		HandleMouseOverSoldierFaceForContMove(s, fOn);
	}
	else if (g_hoverSlot >= 0 && SquadRosterAt(g_hoverSlot) == s)
	{
		HandleMouseOverSoldierFaceForContMove(s, fOn);
	}
}


void SquadRosterCardHover(int const slot, bool const on)
{
	SOLDIERTYPE* const s = SquadRosterAt(slot);
	if (on)
	{
		g_hoverSlot = slot;
		if (s && s->bActive) HandleMouseOverSoldierFaceForContMove(s, TRUE);
	}
	else if (g_hoverSlot == slot)
	{
		g_hoverSlot = -1;
		if (s && s->bActive) HandleMouseOverSoldierFaceForContMove(s, FALSE);
	}
}


void SquadRosterDetailHover(bool const on)
{
	if (g_detailHover == on) return;
	g_detailHover = on;
	if (gpSMCurrentMerc && gpSMCurrentMerc->bActive) HandleMouseOverSoldierFaceForContMove(gpSMCurrentMerc, on);
}


void SquadRosterCardClick(int const slot)
{
	SOLDIERTYPE* const s = SquadRosterAt(slot);
	if (!s || !s->bActive) return;

	if (!gfInItemPickupMenu && gpItemPointer == NULL)
	{
		if (s->uiStatusFlags & (SOLDIER_DRIVER | SOLDIER_PASSENGER))
		{
			SOLDIERTYPE& vs = GetSoldierStructureForVehicle(GetVehicle(s->iVehicleId));
			HandleLocateSelectMerc(&vs, false);
		}
		else if (!InOverheadMap())
		{
			// If we can continue a move, do so!
			if (CheckForMercContMove(s))
			{
				ContinueMercMovement(s);
				ErasePath();
			}
			else
			{
				HandleLocateSelectMerc(s, false);
			}
		}
		else
		{
			SelectSoldier(s, SELSOLDIER_ACKNOWLEDGE);
		}
	}
}


void SquadRosterCardDetails(int const slot)
{
	SOLDIERTYPE* const s = SquadRosterAt(slot);
	if (!s || !s->bActive || InOverheadMap()) return;
	// Only if guy is not dead!
	if (!(s->uiStatusFlags & SOLDIER_DEAD) && !AM_AN_EPC(s) && !(s->uiStatusFlags & (SOLDIER_DRIVER | SOLDIER_PASSENGER)))
	{
		SetNewPanel(s);
	}
}


void SquadRosterCardEnemyClick(int const slot)
{
	SOLDIERTYPE* const s = SquadRosterAt(slot);
	if (!s || !s->bActive) return;
	if (s->uiStatusFlags & (SOLDIER_DRIVER | SOLDIER_PASSENGER)) return;
	if (s->bOppCnt > 0) CycleVisibleEnemies(s);
	else SquadRosterCardClick(slot);
}


void SquadRosterHandClick(int const slot, int const hand, bool const secondary)
{
	SOLDIERTYPE* const s = SquadRosterAt(slot);
	if (!s) return;

	bool const riding = (s->uiStatusFlags & (SOLDIER_PASSENGER | SOLDIER_DRIVER)) != 0;
	if (!secondary)
	{
		if (hand == 0)
		{
			// Change to use cursor mode...
			guiPendingOverrideEvent = A_ON_TERRAIN;
		}
		else if (riding)
		{
			ExitVehicle(s);
		}
		return;
	}
	if (AM_A_ROBOT(s)) return;
	if (hand == 1 && riding) return;
	UINT16 const usOldHandItem = s->inv[HANDPOS].usItem;
	SwapHandItems(s);
	ReLoadSoldierAnimationDueToHandItemChange(s, usOldHandItem, s->inv[HANDPOS].usItem);
	fInterfacePanelDirty = DIRTYLEVEL2;
}
