#pragma once

#include "Lbe.h"
#include "PocketRules.h"
#include "Slots.h"

#include <functional>
#include <stdint.h>
#include <vector>

namespace Equipment
{

// A loadout as plain ids: the weapon platform with its typed attachments, the
// worn load-bearing gear, and what each of its pockets carries. This is the
// headless build surface - the e2e loadout fixtures (#263) drive it.
struct LoadoutEntry
{
	uint16_t itemId = 0;
	uint8_t  count  = 1;
};

struct LoadoutSpec
{
	uint16_t      weapon = 0;
	uint16_t      weaponAttachments[MAX_HOST_SLOTS] = {}; // by weapon slot index
	uint16_t      lbe[NUM_LBE_SLOTS] = {};                // vest, belt, pack
	LoadoutEntry pockets[NUM_LBE_SLOTS][MAX_LBE_POCKETS] = {}; // window contents
};

enum class LoadoutReject : uint8_t
{
	None,
	UnknownItem,
	NotAWeapon,
	BadAttachment,
	BadLbe,
	BadPocketItem,
};

struct LoadoutProblem
{
	LoadoutReject kind;
	int           index;  // weapon slot or LBE slot, depending on kind
	int           pocket; // pocket index, -1 when not a pocket problem
	const char*  text;
};

// Fills ItemTraits for one item id; returns known = false for unknown ids.
using TraitsLookup = std::function<ItemTraits(uint16_t)>;

struct LoadoutReport
{
	bool                     ok;
	std::vector<LoadoutProblem> problems;
};

// Validate a loadout against the slot, attachment and pocket rules. Pure over
// the lookup: tests pass a table, the game passes the item registry.
LoadoutReport ValidateLoadout(const LoadoutSpec& spec, const TraitsLookup& lookup,
		const SlotPolicy& policy = SlotPolicy());

const char* Describe(LoadoutReject reason);

}
