#ifndef SHOP_MERC_PANEL_H
#define SHOP_MERC_PANEL_H

// The legacy single-merc panel, as far as the legacy shopkeeper screen still shows it (issue #319): the merc's face,
// his pockets and the buttons around them, drawn into the frame buffer under the trade screen. Tactical does not use
// it any more. It goes with the legacy shopkeeper (#36).

#include "Button_System.h"
#include "Interface.h"
#include "JA2Types.h"
#include "MouseSystem.h"

enum
{
	STANCEUP_BUTTON = 0,
	UPDOWN_BUTTON,
	CLIMB_BUTTON,
	STANCEDOWN_BUTTON,
	HANDCURSOR_BUTTON,
	PREVMERC_BUTTON,
	NEXTMERC_BUTTON,
	OPTIONS_BUTTON,
	BURSTMODE_BUTTON,
	LOOK_BUTTON,
	TALK_BUTTON,
	MUTE_BUTTON,
	SM_DONE_BUTTON,
	SM_MAP_SCREEN_BUTTON,
	NUM_SM_BUTTONS
};

void InitializeSMPanel();
void ShutdownSMPanel();
void RenderSMPanel(DirtyLevel*);
/** The panel is up (the shopkeeper screen made it). */
bool ShopPanelUp();
/** The merc it shows changed: the buttons follow. */
void ShopPanelRefresh();
/** The same enabling and disabling the item pointer and the pick-up menu asked of the panel's buttons. */
void ShopPanelEnableButtons(BOOLEAN fEnable, BOOLEAN fFromItemPickup);

//Used when the shop keeper interface is active
void ShopKeeperInterface_SetSMpanelButtonsState(bool enabled);

void RenderTownIDString(void);

extern MOUSE_REGION gSMPanelRegion;
extern MOUSE_REGION gSM_SELMERCMoneyRegion;

void ProgressBarBackgroundRect(const INT16 sLeft, const INT16 sTop, const INT16 sWidth, const INT16 sHeight, const UINT32 rgb, const UINT8 scale_rgb);

#endif
