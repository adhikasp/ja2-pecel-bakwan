#include "UILayout.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "GamePolicy.h"
#include "JAScreens.h"
#include "MapScreen.h"
#include "ScreenIDs.h"
#include "Soldier_Control.h"
#include "HImage.h"
#include "VSurface.h"
#include <algorithm>
#include <stdexcept>
#include <string_theory/string>

#define MIN_INTERFACE_WIDTH       640
#define MIN_INTERFACE_HEIGHT      480

/**
 * Default screen layout.
 * It might be changed later when the window size is known for sure. */
UILayout g_ui(MIN_INTERFACE_WIDTH, MIN_INTERFACE_HEIGHT);


/** Constructor. */
UILayout::UILayout(UINT16 screenWidth, UINT16 screenHeight)
	:m_mapScreenWidth(MIN_INTERFACE_WIDTH), m_mapScreenHeight(MIN_INTERFACE_HEIGHT),
	m_screenWidth(screenWidth), m_screenHeight(screenHeight)
{
}


void UILayout::setScreenSize(UINT16 width, UINT16 height)
{
	if (width < MIN_INTERFACE_WIDTH || height < MIN_INTERFACE_HEIGHT)
	{
		ST::string err = ST::format("Failed to set screen resolution {} x {}", width, height);
		throw std::runtime_error(err.to_std_string());
	}
	m_screenWidth = width;
	m_screenHeight = height;
}


void UILayout::setLayers(VideoLayout::LayerLayout const& layers)
{
	setScreenSize(layers.ui.w, layers.ui.h);
	m_layered     = layers.layered;
	m_uiScale     = static_cast<UINT8>(layers.uiScale);
	m_worldZoom   = static_cast<UINT8>(layers.worldZoom);
	m_worldWidth  = static_cast<UINT16>(layers.world.w);
	m_worldHeight = static_cast<UINT16>(layers.world.h);
}


LayerPoint UILayout::uiToWorld(INT32 const x, INT32 const y) const
{
	return { uiToWorld(x), uiToWorld(y) };
}


LayerPoint UILayout::worldToUi(INT32 const x, INT32 const y) const
{
	return { worldToUi(x), worldToUi(y) };
}


INT32 UILayout::uiToWorld(INT32 const v) const
{
	return m_layered ? VideoLayout::UiToWorld(v, m_uiScale, m_worldZoom) : v;
}


INT32 UILayout::worldToUi(INT32 const v) const
{
	return m_layered ? VideoLayout::WorldToUi(v, m_uiScale, m_worldZoom) : v;
}


SGPPoint UILayout::anchorIn(UINT16 screen_w, UINT16 screen_h, Anchor a, UINT16 w, UINT16 h, INT16 dx, INT16 dy)
{
	int x = 0;
	int y = 0;
	switch (a)
	{
		case Anchor::TopLeft:     case Anchor::Left:   case Anchor::BottomLeft:  x = 0; break;
		case Anchor::Top:         case Anchor::Center: case Anchor::Bottom:      x = (screen_w - w) / 2; break;
		case Anchor::TopRight:    case Anchor::Right:  case Anchor::BottomRight: x = screen_w - w; break;
	}
	switch (a)
	{
		case Anchor::TopLeft:     case Anchor::Top:    case Anchor::TopRight:    y = 0; break;
		case Anchor::Left:        case Anchor::Center: case Anchor::Right:       y = (screen_h - h) / 2; break;
		case Anchor::BottomLeft:  case Anchor::Bottom: case Anchor::BottomRight: y = screen_h - h; break;
	}
	SGPPoint p;
	p.set(std::max(0, x + dx), std::max(0, y + dy)); // SGPPoint is unsigned: never wrap
	return p;
}


SGPPoint UILayout::anchor(Anchor a, UINT16 w, UINT16 h, INT16 dx, INT16 dy) const
{
	return anchorIn(m_screenWidth, m_screenHeight, a, w, h, dx, dy);
}


SGPBox UILayout::stdBox() const
{
	SGPPoint p = anchor(Anchor::Center, MIN_INTERFACE_WIDTH, MIN_INTERFACE_HEIGHT);
	SGPBox b;
	b.set(p.iX, p.iY, MIN_INTERFACE_WIDTH, MIN_INTERFACE_HEIGHT);
	return b;
}


SGPBox UILayout::screenBox() const
{
	SGPBox b;
	b.set(0, 0, m_screenWidth, m_screenHeight);
	return b;
}


/** Check if the screen is bigger than original 640x480. */
bool UILayout::isBigScreen() const
{
	return (m_screenWidth > 640) || (m_screenHeight > 480);
}


UINT16 UILayout::tacticalButtonsBoxX() const
{
	return m_anchorRadarClockToScreen ? m_screenWidth - TEAMPANEL_BUTTONSBOX_WIDTH
	                                  : m_teamPanelPosition.iX + m_teamPanelSlotsTotalWidth;
}


int UILayout::getMapMessageLines() const           { return 9 + m_mapMessageExtraPx / 9;                              }
UINT16 UILayout::currentHeight() const             { return fInMapMode ? (m_mapBottomY + m_mapScreenHeight) : m_screenHeight; }
UINT16 UILayout::get_CLOCK_X() const               { return fInMapMode ? (STD_SCREEN_X + 554) : tacticalButtonsBoxX() + 56; }
UINT16 UILayout::get_CLOCK_Y() const               { return currentHeight() - 23;                                  }
UINT16 UILayout::get_RADAR_WINDOW_X() const        { return fInMapMode ? (STD_SCREEN_X + 543) : tacticalButtonsBoxX() + 45; }
UINT16 UILayout::get_RADAR_WINDOW_TM_Y() const     { return currentHeight() - 107;                                 }
UINT16 UILayout::get_INV_INTERFACE_START_Y() const { return m_screenHeight - INV_INTERFACE_HEIGHT;                                  }


void UILayout::recalculatePositions()
{
	m_teamPanelSlotsTotalWidth = getTeamPanelNumSlots() * TEAMPANEL_SLOT_WIDTH;
	UINT16 tpXOffset = (m_screenWidth - m_teamPanelSlotsTotalWidth - TEAMPANEL_BUTTONSBOX_WIDTH) / 2;
	UINT16 tpYOffset = m_screenHeight - TEAMPANEL_HEIGHT;
	m_teamPanelPosition.set(tpXOffset, tpYOffset);
	m_teamPanelWidth = m_teamPanelSlotsTotalWidth + TEAMPANEL_BUTTONSBOX_WIDTH;

	UINT16 startInvY = get_INV_INTERFACE_START_Y();
	UINT16 startX    = INTERFACE_START_X;

	m_stdScreenOffsetX            = (m_screenWidth - MIN_INTERFACE_WIDTH) / 2;
	m_stdScreenOffsetY            = (m_screenHeight - MIN_INTERFACE_HEIGHT) / 2;
	m_mapLeftX                    = 0;
	m_mapBottomY                  = m_screenHeight - MIN_INTERFACE_HEIGHT;
	{
		// The message log grows into the gap between the map panels and the bottom bar, in whole lines.
		constexpr int lineH = 9, maxExtraRows = 20, margin = 20;
		int const gap = m_stdScreenOffsetY - margin;
		m_mapMessageExtraPx = (UINT16)(gap > 0 ? std::min(maxExtraRows, gap / lineH) * lineH : 0);
	}

	// tactical screen inventory position
	m_invSlotPositionTac[HELMETPOS           ].set(startX + 344, startInvY +   6);
	m_invSlotPositionTac[VESTPOS             ].set(startX + 344, startInvY +  35);
	m_invSlotPositionTac[LEGPOS              ].set(startX + 344, startInvY +  95);
	m_invSlotPositionTac[HEAD1POS            ].set(startX + 226, startInvY +   6);
	m_invSlotPositionTac[HEAD2POS            ].set(startX + 226, startInvY +  30);
	m_invSlotPositionTac[HANDPOS             ].set(startX + 226, startInvY +  84);
	m_invSlotPositionTac[SECONDHANDPOS       ].set(startX + 226, startInvY + 108);
	m_invSlotPositionTac[BIGPOCK1POS         ].set(startX + 468, startInvY +   5);
	m_invSlotPositionTac[BIGPOCK2POS         ].set(startX + 468, startInvY +  29);
	m_invSlotPositionTac[BIGPOCK3POS         ].set(startX + 468, startInvY +  53);
	m_invSlotPositionTac[BIGPOCK4POS         ].set(startX + 468, startInvY +  77);
	m_invSlotPositionTac[SMALLPOCK1POS       ].set(startX + 396, startInvY +   5);
	m_invSlotPositionTac[SMALLPOCK2POS       ].set(startX + 396, startInvY +  29);
	m_invSlotPositionTac[SMALLPOCK3POS       ].set(startX + 396, startInvY +  53);
	m_invSlotPositionTac[SMALLPOCK4POS       ].set(startX + 396, startInvY +  77);
	m_invSlotPositionTac[SMALLPOCK5POS       ].set(startX + 432, startInvY +   5);
	m_invSlotPositionTac[SMALLPOCK6POS       ].set(startX + 432, startInvY +  29);
	m_invSlotPositionTac[SMALLPOCK7POS       ].set(startX + 432, startInvY +  53);
	m_invSlotPositionTac[SMALLPOCK8POS       ].set(startX + 432, startInvY +  77);

	// map screen inventory position
	m_invSlotPositionMap[HELMETPOS].set(m_mapLeftX + 204, m_stdScreenOffsetY + 116);
	m_invSlotPositionMap[VESTPOS].set(m_mapLeftX + 204, m_stdScreenOffsetY + 145);
	m_invSlotPositionMap[LEGPOS].set(m_mapLeftX + 204, m_stdScreenOffsetY + 205);
	m_invSlotPositionMap[HEAD1POS].set(m_mapLeftX +  21, m_stdScreenOffsetY + 116);
	m_invSlotPositionMap[HEAD2POS].set(m_mapLeftX +  21, m_stdScreenOffsetY + 140);
	m_invSlotPositionMap[HANDPOS].set(m_mapLeftX +  21, m_stdScreenOffsetY + 194);
	m_invSlotPositionMap[SECONDHANDPOS].set(m_mapLeftX +  21, m_stdScreenOffsetY + 218);
	m_invSlotPositionMap[BIGPOCK1POS].set(m_mapLeftX +  98, m_stdScreenOffsetY + 251);
	m_invSlotPositionMap[BIGPOCK2POS].set(m_mapLeftX +  98, m_stdScreenOffsetY + 275);
	m_invSlotPositionMap[BIGPOCK3POS].set(m_mapLeftX +  98, m_stdScreenOffsetY + 299);
	m_invSlotPositionMap[BIGPOCK4POS].set(m_mapLeftX +  98, m_stdScreenOffsetY + 323);
	m_invSlotPositionMap[SMALLPOCK1POS].set(m_mapLeftX +  22, m_stdScreenOffsetY + 251);
	m_invSlotPositionMap[SMALLPOCK2POS].set(m_mapLeftX +  22, m_stdScreenOffsetY + 275);
	m_invSlotPositionMap[SMALLPOCK3POS].set(m_mapLeftX +  22, m_stdScreenOffsetY + 299);
	m_invSlotPositionMap[SMALLPOCK4POS].set(m_mapLeftX +  22, m_stdScreenOffsetY + 323);
	m_invSlotPositionMap[SMALLPOCK5POS].set(m_mapLeftX +  60, m_stdScreenOffsetY + 251);
	m_invSlotPositionMap[SMALLPOCK6POS].set(m_mapLeftX +  60, m_stdScreenOffsetY + 275);
	m_invSlotPositionMap[SMALLPOCK7POS].set(m_mapLeftX +  60, m_stdScreenOffsetY + 299);
	m_invSlotPositionMap[SMALLPOCK8POS].set(m_mapLeftX +  60, m_stdScreenOffsetY + 323);

	m_invCamoRegion.set(SM_BODYINV_X, SM_BODYINV_Y);

	m_progress_bar_box.set(STD_SCREEN_X + 5, 2, MIN_INTERFACE_WIDTH - 10, 12);
	m_moneyButtonLoc.set(startX + 343, startInvY + 11);
	m_MoneyButtonLocMap.set(m_mapLeftX + 174, m_stdScreenOffsetY + 115);

	m_VIEWPORT_START_X            = 0;
	m_VIEWPORT_START_Y            = 0;
	m_VIEWPORT_WINDOW_START_Y     = 0;
	m_VIEWPORT_END_X              = m_screenWidth;
	m_VIEWPORT_END_Y              = m_screenHeight - 120;
	m_VIEWPORT_WINDOW_END_Y       = m_screenHeight - 120;
	if (m_layered)
	{
		// The world fills its whole buffer, under the HUD. "Centre" is the centre of the part the HUD does
		// not cover, so that locating a merc puts him in the middle of what the player can see.
		m_tacticalMapCenterX      = m_worldWidth / 2;
		m_tacticalMapCenterY      = uiToWorld(m_VIEWPORT_END_Y - m_VIEWPORT_START_Y) / 2;
		m_worldClippingRect.set(0, 0, m_worldWidth, m_worldHeight);
	}
	else
	{
		m_tacticalMapCenterX      = (m_VIEWPORT_END_X - m_VIEWPORT_START_X) / 2;
		m_tacticalMapCenterY      = (m_VIEWPORT_END_Y - m_VIEWPORT_START_Y) / 2;
		m_worldClippingRect.set(0, 0, m_screenWidth, m_screenHeight - 120);
	}

	m_contractPosition.set(       m_mapLeftX + 120, m_stdScreenOffsetY +  50);
	m_attributePosition.set(      m_mapLeftX + 220, m_stdScreenOffsetY + 150);
	m_trainPosition.set(          m_mapLeftX + 160, m_stdScreenOffsetY + 150);
	m_vehiclePosition.set(        m_mapLeftX + 160, m_stdScreenOffsetY + 150);
	m_repairPosition.set(         m_mapLeftX + 160, m_stdScreenOffsetY + 150);
	m_assignmentPosition.set(     m_mapLeftX + 120, m_stdScreenOffsetY + 150);
	m_squadPosition.set(          m_mapLeftX + 160, m_stdScreenOffsetY + 150);
	m_versionPosition.set(        10, m_screenHeight - 15);
}

/** Get X position of tactical textbox. */
UINT16 UILayout::getTacticalTextBoxX() const
{

	if ( guiCurrentScreen == MAP_SCREEN )
	{
		return STD_SCREEN_X + 110;
	}
	else
	{
		return stdBox().x + 110;
	}
}

/** Get Y position of tactical textbox. */
UINT16 UILayout::getTacticalTextBoxY() const
{
	if ( guiCurrentScreen == MAP_SCREEN )
	{
		return DEFAULT_EXTERN_PANEL_Y_POS;
	}
	else
	{
		return 20;
	}
}

UINT16 UILayout::getTeamPanelNumSlots() const
{
	if (!GCM || !GCM->getGamePolicy())
	{
		throw std::runtime_error("ContentManager is not initialized yet. Unable to determine num of team slots");
	}

	int numSlots = std::min({(int)gamepolicy(squad_size), (m_screenWidth - TEAMPANEL_BUTTONSBOX_WIDTH) / TEAMPANEL_SLOT_WIDTH, 12});
	numSlots = std::max((int)numSlots, 6);
	return numSlots;
}


namespace {

inline UINT32 Hash2(UINT32 a, UINT32 b)
{
	UINT32 h = a * 374761393u + b * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

inline UINT16 Rgb(int r, int g, int b)
{
	auto clamp = [](int v) { return (UINT8)(v < 0 ? 0 : v > 255 ? 255 : v); };
	return Get16BPPColor(clamp(r), clamp(g), clamp(b));
}

}


void FillMarginArt(SGPVSurface* const dst, INT32 x, INT32 y, INT32 w, INT32 h, MarginStyle const style)
{
	INT32 const x0 = std::max<INT32>(x, 0);
	INT32 const y0 = std::max<INT32>(y, 0);
	INT32 const x1 = std::min<INT32>(x + w, dst->Width());
	INT32 const y1 = std::min<INT32>(y + h, dst->Height());
	if (x0 >= x1 || y0 >= y1) return;

	SGPVSurface::Lock l(dst);
	UINT16* const buf   = l.Buffer<UINT16>();
	UINT32  const pitch = l.Pitch() / sizeof(UINT16);

	if (style == MarginStyle::Bronze)
	{
		// Riveted plates of 32x32, anchored to the screen origin so that adjacent fills line up.
		constexpr int P = 32;
		UINT16 const base    = Rgb(44, 36, 20);
		UINT16 const light   = Rgb(72, 60, 34);
		UINT16 const dark    = Rgb(20, 16, 9);
		UINT16 const rivet   = Rgb(118, 98, 56);
		UINT16 const rivetSh = Rgb(14, 11, 6);
		for (INT32 py = y0; py < y1; ++py)
		{
			for (INT32 px = x0; px < x1; ++px)
			{
				int const tx = px % P;
				int const ty = py % P;
				UINT16 c;
				if      (tx == 0 || ty == 0)      c = light;
				else if (tx == P - 1 || ty == P - 1) c = dark;
				else
				{
					int const n = (int)(Hash2(px / 2, py / 2) & 7) - 3; // fine grain
					c = Rgb(44 + n, 36 + n, 20 + n / 2);
					(void)base;
				}
				// a rivet in each corner of the plate
				int const rx = std::min(tx, P - 1 - tx);
				int const ry = std::min(ty, P - 1 - ty);
				if (rx >= 4 && rx <= 6 && ry >= 4 && ry <= 6)
				{
					c = (rx == 6 || ry == 6) ? rivetSh : rivet;
				}
				buf[py * pitch + px] = c;
			}
		}
	}
	else
	{
		// Dark desk: horizontal grain with slow drift, darkening towards the far corners.
		INT32 const W = dst->Width();
		INT32 const H = dst->Height();
		for (INT32 py = y0; py < y1; ++py)
		{
			int const rowN = (int)(Hash2(0, py) & 7) - 3;
			for (INT32 px = x0; px < x1; ++px)
			{
				int const drift = (int)(Hash2(px / 48, py / 3) & 7) - 3;
				int const dx = px - W / 2;
				int const dy = py - H / 2;
				// squared distance from the centre, scaled to 0..~40
				int const vig = (int)(((int64_t)dx * dx * 40) / ((int64_t)W * W / 4 + 1) + ((int64_t)dy * dy * 24) / ((int64_t)H * H / 4 + 1));
				int const v = 42 + rowN + drift - vig;
				buf[py * pitch + px] = Rgb(v + 8, v + 2, v - 6);
			}
		}
	}
}
