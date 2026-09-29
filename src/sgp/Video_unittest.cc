#include "gtest/gtest.h"

#include "VideoLayout.h"
#include "VSurface.h"

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


TEST(VideoLayout, FloorDivRoundsDown)
{
	EXPECT_EQ(FloorDiv(7, 2), 3);
	EXPECT_EQ(FloorDiv(-7, 2), -4);
	EXPECT_EQ(FloorDiv(-8, 2), -4);
	EXPECT_EQ(FloorDiv(0, 3), 0);
	EXPECT_EQ(FloorDiv(-1, 3), -1);
}

TEST(VideoLayout, MatchUiIsASingleLayer)
{
	auto const ui = Layout(2560, 1440, 2);
	auto const l = ComputeLayerLayout(ui, WORLD_ZOOM_MATCH_UI);
	EXPECT_FALSE(l.layered);
	EXPECT_EQ(l.ui, (Size{ 1280, 720 }));
	EXPECT_EQ(l.world, l.ui);
	EXPECT_EQ(l.uiScale, 2);
	EXPECT_EQ(l.worldZoom, 2);
	EXPECT_EQ(l.canvas, (Size{ 2560, 1440 }));
}

TEST(VideoLayout, ZoomEqualToUiScaleIsASingleLayer)
{
	auto const l = ComputeLayerLayout(Layout(2560, 1440, 2), 2);
	EXPECT_FALSE(l.layered);
	EXPECT_EQ(l.world, (Size{ 1280, 720 }));
}

TEST(VideoLayout, UiTwiceWorldOnceShowsTheWholeWindowOfWorld)
{
	// 2560x1440, Su = 2, Zw = 1: HUD as at 1280x720, world 2560x1440
	auto const l = ComputeLayerLayout(Layout(2560, 1440, 2), 1);
	EXPECT_TRUE(l.layered);
	EXPECT_EQ(l.ui, (Size{ 1280, 720 }));
	EXPECT_EQ(l.world, (Size{ 2560, 1440 }));
	EXPECT_EQ(l.canvas, (Size{ 2560, 1440 }));
	EXPECT_EQ(l.uiScale, 2);
	EXPECT_EQ(l.worldZoom, 1);
}

TEST(VideoLayout, LayersOn4K)
{
	auto const l = ComputeLayerLayout(Layout(3840, 2160, 3), 1);
	EXPECT_TRUE(l.layered);
	EXPECT_EQ(l.ui, (Size{ 1280, 720 }));
	EXPECT_EQ(l.world, (Size{ 3840, 2160 }));
	auto const l2 = ComputeLayerLayout(Layout(3840, 2160, 3), 2);
	EXPECT_EQ(l2.world, (Size{ 1920, 1080 }));
}

TEST(VideoLayout, WorldLayerCoversTheCanvasWhenNotDivisible)
{
	// canvas 1281*3 = 3843 is not a multiple of 2: the world rounds up
	auto const l = ComputeLayerLayout({ { 1281, 721 }, 3 }, 2);
	EXPECT_TRUE(l.layered);
	EXPECT_EQ(l.canvas, (Size{ 3843, 2163 }));
	EXPECT_EQ(l.world, (Size{ 1922, 1082 }));
	EXPECT_GE(l.world.w * l.worldZoom, l.canvas.w);
	EXPECT_GE(l.world.h * l.worldZoom, l.canvas.h);
}

TEST(VideoLayout, ZoomIsClamped)
{
	auto const l = ComputeLayerLayout(Layout(3840, 2160, 3), 9);
	EXPECT_EQ(l.worldZoom, MAX_UI_SCALE);
}

TEST(VideoLayout, UiToWorldAndBack)
{
	// Su = 2, Zw = 1: a UI pixel is two world pixels
	EXPECT_EQ(UiToWorld(0, 2, 1), 0);
	EXPECT_EQ(UiToWorld(100, 2, 1), 200);
	EXPECT_EQ(WorldToUi(200, 2, 1), 100);
	EXPECT_EQ(WorldToUi(201, 2, 1), 100);
	EXPECT_EQ(WorldToUi(-1, 2, 1), -1);   // off the top/left stays off it
	EXPECT_EQ(WorldToUi(-2, 2, 1), -1);
	EXPECT_EQ(WorldToUi(-3, 2, 1), -2);
	// Su = 3, Zw = 2
	EXPECT_EQ(UiToWorld(10, 3, 2), 15);
	EXPECT_EQ(WorldToUi(15, 3, 2), 10);
	// zooming in beyond the UI: Su = 1, Zw = 2
	EXPECT_EQ(UiToWorld(10, 1, 2), 5);
	EXPECT_EQ(WorldToUi(5, 1, 2), 10);
	// a world position never maps to a UI pixel past the one it came from
	for (int su = 1; su <= 4; ++su)
		for (int zw = 1; zw <= 4; ++zw)
			for (int v = -20; v <= 200; ++v)
				EXPECT_LE(WorldToUi(UiToWorld(v, su, zw), su, zw), v) << su << " " << zw << " " << v;
	// both edges of the screen map onto each other
	auto const l = ComputeLayerLayout(Layout(2560, 1440, 2), 1);
	EXPECT_EQ(UiToWorld(l.ui.w, l.uiScale, l.worldZoom), l.world.w);
	EXPECT_EQ(WorldToUi(l.world.w, l.uiScale, l.worldZoom), l.ui.w);
}

TEST(VideoLayout, AutoScaleFollowsAWindowBeingResized)
{
	// What the runtime does when the user drags the window with an automatic UI scale: the canvas and the scale
	// are a pure function of the window size, so going there and back gives the same layout.
	auto const before = Layout(1920, 1080);
	EXPECT_EQ(Layout(2560, 1440), (DisplayLayout{ { 1280, 720 }, 2 }));
	EXPECT_EQ(Layout(1280, 720), (DisplayLayout{ { 1280, 720 }, 1 }));
	EXPECT_EQ(Layout(800, 600), (DisplayLayout{ { 800, 600 }, 1 }));
	EXPECT_EQ(Layout(1920, 1080), before);
	// a layered layout follows too: the world covers the window at its own scale
	auto const l = ComputeLayerLayout(Layout(2560, 1440), 1);
	EXPECT_TRUE(l.layered);
	EXPECT_EQ(l.world, (Size{ 2560, 1440 }));
	EXPECT_EQ(l.ui, (Size{ 1280, 720 }));
}

TEST(VSurface, ResizeKeepsTheObjectAndClearsIt)
{
	SGPVSurface s(64, 48, 16);
	s.Fill(0x1234);
	SGPVSurface* const address = &s;
	SDL_Surface* const resized = s.Resize(128, 96);
	EXPECT_EQ(address, &s);
	EXPECT_EQ(s.Width(), 128);
	EXPECT_EQ(s.Height(), 96);
	EXPECT_EQ(s.BPP(), 16);
	EXPECT_EQ(&s.GetSDLSurface(), resized);
	auto const* px = static_cast<uint16_t const*>(resized->pixels);
	EXPECT_EQ(px[0], 0);
	EXPECT_EQ(px[128 * 96 - 1], 0);
}
