// The Visualizer's hitbox geometry (Visualizer.h), checked without a GPU: the faces it emits for the SDL_GPU
// compositor are the same isometric shapes the SDL_Renderer overlay always drew, so a misplacement is caught here
// rather than only on a screenshot.
#include "gtest/gtest.h"

#include "Visualizer.h"

using Visualizer::Face;

namespace
{
constexpr float EPS = 1e-3f;

void ExpectParallelogram(Face const& f)
{
	// A parallelogram: p1 - p0 == p2 - p3, and p3 - p0 == p2 - p1.
	EXPECT_NEAR(f.x[1] - f.x[0], f.x[2] - f.x[3], EPS);
	EXPECT_NEAR(f.y[1] - f.y[0], f.y[2] - f.y[3], EPS);
	EXPECT_NEAR(f.x[3] - f.x[0], f.x[2] - f.x[1], EPS);
	EXPECT_NEAR(f.y[3] - f.y[0], f.y[2] - f.y[1], EPS);
}
}

TEST(VisualizerGeometry, ThreeFacesPerHitbox)
{
	Face faces[3];
	EXPECT_EQ(Visualizer::HitboxFaces(0, 0, 0, false, faces), 3);
	for (Face const& f : faces) ExpectParallelogram(f);
}

TEST(VisualizerGeometry, SideFacesHangOffTheTopEdges)
{
	Face faces[3];
	ASSERT_EQ(Visualizer::HitboxFaces(0, 0, 0, false, faces), 3);
	Face const& top = faces[0];
	Face const& right = faces[1];
	Face const& left = faces[2];

	// The right face's top edge is the top face's right edge, dropped by the same height as its other edge.
	EXPECT_NEAR(right.x[0], top.x[1], EPS);
	EXPECT_NEAR(right.y[0], top.y[1], EPS);
	EXPECT_NEAR(right.x[3], top.x[2], EPS);
	EXPECT_NEAR(right.y[3], top.y[2], EPS);
	EXPECT_NEAR(right.x[1], right.x[0], EPS);
	EXPECT_NEAR(right.x[2], right.x[3], EPS);
	float const height = right.y[1] - right.y[0];
	EXPECT_GT(height, 0.0f);
	EXPECT_NEAR(right.y[2] - right.y[3], height, EPS);

	// The left face's top edge is the top face's left edge, and its lower edge reaches the right face's bottom.
	EXPECT_NEAR(left.x[0], top.x[3], EPS);
	EXPECT_NEAR(left.y[0], top.y[3], EPS);
	EXPECT_NEAR(left.x[1], top.x[2], EPS);
	EXPECT_NEAR(left.y[1], top.y[2], EPS);
	EXPECT_NEAR(left.x[2], right.x[2], EPS);
	EXPECT_NEAR(left.y[2], right.y[2], EPS);
	EXPECT_NEAR(left.y[3] - left.y[0], height, EPS);
}

TEST(VisualizerGeometry, RoofOnlyIsJustTheTop)
{
	Face faces[3];
	ASSERT_EQ(Visualizer::HitboxFaces(3, 4, 1, true, faces), 1);
	Face roofTop[3];
	ASSERT_EQ(Visualizer::HitboxFaces(3, 4, 1, false, roofTop), 3);
	// The roof face is the same rhombus as a normal top face, of the same tile and level.
	for (int i = 0; i < 4; ++i)
	{
		EXPECT_NEAR(faces[0].x[i], roofTop[0].x[i], EPS);
		EXPECT_NEAR(faces[0].y[i], roofTop[0].y[i], EPS);
	}
	// ... but a different colour: the roof is drawn in the level-1 tile colour.
	EXPECT_NE(faces[0].color.g, roofTop[0].color.g);
}

TEST(VisualizerGeometry, NeighbouringTilesShiftByTheIsometricStep)
{
	Face here[3], next[3];
	ASSERT_EQ(Visualizer::HitboxFaces(0, 0, 0, false, here), 3);
	ASSERT_EQ(Visualizer::HitboxFaces(1, 0, 0, false, next), 3);
	// Moving one tile along x is exactly the +x isometric leg of the top face (its p0 -> p1 edge).
	for (int i = 0; i < 4; ++i)
	{
		EXPECT_NEAR(next[0].x[i] - here[0].x[i], here[0].x[1] - here[0].x[0], EPS);
		EXPECT_NEAR(next[0].y[i] - here[0].y[i], here[0].y[1] - here[0].y[0], EPS);
	}
}

TEST(VisualizerGeometry, TheCheckeredPatternFlipsWithTheTileAndLevel)
{
	Face a[3], b[3], c[3];
	Visualizer::HitboxFaces(0, 0, 0, false, a);
	Visualizer::HitboxFaces(1, 0, 0, false, b); // (x + y) parity flipped
	Visualizer::HitboxFaces(0, 0, 1, false, c); // z parity flipped
	EXPECT_NE(a[0].color.g, b[0].color.g);
	EXPECT_NE(a[0].color.g, c[0].color.g);
	// The two flips together land back on the original shade.
	Face d[3];
	Visualizer::HitboxFaces(1, 0, 1, false, d);
	EXPECT_FLOAT_EQ(a[0].color.g, d[0].color.g);
}
