#include "Animation.h"
#include "Clock.h"
#include "Headless.h"

#include <algorithm>

namespace sgp
{

bool AnimationFramesObserved() { return !IsHeadless(); }

void RunAnimation(std::chrono::milliseconds const duration, std::function<void(double)> const draw)
{
	bool const observed = AnimationFramesObserved();
	if (!Clock::IsVirtual() && !observed) { draw(1.0); return; } // no clock to tick, nobody watching

	/* The frame schedule of the legacy screen transitions: one frame per pass, in whole
	 * milliseconds of the game clock, until the percentage reaches 100. The clock the frames
	 * used to tick is the clock they still tick — presented or not — so an animation costs the
	 * same game time whether or not anyone watched it. */
	uint32_t const startMs = Clock::TicksMs();
	uint32_t const rangeMs = static_cast<uint32_t>(std::max<std::chrono::milliseconds::rep>(duration.count(), 1));
	unsigned pct = 0;
	do
	{
		uint32_t const elapsed = Clock::TicksMs() - startMs;
		pct = std::min<unsigned>(elapsed * 100 / rangeMs, 100);

		if (observed || pct >= 100)
		{
			uint64_t const presents = Clock::PresentCount();
			draw(pct / 100.0);
			// Drawing presents the frame, and a present ticks the clock (Clock::OnPresent). A
			// draw that did not present at all is ticked here, so the animation always lasts
			// exactly its duration.
			if (Clock::PresentCount() == presents) Clock::OnPresent();
		}
		else
		{
			// The frame nobody sees still ticks the clock exactly as its present would have, so
			// that skipping the pixels skips nothing else.
			Clock::OnPresent();
		}
	} while (pct < 100);
}

}
