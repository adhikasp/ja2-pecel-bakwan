#include "PointerRouter.h"

#include <gtest/gtest.h>

using namespace PointerRouter;

TEST(PointerRouter, theWorldIsTheDefault)
{
	Input in;
	in.insideWorld = true;
	EXPECT_EQ(Route(in), Owner::World);
}

TEST(PointerRouter, aModalLayerTakesEverythingFirst)
{
	Input in;
	in.insideWorld = true;
	in.modalOpen = true;
	EXPECT_EQ(Route(in), Owner::Modal);
	in.atEdge = true; // even the edge strip
	EXPECT_EQ(Route(in), Owner::Modal);
	in.overHud = true;
	EXPECT_EQ(Route(in), Owner::Modal);
}

TEST(PointerRouter, theHudTakesWhatItsHitElementsCover)
{
	Input in;
	in.insideWorld = true;
	in.overHud = true;
	EXPECT_EQ(Route(in), Owner::Hud);
	in.overHud = false;
	EXPECT_EQ(Route(in), Owner::World);
}

TEST(PointerRouter, theEdgeStripIsTheWorldsWhateverTheHudHasThere)
{
	Input in;
	in.insideWorld = true;
	in.overHud = true;
	in.atEdge = true;
	EXPECT_EQ(Route(in), Owner::World);
}

TEST(PointerRouter, aLegacyRegionOnTopBlocksTheWorld)
{
	Input in;
	in.insideWorld = true;
	in.blockedByRegion = true;
	EXPECT_EQ(Route(in), Owner::None);
	// but not its own edge
	in.atEdge = true;
	EXPECT_EQ(Route(in), Owner::World);
}

TEST(PointerRouter, outsideTheWorldIsNeverTheWorld)
{
	Input in;
	EXPECT_EQ(Route(in), Owner::None);
	in.overHud = true;
	EXPECT_EQ(Route(in), Owner::Hud);
}

TEST(PointerRouter, edgesOfAnArea)
{
	Edges e = EdgesOf(0.f, 100.f, 1920.f, 1080.f, 2.f);
	EXPECT_TRUE(e.left);
	EXPECT_FALSE(e.right || e.top || e.bottom);
	e = EdgesOf(1919.f, 1079.5f, 1920.f, 1080.f, 2.f);
	EXPECT_TRUE(e.right);
	EXPECT_TRUE(e.bottom);
	EXPECT_FALSE(e.left || e.top);
	e = EdgesOf(1.9f, 2.f, 1920.f, 1080.f, 2.f);
	EXPECT_TRUE(e.left);
	EXPECT_FALSE(e.top); // 2 is outside a 2 pixel strip
	e = EdgesOf(960.f, 540.f, 1920.f, 1080.f, 2.f);
	EXPECT_FALSE(e.Any());
}

TEST(PointerRouter, theStripIsTwoOutputPixelsWhateverTheUiScale)
{
	EXPECT_FLOAT_EQ(EdgeStripCanvas(1), 2.f);
	EXPECT_FLOAT_EQ(EdgeStripCanvas(2), 1.f);
	EXPECT_FLOAT_EQ(EdgeStripCanvas(4), 0.5f);
	EXPECT_FLOAT_EQ(EdgeStripCanvas(0), 2.f);
}

TEST(PointerRouter, presenceReportsComingAndGoing)
{
	Presence p;
	EXPECT_EQ(p.Update(false), Change::None);
	EXPECT_EQ(p.Update(true), Change::Gained);
	EXPECT_TRUE(p.In());
	EXPECT_EQ(p.Update(true), Change::None);
	EXPECT_EQ(p.Update(false), Change::Lost);
	EXPECT_FALSE(p.In());
}
