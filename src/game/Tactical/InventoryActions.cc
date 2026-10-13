// What the tactical interface keeps of the legacy single-merc panel: which merc the inventory view shows, whether his
// pockets are in reach, the click rules of a pocket (the inventory core's adapter, issue #317), the key ring, the
// money button and the effects of changing equipment. The panel itself - its drawing, regions and buttons - is gone
// from tactical (issue #319); the native HUD (NativeUI/TacticalHud.cc) draws the view and calls into this file.
// The legacy shopkeeper (ShopMercPanel.cc) still shows the merc through the same state.

#include "InventoryAdapter.h"
#include "Animation_Control.h"
#include "Assignments.h"
#include "Boxing.h"
#include "Button_System.h"
#include "ContentManager.h"
#include "Cursors.h"
#include "Debug.h"
#include "Dialogue_Control.h"
#include "Directories.h"
#include "Drugs_And_Alcohol.h"
#include "English.h"
#include "Faces.h"
#include "Finances.h"
#include "Font.h"
#include "Font_Control.h"
#include "GameInstance.h"
#include "GamePolicy.h"
#include "GameScreen.h"
#include "GameSettings.h"
#include "Game_Clock.h"
#include "HImage.h"
#include "Handle_UI.h"
#include "Input.h"
#include "Interface.h"
#include "Interface_Control.h"
#include "Interface_Items.h"
#include "Interface_Utils.h"
#include "Isometric_Utils.h"
#include "Items.h"
#include "JAScreens.h"
#include "Keys.h"
#include "LOS.h"
#include "LaptopSave.h"
#include "Local.h"
#include "Logger.h"
#include "MapScreen.h"
#include "Map_Screen_Interface.h"
#include "Message.h"
#include "MessageBoxScreen.h"
#include "MouseSystem.h"
#include "NativeUI.h"
#include "NewStrings.h"
#include "Object_Cache.h"
#include "OppList.h"
#include "Options_Screen.h"
#include "Overhead.h"
#include "Overhead_Map.h"
#include "PathAI.h"
#include "Points.h"
#include "Radar_Screen.h"
#include "Render_Dirty.h"
#include "ScreenIDs.h"
#include "ShopKeeper_Interface.h"
#include "Soldier_Functions.h"
#include "Soldier_Ani.h"
#include "Soldier_Macros.h"
#include "Sound_Control.h"
#include "Squads.h"
#include "Strategic.h"
#include "StrategicMap.h"
#include "SysUtil.h"
#include "Text.h"
#include "Timer_Control.h"
#include "UILayout.h"
#include "VObject.h"
#include "VObject_Blitters.h"
#include "VSurface.h"
#include "Vehicles.h"
#include "Video.h"
#include "WeaponModels.h"
#include "Weapons.h"
#include "WordWrap.h"
#include <algorithm>
#include <iterator>
#include <memory>
#include <string_theory/format>
#include <string_theory/string>

#include "InventoryActions.h"
#include "ShopMercPanel.h"
#include "SquadRoster.h"

#define SM_ITEMDESC_START_X			214 + INTERFACE_START_X
#define SM_ITEMDESC_START_Y			1 + INV_INTERFACE_START_Y
#define SM_ITEMDESC_HEIGHT			128
#define SM_ITEMDESC_WIDTH			314

static void OpenKeyRing();
static void ConfirmationToDepositMoneyToPlayersAccount(MessageBoxReturnValue);

BOOLEAN gfSMDisableForItems = FALSE;
BOOLEAN gfDisableTacticalPanelButtons = FALSE;
BOOLEAN gfCheckForMouseOverItem = FALSE;
UINT32 guiMouseOverItemTime = 0;
INT8 gbCheckForMouseOverItemPos = 0;
SOLDIERTYPE* gSelectSMPanelToMerc = nullptr;
static BOOLEAN gfReEvaluateDisabledINVPanelButtons = FALSE;

// The merc the single-merc (inventory) view shows
SOLDIERTYPE *gpSMCurrentMerc = nullptr;


void CheckForDisabledForGiveItem(void)
{
	const SOLDIERTYPE* const cur = gpSMCurrentMerc;
	Assert(cur != nullptr);
	Assert(cur->sGridNo != NOWHERE);

	if (guiCurrentScreen == SHOPKEEPER_SCREEN)
	{
		gfSMDisableForItems = !CanMercInteractWithSelectedShopkeeper(cur);
		return;
	}

	// Default to true
	gfSMDisableForItems = TRUE;

	// ATE: Is the current merc unconscious.....
	if (cur->bLife < OKLIFE && gpItemPointer != nullptr)
	{
		// Go through each merc and see if there is one closeby....
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (s->bLife >= OKLIFE && !IsMechanical(*s) && s->bInSector && IsMercOnCurrentSquad(s))
			{
				const INT16 sDist        = PythSpacesAway(cur->sGridNo, s->sGridNo);
				const INT16 sDistVisible = DistanceVisible(s, DIRECTION_IRRELEVANT, DIRECTION_IRRELEVANT, cur->sGridNo, cur->bLevel);

				// Check LOS....
				if (SoldierTo3DLocationLineOfSightTest(s, cur->sGridNo, cur->bLevel, 3, sDistVisible, TRUE))
				{
					if (sDist <= PASSING_ITEM_DISTANCE_NOTOKLIFE)
					{
						gfSMDisableForItems = FALSE;
						break; // found one, no need to keep looking
					}
				}
			}
		}
	}
	else
	{
		const SOLDIERTYPE* src = gpItemPointerSoldier;
		if (src == nullptr)
		{
			src = GetSelectedMan();
			if (src == nullptr)
			{
				gfSMDisableForItems = FALSE;
				return;
			}
		}

		// OK buddy, check our currently selected merc and disable/enable if not close enough...
		if (cur == src)
		{
			gfSMDisableForItems = FALSE;
			return;
		}

		const INT16 sDestGridNo = cur->sGridNo;
		const INT8  bDestLevel  = cur->bLevel;

		// Get distance....
		const INT16 sDist = PythSpacesAway(src->sGridNo, sDestGridNo);

		// is he close enough to see that gridno if he turns his head?
		const INT16 sDistVisible = DistanceVisible(src, DIRECTION_IRRELEVANT, DIRECTION_IRRELEVANT, sDestGridNo, bDestLevel);

		// Check LOS....
		if (SoldierTo3DLocationLineOfSightTest(src, sDestGridNo, bDestLevel, 3, sDistVisible, TRUE))
		{
			// UNCONSCIOUS GUYS ONLY 1 tile AWAY
			if (cur->bLife < CONSCIOUSNESS)
			{
				if (sDist <= PASSING_ITEM_DISTANCE_NOTOKLIFE)
				{
					gfSMDisableForItems = FALSE;
				}
			}
			else if (sDist <= PASSING_ITEM_DISTANCE_OKLIFE)
			{
				gfSMDisableForItems = FALSE;
			}
		}
	}
}

void SetSMPanelCurrentMerc(SOLDIERTYPE* s)
{
	gSelectSMPanelToMerc = nullptr;

	// a question about another merc's pockets does not follow the panel
	TacticalInventory().Answer();

	gpSMCurrentMerc = s;

	// Disable all faces
	SetAllAutoFacesInactive( );

	// Turn off compat ammo....
	if ( gpItemPointer == nullptr )
	{
		HandleCompatibleAmmoUI(s, HANDPOS, FALSE);
		gfCheckForMouseOverItem = FALSE;
	}
	else
	{
		// Turn it all false first....
		InternalHandleCompatibleAmmoUI(s, gpItemPointer, FALSE);
		InternalHandleCompatibleAmmoUI(s, gpItemPointer, TRUE);
	}

	// Remove item desc panel if one up....
	if ( gfInItemDescBox )
	{
		DeleteItemDescriptionBox( );
	}

	if ( gfInItemPickupMenu )
	{
		gfSMDisableForItems = TRUE;
	}
	else
	{
		if (gpItemPointer || guiCurrentScreen == SHOPKEEPER_SCREEN || s->bLife < OKLIFE)
		{
			CheckForDisabledForGiveItem( );
		}
		else
		{
			gfSMDisableForItems = FALSE;
		}
	}

	if (gpItemPointer != nullptr) ReevaluateItemHatches(s, FALSE);

	DisableInvRegions( gfSMDisableForItems );

	fInterfacePanelDirty = DIRTYLEVEL2;


	ShopPanelRefresh();

}

void ReevaluateItemHatches(SOLDIERTYPE* pSoldier, BOOLEAN fAllValid)
{
	INT32 cnt;

	// if there's an item in the cursor and we're not supposed to just make them all valid
	if ( ( gpItemPointer != nullptr ) && !fAllValid )
	{
		// check all inventory positions and mark the ones where cursor item won't fit as invalid
		for ( cnt = 0; cnt < NUM_INV_SLOTS; cnt++ )
		{
			gbInvalidPlacementSlot[ cnt ] = !CanItemFitInPosition( pSoldier, gpItemPointer, (INT8)cnt, FALSE );

			// !!! ATTACHING/MERGING ITEMS IN MAP SCREEN IS NOT SUPPORTED !!!
			// CJC: seems to be supported now...
			//if( guiCurrentScreen != MAP_SCREEN )
			{
				// Check attachments, override to valid placement if valid merge...
				if ( ValidAttachment( gpItemPointer->usItem, pSoldier->inv[ cnt ].usItem ) )
				{
					gbInvalidPlacementSlot[ cnt ] = FALSE;
				}

				if ( ValidMerge( gpItemPointer->usItem, pSoldier->inv[ cnt ].usItem ) )
				{
					gbInvalidPlacementSlot[ cnt ] = FALSE;
				}
			}
		}
	}
	else
	{
		// mark all inventory positions as valid
		for ( cnt = 0; cnt < NUM_INV_SLOTS; cnt++ )
		{
			gbInvalidPlacementSlot[ cnt ] = FALSE;
		}
	}

	fInterfacePanelDirty = DIRTYLEVEL2;
}

void EnableSMPanelButtons(BOOLEAN fEnable, BOOLEAN fFromItemPickup)
{
	if ( fFromItemPickup )
	{
		// If we have the item pointer up (or the pick-up menu is open) the squad's buttons are off
		// CJC Dec 4 2002: or if item pickup menu is up
		LockTacticalButtons( gpItemPointer != nullptr || gfInItemPickupMenu );
		fInterfacePanelDirty = DIRTYLEVEL2;
	}

	if ( gsCurInterfacePanel != SM_PANEL ) return;

	if ( fFromItemPickup )
	{
		// If we have the item pointer up...
		if ( gpItemPointer != nullptr )
		{
			ReevaluateItemHatches( gpSMCurrentMerc, fEnable );

			// Turn it all false first....
			InternalHandleCompatibleAmmoUI( gpSMCurrentMerc, gpItemPointer, FALSE );
			InternalHandleCompatibleAmmoUI( gpSMCurrentMerc, gpItemPointer, TRUE );

			gfCheckForMouseOverItem = FALSE;

			// Highlight guys ....
			HandleAnyMercInSquadHasCompatibleStuff(gpItemPointer);
		}
		else
		{
			gfCheckForMouseOverItem = FALSE;

			HandleAnyMercInSquadHasCompatibleStuff(nullptr);
		}

		if ( fEnable )
		{
			ReevaluateItemHatches( gpSMCurrentMerc, fEnable );
		}

		fInterfacePanelDirty = DIRTYLEVEL2;
	}

	if ( fEnable )
	{
		// only enable the following if NOT in shopkeeper's interface
		if (guiCurrentScreen != SHOPKEEPER_SCREEN)
		{
			gfSMDisableForItems = FALSE;
			DisableInvRegions( gfSMDisableForItems );
		}
	}

	// the legacy single-merc panel, which only the shopkeeper still shows, has buttons of its own
	ShopPanelEnableButtons(fEnable, fFromItemPickup);
}




bool NailsVestBlocks(SOLDIERTYPE const* const s, UINT32 const uiHandPos, UINT16 const usReplaceItem)
{
	if (s->ubProfile != NAILS) return false;
	if (uiHandPos != VESTPOS)  return false;

	switch (usReplaceItem)
	{
		case LEATHER_JACKET:
		case LEATHER_JACKET_W_KEVLAR:
		case LEATHER_JACKET_W_KEVLAR_18:
		case LEATHER_JACKET_W_KEVLAR_Y:
		case COMPOUND18:
		case JAR_QUEEN_CREATURE_BLOOD:
			return false;

		default:
			return true;
	}
}


BOOLEAN HandleNailsVestFetish(const SOLDIERTYPE* const s, const UINT32 uiHandPos, const UINT16 usReplaceItem)
{
	if (!NailsVestBlocks(s, uiHandPos, usReplaceItem)) return FALSE;
	TacticalCharacterDialogue(s, 61);
	return TRUE;
}


// ---------------------------------------------------------------------------------------------------------------
// Clicks on a pocket (issue #317). The inventory core (Equipment/InventoryCore.h) decides what a click means; the
// functions below fill its inputs from the soldiers and apply the verdict through the legacy primitives
// (PlaceObject, BeginItemPointer, CleanUpStack, ...). The shopkeeper's special cases stay here, keyed on the screen.

namespace
{
	using Equipment::InvWhy;

	InventoryOutcome Refusal(InvWhy const why, UINT16 const item = NOTHING)
	{
		InventoryOutcome o;
		o.ok     = false;
		o.action = "refused";
		o.why    = Equipment::Describe(why);
		o.item   = item;
		RecordInventoryOutcome(o);
		return o;
	}

	InventoryOutcome Done(char const* const action, UINT16 const item, int const apFrom = 0, int const apTo = 0)
	{
		InventoryOutcome o;
		o.ok     = true;
		o.action = action;
		o.item   = item;
		o.apFrom = apFrom;
		o.apTo   = apTo;
		RecordInventoryOutcome(o);
		return o;
	}

	void Raise(Observable<InventoryMove const&>& event, SOLDIERTYPE const* const s, int const slot, UINT16 const item,
		int const count, char const* const action, int const apFrom = 0, int const apTo = 0)
	{
		InventoryMove m;
		m.merc   = s ? s->ubID : -1;
		m.slot   = slot;
		m.item   = item;
		m.count  = count;
		m.action = action;
		m.apFrom = apFrom;
		m.apTo   = apTo;
		event(m);
	}

	/** What a refusal tells the player: the legacy messages and Nails' line. */
	InventoryOutcome Refused(SOLDIERTYPE* const s, int const slot, Equipment::PlaceVerdict const& v, UINT16 const item)
	{
		switch (v.why)
		{
			case InvWhy::NoAP:
			case InvWhy::NoAPTarget:
				ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_UI_FEEDBACK, TacticalStr[NOT_ENOUGH_APS_STR]);
				break;

			case InvWhy::NailsVest:
				if (v.fetish) TacticalCharacterDialogue(s, 61);
				break;

			case InvWhy::DoesNotFit:
				// placing says why (the pocket's rule) in a message, as the legacy placement did
				if (gpItemPointer) CanItemFitInPosition(s, gpItemPointer, static_cast<INT8>(slot), TRUE);
				break;

			default: break;
		}
		return Refusal(v.why, item);
	}

	/** Puts the hand down into @a pos of @a s through PlaceObject and charges the passing AP. */
	bool ApplyPlacement(SOLDIERTYPE* const s, UINT8 const pos, UINT16 const oldItem, UINT16 const newItem,
		int const apFrom, int const apTo)
	{
		Raise(BeforeInventoryMove, s, pos, newItem, gpItemPointer ? gpItemPointer->ubNumberOfObjects : 0, "put", apFrom, apTo);

		if (!PlaceObject(s, pos, gpItemPointer)) return false;

		if (apFrom != 0 && gpItemPointerSoldier) DeductPoints(gpItemPointerSoldier, apFrom, 0);
		if (apTo   != 0)                         DeductPoints(s, apTo, 0);

		HandleTacticalEffectsOfEquipmentChange(s, pos, oldItem, newItem);

		fInterfacePanelDirty = DIRTYLEVEL2;

		// the hand is empty now
		if (gpItemPointer->ubNumberOfObjects == 0) EndItemPointer();

		if (gpItemPointerSoldier != s)
		{
			ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_INTERFACE,
				st_format_printf(pMessageStrings[MSG_ITEM_PASSED_TO_MERC], GCM->getItem(newItem)->getShortName(), s->name));
		}

		// the soldier the hand belongs to is the one it was put down on
		gpItemPointerSoldier = s;

		if (gpItemPointer != nullptr) ReevaluateItemHatches(s, FALSE);

		// set the cursor back to normal mode
		guiPendingOverrideEvent = A_CHANGE_TO_MOVE;

		Raise(OnInventoryMoved, s, pos, newItem, 0, "put", apFrom, apTo);
		return true;
	}

	void CleanUpPocket(SOLDIERTYPE* const s, UINT8 const pos, bool const withHand)
	{
		CleanUpStack(&s->inv[pos], withHand ? gpItemPointer : nullptr);
		if (withHand && gpItemPointer->ubNumberOfObjects == 0) EndItemPointer();
	}

	bool IsAttachHostPocket(UINT32 const pos)
	{
		return pos == HANDPOS || pos == SECONDHANDPOS || pos == HELMETPOS || pos == VESTPOS || pos == LEGPOS;
	}
}


static void MergeMessageBoxCallBack(MessageBoxReturnValue);


InventoryOutcome InventoryAnswer(bool const yes)
{
	Equipment::InventoryCore& core = TacticalInventory();
	if (!core.Asking()) return LastInventoryOutcome();

	Equipment::Question const q = core.Answer();

	// The question is only good while what it asked about is still there: the same item in the hand,
	// the same item in the pocket or the sheet.
	bool const handIsAsked = gpItemPointer && gpItemPointer->usItem == q.held;

	if (q.kind == Equipment::QuestionKind::PermanentAttachment)
	{
		// "this attachment cannot be taken off again": the sheet mounts it
		UINT16 const item = q.held;
		if (!yes) return Done("declined", item);
		if (!handIsAsked || !InItemDescriptionBox() || !ItemDescIsOpenOn(q.host)) return Refusal(InvWhy::HandEmpty, item);
		ItemDescConfirmPermanentAttachment();
		return Done("attach", item);
	}

	if (q.merc < 0 || q.merc >= TOTAL_SOLDIERS || !gpItemPointer) return Refusal(InvWhy::HandEmpty);

	SOLDIERTYPE* const s = &GetMan(static_cast<UINT>(q.merc));
	if (q.kind != Equipment::QuestionKind::Merge) return Refusal(InvWhy::None);
	if (q.slot < 0 || q.slot >= NUM_INV_SLOTS || !handIsAsked || s->inv[q.slot].usItem != q.host)
		return Refusal(InvWhy::HandEmpty, q.held);

	if (yes)
	{
		UINT16 const item = gpItemPointer->usItem;
		Raise(BeforeInventoryMove, s, q.slot, item, gpItemPointer->ubNumberOfObjects, "merge");
		AttachObject(gpItemPointerSoldier ? gpItemPointerSoldier : s, &s->inv[q.slot], gpItemPointer);

		// re-evaluate repairs
		gfReEvaluateEveryonesNothingToDo = TRUE;

		if (gpItemPointer->usItem == NOTHING)
		{
			// merge item consumed
			EndItemPointer();
			fInterfacePanelDirty = DIRTYLEVEL2;
		}
		Raise(OnInventoryMoved, s, q.slot, item, 0, "merge");
		return Done("merge", item);
	}

	UINT16 const newItem = gpItemPointer->usItem;
	if (ApplyPlacement(s, static_cast<UINT8>(q.slot), s->inv[q.slot].usItem, newItem, q.apFrom, q.apTo))
		return Done("put", newItem, q.apFrom, q.apTo);
	return Refusal(InvWhy::None, newItem);
}


static void MergeMessageBoxCallBack(MessageBoxReturnValue const ubExitValue)
{
	InventoryAnswer(ubExitValue == MSG_BOX_RETURN_YES);
}


/** Left click on a pocket: pick the item up, or put the hand down into it. */
static InventoryOutcome SlotPrimary(SOLDIERTYPE* const s, UINT32 const pos, bool const ctrl)
{
	Equipment::InventoryCore& core = TacticalInventory();
	bool const ski = guiCurrentScreen == SHOPKEEPER_SCREEN;
	Equipment::InvParty const me = InventoryPartyOf(s);

	// If we do not have an item in hand, start moving it
	if (gpItemPointer == nullptr)
	{
		UINT16 const item = s->inv[pos].usItem;
		if (item == NOTHING) return Refusal(InvWhy::NothingHere);
		if (!me.inReach) return Refusal(InvWhy::OutOfReach, item);

		SelectSoldier(s, SELSOLDIER_NONE);

		// Nails keeps his vest on; Ctrl gathers the pocket's stack
		Equipment::TakeVerdict const t = core.PlanTake(me, static_cast<int>(pos), item, ctrl);
		if (t.kind == Equipment::TakeKind::Refused)
		{
			if (t.why == InvWhy::NailsVest) TacticalCharacterDialogue(s, 61);
			return Refusal(t.why, item);
		}
		if (t.kind == Equipment::TakeKind::CleanUp)
		{
			CleanUpPocket(s, static_cast<UINT8>(pos), false);
			return Done("cleanup", item);
		}

		Raise(BeforeInventoryMove, s, static_cast<int>(pos), item, s->inv[pos].ubNumberOfObjects, "take");

		// Turn off new item glow!
		s->bNewItemCount[pos] = 0;

		// move item into the mouse cursor
		BeginItemPointer(s, static_cast<UINT8>(pos));

		if (ski)
		{
			// pick up item from regular inventory slot into cursor OR try to sell it
			// ( unless CTRL is held down )
			BeginSkiItemPointer(PLAYERS_INVENTORY, static_cast<INT8>(pos), !ctrl);
		}

		HandleTacticalEffectsOfEquipmentChange(s, pos, item, NOTHING);

		Raise(OnInventoryMoved, s, static_cast<int>(pos), item, 0, "take");
		return Done("take", item);
	}

	// item in cursor
	UINT16 const usOldItemIndex = s->inv[pos].usItem;
	UINT16 const usNewItemIndex = gpItemPointer->usItem;

	Equipment::PlaceVerdict const v = core.PlanPlace(InventoryHand(), me, static_cast<int>(pos), usOldItemIndex, IsAttachHostPocket(pos), ctrl);
	switch (v.kind)
	{
		case Equipment::PlaceKind::Refused:
			return Refused(s, static_cast<int>(pos), v, usNewItemIndex);

		case Equipment::PlaceKind::Attach:
			// it's an attempt to attach; bring up the inventory panel
			if (!InItemDescriptionBox())
			{
				InitItemDescriptionBox(s, static_cast<UINT8>(pos), SM_ITEMDESC_START_X, SM_ITEMDESC_START_Y, 0);
			}
			return Done("attach", usNewItemIndex);

		case Equipment::PlaceKind::AskMerge:
		{
			// the question lives in the core; the native HUD shows it, the legacy HUD and the shopkeeper use a box
			Equipment::Question q;
			q.kind   = Equipment::QuestionKind::Merge;
			q.merc   = s->ubID;
			q.slot   = static_cast<int>(pos);
			q.held   = usNewItemIndex;
			q.host   = usOldItemIndex;
			q.apFrom = v.apFrom;
			q.apTo   = v.apTo;
			core.Ask(q);

			if (ski)
			{
				// the only way to merge items is to pick them up. In SKI when you pick up an item, the cursor is
				// locked in a region, free it up.
				FreeMouseCursor();
				DoMessageBox(MSG_BOX_BASIC_STYLE, g_langRes->Message[STR_MERGE_ITEMS], SHOPKEEPER_SCREEN, MSG_BOX_FLAG_YESNO, MergeMessageBoxCallBack, nullptr);
			}
			else if (!NativeUI::TacticalHudActive())
			{
				DoMessageBox(MSG_BOX_BASIC_STYLE, g_langRes->Message[STR_MERGE_ITEMS], GAME_SCREEN, MSG_BOX_FLAG_YESNO, MergeMessageBoxCallBack, nullptr);
			}
			InventoryOutcome o;
			o.ok     = true;
			o.action = "asked";
			o.item   = usNewItemIndex;
			RecordInventoryOutcome(o);
			return o;
		}

		case Equipment::PlaceKind::CleanUp:
			CleanUpPocket(s, static_cast<UINT8>(pos), true);
			return Done("cleanup", usNewItemIndex);

		case Equipment::PlaceKind::Put:
			break;
	}

	// remember the item type currently in the item pointer
	UINT16 const usItemPrevInItemPointer = gpItemPointer->usItem;

	BOOLEAN fNewItem = FALSE;
	if (ski)
	{
		// If it's just been purchased or repaired, mark it as a "new item"
		fNewItem = (gMoveingItem.uiFlags & (ARMS_INV_JUST_PURCHASED | ARMS_INV_ITEM_REPAIRED)) != 0;
	}

	// try to place the item in the cursor into this inventory slot
	if (!ApplyPlacement(s, static_cast<UINT8>(pos), usOldItemIndex, usNewItemIndex, v.apFrom, v.apTo))
		return Refusal(InvWhy::None, usNewItemIndex);

	// it worked! if we're in the SKI...
	if (ski)
	{
		SetNewItem(s, static_cast<UINT8>(pos), fNewItem);

		if (gpItemPointer == nullptr)
		{
			// the cursor is now empty: clean up
			gMoveingItem = INVENTORY_IN_SLOT{};
			SetSkiCursor(CURSOR_NORMAL);
		}
		else if (usItemPrevInItemPointer != gpItemPointer->usItem)
		{
			// pick up item swapped out of inventory slot into cursor (don't try to sell)
			BeginSkiItemPointer(PLAYERS_INVENTORY, -1, FALSE);
		}
		// otherwise leave the cursor as is: more items were picked up at once than fit in this slot,
		// the remainder stays in the cursor to be put down elsewhere
	}
	return Done("put", usNewItemIndex, v.apFrom, v.apTo);
}


/** Right click on a pocket: the stack popup for a stack, the item's description otherwise. */
static InventoryOutcome SlotSecondary(SOLDIERTYPE* const s, UINT32 const pos)
{
	Equipment::InventoryCore& core = TacticalInventory();
	UINT16 const item = s->inv[pos].usItem;
	if (item == NOTHING) return Refusal(InvWhy::NothingHere);
	if (!InventoryPartyOf(s).inReach) return Refusal(InvWhy::OutOfReach, item);

	// Turn off new item glow!
	s->bNewItemCount[pos] = 0;

	// Some global stuff here - for esc, etc
	// Check for # of slots in item
	Equipment::ActivateKind const k = core.PlanActivate(item, s->inv[pos].ubNumberOfObjects,
		ItemSlotLimit(item, static_cast<UINT8>(pos)), guiCurrentScreen != MAP_SCREEN);
	if (k == Equipment::ActivateKind::OpenStack)
	{
		if (!InItemStackPopup())
		{
			InitItemStackPopup(s, static_cast<UINT8>(pos), SM_ITEMDESC_START_X, INV_INTERFACE_START_Y,
				SM_ITEMDESC_WIDTH, SCREEN_HEIGHT - INV_INTERFACE_START_Y);
		}
		return Done("stack", item);
	}

	if (!InItemDescriptionBox())
	{
		InitItemDescriptionBox(s, static_cast<UINT8>(pos), SM_ITEMDESC_START_X, SM_ITEMDESC_START_Y, 0);
	}
	return Done("describe", item);
}


static bool ShopHatched(UINT32 pos);

/** What putting the hand down in pocket @a slot of @a s would do: the verdict of the click, without the click. */
SlotDrop InventorySlotDrop(SOLDIERTYPE* const s, int const slot)
{
	SlotDrop d;
	if (!s || slot < 0 || slot >= NUM_INV_SLOTS || fInMapMode || !gpItemPointer)
	{
		d.why = InvWhy::HandEmpty;
		return d;
	}
	if (s != gpSMCurrentMerc || ShopHatched(static_cast<UINT32>(slot)))
	{
		d.why = InvWhy::OutOfReach;
		return d;
	}
	Equipment::PlaceVerdict const v = TacticalInventory().PlanPlace(InventoryHand(), InventoryPartyOf(s), slot, s->inv[slot].usItem,
		IsAttachHostPocket(static_cast<UINT32>(slot)), false);
	d.kind   = v.kind;
	d.why    = v.why;
	d.apFrom = v.apFrom;
	d.apTo   = v.apTo;
	d.ok     = v.kind != Equipment::PlaceKind::Refused;
	return d;
}



InventoryOutcome InventorySlotClick(SOLDIERTYPE* const merc, int const slot, bool const right, bool const ctrl)
{
	if (!merc || slot < 0 || slot >= NUM_INV_SLOTS || fInMapMode) return Refusal(InvWhy::NothingHere);
	// the pockets are the single-merc panel's: they act on the merc it shows
	if (merc != gpSMCurrentMerc) return Refusal(InvWhy::OutOfReach);
	if (ShopHatched(static_cast<UINT32>(slot))) return Refusal(InvWhy::NothingHere);
	// a click answers nothing: a question still open is dropped
	TacticalInventory().Answer();
	return right ? SlotSecondary(merc, static_cast<UINT32>(slot)) : SlotPrimary(merc, static_cast<UINT32>(slot), ctrl);
}


static bool ShopHatched(UINT32 const pos)
{
	// in the shop keeper interface: is this inventory slot hatched out?
	// It means that item is a copy of one in the player's offer area, so we treat it as if the slot was empty
	// (ignore); with the cursor full we still ignore the click, because handling swaps in this situation would be
	// ugly, we'd have to the the swap, then make the bOwnerSlot of the item just picked up a -1 in its offer area spot.
	return guiCurrentScreen == SHOPKEEPER_SCREEN && ShouldSoldierDisplayHatchOnItem(gpSMCurrentMerc->ubProfile, static_cast<INT16>(pos));
}



void BeginKeyPanelFromKeyShortcut(void)
{
	SOLDIERTYPE *pSoldier = nullptr;
	INT16 sStartYPosition = 0;
	INT16 sWidth = 0, sHeight = 0;

	if ( gsCurInterfacePanel != SM_PANEL )
	{
		return;
	}

	if ( gpSMCurrentMerc == nullptr )
	{
		return;
	}

	if ( gfInKeyRingPopup )
	{
		return;
	}


	sStartYPosition = INV_INTERFACE_START_Y;
	sWidth  = SCREEN_WIDTH;
	sHeight = SCREEN_HEIGHT - INV_INTERFACE_START_Y;
	pSoldier = gpSMCurrentMerc;

	//if we are in the shop keeper interface
	if (guiCurrentScreen == SHOPKEEPER_SCREEN) return;

	InitKeyRingPopup( pSoldier, 0, sStartYPosition, sWidth, sHeight );
}




void KeyRingItemPanelButtonCallback(MOUSE_REGION* pRegion, UINT32 iReason)
{
	if (iReason & MSYS_CALLBACK_REASON_POINTER_UP) OpenKeyRing();
}


/** The key ring button: the popup with the keys of the merc the panel shows. */
static void OpenKeyRing()
{
	SOLDIERTYPE *pSoldier = nullptr;
	INT16 sStartYPosition = 0;
	INT16 sWidth = 0, sHeight = 0;

	if( guiCurrentScreen == MAP_SCREEN )
	{
		pSoldier = GetSelectedInfoChar();
		if (pSoldier == nullptr) return;

		sStartYPosition = MAP_START_KEYRING_Y;
		sWidth = 261;
		sHeight = ( 359 - 107 );
	}
	else
	{
		if ( gpSMCurrentMerc == nullptr )
		{
			return;
		}

		sStartYPosition = INV_INTERFACE_START_Y;
		sWidth   = SCREEN_WIDTH;
		sHeight  = SCREEN_HEIGHT - INV_INTERFACE_START_Y;
		pSoldier = gpSMCurrentMerc;
	}

	//if we are in the shop keeper interface
	if (guiCurrentScreen == SHOPKEEPER_SCREEN) return;

	{
		if( guiCurrentScreen == MAP_SCREEN )
		{
			// shade the background
			FRAME_BUFFER->ShadowRect(MAPLEFT_X + 0, MAPTOP_Y + 107,
							MAPLEFT_X + 261, MAPTOP_Y + 359);
			InvalidateRegion(MAPLEFT_X + 0, MAPTOP_Y + 107, MAPLEFT_X + 261, MAPTOP_Y + 359);
			InitKeyRingPopup(pSoldier, MAPLEFT_X + 0, sStartYPosition, sWidth, sHeight);
		}
		else
		{
			InitKeyRingPopup( pSoldier, INTERFACE_START_X, sStartYPosition, sWidth, sHeight );
		}
	}
}

void KeyRingSlotInvClickCallbackPrimary( MOUSE_REGION * pRegion, UINT32 iReason )
{
	// Copyies of values
	UINT16 usOldItemIndex;
	UINT32 uiKeyRing = MSYS_GetRegionUserData( pRegion, 0 );

	//if we are in the shop keeper interface
	if (guiCurrentScreen == SHOPKEEPER_SCREEN)
	{
		INVENTORY_IN_SLOT InvSlot;

		if( gMoveingItem.sItemIndex == 0 )
		{
			//Delete the contents of the item cursor
			gMoveingItem = INVENTORY_IN_SLOT{};
		}
		else
		{
			InvSlot = INVENTORY_IN_SLOT{};

			// Return if empty
			//if ( gpSMCurrentMerc->inv[ uiHandPos ].usItem == NOTHING )
			//	return;


			// Fill out the inv slot for the item
			//InvSlot.sItemIndex = gpSMCurrentMerc->inv[ uiHandPos ].usItem;
			//InvSlot.ubNumberOfItems = gpSMCurrentMerc->inv[ uiHandPos ].ubNumberOfObjects;
			//InvSlot.ubItemQuality = gpSMCurrentMerc->inv[ uiHandPos ].bGunStatus;
			//InvSlot.ItemObject = gpSMCurrentMerc->inv[uiHandPos];
			//InvSlot.ubLocationOfObject = PLAYERS_INVENTORY;

			//InvSlot.ubIdOfMercWhoOwnsTheItem = gpSMCurrentMerc->ubProfile;


			//Add the item to the Players Offer Area
			//AddItemToPlayersOfferArea( gpSMCurrentMerc->ubProfile, &InvSlot, (UINT8)uiHandPos );

			// Dirty
			fInterfacePanelDirty = DIRTYLEVEL2;
		}
		return;
	}

	// If we do not have an item in hand, start moving it
	if ( gpItemPointer == nullptr )
	{
		// Return if empty
		if (!gpItemPopupSoldier->pKeyRing[uiKeyRing].isValid())
			return;

		// If our flags are set to do this, gofoit!
		if (!fInMapMode)
		{
			SelectSoldier(gpItemPopupSoldier, SELSOLDIER_NONE);
		}

		usOldItemIndex =  ( UINT16 )uiKeyRing ;

		BeginKeyRingItemPointer( gpItemPopupSoldier, (UINT8)usOldItemIndex );
		//BeginItemPointer( gpSMCurrentMerc, (UINT8)uiHandPos );

	}
	else
	{
		BOOLEAN fOKToGo = FALSE;
		BOOLEAN fDeductPoints = FALSE;

		// return if item is not a key
		if (GCM->getItem(gpItemPointer->usItem)->getItemClass() != IC_KEY) {
			return;
		}

		// ATE: OK, get source, dest guy if different... check for and then charge appropriate APs
		if (gpItemPointerSoldier == nullptr ||
			gpItemPointerSoldier == GetSelectedInfoChar())
		{
			// We are doing this ourselve, continue
			fOKToGo = TRUE;
		}
		else
		{
			// These guys are different....
			fDeductPoints = TRUE;

			// First check points for src guy
			if ( gpItemPointerSoldier->bLife >= CONSCIOUSNESS )
			{
				if ( EnoughPoints( gpItemPointerSoldier, 2, 0, TRUE ) )
				{
					fOKToGo = TRUE;
				}
			}
			else
			{
				fOKToGo = TRUE;
			}

			// Should we go on?
			if ( fOKToGo )
			{
				if ( gpSMCurrentMerc->bLife >= CONSCIOUSNESS )
				{
					if ( EnoughPoints( gpSMCurrentMerc, 2, 0, TRUE ) )
					{
						fOKToGo = TRUE;
					}
					else
					{
						fOKToGo = FALSE;
					}
				}
			}
		}

		if ( fOKToGo )
		{
			//usOldItemIndex = gpSMCurrentMerc->inv[ uiHandPos ].usItem;
			//usNewItemIndex = gpItemPointer->usItem;

			if ( gpItemPopupSoldier->pKeyRing[ uiKeyRing ].ubKeyID == INVALID_KEY_NUMBER || gpItemPopupSoldier->pKeyRing[ uiKeyRing ].ubKeyID == gpItemPointer->ubKeyID)
			{
				// Try to place here
				INT32 const iNumberOfKeysTaken = AddKeysToSlot(*gpItemPopupSoldier, (INT8)uiKeyRing, *gpItemPointer);
				if (iNumberOfKeysTaken != 0)
				{

					if ( fDeductPoints )
					{
						// Deduct points
						if ( gpItemPointerSoldier->bLife >= CONSCIOUSNESS )
						{
							DeductPoints( gpItemPointerSoldier,  2, 0 );
						}
						if ( gpItemPopupSoldier->bLife >= CONSCIOUSNESS )
						{
							DeductPoints( gpItemPopupSoldier,  2, 0 );
						}
					}

					// Dirty
					fInterfacePanelDirty = DIRTYLEVEL2;

					gpItemPointer->ubNumberOfObjects -= ( UINT8 )iNumberOfKeysTaken;

					// Check if it's the same now!
					if ( gpItemPointer->ubNumberOfObjects == 0 )
					{
						if (fInMapMode)
						{
							MAPEndItemPointer();
						}
						else
						{
							EndItemPointer();
						}
					}

					// Setup a timer....
					//guiMouseOverItemTime = GetJA2Clock( );
					//gfCheckForMouseOverItem = TRUE;
					//gbCheckForMouseOverItemPos = (INT8)uiHandPos;

				}
			}
			else
			{
				// Swap!
				SwapKeysToSlot(*gpItemPopupSoldier, (INT8)uiKeyRing, *gpItemPointer);

				if ( fDeductPoints )
				{
					// Deduct points
					if ( gpItemPointerSoldier && gpItemPointerSoldier->bLife >= CONSCIOUSNESS )
					{
						DeductPoints( gpItemPointerSoldier,  2, 0 );
					}
					if ( gpSMCurrentMerc->bLife >= CONSCIOUSNESS )
					{
						DeductPoints( gpSMCurrentMerc,  2, 0 );
					}
				}

				// Dirty
				fInterfacePanelDirty = DIRTYLEVEL2;

			}
		}
	}

	UpdateItemHatches();
}

void KeyRingSlotInvClickCallbackSecondary( MOUSE_REGION * pRegion, UINT32 iReason )
{
	UINT32 uiKeyRing = MSYS_GetRegionUserData( pRegion, 0 );

	// Return if empty
	if (!gpItemPopupSoldier->pKeyRing[uiKeyRing].isValid())
	{
		DeleteKeyRingPopup( );
		fTeamPanelDirty = TRUE;
		return;
	}
	// Some global stuff here - for esc, etc
	// Check for # of slots in item
	if ( !InItemDescriptionBox( ) )
	{
		if (fInMapMode)
		{
			//InitKeyItemDescriptionBox(gpItemPopupSoldier, (UINT8)uiKeyRing, MAP_ITEMDESC_START_X, MAP_ITEMDESC_START_Y);
		}
		else
		{
			InitKeyItemDescriptionBox(gpItemPopupSoldier, (UINT8)uiKeyRing, SM_ITEMDESC_START_X, SM_ITEMDESC_START_Y);
		}
	}
}

MOUSE_CALLBACK KeyRingSlotInvClickCallback = MouseCallbackPrimarySecondary(KeyRingSlotInvClickCallbackPrimary, KeyRingSlotInvClickCallbackSecondary, MSYS_NO_CALLBACK, true);



void InventoryCashButton()
{
	{
		//If the current merc is to far away, dont allow anything to be done
		if( gfSMDisableForItems )
			return;

		//if the player has an item in his hand,
		if( gpItemPointer != nullptr )
		{
			//and the item is money
			if( GCM->getItem(gpItemPointer->usItem)->getItemClass() == IC_MONEY )
			{
				ST::string zText;
				ST::string zMoney;

				// Make sure we go back to movement mode...
				guiPendingOverrideEvent = A_CHANGE_TO_MOVE;
				HandleTacticalUI( );

				zMoney = SPrintMoney(gpItemPointer->uiMoneyAmount);

				//ask the user if they are sure they want to deposit the money
				zText = st_format_printf(gzMoneyWithdrawMessageText[ CONFIRMATION_TO_DEPOSIT_MONEY_TO_ACCOUNT ], zMoney);

				if( guiCurrentScreen == SHOPKEEPER_SCREEN )
				{
					//if we are in the shop keeper interface, free the cursor
					FreeMouseCursor();
					DoMessageBox(MSG_BOX_BASIC_STYLE, zText, SHOPKEEPER_SCREEN, MSG_BOX_FLAG_YESNO, ConfirmationToDepositMoneyToPlayersAccount, nullptr);
				}
				else
					DoMessageBox(MSG_BOX_BASIC_STYLE, zText, GAME_SCREEN, MSG_BOX_FLAG_YESNO, ConfirmationToDepositMoneyToPlayersAccount, nullptr);
			}
		}

		//else bring up the money item description box to remove money from the players account
		else
		{
			//set the flag indicating we are removing money from the players account
			gfAddingMoneyToMercFromPlayersAccount = TRUE;

			//create the temp object from the players account balance
			//if( LaptopSaveInfo.iCurrentBalance > MAX_MONEY_PER_SLOT )
			//	CreateMoney( MAX_MONEY_PER_SLOT, &gItemPointer );
			//else
				CreateMoney( LaptopSaveInfo.iCurrentBalance, &gItemPointer );

			InternalInitItemDescriptionBox(&gItemPointer, SM_ITEMDESC_START_X, SM_ITEMDESC_START_Y,
							0, gpSMCurrentMerc);
		}
	}
}

static void ConfirmationToDepositMoneyToPlayersAccount(MessageBoxReturnValue const ubExitValue)
{
	if ( ubExitValue == MSG_BOX_RETURN_YES )
	{
		//add the money to the players account
		AddTransactionToPlayersBook(MERC_DEPOSITED_MONEY_TO_PLAYER_ACCOUNT, gpSMCurrentMerc->ubProfile,
						GetWorldTotalMin(), gpItemPointer->uiMoneyAmount);

		EndItemPointer( );
		// remove contents of the moving item because object still is money and usable
		// you could add endlessly money to your bank account if not reset properly
		gMoveingItem = INVENTORY_IN_SLOT{};
		SetSkiCursor( CURSOR_NORMAL );
		// dirty shopkeeper
		gubSkiDirtyLevel = SKI_DIRTY_LEVEL2;


	}
}


void ReEvaluateDisabledINVPanelButtons(void)
{
	gfReEvaluateDisabledINVPanelButtons = TRUE;
}


void CheckForReEvaluateDisabledINVPanelButtons(void)
{
	// OK, if we currently have an item pointer up....
	if ( gfReEvaluateDisabledINVPanelButtons )
	{
		if ( gpItemPointer || gfInItemPickupMenu )
		{
			EnableSMPanelButtons( FALSE, TRUE );
		}
		else
		{
			EnableSMPanelButtons( TRUE, TRUE );
		}

		gfReEvaluateDisabledINVPanelButtons = FALSE;
	}
}


static void AbandonBoxingCallback(MessageBoxReturnValue const ubExitValue)
{
	if ( ubExitValue == MSG_BOX_RETURN_YES )
	{
		// ok, proceed!
		SetBoxingState( NOT_BOXING );
		gfEnteringMapScreen = TRUE;
	}
	// otherwise do nothing
}


void GoToMapScreenFromTactical(void)
{
	if ( gTacticalStatus.bBoxingState != NOT_BOXING )
	{
		// pop up dialogue asking whether the player wants to abandon the fight
		DoMessageBox(MSG_BOX_BASIC_STYLE, g_langRes->Message[STR_ABANDON_FIGHT], GAME_SCREEN,
				MSG_BOX_FLAG_YESNO, AbandonBoxingCallback, nullptr);
		return;
	}
	// ok, proceed!
	gfEnteringMapScreen = TRUE;
}

void HandleTacticalEffectsOfEquipmentChange(SOLDIERTYPE* pSoldier, UINT32 uiInvPos, UINT16 usOldItem, UINT16 usNewItem)
{
	// if in attached weapon mode and don't have weapon with GL attached in hand, reset weapon mode
	if (pSoldier->bWeaponMode == WM_ATTACHED &&
		FindAttachment(&(pSoldier->inv[HANDPOS]), UNDER_GLAUNCHER) == NO_SLOT)
	{
		pSoldier->bWeaponMode = WM_NORMAL;
		pSoldier->bDoBurst = FALSE;
	}

	// if he is loaded tactically
	if ( pSoldier->bInSector )
	{
		// If this is our main hand
		if ( uiInvPos == HANDPOS || uiInvPos == SECONDHANDPOS )
		{
			// check if we need to change animation!
			ReLoadSoldierAnimationDueToHandItemChange( pSoldier, usOldItem, usNewItem );
		}

		// if this is head gear
		if ( uiInvPos == HEAD1POS || uiInvPos == HEAD2POS )
		{
			// Could be because of GOGGLES change...  Re-create light...
			DeleteSoldierLight( pSoldier );
			PositionSoldierLight( pSoldier );
		}
	}
}

void LockTacticalButtons(BOOLEAN const fLock)
{
	gfDisableTacticalPanelButtons = fLock;
}


void UpdateInventoryView(DirtyLevel* const dirty_level)
{
	// Give him the view
	if (gSelectSMPanelToMerc) SetSMPanelCurrentMerc(gSelectSMPanelToMerc);

	if (gsCurInterfacePanel != SM_PANEL || !gpSMCurrentMerc) return;
	SOLDIERTYPE& s = *gpSMCurrentMerc;
	if (s.sGridNo == NOWHERE) return;

	// an item that was glowing has stopped (the native HUD draws the glow itself)
	for (UINT32 i = 0; i != NUM_INV_SLOTS; ++i)
	{
		if (s.bNewItemCount[i] == -2)
		{
			*dirty_level = DIRTYLEVEL2;
			s.bNewItemCount[i] = 0;
		}
	}

	if (InItemDescriptionBox()) HandleItemDescriptionBox(dirty_level);

	// Check for any newly added items we need
	if (s.fCheckForNewlyAddedItems)
	{
		// Startup any newly added items
		CheckForAnyNewlyAddedItems(&s);
		s.fCheckForNewlyAddedItems = FALSE;
	}

	CheckForReEvaluateDisabledINVPanelButtons();
}


void InventoryKeyRing()
{
	OpenKeyRing();
}
