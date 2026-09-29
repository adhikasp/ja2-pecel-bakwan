#include "gtest/gtest.h"

#include "UILayout.h"

#include <cstdlib>
#include <utility>

namespace
{
struct Res { UINT16 w, h; };
constexpr Res kRes[] = { {640, 480}, {1280, 720}, {2560, 1080} };
}

TEST(UILayout, AnchorCornersAndEdges)
{
	for (auto r : kRes)
	{
		auto at = [&](Anchor a, INT16 dx = 0, INT16 dy = 0) { return UILayout::anchorIn(r.w, r.h, a, 100, 50, dx, dy); };
		EXPECT_EQ(at(Anchor::TopLeft).iX, 0);
		EXPECT_EQ(at(Anchor::TopLeft).iY, 0);
		EXPECT_EQ(at(Anchor::Top).iX, (r.w - 100) / 2);
		EXPECT_EQ(at(Anchor::Top).iY, 0);
		EXPECT_EQ(at(Anchor::TopRight).iX, r.w - 100);
		EXPECT_EQ(at(Anchor::Left).iY, (r.h - 50) / 2);
		EXPECT_EQ(at(Anchor::Center).iX, (r.w - 100) / 2);
		EXPECT_EQ(at(Anchor::Center).iY, (r.h - 50) / 2);
		EXPECT_EQ(at(Anchor::Right).iX, r.w - 100);
		EXPECT_EQ(at(Anchor::BottomLeft).iY, r.h - 50);
		EXPECT_EQ(at(Anchor::Bottom).iX, (r.w - 100) / 2);
		EXPECT_EQ(at(Anchor::Bottom).iY, r.h - 50);
		EXPECT_EQ(at(Anchor::BottomRight).iX, r.w - 100);
		EXPECT_EQ(at(Anchor::BottomRight).iY, r.h - 50);
		// offsets are added
		EXPECT_EQ(at(Anchor::BottomRight, -10, -20).iX, r.w - 110);
		EXPECT_EQ(at(Anchor::BottomRight, -10, -20).iY, r.h - 70);
		EXPECT_EQ(at(Anchor::TopLeft, 7, 9).iX, 7);
		EXPECT_EQ(at(Anchor::TopLeft, 7, 9).iY, 9);
	}
}

TEST(UILayout, AnchorNeverWraps)
{
	// bigger than the screen, or pushed off it: clamps to 0 instead of wrapping the unsigned point
	auto p = UILayout::anchorIn(640, 480, Anchor::Center, 800, 600);
	EXPECT_EQ(p.iX, 0);
	EXPECT_EQ(p.iY, 0);
	p = UILayout::anchorIn(640, 480, Anchor::TopLeft, 10, 10, -5, -5);
	EXPECT_EQ(p.iX, 0);
	EXPECT_EQ(p.iY, 0);
}

TEST(UILayout, StdBoxIsCentred)
{
	for (auto r : kRes)
	{
		UILayout ui(r.w, r.h);
		SGPBox b = ui.stdBox();
		EXPECT_EQ(b.w, 640);
		EXPECT_EQ(b.h, 480);
		EXPECT_EQ(b.x, (r.w - 640) / 2);
		EXPECT_EQ(b.y, (r.h - 480) / 2);
		SGPBox s = ui.screenBox();
		EXPECT_EQ(s.w, r.w);
		EXPECT_EQ(s.h, r.h);
	}
}

TEST(UILayout, ClassicStdBoxIsScreen)
{
	UILayout ui(640, 480);
	SGPBox b = ui.stdBox();
	EXPECT_EQ(b.x, 0);
	EXPECT_EQ(b.y, 0);
}

TEST(UILayout, RadarClockAnchor)
{
	UILayout ui(1280, 720);
	ui.m_teamPanelPosition.set(322, 600);
	ui.m_teamPanelSlotsTotalWidth = 6 * TEAMPANEL_SLOT_WIDTH;
	ui.m_teamPanelWidth = ui.m_teamPanelSlotsTotalWidth + TEAMPANEL_BUTTONSBOX_WIDTH;
	// panel-anchored (default): right end of the slots
	EXPECT_EQ(ui.tacticalButtonsBoxX(), 322 + 6 * TEAMPANEL_SLOT_WIDTH);
	ui.m_anchorRadarClockToScreen = true;
	EXPECT_EQ(ui.tacticalButtonsBoxX(), 1280 - TEAMPANEL_BUTTONSBOX_WIDTH);
}

TEST(UILayout, SingleLayerWorldEqualsScreen)
{
	UILayout ui(1280, 720);
	EXPECT_FALSE(ui.isLayered());
	EXPECT_EQ(ui.worldWidth(), 1280);
	EXPECT_EQ(ui.worldHeight(), 720);
	auto const p = ui.uiToWorld(37, 91);
	EXPECT_EQ(p.x, 37);
	EXPECT_EQ(p.y, 91);
	auto const q = ui.worldToUi(37, 91);
	EXPECT_EQ(q.x, 37);
	EXPECT_EQ(q.y, 91);
}

TEST(UILayout, LayersConvertBetweenUiAndWorld)
{
	UILayout ui(1280, 720);
	ui.setLayers(VideoLayout::ComputeLayerLayout(VideoLayout::ComputeDisplayLayout({ 2560, 1440 }, 2), 1));
	EXPECT_TRUE(ui.isLayered());
	EXPECT_EQ(ui.m_screenWidth, 1280);
	EXPECT_EQ(ui.m_screenHeight, 720);
	EXPECT_EQ(ui.worldWidth(), 2560);
	EXPECT_EQ(ui.worldHeight(), 1440);
	auto const w = ui.uiToWorld(640, 360);
	EXPECT_EQ(w.x, 1280);
	EXPECT_EQ(w.y, 720);
	auto const u = ui.worldToUi(1281, 721);
	EXPECT_EQ(u.x, 640);
	EXPECT_EQ(u.y, 360);
	// off-screen positions keep their sign
	EXPECT_EQ(ui.worldToUi(-1, -1).x, -1);
}

TEST(UILayout, WorldViewportIsTheWholeWorldWhenLayered)
{
	UILayout ui(1280, 720);
	ui.setLayers(VideoLayout::ComputeLayerLayout(VideoLayout::ComputeDisplayLayout({ 2560, 1440 }, 2), 1));
	EXPECT_EQ(ui.worldViewStartX(), 0);
	EXPECT_EQ(ui.worldViewEndX(), 2560);
	EXPECT_EQ(ui.worldWindowStartY(), 0);
	EXPECT_EQ(ui.worldWindowEndY(), 1440);
}

TEST(MapScreenGeometry, ClassicAt640x480)
{
	auto const g = MapScreenGeometry::compute(640, 480);
	EXPECT_EQ(g.scale2, 2);
	EXPECT_FALSE(g.scaled());
	// the classic positions: border at (261, 0), map view at (270, 10), bar art at y 359
	EXPECT_EQ(g.frame.x, 261);
	EXPECT_EQ(g.frame.y, 0);
	EXPECT_EQ(g.frame.w, 379);
	EXPECT_EQ(g.frame.h, 359);
	EXPECT_EQ(g.canvas.x - MapScreenGeometry::VIEW_TO_CANVAS_X, 270);
	EXPECT_EQ(g.canvas.y - MapScreenGeometry::VIEW_TO_CANVAS_Y, 10);
	EXPECT_EQ(g.grid.x, g.canvas.x);
	EXPECT_EQ(g.grid.w, g.canvas.w);
	EXPECT_EQ(g.barTop, 359);
	EXPECT_EQ(g.logExtraH, 0);
	EXPECT_EQ(g.logExtraW, 0);
	EXPECT_EQ(g.barRightX, 0);
	EXPECT_EQ(g.listExtra, 0);
	EXPECT_EQ(g.column.x, 0);
	EXPECT_EQ(g.column.y, 0);
	// sector A1 is at (291, 28), 21 x 18
	SGPBox const a1 = g.sectorBox(1, 1);
	EXPECT_EQ(a1.x, 291);
	EXPECT_EQ(a1.y, 28);
	EXPECT_EQ(a1.w, 21);
	EXPECT_EQ(a1.h, 18);
}

TEST(MapScreenGeometry, ScalesTheMapToFill)
{
	struct Case { UINT16 w, h; INT32 scale2; };
	for (auto c : { Case{ 1280, 720, 3 }, Case{ 1920, 1080, 6 }, Case{ 2560, 1080, 6 }, Case{ 3440, 1440, 8 }, Case{ 800, 600, 2 } })
	{
		auto const g = MapScreenGeometry::compute(c.w, c.h);
		EXPECT_EQ(g.scale2, c.scale2) << c.w << "x" << c.h;
		// everything on screen, nothing overlapping
		EXPECT_LE(g.frame.x + g.frame.w, c.w);
		EXPECT_LE(g.frame.y + g.frame.h, g.barTop);
		EXPECT_GE(g.frame.x, g.column.x + g.column.w);
		EXPECT_EQ(g.column.h, g.barTop);
		EXPECT_EQ(g.barTop + MapScreenGeometry::BAR_H + g.logExtraH, c.h);
		EXPECT_EQ(g.listExtra, g.barTop - 359);
		EXPECT_EQ(g.logExtraH % MapScreenGeometry::LOG_LINE_H, 0);
		EXPECT_EQ(g.logExtraW, c.w - 640);
		EXPECT_EQ(g.barRightX + 640, c.w);
		// the grid is the canvas scaled, inside the frame
		EXPECT_EQ(g.grid.w, MapScreenGeometry::CANVAS_W * g.scale2 / 2);
		EXPECT_EQ(g.grid.h, MapScreenGeometry::CANVAS_H * g.scale2 / 2);
		EXPECT_EQ(g.grid.x, g.frame.x + MapScreenGeometry::FRAME_L);
		EXPECT_EQ(g.grid.x + g.grid.w + MapScreenGeometry::FRAME_R, g.frame.x + g.frame.w);
		EXPECT_GE(g.grid.y, g.frame.y + MapScreenGeometry::FRAME_T);
		EXPECT_LE(g.grid.y + g.grid.h + MapScreenGeometry::FRAME_B, g.frame.y + g.frame.h);
		if (g.scaled())
		{
			// a scaled map's frame reaches from the top of the screen to the bar
			EXPECT_EQ(g.frame.y, 0);
			EXPECT_EQ(g.frame.h, g.barTop);
		}
		EXPECT_EQ(g.canvas.x, g.grid.x);
		EXPECT_EQ(g.canvas.y, g.grid.y);
		// the frame is centred in the map area
		INT32 const left  = g.frame.x - g.mapArea.x;
		INT32 const right = g.mapArea.x + g.mapArea.w - (g.frame.x + g.frame.w);
		EXPECT_LE(std::abs(left - right), 1);
	}
}

TEST(MapScreenGeometry, CanvasAndScreenRoundTrip)
{
	for (auto res : { Res{ 640, 480 }, Res{ 1280, 720 }, Res{ 1920, 1080 }, Res{ 3440, 1440 } })
	{
		auto const g = MapScreenGeometry::compute(res.w, res.h);
		for (INT32 y = g.canvas.y; y < g.canvas.y + g.canvas.h; y += 7)
		{
			for (INT32 x = g.canvas.x; x < g.canvas.x + g.canvas.w; x += 5)
			{
				LayerPoint const s = g.canvasToScreen(x, y);
				LayerPoint const c = g.screenToCanvas(s.x, s.y);
				EXPECT_EQ(c.x, x);
				EXPECT_EQ(c.y, y);
				// the last screen pixel of the scaled copy maps back to the same canvas pixel
				LayerPoint const s2 = g.canvasToScreen(x + 1, y + 1);
				LayerPoint const c2 = g.screenToCanvas(s2.x - 1, s2.y - 1);
				EXPECT_EQ(c2.x, x);
				EXPECT_EQ(c2.y, y);
			}
		}
		// left of / above the grid is outside the canvas
		EXPECT_LT(g.screenToCanvas(g.grid.x - 1, g.grid.y).x, g.canvas.x);
		EXPECT_LT(g.screenToCanvas(g.grid.x, g.grid.y - 1).y, g.canvas.y);
	}
}

TEST(MapScreenGeometry, SectorsUnderTheMouse)
{
	for (auto res : { Res{ 640, 480 }, Res{ 1280, 720 }, Res{ 1920, 1080 }, Res{ 2560, 1080 }, Res{ 3440, 1440 } })
	{
		auto const g = MapScreenGeometry::compute(res.w, res.h);
		for (INT32 sy = 1; sy <= 16; ++sy)
		{
			for (INT32 sx = 1; sx <= 16; ++sx)
			{
				SGPBox const b = g.sectorBox(sx, sy);
				// the sectors tile the grid: 21 x 18 scaled, give or take the rounding of 1.5x
				EXPECT_LE(std::abs(b.w - 21 * g.scale2 / 2), 1);
				EXPECT_LE(std::abs(b.h - 18 * g.scale2 / 2), 1);
				for (auto [x, y] : { std::pair<INT32, INT32>{ b.x, b.y }, { b.x + b.w - 1, b.y + b.h - 1 }, { b.x + b.w / 2, b.y + b.h / 2 } })
				{
					LayerPoint const s = g.sectorAt(x, y);
					EXPECT_EQ(s.x, sx) << res.w << "x" << res.h << " at " << x << "," << y;
					EXPECT_EQ(s.y, sy);
				}
			}
		}
		// off the grid
		SGPBox const a1 = g.sectorBox(1, 1);
		EXPECT_EQ(g.sectorAt(a1.x - 1, a1.y).x, 0);
		EXPECT_EQ(g.sectorAt(a1.x, a1.y - 1).x, 0);
		SGPBox const p16 = g.sectorBox(16, 16);
		EXPECT_EQ(g.sectorAt(p16.x + p16.w, p16.y).x, 0);
		EXPECT_EQ(g.sectorAt(p16.x, p16.y + p16.h).x, 0);
	}
}
