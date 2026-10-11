#pragma once
// The pointer over the tactical world (issue #319, docs/plan/native-tactical.md "WorldPointer"): the adapter of
// NativeUI/PointerRouter.h. It replaces the legacy viewport mouse region: whether the pointer is over the world, which
// buttons are down, where in world pixels it is, and the move / wheel / touch events the world reacts to.
// Handle_UI.cc, Turn_Based_Input.cc and Real_Time_Input.cc read it instead of gViewportRegion.

#include "Input.h"
#include "JA2Types.h"
#include "PointerRouter.h"
#include "UILayout.h"

namespace WorldPointer
{
	/** Once per frame, after the pointer moved and before the events are dispatched: who owns the pointer now. */
	void Frame();

	/** A mouse or touch event the world may react to (the game loop calls it for events the HUD did not take). */
	void Event(InputAtom const& e);

	/** The pointer is over the world: not over a HUD element or a legacy region on top of it, inside the view. */
	bool In();

	/** The buttons down while the pointer is over the world: MSYS_LEFT_BUTTON | MSYS_RIGHT_BUTTON | MSYS_MIDDLE_BUTTON. */
	UINT32 Buttons();

	/** The world's rectangle on the screen, in canvas pixels. */
	INT16 ViewportRight();
	INT16 ViewportBottom();

	/** The pointer in world pixels, exact to the output pixel (the touch UI's manual cursor when it is set). */
	LayerPoint World();

	/** Which screen edges the pointer is within the edge strip of (2 output pixels): the view scrolls and the sector
	 * exit arrows show there, whatever the HUD has under it. */
	PointerRouter::Edges Edges();

	/** The pointer left the tactical screen. */
	void Reset();
}
