#pragma once

#include "AutomationLua.h"

/** @file
 * The Lua door onto the tactical world overlays (issue #322, docs/plan/native-tactical.md "OverlayModel").
 * The rules are in `NativeUI/OverlayModel.h`, the adapter in `Tactical/OverlayAdapter.cc`; this reads the frame the
 * adapter last built as data, so what is over the world is asserted without a screenshot.
 *
 *   ja2.overlays()  { paused,
 *                     locators = { { kind = "merc"|"place"|"item", tone = "friend"|"foe"|"place", frame, gridNo,
 *                                    x, y }... },
 *                     bursts   = { { gridNo, x, y }... },
 *                     arrows   = { { dir = "up"|"down", climb, tones = { "plain"|"yellow"|"green"... } }... },
 *                     band     = { l, t, r, b } | nil,
 *                     pools    = { { pointer, gridNo, hidden, items = { { item, name, count }... } }... },
 *                     speech   = { { text, x, y }... } }
 *                   x and y are world pixels (the world renderer's coordinates); band is in canvas pixels.
 */
namespace Automation
{
	sol::table OverlayState(sol::state& L);
}
