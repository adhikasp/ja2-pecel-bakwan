#include "Font.h"
#include "Handle_Items.h"
#include "Isometric_Utils.h"
#include "JAScreens.h"
#include "HImage.h"
#include "MapScreen.h"
#include "Soldier_Find.h"
#include "TileDat.h"
#include "Timer_Control.h"
#include "Overhead.h"
#include "Button_System.h"
#include "Interface.h"
#include "VObject.h"
#include "VSurface.h"
#include "Input.h"
#include "Handle_UI.h"
#include "Animation_Data.h"
#include "RenderWorld.h"
#include "Radar_Screen.h"
#include "WorldMan.h"
#include "Font_Control.h"
#include "Render_Dirty.h"
#include "Interface_Cursors.h"
#include "ShopMercPanel.h"
#include "InventoryActions.h"
#include "SquadRoster.h"
#include "VObject_Blitters.h"
#include "Faces.h"
#include "Interface_Control.h"
#include "NativeUI.h"
#include "Interface_Items.h"
#include "Interface_Dialogue.h"
#include "Interactive_Tiles.h"
#include "Game_Clock.h"
#include "Strategic_Exit_GUI.h"
#include "PopUpBox.h"
#include "Assignments.h"
#include "Spread_Burst.h"
#include "Line.h"
#include "Map_Screen_Interface.h"
#include "Civ_Quotes.h"
#include "UILayout.h"
#include "OverlayAdapter.h"

#include <string_theory/format>


static UINT16 gusUICurIntTileEffectIndex;
static INT16  gsUICurIntTileEffectGridNo;
static UINT8  gsUICurIntTileOldShade;

BOOLEAN gfRerenderInterfaceFromHelpText = FALSE;


DirtyLevel gfPausedTacticalRenderInterfaceFlags = DIRTYLEVEL0;
BOOLEAN    gfPausedTacticalRenderFlags          = FALSE;


static bool         g_switch_panel      = false;
static SOLDIERTYPE* g_new_panel_soldier = 0;


void SetNewPanel(SOLDIERTYPE* const s)
{
	g_switch_panel      = true;
	g_new_panel_soldier = s;
}


void HandleTacticalPanelSwitch()
{
	if (!g_switch_panel) return;
	g_switch_panel = false;

	SOLDIERTYPE* const s = g_new_panel_soldier;
	if (s)
	{
		SetCurrentInterfacePanel(SM_PANEL);
		SetCurrentTacticalPanelCurrentMerc(s);
	}
	else
	{
		SetCurrentInterfacePanel(TEAM_PANEL);
	}

	if (guiCurrentScreen != SHOPKEEPER_SCREEN) // XXX necessary?
	{
		RenderButtons();
	}
}


static void RenderPanel(void)
{
	// the legacy trade screen still draws the single-merc panel under its own drawing (ShopMercPanel.cc)
	if (guiCurrentScreen == SHOPKEEPER_SCREEN)
	{
		if (gsCurInterfacePanel == SM_PANEL && ShopPanelUp()) RenderSMPanel(&fInterfacePanelDirty);
		return;
	}
	// in tactical there is nothing to draw here: the native HUD is the panel
	UpdateInventoryView(&fInterfacePanelDirty);
}


static void HandlePausedTacticalRender(void);


void RenderTacticalInterface( )
{

	// handle paused render of tactical
	HandlePausedTacticalRender( );

	if (!fInMapMode) // XXX necessary?
	{
		HandleFlashingItems( );

		HandleMultiPurposeLocator( );

	}

	// Handle degrading new items...
	DegradeNewlyAddedItems( );

	RenderPanel();

	// Handle faces
	if (guiCurrentScreen != SHOPKEEPER_SCREEN)
	{
		HandleAutoFaces();
		// the faces on the cards: blink and mouth, not drawn
		StepSoldierFaces();
	}
}


// handle paused render of tactical panel, if flag set, OR it in with tactical
// render flags then reset
static void HandlePausedTacticalRender(void)
{

	// for a one frame paused render of tactical
	if( gfPausedTacticalRenderFlags )
	{
		SetRenderFlags(RENDER_FLAG_FULL);
		gfPausedTacticalRenderFlags = FALSE;
	}

	if (gfPausedTacticalRenderInterfaceFlags != DIRTYLEVEL0)
	{
		fInterfacePanelDirty = gfPausedTacticalRenderInterfaceFlags;
		gfPausedTacticalRenderInterfaceFlags = DIRTYLEVEL0;
	}
}


void RenderTacticalInterfaceWhileScrolling( )
{
	RenderButtons();
	RenderPanel();
	HandleAutoFaces();
	StepSoldierFaces();
}


void SetUpInterface()
{
	if (fInMapMode) return; // XXX necessary?

	DrawUICursor();
	SetupPhysicsTrajectoryUI();

	if (g_ui_message_overlay && GetJA2Clock() - guiUIMessageTime > guiUIMessageTimeDelay)
	{
		EndUIMessage();
	}

	if (gCurrentUIMode == OPENDOOR_MENU_MODE) HandleOpenDoorMenu();

	HandleTalkingMenu();

	if (gCurrentUIMode == EXITSECTORMENU_MODE) HandleSectorExitMenu();

	// For the most part - shutdown interface when it's the enemy's turn
	if (gTacticalStatus.ubCurrentTeam != OUR_TEAM) return;

	HandleInterfaceBackgrounds();

	switch (gfUIHandleSelection)
	{
		UINT16 idx;

		case NONSELECTED_GUY_SELECTION:
			idx = GOODRING1;
			goto add_node;
		//case SELECTED_GUY_SELECTION:
			idx = SELRING1;
			goto add_node;
		case SELECTED_GUY_SELECTION:
			idx = FIRSTPOINTERS2;
			goto add_node;
		case ENEMY_GUY_SELECTION:
			idx = FIRSTPOINTERS2;
			goto add_node;
		default:
			break;

add_node:
			LEVELNODE* (&add)(UINT32, UINT16) = gsSelectedLevel == 0 ? AddObjectToHead : AddRoofToHead;
			LEVELNODE* const n                = add(gsSelectedGridNo, idx);
			n->ubShadeLevel        = DEFAULT_SHADE_LEVEL;
			n->ubNaturalShadeLevel = DEFAULT_SHADE_LEVEL;
			break;
	}

	if (gfUIShowCurIntTile)
	{
		// Check if we are over an interactive tile
		if (LEVELNODE* const int_tile = GetCurInteractiveTileGridNo(&gsUICurIntTileEffectGridNo))
		{
			gusUICurIntTileEffectIndex = int_tile->usIndex;
			// Shade green
			gsUICurIntTileOldShade     = int_tile->ubShadeLevel;
			int_tile->ubShadeLevel     = 0;
			int_tile->uiFlags         |= LEVELNODE_DYNAMIC;
		}
	}
}


void ResetInterface()
{
	if (fInMapMode) return; // XXX necessary?

	// Find out if we need to show any menus
	DetermineWhichAssignmentMenusCanBeShown();
	CreateDestroyAssignmentPopUpBoxes();

	HideUICursor();
	ResetPhysicsTrajectoryUI();

	if (gfUIHandleSelection)
	{
		BOOLEAN (&remove)(UINT32, UINT16) = gsSelectedLevel == 0 ? RemoveObject : RemoveRoof;
		GridNo const gridno               = gsSelectedGridNo;
		remove(gridno, GOODRING1);
		remove(gridno, FIRSTPOINTERS2);
	}

	if (gfUIHandleShowMoveGrid)
	{
		BOOLEAN (&remove)(UINT32, UINT16) = gsInterfaceLevel == 0 ? RemoveTopmost : RemoveOnRoof;
		GridNo const gridno               = gsUIHandleShowMoveGridLocation;
		remove(gridno, FIRSTPOINTERS2);
		remove(gridno, FIRSTPOINTERS4);
		remove(gridno, FIRSTPOINTERS9);
		remove(gridno, FIRSTPOINTERS13);
		remove(gridno, FIRSTPOINTERS15);
		remove(gridno, FIRSTPOINTERS19);
		remove(gridno, FIRSTPOINTERS20);
	}

	fInterfacePanelDirty = DIRTYLEVEL0;

	// Reset int tile cursor stuff
	if (gfUIShowCurIntTile && gsUICurIntTileEffectGridNo != NOWHERE)
	{
		for (LEVELNODE* i = gpWorldLevelData[gsUICurIntTileEffectGridNo].pStructHead; i; i = i->pNext)
		{
			LEVELNODE& n = *i;
			if (n.usIndex != gusUICurIntTileEffectIndex) continue;
			n.ubShadeLevel  = gsUICurIntTileOldShade;
			n.uiFlags      &= ~LEVELNODE_DYNAMIC;
			break;
		}
	}
}


void RenderTopmostTacticalInterface()
{
	if (gfRerenderInterfaceFromHelpText)
	{
		fInterfacePanelDirty = DIRTYLEVEL2;
		RenderPanel();
		gfRerenderInterfaceFromHelpText = FALSE;
	}

	if (fInMapMode) // XXX necessary?
	{
		// If we want to rederaw whole screen, dirty all buttons!
		if (fInterfacePanelDirty == DIRTYLEVEL2) MarkButtonsDirty();
		RenderButtons();
		return;
	}

	// the native HUD draws the stack popup, the key ring, the menus (PopupAdapter.h) and the world overlays
	// (locators, items, burst marks, arrows, the rubber band, the lists, the pause box) itself, from the frame the
	// adapter builds (OverlayAdapter.h)
	UpdateOverlayFrame();

	if (!gfInItemPickupMenu && !gpItemPointer)
	{
		HandleAnyMercInSquadHasCompatibleStuff(NULL);
	}

	// the native sector card shows the clock and the town; a click on the clock pauses (TacticalCommands)
	RemoveMouseRegionForPauseOfClock();

	// If we want to rederaw whole screen, dirty all buttons!
	if (fInterfacePanelDirty == DIRTYLEVEL2) MarkButtonsDirty();

	RenderButtons();
	RenderPausedGameBox();

	MarkAllBoxesAsAltered();

	HandleShowingOfTacticalInterfaceFastHelpText();
	HandleShadingOfLinesForAssignmentMenus();
	DetermineBoxPositions();
	DisplayBoxes(FRAME_BUFFER);
}




void EraseInterfaceMenus( BOOLEAN fIgnoreUIUnLock )
{
	// ATE: If we are currently talking, setup this flag so that the
	// automatic handler in handledialogue doesn't adjust the UI setting
	if ( ( gTacticalStatus.uiFlags & ENGAGED_IN_CONV ) && fIgnoreUIUnLock )
	{
		gTacticalStatus.uiFlags |= IGNORE_ENGAGED_IN_CONV_UI_UNLOCK;
	}

	// Remove item pointer if one active
	CancelItemPointer( );

	ShutDownQuoteBoxIfActive( );
	PopDownMovementMenu( );
	PopDownOpenDoorMenu( );
	RemoveItemPickupMenu( );
	DeleteTalkingMenu( );

	// Cancel Rubberbanding every time a menu is erased/opened
	EndRubberBanding(true);
	ResetMultiSelection();
}


bool AreWeInAUIMenu()
{
	return gfInMovementMenu || gfInOpenDoorMenu || gfInItemPickupMenu || gfInSectorExitMenu || gfInTalkPanel;
}


void ResetInterfaceAndUI( )
{
	// Erase menus
	EraseInterfaceMenus( FALSE );

	EraseRenderArrows( );

	//ResetMultiSelection( );

	if (g_ui_message_overlay != NULL)
	{
		RemoveVideoOverlay(g_ui_message_overlay);
		g_ui_message_overlay = NULL;
	}

	// Set UI back to movement...
	guiPendingOverrideEvent = M_ON_TERRAIN;
	HandleTacticalUI( );
}


bool InterfaceOKForMeanwhilePopup()
{
	return !g_switch_panel;
}
