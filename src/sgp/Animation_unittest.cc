#include "Animation.h"
#include "Clock.h"
#include "Headless.h"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace
{
	/** Virtual time with a frame quantum, restored to the wall clock afterwards. */
	class VirtualTime : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			sgp::Clock::EnableVirtual(QUANTUM);
		}

		void TearDown() override
		{
			sgp::Clock::DisableVirtual();
			sgp::SetHeadless(false);
		}

		static constexpr std::chrono::milliseconds QUANTUM = 16ms;
		static constexpr std::chrono::milliseconds DURATION = 200ms;

		/** The draw of an observed frame: renders (recorded) and presents, like a real draw
		 * ends in RefreshScreen(), whose present ticks the clock. */
		static std::function<void(double)> RecordingDraw(std::vector<double>& frames)
		{
			return [&frames](double t) { frames.push_back(t); sgp::Clock::OnPresent(); };
		}
	};
}

TEST_F(VirtualTime, ObservedAnimationDrawsFramesToEndAtOne)
{
	sgp::SetHeadless(false);
	std::vector<double> frames;
	auto const start = sgp::Clock::TicksMs();
	sgp::RunAnimation(DURATION, RecordingDraw(frames));

	ASSERT_GT(frames.size(), 1u);
	EXPECT_EQ(frames.back(), 1.0);
	for (size_t i = 1; i < frames.size(); ++i) EXPECT_LT(frames[i - 1], frames[i]);
	// One frame quantum per presented frame: the animation lasts its duration of game time.
	EXPECT_GE(sgp::Clock::TicksMs() - start, DURATION.count());
	EXPECT_LT(sgp::Clock::TicksMs() - start, DURATION.count() + 2 * QUANTUM.count());
}

TEST_F(VirtualTime, UnobservedAnimationDrawsOnlyTheEndState)
{
	sgp::SetHeadless(true);
	std::vector<double> frames;
	sgp::RunAnimation(DURATION, RecordingDraw(frames));

	ASSERT_EQ(frames.size(), 1u);
	EXPECT_EQ(frames.front(), 1.0);
}

TEST_F(VirtualTime, SkippedFramesTickExactlyAsPresentedOnes)
{
	// The contract that keeps goldens and determinism valid across the skip: the run must cost
	// the same virtual time whether or not anyone watched the frames.
	std::vector<double> observed;
	sgp::SetHeadless(false);
	auto const observedStart = sgp::Clock::TicksMs();
	sgp::RunAnimation(DURATION, RecordingDraw(observed));
	uint32_t const observedMs = sgp::Clock::TicksMs() - observedStart;

	std::vector<double> skipped;
	sgp::SetHeadless(true);
	auto const skippedStart = sgp::Clock::TicksMs();
	sgp::RunAnimation(DURATION, RecordingDraw(skipped));
	uint32_t const skippedMs = sgp::Clock::TicksMs() - skippedStart;

	EXPECT_GT(observed.size(), 2u);   // every frame of the animation but the end state
	EXPECT_EQ(skipped.size(), 1u);    // ...was skipped in the headless run
	EXPECT_EQ(observedMs, skippedMs);
}

TEST_F(VirtualTime, ADrawThatDoesNotPresentStillLastsTheDuration)
{
	sgp::SetHeadless(false);
	int draws = 0;
	auto const start = sgp::Clock::TicksMs();
	sgp::RunAnimation(DURATION, [&draws](double) { ++draws; });

	EXPECT_GT(draws, 1);
	EXPECT_GE(sgp::Clock::TicksMs() - start, DURATION.count());
	EXPECT_LT(sgp::Clock::TicksMs() - start, DURATION.count() + 2 * QUANTUM.count());
}

TEST_F(VirtualTime, ZeroDurationDrawsOneFrame)
{
	sgp::SetHeadless(true);
	std::vector<double> frames;
	sgp::RunAnimation(0ms, RecordingDraw(frames));

	ASSERT_EQ(frames.size(), 1u);
	EXPECT_EQ(frames.front(), 1.0);
}

TEST(AnimationWallClock, UnobservedAnimationDrawsOnlyTheEndState)
{
	sgp::SetHeadless(true);
	std::vector<double> frames;
	sgp::RunAnimation(10ms, [&frames](double t) { frames.push_back(t); });

	ASSERT_EQ(frames.size(), 1u);
	EXPECT_EQ(frames.front(), 1.0);
	sgp::SetHeadless(false);
}
