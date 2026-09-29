#include "gtest/gtest.h"

#include "UILayout.h"

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
