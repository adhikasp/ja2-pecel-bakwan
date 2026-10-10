#pragma once
// Drag and drop in the tactical HUD (issue #320, docs/plan/native-tactical.md "InventoryCore" and "CursorModel"): the
// gesture (when a press becomes a drag) and the verdict for letting go of the hand over a merc's card. No game state:
// the adapter (Tactical/DragAdapter.cc) fills the plain inputs from the live soldiers and carries the verdict out, the
// HUD draws the chip and the outlines from it. DragDrop_unittest.cc asserts every rule against hand-made values.
//
// Dropping on a pocket is InventoryCore::PlanPlace, on the world it is the cursor's drop / give / throw (Handle_UI);
// this file adds the card, which is where "give" and "the merc himself: at his feet" are decided.

#include "InventoryCore.h"

#include <cmath>
#include <cstdint>

namespace Equipment
{

/** How many tiles away a merc can still be handed something (PASSING_ITEM_DISTANCE_OKLIFE). */
constexpr int GIVE_RANGE_TILES = 3;

/** A press becomes a drag after this much movement, in dp: a click stays a click. */
constexpr float DRAG_THRESHOLD_DP = 6.0f;

/** The press-move-release of one drag. Positions are output pixels, the threshold is in the same unit. */
class DragGesture
{
public:
	/** The left button went down on @a slot (a pocket of the shown merc, or an attachment position). */
	void Press(float x, float y, int slot)
	{
		pressed_ = true;
		dragging_ = false;
		x_ = x; y_ = y;
		slot_ = slot;
	}

	/** The pointer moved with the button held. True exactly once, on the move that crosses the threshold. */
	bool Move(float x, float y, float threshold)
	{
		if (!pressed_ || dragging_) return false;
		float const dx = x - x_, dy = y - y_;
		if (std::sqrt(dx * dx + dy * dy) < threshold) return false;
		dragging_ = true;
		return true;
	}

	/** The button went up. Returns whether it ended a drag (as opposed to a click). */
	bool Release()
	{
		bool const was = dragging_;
		pressed_ = dragging_ = false;
		return was;
	}

	void Cancel() { pressed_ = dragging_ = false; }

	bool Pressed() const  { return pressed_; }
	bool Dragging() const { return dragging_; }
	int  Slot() const     { return slot_; }

private:
	bool  pressed_ = false, dragging_ = false;
	float x_ = 0, y_ = 0;
	int   slot_ = -1;
};

enum class DropAction : uint8_t
{
	None,
	Give,        // hand it to the other merc
	DropAtFeet,  // the merc himself: it goes on the ground where he stands
};

/** Everything a drop on a merc's card depends on, as plain values. */
struct CardDrop
{
	InvParty giver;                // the merc whose pocket the item came out of
	InvParty receiver;             // the merc whose card the item is over
	bool     sameMerc = false;     // the card is the giver's own
	int      tiles = 0;            // distance between the two, in tiles
	bool     inSight = true;       // they can see each other
	bool     receiverTakes = true; // a merc of the team that can carry things (not a vehicle, not an escort)
	int      dropAp = 0;           // what putting something down at his feet costs (AP_PICKUP_ITEM)
	bool     dropFree = false;     // the item came off the ground: picking it up already paid
};

struct DropVerdict
{
	DropAction action = DropAction::None;
	InvWhy     why    = InvWhy::None;
	int        apGiver = 0;      // charged to the giver
	int        apReceiver = 0;   // charged to the receiver
	int        tiles = 0;        // the distance the chip shows

	bool Ok() const { return action != DropAction::None; }
};

/** Letting go of the hand over a card: give (range, sight, both mercs' points) or, on his own card, drop at his feet. */
DropVerdict PlanCardDrop(CardDrop const& d);

}
