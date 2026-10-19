#include "gtest/gtest.h"

#include "ScrollMotion.h"

namespace
{
// A signature that changes on every frame, as a scroll in flight produces.
uint64_t Moving(unsigned const frame)
{
	return 1000 + frame;
}
}

TEST(ScrollMotion, nothingIsInFlightBeforeAScroll)
{
	Automation::ScrollMotion m;
	EXPECT_FALSE(m.Sample(false, Moving(0))); // the first sample is only the baseline
	EXPECT_FALSE(m.Sample(true, Moving(0)));
}

TEST(ScrollMotion, aScrollInFlightIsReportedFromItsSecondFrame)
{
	Automation::ScrollMotion m;
	m.Sample(true, Moving(0));
	EXPECT_FALSE(m.Sample(true, Moving(1))); // the first frame that moved: not yet a scroll in flight
	EXPECT_TRUE(m.Sample(true, Moving(2)));
	EXPECT_TRUE(m.Sample(true, Moving(3)));
	// the page stops: the frame the last change landed on is still in flight, the next is settled
	EXPECT_FALSE(m.Sample(true, Moving(3)));
}

TEST(ScrollMotion, anInstantScrollIsNotInFlight)
{
	// An instant scroll (a scrollbar thumb drag, ScrollIntoView) moves the offsets once: the page is already
	// where the gesture left it, so a wait must not sit through it - even while RmlUi is animating something
	// else (a hover transition, a toast).
	Automation::ScrollMotion m;
	m.Sample(true, Moving(0));
	EXPECT_FALSE(m.Sample(true, Moving(1)));
	EXPECT_FALSE(m.Sample(true, Moving(1)));
	EXPECT_FALSE(m.Sample(true, Moving(2))); // another single move is still not a scroll in flight
}

TEST(ScrollMotion, aLayoutThatClampsTheOffsetsIsNotInFlight)
{
	// A list that grows moves a scroll container's offsets as the layout clamps them, over consecutive frames,
	// with RmlUi animating nothing: not a scroll in flight.
	Automation::ScrollMotion m;
	m.Sample(false, Moving(0));
	EXPECT_FALSE(m.Sample(false, Moving(1)));
	EXPECT_FALSE(m.Sample(false, Moving(2)));
	EXPECT_FALSE(m.Sample(false, Moving(3)));
}

TEST(ScrollMotion, aNewScrollAfterAStableFrameReArms)
{
	Automation::ScrollMotion m;
	m.Sample(true, Moving(0));
	m.Sample(true, Moving(1));
	EXPECT_TRUE(m.Sample(true, Moving(2)));
	EXPECT_FALSE(m.Sample(true, Moving(2))); // settled
	EXPECT_FALSE(m.Sample(true, Moving(3))); // a new scroll's first frame
	EXPECT_TRUE(m.Sample(true, Moving(4)));
}

TEST(ScrollMotion, aScrollThatNeverSettlesIsBounded)
{
	Automation::ScrollMotion m;
	m.Sample(true, Moving(0));
	unsigned inFlight = 0;
	for (unsigned i = 1; i <= Automation::ScrollMotion::MAX_FRAMES * 5; ++i)
	{
		if (m.Sample(true, Moving(i))) ++inFlight;
	}
	// exactly the bound, then it gives up - an idle that waits forever is worse than one that returns early
	EXPECT_EQ(inFlight, Automation::ScrollMotion::MAX_FRAMES);
}

TEST(ScrollMotion, givingUpStaysGivenUpUntilThePageStops)
{
	Automation::ScrollMotion m;
	uint64_t signature = Moving(0);
	m.Sample(true, signature);
	signature = Moving(1);
	m.Sample(true, signature); // the first frame, which arms nothing
	for (unsigned i = 2; i <= Automation::ScrollMotion::MAX_FRAMES + 1; ++i)
	{
		signature = Moving(i);
		EXPECT_TRUE(m.Sample(true, signature));
	}
	// the bound is reached: every further change is ignored rather than re-arming the wait
	for (unsigned i = 1; i <= 50; ++i)
	{
		signature = Moving(Automation::ScrollMotion::MAX_FRAMES + 1 + i);
		EXPECT_FALSE(m.Sample(true, signature));
	}
	// once the page is still, a later change is a new scroll and is waited for again
	EXPECT_FALSE(m.Sample(true, signature));
	EXPECT_FALSE(m.Sample(true, Moving(9999))); // its first frame
	EXPECT_TRUE(m.Sample(true, Moving(10000)));
}

TEST(ScrollMotion, theNativeUiNotRunningIsNotAScroll)
{
	Automation::ScrollMotion m;
	EXPECT_FALSE(m.Sample(false, 0));
	EXPECT_FALSE(m.Sample(false, 0));
}
