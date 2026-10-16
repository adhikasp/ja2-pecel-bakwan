#include "gtest/gtest.h"

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
