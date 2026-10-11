#ifndef INVENTORY_ACTIONS_H
#define INVENTORY_ACTIONS_H

// What the tactical interface keeps of the legacy single-merc panel (issue #319): the merc the inventory view shows,
// whether his pockets are in reach, the effects of changing equipment, the key ring and the money button. The native
// HUD draws the view; the panel itself, with its regions and buttons, no longer exists in tactical.

#include "Interface.h"
#include "JA2Types.h"
#include "MouseSystem.h"

#define NEW_ITEM_CYCLE_COUNT			19
#define NEW_ITEM_CYCLES			4

#define PASSING_ITEM_DISTANCE_OKLIFE		3
#define PASSING_ITEM_DISTANCE_NOTOKLIFE	2

// The merc the single-merc view shows (SetSMPanelCurrentMerc), and the one it is going to show next frame.
extern SOLDIERTYPE* gpSMCurrentMerc;
extern SOLDIERTYPE* gSelectSMPanelToMerc;
// The pockets of the merc shown are out of reach of the item in hand (or a pick-up menu is open).
extern BOOLEAN gfSMDisableForItems;
// The game flow disables the tactical buttons (a message box, the overhead map).
extern BOOLEAN gfDisableTacticalPanelButtons;

extern BOOLEAN gfCheckForMouseOverItem;
extern UINT32  guiMouseOverItemTime;
extern INT8    gbCheckForMouseOverItemPos;

void SetSMPanelCurrentMerc(SOLDIERTYPE* s);
void CheckForDisabledForGiveItem(void);
void ReevaluateItemHatches(SOLDIERTYPE* s, BOOLEAN fEnable);
void EnableSMPanelButtons(BOOLEAN fEnable, BOOLEAN fFromItemPickup);
void ReEvaluateDisabledINVPanelButtons(void);
void CheckForReEvaluateDisabledINVPanelButtons(void);
/** The game flow switches the tactical buttons off (true) or on (false). */
void LockTacticalButtons(BOOLEAN fLock);
/** What the old single-merc panel did each frame besides drawing: the merc it is to show, the end of an item's
 * glow, the item sheet's deferred close, newly added items. */
void UpdateInventoryView(DirtyLevel* dirty_level);

void HandleTacticalEffectsOfEquipmentChange(SOLDIERTYPE* s, UINT32 uiInvPos, UINT16 usOldItem, UINT16 usNewItem);
BOOLEAN HandleNailsVestFetish(const SOLDIERTYPE* pSoldier, UINT32 uiHandPos, UINT16 usReplaceItem);

void GoToMapScreenFromTactical(void);

/** The key ring popup of the merc shown (K). */
void BeginKeyPanelFromKeyShortcut(void);
void KeyRingItemPanelButtonCallback(MOUSE_REGION* pRegion, UINT32 iReason);
extern MOUSE_CALLBACK KeyRingSlotInvClickCallback;

/** The HUD's cash button (deposit the money in the hand, or open the withdrawal). */
void InventoryCashButton();
/** The HUD's key ring button. */
void InventoryKeyRing();

#endif
