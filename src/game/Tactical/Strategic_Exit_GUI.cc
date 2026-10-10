#include "Button_System.h"
#include "Directories.h"
#include "Font.h"
#include "Handle_UI.h"
#include "MouseSystem.h"
#include "GameScreen.h"
#include "StrategicMap.h"
#include "Game_Clock.h"
#include "Font_Control.h"
#include "Cursors.h"
#include "Strategic_Exit_GUI.h"
#include "MercTextBox.h"
#include "RenderWorld.h"
#include "Overhead.h"
#include "Cursor_Control.h"
#include "Input.h"
#include "NativeUI.h"
#include "PopupAdapter.h"
#include "PopupModels.h"
#include "Text.h"
#include "Strategic_Movement.h"
#include "Soldier_Macros.h"
#include "Squads.h"
#include "Map_Screen_Interface_Map.h"
#include "PreBattle_Interface.h"
#include "Strategic.h"
#include "Fade_Screen.h"
#include "MessageBoxScreen.h"
#include "Quests.h"
#include "Creature_Spreading.h"
#include "Video.h"
#include "ScreenIDs.h"
#include "Render_Dirty.h"
#include "VSurface.h"
#include "UILayout.h"

#include <string_theory/format>
#include <string_theory/string>


BOOLEAN gfInSectorExitMenu = FALSE;
static bool gfExitModal = false; // the dialogue entered the modal tactical state


// The sector exit menu is a model (PopupModels::ExitState): which radio is on, whether the next sector loads, what is
// off and why. The native HUD draws it (CurrentExitPopup) and the choices come back as calls. No button, region or
// text box is made for it.
struct EXIT_DIALOG_STRUCT
{
	PopupModels::ExitInput input;
	PopupModels::ExitState state;
	UINT8 ubDirection;
	INT16 sAdditionalData;
	UINT8 ubNumPeopleOnSquad;
	UINT32 uiMinutes;
	const SOLDIERTYPE* single_move_will_isolate_epc; //if not NULL, then that means it is an EPC
};


static EXIT_DIALOG_STRUCT gExitDialog;


UINT8   gubExitGUIDirection;
INT16   gsExitGUIAdditionalData;
SGPSector gsWarpWorld;
INT16   gsWarpGridNo;


static void Finish(bool fOk);


//KM:  New method is coded for more sophistocated rules.  All the information is stored within the gExitDialog struct
//     and calculated upon entry to this function instead of passing in multiple arguments and calculating it prior.
static void InternalInitSectorExitMenu(UINT8 const ubDirection, INT16 const sAdditionalData)
{
	UINT32  uiTraverseTimeInMinutes;
	UINT16  usMapPos = 0;
	INT8    bExitCode = -1;
	BOOLEAN OkExitCode;

	//STEP 1:  Calculate the information for the exit gui
	gExitDialog = EXIT_DIALOG_STRUCT{};
	PopupModels::ExitInput& in = gExitDialog.input;

	// OK, bring up dialogue... first determine some logic here...
	switch( ubDirection )
	{
		case EAST:
			bExitCode = EAST_STRATEGIC_MOVE;
			break;
		case WEST:
			bExitCode = WEST_STRATEGIC_MOVE;
			break;
		case NORTH:
			bExitCode = NORTH_STRATEGIC_MOVE;
			break;
		case SOUTH:
			bExitCode = SOUTH_STRATEGIC_MOVE;
			break;
		case DIRECTION_EXITGRID:
			bExitCode = -1;
			usMapPos = sAdditionalData;
			break;
	}

	OkExitCode = OKForSectorExit( bExitCode, usMapPos, &uiTraverseTimeInMinutes );
	gExitDialog.uiMinutes = uiTraverseTimeInMinutes;

	//if the traverse time is short, then traversal is percieved to be instantaneous.
	in.shortTrip = uiTraverseTimeInMinutes <= 5;

	if( OkExitCode == 1 )
	{
		in.okSingle = true;
		if( gfRobotWithoutControllerAttemptingTraversal )
		{
			gfRobotWithoutControllerAttemptingTraversal = FALSE;
			in.robotUncontrolled = true;
		}
	}
	else if( OkExitCode == 2 )
	{
		in.okAll = true;
	}

	in.combat = (gTacticalStatus.uiFlags & INCOMBAT) != 0;
	if (in.combat)
	{
		INT32 cnt = 0;
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (OkControllableMerc(s)) ++cnt;
		}
		in.controllableMercs = cnt;
	}

	const SOLDIERTYPE* const sel = GetSelectedMan();
	gExitDialog.ubNumPeopleOnSquad = NumberOfPlayerControllableMercsInSquad(sel->bAssignment);
	in.squadSize = gExitDialog.ubNumPeopleOnSquad;

	//Determine
	CFOR_EACH_IN_TEAM(pSoldier, OUR_TEAM)
	{
		if (pSoldier == sel) continue;
		if( !pSoldier->fBetweenSectors &&
			pSoldier->sSector == gWorldSector &&
			pSoldier->bLife >= OKLIFE &&
			pSoldier->bAssignment != sel->bAssignment &&
			pSoldier->bAssignment != ASSIGNMENT_POW &&
			pSoldier->bAssignment != IN_TRANSIT &&
			pSoldier->bAssignment != ASSIGNMENT_DEAD )
		{
			//KM:  We need to determine if there are more than one squad (meaning other concious mercs in a different squad or assignment)
			//     These conditions were done to the best of my knowledge, so if there are other situations that require modification,
			//     then feel free to do so.
			in.multipleSquads = true;
			break;
		}
	}

	// the selected merc: an escort goes with the squad; a merc alone with escorts may not leave them behind
	in.selectedIsEscort = AM_AN_EPC(sel);
	if (!in.selectedIsEscort)
	{
		//check to see if we have one selected merc and one or more EPCs.
		//If so, don't allow the selected merc to leave by himself.
		//Assuming that the matching squad assignment is in the same sector.
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (s == sel) continue;
			if (s->bAssignment != sel->bAssignment) continue;
			if (AM_AN_EPC(s))
			{
				// record the epc.  If there are more than one EPCs, then it doesn't
				// matter.  This is used in building the text message explaining why
				// the selected merc can't leave.  This is how we extract the EPC's
				// name.
				gExitDialog.single_move_will_isolate_epc = s;
				++in.escortsInSquad;
			}
			else
			{
				//We have more than one merc, so we will allow the selected merc to leave alone if
				//the user so desired.
				in.otherMercInSquad = true;
				break;
			}
		}
	}

	in.enemyInSector = gTacticalStatus.fEnemyInSector != 0;
	if (in.enemyInSector) in.militiaInSector = GetNumberOfMilitiaInSector(gWorldSector);

	gExitDialog.state = PopupModels::OpenExit(in);
	gExitDialog.ubDirection = ubDirection;
	gExitDialog.sAdditionalData = sAdditionalData;

	// nothing draws the menu without the native HUD: what the dialogue opened with is what OK would have done
	if (!NativeUI::TacticalHudActive())
	{
		gfInSectorExitMenu = TRUE;
		Finish(true);
		return;
	}

	EnterModalTactical( TACTICAL_MODAL_WITHMOUSE );
	gfExitModal = true;
	gfIgnoreScrolling = TRUE;

	guiPendingOverrideEvent = EX_EXITSECTORMENU;
	HandleTacticalUI( );

	gfInSectorExitMenu = TRUE;

	InterruptTime();
	PauseGame();
	LockPauseState(LOCK_PAUSE_SECTOR_EXIT);

	PopupEvent e;
	e.kind = PopupKind::Exit;
	e.action = "open";
	e.what = PopupModels::ExitJumpName(PopupModels::Confirm(gExitDialog.state));
	FinishPopup(e);
}


static void DoneFadeInWarp(void)
{
}


static void DoneFadeOutWarpCallback(void)
{
	// Warp!
	FOR_EACH_IN_TEAM(pSoldier, OUR_TEAM)
	{
		// Are we in this sector, On the current squad?
		if (pSoldier->bLife >= OKLIFE && pSoldier->bInSector)
		{
			gfTacticalTraversal = TRUE;
			SetGroupSectorValue(gsWarpWorld, *GetGroup(pSoldier->ubGroupID));

			// Set next sectore
			pSoldier->sSector = gsWarpWorld;

			// Set gridno
			pSoldier->ubStrategicInsertionCode = INSERTION_CODE_GRIDNO;
			pSoldier->usStrategicInsertionData = gsWarpGridNo;
			// Set direction to face....
			pSoldier->ubInsertionDirection = 100 + NORTHWEST;
		}
	}


	// OK, insertion data found, enter sector!
	SetCurrentWorldSector(gsWarpWorld);

	// OK, once down here, adjust the above map with crate info....
	gfTacticalTraversal = FALSE;
	gpTacticalTraversalGroup = NULL;
	gpTacticalTraversalChosenSoldier = NULL;

	gFadeInDoneCallback = DoneFadeInWarp;

	FadeInGameScreen( );
}


static void WarpToSurfaceCallback(MessageBoxReturnValue const bExitValue)
{
	if( bExitValue == MSG_BOX_RETURN_YES )
	{
		gFadeOutDoneCallback = DoneFadeOutWarpCallback;

		FadeOutGameScreen( );
	}
	else
	{
		InternalInitSectorExitMenu( gubExitGUIDirection, gsExitGUIAdditionalData );
	}
}


void InitSectorExitMenu(UINT8 const ubDirection, INT16 const sAdditionalData)
{
	gubExitGUIDirection     = ubDirection;
	gsExitGUIAdditionalData = sAdditionalData;

	if (gWorldSector.z >= 2 && gubQuest[ QUEST_CREATURES ] == QUESTDONE)
	{
		if (GetWarpOutOfMineCodes(gsWarpWorld, &gsWarpGridNo))
		{
			// ATE: Check if we are in a creature lair and bring up box if so....
			DoMessageBox(MSG_BOX_BASIC_STYLE, gzLateLocalizedString[STR_LATE_33], GAME_SCREEN,
					MSG_BOX_FLAG_YESNO, WarpToSurfaceCallback, NULL);
			return;
		}
	}

	InternalInitSectorExitMenu(ubDirection, sAdditionalData);
}


BOOLEAN HandleSectorExitMenu( )
{
	if (!gfInSectorExitMenu) return FALSE;

	// the modal dialogue takes the keys itself: Esc leaves, Enter goes
	InputAtom Event;
	while (DequeueSpecificEvent(&Event, KEYBOARD_EVENTS))
	{
		if (Event.usEvent != KEY_DOWN) continue;
		switch (Event.usParam)
		{
			case SDLK_ESCAPE: RemoveSectorExitMenu(FALSE); return TRUE;
			case SDLK_RETURN: RemoveSectorExitMenu(TRUE);  return TRUE;
		}
	}
	return FALSE;
}


/** The menu closes; with @a fOk the squad leaves as the dialogue was set. */
static void Finish(bool const fOk)
{
	if (!gfInSectorExitMenu) return;
	gfInSectorExitMenu = FALSE;

	guiPendingOverrideEvent = A_CHANGE_TO_MOVE;

	EXIT_DIALOG_STRUCT& d = gExitDialog;

	gfIgnoreScrolling = FALSE;

	if (gfExitModal)
	{
		gfExitModal = false;
		UnLockPauseState();
		UnPauseGame();
		EndModalTactical();
	}

	if (!fOk) return;

	// If we are an EPC, don't allow this if nobody else on squad
	SOLDIERTYPE const* const sel = GetSelectedMan();
	if (AM_AN_EPC(sel) && d.ubNumPeopleOnSquad == 0)
	{
		ST::string buf = st_format_printf(pMessageStrings[MSG_EPC_CANT_TRAVERSE], sel->name);
		DoMessageBox(MSG_BOX_BASIC_STYLE, buf, GAME_SCREEN, MSG_BOX_FLAG_OK, 0, 0);
		return;
	}

	UINT8 jump_code;
	switch (PopupModels::Confirm(d.state))
	{
		case PopupModels::ExitJump::AllLoad:      jump_code = JUMP_ALL_LOAD_NEW;      break;
		case PopupModels::ExitJump::AllNoLoad:    jump_code = JUMP_ALL_NO_LOAD;       break;
		case PopupModels::ExitJump::SingleLoad:   jump_code = JUMP_SINGLE_LOAD_NEW;   break;
		case PopupModels::ExitJump::SingleNoLoad: jump_code = JUMP_SINGLE_NO_LOAD;    break;
		default: return;
	}

	JumpIntoAdjacentSector(d.ubDirection, jump_code, d.sAdditionalData);
}


void RemoveSectorExitMenu(BOOLEAN const fOk)
{
	if (!gfInSectorExitMenu) return;
	PopupEvent e;
	e.kind = PopupKind::Exit;
	e.action = fOk ? "go" : "cancel";
	e.what = PopupModels::ExitJumpName(fOk ? PopupModels::Confirm(gExitDialog.state) : PopupModels::ExitJump::None);
	BeginPopup(e);
	Finish(fOk != FALSE);
	FinishPopup(e);
}


// ---------------------------------------------------------------------------------------------------------------
// The popup's side (PopupAdapter.h)

/** What a disabled radio or the load box says: the legacy help text, with the names in it. */
static ST::string WhyText(PopupModels::Why const why)
{
	using PopupModels::Why;
	SOLDIERTYPE const* const sel = GetSelectedMan();
	switch (why)
	{
		case Why::MustTravel:      return pExitingSectorHelpText[EXIT_GUI_MUST_GOTO_MAPSCREEN_HELPTEXT];
		case Why::Hostile:         return pExitingSectorHelpText[EXIT_GUI_CANT_LEAVE_HOSTILE_SECTOR_HELPTEXT];
		case Why::MustLoad:        return pExitingSectorHelpText[EXIT_GUI_MUST_LOAD_ADJACENT_SECTOR_HELPTEXT];
		case Why::NeedsTogether:   return pExitingSectorHelpText[EXIT_GUI_ALL_MERCS_MUST_BE_TOGETHER_TO_ALLOW_HELPTEXT];
		case Why::ControlRobot:    return gzLateLocalizedString[STR_LATE_01];
		case Why::MustBeEscorted:
			return sel ? st_format_printf(pExitingSectorHelpText[EXIT_GUI_ESCORTED_CHARACTERS_MUST_BE_ESCORTED_HELPTEXT], sel->name) : ST::string();
		case Why::WouldIsolate:
		case Why::WouldIsolateMany:
		{
			SOLDIERTYPE const* const epc = gExitDialog.single_move_will_isolate_epc;
			if (!sel || !epc) return ST::string();
			bool const male = gMercProfiles[sel->ubProfile].bSex == MALE;
			if (why == Why::WouldIsolate)
			{
				return st_format_printf(pExitingSectorHelpText[male ?
					EXIT_GUI_MERC_CANT_ISOLATE_EPC_HELPTEXT_MALE_SINGULAR : EXIT_GUI_MERC_CANT_ISOLATE_EPC_HELPTEXT_FEMALE_SINGULAR],
					sel->name, epc->name);
			}
			return st_format_printf(pExitingSectorHelpText[male ?
				EXIT_GUI_MERC_CANT_ISOLATE_EPC_HELPTEXT_MALE_PLURAL : EXIT_GUI_MERC_CANT_ISOLATE_EPC_HELPTEXT_FEMALE_PLURAL], sel->name);
		}
		default: return ST::string();
	}
}

ExitPopup CurrentExitPopup()
{
	ExitPopup v;
	if (!gfInSectorExitMenu) return v;
	EXIT_DIALOG_STRUCT const& d = gExitDialog;
	PopupModels::ExitState const& s = d.state;
	v.open = true;
	v.minutes = int(d.uiMinutes);
	v.shortTrip = d.input.shortTrip;
	switch (d.ubDirection)
	{
		case NORTH: v.direction = "north"; break;
		case EAST:  v.direction = "east"; break;
		case SOUTH: v.direction = "south"; break;
		case WEST:  v.direction = "west"; break;
		default:    v.direction = "exit"; break;
	}
	v.from = gWorldSector.AsShortString();
	if (d.ubDirection == NORTH || d.ubDirection == EAST || d.ubDirection == SOUTH || d.ubDirection == WEST)
	{
		SGPSector to = gWorldSector;
		switch (d.ubDirection)
		{
			case NORTH: --to.y; break;
			case SOUTH: ++to.y; break;
			case EAST:  ++to.x; break;
			case WEST:  --to.x; break;
		}
		if (to.IsValid()) v.to = to.AsShortString();
	}
	SOLDIERTYPE const* const sel = GetSelectedMan();
	v.selected = sel ? sel->name : ST::string();
	v.selectedLabel = TacticalStr[EXIT_GUI_SELECTED_MERC_STR];
	v.allLabel = TacticalStr[EXIT_GUI_ALL_MERCS_IN_SQUAD_STR];
	v.loadLabel = d.input.shortTrip ? TacticalStr[EXIT_GUI_GOTO_SECTOR_STR] : TacticalStr[EXIT_GUI_GOTO_MAP_STR];
	v.squadSize = s.squadSize;
	v.singleSel = s.single;
	v.allSel = s.all;
	v.loadSel = s.load;
	v.singleOff = s.singleOff;
	v.allOff = s.allOff;
	v.loadOff = s.loadOff;
	v.singleWhy = PopupModels::WhyKey(s.singleOff ? s.singleWhy : PopupModels::Why::None);
	v.allWhy = PopupModels::WhyKey(s.allOff ? s.allWhy : PopupModels::Why::None);
	v.loadWhy = PopupModels::WhyKey(s.loadOff ? s.loadWhy : PopupModels::Why::None);
	v.singleTip = s.singleOff ? WhyText(s.singleWhy) : ST::string();
	v.allTip = s.allOff ? WhyText(s.allWhy) : ST::string();
	v.loadTip = s.loadOff ? WhyText(s.loadWhy)
		: s.loadHint ? pExitingSectorHelpText[EXIT_GUI_LOAD_ADJACENT_SECTOR_HELPTEXT]
		: pExitingSectorHelpText[EXIT_GUI_GOTO_MAPSCREEN_HELPTEXT];
	v.loadNow = s.loadHint;
	v.jump = PopupModels::ExitJumpName(PopupModels::Confirm(s));
	v.canGo = PopupModels::Confirm(s) != PopupModels::ExitJump::None;
	return v;
}

static bool Choose(char const* const action, void (*apply)(PopupModels::ExitState&, PopupModels::ExitInput const&), bool const off)
{
	if (!gfInSectorExitMenu) return false;
	PopupEvent e;
	e.kind = PopupKind::Exit;
	e.action = "choose";
	e.what = action;
	if (off)
	{
		e.ok = false;
		e.why = "off";
		return FinishPopup(e);
	}
	BeginPopup(e);
	apply(gExitDialog.state, gExitDialog.input);
	return FinishPopup(e);
}

bool ExitChooseSingle() { return Choose("single", PopupModels::ChooseSingle, gfInSectorExitMenu && gExitDialog.state.singleOff); }
bool ExitChooseAll()    { return Choose("all", PopupModels::ChooseAll, gfInSectorExitMenu && gExitDialog.state.allOff); }

bool ExitToggleLoad()
{
	return Choose("load", [](PopupModels::ExitState& s, PopupModels::ExitInput const&) { PopupModels::ToggleLoad(s); },
		gfInSectorExitMenu && gExitDialog.state.loadOff);
}

bool ExitGo()
{
	if (!gfInSectorExitMenu) return false;
	RemoveSectorExitMenu(TRUE);
	return true;
}

void ExitCancel()
{
	RemoveSectorExitMenu(FALSE);
}
