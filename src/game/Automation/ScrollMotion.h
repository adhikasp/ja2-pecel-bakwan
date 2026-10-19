#pragma once
// The bounded "the native UI is still scrolling" latch behind NothingInFlight() (AutomationSession.cc).
//
// RmlUi animates a scroll over several frames (its smooth-scroll controller), and while it runs the geometry the
// element list reports and what is drawn disagree - a click is dropped, because RmlUi dispatches a click only when
// the element under the mouse is the same on press and release. The session cannot ask the controller directly (it
// is private to RmlUi), so it watches the one thing a scroll changes - the scroll offsets of the native documents
// (NativeUI::ScrollSignature) - and calls a scroll in flight once it has moved the page on two frames in a row: a
// smooth scroll moves every frame it runs, where an *instant* scroll (a scrollbar thumb drag, Tab's
// ScrollIntoView) moves the page once and is already where the gesture left it.
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

	/** Feed this frame's scroll signature and return true while a scroll is in flight.
	 *
	 *  A smooth scroll moves the page on every frame it is in flight, so a *second* frame that moved is what
	 *  tells it apart from an instant scroll (a scrollbar thumb drag, Tab's ScrollIntoView): that one moves the
	 *  page once and is already where the gesture left it, and waiting for it would cost every wait a frame. */
	bool Sample(uint64_t signature);

  private:
	uint64_t last = 0;
	bool have = false;
	bool gaveUp = false;
	bool movedOnce = false;
	unsigned frames = 0;
};

inline bool ScrollMotion::Sample(uint64_t const signature)
{
	bool const changed = have && signature != last;
	have = true;
	last = signature;
	if (!changed)
	{
		// The page has stopped: a later change is a new scroll.
		gaveUp = false;
		movedOnce = false;
		frames = 0;
		return false;
	}
	if (!movedOnce)
	{
		// The first frame that moved. An instant scroll stops here; a smooth one is back next frame.
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
