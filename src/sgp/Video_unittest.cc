#include "gtest/gtest.h"

#include "VideoLayout.h"

using namespace VideoLayout;

namespace
{
DisplayLayout Layout(int w, int h, int scale = UI_SCALE_AUTO)
{
	return ComputeDisplayLayout({ w, h }, scale);
}
}

TEST(VideoLayout, AutoScaleOn4K16x9)
{
	// 3x gives exactly 1280x720, the largest scale keeping >= 1280x720
	auto const l = Layout(3840, 2160);
	EXPECT_EQ(l.scale, 3);
	EXPECT_EQ(l.logical, (Size{ 1280, 720 }));
}

TEST(VideoLayout, AutoScaleOnUltrawide)
{
	auto const l = Layout(3440, 1440);
	EXPECT_EQ(l.scale, 2);
	EXPECT_EQ(l.logical, (Size{ 1720, 720 }));
}

TEST(VideoLayout, AutoScaleOnCommonDesktops)
{
	EXPECT_EQ(Layout(2560, 1440), (DisplayLayout{ { 1280, 720 }, 2 }));
	EXPECT_EQ(Layout(1920, 1080), (DisplayLayout{ { 1920, 1080 }, 1 }));
	EXPECT_EQ(Layout(2560, 1600), (DisplayLayout{ { 1280, 800 }, 2 }));
	EXPECT_EQ(Layout(5120, 2880), (DisplayLayout{ { 1280, 720 }, 4 }));
}

TEST(VideoLayout, AutoScaleOnSmallDisplaysIsOne)
{
	EXPECT_EQ(Layout(1366, 768), (DisplayLayout{ { 1366, 768 }, 1 }));
	EXPECT_EQ(Layout(1280, 720), (DisplayLayout{ { 1280, 720 }, 1 }));
	EXPECT_EQ(Layout(800, 600), (DisplayLayout{ { 800, 600 }, 1 }));
}

TEST(VideoLayout, ClassicWindowIsUnchanged)
{
	EXPECT_EQ(Layout(640, 480), (DisplayLayout{ { 640, 480 }, 1 }));
	EXPECT_EQ(Layout(640, 480, 2), (DisplayLayout{ { 640, 480 }, 1 }));
}

TEST(VideoLayout, ExplicitScaleFloorsTheLogicalSize)
{
	EXPECT_EQ(Layout(3840, 2160, 2), (DisplayLayout{ { 1920, 1080 }, 2 }));
	EXPECT_EQ(Layout(3840, 2160, 4), (DisplayLayout{ { 960, 540 }, 4 }));
	// leftover pixels (window % scale) are not part of the canvas
	EXPECT_EQ(Layout(1601, 1201, 2), (DisplayLayout{ { 800, 600 }, 2 }));
}

TEST(VideoLayout, ExplicitScaleIsReducedToKeepTheMinimumCanvas)
{
	// 3x of 1366x768 would be 455x256 (< 640x480): falls back to the largest that fits
	auto const l = Layout(1366, 768, 3);
	EXPECT_EQ(l.scale, 1);
	EXPECT_EQ(l.logical, (Size{ 1366, 768 }));

	auto const m = Layout(1920, 1080, 4);
	EXPECT_EQ(m.scale, 2); // 4x = 480x270 too small, 3x = 640x360 too short, 2x fits
	EXPECT_EQ(m.logical, (Size{ 960, 540 }));
}

TEST(VideoLayout, LogicalNeverBelowMinimum)
{
	auto const l = Layout(500, 300);
	EXPECT_EQ(l.scale, 1);
	EXPECT_EQ(l.logical, (Size{ 640, 480 }));
}

TEST(VideoLayout, ScaleOutOfRangeIsClamped)
{
	EXPECT_EQ(Layout(3840, 2160, 99).scale, 4);
	EXPECT_EQ(Layout(3840, 2160, -3).scale, 1);
}

TEST(VideoLayout, CanvasTimesScaleFitsTheWindow)
{
	for (int w : { 800, 1024, 1366, 1600, 1920, 2560, 3440, 3840, 5120 })
	{
		for (int h : { 600, 768, 900, 1080, 1440, 2160 })
		{
			for (int s : { 0, 1, 2, 3, 4 })
			{
				auto const l = Layout(w, h, s);
				EXPECT_GE(l.scale, 1);
				EXPECT_LE(l.scale, MAX_UI_SCALE);
				EXPECT_GE(l.logical.w, MIN_LOGICAL_WIDTH);
				EXPECT_GE(l.logical.h, MIN_LOGICAL_HEIGHT);
				EXPECT_LE(l.logical.w * l.scale, w);
				EXPECT_LE(l.logical.h * l.scale, h);
			}
		}
	}
}

TEST(VideoLayout, PresentationIsIntegerFitForLayoutSizes)
{
	// The layout guarantees canvas * scale <= window with < scale leftover pixels
	for (Size const window : { Size{ 3840, 2160 }, Size{ 3440, 1440 }, Size{ 2560, 1440 }, Size{ 1366, 768 }, Size{ 3439, 1441 } })
	{
		auto const l = ComputeDisplayLayout(window, UI_SCALE_AUTO);
		auto const p = ComputePresentation(window, l.logical);
		EXPECT_TRUE(p.integerFit);
		EXPECT_EQ(p.k, l.scale);
	}
}

TEST(VideoLayout, PresentationOfResizedWindowIsSharpBilinear)
{
	// A 1280x720 canvas in an arbitrary 2000x1200 window: 1x nearest + linear
	auto const p = ComputePresentation({ 2000, 1200 }, { 1280, 720 });
	EXPECT_FALSE(p.integerFit);
	EXPECT_EQ(p.k, 1);

	// 1920x1080 window for a 640x480 canvas: k = 2 (limited by the height), lots of leftover
	auto const q = ComputePresentation({ 1920, 1080 }, { 640, 480 });
	EXPECT_FALSE(q.integerFit);
	EXPECT_EQ(q.k, 2);
}

TEST(VideoLayout, PresentationOfTooSmallWindow)
{
	auto const p = ComputePresentation({ 500, 400 }, { 640, 480 });
	EXPECT_FALSE(p.integerFit);
	EXPECT_EQ(p.k, 0);
}

TEST(VideoLayout, ClassicWindowIsAnExactFit)
{
	auto const p = ComputePresentation({ 640, 480 }, { 640, 480 });
	EXPECT_TRUE(p.integerFit);
	EXPECT_EQ(p.k, 1);
}

TEST(VideoLayout, ResolveWindowSize)
{
	Size const desktop{ 3840, 2160 };
	EXPECT_EQ(ResolveWindowSize(0, 0, desktop), desktop);
	EXPECT_EQ(ResolveWindowSize(1280, 720, desktop), (Size{ 1280, 720 }));
}

TEST(VideoLayout, DefaultWindowedSizeLeavesRoomOnTheDesktop)
{
	EXPECT_EQ(DefaultWindowedSize({ 3840, 2160 }), (Size{ 3264, 1836 }));
	EXPECT_EQ(DefaultWindowedSize({ 800, 600 }), (Size{ 680, 510 }));
	EXPECT_EQ(DefaultWindowedSize({ 640, 480 }), (Size{ 640, 480 }));
}
