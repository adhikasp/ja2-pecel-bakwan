#include "gtest/gtest.h"

#include <cmath>
#include <vector>

#include "Camera.h"

// The camera core (#38): a tile's centre, the legacy settle onto it, and the free resting position a pan now
// leaves behind.
TEST(Camera, aTileCentreIsTheMiddleOfTheTile)
{
	EXPECT_DOUBLE_EQ(Camera::FromTile(0, 0).x, 5.0);
	EXPECT_DOUBLE_EQ(Camera::FromTile(0, 0).y, 5.0);
	// Omerta's landing sector centres on tile 80,80: the value Init.cc starts the camera at.
	EXPECT_DOUBLE_EQ(Camera::FromTile(80, 80).x, 805.0);
	EXPECT_DOUBLE_EQ(Camera::FromTile(80, 80).y, 805.0);
}

TEST(Camera, theLegacySettleRoundsToTheNearestTileCentre)
{
	// A pan used to round up to the next tile centre rather than land where it was asked to (805 + 5 cells).
	Camera::Centre const snapped = Camera::SnapToTileCentre({ 810.0, 800.0 });
	EXPECT_DOUBLE_EQ(snapped.x, 815.0);
	EXPECT_DOUBLE_EQ(snapped.y, 805.0);

	// A whole tile centre is its own snap.
	EXPECT_DOUBLE_EQ(Camera::SnapToTileCentre({ 15.0, 15.0 }).x, 15.0);
}

TEST(Camera, aTileCentreIsOnATileCentreAndABetweenTilesPositionIsNot)
{
	EXPECT_TRUE(Camera::OnTileCentre(Camera::FromTile(80, 80)));
	EXPECT_TRUE(Camera::OnTileCentre(Camera::FromTile(0, 0)));
	// Where a free pan leaves the camera: half a tile along, between two tile centres.
	EXPECT_FALSE(Camera::OnTileCentre({ 810.0, 800.0 }));
	EXPECT_FALSE(Camera::OnTileCentre({ 805.0, 805.5 }));
}

// The eased camera (decision A2). An ease that never quite arrives, overshoots, or takes a different path on a
// different frame rate is the failure mode of the whole feature, so it is asserted as data rather than by eye.
TEST(Camera, anEaseArrivesExactlyOnItsTargetInABoundedNumberOfFrames)
{
	Camera::Ease ease;
	Camera::Centre const from{ 805.0, 805.0 };
	Camera::Centre const to{ 1237.5, 412.25 };
	ease.Start(from, to);
	EXPECT_TRUE(ease.Active());

	Camera::Centre c = from;
	int frames = 0;
	while (ease.Active() && frames < 100)
	{
		c = ease.Advance();
		++frames;
	}
	EXPECT_EQ(frames, Camera::kEaseFrames) << "the move is counted in frames and does not run on";
	EXPECT_EQ(c.x, to.x) << "the last frame is the target itself, not a rounding of it";
	EXPECT_EQ(c.y, to.y);
	EXPECT_FALSE(ease.Active());
}

TEST(Camera, anEaseOnlyEverGetsCloserAndNeverPassesTheTarget)
{
	Camera::Ease ease;
	Camera::Centre const from{ 805.0, 805.0 };
	Camera::Centre const to{ 1205.0, 605.0 }; // east and north
	ease.Start(from, to);

	double previous = 1e30;
	Camera::Centre c = from;
	while (ease.Active())
	{
		c = ease.Advance();
		double const distance = std::hypot(to.x - c.x, to.y - c.y);
		EXPECT_LT(distance, previous) << "every frame is closer than the last (no chatter, no overshoot)";
		previous = distance;

		// On the segment from -> to, between its ends: an ease that curved past the target would fail here.
		double const dt = (c.x - from.x) * (to.x - from.x) + (c.y - from.y) * (to.y - from.y);
		double const length2 = (to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y);
		EXPECT_GE(dt, 0.0);
		EXPECT_LE(dt, length2);
	}
}

TEST(Camera, theEaseIsAFastStartThatSettles)
{
	// A cubic ease-out: past half the distance in the first quarter of the frames, and almost there at nine tenths.
	EXPECT_DOUBLE_EQ(Camera::EaseProgress(0, Camera::kEaseFrames), 0.0);
	EXPECT_DOUBLE_EQ(Camera::EaseProgress(Camera::kEaseFrames, Camera::kEaseFrames), 1.0);
	EXPECT_GT(Camera::EaseProgress(Camera::kEaseFrames / 4, Camera::kEaseFrames), 0.5);
	EXPECT_LT(Camera::EaseProgress(Camera::kEaseFrames / 2, Camera::kEaseFrames),
	          Camera::EaseProgress(Camera::kEaseFrames * 3 / 4, Camera::kEaseFrames));
}

TEST(Camera, theEaseTakesTheSamePathEveryTime)
{
	// Decision A3's inertia is out precisely so that this holds: no wall clock, no carried velocity.
	auto run = [] {
		Camera::Ease ease;
		ease.Start({ 0.0, 0.0 }, { 100.0, 50.0 });
		std::vector<Camera::Centre> path;
		while (ease.Active()) path.push_back(ease.Advance());
		return path;
	};
	EXPECT_EQ(run(), run());
}

TEST(Camera, retargetingAnEaseStartsFromWhereTheCentreIs)
{
	Camera::Ease ease;
	ease.Start({ 0.0, 0.0 }, { 100.0, 0.0 });
	ease.Advance();
	ease.Advance();
	Camera::Centre const at = ease.Advance();

	// A new target while in flight: the ease restarts from the current position, so nothing jumps to the old start.
	ease.Start(at, { 200.0, 100.0 });
	EXPECT_TRUE(ease.Active());
	Camera::Centre const first = ease.Advance();
	EXPECT_GT(first.x, at.x);
	EXPECT_LT(first.x, 100.0) << "the first frame is a fraction of the new leg, not a jump to it";

	while (ease.Active()) ease.Advance();
	EXPECT_EQ(ease.Target().x, 200.0);
}

TEST(Camera, aCancelledEaseStaysWhereItIsAndAnEaseToItsOwnPositionDoesNothing)
{
	Camera::Ease ease;
	ease.Start({ 0.0, 0.0 }, { 100.0, 0.0 });
	ease.Advance();
	ease.Cancel();
	EXPECT_FALSE(ease.Active());
	EXPECT_EQ(ease.Advance().x, 100.0) << "cancelled leaves the last target reported, but no frames run";

	ease.Start({ 12.0, 34.0 }, { 12.0, 34.0 });
	EXPECT_FALSE(ease.Active()) << "a move to where the camera already is has nothing to animate";
}

