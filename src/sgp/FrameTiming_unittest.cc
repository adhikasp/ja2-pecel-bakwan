#include "FrameTiming.h"
#include "gtest/gtest.h"

using sgp::FrameTiming;

TEST(FrameTiming, noSamplesIsAllZeros)
{
	FrameTiming t;
	FrameTiming::Stats const s = t.Take(false);
	EXPECT_EQ(s.frames, 0u);
	EXPECT_EQ(s.samples, 0);
	EXPECT_DOUBLE_EQ(s.meanMs, 0);
	EXPECT_DOUBLE_EQ(s.p50Ms, 0);
	EXPECT_DOUBLE_EQ(s.p95Ms, 0);
	EXPECT_DOUBLE_EQ(s.maxMs, 0);
}

TEST(FrameTiming, oneSampleIsEveryStatistic)
{
	FrameTiming t;
	t.Add(4.5);
	FrameTiming::Stats const s = t.Take(false);
	EXPECT_EQ(s.frames, 1u);
	EXPECT_EQ(s.samples, 1);
	EXPECT_DOUBLE_EQ(s.meanMs, 4.5);
	EXPECT_DOUBLE_EQ(s.p50Ms, 4.5);
	EXPECT_DOUBLE_EQ(s.p95Ms, 4.5);
	EXPECT_DOUBLE_EQ(s.maxMs, 4.5);
}

TEST(FrameTiming, distributionIsMeanP50P95AndWorst)
{
	FrameTiming t;
	for (int i = 1; i <= 100; ++i) t.Add(double(i));
	FrameTiming::Stats const s = t.Take(false);
	EXPECT_EQ(s.samples, 100);
	EXPECT_DOUBLE_EQ(s.meanMs, 50.5);
	EXPECT_DOUBLE_EQ(s.p50Ms, 51); // v[100 / 2], the same convention as WorldRenderTiming
	EXPECT_DOUBLE_EQ(s.p95Ms, 96); // v[100 * 95 / 100]
	EXPECT_DOUBLE_EQ(s.maxMs, 100);
}

TEST(FrameTiming, resetClearsSamplesAndFrames)
{
	FrameTiming t;
	t.Add(1);
	t.Add(2);
	FrameTiming::Stats const first = t.Take(true);
	EXPECT_EQ(first.samples, 2);
	FrameTiming::Stats const after = t.Take(false);
	EXPECT_EQ(after.frames, 0u);
	EXPECT_EQ(after.samples, 0);
}

TEST(FrameTiming, theRingKeepsTheNewestSamplesAndCountsEveryFrame)
{
	FrameTiming t(4);
	for (int i = 1; i <= 6; ++i) t.Add(double(i));
	FrameTiming::Stats const s = t.Take(false);
	EXPECT_EQ(s.frames, 6u); // every frame is counted, even the ones the ring dropped
	EXPECT_EQ(s.samples, 4);
	EXPECT_DOUBLE_EQ(s.meanMs, 4.5); // 3, 4, 5, 6
	EXPECT_DOUBLE_EQ(s.p50Ms, 5);
	EXPECT_DOUBLE_EQ(s.p95Ms, 6);
	EXPECT_DOUBLE_EQ(s.maxMs, 6);
}

TEST(FrameTiming, aZeroCapacityStillKeepsOneSample)
{
	FrameTiming t(0);
	t.Add(7);
	t.Add(9);
	FrameTiming::Stats const s = t.Take(false);
	EXPECT_EQ(s.samples, 1);
	EXPECT_DOUBLE_EQ(s.maxMs, 9);
}
