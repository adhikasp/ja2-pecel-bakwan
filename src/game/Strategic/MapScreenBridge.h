#pragma once
// What the native strategic map screen (NativeUI/MapScreenNative.cc, docs/ui/mapscreen.md) needs from the legacy one.
// The native screen draws from game state and forwards the player's input to the legacy screen's own handlers (its
// mouse regions and hotkeys), so both UIs run exactly the same game code.

#include "JA2Types.h"
#include "Types.h"

struct MOUSE_REGION;

namespace MapBridge
{
	enum TeamColumn { TEAM_NAME, TEAM_ASSIGNMENT, TEAM_SLEEP, TEAM_LOCATION, TEAM_DESTINATION, TEAM_CONTRACT };
	/** A click (left or right) on a cell of line @a line of the legacy team list. */
	void TeamClick(int line, TeamColumn, bool right);
	/** The mouse is over that cell (legacy highlights, e.g. the destination line while plotting), or left it. */
	void TeamHover(int line, TeamColumn, bool gain);

	/** A click on the strategic map at @a sector (x, y 1..16 on the current level). */
	void SectorClick(SGPSector const& sector, bool right);
	/** The mouse is over @a sector (the temporary route while plotting follows it); nullptr: it left the map. */
	void SectorHover(SGPSector const* sector);

	/** A map screen hotkey, as if pressed: @a mods 0 none, 1 shift, 2 ctrl, 3 alt. */
	void Key(UINT32 key, int mods);

	/** The merc inventory panel's slot @a invPos (InvSlotPos), and the sector inventory's item @a index. */
	void InventorySlotClick(int invPos, bool right);
	void PoolItemClick(int index, bool right);

	/** A legacy mouse region under the legacy point (x, y) (a line of a popup box), clicked. */
	void ClickAt(int x, int y, bool right);
	void HoverAt(int x, int y);

	/** The merc inventory panel's "done" (close) and the sector inventory's done. */
	void CloseMercInventory();
	/** Opens or closes the sector inventory (legacy Ctrl+I, the town box "Inventory" button). */
	void ToggleSectorInventory();
	/** The map UI message ("Click again on the destination..."), dismissed like a click on the map does. */
	void CancelMessage();
	/** Sector inventory: like items stacked together, money in one pile (new; the legacy screen has no button). */
	void StackAndMerge();
}

#include <vector>
/** The mercs shown in the update box and the reason (UpdateBoxReason) (Map_Screen_Interface.cc). */
std::vector<SOLDIERTYPE*> UpdateBoxSoldiers(int* reason);
