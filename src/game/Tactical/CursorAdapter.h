#pragma once
// The native tactical cursor's adapter (issue #318): reads what the legacy UI decided (the UICursorID, the action
// points, the texts it wrote beside the cursor, the plotted path, the tile the merc walks to) and hands it to
// the CursorModel core. The native view (NativeUI/TacticalCursor.cc) and Lua (ja2.cursor) read the frame.

#include "CursorModel.h"
#include "Interface_Cursors.h"
#include "Isometric_Utils.h"
#include "JA2Types.h"

#include <vector>

/** A point in world pixels: where the world renderer draws it (before the world-to-output mapping). */
struct CursorPoint { float x, y; };

struct CursorFrame
{
	CursorModel::State    state;
	CursorModel::PathPlan plan;
	int                   uiCursor = NO_UICURSOR;
	GridNo                cursorTile = NOWHERE;      // the tile under the pointer
	bool                  inWorld = false;           // the pointer is in the world's mouse region
	bool                  overHud = false;           // ... and the native HUD (or a native modal) has it
	bool                  marker = false;            // the tile marker at markerAt
	CursorPoint           markerAt{};
	bool                  dest = false;              // the tile the merc walks to for the action (the legacy move grid)
	bool                  destAttack = false;        // ... and it is for an attack
	CursorPoint           destAt{};
	std::vector<CursorPoint> path;                   // tile centres: the merc's tile, then each step, destination last
};

/** The ground point of a tile's centre, in world pixels (the place a merc standing there has his feet). */
CursorPoint GridNoToWorldPixels(GridNo, INT8 level);

/** Builds the frame for this game frame from the legacy globals. Called by DrawUICursor when the native HUD is up. */
void UpdateCursorFrame(UICursorID uiCursor, int heldItem);

/** The cursor is over nothing (the HUD took it, the screen is not the tactical one). */
void ClearCursorFrame();

CursorFrame const& CurrentCursorFrame();
