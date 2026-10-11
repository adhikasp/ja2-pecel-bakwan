#pragma once
// The native world overlays' adapter (issue #322): reads what the legacy UI decided is on the world (the item-flash
// slots, the locators, the burst spread, the up/down arrows, the rubber band, the pool under the cursor, the pause)
// and hands it to the OverlayModel core as one Frame. The native view (NativeUI/TacticalOverlays.cc) and Lua
// (ja2.overlays) read the frame.

#include "JA2Types.h"
#include "OverlayModel.h"

#define SHOW_LOCATOR_NORMAL			1
#define SHOW_LOCATOR_FAST			2

/** Starts a soldier's locator ring: the pulse the overlay layer draws over him (a few cycles, more when normal). */
void ShowRadioLocator(SOLDIERTYPE* s, UINT8 ubLocatorSpeed);
/** Ends it. */
void EndRadioLocator(SOLDIERTYPE* s);

/** Builds the frame for this game frame from the legacy globals. Called by RenderTopmostTacticalInterface when the
 * native HUD is up (it does the legacy draws otherwise). */
void UpdateOverlayFrame();

/** Nothing is over the world (the native HUD is not up, the screen is not the tactical one). */
void ClearOverlayFrame();

OverlayModel::Frame const& CurrentOverlayFrame();

/** Test aid (ja2.debug("arrows")): the arrows show these ARROWS_* flags whatever the UI says; -1 stops it. */
void ForceOverlayArrows(int flags);
