#pragma once

#include "Lbe.h"
#include "Slots.h"

#include <stdint.h>

struct ItemModel;

namespace Equipment
{

// Everything the pocket-fit rules need to know about one item type. The rules
// run on these plain values; TraitsOf() fills them from the item definition.
struct ItemTraits
{
	bool     known      = true; // false: the id matches no item type
	ItemSize size        = ItemSize::Medium;
	bool     isMagazine  = false;
	bool     isLbe       = false;
	uint16_t stackLimit  = 1; // how many stack in one pocket
	Platform host        = {}; // typed slots this item offers as an attachment host
};

enum class FitReject : uint8_t
{
	None,
	NoNesting,  // load-bearing gear holds items, never more load-bearing gear
	WrongKind,  // a magazine pocket takes magazines and nothing else
	TooBig,     // the item is larger than the pocket takes
	StackFull,  // the pocket already holds as many of these as it can
};

struct FitResult
{
	bool      ok;
	FitReject reason;
};

// Can `item` go into a `pocket` that already holds `inPocket` items? Pure.
// Nesting depth one is structural here: LBE items never fit any pocket.
FitResult CanFit(PocketKind pocket, const ItemTraits& item, uint16_t inPocket);

// The same rule over a live item definition.
FitResult CanFit(PocketKind pocket, const ItemModel& item, uint16_t inPocket);

// How much room an item takes: weapons, armour and big gear are large,
// magazines and small kit are small, everything else falls to its weight.
ItemSize SizeOf(const ItemModel& item);
ItemTraits TraitsOf(const ItemModel& item);

const char* Describe(FitReject reason);

}
