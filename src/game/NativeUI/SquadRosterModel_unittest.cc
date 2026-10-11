#include "SquadRosterModel.h"

#include <gtest/gtest.h>

using namespace SquadRosterModel;

TEST(SquadRoster, newMercsTakeTheFirstFreeSlot)
{
	Roster r(6);
	EXPECT_EQ(r.Slots(), 6);
	EXPECT_TRUE(r.Add(10));
	EXPECT_TRUE(r.Add(11));
	EXPECT_TRUE(r.Add(12));
	EXPECT_EQ(r.At(0), 10);
	EXPECT_EQ(r.At(2), 12);
	EXPECT_EQ(r.SlotOf(11), 1);
	EXPECT_EQ(r.Filled(), 3);
	EXPECT_EQ(r.Mask(), 0x7u);
}

TEST(SquadRoster, aMercIsInTheBarOnce)
{
	Roster r(6);
	EXPECT_TRUE(r.Add(5));
	EXPECT_FALSE(r.Add(5));
	EXPECT_EQ(r.Filled(), 1);
	EXPECT_FALSE(r.Add(NONE));
}

TEST(SquadRoster, aLeftSlotStaysEmptyUntilTheNextMercFillsIt)
{
	Roster r(6);
	r.Add(1); r.Add(2); r.Add(3);
	EXPECT_TRUE(r.Remove(2));
	EXPECT_EQ(r.At(1), NONE);
	EXPECT_EQ(r.At(2), 3); // nobody shuffles up: the cards keep their place
	EXPECT_FALSE(r.Remove(2));
	EXPECT_TRUE(r.Add(9));
	EXPECT_EQ(r.At(1), 9);
	EXPECT_EQ(r.Mask(), 0x7u);
}

TEST(SquadRoster, aFullBarRefuses)
{
	Roster r(2);
	EXPECT_TRUE(r.Add(1));
	EXPECT_TRUE(r.Add(2));
	EXPECT_FALSE(r.Add(3));
	EXPECT_EQ(r.Filled(), 2);
}

TEST(SquadRoster, outOfRangeSlotsAreEmpty)
{
	Roster r(3);
	r.Add(7);
	EXPECT_EQ(r.At(-1), NONE);
	EXPECT_EQ(r.At(3), NONE);
	EXPECT_EQ(r.SlotOf(99), -1);
	EXPECT_EQ(r.SlotOf(NONE), -1);
}

TEST(SquadRoster, resizeKeepsWhoStillFits)
{
	Roster r(6);
	for (int i = 1; i <= 5; ++i) r.Add(i);
	r.Resize(3);
	EXPECT_EQ(r.Slots(), 3);
	EXPECT_EQ(r.Filled(), 3);
	EXPECT_FALSE(r.Has(4));
	r.Resize(8);
	EXPECT_EQ(r.Slots(), 8);
	EXPECT_EQ(r.Filled(), 3);
}

TEST(SquadRoster, clearEmptiesEverySlot)
{
	Roster r(6);
	r.Add(1); r.Add(2);
	r.Clear();
	EXPECT_EQ(r.Filled(), 0);
	EXPECT_EQ(r.Slots(), 6);
	EXPECT_TRUE(r.Members().empty());
}

TEST(SquadRoster, nextMercFollowsTheSlotsAndWraps)
{
	Roster r(6);
	r.Add(10); r.Add(11); r.Add(12);
	auto all = [](int) { return true; };
	EXPECT_EQ(r.Next(10, all), 11);
	EXPECT_EQ(r.Next(11, all), 12);
	EXPECT_EQ(r.Next(12, all), 10); // wraps round
}

TEST(SquadRoster, nextMercSkipsWhoCannotBeSelected)
{
	Roster r(6);
	r.Add(10); r.Add(11); r.Add(12); r.Add(13);
	auto not11and12 = [](int id) { return id != 11 && id != 12; };
	EXPECT_EQ(r.Next(10, not11and12), 13);
	EXPECT_EQ(r.Next(13, not11and12), 10);
}

TEST(SquadRoster, nextMercIsTheSameMercWhenHeIsAlone)
{
	Roster r(6);
	r.Add(10);
	EXPECT_EQ(r.Next(10, [](int) { return true; }), 10);
	r.Add(11);
	EXPECT_EQ(r.Next(10, [](int id) { return id == 10; }), 10);
}

TEST(SquadRoster, nextMercFromSomeoneNotInTheBarTakesTheFirstEligible)
{
	Roster r(6);
	r.Add(10); r.Add(11);
	// legacy: a prev that is not in the bar never "was seen", so the answer is prev itself
	EXPECT_EQ(r.Next(99, [](int) { return true; }), 99);
}
