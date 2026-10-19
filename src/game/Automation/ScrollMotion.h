#pragma once
// The bounded "the native UI is still scrolling" latch behind NothingInFlight() (AutomationSession.cc).
//
// RmlUi animates a scroll over several frames (its smooth-scroll controller), and while it runs the geometry the
// element list reports and what is drawn disagree - a click is dropped, because RmlUi dispatches a click only when
// the element under the mouse is the same on press and release. The session cannot ask the controller directly (it
// is private to RmlUi), so it asks the two things the controller does while it runs: it moves the page (the scroll
// offsets of the native documents change, NativeUI::ScrollSignature) and it asks for the next frame
// (NativeUI::WantsNextFrame). A scroll that does both is in flight; an *instant* scroll - a scrollbar thumb drag,
// Tab's ScrollIntoView - moves the page once and asks for nothing, and is already where the gesture left it.
//
// RmlUi's smooth-scroll controller always reaches its target, so the latch would normally clear itself. It is
// bounded anyway: a scroll that somehow never settles - a controller re-targeted every frame, RmlUi's middle-button
// autoscroll held open - must not hold ja2.waitIdle() open forever. When the bound is reached the latch gives up
// and reports settled until the page stops moving, which re-arms it.

#include <cstdint>

namespace Automation
{
class ScrollMotion
{
  public:
	/** Frames of continuous scroll a wait will sit through before giving up.
	 *
	 *  Measured at 1280x720 on the native options screen (the tallest native document): a scrollbar page
	 *  up/down settles in ~12 frames, a thumb drag and Tab's ScrollIntoView scroll instantly, and a wheel always
	 *  settles inside ja2.wheel itself. The only scroll that outlasts this bound is a held one - the
	 *  middle-button autoscroll covers that page in ~320 frames - so 120 is an order of magnitude above any
	 *  one-shot scroll and is reached only by a scroll that is not going to stop. */
	static constexpr unsigned MAX_FRAMES = 120;

	/** Feed this frame's observation and return true while a scroll is in flight. */
	bool Sample(bool animating, uint64_t signature);

  private:
	uint64_t last = 0;
	bool have = false;
	bool gaveUp = false;
	unsigned frames = 0;
};

inline bool ScrollMotion::Sample(bool const animating, uint64_t const signature)
{
	if (!animating)
	{
		// RmlUi is not animating a scroll: whatever the offsets are, nothing is in flight.
		have = true;
		last = signature;
		gaveUp = false;
		frames = 0;
		return false;
	}
	bool const changed = have && signature != last;
	have = true;
	last = signature;
	if (!changed)
	{
		// Animating, but the page did not move this frame.
		gaveUp = false;
		frames = 0;
		return false;
	}
	if (gaveUp) return false; // this motion is already over as far as a wait is concerned
	if (frames < MAX_FRAMES)
	{
		++frames;
		return true;
	}
	gaveUp = true;
	return false;
}
}
