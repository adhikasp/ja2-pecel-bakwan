#include "TacticalCommandsAdapter.h"

#include "Animation_Control.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "Auto_Bandage.h"
#include "Faces.h"
#include "GamePolicy.h"
#include "GameScreen.h"
#include "GameSettings.h"
#include "Handle_Items.h"
#include "Handle_UI.h"
#include "Input.h"
#include "Interface.h"
#include "Interface_Control.h"
#include "Game_Clock.h"
#include "Interface_Items.h"
#include "InventoryActions.h"
#include "InventoryAdapter.h"
#include "ItemModel.h"
#include "Items.h"
#include "JAScreens.h"
#include "Keys.h"
#include "KeyBindings.h"
#include "Message.h"
#include "NativeUI.h"
#include "Overhead.h"
#include "Options_Screen.h"
#include "Overhead_Map.h"
#include "PopupAdapter.h"
#include "PathAI.h"
#include "Points.h"
#include "Soldier_Ani.h"
#include "Soldier_Control.h"
#include "Soldier_Find.h"
#include "Soldier_Functions.h"
#include "Soldier_Macros.h"
#include "SquadRoster.h"
#include "Squads.h"
#include "Strategic.h"
#include "Text.h"
#include "Turn_Based_Input.h"
#include "Vehicles.h"
#include "Weapons.h"
#include "WorldMan.h"

#include <string_theory/format>

using namespace TacticalCommands;

extern BOOLEAN fDisableMapInterfaceDueToBattle;
extern BOOLEAN gfInMeanwhile;

namespace
{
	INT8 StanceOf(SOLDIERTYPE const& s)
	{
		UINT8 h = s.ubDesiredHeight;
		if (h == NO_DESIRED_HEIGHT) h = gAnimControl[s.usAnimState].ubEndHeight;
		return h;
	}

	SOLDIERTYPE* ShownMerc()
	{
		return gsCurInterfacePanel == SM_PANEL && gpSMCurrentMerc ? gpSMCurrentMerc : GetSelectedMan();
	}

	void SelectSlot(int const slot, bool const force)
	{
		SOLDIERTYPE* const s = SquadRosterAt(slot);
		if (!s) return;
		HandleLocateSelectMerc(s, force);
		ErasePath();
		gfPlotNewMovement = TRUE;
	}

	void NextMerc()
	{
		SOLDIERTYPE* const sel = GetSelectedMan();
		if (sel)
		{
			// Select next merc
			SOLDIERTYPE* const next = SquadRosterNext(sel);
			HandleLocateSelectMerc(next, true);
			// Center to guy
			LocateSoldier(GetSelectedMan(), SETLOCATOR);
		}
		guiPendingOverrideEvent = M_ON_TERRAIN;
	}

	void NextSquad()
	{
		if (SOLDIERTYPE* const sel = GetSelectedMan())
		{
			INT32        const current_squad = CurrentSquad();
			SOLDIERTYPE* const new_soldier   = FindNextActiveSquad(sel);
			if (new_soldier->bAssignment != current_squad)
			{
				HandleLocateSelectMerc(new_soldier, true);
				ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_INTERFACE, st_format_printf(pMessageStrings[MSG_SQUAD_ACTIVE], CurrentSquad() + 1));
				// Center to guy
				LocateSoldier(GetSelectedMan(), SETLOCATOR);
			}
		}
		guiPendingOverrideEvent = M_ON_TERRAIN;
	}

	void Run()
	{
		SOLDIERTYPE* const sel = GetSelectedMan();
		if (!sel || MercInWater(sel) || (sel->uiStatusFlags & SOLDIER_ROBOT)) return;
		// Change selected merc to run
		if (sel->usUIMovementMode != WALKING && sel->usUIMovementMode != RUNNING)
		{
			UIHandleSoldierStanceChange(sel, ANIM_STAND);
		}
		else
		{
			sel->usUIMovementMode = RUNNING;
			gfPlotNewMovement     = TRUE;
		}
		sel->fUIMovementFast = 1;
	}

	void Climb()
	{
		SOLDIERTYPE* const s = GetSelectedMan();
		if (!s) return;
		if (FindLowerLevel(s))              BeginSoldierClimbDownRoof(s);
		else if (FindHigherLevel(s))        BeginSoldierClimbUpRoof(s);
		else if (FindFenceJumpDirection(s)) BeginSoldierClimbFence(s);
	}

	void Mute()
	{
		SOLDIERTYPE* const s = ShownMerc();
		if (!s) return;
		s->uiStatusFlags ^= SOLDIER_MUTE;
		ST::string const msg = s->uiStatusFlags & SOLDIER_MUTE ? TacticalStr[MUTE_ON_STR] : TacticalStr[MUTE_OFF_STR];
		ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_INTERFACE, st_format_printf(msg, s->name));
	}

	void EndTurn()
	{
		if (gTacticalStatus.uiFlags & INCOMBAT)
		{
			gfBeginEndTurn = TRUE;
		}
		// Go through the regular entry point so the battle state gets set up properly. It refuses when there is
		// nothing in the sector to fight.
		else if (gamepolicy(can_enter_turnbased))
		{
			EnterCombatMode(OUR_TEAM);
		}
	}

	void Execute(Command const& c)
	{
		switch (c.id)
		{
			case Id::StanceStand:  gfPlotNewMovement = TRUE; HandleStanceChangeFromUIKeys(ANIM_STAND); break;
			case Id::StanceCrouch: HandleStanceChangeFromUIKeys(ANIM_CROUCH); break;
			case Id::StanceProne:  HandleStanceChangeFromUIKeys(ANIM_PRONE);  break;
			case Id::StanceUp:     if (SOLDIERTYPE* const s = GetSelectedMan()) GotoHigherStance(s); break;
			case Id::StanceDown:   if (SOLDIERTYPE* const s = GetSelectedMan()) GotoLowerStance(s);  break;
			case Id::Run:          Run(); break;
			case Id::Stealth:      HandleStealthChangeFromUIKeys(); break;
			case Id::Climb:        Climb(); break;
			case Id::Level:        UIHandleChangeLevel(nullptr); break;
			case Id::Burst:        SetBurstMode(); break;
			case Id::Look:         ToggleLookCursorMode(); break;
			case Id::Talk:         ToggleTalkCursorMode(&guiCurrentEvent); break;
			case Id::SwapHands:    HandleTBSwapHands(); break;
			case Id::EndTurn:      EndTurn(); break;
			case Id::Map:          GoToMapScreenFromTactical(); break;
			case Id::Options:
				guiPreviousOptionScreen = guiCurrentScreen;
				LeaveTacticalScreen(OPTIONS_SCREEN);
				break;
			case Id::Overhead:     if (!InOverheadMap()) GoIntoOverheadMap(); break;
			case Id::Pause:        HandlePlayerPauseUnPauseOfGame(); break;
			case Id::Detail:       ToggleTacticalPanels(); break;
			case Id::DetailClose:  SetNewPanel(0); break;
			case Id::KeyRing:      if (InKeyRingPopup()) KeyRingClose(); else InventoryKeyRing(); break;
			case Id::Log:          NativeUI::TacticalHudToggleLog(); break;
			case Id::Mute:         Mute(); break;
			case Id::Readout:      NativeUI::OpenWeaponReadout(); break;
			case Id::Loadout:      NativeUI::OpenLoadout(); break;
			case Id::MercNext:     NextMerc(); break;
			case Id::SquadNext:    NextSquad(); break;
			case Id::Locate:       if (SOLDIERTYPE* const s = GetSelectedMan()) LocateSoldier(s, 10); break;
			case Id::Roofs:        ToggleTreeTops(); break;
			case Id::Glow:         ToggleItemGlow(!gGameSettings.fOptions[TOPTION_GLOW_ITEMS]); break;
			case Id::Merc1: case Id::Merc2: case Id::Merc3: case Id::Merc4: case Id::Merc5:
			case Id::Merc6: case Id::Merc7: case Id::Merc8: case Id::Merc9: case Id::Merc10:
				SelectSlot(c.arg, true);
				break;
			case Id::Squad1: case Id::Squad2: case Id::Squad3: case Id::Squad4: case Id::Squad5:
			case Id::Squad6: case Id::Squad7: case Id::Squad8: case Id::Squad9: case Id::Squad10:
				ChangeCurrentSquad(c.arg);
				break;
			case Id::Count: break;
		}
	}

	/** A refusal worth saying: the ones a player could not have seen coming. */
	bool Announceable(Why const w)
	{
		switch (w)
		{
			case Why::None: case Why::NoMerc: case Why::Nobody: case Why::Already: case Why::NoDetail: return false;
			default: return true;
		}
	}
}


Context ReadTacticalContext()
{
	Context c;
	SOLDIERTYPE* const sel = GetSelectedMan();
	c.hasMerc = sel != nullptr;
	c.alive = sel && sel->bLife >= OKLIFE;
	c.combat = (gTacticalStatus.uiFlags & INCOMBAT) != 0;
	c.ourTurn = !c.combat || gTacticalStatus.ubCurrentTeam == OUR_TEAM;
	c.handFull = InventoryHand().item != NOTHING;
	c.itemsLocked = gfSMDisableForItems && gsCurInterfacePanel == SM_PANEL;
	c.panelsLocked = gfDisableTacticalPanelButtons != FALSE;
	c.conversation = (gTacticalStatus.uiFlags & ENGAGED_IN_CONV) != 0;
	c.pickupOpen = gfInItemPickupMenu != FALSE;
	c.meanwhile = gfInMeanwhile != FALSE;
	c.mapDisabled = fDisableMapInterfaceDueToBattle != FALSE;
	c.overhead = InOverheadMap();
	c.onRoof = gsInterfaceLevel != 0;
	c.roofAvailable = true;
	c.detailOpen = gsCurInterfacePanel == SM_PANEL && gpSMCurrentMerc != nullptr;
	c.turnBasedAllowed = gamepolicy(can_enter_turnbased);
	c.keyRingOpen = InKeyRingPopup();
	c.slotMask = static_cast<uint16_t>(SquadRosterMask());
	if (sel)
	{
		c.robot = (sel->uiStatusFlags & SOLDIER_ROBOT) != 0;
		c.vehicle = (sel->uiStatusFlags & SOLDIER_VEHICLE) != 0;
		c.inWater = MercInWater(sel);
		switch (StanceOf(*sel))
		{
			case ANIM_CROUCH: c.stance = 1; break;
			case ANIM_PRONE:  c.stance = 2; break;
			default:          c.stance = 0; break;
		}
		// a merc who is not on the map yet (the placement, a cinematic) can do nothing with the ground
		bool const onMap = sel->sGridNo != NOWHERE && sel->bInSector;
		c.canCrouch = onMap && IsValidStance(sel, ANIM_CROUCH);
		c.canProne = onMap && IsValidStance(sel, ANIM_PRONE);
		c.canClimb = onMap && (
			(FindLowerLevel(sel)  && EnoughPoints(sel, GetAPsToClimbRoof(sel, TRUE),  0, FALSE)) ||
			(FindHigherLevel(sel) && EnoughPoints(sel, GetAPsToClimbRoof(sel, FALSE), 0, FALSE)) ||
			FindFenceJumpDirection(sel));
		c.canBurst = IsGunBurstCapable(sel, HANDPOS) || FindAttachment(&sel->inv[HANDPOS], UNDER_GLAUNCHER) != ITEM_NOT_FOUND;
		c.canSwapHands = sel->inv[HANDPOS].usItem != NOTHING || sel->inv[SECONDHANDPOS].usItem != NOTHING;
		c.muted = (sel->uiStatusFlags & SOLDIER_MUTE) != 0;
		c.otherMercs = SquadRosterNext(sel) != sel;
		// a merc who is on a vehicle, in transit or on another assignment is in no squad to walk on from
		c.otherSquads = sel->bAssignment < NUMBER_OF_SQUADS && FindNextActiveSquad(sel)->bAssignment != CurrentSquad();
	}
	return c;
}


Availability TacticalCommandState(Id const id)
{
	return Check(ReadTacticalContext(), id);
}


CommandOutcome RunTacticalCommand(Id const id, bool const announce)
{
	CommandOutcome o;
	Command const& c = Get(id);
	Availability const a = Check(ReadTacticalContext(), id);
	if (!a.enabled)
	{
		o.why = WhyName(a.why);
		if (announce && Announceable(a.why))
		{
			std::string const t = NativeUI::UiString(std::string("tac.cmd.why.") + o.why);
			if (!t.empty()) ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_UI_FEEDBACK, ST::string(t));
		}
		return o;
	}
	Execute(c);
	o.ran = true;
	return o;
}


bool RunTacticalKey(unsigned const key, unsigned const mods)
{
	bool const ctrl = (mods & CTRL_DOWN) != 0, shift = (mods & SHIFT_DOWN) != 0, alt = (mods & ALT_DOWN) != 0;
	std::string const k = KeyBindings::KeyFromInput(key, ctrl, shift, alt);
	if (k.empty()) return false;
	Command const* const cmd = ForKey(k, false, false, false);
	if (!cmd) return false;
	RunTacticalCommand(cmd->id);
	return true;
}
