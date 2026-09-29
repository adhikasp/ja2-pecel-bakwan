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
#define MAPLEFT_X                       (g_ui.m_mapLeftX)              // left edge of the map screen left column (character list and info panel)
#define MAPTOP_Y                        (g_ui.m_mapTopY)               // top edge of the map screen left column
#define MAP_MESSAGE_EXTRA_Y             (g_ui.m_mapMessageExtraPx)
#define MAP_MESSAGE_EXTRA_X             (g_ui.m_map.logExtraW)         // extra width of the map screen message log
#define MAPBOT_Y                        (g_ui.m_mapBottomY)            // top edge of the map screen bottom bar art minus 359, i.e. the bar is at MAPBOT_Y + 359
#define MAPBOTR_X                       (g_ui.m_map.barRightX)         // right part of the bottom bar (money, clock, radar): art x = n is at MAPBOTR_X + n
#define MAP_LIST_EXTRA_Y                (g_ui.m_map.listExtra)         // extra height of the character list (the vehicle rows move down by this)
// The sector inventory (379 x 359, in place of the map border in 640x480) is centred on the map frame: art x = n is at MAPINV_X + n.
#define MAPINV_X                        (g_ui.m_map.frame.x + (g_ui.m_map.frame.w - 379) / 2 - 261)
#define MAPINV_Y                        (g_ui.m_map.frame.y + (g_ui.m_map.frame.h - 359) / 2)

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


class SGPVSurface;

/** Procedural backdrop art for the margins around fixed-size (640x480 era) screens on big displays. */
enum class MarginStyle
{
	Bronze, /**< riveted brass plates, matches the strategic map screen frame */
	Desk    /**< dark desk surface, for the laptop */
};

/** Fill the rectangle (x, y, w, h) of dst with margin art (clipped to dst). Deterministic. */
void FillMarginArt(SGPVSurface* dst, INT32 x, INT32 y, INT32 w, INT32 h, MarginStyle style);

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


/** Layout of the strategic map screen (the "expanded" layout).
 *
 * - The left column (character info panel, then the character list or the merc inventory) is at native
 *   size in the top-left corner and reaches down to the bottom bar.
 * - The sector map in its border frame takes the rest above the bottom bar, scaled by scale2 / 2.
 * - The bottom bar spans the whole width: the message log on the left, money, clock and radar on the right.
 *
 * The sector map keeps being drawn at native size, exactly as in 640x480, into the "canvas": a rectangle
 * of the frame buffer at the top-left corner of the scaled map. Once per frame the canvas is copied aside
 * and stretched (nearest neighbour) over the whole scaled rectangle. So everything the map code draws
 * (terrain, shading, icons, paths, town names, the helicopter, the cursor highlight) ends up in the right
 * place untouched; what reads the mouse or puts UI next to the map (clicks, popups, grid labels, the
 * frame) goes through canvasToScreen / screenToCanvas.
 *
 * All values are screen pixels. At 640x480 this is the classic layout, pixel for pixel. */
struct MapScreenGeometry
{
	// Classic (640x480) sizes of the pieces
	static constexpr INT32 COLUMN_W    = 261; // left column
	static constexpr INT32 INFO_H      = 107; // character info panel
	static constexpr INT32 LIST_H      = 252; // character list art
	static constexpr INT32 BAR_H       = 121; // bottom bar art
	static constexpr INT32 BAR_W       = 640;
	static constexpr INT32 BAR_RIGHT_W = 285; // right part of the bottom bar (money, buttons, radar)
	static constexpr INT32 CANVAS_W    = 340; // the 16 x 16 sector grid (21 x 18 px each) and a little margin
	static constexpr INT32 CANVAS_H    = 292;
	static constexpr INT32 FRAME_L     = 27;  // map border around the canvas: row letters
	static constexpr INT32 FRAME_T     = 26;  // column numbers
	static constexpr INT32 FRAME_R     = 12;
	static constexpr INT32 FRAME_B     = 41;  // map border buttons
	static constexpr INT32 VIEW_TO_CANVAS_X = 18; // the canvas is at MAP_VIEW_START_X + 18, MAP_VIEW_START_Y + 16
	static constexpr INT32 VIEW_TO_CANVAS_Y = 16;
	static constexpr INT32 LOG_LINE_H  = 9;   // message log line height
	static constexpr INT32 LOG_MAX_EXTRA_LINES = 10;

	INT32  scale2    = 2; // map scale times two: 2 = 1x, 3 = 1.5x, 4 = 2x, 6 = 3x, ...
	SGPBox column{};      // the left column, from the top of the screen down to the bottom bar
	INT32  listExtra = 0; // extra height of the character list
	SGPBox mapArea{};     // right of the column, above the bottom bar
	SGPBox frame{};       // the map border frame
	SGPBox grid{};        // where the canvas is shown, scaled
	SGPBox canvas{};      // where the map is drawn at native size: the top-left corner of grid
	INT32  barTop    = 0; // top of the bottom bar (including a taller message log)
	INT32  logExtraH = 0; // extra height of the message log, whole lines
	INT32  logExtraW = 0; // extra width of the message log
	INT32  barRightX = 0; // the right part of the bar: its art x = n (355..639) is at barRightX + n

	/** Is the map scaled, i.e. is the canvas stretched every frame? */
	bool scaled() const { return scale2 != 2; }

	/** Is the frame bigger than the border art, so that it has to be put together from pieces? */
	bool composedFrame() const { return scaled(); }

	/** Canvas pixel -> top-left screen pixel of its scaled copy. */
	LayerPoint canvasToScreen(INT32 x, INT32 y) const;
	/** Screen pixel -> the canvas pixel shown there. */
	LayerPoint screenToCanvas(INT32 x, INT32 y) const;
	/** Canvas length -> screen length. */
	INT32      canvasToScreen(INT32 len) const { return len * scale2 / 2; }
	/** Screen rectangle of the sector (1..16, 1..16) of the map grid. */
	SGPBox     sectorBox(INT32 sx, INT32 sy) const;
	/** Sector (1..16, 1..16) under a screen pixel, or {0, 0} outside the grid. */
	LayerPoint sectorAt(INT32 x, INT32 y) const;

	/** The layout for a w x h screen. Pure function. */
	static MapScreenGeometry compute(UINT16 w, UINT16 h);
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

	/** Map screen layout, see MapScreenGeometry. */
	MapScreenGeometry     m_map;

	/** Map screen: the left column (character list, info panel, inventory) is anchored to the top-left corner. */
	UINT16                m_mapLeftX;
	UINT16                m_mapTopY;

	/** Map screen: MAP_VIEW_START_X/Y, the origin of the (native size) sector map drawing. */
	UINT16                m_mapViewX;
	UINT16                m_mapViewY;

	/** Map screen: y origin of the bottom bar (message log, clock, buttons). The bar is anchored to the bottom screen edge:
	 * its art (121 px high) starts at m_mapBottomY + 359. 0 at 640x480. */
	UINT16                m_mapBottomY;

	/** Map screen: extra height (px) of the message log above the standard bar, a multiple of the line height. 0 at 640x480. */
	UINT16                m_mapMessageExtraPx;


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

	/** Number of message lines shown in the map screen message log (9 in the standard layout, more on tall screens). */
	int getMapMessageLines() const;

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
