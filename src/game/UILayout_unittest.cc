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
