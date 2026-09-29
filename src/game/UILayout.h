#ifndef _UI_LAYOUT_H_
#define _UI_LAYOUT_H_

#include "Types.h"
#include "VideoLayout.h"

/////////////////////////////////////////////////////////////
// defines
/////////////////////////////////////////////////////////////

#define NUM_INVENTORY_SLOTS     (19)

/* Following defines allow us to not change the old code too much.
 * It will help to preserve original Stracciatella codebase. */

#define SCREEN_HEIGHT                   (g_ui.m_screenHeight)
#define SCREEN_WIDTH                    (g_ui.m_screenWidth)
#define INV_INTERFACE_START_Y           (g_ui.get_INV_INTERFACE_START_Y())
#define INV_INTERFACE_HEIGHT            (140)                                 // height of the bottom bar single-merc inventory panel
#define INTERFACE_START_X               (g_ui.m_teamPanelPosition.iX)
#define INTERFACE_START_Y               (g_ui.m_teamPanelPosition.iY)
#define gsVIEWPORT_START_X              (g_ui.m_VIEWPORT_START_X)
#define gsVIEWPORT_START_Y              (g_ui.m_VIEWPORT_START_Y)
#define gsVIEWPORT_WINDOW_START_Y       (g_ui.m_VIEWPORT_WINDOW_START_Y)
#define gsVIEWPORT_END_X                (g_ui.m_VIEWPORT_END_X)
#define gsVIEWPORT_END_Y                (g_ui.m_VIEWPORT_END_Y)
#define gsVIEWPORT_WINDOW_END_Y         (g_ui.m_VIEWPORT_WINDOW_END_Y)
#define STD_SCREEN_X                    (g_ui.m_stdScreenOffsetX)
#define STD_SCREEN_Y                    (g_ui.m_stdScreenOffsetY)
#define MAP_SCREEN_WIDTH                (g_ui.m_mapScreenWidth)
#define MAP_SCREEN_HEIGHT               (g_ui.m_mapScreenHeight)

/* The tactical world can be a layer of its own, with its own scale (see "world_zoom"). World
 * pixels are then not UI pixels: the world is rendered into a WORLD_BUFFER of
 * WORLD_SCREEN_WIDTH x WORLD_SCREEN_HEIGHT pixels, under the UI layer. Without layers all of these
 * are the same as the UI values above. Code that renders, clips or hit-tests the world uses these;
 * code that places UI (menus, text, regions) keeps using the gsVIEWPORT_* values. */
#define WORLD_SCREEN_WIDTH              (g_ui.worldWidth())
#define WORLD_SCREEN_HEIGHT             (g_ui.worldHeight())
#define gsWORLD_VIEWPORT_START_X        (g_ui.worldViewStartX())
#define gsWORLD_VIEWPORT_START_Y        (g_ui.worldViewStartY())
#define gsWORLD_VIEWPORT_WINDOW_START_Y (g_ui.worldWindowStartY())
#define gsWORLD_VIEWPORT_END_X          (g_ui.worldViewEndX())
#define gsWORLD_VIEWPORT_END_Y          (g_ui.worldViewEndY())
#define gsWORLD_VIEWPORT_WINDOW_END_Y   (g_ui.worldWindowEndY())

#define SM_BODYINV_X                    (INTERFACE_START_X + 244)
#define SM_BODYINV_Y                    (INV_INTERFACE_START_Y + 6)
#define SM_INVINTERFACE_WIDTH           (532)    // width of the single-merc inventory panel excluding the right-side buttons and minimap

#define EDITOR_TASKBAR_HEIGHT           (120)
#define EDITOR_TASKBAR_POS_Y            (UINT16)(SCREEN_HEIGHT - EDITOR_TASKBAR_HEIGHT)

#define DEFAULT_EXTERN_PANEL_X_POS      (STD_SCREEN_X + 320)
#define DEFAULT_EXTERN_PANEL_Y_POS      (STD_SCREEN_Y + 40)

#define TEAMPANEL_SLOT_WIDTH            (83)     // width of one slot in the bottom team panel
#define TEAMPANEL_BUTTONSBOX_WIDTH      (142)    // width of the container of the buttons on the right of team panel
#define TEAMPANEL_HEIGHT                (120)    // height of the bottom bar team panel


/////////////////////////////////////////////////////////////
// type definitions
/////////////////////////////////////////////////////////////

// USED TO SETUP REGION POSITIONS, ETC
struct INV_REGION_DESC
{
	UINT16     uX;
	UINT16     uY;

	void set(UINT16 x, UINT16 y)
	{
		uX = x;
		uY = y;
	}
};


struct MoneyLoc
{
	UINT16 x;
	UINT16 y;

	void set(UINT16 _x, UINT16 _y)
	{
		x = _x;
		y = _y;
	}
};


/** Anchor point on the screen used by UILayout::anchor. */
enum class Anchor
{
	TopLeft,    Top,     TopRight,
	Left,       Center,  Right,
	BottomLeft, Bottom,  BottomRight
};


/** A point that may be negative, unlike SGPPoint (world positions can be off the screen). */
struct LayerPoint
{
	INT32 x;
	INT32 y;
};


/** User Interface layout definition. */
struct UILayout
{
public:
	UINT16                m_mapScreenWidth;
	UINT16                m_mapScreenHeight;
	UINT16                m_screenWidth;
	UINT16                m_screenHeight;
	INV_REGION_DESC       m_invSlotPositionMap[NUM_INVENTORY_SLOTS];      /**< Map screen inventory slots positions  */
	INV_REGION_DESC       m_invSlotPositionTac[NUM_INVENTORY_SLOTS];      /**< Tactical screen Inventory slots positions */
	INV_REGION_DESC       m_invCamoRegion;                                /**< Camo (body) region in the inventory. */

	SGPBox                m_progress_bar_box;
	MoneyLoc              m_moneyButtonLoc;
	MoneyLoc              m_MoneyButtonLocMap;

	/** Viewport coordiantes.
	 * Viewport is the area of the screen where tactical map is displayed.
	 * For 640x480 it is (320, 180) */
	UINT16                m_VIEWPORT_START_X;
	UINT16                m_VIEWPORT_START_Y;
	UINT16                m_VIEWPORT_WINDOW_START_Y;

	UINT16                m_VIEWPORT_END_X;
	UINT16                m_VIEWPORT_END_Y;
	UINT16                m_tacticalMapCenterX;                           /**< Center of the tactical map in WORLD pixels (for 640x480 it is (320, 180)). */
	UINT16                m_tacticalMapCenterY;                           /**< Center of the tactical map in WORLD pixels (for 640x480 it is (320, 180)). */

	UINT16                m_VIEWPORT_WINDOW_END_Y;

	SGPRect               m_worldClippingRect;                            /**< In WORLD pixels. */

	// Layers (see VideoLayout::LayerLayout). Not layered: the world is drawn into the same
	// surface as the UI, both at m_uiScale, and world pixels are UI pixels.
	bool                  m_layered = false;
	UINT16                m_worldWidth = 0;                               /**< World buffer size in world pixels (0: same as the screen). */
	UINT16                m_worldHeight = 0;
	UINT8                 m_uiScale = 1;                                  /**< Su: physical pixels per UI pixel */
	UINT8                 m_worldZoom = 1;                                /**< Zw: physical pixels per world pixel */

	// Map screen interface
	SGPPoint              m_versionPosition;
	SGPPoint              m_contractPosition;
	SGPPoint              m_attributePosition;
	SGPPoint              m_trainPosition;
	SGPPoint              m_vehiclePosition;
	SGPPoint              m_repairPosition;
	SGPPoint              m_assignmentPosition ;
	SGPPoint              m_squadPosition ;

	// Tactical screen bottom bar
	// It can be in the "team" (TEAM) or the "single merc inventory" (SM or INV_) mode. Both modes have the same
	// width, but the single-merc mode is slightly taller.
	SGPPoint              m_teamPanelPosition;              // offset position of the bottom bar
	UINT16                m_teamPanelSlotsTotalWidth;       // total width of all team slots in the bottom team panel
	UINT16                m_teamPanelWidth;                 // width of the entire team panel including slots and buttons

	/** Tactical only: anchor the radar and the clock to the bottom-right corner of the screen instead of the
	 * right edge of the bottom panel. Off by default (classic look); the panel art is not moved, so this is
	 * meant for skins that provide their own backdrop. */
	bool                  m_anchorRadarClockToScreen = false;

	UINT16                m_stdScreenOffsetX;             /** Offset of the standard (640x480) window */
	UINT16                m_stdScreenOffsetY;             /** Offset of the standard (640x480) window */

	/** Constructor.
	 * @param screenWidth Screen width
	 * @param screenHeight Screen height */
	UILayout(UINT16 screenWidth, UINT16 screenHeight);

	/** Set new screen size. Element positions should be recalculated after setting this. @see UILayout::recalculatePositions */
	void setScreenSize(UINT16 width, UINT16 height);

	/** Set the UI canvas size and the layers from the layout computed by VideoLayout. Element positions should
	 * be recalculated afterwards. */
	void setLayers(VideoLayout::LayerLayout const& layers);

	bool   isLayered() const  { return m_layered; }
	UINT16 worldWidth() const  { return m_layered ? m_worldWidth  : m_screenWidth;  }
	UINT16 worldHeight() const { return m_layered ? m_worldHeight : m_screenHeight; }

	/** World rendering viewport, in world pixels. Without layers the same as the gsVIEWPORT_* values (which
	 * change with the interface panel); with layers the world fills its whole buffer, under the HUD. */
	UINT16 worldViewStartX() const     { return m_layered ? 0              : m_VIEWPORT_START_X;        }
	UINT16 worldViewStartY() const     { return m_layered ? 0              : m_VIEWPORT_START_Y;        }
	UINT16 worldWindowStartY() const   { return m_layered ? 0              : m_VIEWPORT_WINDOW_START_Y; }
	UINT16 worldViewEndX() const       { return m_layered ? m_worldWidth   : m_VIEWPORT_END_X;          }
	UINT16 worldViewEndY() const       { return m_layered ? m_worldHeight  : m_VIEWPORT_END_Y;          }
	UINT16 worldWindowEndY() const     { return m_layered ? m_worldHeight  : m_VIEWPORT_WINDOW_END_Y;   }

	/** Convert a position in UI pixels (the mouse, regions, everything UI code deals in) to world pixels (tile
	 * rendering and picking), and back. Identity without layers. */
	LayerPoint uiToWorld(INT32 x, INT32 y) const;
	LayerPoint worldToUi(INT32 x, INT32 y) const;
	INT32      uiToWorld(INT32 v) const;
	INT32      worldToUi(INT32 v) const;

	/** Position of a w x h rectangle attached to the given anchor of a screen_w x screen_h screen, shifted by (dx, dy).
	 * Pure function, no dependency on game state. */
	static SGPPoint anchorIn(UINT16 screen_w, UINT16 screen_h, Anchor a, UINT16 w, UINT16 h, INT16 dx = 0, INT16 dy = 0);

	/** Top-left position of a w x h rectangle attached to the given anchor of the current screen. */
	SGPPoint anchor(Anchor a, UINT16 w, UINT16 h, INT16 dx = 0, INT16 dy = 0) const;

	/** The standard (640x480) box centred on the current screen. Replaces ad-hoc STD_SCREEN_X + n arithmetic in new code. */
	SGPBox stdBox() const;

	/** The rectangle of the whole screen. */
	SGPBox screenBox() const;

	/** Check if the screen is bigger than original 640x480. */
	bool isBigScreen() const;

	/** Left edge of the box with the radar and clock in tactical (right end of the panel, or of the screen). */
	UINT16 tacticalButtonsBoxX() const;

	UINT16 currentHeight() const;
	UINT16 get_CLOCK_X() const;
	UINT16 get_CLOCK_Y() const;
	UINT16 get_INV_INTERFACE_START_Y() const;
	UINT16 get_RADAR_WINDOW_X() const;
	UINT16 get_RADAR_WINDOW_TM_Y() const;

	/** Get X position of tactical textbox. */
	UINT16 getTacticalTextBoxX() const;

	/** Get Y position of tactical textbox. */
	UINT16 getTacticalTextBoxY() const;

	/** Number of displayable slots in the team panel, based on the game policy and screen width. */
	UINT16 getTeamPanelNumSlots() const;

	/** Recalculate UI elements' positions after changing screen size.
	 *  This method requires the game data to be loaded, but it should be called before most other the application initialization is done.
	 */
	void recalculatePositions();
};

/////////////////////////////////////////////////////////////
// external declarations
/////////////////////////////////////////////////////////////

extern UILayout g_ui;

/////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////

#endif
