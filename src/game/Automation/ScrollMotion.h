#pragma once
// The bounded "the native UI is still scrolling" latch behind NothingInFlight() (AutomationSession.cc).
//
// RmlUi animates a scroll over several frames (its smooth-scroll controller), and while it runs the geometry the
// element list reports and what is drawn disagree - a click is dropped, because RmlUi dispatches a click only when
// the element under the mouse is the same on press and release. The session cannot ask the controller directly (it
// is private to RmlUi), so it watches the two things a smooth scroll does while it runs - it moves the page (the
// scroll offsets of the native documents, NativeUI::ScrollSignature) and it asks for the next frame
// (NativeUI::WantsNextFrame) - and calls a scroll in flight once both have held for two frames in a row. The two
// ways the offsets move without a scroll in flight are excluded: an *instant* scroll (a scrollbar thumb drag, Tab's
// ScrollIntoView) moves the page once and is already where the gesture left it, and a list that grows moves them as
// the layout clamps them, with RmlUi animating nothing.
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

	/** Feed this frame's observation and return true while a scroll is in flight.
	 *
	 *  @a animating is whether RmlUi is animating something (its controller asked for the next frame) and
	 *  @a signature is the scroll offsets. A smooth scroll is the only thing that does both *and* keeps doing it:
	 *  it moves the page on every frame it runs, so a second frame that moved is what tells it apart from the two
	 *  ways the offsets change without a scroll in flight - an instant scroll (a scrollbar thumb drag, Tab's
	 *  ScrollIntoView) moves the page once and is already where the gesture left it, and a list that grows moves
	 *  them as the layout clamps them, with RmlUi animating nothing. */
	bool Sample(bool animating, uint64_t signature);

  private:
	uint64_t last = 0;
	bool have = false;
	bool gaveUp = false;
	bool movedOnce = false;
	unsigned frames = 0;
};

inline bool ScrollMotion::Sample(bool const animating, uint64_t const signature)
{
	bool const changed = have && signature != last;
	have = true;
	last = signature;
	if (!changed || !animating)
	{
		// Nothing moved, or RmlUi is not animating a scroll.
		gaveUp = false;
		movedOnce = false;
		frames = 0;
		return false;
	}
	if (!movedOnce)
	{
		// The first frame that moved: an instant scroll stops here, a smooth one is back next frame.
		movedOnce = true;
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
