#ifndef __INTERFACE_ITEMS_H
#define __INTERFACE_ITEMS_H

#include "Button_System.h"
#include "Interface.h"
#include "MouseSystem.h"
#include "Soldier_Control.h"

#include "UILayout.h"
#include "VObject.h"

#include <string_theory/string>


struct ItemModel;


// DEFINES FOR ITEM SLOT SIZES IN PIXELS
#define BIG_INV_SLOT_WIDTH	61
#define BIG_INV_SLOT_HEIGHT	22
#define SM_INV_SLOT_WIDTH	30
#define SM_INV_SLOT_HEIGHT	23
#define VEST_INV_SLOT_WIDTH	43
#define VEST_INV_SLOT_HEIGHT	24
#define LEGS_INV_SLOT_WIDTH	43
#define LEGS_INV_SLOT_HEIGHT	24
#define HEAD_INV_SLOT_WIDTH	43
#define HEAD_INV_SLOT_HEIGHT	24


// Itempickup stuff
void InitializeItemPickupMenu(SOLDIERTYPE* pSoldier, INT16 sGridNo, ITEM_POOL* pItemPool, INT8 bZLevel);
void RenderItemPickupMenu(void);
void RemoveItemPickupMenu(void);
void SetItemPickupMenuDirty( BOOLEAN fDirtyLevel );
BOOLEAN HandleItemPickupMenu(void);


// FUNCTIONS FOR INTERFACEING WITH ITEM PANEL STUFF
void InitInvSlotInterface(INV_REGION_DESC const* pRegionDesc, INV_REGION_DESC const* pCamoRegion, MOUSE_CALLBACK INVMoveCallback, MOUSE_CALLBACK INVClickCallback, MOUSE_CALLBACK INVMoveCamoCallback, MOUSE_CALLBACK INVClickCamoCallback);
void ShutdownInvSlotInterface();
void HandleRenderInvSlots(SOLDIERTYPE const&, DirtyLevel);
void HandleNewlyAddedItems(SOLDIERTYPE&, DirtyLevel*);
void RenderInvBodyPanel(const SOLDIERTYPE* pSoldier, INT16 sX, INT16 sY);
void DisableInvRegions( BOOLEAN fDisable );

void DegradeNewlyAddedItems(void);
void CheckForAnyNewlyAddedItems( SOLDIERTYPE *pSoldier );


BOOLEAN HandleCompatibleAmmoUI(const SOLDIERTYPE* pSoldier, INT8 bInvPos, BOOLEAN fOn);


// THIS FUNCTION IS CALLED TO RENDER AN ITEM.
// uiBuffer - The Dest Video Surface - can only be FRAME_BUFFER or guiSAVEBUFFER
// pSoldier - used for determining whether burst mode needs display
// pObject  - Usually taken from pSoldier->inv[HANDPOS]
// sX, sY, Width, Height, - Will Center it in the Width
// fDirtyLevel  if == DIRTYLEVEL2 will render everything
//              if == DIRTYLEVEL1 will render bullets and status only
//
//  Last parameter used mainly for when mouse is over item
void INVRenderItem(SGPVSurface* uiBuffer, SOLDIERTYPE const* pSoldier, OBJECTTYPE const&, INT16 sX, INT16 sY, INT16 sWidth, INT16 sHeight, DirtyLevel, UINT8 ubStatusIndex, INT16 sOutlineColor);


extern BOOLEAN gfInItemDescBox;

BOOLEAN InItemDescriptionBox(void);
void InitItemDescriptionBox(SOLDIERTYPE* pSoldier, UINT8 ubPosition, INT16 sX, INT16 sY, UINT8 ubStatusIndex);
void InternalInitItemDescriptionBox(OBJECTTYPE* pObject, INT16 sX, INT16 sY, UINT8 ubStatusIndex, SOLDIERTYPE* pSoldier);
void InitKeyItemDescriptionBox(SOLDIERTYPE* pSoldier, UINT8 ubPosition, INT16 sX, INT16 sY);
void RenderItemDescriptionBox(void);
void HandleItemDescriptionBox(DirtyLevel*);
void DeleteItemDescriptionBox(void);


BOOLEAN InItemStackPopup(void);
void    InitItemStackPopup(SOLDIERTYPE* pSoldier, UINT8 ubPosition, INT16 sInvX, INT16 sInvY, INT16 sInvWidth, INT16 sInvHeight);
void RenderItemStackPopup( BOOLEAN fFullRender );


// keyring handlers
void InitKeyRingPopup(SOLDIERTYPE* pSoldier, INT16 sInvX, INT16 sInvY, INT16 sInvWidth, INT16 sInvHeight);
void RenderKeyRingPopup( BOOLEAN fFullRender );
void InitKeyRingInterface( MOUSE_CALLBACK KeyRingClickCallback );
void InitMapKeyRingInterface( MOUSE_CALLBACK KeyRingClickCallback );
void DeleteKeyRingPopup(void);


void ShutdownKeyRingInterface( void );
BOOLEAN InKeyRingPopup( void );
void BeginKeyRingItemPointer( SOLDIERTYPE *pSoldier, UINT8 ubKeyRingPosition );


extern OBJECTTYPE*  gpItemPointer;
extern OBJECTTYPE   gItemPointer;
extern SOLDIERTYPE* gpItemPointerSoldier;
extern BOOLEAN      gfItemPointerDifferentThanDefault;


void BeginItemPointer( SOLDIERTYPE *pSoldier, UINT8 ubHandPos );
void InternalBeginItemPointer( SOLDIERTYPE *pSoldier, OBJECTTYPE *pObject, INT8 bHandPos );
void EndItemPointer(void);
void DrawItemFreeCursor(void);
void DrawItemTileCursor(void);
BOOLEAN HandleItemPointerClick( UINT16 usMapPos );
UINT8 GetAttachmentHintColor(const OBJECTTYPE* pObj);
CSubVObject GetSmallInventoryGraphicForItem(const ItemModel *item);

// For the native map screen: what the item description box and the stack popup show, and their clicks.
struct ItemDescNativeView
{
	bool   open = false;
	UINT16 item = 0;
	INT8   status = 0;
	UINT8  count = 0;
	UINT32 money = 0;
	INT32  shotsLeft = -1, magSize = 0;
	UINT16 attachments[4]{};
	INT8   attachStatus[4]{};
	bool   attachEnabled[4]{};
	// Typed slots: how many the platform offers and each slot's role key
	// ("optic", "muzzle", ...), aligned with the attachment positions.
	UINT8  attachSlots = 0;
	const char* attachRole[4]{};
};
ItemDescNativeView GetItemDescNativeView();
void ItemDescNativeAttachmentClick(int slot, bool right);
void ItemDescNativeClose();
struct ItemStackNativeView
{
	bool   open = false;
	UINT16 item = 0;
	int    slots = 0, count = 0;
	INT8   status[8]{};
};
ItemStackNativeView GetItemStackNativeView();
void ItemStackNativeClick(int index, bool right);
void ItemStackNativeClose();
CSubVObject GetBigInventoryGraphicForItem(const ItemModel *item);
UINT16            GetTileGraphicForItem(const ItemModel *item);

ST::string GetHelpTextForItem(const OBJECTTYPE& obj);

void CancelItemPointer(void);

void LoadItemCursorFromSavedGame(HWFILE);
void SaveItemCursorToSavedGame(HWFILE);

// handle compatable items for merc and map inventory
BOOLEAN HandleCompatibleAmmoUIForMapScreen(const SOLDIERTYPE* pSoldier, INT32 bInvPos, BOOLEAN fOn, BOOLEAN fFromMerc);
BOOLEAN HandleCompatibleAmmoUIForMapInventory( SOLDIERTYPE *pSoldier, INT32 bInvPos, INT32 iStartSlotNumber, BOOLEAN fOn, BOOLEAN fFromMerc  );
void ResetCompatibleItemArray();

void CycleItemDescriptionItem(void);

void UpdateItemHatches(void);

extern BOOLEAN      gfInKeyRingPopup;
extern BOOLEAN      gfInItemPickupMenu;
extern SOLDIERTYPE* gpItemPopupSoldier;
extern INT8         gbCompatibleApplyItem;
extern INT8         gbInvalidPlacementSlot[NUM_INV_SLOTS];
extern MOUSE_REGION gInvDesc;
extern BOOLEAN      gfAddingMoneyToMercFromPlayersAccount;
extern MOUSE_REGION gItemDescAttachmentRegions[MAX_ATTACHMENTS];
extern INT8         gbItemPointerSrcSlot;
extern BOOLEAN      gfDontChargeAPsToPickup;
extern GUIButtonRef giMapInvDescButton;

void    HandleAnyMercInSquadHasCompatibleStuff(const OBJECTTYPE* pObject);
BOOLEAN InternalHandleCompatibleAmmoUI(const SOLDIERTYPE* pSoldier, const OBJECTTYPE* pTestObject, BOOLEAN fOn);

void SetMouseCursorFromItem(UINT16 item_idx);
void SetMouseCursorFromCurrentItem();

void SetItemPointer(OBJECTTYPE*, SOLDIERTYPE*);

void LoadInterfaceItemsGraphics();
void DeleteInterfaceItemsGraphics();

// The native tactical HUD (src/game/NativeUI/TacticalHud.cc) acts on the item sheet through the inventory core's
// verdicts (InventoryAdapter.h); nothing here clicks a legacy region or button.
/** A click on attachment position @a i of the open sheet: mount the hand there, or take the attachment out. */
void InventoryAttachClick(int i, bool right);
/** The sheet's unload button. */
void InventoryUnload();
/** which: 0 = 1000, 1 = 100, 2 = 10 (right: take the amount back), 3 = done */
void InventoryMoneyStep(int which, bool right);
struct InventoryMoneySplit { UINT32 total, remaining, removing; };
InventoryMoneySplit InventoryMoneyState();
/** The item sheet is open on an item of this kind (a pending question about it is still good). */
bool ItemDescIsOpenOn(UINT16 item);
/** The player said yes to "this attachment cannot be removed again": mount it. */
void ItemDescConfirmPermanentAttachment();
SOLDIERTYPE* NativeItemDescSoldier();
UINT8 NativeItemDescStatusIndex();
/** What the open item description box shows, as values (the native HUD draws them). */
struct NativeItemDescInfo
{
	ST::string name, desc, type, pros, cons, statusLabel, statusText, weight, weightUnit;
	bool weapon = false, gun = false, money = false, ammo = false, key = false, prosCons = false, attachmentsHatched = false;
	int  status = 0, range = -1, damage = -1, aps = -1, burstAps = -1, burstShots = 0;
	int  shotsLeft = -1, magSize = 0;
	UINT16 ammoItem = 0;
	UINT16 attachments[4]{};
	int  attachmentStatus[4]{};
	// Typed slots: how many the platform offers and each slot's role key
	// ("optic", "muzzle", ...), aligned with the attachment positions.
	UINT8  attachSlots = 0;
	const char* attachRole[4]{};
	ST::string keySector, keyDate;
};
NativeItemDescInfo NativeItemDescData();

// The pick-up menu: the native HUD draws it itself (the same rows, the same rules); its clicks reach the regions and
// buttons the legacy menu makes.
struct NativePickupRow
{
	INT16 slot = -1; // the row on the page: NativePickupClick toggles its selection
	INT16 item = 0;  // the item type (the picture the HUD draws)
	INT16 cond = 0;  // status, 0-100
	ST::string name, count, title;
	bool empty = true, sel = false, att = false;
};
struct NativePickupView
{
	bool open = false;
	INT16 x = 0, y = 0; // where the menu goes, in UI pixels (the legacy anchor)
	ST::string who;
	INT16 total = 0, page = 0, pages = 0;
	bool canUp = false, canDown = false, okEnabled = false, allSelected = false;
	std::vector<NativePickupRow> rows;
};
NativePickupView NativeItemPickupView();
void NativePickupClick(INT16 slot);
void NativePickupHover(INT16 slot); // -1: the pointer left the list
void NativePickupAll();
void NativePickupOK();
void NativePickupCancel();
void NativePickupScroll(INT16 dir);

#endif
