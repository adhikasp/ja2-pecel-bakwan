#pragma once
// The bounded "the native UI is still scrolling" latch behind NothingInFlight() (AutomationSession.cc).
//
// RmlUi animates a scroll over several frames (its smooth-scroll controller), and while it runs the geometry the
// element list reports and what is drawn disagree - a click is dropped, because RmlUi dispatches a click only when
// the element under the mouse is the same on press and release. The session cannot ask the controller directly (it
// is private to RmlUi), so it watches the one thing a scroll changes, the scroll offsets of the native documents
// (NativeUI::ScrollSignature), and calls a scroll in flight while that signature keeps moving.
//
// RmlUi's smooth-scroll controller always reaches its target, so the latch would normally clear itself. It is
// bounded anyway: a scroll that somehow never settles - a controller re-targeted every frame, RmlUi's middle-button
// autoscroll held open - must not hold ja2.waitIdle() open forever. When the bound is reached the latch gives up
// and reports settled until the signature is stable again, which re-arms it.

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

		/** Feed this frame's scroll signature (@a signature is 0 when the native UI is not running); returns true
		 *  while a scroll is still in flight. */
		bool Sample(uint64_t signature);

	private:
		uint64_t last = 0;
		bool     have = false;
		bool     gaveUp = false;
		unsigned frames = 0;
	};

	inline bool ScrollMotion::Sample(uint64_t const signature)
	{
		if (!have) { have = true; last = signature; return false; }
		if (signature == last)
		{
			// The page has stopped: a later change is a new scroll.
			gaveUp = false;
			frames = 0;
			return false;
		}
		last = signature;
		if (gaveUp) return false; // this motion is already over as far as a wait is concerned
		if (frames < MAX_FRAMES) { ++frames; return true; }
		gaveUp = true;
		return false;
	}
}
