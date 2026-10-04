#include "gtest/gtest.h"

#include "Easings.h"

TEST(EasingsTest, easeInCubic)
{
	ASSERT_EQ(EaseInCubic(100, 101, 100), 0.0);
	ASSERT_EQ(EaseInCubic(1000, 1100, 1100), 1.0);

	ASSERT_EQ(EaseInCubic(1000, 1100, 1025), 0.0625);
	ASSERT_EQ(EaseInCubic(1000, 1100, 1075), 0.9375);
}

TEST(EasingsTest, easeInOutGravity)
{
	// The screen-transition curve: a slow start, a fall through the middle, a settle at the end.
	ASSERT_DOUBLE_EQ(EaseInOutGravity(0.0), 0.0);
	ASSERT_DOUBLE_EQ(EaseInOutGravity(1.0), 1.0);

	double previous = 0.0;
	for (int i = 0; i <= 100; ++i)
	{
		double const eased = EaseInOutGravity(i / 100.0);
		ASSERT_GE(eased, previous);
		ASSERT_GE(eased, 0.0);
		ASSERT_LE(eased, 1.0);
		previous = eased;
	}
	// Slow at the start, most of the way done by three quarters.
	ASSERT_LT(EaseInOutGravity(0.25), 0.25);
	ASSERT_GT(EaseInOutGravity(0.75), 0.85);
}
