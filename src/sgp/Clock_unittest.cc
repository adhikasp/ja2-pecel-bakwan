#include "Clock.h"
#include "gtest/gtest.h"

#include <cstdlib>

using namespace sgp::Clock;

// WallSecondsFromUtc is what lets a driven run name a fixed instant without consulting the
// process timezone, so its results must not depend on TZ. Assert them against values
// timegm()/date(1) would produce.
TEST(Clock, wallSecondsFromUtcMatchesKnownInstants)
{
	EXPECT_EQ(WallSecondsFromUtc(1970, 1, 1, 0, 0, 0), 0);
	EXPECT_EQ(WallSecondsFromUtc(2001, 2, 3, 4, 5, 6), 981173106);
	EXPECT_EQ(WallSecondsFromUtc(2026, 1, 15, 12, 0, 0), 1768478400);
	EXPECT_EQ(WallSecondsFromUtc(2038, 1, 19, 3, 14, 7), 2147483647); // the 32-bit time_t edge
	// before the epoch: negative, and the civil conversion has to floor, not truncate
	EXPECT_EQ(WallSecondsFromUtc(1969, 12, 31, 23, 59, 59), -1);
	EXPECT_EQ(WallSecondsFromUtc(1900, 1, 1, 0, 0, 0), -2208988800LL);
}

TEST(Clock, wallSecondsFromUtcKnowsItsLeapYears)
{
	// 2000 was a leap year (divisible by 400), 1900 was not (divisible by 100 but not 400)
	EXPECT_EQ(WallSecondsFromUtc(2000, 2, 29, 0, 0, 0), 951782400);
	EXPECT_FALSE(WallSecondsFromUtc(1900, 2, 29, 0, 0, 0).has_value());
	EXPECT_FALSE(WallSecondsFromUtc(2026, 2, 29, 0, 0, 0).has_value());
	EXPECT_TRUE(WallSecondsFromUtc(2024, 2, 29, 0, 0, 0).has_value());
}

TEST(Clock, wallSecondsFromUtcRejectsDatesThatDoNotExist)
{
	EXPECT_FALSE(WallSecondsFromUtc(2026, 13, 1, 0, 0, 0).has_value());
	EXPECT_FALSE(WallSecondsFromUtc(2026, 0, 1, 0, 0, 0).has_value());
	EXPECT_FALSE(WallSecondsFromUtc(2026, 4, 31, 0, 0, 0).has_value());
	EXPECT_FALSE(WallSecondsFromUtc(2026, 1, 32, 0, 0, 0).has_value());
	EXPECT_FALSE(WallSecondsFromUtc(2026, 1, 0, 0, 0, 0).has_value());
	EXPECT_FALSE(WallSecondsFromUtc(2026, 1, 1, 24, 0, 0).has_value());
	EXPECT_FALSE(WallSecondsFromUtc(2026, 1, 1, 0, 60, 0).has_value());
}

TEST(Clock, freezingTheWallClockPinsItAndZeroUnfreezes)
{
	// left frozen by another test's failure, the rest of the file would be misleading
	FreezeWall(0);
	ASSERT_FALSE(IsWallFrozen());

	FreezeWall(1768478400);
	EXPECT_TRUE(IsWallFrozen());
	EXPECT_EQ(WallSeconds(), 1768478400);
	EXPECT_EQ(WallSeconds(), 1768478400); // and it does not creep forward

	FreezeWall(0);
	EXPECT_FALSE(IsWallFrozen());
	// back to something close to the real clock; 2026 is comfortably in range
	EXPECT_GT(WallSeconds(), 1768478400);
}