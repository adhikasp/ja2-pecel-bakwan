#pragma once

#include "AutomationLua.h"

/** @file
 * The Lua door onto the tactical cursor (issue #318, docs/plan/native-tactical.md "CursorModel").
 * The rules are in `NativeUI/CursorModel.h`, the adapter in `Tactical/CursorAdapter.cc`; this reads the frame the
 * adapter last built as data, so what the pointer means over a tile is asserted without a screenshot.
 *
 *   ja2.cursor()   { shown, mode, shape, tone, marker, ap, apLeft, hit, aim, why, target, tile, id, chip,
 *                    lines = { { kind, a, b, text }... },
 *                    path = { steps, solid, total, now, next, beyond } | nil,
 *                    markerAt = { x, y } | nil, destAt = { x, y, attack } | nil }
 *                  markerAt and destAt are world pixels (the world renderer's coordinates).
 */
namespace Automation
{
	sol::table CursorState(sol::state& L);
}
