#include "gtest/gtest.h"

#include "DragDrop.h"

using namespace Equipment;

namespace
{
	CardDrop Give(int tiles = 1)
	{
		CardDrop d;
		d.giver.merc = 1;
		d.receiver.merc = 2;
		d.tiles = tiles;
		d.dropAp = 1;
		return d;
	}
}

TEST(DragGesture, ClickStaysAClickUntilTheThreshold)
{
	DragGesture g;
	g.Press(100, 100, 7);
	EXPECT_TRUE(g.Pressed());
	EXPECT_FALSE(g.Move(102, 101, 6));
	EXPECT_FALSE(g.Dragging());
	EXPECT_FALSE(g.Release()); // a click, not a drag
	EXPECT_FALSE(g.Pressed());
}

TEST(DragGesture, CrossingTheThresholdStartsOnceAndKeepsTheSlot)
{
	DragGesture g;
	g.Press(100, 100, 7);
	EXPECT_TRUE(g.Move(104, 105, 6)); // 6.4 px
	EXPECT_TRUE(g.Dragging());
	EXPECT_FALSE(g.Move(140, 140, 6)); // already dragging: no second start
	EXPECT_EQ(g.Slot(), 7);
	EXPECT_TRUE(g.Release());
	EXPECT_FALSE(g.Dragging());
}

TEST(DragGesture, MovingWithoutAPressDoesNothingAndCancelEndsIt)
{
	DragGesture g;
	EXPECT_FALSE(g.Move(500, 500, 6));
	g.Press(0, 0, 1);
	g.Cancel();
	EXPECT_FALSE(g.Move(50, 50, 6));
	EXPECT_FALSE(g.Release());
}

TEST(CardDrop, GiveInReachCostsBothMercsTwo)
{
	DropVerdict const v = PlanCardDrop(Give(1));
	EXPECT_EQ(v.action, DropAction::Give);
	EXPECT_EQ(v.apGiver, PASS_COST_AP);
	EXPECT_EQ(v.apReceiver, PASS_COST_AP);
	EXPECT_EQ(v.tiles, 1);
	EXPECT_TRUE(v.Ok());
}

TEST(CardDrop, RangeIsInclusiveAtThreeTiles)
{
	EXPECT_TRUE(PlanCardDrop(Give(GIVE_RANGE_TILES)).Ok());
	DropVerdict const far = PlanCardDrop(Give(GIVE_RANGE_TILES + 1));
	EXPECT_FALSE(far.Ok());
	EXPECT_EQ(far.why, InvWhy::OutOfReach);
}

TEST(CardDrop, OutOfSightIsOutOfReach)
{
	CardDrop d = Give(1);
	d.inSight = false;
	EXPECT_EQ(PlanCardDrop(d).why, InvWhy::OutOfReach);
}

TEST(CardDrop, EachMercMustAffordTheCheck)
{
	CardDrop d = Give(1);
	d.giver.canPay = false;
	EXPECT_EQ(PlanCardDrop(d).why, InvWhy::NoAP);
	d = Give(1);
	d.receiver.canPay = false;
	EXPECT_EQ(PlanCardDrop(d).why, InvWhy::NoAPTarget);
}

TEST(CardDrop, AnUnconsciousMercCannotTakeOrGive)
{
	CardDrop d = Give(1);
	d.receiver.conscious = false;
	EXPECT_EQ(PlanCardDrop(d).why, InvWhy::Unconscious);
	d = Give(1);
	d.giver.conscious = false;
	EXPECT_EQ(PlanCardDrop(d).why, InvWhy::Unconscious);
}

TEST(CardDrop, AVehicleOrEscortTakesNothing)
{
	CardDrop d = Give(1);
	d.receiverTakes = false;
	EXPECT_FALSE(PlanCardDrop(d).Ok());
}

TEST(CardDrop, TheMercHimselfDropsAtHisFeet)
{
	CardDrop d = Give(0);
	d.sameMerc = true;
	DropVerdict const v = PlanCardDrop(d);
	EXPECT_EQ(v.action, DropAction::DropAtFeet);
	EXPECT_EQ(v.apGiver, 1);
	EXPECT_EQ(v.apReceiver, 0);
}

TEST(CardDrop, DroppingAtHisFeetNeedsThePointsUnlessItWasFree)
{
	CardDrop d = Give(0);
	d.sameMerc = true;
	d.giver.canPay = false;
	EXPECT_EQ(PlanCardDrop(d).why, InvWhy::NoAP);
	d.dropFree = true;
	DropVerdict const v = PlanCardDrop(d);
	EXPECT_TRUE(v.Ok());
	EXPECT_EQ(v.apGiver, 0);
}
