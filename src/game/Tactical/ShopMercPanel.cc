#include "ShopMercPanel.h"
#include "InventoryActions.h"
#include "InventoryAdapter.h"
#include "SquadRoster.h"
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

// DEFINES FOR VARIOUS PANELS
#define SM_ITEMDESC_START_X			214 + INTERFACE_START_X
#define SM_ITEMDESC_START_Y			1 + INV_INTERFACE_START_Y
#define SM_ITEMDESC_HEIGHT			128
#define SM_ITEMDESC_WIDTH			314

// SINGLE MERC SCREEN

#define SM_SELMERC_AP_X			67
#define SM_SELMERC_AP_Y			53
#define SM_SELMERC_AP_HEIGHT			10
#define SM_SELMERC_AP_WIDTH			17

#define SM_SELMERC_HEALTH_X			69
#define SM_SELMERC_HEALTH_Y			47

#define SM_SELMERCNAME_X			11
#define SM_SELMERCNAME_Y			53
#define SM_SELMERCNAME_WIDTH			53
#define SM_SELMERCNAME_HEIGHT			10

#define SM_SELMERC_FACE_X			13
#define SM_SELMERC_FACE_Y			6
#define SM_SELMERC_FACE_HEIGHT			42
#define SM_SELMERC_FACE_WIDTH			48

#define SM_SELMERC_PLATE_X			4
#define SM_SELMERC_PLATE_Y			2
#define SM_SELMERC_PLATE_HEIGHT		65
#define SM_SELMERC_PLATE_WIDTH			83


#define STATS_TITLE_FONT_COLOR			6
#define STATS_TEXT_FONT_COLOR			5


#define SM_TALKB_X				155
#define SM_TALKB_Y				108
#define SM_MUTEB_X				91
#define SM_MUTEB_Y				108
#define SM_STANCEUPB_X				187
#define SM_STANCEUPB_Y				40
#define SM_UPDOWNB_X				91
#define SM_UPDOWNB_Y				73
#define SM_CLIMBB_X				187
#define SM_CLIMBB_Y				8
#define SM_STANCEDOWNB_X			187
#define SM_STANCEDOWNB_Y			108
#define SM_HANDCURSORB_X			123
#define SM_HANDCURSORB_Y			73
#define SM_PREVMERCB_X				9
#define SM_PREVMERCB_Y				70
#define SM_NEXTMERCB_X				51
#define SM_NEXTMERCB_Y				70
#define SM_OPTIONSB_X				9
#define SM_OPTIONSB_Y				105
#define SM_BURSTMODEB_X			155
#define SM_BURSTMODEB_Y			73
#define SM_LOOKB_X				123
#define SM_LOOKB_Y				108
#define SM_STEALTHMODE_X			187
#define SM_STEALTHMODE_Y			73
#define SM_DONE_X				(g_ui.m_teamPanelSlotsTotalWidth + 45)
#define SM_DONE_Y				4
#define SM_MAPSCREEN_X				(g_ui.m_teamPanelSlotsTotalWidth + 91)
#define SM_MAPSCREEN_Y				4


#define SM_PERCENT_WIDTH			20
#define SM_PERCENT_HEIGHT			10
#define SM_ARMOR_X				347
#define SM_ARMOR_Y				79
#define SM_ARMOR_LABEL_X			363
#define SM_ARMOR_LABEL_Y			69
#define SM_ARMOR_PERCENT_X			368
#define SM_ARMOR_PERCENT_Y			79

#define SM_WEIGHT_LABEL_X			430
#define SM_WEIGHT_LABEL_Y			107
#define SM_WEIGHT_PERCENT_X			449
#define SM_WEIGHT_PERCENT_Y			106
#define SM_WEIGHT_X				428
#define SM_WEIGHT_Y				106

#define SM_CAMO_LABEL_X			430
#define SM_CAMO_LABEL_Y			122
#define SM_CAMO_PERCENT_X			449
#define SM_CAMO_PERCENT_Y			121
#define SM_CAMO_X				428
#define SM_CAMO_Y				121


#define SM_STATS_WIDTH				30
#define SM_STATS_HEIGHT			8
#define SM_AGI_X				99
#define SM_AGI_Y				7
#define SM_DEX_X				99
#define SM_DEX_Y				17
#define SM_STR_X				99
#define SM_STR_Y				27
#define SM_CHAR_X				99
#define SM_CHAR_Y				37
#define SM_WIS_X				99
#define SM_WIS_Y				47
#define SM_EXPLVL_X				148
#define SM_EXPLVL_Y				7
#define SM_MRKM_X				148
#define SM_MRKM_Y				17
#define SM_EXPL_X				148
#define SM_EXPL_Y				27
#define SM_MECH_X				148
#define SM_MECH_Y				37
#define SM_MED_X				148
#define SM_MED_Y				47

#define MONEY_X				460
#define MONEY_Y				105
#define MONEY_WIDTH				30
#define MONEY_HEIGHT				22

#define TM_FACE_X				14
#define TM_FACE_Y				6
#define TM_FACE_WIDTH				48
#define TM_FACE_HEIGHT				43

#define TM_ENDTURN_X				(g_ui.m_teamPanelSlotsTotalWidth + 9)
#define TM_ENDTURN_Y				9
#define TM_ROSTERMODE_X			(g_ui.m_teamPanelSlotsTotalWidth + 9)
#define TM_ROSTERMODE_Y			45
#define TM_DISK_X				(g_ui.m_teamPanelSlotsTotalWidth + 9)
#define TM_DISK_Y				81

#define TM_NAME_X				11
#define TM_NAME_Y				53
#define TM_NAME_WIDTH				53
#define TM_NAME_HEIGHT				10

#define TM_AP_X				67
#define TM_AP_Y				53
#define TM_AP_WIDTH				18
#define TM_AP_HEIGHT				10

#define TM_LIFEBAR_HEIGHT			42

#define TM_FACEHIGHTL_X			4
#define TM_FACEHIGHTL_Y			2
#define TM_FACEHIGHTL_WIDTH			84
#define TM_FACEHIGHTL_HEIGHT			114

#define TM_INV_WIDTH				58
#define TM_INV_HEIGHT				23
#define TM_INV_HAND1STARTX			8
#define TM_INV_HAND1STARTY			67
#define TM_INV_HAND_SEP			83
#define TM_INV_HAND_SEPY			24

#define TM_BARS_X				67
#define TM_BARS_Y				3
#define TM_BARS_WIDTH				19
#define TM_BARS_HEIGHT				47

#define INDICATOR_BOX_WIDTH			12
#define INDICATOR_BOX_HEIGHT			10


enum
{
	STANCEUP_IMAGES = 0,
	UPDOWN_IMAGES,
	CLIMB_IMAGES,
	STANCEDOWN_IMAGES,
	HANDCURSOR_IMAGES,
	PREVMERC_IMAGES,
	NEXTMERC_IMAGES,
	OPTIONS_IMAGES,
	//BURSTMODE_IMAGES,
	LOOK_IMAGES,
	TALK_IMAGES,
	MUTE_IMAGES,
	STANCE_IMAGES,
	DONE_IMAGES,
	MAPSCREEN_IMAGES,
	NUM_SM_BUTTON_IMAGES
};





// The panel under the trade screen is not tactical: only "previous / next merc" works, the rest is for show
static void BtnNothing(GUI_BUTTON*, UINT32) {}
static void CreateSMPanelButtons(void);

static BUTTON_PICS* iSMPanelImages[NUM_SM_BUTTON_IMAGES];
static BUTTON_PICS* iBurstButtonImages[NUM_WEAPON_MODES];

static BUTTON_PICS* giSMStealthImages;
GUIButtonRef giSMStealthButton;

static BOOLEAN gfUIStanceDifferent = FALSE;
static BOOLEAN gfAllDisabled = FALSE;







GUIButtonRef iSMPanelButtons[NUM_SM_BUTTONS];

// Video Surface for Single Merc Panel
static SGPVSurface* guiSMPanel;
static SGPVObject* guiSMObjects;
static SGPVObject* guiSMObjects2;

static cache_key_t const guiCLOSE{ INTERFACEDIR "/p_close.sti" };

// Globals for various mouse regions
static MOUSE_REGION gSM_SELMERCBarsRegion;
MOUSE_REGION        gSM_SELMERCMoneyRegion;
static MOUSE_REGION gSM_SELMERCStatsRegion[NUM_ATTRIBUTES];

static std::unique_ptr<SGPVSurface> CreateVideoSurfaceFromObjectFile(const ST::string& filename, UINT16 usRegionIndex);

MOUSE_REGION gSMPanelRegion;




// Wraps up check for AP-s get from a different soldier for in a vehicle...
static INT8 GetUIApsToDisplay(SOLDIERTYPE const* s)
{
	if (s->uiStatusFlags & SOLDIER_DRIVER)
	{
		return GetSoldierStructureForVehicle(GetVehicle(s->iVehicleId)).bActionPoints;
	}
	return s->bActionPoints;
}





static void SetButtonState(UINT const idx, bool const clicked)
{
	GUI_BUTTON& b = *iSMPanelButtons[idx];
	if (b.ubToggleButtonActivated)
		return;
	b.uiFlags &= ~BUTTON_CLICKED_ON;
	b.uiFlags |= clicked ? BUTTON_CLICKED_ON : 0;
}




static void UpdateSMPanel()
{
	SOLDIERTYPE& s = *gpSMCurrentMerc;
	if (s.sGridNo == NOWHERE) return;

	UINT8 stance_state = s.ubDesiredHeight;
	if (stance_state == NO_DESIRED_HEIGHT)
	{
		stance_state = gAnimControl[s.usAnimState].ubEndHeight;
	}

	INT32 stance_gfx; // XXX HACK000E
	switch (stance_state)
	{
		case ANIM_STAND:
			stance_gfx = 11;
			DisableButton(iSMPanelButtons[STANCEUP_BUTTON]);
			EnableButton(iSMPanelButtons[STANCEDOWN_BUTTON], IsValidStance(&s, ANIM_CROUCH));
			break;

		case ANIM_CROUCH:
			stance_gfx = 5;
			EnableButton(iSMPanelButtons[STANCEUP_BUTTON]);
			EnableButton(iSMPanelButtons[STANCEDOWN_BUTTON], IsValidStance(&s, ANIM_PRONE));
			break;

		case ANIM_PRONE:
			stance_gfx = 17;
			EnableButton(iSMPanelButtons[STANCEUP_BUTTON]);
			DisableButton(iSMPanelButtons[STANCEDOWN_BUTTON]);
			break;

		default: abort(); // HACK000E
	}

	// Stance button done whether we're disabled or not
	if (gfUIStanceDifferent)
	{
		// Remove old
		if (giSMStealthButton) RemoveButton(giSMStealthButton);
		if (giSMStealthImages) UnloadButtonImage(giSMStealthImages);

		// Make new
		if (!s.bStealthMode) stance_gfx += 3;
		giSMStealthImages = UseLoadedButtonImage(iSMPanelImages[STANCE_IMAGES], stance_gfx + 2, stance_gfx,
								-1, stance_gfx + 1, -1);
		giSMStealthButton = QuickCreateButton(giSMStealthImages,
							INTERFACE_START_X + SM_STEALTHMODE_X,
							INV_INTERFACE_START_Y + SM_STEALTHMODE_Y,
							MSYS_PRIORITY_HIGH - 1, BtnNothing);
		giSMStealthButton->SetFastHelpText(TacticalStr[TOGGLE_STEALTH_MODE_POPUPTEXT]);

		gfUIStanceDifferent = FALSE;

		if (gfAllDisabled) DisableButton(giSMStealthButton);
	}

	if (gfAllDisabled) return;

	CheckForReEvaluateDisabledINVPanelButtons();

	// Check for any newly added items we need
	if (s.fCheckForNewlyAddedItems)
	{
		// Startup any newly added items
		CheckForAnyNewlyAddedItems(&s);
		s.fCheckForNewlyAddedItems = FALSE;
	}

	// Set Disable/Enable UI based on buddy's stats
	GUIButtonRef const burst = iSMPanelButtons[BURSTMODE_BUTTON];
	if (burst->image != iBurstButtonImages[s.bWeaponMode])
	{
		burst->image    = iBurstButtonImages[s.bWeaponMode];
		burst->uiFlags |= BUTTON_DIRTY;
	}

	SetButtonState(MUTE_BUTTON, s.uiStatusFlags & SOLDIER_MUTE);

	bool const enable_climb =
		(FindLowerLevel(&s)  && EnoughPoints(&s, GetAPsToClimbRoof(&s, TRUE),  0, FALSE)) ||
		(FindHigherLevel(&s) && EnoughPoints(&s, GetAPsToClimbRoof(&s, FALSE), 0, FALSE)) ||
		FindFenceJumpDirection(&s);
	EnableButton(iSMPanelButtons[CLIMB_BUTTON], enable_climb);

	EnableButton(iSMPanelButtons[SM_DONE_BUTTON], gTacticalStatus.ubCurrentTeam == OUR_TEAM && gTacticalStatus.uiFlags & INCOMBAT);

	SetButtonState(UPDOWN_BUTTON, gsInterfaceLevel > 0);

	SetButtonState(HANDCURSOR_BUTTON, gCurrentUIMode == HANDCURSOR_MODE);
	SetButtonState(TALK_BUTTON,       gCurrentUIMode == TALKCURSOR_MODE);
	SetButtonState(LOOK_BUTTON,       gCurrentUIMode == LOOKCURSOR_MODE);

	// If not selected (or dead), disable/gray some buttons
	if (&s != GetSelectedMan() ||
		s.bLife< OKLIFE ||
		gTacticalStatus.ubCurrentTeam != OUR_TEAM ||
		gfSMDisableForItems)
	{
		DisableButton(iSMPanelButtons[CLIMB_BUTTON]);
		DisableButton(iSMPanelButtons[BURSTMODE_BUTTON]);
		DisableButton(iSMPanelButtons[STANCEUP_BUTTON]);
		DisableButton(iSMPanelButtons[STANCEDOWN_BUTTON]);
		DisableButton(iSMPanelButtons[LOOK_BUTTON]);
		DisableButton(iSMPanelButtons[UPDOWN_BUTTON]);
		DisableButton(iSMPanelButtons[HANDCURSOR_BUTTON]);
		if (giSMStealthButton)
			DisableButton(giSMStealthButton);
	}
	else
	{
		bool const enable_burst = IsGunBurstCapable(&s, HANDPOS) ||
						FindAttachment(&s.inv[HANDPOS], UNDER_GLAUNCHER) != ITEM_NOT_FOUND;
		EnableButton(iSMPanelButtons[BURSTMODE_BUTTON], enable_burst);
		EnableButton(iSMPanelButtons[LOOK_BUTTON]);
		EnableButton(iSMPanelButtons[UPDOWN_BUTTON]);
		EnableButton(iSMPanelButtons[HANDCURSOR_BUTTON]);

		if (giSMStealthButton)
			EnableButton(giSMStealthButton);
	}

	// CJC Dec 4 2002: or if item pickup menu is up
	EnableButton(iSMPanelButtons[SM_MAP_SCREEN_BUTTON],
			!(gTacticalStatus.uiFlags & ENGAGED_IN_CONV) && !gfInItemPickupMenu);
}




static void SMInvClickCallbackPrimary(MOUSE_REGION* pRegion, UINT32 iReason);
static void SMInvClickCallbackSecondary(MOUSE_REGION* pRegion, UINT32 iReason);
static void SMInvClickCamoCallback(MOUSE_REGION* pRegion, UINT32 iReason);
static void SMInvMoneyButtonCallback(MOUSE_REGION* pRegion, UINT32 iReason);
static void SMInvMoveCallback(MOUSE_REGION* pRegion, UINT32 iReason);
static void SMInvMoveCamoCallback(MOUSE_REGION* pRegion, UINT32 iReason);
static void SelectedMercPopupMoveCallback(MOUSE_REGION* pRegion, uint32_t iReason);


/** Tile a plain area of the panel art over dest (clipped to it) on the given surface. */
static void TileFillerOnSurface(SGPVSurface* dst, SGPVSurface* src, SGPBox const& tile, SGPBox const& dest)
{
	for (int y = 0; y < dest.h; y += tile.h)
	{
		for (int x = 0; x < dest.w; x += tile.w)
		{
			SGPBox r = tile;
			r.w = std::min<int>(tile.w, dest.w - x);
			r.h = std::min<int>(tile.h, dest.h - y);
			BltVideoSurface(dst, src, dest.x + x, dest.y + y, &r);
		}
	}
}

// Pre-rendered side fillers of the bottom bar (screen wide, INV_INTERFACE_HEIGHT tall; only the areas
// left and right of the panel are meaningful). Built once, released with the panels.
// A raw pointer on purpose: a static owner would be destroyed after the video system is gone.
static SGPVSurface* gBottomFillerStrip = nullptr;

static void BuildBottomFillerStrip(UINT16 const height)
{
	delete gBottomFillerStrip;
	gBottomFillerStrip = new SGPVSurface(g_ui.m_screenWidth, height, PIXEL_DEPTH);
	auto vsFiller = CreateVideoSurfaceFromObjectFile(INTERFACEDIR "/overheadinterface.sti", 0);
	// a plain stretch of leather from the overhead panel; 70 rows so that two tiles make up the 140-row bar
	SGPBox const tile = {96, 50, 64, 70};

	INT32 const leftW  = INTERFACE_START_X;
	INT32 const rightX = INTERFACE_START_X + g_ui.m_teamPanelWidth;
	INT32 const rightW = g_ui.m_screenWidth - rightX;

	SGPVSurface* const dst = gBottomFillerStrip;
	UINT16 const dark  = Get16BPPColor(FROMRGB(16, 12, 8));
	UINT16 const light = Get16BPPColor(FROMRGB(120, 96, 64));
	if (leftW > 0)
	{
		SGPBox const dest = {0, 0, static_cast<UINT16>(leftW), height};
		TileFillerOnSurface(dst, vsFiller.get(), tile, dest);
		ColorFillVideoSurfaceArea(dst, 0, 0, leftW, 1, light);
		ColorFillVideoSurfaceArea(dst, leftW - 2, 0, leftW, height, dark);
	}
	if (rightW > 0)
	{
		SGPBox const dest = {static_cast<UINT16>(rightX), 0, static_cast<UINT16>(rightW), height};
		TileFillerOnSurface(dst, vsFiller.get(), tile, dest);
		ColorFillVideoSurfaceArea(dst, rightX, 0, g_ui.m_screenWidth, 1, light);
		ColorFillVideoSurfaceArea(dst, rightX, 0, rightX + 2, height, dark);
	}
}

/** Fill the bottom bar area left and right of the panel (wide screens) with tiled panel art and a bevel
 * against the panel, instead of leaving it black. Does nothing at classic 640x480. */
static void FillEmptySpaceAtBottom()
{
	if (!g_ui.isBigScreen()) return;

	UINT16 const top    = g_ui.get_INV_INTERFACE_START_Y();
	UINT16 const height = g_ui.m_screenHeight - top;
	if (!gBottomFillerStrip) BuildBottomFillerStrip(height);

	INT32 const leftW  = INTERFACE_START_X;
	INT32 const rightX = INTERFACE_START_X + g_ui.m_teamPanelWidth;
	if (leftW > 0)
	{
		SGPBox const r = {0, 0, static_cast<UINT16>(leftW), height};
		BltVideoSurface(guiSAVEBUFFER, gBottomFillerStrip, 0, top, &r);
	}
	if (rightX < g_ui.m_screenWidth)
	{
		SGPBox const r = {static_cast<UINT16>(rightX), 0, static_cast<UINT16>(g_ui.m_screenWidth - rightX), height};
		BltVideoSurface(guiSAVEBUFFER, gBottomFillerStrip, rightX, top, &r);
	}
}

/** Fill up some space with a textured space filler */
static void DrawFillerOnSurface(SGPVSurface* vsSurface, SGPBox const &dest)
{
	auto vsFiller = CreateVideoSurfaceFromObjectFile(INTERFACEDIR "/overheadinterface.sti", 0);

	// clip and blit the big panel from the overheadinterface graphics
	SGPBox const src  = {80, 42, (UINT16) std::min(int(dest.w), 560), (UINT16) std::min(int(dest.h), 112)};
	BltStretchVideoSurface(vsSurface, vsFiller.get(), &src, &dest);
}

static void MakeRegionForAttributeHelpText(Attributes attr, uint16_t tlx, uint16_t tly, uint16_t brx, uint16_t bry)
{
	MSYS_DefineRegion(&gSM_SELMERCStatsRegion[attr], tlx, tly, brx, bry, MSYS_PRIORITY_HIGH, MSYS_NO_CURSOR, SelectedMercPopupMoveCallback, MSYS_NO_CALLBACK);
}

static bool g_shopPanelUp;

void InitializeSMPanel()
{
	// Assemble the SMPanel from graphic objects.
	// For visual consistency, the SMPanel should fill up the same width as the TEAMPanel, that the buttons and
	// minimap are in the bottom-right corner.
	SGPVObject* voSMPanel = AddVideoObjectFromFile(INTERFACEDIR "/inventory_bottom_panel.sti");
	guiSMPanel = new SGPVSurface(g_ui.m_teamPanelWidth, INV_INTERFACE_HEIGHT, PIXEL_DEPTH);
	if (g_ui.m_teamPanelWidth > 640)
	{
		// The team panel is longer than default
		// need a second blit, and we will start from the right
		BltVideoObject(guiSMPanel, voSMPanel, 0, g_ui.m_teamPanelWidth - 640, 0);
	}
	// draw the basic Single-Merc panel
	BltVideoObject(guiSMPanel, voSMPanel, 0, 0, 0);
	DeleteVideoObject(voSMPanel);

	INT16 sFillerWidth = g_ui.m_teamPanelWidth - 640;
	if (sFillerWidth > 0)
	{
		// draw a space filler if needed
		SGPBox const dest = {SM_INVINTERFACE_WIDTH, 2, static_cast<UINT16>(sFillerWidth), INV_INTERFACE_HEIGHT - 6};
		DrawFillerOnSurface(guiSMPanel, dest);
	}

	guiSMObjects  = AddVideoObjectFromFile(INTERFACEDIR "/inventory_gold_front.sti");
	guiSMObjects2 = AddVideoObjectFromFile(INTERFACEDIR "/inv_frn.sti");

	FillEmptySpaceAtBottom();

	// Create buttons
	CreateSMPanelButtons();
	g_shopPanelUp = true;

	const INT32 dx = INTERFACE_START_X;
	const INT32 dy = INV_INTERFACE_START_Y;

	// Set viewports
	// Define region for panel
	MSYS_DefineRegion(&gSMPanelRegion, dx, dy, SCREEN_WIDTH, SCREEN_HEIGHT, MSYS_PRIORITY_NORMAL, CURSOR_NORMAL, MSYS_NO_CALLBACK, MSYS_NO_CALLBACK);

	INT32 x;
	INT32 y;

	x = dx + SM_SELMERC_FACE_X;
	y = dy + SM_SELMERC_FACE_Y;

	//DEfine region for money button
	x = dx + MONEY_X;
	y = dy + MONEY_Y;
	MSYS_DefineRegion(&gSM_SELMERCMoneyRegion, x, y, x + MONEY_WIDTH, y + MONEY_HEIGHT, MSYS_PRIORITY_HIGH, MSYS_NO_CURSOR, MSYS_NO_CALLBACK, SMInvMoneyButtonCallback);
	gSM_SELMERCMoneyRegion.SetFastHelpText(TacticalStr[MONEY_BUTTON_HELP_TEXT]);

	// the bars carry the merc's vital figures as their tooltip
	MSYS_DefineRegion(&gSM_SELMERCBarsRegion, dx + 62, dy + 2, dx + 85, dy + 51, MSYS_PRIORITY_NORMAL, MSYS_NO_CURSOR, MSYS_NO_CALLBACK, MSYS_NO_CALLBACK);

	MOUSE_CALLBACK smInvClickCallback = MouseCallbackPrimarySecondary(SMInvClickCallbackPrimary, SMInvClickCallbackSecondary, MSYS_NO_CALLBACK, true);
	InitInvSlotInterface(g_ui.m_invSlotPositionTac, &g_ui.m_invCamoRegion, SMInvMoveCallback, smInvClickCallback, SMInvMoveCamoCallback, SMInvClickCamoCallback);
	InitKeyRingInterface(KeyRingItemPanelButtonCallback);

	if (gamepolicy(informative_tooltips))
	{	//Define regions for the informative tooltips displaying effective attribute values
		MakeRegionForAttributeHelpText(ATTR_AGILITY,      dx + SM_AGI_X - 10,    dy + SM_AGI_Y,    dx + SM_AGI_X + SM_STATS_WIDTH,    dy + SM_AGI_Y + 10);
		MakeRegionForAttributeHelpText(ATTR_DEXTERITY,    dx + SM_DEX_X - 10,    dy + SM_DEX_Y,    dx + SM_DEX_X + SM_STATS_WIDTH,    dy + SM_DEX_Y + 10);
		MakeRegionForAttributeHelpText(ATTR_STRENGTH,     dx + SM_STR_X - 10,    dy + SM_STR_Y,    dx + SM_STR_X + SM_STATS_WIDTH,    dy + SM_STR_Y + 10);
		MakeRegionForAttributeHelpText(ATTR_LEADERSHIP,   dx + SM_CHAR_X - 10,   dy + SM_CHAR_Y,   dx + SM_CHAR_X + SM_STATS_WIDTH,   dy + SM_CHAR_Y + 10);
		MakeRegionForAttributeHelpText(ATTR_WISDOM,       dx + SM_WIS_X - 10,    dy + SM_WIS_Y,    dx + SM_WIS_X + SM_STATS_WIDTH,    dy + SM_WIS_Y + 10);
		//column 2
		MakeRegionForAttributeHelpText(ATTR_EXPLEVEL,     dx + SM_EXPLVL_X - 10, dy + SM_EXPLVL_Y, dx + SM_EXPLVL_X + SM_STATS_WIDTH, dy + SM_EXPLVL_Y + 10);
		MakeRegionForAttributeHelpText(ATTR_MARKSMANSHIP, dx + SM_MRKM_X - 10,   dy + SM_MRKM_Y,   dx + SM_MRKM_X + SM_STATS_WIDTH,   dy + SM_MRKM_Y + 10);
		MakeRegionForAttributeHelpText(ATTR_EXPLOSIVES,   dx + SM_EXPL_X - 10,   dy + SM_EXPL_Y,   dx + SM_EXPL_X + SM_STATS_WIDTH,   dy + SM_EXPL_Y + 10);
		MakeRegionForAttributeHelpText(ATTR_MECHANICAL,   dx + SM_MECH_X - 10,   dy + SM_MECH_Y,   dx + SM_MECH_X + SM_STATS_WIDTH,   dy + SM_MECH_Y + 10);
		MakeRegionForAttributeHelpText(ATTR_MEDICAL,      dx + SM_MED_X - 10,    dy + SM_MED_Y,    dx + SM_MED_X + SM_STATS_WIDTH,    dy + SM_MED_Y + 10);
	}

	// this is important! It will disable buttons like SM_MAP_SCREEN_BUTTON when they're supposed to be
	// disabled - the previous disabled state is lost everytime panel is reinitialized, because all the
	// buttons are created from scratch!
	if (gpItemPointer == nullptr)
	{
		// empty cursor - enable, not from item pickup
		EnableSMPanelButtons(TRUE, FALSE);
	}
	else
	{
		// full cursor - disable, from item pickup
		EnableSMPanelButtons(FALSE, TRUE);
	}
}


static void MakeButtonN(UINT idx, BUTTON_PICS* image, INT16 x, INT16 y, const GUI_CALLBACK click, const ST::string& help)
try
{
	GUIButtonRef const btn = QuickCreateButtonToggle(image, x, y, MSYS_PRIORITY_HIGH - 1, click);
	iSMPanelButtons[idx] = btn;
	btn->SetFastHelpText(help);
}
catch (...)
{
	SLOGE("Cannot create Interface button");
	throw;
}


static void MakeButtonT(UINT idx, BUTTON_PICS* image, INT16 x, INT16 y, const GUI_CALLBACK click, const ST::string& help)
try
{
	GUIButtonRef const btn = QuickCreateButton(image, x, y, MSYS_PRIORITY_HIGH - 1, click);
	iSMPanelButtons[idx] = btn;
	btn->SetFastHelpText(help);
}
catch (...)
{
	SLOGE("Cannot create Interface button");
	throw;
}


static void BtnPrevMercCallback(GUI_BUTTON* btn, UINT32 reason);
static void BtnNextMercCallback(GUI_BUTTON* btn, UINT32 reason);
// The panel under the trade screen is not tactical: only "previous / next merc" works, the rest is for show


static void CreateSMPanelButtons(void)
{
	giSMStealthImages = nullptr;
	gfUIStanceDifferent = TRUE;
	gfAllDisabled	= FALSE;

	// Load button Graphics
	iSMPanelImages[STANCEUP_IMAGES]   = LoadButtonImage(INTERFACEDIR "/inventory_buttons.sti",    0, 10);
	iSMPanelImages[UPDOWN_IMAGES]     = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],     9, 19);
	iSMPanelImages[CLIMB_IMAGES]      = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],     3, 13);
	iSMPanelImages[STANCEDOWN_IMAGES] = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],     8, 18);
	iSMPanelImages[HANDCURSOR_IMAGES] = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],     1, 11);
	iSMPanelImages[PREVMERC_IMAGES]   = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],    20, 22);
	iSMPanelImages[NEXTMERC_IMAGES]   = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],    21, 23);
#if 0
	iSMPanelImages[BURSTMODE_IMAGES]  = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],     7, 17);
#endif
	iSMPanelImages[LOOK_IMAGES]       = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],     2, 12);
	iSMPanelImages[TALK_IMAGES]       = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],     6, 16);
	iSMPanelImages[MUTE_IMAGES]       = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],     5, 15);
	iSMPanelImages[OPTIONS_IMAGES]    = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],    24, 25);

	iBurstButtonImages[WM_NORMAL]     = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],     7,  7);
	iBurstButtonImages[WM_BURST]      = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],    17, 17);
	iBurstButtonImages[WM_ATTACHED]   = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],    26, 26);
	// Autofire reuses the burst artwork: it is the same "hold the trigger" verb.
	iBurstButtonImages[WM_AUTOFIRE]   = UseLoadedButtonImage(iSMPanelImages[STANCEUP_IMAGES],    17, 17);

	iSMPanelImages[STANCE_IMAGES]     = LoadButtonImage(INTERFACEDIR "/invadd-ons.sti", 0, 0, -1, 2, -1);

	iSMPanelImages[DONE_IMAGES]       = LoadButtonImage(INTERFACEDIR "/inventory_buttons_2.sti",  1,  3);
	iSMPanelImages[MAPSCREEN_IMAGES]  = UseLoadedButtonImage(iSMPanelImages[DONE_IMAGES],         0,  2);


	// Create buttons

	// SET BUTTONS TO -1
	std::fill(std::begin(iSMPanelButtons), std::end(iSMPanelButtons), GUIButtonRef::NoButton());

	const INT32 dx = INTERFACE_START_X;
	const INT32 dy = INV_INTERFACE_START_Y;

	MakeButtonT(SM_MAP_SCREEN_BUTTON, iSMPanelImages[MAPSCREEN_IMAGES],  dx + SM_MAPSCREEN_X,   dy + SM_MAPSCREEN_Y,   BtnNothing,  TacticalStr[MAPSCREEN_POPUPTEXT]);
	MakeButtonT(SM_DONE_BUTTON,       iSMPanelImages[DONE_IMAGES],       dx + SM_DONE_X,        dy + SM_DONE_Y,        BtnNothing,     TacticalStr[END_TURN_POPUPTEXT]);
	MakeButtonN(TALK_BUTTON,          iSMPanelImages[TALK_IMAGES],       dx + SM_TALKB_X,       dy + SM_TALKB_Y,       BtnNothing,       TacticalStr[TALK_CURSOR_POPUPTEXT]);
	MakeButtonN(MUTE_BUTTON,          iSMPanelImages[MUTE_IMAGES],       dx + SM_MUTEB_X,       dy + SM_MUTEB_Y,       BtnNothing,       TacticalStr[TOGGLE_MUTE_POPUPTEXT]);
	MakeButtonT(STANCEUP_BUTTON,      iSMPanelImages[STANCEUP_IMAGES],   dx + SM_STANCEUPB_X,   dy + SM_STANCEUPB_Y,   BtnNothing,   TacticalStr[CHANGE_STANCE_UP_POPUPTEXT]);
	MakeButtonN(UPDOWN_BUTTON,        iSMPanelImages[UPDOWN_IMAGES],     dx + SM_UPDOWNB_X,     dy + SM_UPDOWNB_Y,     BtnNothing,     TacticalStr[CURSOR_LEVEL_POPUPTEXT]);
	MakeButtonT(CLIMB_BUTTON,         iSMPanelImages[CLIMB_IMAGES],      dx + SM_CLIMBB_X,      dy + SM_CLIMBB_Y,      BtnNothing,      TacticalStr[JUMPCLIMB_POPUPTEXT]);
	MakeButtonT(STANCEDOWN_BUTTON,    iSMPanelImages[STANCEDOWN_IMAGES], dx + SM_STANCEDOWNB_X, dy + SM_STANCEDOWNB_Y, BtnNothing, TacticalStr[CHANGE_STANCE_DOWN_POPUPTEXT]);
	MakeButtonN(HANDCURSOR_BUTTON,    iSMPanelImages[HANDCURSOR_IMAGES], dx + SM_HANDCURSORB_X, dy + SM_HANDCURSORB_Y, BtnNothing, TacticalStr[EXAMINE_CURSOR_POPUPTEXT]);
	MakeButtonT(PREVMERC_BUTTON,      iSMPanelImages[PREVMERC_IMAGES],   dx + SM_PREVMERCB_X,   dy + SM_PREVMERCB_Y,   BtnPrevMercCallback,   TacticalStr[PREV_MERC_POPUPTEXT]);
	MakeButtonT(NEXTMERC_BUTTON,      iSMPanelImages[NEXTMERC_IMAGES],   dx + SM_NEXTMERCB_X,   dy + SM_NEXTMERCB_Y,   BtnNextMercCallback,   TacticalStr[NEXT_MERC_POPUPTEXT]);
	MakeButtonT(OPTIONS_BUTTON,       iSMPanelImages[OPTIONS_IMAGES],    dx + SM_OPTIONSB_X,    dy + SM_OPTIONSB_Y,    BtnNothing,    TacticalStr[CHANGE_OPTIONS_POPUPTEXT]);
#if 0
	MakeButtonN(BURSTMODE_BUTTON,     iSMPanelImages[BURSTMODE_IMAGES],  SM_BURSTMODEB_X,  dy + SM_BURSTMODEB_Y,  BtnNothing,  TacticalStr[TOGGLE_BURSTMODE_POPUPTEXT]);
#endif
	MakeButtonT(BURSTMODE_BUTTON,     iBurstButtonImages[WM_NORMAL],     dx + SM_BURSTMODEB_X,  dy + SM_BURSTMODEB_Y,  BtnNothing,  TacticalStr[TOGGLE_BURSTMODE_POPUPTEXT]);
	MakeButtonN(LOOK_BUTTON,          iSMPanelImages[LOOK_IMAGES],       dx + SM_LOOKB_X,       dy + SM_LOOKB_Y,       BtnNothing,       TacticalStr[LOOK_CURSOR_POPUPTEXT]);
}


void RemoveSMPanelButtons(void)
{
	for (UINT32 cnt = 0; cnt < NUM_SM_BUTTONS; ++cnt)
	{
		if (iSMPanelButtons[cnt]) RemoveButton(iSMPanelButtons[cnt]);
	}

	for (UINT32 cnt = 0; cnt < NUM_SM_BUTTON_IMAGES; ++cnt)
	{
		UnloadButtonImage(iSMPanelImages[cnt]);
	}

	if (giSMStealthButton) RemoveButton(giSMStealthButton);
	if (giSMStealthImages) UnloadButtonImage(giSMStealthImages);

	UnloadButtonImage(iBurstButtonImages[WM_NORMAL]);
	UnloadButtonImage(iBurstButtonImages[WM_BURST]);
	UnloadButtonImage(iBurstButtonImages[WM_ATTACHED]);
	UnloadButtonImage(iBurstButtonImages[WM_AUTOFIRE]);
}


void ShutdownSMPanel()
{
	// All buttons and regions and video objects and video surfaces will be deleted at shutddown of SGM
	// We may want to delete them at the interm as well, to free up room for other panels
	delete guiSMPanel;
	delete gBottomFillerStrip;
	gBottomFillerStrip = nullptr;
	DeleteVideoObject(guiSMObjects);
	DeleteVideoObject(guiSMObjects2);

	gSelectSMPanelToMerc = nullptr;

	// CJC: delete key ring if open
	DeleteKeyRingPopup(); // function will abort if key ring is not up

	// ATE: Delete desc panel if it was open....
	if (gfInItemDescBox)
	{
		DeleteItemDescriptionBox();
	}

	ShutdownInvSlotInterface();
	ShutdownKeyRingInterface();

	MSYS_RemoveRegion(&gSMPanelRegion);
	MSYS_RemoveRegion(&gSM_SELMERCBarsRegion);
	MSYS_RemoveRegion(&gSM_SELMERCMoneyRegion);
	if (gamepolicy(informative_tooltips)) {
		for (auto& i : gSM_SELMERCStatsRegion) {
			MSYS_RemoveRegion(&i);
		}
	}

	RemoveSMPanelButtons();
	g_shopPanelUp = false;
}


static void PrintAP(SOLDIERTYPE* const s, INT16 const x, INT16 const y, INT16 const w, INT16 const h)
{
	if (!(gTacticalStatus.uiFlags & INCOMBAT))
		return;
	if (s->bLife < OKLIFE)
		return;

	INT8  const ap         = GetUIApsToDisplay(s);
	UINT8 const min_ap     = MinAPsToAttack(s, s->sLastTarget, TRUE);
	UINT8 const foreground =
		!EnoughPoints(s, min_ap, 0, FALSE) ? FONT_MCOLOR_DKRED    :
		ap < 0                             ? FONT_MCOLOR_DKRED    :
		MercUnderTheInfluence(s)           ? FONT_MCOLOR_LTBLUE   :
		s->bStealthMode                    ? FONT_MCOLOR_LTYELLOW :
		FONT_MCOLOR_LTGRAY;
	SetFontAttributes(TINYFONT1, foreground);

	RestoreExternBackgroundRect(x, y, w, h);
	MPrint(x, y, ap, HCenterVCenterAlign(w, h));
}


static void SetStatsHelp(MOUSE_REGION& r, SOLDIERTYPE const& s)
{
	ST::string help;
	ST::string text;
	if (s.bLife != 0)
	{
		if (s.uiStatusFlags & SOLDIER_VEHICLE)
		{
			text = st_format_printf(TacticalStr[VEHICLE_VITAL_STATS_POPUPTEXT], s.bLife, s.bLifeMax, s.bBreath, s.bBreathMax);
		}
		else if (s.uiStatusFlags & SOLDIER_ROBOT)
		{
			text = st_format_printf(gzLateLocalizedString[STR_LATE_16], s.bLife, s.bLifeMax);
		}
		else
		{
			text = st_format_printf(TacticalStr[MERC_VITAL_STATS_POPUPTEXT], s.bLife, s.bLifeMax, s.bBreath, s.bBreathMax, GetMoraleString(s));
		}
		help = text;
	}
	r.SetFastHelpText(help);
}

void ProgressBarBackgroundRect(const INT16 sLeft, const INT16 sTop, const INT16 sWidth, const INT16 sHeight, const UINT32 rgb, const UINT8 scale_rgb)
{
	SGPVSurface::Lock l(guiSAVEBUFFER);

	#define s(a)				((a) / 2) + ((a) / 2) * (scale_rgb) / 100

	const int r = s(0xff & rgb >> 16);
	const int g = s(0xff & rgb >> 8);
	const int b = s(0xff & rgb);

	const UINT16 fill_color = Get16BPPColor(r, g, b);

	UINT16* const dst = l.Buffer<UINT16>();

	for (int y = 0; y < sHeight; ++y)
	{
		for(int x = 0; x < sWidth; ++x)
		{
			dst[(y + sTop)*l.Pitch() / 2 + sLeft + x] = fill_color;
		}
	}
}

static void PrintStat(UINT32 const change_time, UINT16 const stat_bit, INT8 const stat_val, INT16 const x, INT16 const y, INT32 const progress)
{
	SOLDIERTYPE const& s  = *gpSMCurrentMerc;

	UINT8       const  fg =
		s.bLife < OKLIFE                                             ? FONT_MCOLOR_DKGRAY    :
		GetJA2Clock() >= CHANGE_STAT_RECENTLY_DURATION + change_time ? STATS_TEXT_FONT_COLOR :
		change_time == 0                                             ? STATS_TEXT_FONT_COLOR :
		s.usValueGoneUp & stat_bit                                   ? FONT_LTGREEN          :
		FONT_RED;

	SetFontForeground(fg);

	ST::string str = ST::format("{3d}", stat_val);
	if (gamepolicy(gui_extras))
	{
		ProgressBarBackgroundRect(x + 16, y - 2, 15 * progress / 100, 10, 0x514A05, progress);
	}

	DrawStringRight(str, x, y, SM_STATS_WIDTH, SM_STATS_HEIGHT, BLOCKFONT2);
}

void RenderSMPanel(DirtyLevel* const dirty_level)
{
	// Give him the panel
	if (gSelectSMPanelToMerc) SetSMPanelCurrentMerc(gSelectSMPanelToMerc);

	// ATE: Don't do anything if we are in stack popup and are refreshing stuff
	if ((InItemStackPopup() || InKeyRingPopup()) && *dirty_level == DIRTYLEVEL1)
		return;

	if (!gpSMCurrentMerc) return;

	SOLDIERTYPE& s = *gpSMCurrentMerc;

	if (gfCheckForMouseOverItem && GetJA2Clock() - guiMouseOverItemTime > 100)
	{
		if (HandleCompatibleAmmoUI(&s, (INT8)gbCheckForMouseOverItemPos, TRUE))
		{
			*dirty_level = DIRTYLEVEL2;
		}

		gfCheckForMouseOverItem = FALSE;
	}

	HandleNewlyAddedItems(s, dirty_level);

	if (InItemDescriptionBox()) HandleItemDescriptionBox(dirty_level);

	INT32 const dx = INTERFACE_START_X;
	INT32 const dy = INV_INTERFACE_START_Y;

	if (*dirty_level == DIRTYLEVEL2)
	{
		BltVideoSurface(guiSAVEBUFFER, guiSMPanel, dx, dy, nullptr);

		{
			SGPVObject const* gfx;
			if (gfSMDisableForItems)
			{
				gfx = guiSMObjects2;
			}
			else if (gTacticalStatus.ubCurrentTeam == OUR_TEAM &&
				&s == GetSelectedMan() &&
				OK_INTERRUPT_MERC(&s))
			{
				gfx = guiSMObjects;
			}
			else
			{
				goto no_plate;
			}
			INT32 const x = SM_SELMERC_PLATE_X + dx;
			INT32 const y = SM_SELMERC_PLATE_Y + dy;
			BltVideoObject(guiSAVEBUFFER, gfx, 0, x, y);
			RestoreExternBackgroundRect(x, y, SM_SELMERC_PLATE_WIDTH, SM_SELMERC_PLATE_HEIGHT);
		}
no_plate:

		RenderSoldierFace(s, dx + SM_SELMERC_FACE_X, dy + SM_SELMERC_FACE_Y);

		if (InItemDescriptionBox())
		{
			RenderItemDescriptionBox();
		}
		else
		{
			RenderInvBodyPanel(&s, SM_BODYINV_X, SM_BODYINV_Y);

			// Render Values for stats
			SetFontDestBuffer(guiSAVEBUFFER);
			SetFontAttributes(BLOCKFONT2, STATS_TITLE_FONT_COLOR);
			for (UINT32 i = 0; i != 5; ++i)
			{
				INT32 const y = dy + 7 + i * 10;
				MPrint( dx + 92, y, pShortAttributeStrings[i]);
				MPrint(dx + 137, y, pShortAttributeStrings[i + 5]);
			}

			MPrint(dx + SM_ARMOR_LABEL_X - StringPixLength(pInvPanelTitleStrings[0], BLOCKFONT2) / 2, dy + SM_ARMOR_LABEL_Y, pInvPanelTitleStrings[0]);
			MPrint(dx + SM_ARMOR_PERCENT_X, dy + SM_ARMOR_PERCENT_Y, "%");

			MPrint(dx + SM_WEIGHT_LABEL_X - StringPixLength(pInvPanelTitleStrings[1], BLOCKFONT2), dy + SM_WEIGHT_LABEL_Y, pInvPanelTitleStrings[1]);
			MPrint(dx + SM_WEIGHT_PERCENT_X, dy + SM_WEIGHT_PERCENT_Y, "%");

			MPrint(dx + SM_CAMO_LABEL_X - StringPixLength(pInvPanelTitleStrings[2], BLOCKFONT2), dy + SM_CAMO_LABEL_Y, pInvPanelTitleStrings[2]);
			MPrint(dx + SM_CAMO_PERCENT_X, dy + SM_CAMO_PERCENT_Y, "%");

			MERCPROFILESTRUCT& p = GetProfile(s.ubProfile);
			PrintStat(s.uiChangeAgilityTime,      AGIL_INCREASE,     s.bAgility,      dx + SM_AGI_X,    dy + SM_AGI_Y,    p.sAgilityGain*2);
			PrintStat(s.uiChangeDexterityTime,    DEX_INCREASE,      s.bDexterity,    dx + SM_DEX_X,    dy + SM_DEX_Y,    p.sDexterityGain*2);
			PrintStat(s.uiChangeStrengthTime,     STRENGTH_INCREASE, s.bStrength,     dx + SM_STR_X,    dy + SM_STR_Y,    p.sStrengthGain*2);
			PrintStat(s.uiChangeLeadershipTime,   LDR_INCREASE,      s.bLeadership,   dx + SM_CHAR_X,   dy + SM_CHAR_Y,   p.sLeadershipGain*2);
			PrintStat(s.uiChangeWisdomTime,       WIS_INCREASE,      s.bWisdom,       dx + SM_WIS_X,    dy + SM_WIS_Y,    p.sWisdomGain*2);
			PrintStat(s.uiChangeLevelTime,        LVL_INCREASE,      s.bExpLevel,     dx + SM_EXPLVL_X, dy + SM_EXPLVL_Y, p.sExpLevelGain*100/(350*p.bExpLevel));
			PrintStat(s.uiChangeMarksmanshipTime, MRK_INCREASE,      s.bMarksmanship, dx + SM_MRKM_X,   dy + SM_MRKM_Y,   p.sMarksmanshipGain*4);
			PrintStat(s.uiChangeExplosivesTime,   EXP_INCREASE,      s.bExplosive,    dx + SM_EXPL_X,   dy + SM_EXPL_Y,   p.sExplosivesGain*4);
			PrintStat(s.uiChangeMechanicalTime,   MECH_INCREASE,     s.bMechanical,   dx + SM_MECH_X,   dy + SM_MECH_Y,   p.sMechanicGain*4);
			PrintStat(s.uiChangeMedicalTime,      MED_INCREASE,      s.bMedical,      dx + SM_MED_X,    dy + SM_MED_Y,    p.sMedicalGain*4);

			SetFontForeground(s.bLife >= OKLIFE ? STATS_TEXT_FONT_COLOR : FONT_MCOLOR_DKGRAY);

			INT16   usX;
			INT16   usY;
			ST::string sString;

			// Display armour value
			sString = ST::format("{3d}", ArmourPercent(&s));
			FindFontRightCoordinates(dx + SM_ARMOR_X, dy + SM_ARMOR_Y, SM_PERCENT_WIDTH, SM_PERCENT_HEIGHT, sString, BLOCKFONT2, &usX, &usY);
			MPrint(usX, usY , sString);

			// Display weight value
			sString = ST::format("{3d}", CalculateCarriedWeight(&s));
			FindFontRightCoordinates(dx + SM_WEIGHT_X, dy + SM_WEIGHT_Y, SM_PERCENT_WIDTH, SM_PERCENT_HEIGHT, sString, BLOCKFONT2, &usX, &usY);
			MPrint(usX, usY, sString);

			// Display camo value
			sString = ST::format("{3d}", s.bCamo);
			FindFontRightCoordinates(dx + SM_CAMO_X, dy + SM_CAMO_Y, SM_PERCENT_WIDTH, SM_PERCENT_HEIGHT, sString, BLOCKFONT2, &usX, &usY);
			MPrint(usX, usY, sString);

			SetFontDestBuffer(FRAME_BUFFER);

			RestoreExternBackgroundRect(0, INV_INTERFACE_START_Y, SCREEN_WIDTH, SCREEN_HEIGHT - INV_INTERFACE_START_Y);
		}

		// Render name
		UINT8 const fg = s.bStealthMode ? FONT_MCOLOR_LTYELLOW : FONT_MCOLOR_LTGRAY;
		SetFontAttributes(BLOCKFONT2, fg);

		INT16 const x = SM_SELMERCNAME_X + dx;
		INT16 const y = SM_SELMERCNAME_Y + dy;
		INT16 const w = SM_SELMERCNAME_WIDTH;
		INT16 const h = SM_SELMERCNAME_HEIGHT;
		RestoreExternBackgroundRect(x, y, w, h);
		MPrint(x, y, s.name, HCenterVCenterAlign(w, h));
	}

	if (*dirty_level != DIRTYLEVEL0)
	{
		SetStatsHelp(gSM_SELMERCBarsRegion, s);

		// Display AP
		if (!(s.uiStatusFlags & SOLDIER_DEAD))
		{
			PrintAP(&s, dx + SM_SELMERC_AP_X, dy + SM_SELMERC_AP_Y, SM_SELMERC_AP_WIDTH, SM_SELMERC_AP_HEIGHT);
			DrawSoldierUIBars(s, dx + SM_SELMERC_HEALTH_X, dy + SM_SELMERC_HEALTH_Y, TRUE, FRAME_BUFFER);
		}
	}

	UpdateSMPanel();

	// Render items in guy's hand
	HandleRenderInvSlots(s, *dirty_level);

	if (gfSMDisableForItems && *dirty_level != DIRTYLEVEL0)
	{
		SGPRect ClipRect;
		ClipRect.iLeft = dx + 87;
		ClipRect.iRight = dx + 536;
		ClipRect.iTop = INV_INTERFACE_START_Y;
		ClipRect.iBottom = SCREEN_HEIGHT;
		SGPVSurface::Lock l(FRAME_BUFFER);
		Blt16BPPBufferHatchRect(l.Buffer<UINT16>(), l.Pitch(), &ClipRect);
	}
}


static void SMInvMoveCallback(MOUSE_REGION* pRegion, UINT32 iReason)
{
	UINT32 uiHandPos;

	uiHandPos = MSYS_GetRegionUserData( pRegion, 0 );

	if ( gpSMCurrentMerc->inv[ uiHandPos ].usItem == NOTHING )
		return;

	if (iReason & MSYS_CALLBACK_REASON_GAIN_MOUSE)
	{
		if (gpItemPointer == nullptr)
		{
			// Setup a timer....
			guiMouseOverItemTime = GetJA2Clock( );
			gfCheckForMouseOverItem = TRUE;
			gbCheckForMouseOverItemPos = (INT8)uiHandPos;
		}
	}
	else if (iReason & MSYS_CALLBACK_REASON_LOST_MOUSE )
	{
		if (gpItemPointer == nullptr)
		{
			HandleCompatibleAmmoUI( gpSMCurrentMerc, (INT8)uiHandPos, FALSE );
			gfCheckForMouseOverItem = FALSE;
			fInterfacePanelDirty = DIRTYLEVEL2;
		}
	}
}


static void SMInvMoveCamoCallback(MOUSE_REGION* const pRegion, const UINT32 iReason)
{
	if (iReason & MSYS_CALLBACK_REASON_GAIN_MOUSE )
	{
		// Setup a timer....
		guiMouseOverItemTime = GetJA2Clock( );
		gfCheckForMouseOverItem = TRUE;
		gbCheckForMouseOverItemPos = NO_SLOT;
	}
	else if (iReason & MSYS_CALLBACK_REASON_LOST_MOUSE )
	{
		HandleCompatibleAmmoUI( gpSMCurrentMerc, (INT8)NO_SLOT, FALSE );
		gfCheckForMouseOverItem = FALSE;
	}
}


static void SMInvClickCamoCallback(MOUSE_REGION* pRegion, UINT32 iReason)
{
	if (iReason & MSYS_CALLBACK_REASON_POINTER_DWN)
	{
		// Apply camo (if we have something in cursor...)
		// If we do not have an item in hand, start moving it
		OBJECTTYPE* const obj = gpItemPointer;
		if (obj == nullptr) return;

		SOLDIERTYPE* const s = gpSMCurrentMerc;
		if (TacticalInventory().PlanApply(InventoryHand(), InventoryPartyOf(s)) != Equipment::InvWhy::None) return;

		BOOLEAN fGoodAPs;
		// Try to apply camo....
		if (ApplyCamo(s, obj, &fGoodAPs))
		{
			if (!fGoodAPs) return;

			// Say OK acknowledge....
			DoMercBattleSound(s, BATTLE_SOUND_COOL1);
		}
		else if (ApplyCanteen(s, obj, &fGoodAPs))
		{
			if (!fGoodAPs) return;
		}
		else if (ApplyElixir(s, obj, &fGoodAPs))
		{
			if (!fGoodAPs) return;

			// Say OK acknowledge....
			DoMercBattleSound(s, BATTLE_SOUND_COOL1);
		}
		else if (ApplyDrugs(s, obj))
		{
			// Say OK acknowledge....
			DoMercBattleSound(s, BATTLE_SOUND_COOL1);
		}
		else
		{
			ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_UI_FEEDBACK, TacticalStr[CANNOT_DO_INV_STUFF_STR]);
			return;
		}

		fInterfacePanelDirty = DIRTYLEVEL2;

		// Check if it's the same now!
		if (obj->ubNumberOfObjects == 0)
		{
			gbCompatibleApplyItem = FALSE;
			EndItemPointer();
		}
	}
}


static void SMInvClickCallbackPrimary(MOUSE_REGION* pRegion, UINT32 iReason)
{
	UINT32 const uiHandPos = MSYS_GetRegionUserData(pRegion, 0);
	if (fInMapMode) return; // XXX necessary?
	InventorySlotClick(gpSMCurrentMerc, static_cast<int>(uiHandPos), false, _KeyDown(CTRL));
}


static void SMInvClickCallbackSecondary(MOUSE_REGION* pRegion, UINT32 iReason)
{
	UINT32 const uiHandPos = MSYS_GetRegionUserData(pRegion, 0);
	if (fInMapMode) return; // XXX necessary?
	InventorySlotClick(gpSMCurrentMerc, static_cast<int>(uiHandPos), true, false);
}




static void SelectedMercPopupMoveCallback(MOUSE_REGION* pRegion, uint32_t iReason)
{
	if (gpSMCurrentMerc == nullptr)
		return;

	if (iReason & MSYS_CALLBACK_REASON_GAIN_MOUSE) {
		Attributes attr = static_cast<Attributes>(pRegion - gSM_SELMERCStatsRegion);
		pRegion->SetFastHelpText(GetModifiersForEffectiveAttributes(gpSMCurrentMerc, attr));
	}
}









void RenderTownIDString(void)
{
	// Render town, position
	SetFontAttributes(COMPFONT, 183);
	ST::string zTownIDString = GetSectorIDString(gWorldSector, TRUE);
	zTownIDString = ReduceStringLength(zTownIDString, 80, COMPFONT);
	MPrint(INTERFACE_START_X + g_ui.m_teamPanelSlotsTotalWidth + 50,
		SCREEN_HEIGHT - 55, zTownIDString, HCenterVCenterAlign(80, 16));
}





void ShopKeeperInterface_SetSMpanelButtonsState(bool const enabled)
{
	//Go through the buttons that will be under the ShopKeepers ATM panel and disable them
	EnableButton( iSMPanelButtons[ STANCEUP_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ UPDOWN_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ CLIMB_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ STANCEDOWN_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ HANDCURSOR_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ BURSTMODE_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ LOOK_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ TALK_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ MUTE_BUTTON ], enabled );

	EnableButton( giSMStealthButton, enabled );

	//Make sure the options button is disabled
	EnableButton( iSMPanelButtons[ OPTIONS_BUTTON ], enabled );

	//Make sure the done and mapscreen buttons are disabled
	EnableButton( iSMPanelButtons[ SM_DONE_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ SM_MAP_SCREEN_BUTTON ], enabled );

	EnableButton( iSMPanelButtons[ STANCEUP_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ UPDOWN_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ CLIMB_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ STANCEDOWN_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ HANDCURSOR_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ BURSTMODE_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ LOOK_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ TALK_BUTTON ], enabled );
	EnableButton( iSMPanelButtons[ MUTE_BUTTON ], enabled );

	EnableButton( giSMStealthButton, enabled );
}







static void SMInvMoneyButtonCallback(MOUSE_REGION* pRegion, UINT32 iReason)
{
	if (iReason & MSYS_CALLBACK_REASON_POINTER_DWN) InventoryCashButton();
}


/** The money button of the single-merc panel: deposit the money in the hand, or open the withdrawal. */





static std::unique_ptr<SGPVSurface> CreateVideoSurfaceFromObjectFile(const ST::string& filename, UINT16 usRegionIndex)
{
	AutoSGPVObject vo(AddVideoObjectFromFile(filename));
	auto r = vo->SubregionProperties(usRegionIndex);
	auto sf = std::make_unique<SGPVSurface>(r.usWidth, r.usHeight, PIXEL_DEPTH);
	BltVideoObject(sf.get(), vo.get(), usRegionIndex, 0, 0);

	return sf;
}


// The native tactical HUD's cash button (deposit / withdraw): the button's own action, no hidden region.


// ---- the panel as the shopkeeper screen uses it ---------------------------------------------------------------

bool ShopPanelUp() { return g_shopPanelUp; }

void ShopPanelRefresh()
{
	if (g_shopPanelUp && gpSMCurrentMerc) UpdateSMPanel();
}

void ShopPanelEnableButtons(BOOLEAN const fEnable, BOOLEAN const fFromItemPickup)
{
	if (!g_shopPanelUp) return;

	if (fEnable)
	{
		// only enable the following if NOT in shopkeeper's interface
		if (guiCurrentScreen != SHOPKEEPER_SCREEN)
		{
			EnableButton( iSMPanelButtons[ CLIMB_BUTTON ] );
			EnableButton( iSMPanelButtons[ BURSTMODE_BUTTON ] );
			EnableButton( iSMPanelButtons[ STANCEUP_BUTTON ] );
			EnableButton( iSMPanelButtons[ STANCEDOWN_BUTTON ] );
			EnableButton( iSMPanelButtons[ LOOK_BUTTON ] );
			EnableButton( iSMPanelButtons[ UPDOWN_BUTTON ] );
			EnableButton( iSMPanelButtons[ HANDCURSOR_BUTTON ] );
			if (giSMStealthButton) EnableButton(giSMStealthButton);

			bool const enable = !gfDisableTacticalPanelButtons;
			EnableButton(iSMPanelButtons[OPTIONS_BUTTON],       enable);
			EnableButton(iSMPanelButtons[SM_DONE_BUTTON],       enable);
			EnableButton(iSMPanelButtons[SM_MAP_SCREEN_BUTTON], enable);

			//enable the radar map region
			gRadarRegion.Enable();
		}

		if ( !fFromItemPickup )
		{
			EnableButton( iSMPanelButtons[ NEXTMERC_BUTTON ] );
			EnableButton( iSMPanelButtons[ PREVMERC_BUTTON ] );
		}
	}
	else
	{
		DisableButton( iSMPanelButtons[ CLIMB_BUTTON ] );
		DisableButton( iSMPanelButtons[ BURSTMODE_BUTTON ] );
		DisableButton( iSMPanelButtons[ STANCEUP_BUTTON ] );
		DisableButton( iSMPanelButtons[ STANCEDOWN_BUTTON ] );
		DisableButton( iSMPanelButtons[ LOOK_BUTTON ] );
		DisableButton( iSMPanelButtons[ UPDOWN_BUTTON ] );
		DisableButton( iSMPanelButtons[ HANDCURSOR_BUTTON ] );
		if (giSMStealthButton) DisableButton(giSMStealthButton);

		if ( !fFromItemPickup )
		{
			DisableButton( iSMPanelButtons[ NEXTMERC_BUTTON ] );
			DisableButton( iSMPanelButtons[ PREVMERC_BUTTON ] );
		}

		DisableButton( iSMPanelButtons[ OPTIONS_BUTTON ] );
		DisableButton( iSMPanelButtons[ SM_DONE_BUTTON ] );
		DisableButton( iSMPanelButtons[ SM_MAP_SCREEN_BUTTON ] );

		gRadarRegion.Disable();
	}

	gfAllDisabled = !fEnable;
}


static void SelectMerc(SOLDIERTYPE* const s)
{
	gSelectSMPanelToMerc = s;

	if (!gfInItemPickupMenu)
	{
		BOOLEAN set_locator;
		if (guiCurrentScreen == SHOPKEEPER_SCREEN)
		{
			// Refresh background for player slots (in case item values change due to Flo's discount)
			gubSkiDirtyLevel = SKI_DIRTY_LEVEL2;
			set_locator = DONTSETLOCATOR;
		}
		else
		{
			set_locator = SETLOCATOR;
		}
		LocateSoldier(s, set_locator);
	}

	// If the user is in the shop keeper interface and is in the item desc
	if (guiCurrentScreen == SHOPKEEPER_SCREEN && InItemDescriptionBox())
	{
		DeleteItemDescriptionBox();
	}
}


static void BtnPrevMercCallback(GUI_BUTTON* btn, UINT32 reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP)
	{
		SelectMerc(FindPrevActiveAndAliveMerc(gpSMCurrentMerc, TRUE, TRUE));
	}
}


static void BtnNextMercCallback(GUI_BUTTON* btn, UINT32 reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP)
	{
		SelectMerc(FindNextActiveAndAliveMerc(gpSMCurrentMerc, TRUE, TRUE));
	}
}
