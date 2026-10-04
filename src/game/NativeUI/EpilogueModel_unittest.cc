#include "EpilogueModel.h"

#include <gtest/gtest.h>

namespace NativeUI
{

TEST(EpilogueModel, theCardsAndTheirNumbers)
{
	EpilogueModel::Raw r;
	r.days = 18;
	r.sectors = 41;
	r.sectorsTotal = 256;
	r.killedAdmin = 21;
	r.killedTroop = 402;
	r.killedElite = 189;
	r.effort = 78;
	r.served = 9;
	r.fell = 2;

	auto const stats = EpilogueModel::Stats(r);
	ASSERT_EQ(stats.size(), 4u);
	EXPECT_EQ(stats[0].key, "days");
	EXPECT_EQ(stats[0].value, 18);
	EXPECT_EQ(stats[1].key, "sectors");
	EXPECT_EQ(stats[1].value, 41);
	EXPECT_EQ(stats[1].subValue, 256);
	EXPECT_EQ(stats[2].key, "killed");
	EXPECT_EQ(stats[2].value, 612);
	EXPECT_EQ(stats[3].key, "served");
	EXPECT_EQ(stats[3].value, 9);
	EXPECT_EQ(stats[3].subValue, 2);
}

TEST(EpilogueModel, killedTotalCountsEveryRank)
{
	EpilogueModel::Raw r;
	EXPECT_EQ(EpilogueModel::KilledTotal(r), 0);
	r.killedAdmin = 21;
	r.killedTroop = 402;
	r.killedElite = 189;
	EXPECT_EQ(EpilogueModel::KilledTotal(r), 612);
}

TEST(EpilogueModel, effortIsClamped)
{
	EpilogueModel::Raw r;
	r.effort = -5;
	EXPECT_EQ(EpilogueModel::EffortPercent(r), 0);
	r.effort = 78;
	EXPECT_EQ(EpilogueModel::EffortPercent(r), 78);
	r.effort = 140;
	EXPECT_EQ(EpilogueModel::EffortPercent(r), 100);
}

TEST(EpilogueModel, aFreshGameShowsZeroesNotGaps)
{
	// S3 (docs/ui/epilogue.md): the screen opened with no campaign behind it reports the fresh-state numbers
	EpilogueModel::Raw const r;
	auto const stats = EpilogueModel::Stats(r);
	for (auto const& s : stats) EXPECT_EQ(s.value, 0);
	EXPECT_EQ(EpilogueModel::KilledTotal(r), 0);
	EXPECT_EQ(EpilogueModel::EffortPercent(r), 0);
}

}
