#include "OverlayModel.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace OverlayModel;

TEST(OverlayModel, ringWidensAndFadesOverThePulse)
{
	RingPhase prev = RingAt(0);
	for (int f = 1; f < FRAMES; ++f)
	{
		RingPhase const p = RingAt(f);
		EXPECT_GT(p.radiusDp, prev.radiusDp) << f;
		EXPECT_LT(p.alpha, prev.alpha) << f;
		EXPECT_GT(p.alpha, 0.f);
		prev = p;
	}
	// out of range frames are clamped, not extrapolated
	EXPECT_EQ(RingAt(-3).radiusDp, RingAt(0).radiusDp);
	EXPECT_EQ(RingAt(99).radiusDp, RingAt(FRAMES - 1).radiusDp);
}

TEST(OverlayModel, arrowsDecodeEveryLegacyFlag)
{
	EXPECT_TRUE(ArrowsFor(0).empty());
	// both hidden: nothing, whatever else is set
	EXPECT_TRUE(ArrowsFor(0x2 | 0x4 | 0x8 | 0x20).empty());

	auto one = ArrowsFor(0x8); // up beside
	ASSERT_EQ(one.size(), 1u);
	EXPECT_TRUE(one[0].up);
	EXPECT_EQ(one[0].tones.size(), 1u);

	auto yy = ArrowsFor(0x20000); // up above, two
	ASSERT_EQ(yy.size(), 1u);
	EXPECT_EQ(yy[0].tones.size(), 2u);

	auto climb = ArrowsFor(0x80000);
	ASSERT_EQ(climb.size(), 1u);
	EXPECT_TRUE(climb[0].climb);

	auto yg = ArrowsFor(0x400); // down below, green then yellow
	ASSERT_EQ(yg.size(), 1u);
	EXPECT_FALSE(yg[0].up);
	ASSERT_EQ(yg[0].tones.size(), 2u);
	EXPECT_EQ(yg[0].tones[0], ArrowTone::Green);
	EXPECT_EQ(yg[0].tones[1], ArrowTone::Yellow);

	auto gg = ArrowsFor(0x800);
	ASSERT_EQ(gg.size(), 1u);
	EXPECT_EQ(gg[0].tones[0], ArrowTone::Green);
	EXPECT_EQ(gg[0].tones[1], ArrowTone::Green);

	auto both = ArrowsFor(0x8 | 0x80);
	ASSERT_EQ(both.size(), 2u);
	EXPECT_TRUE(both[0].up);
	EXPECT_FALSE(both[1].up);

	// one direction hidden, the other still shows
	auto halfHidden = ArrowsFor(0x2 | 0x80);
	ASSERT_EQ(halfHidden.size(), 1u);
	EXPECT_FALSE(halfHidden[0].up);
}

TEST(OverlayModel, everyArrowFlagDrawsSomething)
{
	for (uint32_t f : { 0x8u, 0x20u, 0x40u, 0x80u, 0x400u, 0x800u, 0x20000u, 0x40000u, 0x80000u, 0x2000000u })
	{
		auto a = ArrowsFor(f);
		ASSERT_FALSE(a.empty()) << std::hex << f;
		for (Arrow const& r : a) EXPECT_FALSE(r.tones.empty());
	}
}

TEST(OverlayModel, bandOrdersCornersAndIgnoresAClick)
{
	Band b = NormalizeBand(100, 80, 40, 120);
	EXPECT_TRUE(b.valid);
	EXPECT_EQ(b.l, 40);
	EXPECT_EQ(b.r, 100);
	EXPECT_EQ(b.t, 80);
	EXPECT_EQ(b.b, 120);
	EXPECT_FALSE(NormalizeBand(50, 50, 50, 50).valid);
	// a thin drag along one axis is a line: still drawn
	EXPECT_TRUE(NormalizeBand(50, 50, 90, 50).valid);
}

TEST(OverlayModel, bandGlowIsATriangleWave)
{
	float lo = 2, hi = -1;
	for (uint32_t t = 0; t < 60 * 12; t += 60)
	{
		float const g = BandGlow(t);
		EXPECT_GE(g, 0.f);
		EXPECT_LE(g, 1.f);
		lo = std::min(lo, g);
		hi = std::max(hi, g);
	}
	EXPECT_EQ(lo, 0.f);
	EXPECT_EQ(hi, 1.f);
	// periodic over 12 steps
	EXPECT_EQ(BandGlow(60 * 3), BandGlow(60 * 15));
}

TEST(OverlayModel, poolTruncatesAtEightAndCountsTheRest)
{
	std::vector<PoolRow> all;
	for (int i = 0; i < 11; ++i) all.push_back({ i, "item", 1 });
	PoolList const p = BuildPool(all);
	EXPECT_EQ(int(p.rows.size()), MAX_LISTED);
	EXPECT_EQ(p.hidden, 3);
	EXPECT_EQ(p.rows.front().item, 0);

	PoolList const few = BuildPool({ { 1, "a", 2 } });
	EXPECT_EQ(few.rows.size(), 1u);
	EXPECT_EQ(few.hidden, 0);
	EXPECT_EQ(few.rows[0].count, 2);
}

TEST(OverlayModel, listGoesRightOfTheAnchorThenLeftThenIsClamped)
{
	Rect const view{ 0, 0, 1000, 600 };
	Rect r = PlaceList(100, 300, 200, 100, view, 15);
	EXPECT_EQ(r.x, 115);
	EXPECT_EQ(r.y, 250); // centred vertically

	r = PlaceList(900, 300, 200, 100, view, 15); // 915 + 200 > 1000: to the left
	EXPECT_EQ(r.x, 900 - 15 - 200);

	r = PlaceList(100, 10, 200, 100, view, 15); // would start above the view
	EXPECT_EQ(r.y, 0);
	r = PlaceList(100, 590, 200, 100, view, 15);
	EXPECT_EQ(r.y, 500);
	// a list wider than the view stays at its left edge rather than going negative
	r = PlaceList(10, 100, 2000, 50, view, 15);
	EXPECT_EQ(r.x, 0);
}
