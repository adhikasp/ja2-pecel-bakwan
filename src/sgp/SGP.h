#ifndef __SGP_
#define __SGP_

#include <SDL3/SDL_events.h>

/** Request game exit.
 * Call this function if you want to exit the game. */
void requestGameExit();

/** Handler for set_terminate */
void TerminationHandler();

namespace sgp
{
	/** Run one iteration of the engine: system events, clocks, GameLoop().
	 * This is what MainLoop() does between events; the automation driver calls
	 * it to advance the game one frame at a time. Returns false once the game
	 * has asked to quit. */
	bool StepFrame();

	/** Deliver a synthetic input event exactly like a real one. Mouse
	 * coordinates are in logical (game) pixels. */
	void DispatchInputEvent(SDL_Event const&);

	bool QuitRequested();
}
#endif
